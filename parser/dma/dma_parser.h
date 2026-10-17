//
// DmaConfig / parseDmaConfig — по образцу parser/dac/dac_parser.h (instance,
// в отличие от RTC/CRC/EEPROM/WDT/ADC/CRYPTO/OTP/TSENS/PVD/BusWatchdog).
// instance здесь — номер канала ПДП (0-3), глобальный пул на 4 физических
// канала (shared/periphery/dma_config.h, DMA_CHANNEL_COUNT=4), общий с
// встроенным полем "dma" у UART (schemas/uart.json) — см. schemas/dma.json,
// docs/implementations/dma.md.
//
#ifndef MIK32_CONFIGURATOR_DMA_PARSER_H
#define MIK32_CONFIGURATOR_DMA_PARSER_H
#include <string>
#include "parse_result.h"
#include "json.hpp"

struct DmaConfig
{
    std::string type;
    int instance; // 0-3, номер канала ПДП

    std::string request; // пусто для direction=memory_to_memory
    std::string direction; // peripheral_to_memory | memory_to_peripheral | memory_to_memory
    int buffer_size_bytes;
    std::string element_size; // byte | halfword | word
    std::string priority = "very_high";
    bool irq_enable = false;
};

ParseResult<DmaConfig> parseDmaConfig(const std::string& file_name_path);

// Блочная точка входа — см. обоснование в parser/gpio/gpio_parser.h.
ParseResult<DmaConfig> parseDmaBlock(const nlohmann::json& dma);

#endif //MIK32_CONFIGURATOR_DMA_PARSER_H
