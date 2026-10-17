//
// Вклад DAC в общий сгенерированный проект (см. концепцию в
// codegen/project/peripheral_contribution.h). По образцу
// codegen/project/timer32_contribution.h — есть instance (0/1), в отличие
// от RTC/CRC/EEPROM/WDT.
//
#ifndef MIK32_CONFIGURATOR_DAC_CONTRIBUTION_H
#define MIK32_CONFIGURATOR_DAC_CONTRIBUTION_H

#include "dac_parser.h"
#include "dac_knowledge.h"
#include "parse_result.h"
#include "peripheral_contribution.h"

ParseResult<PeripheralContribution> buildDacContribution(
    const DacConfig& config,
    const DacKnowledge& knowledge);

#endif //MIK32_CONFIGURATOR_DAC_CONTRIBUTION_H
