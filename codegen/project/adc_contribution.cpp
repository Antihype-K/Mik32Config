#include "adc_contribution.h"
#include "contribution_utils.h"
#include <sstream>

namespace
{
    // mik32_hal_adc.h: ADC_CHANNEL0..7 и соответствующие физические выводы
    // (комментарий "Канал АЦПx (PORT_y_z)") — только для комментария в коде.
    struct ChannelPin { int gpio_instance; int pin; };
    const ChannelPin kChannelPins[8] = {
        {1, 5},  // ADC_CHANNEL0 - PORT_1_5
        {1, 7},  // ADC_CHANNEL1 - PORT_1_7
        {0, 2},  // ADC_CHANNEL2 - PORT_0_2
        {0, 4},  // ADC_CHANNEL3 - PORT_0_4
        {0, 7},  // ADC_CHANNEL4 - PORT_0_7
        {0, 9},  // ADC_CHANNEL5 - PORT_0_9
        {0, 11}, // ADC_CHANNEL6 - PORT_0_11
        {0, 13}, // ADC_CHANNEL7 - PORT_0_13
    };
}

// Концепция: ADC — единственный глобальный блок (hadc.Instance = ANALOG_REG,
// mik32_hal_adc.h), как DAC. HAL_ADC_Init сама вызывает HAL_ADC_MspInit
// (clock + GPIO pin mux) — тот же "безопасный" случай, что у CRC/EEPROM/WDT/DAC.
// Переключение каналов в рантайме (ADC_SEL_CHANNEL/HAL_ADC_SINGLE_AND_SET_CH)
// остаётся за пользователем — кодоген конфигурирует только начальный канал.
ParseResult<PeripheralContribution> buildAdcContribution(const AdcConfig& config)
{
    const ChannelPin& pin = kChannelPins[config.channel];
    const bool adc_ref_pin_used = config.ext_ref && config.ext_clb;

    PeripheralContribution contribution;
    contribution.label = "ADC";
    contribution.init_order = kInitOrderAdc;
    contribution.init_function_name = "ADC_Init";
    contribution.init_c_includes = {"mik32_hal_adc.h", "mik32_hal_pcc.h"};
    contribution.public_prototypes = {"void " + contribution.init_function_name + "(void);"};
    contribution.global_definitions = "ADC_HandleTypeDef hadc;\n";

    std::ostringstream init_c;
    init_c << "/*\n"
              " * ADC: начальный channel=" << config.channel << ", ext_ref=" << (config.ext_ref ? "true" : "false")
           << ", ext_clb=" << (config.ext_clb ? "true" : "false") << ".\n"
              " * Вывод канала: GPIO_" << pin.gpio_instance << "/PIN_" << pin.pin
           << " (mik32_hal_adc.h, жёстко закреплён за channel).\n";
    if (adc_ref_pin_used)
    {
        init_c << " * Внешний опорный вывод ADC_REF: GPIO_1/PIN_11 (занят, т.к. ext_ref=true и ext_clb=true одновременно).\n";
    }
    init_c << " * Переключение на другие каналы в рантайме (ADC_SEL_CHANNEL/\n"
              " * HAL_ADC_SINGLE_AND_SET_CH) остаётся за пользователем — этот кодоген\n"
              " * конфигурирует только начальный канал (см. docs/implementations/adc.md).\n"
              " */\n"
              "void " << contribution.init_function_name << "(void)\n"
              "{\n"
              "    hadc.Instance = ANALOG_REG;\n"
              "\n"
              "    hadc.Init.Sel = ADC_CHANNEL" << config.channel << ";\n"
              "    hadc.Init.EXTRef = " << (config.ext_ref ? "ADC_EXTREF_ON" : "ADC_EXTREF_OFF") << ";\n"
              "    hadc.Init.EXTClb = " << (config.ext_clb ? "ADC_EXTCLB_ADCREF" : "ADC_EXTCLB_CLBREF") << ";\n"
              "\n"
              "    HAL_ADC_Init(&hadc);\n"
              "}\n";
    contribution.init_function_text = init_c.str();

    return ParseResult<PeripheralContribution>{true, "", contribution};
}
