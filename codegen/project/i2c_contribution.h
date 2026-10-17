//
// Вклад I2C в общий сгенерированный проект (см. концепцию в
// codegen/project/peripheral_contribution.h).
//
// Отличия от удалённого codegen/i2c/:
//  - на выходе кусочки, а не три файла;
//  - хендл называется hi2c_<instance>, функции — I2C_<instance>_Init /
//    I2C_<instance>_Reset: в МИК32 два блока I2C, и оба могут быть в одном
//    конфиге;
//  - логика UART здесь больше НЕ дублируется — UART настраивает себя сам.
//
// ТЗ по I2C молчит (см. docs/implementations/i2c.md); источники — черновик
// архитектуры §5.4 и реальный HAL.
//
#ifndef MIK32_CONFIGURATOR_I2C_CONTRIBUTION_H
#define MIK32_CONFIGURATOR_I2C_CONTRIBUTION_H

#include "i2c_parser.h"
#include "i2c_knowledge.h"
#include "parse_result.h"
#include "peripheral_contribution.h"

ParseResult<PeripheralContribution> buildI2cContribution(
    const I2cConfig& config,
    const I2cKnowledge& knowledge);

#endif //MIK32_CONFIGURATOR_I2C_CONTRIBUTION_H
