#include "tsens_contribution.h"
#include "contribution_utils.h"
#include <sstream>

namespace
{
    std::string clockSourceMacro(const std::string& value)
    {
        if (value == "sys_clk") return "HAL_TSENS_SYS_CLK";
        if (value == "hclk") return "HAL_TSENS_HCLK";
        if (value == "osc32m") return "HAL_TSENS_OSC32M";
        if (value == "hsi32m") return "HAL_TSENS_HSI32M";
        if (value == "osc32k") return "HAL_TSENS_OSC32K";
        return "HAL_TSENS_LSI32K";
    }

    std::string interruptEnableMacro(const std::string& value)
    {
        if (value == "low") return "__HAL_TSENS_IRQ_ENABLE_LOWIRQ(&htsens)";
        if (value == "high") return "__HAL_TSENS_IRQ_ENABLE_HIIRQ(&htsens)";
        return "__HAL_TSENS_IRQ_ENABLE_EOC(&htsens)";
    }
}

// Концепция: TSENS — единственный глобальный блок (htsens.Instance = ANALOG_REG,
// mik32_hal_tsens.h), как DAC/ADC. HAL_TSENS_Init сама вызывает HAL_TSENS_MspInit
// (clock-enable). HAL_TSENS_SingleStart/ContinuousStart (когда и как часто
// измерять) остаются за пользователем — тот же принцип, что у WDT/ADC/DAC.
ParseResult<PeripheralContribution> buildTsensContribution(const TsensConfig& config)
{
    PeripheralContribution contribution;
    contribution.label = "TSENS";
    contribution.init_order = kInitOrderTsens;
    contribution.init_function_name = "TSENS_Init";
    contribution.init_c_includes = {"mik32_hal_tsens.h", "mik32_hal_pcc.h"};
    contribution.public_prototypes = {"void " + contribution.init_function_name + "(void);"};
    contribution.global_definitions = "TSENS_HandleTypeDef htsens;\n";

    std::ostringstream init_c;
    init_c << "/*\n"
              " * TSENS: clock_source=" << config.clock_source << ", frequency_hz=" << config.frequency_hz << ".\n"
              " * Только конфигурация — HAL_TSENS_SingleStart/ContinuousStart (когда и как\n"
              " * часто измерять) остаются за пользователем (см. docs/implementations/tsens.md).\n"
              " */\n"
              "void " << contribution.init_function_name << "(void)\n"
              "{\n"
              "    htsens.Instance = ANALOG_REG;\n"
              "    htsens.Clock = " << clockSourceMacro(config.clock_source) << ";\n"
              "    htsens.Frequency = " << config.frequency_hz << ";\n"
              "\n"
              "    if (HAL_TSENS_Init(&htsens) != HAL_OK)\n"
              "    {\n"
              "        /* USER TODO: обработать ошибку инициализации TSENS (TSENS_Init_Error). */\n"
              "    }\n";

    if (config.has_low_threshold)
    {
        init_c << "\n    HAL_TSENS_SetLowThreshold(&htsens, " << config.low_threshold_celsius << ");\n";
    }
    if (config.has_high_threshold)
    {
        init_c << "\n    HAL_TSENS_SetHiThreshold(&htsens, " << config.high_threshold_celsius << ");\n";
    }
    if (!config.interrupts.empty())
    {
        init_c << "\n"
                  "    /* EPIC-маскирование (HAL_EPIC_MaskLevelSet(HAL_EPIC_TSENS_MASK)) и\n"
                  "     * HAL_IRQ_EnableInterrupts() остаются на стороне пользователя. */\n";
        for (const std::string& interrupt : config.interrupts)
        {
            init_c << "    " << interruptEnableMacro(interrupt) << ";\n";
        }
    }

    init_c << "}\n";
    contribution.init_function_text = init_c.str();

    return ParseResult<PeripheralContribution>{true, "", contribution};
}
