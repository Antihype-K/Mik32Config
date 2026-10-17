#include "dac_contribution.h"
#include "contribution_utils.h"
#include <sstream>

// Концепция: DAC — два физических канала (instance 0/1), в отличие от
// RTC/CRC/EEPROM/WDT. HAL_DAC_Init сама вызывает HAL_DAC_MspInit (clock +
// GPIO pin mux) — тот же "безопасный" случай, что у CRC/EEPROM/WDT.
ParseResult<PeripheralContribution> buildDacContribution(
    const DacConfig& config,
    const DacKnowledge& knowledge)
{
    const std::string* dac_handle = findOrNull(knowledge.instance_handle, config.instance);
    const DacKnowledge::FixedPin* fixed_pin = findOrNull(knowledge.instance_fixed_pin, config.instance);
    if (!dac_handle || !fixed_pin)
    {
        return ParseResult<PeripheralContribution>{
            false, "DacConfig ссылается на instance, отсутствующий в knowledge_base/dac.json", std::nullopt};
    }

    const std::string handle_name = "hdac" + std::to_string(config.instance);
    const bool dac_ref_pin_used = config.ext_ref && config.ext_clb;

    PeripheralContribution contribution;
    contribution.label = "DAC_" + std::to_string(config.instance);
    contribution.init_order = kInitOrderDac;
    contribution.init_function_name = "DAC_" + std::to_string(config.instance) + "_Init";
    contribution.init_c_includes = {"mik32_hal_dac.h", "mik32_hal_pcc.h"};
    contribution.public_prototypes = {"void " + contribution.init_function_name + "(void);"};
    contribution.global_definitions = "DAC_HandleTypeDef " + handle_name + ";\n";

    std::ostringstream init_c;
    init_c << "/*\n"
              " * " << *dac_handle << ": div=" << config.div << ", ext_ref=" << (config.ext_ref ? "true" : "false")
           << ", ext_clb=" << (config.ext_clb ? "true" : "false") << ".\n"
              " * Вывод: GPIO_" << fixed_pin->gpio_instance << "/PIN_" << fixed_pin->pin
           << " (mik32_hal_dac.h, жёстко закреплён за instance).\n";
    if (dac_ref_pin_used)
    {
        init_c << " * Внешний опорный вывод DAC_REF: GPIO_" << knowledge.dac_ref_pin.gpio_instance
               << "/PIN_" << knowledge.dac_ref_pin.pin
               << " (занят, т.к. ext_ref=true и ext_clb=true одновременно).\n";
    }
    init_c << " * Только конфигурация — HAL_DAC_SetValue с собственным значением\n"
              " * пользователя остаётся за пользователем (см. docs/implementations/dac.md).\n"
              " */\n"
              "void " << contribution.init_function_name << "(void)\n"
              "{\n"
              "    " << handle_name << ".Instance = ANALOG_REG;\n"
              "    " << handle_name << ".Instance_dac = " << *dac_handle << ";\n"
              "\n"
              "    " << handle_name << ".Init.DIV = " << config.div << ";\n"
              "    " << handle_name << ".Init.EXTRef = " << (config.ext_ref ? "DAC_EXTREF_ON" : "DAC_EXTREF_OFF") << ";\n"
              "    " << handle_name << ".Init.EXTClb = " << (config.ext_clb ? "DAC_EXTCLB_DACREF" : "DAC_EXTCLB_CLBREF") << ";\n"
              "\n"
              "    HAL_DAC_Init(&" << handle_name << ");\n"
              "}\n";
    contribution.init_function_text = init_c.str();

    return ParseResult<PeripheralContribution>{true, "", contribution};
}
