//
// Вклад Bus Watchdog (сторожевой таймер шины EEPROM/SPIFI/DOM3) в общий
// сгенерированный проект (см. концепцию в codegen/project/peripheral_contribution.h).
// Единственный глобальный блок, без knowledge_base. Нет HAL-обёртки
// конфигурации — только __HAL_PCC_WDT_BUS_CLK_ENABLE, сама настройка
// TIMEOUT/ENABLE — прямая запись (shared/periphery/wdt_bus.h),
// AUDIT_TZ_PERIPHERY_GAPS.md, Тир 3.
//
#ifndef MIK32_CONFIGURATOR_BUS_WATCHDOG_CONTRIBUTION_H
#define MIK32_CONFIGURATOR_BUS_WATCHDOG_CONTRIBUTION_H

#include "bus_watchdog_parser.h"
#include "parse_result.h"
#include "peripheral_contribution.h"

ParseResult<PeripheralContribution> buildBusWatchdogContribution(const BusWatchdogConfig& config);

#endif //MIK32_CONFIGURATOR_BUS_WATCHDOG_CONTRIBUTION_H
