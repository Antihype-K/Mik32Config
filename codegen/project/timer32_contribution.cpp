#include "timer32_contribution.h"
#include "contribution_utils.h"
#include <iomanip>
#include <map>
#include <sstream>
#include <vector>

namespace
{
    // mik32_hal_timer32.h, HAL_TIMER32_SourceTypeDef. known_frequency=true
    // только для вариантов, совпадающих с sys_clk=32МГц при дефолтной
    // конфигурации тактирования (AHBDivider=0, ТЗ §3.1.8) — тот же приём,
    // что у Timer16 (codegen/project/timer16_contribution.cpp, kClockSourceInfo).
    struct ClockSourceInfo { const char* hal_enum; bool known_frequency; };
    const std::map<std::string, ClockSourceInfo> kClockSourceInfo = {
        {"prescaler",    {"TIMER32_SOURCE_PRESCALER", true}},
        {"tim1_sys_clk", {"TIMER32_SOURCE_TIM1_SYS_CLK", true}},
        {"tim1_hclk",    {"TIMER32_SOURCE_TIM1_HCLK", true}},
        {"tim2_osc32k",  {"TIMER32_SOURCE_TIM2_OSC32K", false}},
        {"tim2_lsi32k",  {"TIMER32_SOURCE_TIM2_LSI32K", false}},
        {"tx_pad",       {"TIMER32_SOURCE_TX_PAD", false}},
    };

    // Errata MIK32V2 v1.0.1, §9.1 «Сброс флагов» — текст цитаты общий для
    // предупреждения (ТЗ §3.1.5 п.3) и для шаблона обработчика прерывания.
    // Полный обход реализован ниже (buildErrata91IrqTemplate) по буквальному
    // примеру из errata (mik32v2errataV1_0_1_1 v1.0.1, §9.1, листинг
    // "Отключение прерывания по захвату и повторное включение в основном
    // цикле после того как флаг был очищен"): в обработчике — отключить
    // маску сработавшего источника (HAL_Timer32_InterruptMask_Clear) и
    // сбросить только его флаг (HAL_Timer32_InterruptFlags_ClearMask), а не
    // общий HAL_Timer32_InterruptFlags_Clear (он бы сбросил вообще все
    // флаги разом, включая ещё не обработанные); повторное включение —
    // отдельным шаблоном-функцией для вызова из основного цикла
    // пользователя, строго когда флаг этого источника реально читается
    // нулём (errata читает статус ДО очистки — HAL_Timer32_InterruptFlags_Get).
    constexpr const char* kErrata91Comment =
        " * Errata п. 9.1 (mik32v2errataV1_0_1_1 v1.0.1): сброс флагов IC_INT_CHx/\n"
        " * OC_INT_CHx/UDF_INT/OVF_INT происходит в конце счёта таймера, во время\n"
        " * которого была запись в INT_CLEAR — возможен повторный вход в обработчик\n"
        " * прерывания при низкой частоте счёта. Обход (реализован ниже, по примеру\n"
        " * из errata): обработчик отключает маску ТОЛЬКО сработавшего источника и\n"
        " * сбрасывает только его флаг; повторно источник включает отдельная функция\n"
        " * TIMER32_<N>_ReenableAfterErrata91_Template — вызовите её из своего\n"
        " * основного цикла, она включает источник обратно, только когда его флаг\n"
        " * реально читается нулём (т.е. сброс уже гарантированно отработал).\n";

    std::string channelHandleName(const std::string& base_handle_name, int channel_index)
    {
        return base_handle_name + "_ch" + std::to_string(channel_index);
    }

    // "TIMER32_1_IRQHandler_Template" — не настоящий обработчик вектора
    // прерываний (см. knowledge_base/timer32.json, not_covered_by_this_codegen
    // -> interrupt_wiring): подключение к raw_trap_handler/EPIC оставлено
    // пользователю, шаблон — только тело с обязательным по ТЗ сбросом флага.
    std::string irqTemplateFunctionName(int instance)
    {
        return "TIMER32_" + std::to_string(instance) + "_IRQHandler_Template";
    }

    std::string reenableTemplateFunctionName(int instance)
    {
        return "TIMER32_" + std::to_string(instance) + "_ReenableAfterErrata91_Template";
    }

    // Один независимый источник прерывания Timer32: выражение маски
    // (TIMER32_INT_OC_M(TIMER32_CHANNEL_0), TIMER32_INT_OVERFLOW_M, ...) и
    // короткая подпись для комментария. overflow у PWM-канала и у
    // overflow_interrupt — один и тот же бит (HAL_Timer32_PWM_Start_IT
    // включает именно TIMER32_INT_OVERFLOW_M, mik32_hal_timer32.c:404) —
    // вызывающий код обязан не продублировать его в списке дважды.
    struct InterruptSource { std::string mask_expr; std::string label; };

