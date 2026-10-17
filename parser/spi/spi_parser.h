//
// SpiConfig / parseSpiConfig — по образцу parser/i2c/i2c_parser.h,
// parser/gpio/gpio_parser.h. Схема — schemas/spi.json (ТЗ по SPI не содержит
// отдельного раздела, см. docs/implementations/spi.md).
//
#ifndef MIK32_CONFIGURATOR_SPI_PARSER_H
#define MIK32_CONFIGURATOR_SPI_PARSER_H
#include <string>
#include <vector>
#include "parse_result.h"
#include "json.hpp"

struct SpiConfig
{
    std::string type;
    int instance;
    std::string mode;           // "master" / "slave"
    std::string clk_phase;      // "off" / "on"
    std::string clk_polarity;   // "low" / "high"
    int threshold_tx;

    // Только для mode == "master" (иначе пустые/0 — HAL_SPI_Init форсирует
    // ManualCS=OFF для slave, а вендорный пример HAL_SPI_Slave вообще не
    // трогает эти поля, см. schemas/spi.json).
    int baud_rate_div;
    bool manual_cs;
    bool decoder;
    std::string chip_select;    // "cs0".."cs3"

    // Общее для master/slave, необязательное. Пусто — старое поведение
    // (режим опроса). Допустимые значения — "rx_fifo_not_empty",
    // "tx_fifo_not_full", "rx_overflow", "mode_fail" (см. schemas/spi.json).
    std::vector<std::string> interrupts;
};

ParseResult<SpiConfig> parseSpiConfig(const std::string& file_name_path);

// Блочная точка входа — см. обоснование в parser/gpio/gpio_parser.h.
ParseResult<SpiConfig> parseSpiBlock(const nlohmann::json& spi);

#endif //MIK32_CONFIGURATOR_SPI_PARSER_H
