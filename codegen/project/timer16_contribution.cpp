#include "timer16_contribution.h"
#include "contribution_utils.h"
#include <iomanip>
#include <sstream>

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

    // ТЗ §3.1.4 п.2: mode задаёт и источник тактирования, и способ счёта, и
    // то, генерируется ли волновая форма (knowledge_base/timer16.json,
    // mode_waveform_enable).
    const bool is_external_event_count = (config.mode == "external_event_count");
    const std::string clock_source = is_external_event_count
        ? "TIMER16_SOURCE_EXTERNAL_INPUT1"
        : "TIMER16_SOURCE_INTERNAL_SYSTEM";
    const std::string count_mode = is_external_event_count
        ? "TIMER16_COUNTMODE_EXTERNAL"
        : "TIMER16_COUNTMODE_INTERNAL";
    const std::string waveform_enable_enum = *waveform_enable
        ? "TIMER16_WAVEFORM_GENERATION_ENABLE"
        : "TIMER16_WAVEFORM_GENERATION_DISABLE";

    std::ostringstream freq_comment;
    if (!is_external_event_count)
    {
        const double pwm_freq_hz = static_cast<double>(knowledge.sys_clk_hz)
            / (static_cast<double>(config.prescaler) * (static_cast<double>(config.arr) + 1.0));
        freq_comment << std::fixed << std::setprecision(3);
        freq_comment << " * Расчётная частота ШИМ при sys_clk=" << knowledge.sys_clk_hz
                     << "Гц (ТЗ §3.1.8): sys_clk/(prescaler*(ARR+1)) = " << pwm_freq_hz << " Гц.\n";
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
           << "/PIN_" << pwm_pin->pin << " (ТЗ §3.1.4, датащит — жёстко закреплён за instance).\n"
              " * Errata п. 8.1 (mik32v2errataV1_0_1_1 v1.0.1): при PRESCALER>1 длительность первого\n"
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
              "    " << handle_name << ".Trigger.ActiveEdge = TIMER16_TRIGGER_ACTIVEEDGE_SOFTWARE;\n"
              "    " << handle_name << ".Trigger.TimeOut = TIMER16_TIMEOUT_DISABLE;\n"
              "\n"
              "    " << handle_name << ".Filter.ExternalClock = TIMER16_FILTER_NONE;\n"
              "    " << handle_name << ".Filter.Trigger = TIMER16_FILTER_NONE;\n"
              "\n"
              "    " << handle_name << ".EncoderMode = TIMER16_ENCODER_DISABLE;\n"
              "\n"
              "    " << handle_name << ".Waveform.Enable = " << waveform_enable_enum << ";\n"
              "    " << handle_name << ".Waveform.Polarity = TIMER16_WAVEFORM_POLARITY_NONINVERTED;\n"
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
    else
    {
        init_c << "    HAL_Timer16_Counter_Start(&" << handle_name << ", " << config.arr << ");\n";
    }
    init_c << "}\n";
    contribution.init_function_text = init_c.str();

    return ParseResult<PeripheralContribution>{true, "", contribution};
}
