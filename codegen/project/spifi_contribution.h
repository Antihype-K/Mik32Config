//
// Вклад SPIFI в общий сгенерированный проект (см. концепцию в
// codegen/project/peripheral_contribution.h). SPIFI — единственный
// глобальный блок, без instance (см. parser/spifi/spifi_parser.h).
//
#ifndef MIK32_CONFIGURATOR_SPIFI_CONTRIBUTION_H
#define MIK32_CONFIGURATOR_SPIFI_CONTRIBUTION_H

#include "spifi_parser.h"
#include "parse_result.h"
#include "peripheral_contribution.h"

ParseResult<PeripheralContribution> buildSpifiContribution(const SpifiConfig& config);

#endif //MIK32_CONFIGURATOR_SPIFI_CONTRIBUTION_H