    // Errata §9.1, полный обход: обработчик отключает маску и сбрасывает
    // ТОЛЬКО флаг сработавшего источника (не все разом — иначе ещё не
    // обработанный параллельный источник потерял бы свой флаг раньше
    // времени), плюс отдельный шаблон для повторного включения из
    // основного цикла, строго когда флаг этого источника реально нулевой.
    std::string buildErrata91IrqTemplate(const std::string& handle_name, int instance,
                                          const std::vector<InterruptSource>& sources)
    {
        std::ostringstream irq_template;
        irq_template << "/*\n"
                     << kErrata91Comment
                     << " *\n"
                        " * Шаблон НЕ подключён к вектору прерываний RISC-V (raw_trap_handler,\n"
                        " * HAL_EPIC_MaskLevelSet, HAL_IRQ_EnableInterrupts) — это забота\n"
                        " * пользователя, см. docs/implementations/timer32.md. Вызвать эту функцию\n"
                        " * нужно из своего raw_trap_handler после проверки EPIC_CHECK_TIMER32_"
                     << instance << "().\n"
                        " */\n"
                        "void " << irqTemplateFunctionName(instance) << "(void)\n"
                        "{\n"
                        "    const uint32_t flags = HAL_Timer32_InterruptFlags_Get(&" << handle_name << ");\n"
                        "\n";
        for (const InterruptSource& source : sources)
        {
            irq_template << "    if (flags & " << source.mask_expr << ")\n"
                            "    {\n"
                            "        /* " << source.label << ": отключить маску ДО сброса флага — буквально\n"
                            "           по примеру errata §9.1, иначе возможен повторный вход. */\n"
                            "        HAL_Timer32_InterruptMask_Clear(&" << handle_name << ", " << source.mask_expr << ");\n"
                            "        HAL_Timer32_InterruptFlags_ClearMask(&" << handle_name << ", " << source.mask_expr << ");\n"
                            "\n"
                            "        /* USER TODO: обработать " << source.label << " здесь. */\n"
                            "    }\n";
        }
        irq_template << "}\n"
                        "\n"
                        "/*\n"
                        " * Errata п. 9.1 — повторное включение из ОСНОВНОГО ЦИКЛА пользователя\n"
                        " * (буквально по тексту errata), а не из обработчика: источник включается\n"
                        " * обратно, только когда его собственный флаг реально читается нулём —\n"
                        " * то есть сброс из " << irqTemplateFunctionName(instance) << "() уже\n"
                        " * гарантированно отработал на аппаратном уровне.\n"
                        " */\n"
                        "void " << reenableTemplateFunctionName(instance) << "(void)\n"
                        "{\n"
                        "    const uint32_t flags = HAL_Timer32_InterruptFlags_Get(&" << handle_name << ");\n"
                        "\n";
        for (const InterruptSource& source : sources)
        {
            irq_template << "    if ((flags & " << source.mask_expr << ") == 0)\n"
                            "    {\n"
                            "        HAL_Timer32_InterruptMask_Set(&" << handle_name << ", " << source.mask_expr << ");\n"
                            "    }\n";
        }
        irq_template << "}\n";
        return irq_template.str();
    }
}

