//
// Вклад EEPROM-контроллера в общий сгенерированный проект (см. концепцию в
// codegen/project/peripheral_contribution.h). EEPROM — единственный
// глобальный блок, без instance (см. parser/eeprom/eeprom_parser.h). Без
// knowledge_base — тот же случай, что у CRC: оба enum/bool поля отображаются
// в фиксированные HAL-константы напрямую, без per-instance данных датащита.
//
#ifndef MIK32_CONFIGURATOR_EEPROM_CONTRIBUTION_H
#define MIK32_CONFIGURATOR_EEPROM_CONTRIBUTION_H

#include "eeprom_parser.h"
#include "parse_result.h"
#include "peripheral_contribution.h"

ParseResult<PeripheralContribution> buildEepromContribution(const EepromConfig& config);

#endif //MIK32_CONFIGURATOR_EEPROM_CONTRIBUTION_H
