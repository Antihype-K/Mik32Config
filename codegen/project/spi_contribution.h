//
// Вклад SPI в общий сгенерированный проект (см. концепцию в
// codegen/project/peripheral_contribution.h). По образцу
// codegen/project/i2c_contribution.h.
//
// ТЗ по SPI молчит (см. docs/implementations/spi.md); источники — реальный
// HAL (mik32_hal_spi.h/.c) и datasheet (docs/MIK32_REFERENCE.md, §4).
//
#ifndef MIK32_CONFIGURATOR_SPI_CONTRIBUTION_H
#define MIK32_CONFIGURATOR_SPI_CONTRIBUTION_H

#include "spi_parser.h"
#include "spi_knowledge.h"
#include "parse_result.h"
#include "peripheral_contribution.h"

ParseResult<PeripheralContribution> buildSpiContribution(
    const SpiConfig& config,
    const SpiKnowledge& knowledge);

#endif //MIK32_CONFIGURATOR_SPI_CONTRIBUTION_H