ParseResult<PeripheralContribution> buildTimer32Contribution(
    const Timer32Config& config,
    const Timer32Knowledge& knowledge)
{
    const std::string* timer_handle = findOrNull(knowledge.instance_handle, config.instance);
    const std::string* count_mode_enum = findOrNull(knowledge.count_mode_hal_enum, config.count_mode);
    const bool* has_channels = findOrNull(knowledge.instance_has_channels, config.instance);

    if (!timer_handle || !count_mode_enum || !has_channels)
    {
        return ParseResult<PeripheralContribution>{
            false,
            "Timer32Config ссылается на instance/count_mode, отсутствующий в knowledge_base/timer32.json",
            std::nullopt};
    }

    if (!config.channels.empty() && !*has_channels)
    {
        // Дублирует проверку parser/timer32/timer32_parser.cpp — второй
        // независимый барьер на случай прямого вызова этой функции с
        // Timer32Config, собранным в обход парсера (например, в тестах).
        return ParseResult<PeripheralContribution>{
            false,
            "Timer32 instance " + std::to_string(config.instance) + " не имеет каналов (Timer32_0 — базовый счётчик)",
            std::nullopt};
    }

    const ClockSourceInfo* clock_source_info = findOrNull(kClockSourceInfo, config.clock_source);
    if (!clock_source_info)
    {
        return ParseResult<PeripheralContribution>{
            false, "Timer32Config.clock_source содержит значение, отсутствующее в HAL-таблице кодогена", std::nullopt};
    }

    const std::string handle_name = "htimer32_" + std::to_string(config.instance);
    const bool has_extra_interrupts = config.overflow_interrupt || config.underflow_interrupt;
    const bool uses_interrupts = !config.channels.empty() || has_extra_interrupts;

    PeripheralContribution contribution;
    contribution.label = "TIMER32_" + std::to_string(config.instance);
    contribution.init_order = kInitOrderTimer32;
    contribution.init_function_name = "TIMER32_" + std::to_string(config.instance) + "_Init";
    contribution.init_c_includes = {"mik32_hal_timer32.h", "mik32_hal_pcc.h"};
    contribution.public_prototypes = {"void " + contribution.init_function_name + "(void);"};

    std::ostringstream globals;
    globals << "TIMER32_HandleTypeDef " << handle_name << ";\n";
    for (const Timer32ChannelConfig& channel : config.channels)
    {
        globals << "TIMER32_CHANNEL_HandleTypeDef " << channelHandleName(handle_name, channel.index) << ";\n";
    }
    contribution.global_definitions = globals.str();

    std::ostringstream freq_comment;
    freq_comment << std::fixed << std::setprecision(3);
    if (clock_source_info->known_frequency)
    {
        // mik32_hal_timer32.h, TIMER32_ClockConfigTypeDef.Prescaler: «Делитель
        // частоты. Частота делится на Prescaler + 1» (строки 93-94) — в отличие
        // от Timer16 это прямое значение регистра, а не индекс enum'а.
        // Верно для prescaler/tim1_sys_clk/tim1_hclk — все три дают на входе
        // предделителя 32МГц при AHBDivider=0 (ТЗ §3.1.8).
        const double count_freq_hz = static_cast<double>(knowledge.sys_clk_hz) / (static_cast<double>(config.prescaler) + 1.0);
        freq_comment << " * Расчётная частота счёта при sys_clk=" << knowledge.sys_clk_hz
                     << "Гц, clock_source=" << config.clock_source << " (ТЗ §3.1.8): "
                        "sys_clk/(prescaler+1) = " << count_freq_hz << " Гц.\n";
        if (config.has_frequency)
        {
            // OVERNIGHT_FIELD_REFERENCE.md: период ШИМ = top+1 тактов (по аналогии
            // с Timer16 ARR+1) — та же идея "запрошено/получилось", что у UART
            // baud_rate и Timer16 frequency_hz.
            const double output_freq_hz = count_freq_hz / (static_cast<double>(config.top) + 1.0);
            freq_comment << " * Запрошено (frequency_hz): " << config.requested_frequency_hz
                         << " Гц, получилось (после подбора prescaler=0 и округления TOP): "
                         << output_freq_hz << " Гц.\n";
        }
    }
    else
    {
        // clock_source=tim2_osc32k/tim2_lsi32k/tx_pad — внешний кристалл,
        // некалиброванный внутренний RC-генератор или внешний вывод, датащит
        // не даёт зафиксированного значения частоты (AUDIT_TZ_PERIPHERY_GAPS.md,
        // Тир 1 / Timer32) — не вычисляем частоту счёта по неподтверждённому
        // числу (тот же принцип, что у Timer16, см. timer16_contribution.cpp).
        freq_comment << " * clock_source=" << config.clock_source << ": частота счёта не вычислена — "
                        "значение этого источника не зафиксировано датащитом/HAL (внешний кристалл, "
                        "некалиброванный RC-генератор или внешний вывод), посчитать можно только "
                        "после измерения на реальной плате.\n";
    }

    std::ostringstream init_c;
    init_c << "/*\n"
              " * " << *timer_handle << ": count_mode=" << config.count_mode
           << ", prescaler=" << config.prescaler << ", top=" << config.top << ".\n"
           << freq_comment.str();
    if (uses_interrupts)
    {
        init_c << kErrata91Comment;
    }
    init_c << " */\n"
              "void " << contribution.init_function_name << "(void)\n"
              "{\n"
              "    " << handle_name << ".Instance = " << *timer_handle << ";\n"
              "\n"
              "    " << handle_name << ".Clock.Source = " << clock_source_info->hal_enum << ";\n"
              "    " << handle_name << ".Clock.Prescaler = " << config.prescaler << ";\n"
              "    " << handle_name << ".CountMode = " << *count_mode_enum << ";\n"
              "    " << handle_name << ".Top = " << config.top << ";\n"
              "\n"
              "    HAL_Timer32_Init(&" << handle_name << ");\n";

    // AUDIT_TZ_PERIPHERY_GAPS.md, Timer32: "overflow_interrupt"/"underflow_interrupt"
    // — общие флаги, независимые от набора каналов. shared/periphery/timer32.h:52-55.
    std::vector<std::string> extra_masks;
    if (config.overflow_interrupt) extra_masks.push_back("TIMER32_INT_OVERFLOW_M");
    if (config.underflow_interrupt) extra_masks.push_back("TIMER32_INT_UNDERFLOW_M");

    if (config.channels.empty())
    {
        // ТЗ §3.1.5 п.2: «для Timer32_0 – только настройку счётчика» —
        // тот же путь используется и для Timer32_1/2 без сконфигурированных
        // каналов (просто счётчик, без ШИМ/захвата/сравнения). Обычный
        // HAL_Timer32_Start (не _IT-вариант) не трогает INT_MASK — используется,
        // если ни overflow_interrupt, ни underflow_interrupt не запрошены
        // (Errata п.9.1 здесь не актуальна — прерывание никогда не включается).
        if (extra_masks.empty())
        {
            init_c << "    HAL_Timer32_Start(&" << handle_name << ");\n"
                      "}\n";
            contribution.init_function_text = init_c.str();
            return ParseResult<PeripheralContribution>{true, "", contribution};
        }

        // HAL_Timer32_Start_IT(timer, intMask) — mik32_hal_timer32.c:488-494,
        // включает маску прерываний (HAL_Timer32_InterruptMask_Set) и
        // запускает счёт, тот же вызов, которым HAL_Timer32_Base_Start_IT
        // включает ровно TIMER32_INT_OVERFLOW_M (mik32_hal_timer32.c:376-382).
        std::ostringstream mask;
        for (size_t i = 0; i < extra_masks.size(); ++i)
        {
            if (i > 0) mask << " | ";
            mask << extra_masks[i];
        }
        init_c << "    HAL_Timer32_Start_IT(&" << handle_name << ", " << mask.str() << ");\n"
                  "}\n";
        contribution.init_function_text = init_c.str();

        std::vector<InterruptSource> sources;
        if (config.overflow_interrupt) sources.push_back({"TIMER32_INT_OVERFLOW_M", "переполнение"});
        if (config.underflow_interrupt) sources.push_back({"TIMER32_INT_UNDERFLOW_M", "антипереполнение"});
        contribution.helper_functions_text = buildErrata91IrqTemplate(handle_name, config.instance, sources);

        return ParseResult<PeripheralContribution>{true, "", contribution};
    }

    // Errata §9.1: каждый канал в режиме compare/capture включает СВОЙ
    // независимый бит маски (TIMER32_INT_OC_M(ch)/TIMER32_INT_IC_M(ch)),
    // а pwm делит один и тот же TIMER32_INT_OVERFLOW_M с overflow_interrupt
    // (HAL_Timer32_PWM_Start_IT включает именно его, mik32_hal_timer32.c:404) —
    // поэтому overflow добавляется в список источников не более одного раза.
    std::vector<InterruptSource> channel_sources;
    bool overflow_source_added = false;

    init_c << "\n";
    for (const Timer32ChannelConfig& channel : config.channels)
    {
        const std::string* mode_enum = findOrNull(knowledge.channel_mode_hal_enum, channel.mode);
        if (!mode_enum)
        {
            return ParseResult<PeripheralContribution>{
                false, "Timer32ChannelConfig.mode отсутствует в knowledge_base/timer32.json", std::nullopt};
        }

        const std::string channel_handle = channelHandleName(handle_name, channel.index);
        const Timer32Knowledge::FixedPin* fixed_pin = nullptr;
        if (auto instance_map = findOrNull(knowledge.instance_channel_fixed_pin, config.instance))
        {
            fixed_pin = findOrNull(*instance_map, channel.index);
        }

        init_c << "    /* Канал " << channel.index << ": mode=" << channel.mode;
        if (fixed_pin)
        {
            init_c << ", вывод GPIO_" << fixed_pin->gpio_instance << "/PIN_" << fixed_pin->pin
                   << " (mik32_hal_timer32.c, HAL_TIMER32_Channel_MspInit)";
        }
        init_c << " */\n"
                  "    " << channel_handle << ".TimerInstance = " << handle_name << ".Instance;\n"
                  "    " << channel_handle << ".ChannelIndex = TIMER32_CHANNEL_" << channel.index << ";\n"
                  "    " << channel_handle << ".Mode = " << *mode_enum << ";\n";

        if (channel.mode == "pwm")
        {
            init_c << "    " << channel_handle << ".PWM_Invert = "
                   << (channel.pwm_invert ? "TIMER32_CHANNEL_INVERTED_PWM" : "TIMER32_CHANNEL_NON_INVERTED_PWM") << ";\n"
                      "    " << channel_handle << ".OCR = " << channel.ocr << ";\n";
        }
        else if (channel.mode == "compare")
        {
            init_c << "    " << channel_handle << ".OCR = " << channel.ocr << ";\n";
        }
        else // capture
        {
            const std::string* edge_enum = findOrNull(knowledge.capture_edge_hal_enum, channel.capture_edge);
            if (!edge_enum)
            {
                return ParseResult<PeripheralContribution>{
                    false, "Timer32ChannelConfig.capture_edge отсутствует в knowledge_base/timer32.json", std::nullopt};
            }
            init_c << "    " << channel_handle << ".CaptureEdge = " << *edge_enum << ";\n"
                      "    " << channel_handle << ".Noise = "
                   << (channel.noise_filter ? "TIMER32_CHANNEL_FILTER_ON" : "TIMER32_CHANNEL_FILTER_OFF") << ";\n";
        }

        init_c << "\n"
                  "    HAL_Timer32_Channel_Init(&" << channel_handle << ");\n";

        if (channel.mode == "pwm")
        {
            init_c << "    HAL_Timer32_PWM_Start_IT(&" << handle_name << ", &" << channel_handle << ");\n";
            if (!overflow_source_added)
            {
                channel_sources.push_back({"TIMER32_INT_OVERFLOW_M", "переполнение (в т.ч. канал ШИМ)"});
                overflow_source_added = true;
            }
        }
        else if (channel.mode == "compare")
        {
            init_c << "    HAL_Timer32_Compare_Start_IT(&" << handle_name << ", &" << channel_handle << ");\n";
            channel_sources.push_back({"TIMER32_INT_OC_M(TIMER32_CHANNEL_" + std::to_string(channel.index) + ")",
                                        "сравнение, канал " + std::to_string(channel.index)});
        }
        else
        {
            init_c << "    HAL_Timer32_Capture_Start_IT(&" << handle_name << ", &" << channel_handle << ");\n";
            channel_sources.push_back({"TIMER32_INT_IC_M(TIMER32_CHANNEL_" + std::to_string(channel.index) + ")",
                                        "захват, канал " + std::to_string(channel.index)});
        }
        init_c << "\n";
    }

    if (!extra_masks.empty())
    {
        // overflow_interrupt/underflow_interrupt — аддитивны к маскам, уже
        // включённым каждым каналом через *_Start_IT выше (HAL_Timer32_InterruptMask_Set
        // всегда |=, mik32_hal_timer32.c:177-179 — повторное/дополнительное
        // включение безвредно).
        init_c << "    /* overflow_interrupt/underflow_interrupt (AUDIT_TZ_PERIPHERY_GAPS.md) */\n"
                  "    HAL_Timer32_InterruptMask_Set(&" << handle_name << ", ";
        for (size_t i = 0; i < extra_masks.size(); ++i)
        {
            if (i > 0) init_c << " | ";
            init_c << extra_masks[i];
        }
        init_c << ");\n\n";

        if (config.overflow_interrupt && !overflow_source_added)
        {
            channel_sources.push_back({"TIMER32_INT_OVERFLOW_M", "переполнение"});
            overflow_source_added = true;
        }
        if (config.underflow_interrupt)
        {
            channel_sources.push_back({"TIMER32_INT_UNDERFLOW_M", "антипереполнение"});
        }
    }

    init_c << "}\n";
    contribution.init_function_text = init_c.str();

    // ТЗ §3.1.5 п.3: «генерируемый шаблон обработчика прерывания Timer32
    // должен содержать программный сброс флага в начале тела функции» —
    // выполнено построчно для каждого источника внутри buildErrata91IrqTemplate
    // (HAL_Timer32_InterruptFlags_ClearMask на каждый сработавший mask_expr).
    // Не подключён к raw_trap_handler — см. knowledge_base/timer32.json,
    // not_covered_by_this_codegen.interrupt_wiring и
    // docs/implementations/timer32.md.
    contribution.helper_functions_text = buildErrata91IrqTemplate(handle_name, config.instance, channel_sources);

    return ParseResult<PeripheralContribution>{true, "", contribution};
}
