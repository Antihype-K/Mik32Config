//
// UartConfig / parseUsartConfig — по образцу parser/gpio/gpio_parser.h.
//
#ifndef MIK32_CONFIGURATOR_USART_PARSER_H
#define MIK32_CONFIGURATOR_USART_PARSER_H
#include <string>
#include <vector>
#include "parse_result.h"
#include "json.hpp"

struct UartDmaConfig
{
    int channel;               // 0-3 (ТЗ §3.1.3.3)
    std::string direction;     // "tx" | "rx"
    std::string buffer_type;   // "linear" | "circular"
    int buffer_size_bytes;
    std::string element_size;  // "byte" | "halfword" | "word"
    bool increment;
};

struct UartConfig
{
    std::string type;
    int instance;
    int baud_rate;
    int data_bits;
    int stop_bits;
    std::string parity;
    bool flow_control;
    std::string direction;
    // Пусто, если DMA не запрошен (обычный случай) — ТЗ §3.1.3.3.
    std::vector<UartDmaConfig> dma;
};

ParseResult<UartConfig> parseUsartConfig(const std::string& file_name_path);

// Блочная точка входа — см. обоснование в parser/gpio/gpio_parser.h:
// валидирует ОДИН уже извлечённый элемент "peripherals", чтобы
// мультипериферийный пайплайн мог разобрать все блоки этого типа, а не
// только первый, не дублируя правила схемы.
ParseResult<UartConfig> parseUartBlock(const nlohmann::json& uart);

#endif //MIK32_CONFIGURATOR_USART_PARSER_H
