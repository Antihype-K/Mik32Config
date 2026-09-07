//
// Вклад Timer16 в общий сгенерированный проект (см. концепцию в
// codegen/project/peripheral_contribution.h). ТЗ §3.1.4.
//
// Отличия от удалённого codegen/timer16/: на выходе кусочки, а не три
// файла, и хендл называется htimer16_<instance> — при трёх блоках timer16 в
// одном проекте (ТЗ §3.1.4: «МК МИК32 «Амур» содержит 3 модуля Timer16»)
// один общий "Timer16_HandleTypeDef htimer16" был бы переопределением
// символа. Прерывания Timer16 (Start*_IT, trap_handler) по-прежнему вне
// скоупа.
//
#ifndef MIK32_CONFIGURATOR_TIMER16_CONTRIBUTION_H
#define MIK32_CONFIGURATOR_TIMER16_CONTRIBUTION_H

#include "timer16_parser.h"
#include "timer16_knowledge.h"
#include "parse_result.h"
#include "peripheral_contribution.h"

// Флага emit_demo_loop нет намеренно: Timer16 после запуска работает
// аппаратно, без участия CPU (HAL_Timer16_StartPWM вызывается прямо в
// init-функции), поэтому демонстрировать его телом while(1) нечем — любое
// «демо» здесь было бы пустой тратой тактов.
ParseResult<PeripheralContribution> buildTimer16Contribution(
    const Timer16Config& config,
    const Timer16Knowledge& knowledge);

#endif //MIK32_CONFIGURATOR_TIMER16_CONTRIBUTION_H
