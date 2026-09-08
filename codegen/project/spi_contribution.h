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

// console_available — есть ли в проекте UART, назначенный консолью xprintf
// (см. buildUartContribution). В отличие от I2C-сканера SPI-обмен не
// требует консоли для того, чтобы быть наблюдаемым (сигналы видны на
// осциллографе/логическом анализаторе без единой строки в UART), поэтому
// её отсутствие не блокирует кодоген — просто demo-цикл не печатает байты.
//
// emit_demo_loop — общий флаг проекта (см. i2c_contribution.h) — при false
// SPI генерирует только инициализацию.
ParseResult<PeripheralContribution> buildSpiContribution(
    const SpiConfig& config,
    const SpiKnowledge& knowledge,
    bool console_available,
    bool emit_demo_loop);

#endif //MIK32_CONFIGURATOR_SPI_CONTRIBUTION_H
