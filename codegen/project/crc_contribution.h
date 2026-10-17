//
// Вклад CRC в общий сгенерированный проект (см. концепцию в
// codegen/project/peripheral_contribution.h). CRC — единственный глобальный
// блок, без instance (см. parser/crc/crc_parser.h). Без knowledge_base —
// в отличие от RTC (weekday/month, 7/12 реальных значений), у CRC нет
// per-instance таблицы: оба булевых поля отображаются в ровно два
// документированных HAL-макроса каждое, это не данные датащита, а
// фиксированное имя константы (см. mik32_hal_crc32.h).
//
#ifndef MIK32_CONFIGURATOR_CRC_CONTRIBUTION_H
#define MIK32_CONFIGURATOR_CRC_CONTRIBUTION_H

#include "crc_parser.h"
#include "parse_result.h"
#include "peripheral_contribution.h"

ParseResult<PeripheralContribution> buildCrcContribution(const CrcConfig& config);

#endif //MIK32_CONFIGURATOR_CRC_CONTRIBUTION_H
