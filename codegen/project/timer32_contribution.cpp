#include "timer32_contribution.h"
#include "contribution_utils.h"
#include <iomanip>
#include <sstream>

namespace
{
    // Errata MIK32V2 v1.0.1, §9.1 «Сброс флагов» — текст цитаты общий для
    // предупреждения (ТЗ §3.1.5 п.3) и для шаблона обработчика прерывания.
    constexpr const char* kErrata91Comment =
        " * Errata п. 9.1 (mik32v2errataV1_0_1_1 v1.0.1): сброс флагов IC_INT_CHx/\n"
        " * OC_INT_CHx/UDF_INT/OVF_INT происходит в конце счёта таймера, во время\n"
        " * которого была запись в INT_CLEAR — возможен повторный вход в обработчик\n"
        " * прерывания при низкой частоте счёта. Полный обход по тексту errata —\n"
        " * отключать прерывание по сработавшему флагу и включать его заново в\n"
        " * основном цикле, только когда флаг реально очистился (пример в errata\n"
        " * читает статус ДО очистки — HAL_Timer32_InterruptFlags_Get). ТЗ §3.1.5\n"
        " * п.3 требует буквально программный сброс флага как первое действие\n"
        " * функции-обработчика — это и сделано ниже; различение источника\n"
        " * прерывания (если нужно) требует читать статус ДО этого вызова.\n";

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

    const std::string handle_name = "htimer32_" + std::to_string(config.instance);
    const bool uses_interrupts = !config.channels.empty();

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
    // mik32_hal_timer32.h, TIMER32_ClockConfigTypeDef.Prescaler: «Делитель
    // частоты. Частота делится на Prescaler + 1» (строки 93-94) — в отличие
    // от Timer16 это прямое значение регистра, а не индекс enum'а.
    const double count_freq_hz = static_cast<double>(knowledge.sys_clk_hz) / (static_cast<double>(config.prescaler) + 1.0);
    freq_comment << std::fixed << std::setprecision(3);
    freq_comment << " * Расчётная частота счёта при sys_clk=" << knowledge.sys_clk_hz
                 << "Гц, Clock.Source=TIMER32_SOURCE_PRESCALER (ТЗ §3.1.8): "
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
              "    " << handle_name << ".Clock.Source = TIMER32_SOURCE_PRESCALER;\n"
              "    " << handle_name << ".Clock.Prescaler = " << config.prescaler << ";\n"
              "    " << handle_name << ".CountMode = " << *count_mode_enum << ";\n"
              "    " << handle_name << ".Top = " << config.top << ";\n"
              "\n"
              "    HAL_Timer32_Init(&" << handle_name << ");\n";

    if (config.channels.empty())
    {
        // ТЗ §3.1.5 п.2: «для Timer32_0 – только настройку счётчика» —
        // тот же путь используется и для Timer32_1/2 без сконфигурированных
        // каналов (просто счётчик, без ШИМ/захвата/сравнения). Обычный
        // HAL_Timer32_Start (не _IT-вариант) не трогает INT_MASK, поэтому
        // Errata п.9.1 здесь не актуальна — прерывание никогда не включается.
        init_c << "    HAL_Timer32_Start(&" << handle_name << ");\n"
                  "}\n";
        contribution.init_function_text = init_c.str();
        return ParseResult<PeripheralContribution>{true, "", contribution};
    }

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
        }
        else if (channel.mode == "compare")
        {
            init_c << "    HAL_Timer32_Compare_Start_IT(&" << handle_name << ", &" << channel_handle << ");\n";
        }
        else
        {
            init_c << "    HAL_Timer32_Capture_Start_IT(&" << handle_name << ", &" << channel_handle << ");\n";
        }
        init_c << "\n";
    }
    init_c << "}\n";
    contribution.init_function_text = init_c.str();

    // ТЗ §3.1.5 п.3: «генерируемый шаблон обработчика прерывания Timer32
    // должен содержать программный сброс флага в начале тела функции».
    // Не подключён к raw_trap_handler — см. knowledge_base/timer32.json,
    // not_covered_by_this_codegen.interrupt_wiring и
    // docs/implementations/timer32.md.
    std::ostringstream irq_template;
    irq_template << "/*\n"
                 << kErrata91Comment
                 << " *\n"
                 << " * Шаблон НЕ подключён к вектору прерываний RISC-V (raw_trap_handler,\n"
                    " * HAL_EPIC_MaskLevelSet, HAL_IRQ_EnableInterrupts) — это забота\n"
                    " * пользователя, см. docs/implementations/timer32.md. Вызвать эту функцию\n"
                    " * нужно из своего raw_trap_handler после проверки EPIC_CHECK_TIMER32_"
                 << config.instance << "().\n"
                    " */\n"
                    "void " << irqTemplateFunctionName(config.instance) << "(void)\n"
                    "{\n"
                    "    HAL_Timer32_InterruptFlags_Clear(&" << handle_name << ");\n"
                    "\n"
                    "    /* USER TODO: если нужно различать источник прерывания (канал/переполнение),\n"
                       "       читать HAL_Timer32_InterruptFlags_Get(&" << handle_name << ") ДО вызова\n"
                       "       HAL_Timer32_InterruptFlags_Clear выше — после сброса флаги уже не различить. */\n"
                    "}\n";
    contribution.helper_functions_text = irq_template.str();

    return ParseResult<PeripheralContribution>{true, "", contribution};
}
