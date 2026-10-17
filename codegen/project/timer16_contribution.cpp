#include "timer16_contribution.h"
#include "contribution_utils.h"
#include <iomanip>
#include <map>
#include <sstream>

namespace
{
    // mik32_hal_timer16.h, HAL_Timer16_SourceTypeDef (минус EXTERNAL_INPUT1 —
    // см. schemas/timer16.json). known_frequency=true только для вариантов,
    // совпадающих с sys_clk=32МГц при дефолтной конфигурации тактирования
    // (ТЗ §3.1.8) — для остальных реальная частота не зафиксирована ни
    // датащитом, ни этим проектом (внешний кристалл/некалиброванный RC).
    struct ClockSourceInfo { const char* hal_enum; bool known_frequency; };
    const std::map<std::string, ClockSourceInfo> kClockSourceInfo = {
        {"internal_system", {"TIMER16_SOURCE_INTERNAL_SYSTEM", true}},
        {"internal_ahb",    {"TIMER16_SOURCE_INTERNAL_AHB", true}},
        {"internal_osc32m", {"TIMER16_SOURCE_INTERNAL_OSC32M", false}},
        {"internal_hsi32m", {"TIMER16_SOURCE_INTERNAL_HSI32M", true}},
        {"internal_osc32k", {"TIMER16_SOURCE_INTERNAL_OSC32K", false}},
        {"internal_lsi32k", {"TIMER16_SOURCE_INTERNAL_LSI32K", false}},
    };

    // mik32_hal_timer16.h, HAL_Timer16_Trigger_ActiveEdgeTypeDef.
    std::string triggerEdgeConstant(const std::string& edge)
    {
        if (edge == "rising") return "TIMER16_TRIGGER_ACTIVEEDGE_RISING";
        if (edge == "falling") return "TIMER16_TRIGGER_ACTIVEEDGE_FOLLING";
        if (edge == "both") return "TIMER16_TRIGGER_ACTIVEEDGE_BOTH";
        return "TIMER16_TRIGGER_ACTIVEEDGE_SOFTWARE";
    }

    // mik32_hal_timer16.h, HAL_Timer16_FilterTypeDef — общий для
    // Filter.ExternalClock и Filter.Trigger.
    std::string filterConstant(const std::string& filter)
    {
        if (filter == "2clock") return "TIMER16_FILTER_2CLOCK";
        if (filter == "4clock") return "TIMER16_FILTER_4CLOCK";
        if (filter == "8clock") return "TIMER16_FILTER_8CLOCK";
        return "TIMER16_FILTER_NONE";
    }
}

