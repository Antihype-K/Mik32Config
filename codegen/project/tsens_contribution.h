//
// Вклад TSENS (датчик температуры) в общий сгенерированный проект (см.
// концепцию в codegen/project/peripheral_contribution.h). Единственный
// глобальный блок, без knowledge_base. HAL_TSENS_SingleStart/ContinuousStart
// остаются за пользователем (см. parser/tsens/tsens_parser.h).
//
#ifndef MIK32_CONFIGURATOR_TSENS_CONTRIBUTION_H
#define MIK32_CONFIGURATOR_TSENS_CONTRIBUTION_H

#include "tsens_parser.h"
#include "parse_result.h"
#include "peripheral_contribution.h"

ParseResult<PeripheralContribution> buildTsensContribution(const TsensConfig& config);

#endif //MIK32_CONFIGURATOR_TSENS_CONTRIBUTION_H
