//
// Вклад ADC в общий сгенерированный проект (см. концепцию в
// codegen/project/peripheral_contribution.h). ADC — единственный глобальный
// блок (как DAC), без knowledge_base — канал/пин отображаются в
// фиксированные HAL-константы напрямую (mik32_hal_adc.h), без per-instance
// данных датащита сверх самого заголовка.
//
#ifndef MIK32_CONFIGURATOR_ADC_CONTRIBUTION_H
#define MIK32_CONFIGURATOR_ADC_CONTRIBUTION_H

#include "adc_parser.h"
#include "parse_result.h"
#include "peripheral_contribution.h"

ParseResult<PeripheralContribution> buildAdcContribution(const AdcConfig& config);

#endif //MIK32_CONFIGURATOR_ADC_CONTRIBUTION_H
