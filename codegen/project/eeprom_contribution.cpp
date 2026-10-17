#include "eeprom_contribution.h"
#include "contribution_utils.h"
#include <sstream>

namespace
{
    // ТЗ §3.1.8: HSI 32МГц — та же константа, что sys_clk_hz у остальных
    // модулей. mik32_hal.h определяет её же как OSC_SYSTEM_VALUE (строка 11):
    // "Значение частоты основного внешнего источника по умолчанию" — вендорный
    // пример HAL_EEPROM/src/main.c передаёт именно этот макрос в
    // HAL_EEPROM_CalculateTimings, не вычисленное значение.
    constexpr long kSysClkHz = 32000000;
}

// Концепция: EEPROM — единственный глобальный блок (heeprom.Instance =
// EEPROM_REGS, mik32_hal_eeprom.h), без instance в имени хендла/функций —
// тот же приём, что у RTC/CRC. В отличие от RTC, ни HAL_EEPROM_Init, ни
// вендорный пример не вызывают __HAL_PCC_EEPROM_CLK_ENABLE() — доверяем
// реальному коду (CLAUDE.md: «реальный HAL/SDK код выигрывает») и не
// добавляем вызов, которого нет даже в проверенном вендорном примере.
ParseResult<PeripheralContribution> buildEepromContribution(const EepromConfig& config)
{
    const bool is_three_stage = (config.mode == "three_stage");

    PeripheralContribution contribution;
    contribution.label = "EEPROM";
    contribution.init_order = kInitOrderEeprom;
    contribution.init_function_name = "EEPROM_Init";
    contribution.init_c_includes = {"mik32_hal_eeprom.h", "mik32_hal_pcc.h"};
    contribution.public_prototypes = {"void " + contribution.init_function_name + "(void);"};
    contribution.global_definitions = "HAL_EEPROM_HandleTypeDef heeprom;\n";

    std::ostringstream init_c;
    init_c << "/*\n"
              " * EEPROM: mode=" << config.mode << ", ecc_enabled=" << (config.ecc_enabled ? "true" : "false")
           << ", single_error_interrupt_enabled=" << (config.single_error_interrupt_enabled ? "true" : "false") << ".\n"
              " * Тайминги подобраны HAL_EEPROM_CalculateTimings от sys_clk=" << kSysClkHz << "Гц\n"
              " * (ТЗ §3.1.8, тот же sys_clk_hz, что у остальных модулей — mik32_hal.h\n"
              " * называет его OSC_SYSTEM_VALUE).\n"
              " * Только конфигурация контроллера — HAL_EEPROM_Erase/Write/Read на\n"
              " * собственных адресах/данных остаются за пользователем, этот кодоген не\n"
              " * может угадать их наперёд (см. docs/implementations/eeprom.md).\n"
              " */\n"
              "void " << contribution.init_function_name << "(void)\n"
              "{\n"
              "    heeprom.Instance = EEPROM_REGS;\n"
              "    heeprom.Mode = " << (is_three_stage ? "HAL_EEPROM_MODE_THREE_STAGE" : "HAL_EEPROM_MODE_TWO_STAGE") << ";\n"
              "    heeprom.ErrorCorrection = " << (config.ecc_enabled ? "HAL_EEPROM_ECC_ENABLE" : "HAL_EEPROM_ECC_DISABLE") << ";\n"
              "    heeprom.EnableInterrupt = "
           << (config.single_error_interrupt_enabled ? "HAL_EEPROM_SERR_ENABLE" : "HAL_EEPROM_SERR_DISABLE") << ";\n"
              "\n"
              "    HAL_EEPROM_Init(&heeprom);\n"
              "    HAL_EEPROM_CalculateTimings(&heeprom, " << kSysClkHz << ");\n"
              "}\n";
    contribution.init_function_text = init_c.str();

    return ParseResult<PeripheralContribution>{true, "", contribution};
}
