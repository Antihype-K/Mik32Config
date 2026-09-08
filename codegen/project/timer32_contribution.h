//
// Вклад Timer32 в общий сгенерированный проект (см. концепцию в
// codegen/project/peripheral_contribution.h). ТЗ §3.1.5.
//
// Как и Timer16, без флага emit_demo_loop: после HAL_Timer32_Start(_IT)
// периферия работает аппаратно, без участия CPU в теле while(1).
//
#ifndef MIK32_CONFIGURATOR_TIMER32_CONTRIBUTION_H
#define MIK32_CONFIGURATOR_TIMER32_CONTRIBUTION_H

#include "timer32_parser.h"
#include "timer32_knowledge.h"
#include "parse_result.h"
#include "peripheral_contribution.h"

ParseResult<PeripheralContribution> buildTimer32Contribution(
    const Timer32Config& config,
    const Timer32Knowledge& knowledge);

#endif //MIK32_CONFIGURATOR_TIMER32_CONTRIBUTION_H