ParseResult<PeripheralContribution> buildTimer16Contribution(
    const Timer16Config& config,
    const Timer16Knowledge& knowledge)
{
    const std::string* timer_handle = findOrNull(knowledge.instance_handle, config.instance);
    const std::string* prescaler_enum = findOrNull(knowledge.prescaler_hal_enum, std::to_string(config.prescaler));
    const std::string* trigger_source_enum = findOrNull(knowledge.instance_default_trigger_source, config.instance);
    const Timer16Knowledge::FixedPin* pwm_pin = findOrNull(knowledge.instance_fixed_pwm_pin, config.instance);
    const bool* waveform_enable = findOrNull(knowledge.mode_waveform_enable, config.mode);

    if (!timer_handle || !prescaler_enum || !trigger_source_enum || !pwm_pin || !waveform_enable)
    {
        return ParseResult<PeripheralContribution>{
            false,
            "Timer16Config ссылается на instance/prescaler/mode, отсутствующий в knowledge_base/timer16.json",
            std::nullopt};
    }

    const std::string handle_name = "htimer16_" + std::to_string(config.instance);

    PeripheralContribution contribution;
    contribution.label = "TIMER16_" + std::to_string(config.instance);
    contribution.init_order = kInitOrderTimer16;
    contribution.init_function_name = "TIMER16_" + std::to_string(config.instance) + "_Init";
    contribution.init_c_includes = {"mik32_hal_timer16.h", "mik32_hal_pcc.h"};
    contribution.public_prototypes = {"void " + contribution.init_function_name + "(void);"};
    contribution.global_definitions = "Timer16_HandleTypeDef " + handle_name + ";\n";

    // ТЗ §3.1.4 п.2: mode задаёт и способ счёта, и то, генерируется ли
    // волновая форма (knowledge_base/timer16.json, mode_waveform_enable).
    // Источник тактирования (Clock.Source) при external_event_count всегда
    // EXTERNAL_INPUT1 (смысл режима — считать внешние события на этом
    // входе), config.clock_source в этом случае принят парсером, но
    // игнорируется (см. schemas/timer16.json).
    const bool is_external_event_count = (config.mode == "external_event_count");
    // AUDIT_TZ_PERIPHERY_GAPS.md, Timer16 (2026-10-14): mode=encoder — датащит,
    // раздел «Режим энкодера»: CKSEL обязан быть 0 (внутренний источник),
    // PRESC[2:0] обязан быть 0b000 (prescaler=1, уже проверено парсером).
    const bool is_encoder = (config.mode == "encoder");
    const ClockSourceInfo* clock_source_info = findOrNull(kClockSourceInfo, config.clock_source);
    if (!clock_source_info)
    {
        return ParseResult<PeripheralContribution>{
            false, "Timer16Config.clock_source содержит значение, отсутствующее в HAL-таблице кодогена", std::nullopt};
    }
    const std::string clock_source = is_external_event_count
        ? "TIMER16_SOURCE_EXTERNAL_INPUT1"
        : clock_source_info->hal_enum;
    const std::string count_mode = is_external_event_count
        ? "TIMER16_COUNTMODE_EXTERNAL"
        : "TIMER16_COUNTMODE_INTERNAL";
    const std::string waveform_enable_enum = *waveform_enable
        ? "TIMER16_WAVEFORM_GENERATION_ENABLE"
        : "TIMER16_WAVEFORM_GENERATION_DISABLE";
    const std::string waveform_polarity_enum = (*waveform_enable && config.waveform_inverted)
        ? "TIMER16_WAVEFORM_POLARITY_INVERTED"
        : "TIMER16_WAVEFORM_POLARITY_NONINVERTED";

    std::ostringstream freq_comment;
    if (is_encoder)
    {
        // Датащит, раздел «Режим энкодера» (стр.128-129): ARR здесь не период
        // ШИМ, а верхняя граница положения энкодера (счётчик идёт 0..ARR или
        // ARR..0 в зависимости от направления вращения) — вычислять "частоту
        // ШИМ" для этого режима не имеет смысла, в отличие от period/one_shot.
        freq_comment << " * Режим encoder: ARR задаёт верхнюю границу положения энкодера "
                        "(счёт 0..ARR или ARR..0 по направлению вращения), не период ШИМ. "
                        "cmp игнорируется этим кодогеном — HAL_Timer16_Encoder_Start принимает только ARR.\n";
    }
    else if (!is_external_event_count)
    {
        if (clock_source_info->known_frequency)
        {
            const double pwm_freq_hz = static_cast<double>(knowledge.sys_clk_hz)
                / (static_cast<double>(config.prescaler) * (static_cast<double>(config.arr) + 1.0));
            freq_comment << std::fixed << std::setprecision(3);
            freq_comment << " * Расчётная частота ШИМ при sys_clk=" << knowledge.sys_clk_hz
                         << "Гц (ТЗ §3.1.8, clock_source=" << config.clock_source
                         << "): sys_clk/(prescaler*(ARR+1)) = " << pwm_freq_hz << " Гц.\n";
            if (config.has_frequency_duty)
            {
                // Высокий уровень держится ARR-CMP+1 тактов из ARR+1 (подтверждено
                // мультиметр-тестом, docs/implementations/timer16.md) — duty% обратно
                // пропорционален CMP, не напрямую cmp/(arr+1).
                const double actual_duty_percent = ((static_cast<double>(config.arr) + 1.0 - static_cast<double>(config.cmp))
                    / (static_cast<double>(config.arr) + 1.0)) * 100.0;
                freq_comment << " * Запрошено (frequency_hz/duty_percent): " << config.requested_frequency_hz
                             << " Гц / " << config.requested_duty_percent << "%, получилось (после подбора"
                                " prescaler и округления ARR/CMP): " << pwm_freq_hz << " Гц / "
                             << actual_duty_percent << "%.\n";
            }
        }
        else
        {
            // clock_source=internal_osc32m/osc32k/lsi32k — внешний кристалл
            // или некалиброванный внутренний RC-генератор, датащит не даёт
            // зафиксированного значения частоты (AUDIT_TZ_PERIPHERY_GAPS.md,
            // Тир 1 / Timer16) — не вычисляем частоту ШИМ по неподтверждённому
            // числу.
            freq_comment << " * clock_source=" << config.clock_source << ": частота ШИМ не вычислена — "
                            "значение этого источника не зафиксировано датащитом/HAL (внешний кристалл "
                            "или некалиброванный RC-генератор), посчитать можно только после измерения "
                            "на реальной плате.\n";
        }
    }
    else
    {
        freq_comment << " * Режим external_event_count: волновая форма не генерируется, "
                        "ШИМ-вывод не занят (см. knowledge_base/timer16.json, mode_waveform_enable). "
                        "Поле cmp игнорируется этим кодогеном — HAL_Timer16_Counter_Start принимает только ARR.\n";
    }

    std::ostringstream init_c;
    init_c << "/*\n"
              " * " << *timer_handle << ": mode=" << config.mode << ", prescaler=" << config.prescaler
           << ", arr=" << config.arr << ", cmp=" << config.cmp << ".\n"
           << freq_comment.str()
           << " * ШИМ-вывод (когда используется): GPIO_" << pwm_pin->gpio_instance
           << "/PIN_" << pwm_pin->pin << " (ТЗ §3.1.4, датащит — жёстко закреплён за instance).\n";
    if (is_encoder)
    {
        const Timer16Knowledge::FixedPin* input1_pin = findOrNull(knowledge.instance_fixed_input1_pin, config.instance);
        const Timer16Knowledge::FixedPin* input2_pin = findOrNull(knowledge.instance_fixed_input2_pin, config.instance);
        if (!input1_pin || !input2_pin)
        {
            return ParseResult<PeripheralContribution>{
                false, "Timer16Config.instance отсутствует в instance_fixed_input1_pin/input2_pin knowledge_base/timer16.json", std::nullopt};
        }
        init_c << " * Входы энкодера (HAL_TIMER16_MspInit настраивает оба автоматически): "
               << "Input1=GPIO_" << input1_pin->gpio_instance << "/PIN_" << input1_pin->pin
               << ", Input2=GPIO_" << input2_pin->gpio_instance << "/PIN_" << input2_pin->pin
               << " (датащит, таблица 193).\n";
    }
    init_c << " * Errata п. 8.1 (mik32v2errataV1_0_1_1 v1.0.1): при PRESCALER>1 длительность первого\n"
              " * и последнего такта ШИМ-периода отличается от промежуточных на 1 такт Timer16_clk,\n"
              " * из-за чего фактическая скважность смещена относительно заданной по CMP/ARR.\n"
              " * Обход: отсутствует.\n"
              " * Errata п. 8.2 (mik32v2errataV1_0_1_1 v1.0.1): флаг прерывания по сравнению CMPM\n"
              " * может выставиться повторно, если счётчик не успел измениться за такт после сброса\n"
              " * флага. Актуально только при использовании прерываний Timer16 — этот кодоген\n"
              " * прерывания не включает (HAL_Timer16_StartPWM/StartOneShot/Counter_Start без _IT).\n"
              " */\n"
              "void " << contribution.init_function_name << "(void)\n"
              "{\n"
              "    " << handle_name << ".Instance = " << *timer_handle << ";\n"
              "\n"
              "    " << handle_name << ".Clock.Source = " << clock_source << ";\n"
              "    " << handle_name << ".CountMode = " << count_mode << ";\n"
              "    " << handle_name << ".Clock.Prescaler = " << *prescaler_enum << ";\n"
              "    " << handle_name << ".ActiveEdge = TIMER16_ACTIVEEDGE_RISING;\n"
              "\n"
              "    " << handle_name << ".Preload = TIMER16_PRELOAD_AFTERWRITE;\n"
              "\n"
              "    " << handle_name << ".Trigger.Source = " << *trigger_source_enum << ";\n"
              "    " << handle_name << ".Trigger.ActiveEdge = " << triggerEdgeConstant(config.trigger_edge) << ";\n"
              "    " << handle_name << ".Trigger.TimeOut = TIMER16_TIMEOUT_DISABLE;\n"
              "\n"
              "    " << handle_name << ".Filter.ExternalClock = " << filterConstant(config.external_clock_filter) << ";\n"
              "    " << handle_name << ".Filter.Trigger = " << filterConstant(config.trigger_filter) << ";\n"
              "\n"
              // HAL_TIMER16_MspInit (вызывается ВНУТРИ HAL_Timer16_Init ниже)
              // проверяет EncoderMode, чтобы решить, настраивать ли выводы
              // Input1/Input2 — поле обязано быть выставлено ДО Init, сам
              // HAL_Timer16_Encoder_Start() (вызывается позже) выставляет его
              // ещё раз явным вызовом SetEncoderMode, но для MspInit уже
              // поздно — она успевает отработать раньше.
              "    " << handle_name << ".EncoderMode = "
           << (is_encoder ? "TIMER16_ENCODER_ENABLE" : "TIMER16_ENCODER_DISABLE") << ";\n"
              "\n"
              "    " << handle_name << ".Waveform.Enable = " << waveform_enable_enum << ";\n"
              "    " << handle_name << ".Waveform.Polarity = " << waveform_polarity_enum << ";\n"
              "\n"
              "    HAL_Timer16_Init(&" << handle_name << ");\n"
              "\n";

    // ТЗ §3.1.4 п.5 / расхождение с реальным HAL зафиксировано в
    // docs/implementations/timer16.md: HAL_Timer16_StartPWM/StartOneShot сами
    // всегда пишут CMP раньше ARR, порядок записи регистров не контролируется
    // вызывающим кодом ни в каком случае.
    if (config.mode == "period")
    {
        init_c << "    HAL_Timer16_StartPWM(&" << handle_name << ", " << config.arr << ", " << config.cmp << ");\n";
    }
    else if (config.mode == "one_shot")
    {
        init_c << "    HAL_Timer16_StartOneShot(&" << handle_name << ", " << config.arr << ", " << config.cmp << ");\n";
    }
    else if (is_encoder)
    {
        // Датащит, раздел «Режим энкодера»: Encoder_Start сама выставляет
        // EncoderMode=ENABLE (повторно — поле уже выставлено выше ради
        // MspInit), включает таймер, сбрасывает флаги ARROK/DOWN/UP и пишет
        // ARR (mik32_hal_timer16.c:677-688).
        init_c << "    HAL_Timer16_Encoder_Start(&" << handle_name << ", " << config.arr << ");\n";
    }
    else
    {
        init_c << "    HAL_Timer16_Counter_Start(&" << handle_name << ", " << config.arr << ");\n";
    }
    init_c << "}\n";
    contribution.init_function_text = init_c.str();

    return ParseResult<PeripheralContribution>{true, "", contribution};
}
