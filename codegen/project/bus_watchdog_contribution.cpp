#include "bus_watchdog_contribution.h"
#include "contribution_utils.h"
#include <sstream>

// Концепция: Bus Watchdog — единственный глобальный блок (WDT_BUS,
// wdt_bus.h), без HAL-обёртки инициализации (AUDIT_TZ_PERIPHERY_GAPS.md,
// Тир 3) — только макрос включения тактирования __HAL_PCC_WDT_BUS_CLK_ENABLE.
// По умолчанию все три линии выключены (как системный WDT — кодоген не
// включает сторожевой таймер без явного запроса пользователя в JSON).
ParseResult<PeripheralContribution> buildBusWatchdogContribution(const BusWatchdogConfig& config)
{
    PeripheralContribution contribution;
    contribution.label = "BusWatchdog";
    contribution.init_order = kInitOrderBusWatchdog;
    contribution.init_function_name = "BusWatchdog_Init";
    contribution.init_c_includes = {"wdt_bus.h", "mik32_hal_pcc.h"};
    contribution.public_prototypes = {"void " + contribution.init_function_name + "(void);"};

    std::ostringstream init_c;
    init_c << "/*\n"
              " * Bus Watchdog: eeprom_enabled=" << (config.eeprom_enabled ? "true" : "false")
           << ", spifi_enabled=" << (config.spifi_enabled ? "true" : "false")
           << ", dom3_enabled=" << (config.dom3_enabled ? "true" : "false")
           << ", timeout=" << config.timeout << ".\n"
              " * WDT_BUS->TIMEOUT — один общий регистр таймаута на все три линии\n"
              " * (wdt_bus.h не содержит отдельных TIMEOUT per-линия).\n"
              " * Сброс по срабатыванию — через контроллер прерываний EPIC\n"
              " * (HAL_EPIC_WDT_DOM3/SPIFI/EEPROM_MASK, mik32_hal_irq.h), не настраивается\n"
              " * здесь — см. AUDIT_TZ_PERIPHERY_GAPS.md.\n"
              " */\n"
              "void " << contribution.init_function_name << "(void)\n"
              "{\n"
              "    __HAL_PCC_WDT_BUS_CLK_ENABLE();\n"
              "\n"
              "    WDT_BUS->TIMEOUT = " << config.timeout << "u;\n"
              "\n"
              "    uint32_t bus_watchdog_enable = 0;\n";
    if (config.eeprom_enabled)
    {
        init_c << "    bus_watchdog_enable |= (1 << WDT_BUS_EEPROM_S);\n";
    }
    if (config.spifi_enabled)
    {
        init_c << "    bus_watchdog_enable |= (1 << WDT_BUS_SPIFI_S);\n";
    }
    if (config.dom3_enabled)
    {
        init_c << "    bus_watchdog_enable |= (1 << WDT_BUS_DOM3_S);\n";
    }
    init_c << "    WDT_BUS->ENABLE = bus_watchdog_enable;\n"
              "}\n";
    contribution.init_function_text = init_c.str();

    return ParseResult<PeripheralContribution>{true, "", contribution};
}
