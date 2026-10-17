//
// Вклад RTC в общий сгенерированный проект (см. концепцию в
// codegen/project/peripheral_contribution.h). По образцу
// codegen/project/timer32_contribution.h. RTC — единственный глобальный
// блок, без instance (см. parser/rtc/rtc_parser.h).
//
#ifndef MIK32_CONFIGURATOR_RTC_CONTRIBUTION_H
#define MIK32_CONFIGURATOR_RTC_CONTRIBUTION_H

#include "rtc_parser.h"
#include "rtc_knowledge.h"
#include "parse_result.h"
#include "peripheral_contribution.h"

ParseResult<PeripheralContribution> buildRtcContribution(
    const RtcConfig& config,
    const RtcKnowledge& knowledge);

#endif //MIK32_CONFIGURATOR_RTC_CONTRIBUTION_H
