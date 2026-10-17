//
// Вклад WDT в общий сгенерированный проект (см. концепцию в
// codegen/project/peripheral_contribution.h). WDT — единственный глобальный
// блок, без instance (см. parser/wdt/wdt_parser.h). Без knowledge_base —
// четыре значения clock_source отображаются в фиксированные HAL-константы
// напрямую, без per-instance данных датащита.
//
// ВАЖНО: генерирует только HAL_WDT_Init — HAL_WDT_Start/HAL_WDT_Refresh
// сознательно не вызываются (см. docs/implementations/wdt.md) — автозапуск
// без гарантии регулярного HAL_WDT_Refresh в пользовательском коде означал
// бы безусловный циклический сброс платы сразу после старта.
//
#ifndef MIK32_CONFIGURATOR_WDT_CONTRIBUTION_H
#define MIK32_CONFIGURATOR_WDT_CONTRIBUTION_H

#include "wdt_parser.h"
#include "parse_result.h"
#include "peripheral_contribution.h"

ParseResult<PeripheralContribution> buildWdtContribution(const WdtConfig& config);

#endif //MIK32_CONFIGURATOR_WDT_CONTRIBUTION_H
