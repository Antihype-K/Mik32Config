//
// Типовой маппер reverse-import для TSENS (датчик температуры): превращает
// RawInitBlock, собранный вокруг HAL_TSENS_Init, в JSON-блок формы
// schemas/tsens.json. Пороги (low/high_threshold_celsius) приходят не из
// присваиваний полей, а из вызовов HAL_TSENS_SetLowThreshold/SetHiThreshold
// (trailing_calls) — см. docs/examples/mik32-examples/HAL_TSENS_IRQ/src/
// main.c:47. Прерывания — из __HAL_TSENS_IRQ_ENABLE_LOWIRQ/HIIRQ/EOC
// (макросы, но сканер не различает макрос и функцию — видит как обычный
// вызов, см. reverse_import_scanner.cpp::findGenericCalls).
//
// TSENS — единственный глобальный блок (как ADC/DAC/RTC/CRC).
//
#ifndef MIK32_CONFIGURATOR_TSENS_REVERSE_MAPPER_H
#define MIK32_CONFIGURATOR_TSENS_REVERSE_MAPPER_H

#include <optional>

#include "mapped_block.h"
#include "reverse_import_scanner.h"

namespace reverse_import
{

// block.handle_type должен быть "TSENS_HandleTypeDef", иначе nullopt.
std::optional<MappedBlock> mapTsensBlock(const RawInitBlock& block);

} // namespace reverse_import

#endif //MIK32_CONFIGURATOR_TSENS_REVERSE_MAPPER_H
