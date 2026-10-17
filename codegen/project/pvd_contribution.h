//
// Вклад PVD (монитор напряжения VCC/AVCC) в общий сгенерированный проект
// (см. концепцию в codegen/project/peripheral_contribution.h). PVD —
// единственный глобальный блок (как ADC/DAC), без knowledge_base. В отличие
// от ADC/DAC здесь нет HAL_PVD_Init — есть только макрос включения
// тактирования (__HAL_PCC_PVD_CONTROL_CLK_ENABLE), сама настройка регистров
// CONFIG/DPF_VALUE — прямая запись (shared/periphery/pvd_control.h),
// AUDIT_TZ_PERIPHERY_GAPS.md, Тир 3.
//
#ifndef MIK32_CONFIGURATOR_PVD_CONTRIBUTION_H
#define MIK32_CONFIGURATOR_PVD_CONTRIBUTION_H

#include "pvd_parser.h"
#include "parse_result.h"
#include "peripheral_contribution.h"

ParseResult<PeripheralContribution> buildPvdContribution(const PvdConfig& config);

#endif //MIK32_CONFIGURATOR_PVD_CONTRIBUTION_H
