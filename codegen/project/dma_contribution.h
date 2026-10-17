//
// Вклад standalone-канала ПДП (DMA) в общий сгенерированный проект (см.
// концепцию в codegen/project/peripheral_contribution.h). Расширяет DMA за
// пределы UART (docs/implementations/dma.md) на остальные периферийные
// запросы (I2C/SPI/CRYPTO/Timer32/DAC/SPIFI). Общий hdma/DMA_Init()
// по-прежнему собирается в project_codegen.cpp (DMA_CONFIG — один
// физический блок на весь МК), не здесь — см. any_dma_usage.
//
#ifndef MIK32_CONFIGURATOR_DMA_CONTRIBUTION_H
#define MIK32_CONFIGURATOR_DMA_CONTRIBUTION_H

#include "dma_parser.h"
#include "parse_result.h"
#include "peripheral_contribution.h"

ParseResult<PeripheralContribution> buildDmaContribution(const DmaConfig& config);

#endif //MIK32_CONFIGURATOR_DMA_CONTRIBUTION_H
