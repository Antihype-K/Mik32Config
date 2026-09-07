//
// Timer16Config / parseTimer16Config — по образцу parser/gpio/gpio_parser.h
// и parser/USART/usart_parser.h. ТЗ §3.1.4.
//
#ifndef MIK32_CONFIGURATOR_TIMER16_PARSER_H
#define MIK32_CONFIGURATOR_TIMER16_PARSER_H
#include <string>
#include <cstdint>
#include "parse_result.h"
#include "json.hpp"

struct Timer16Config
{
    std::string type;
    int instance;
    std::string mode;
    int prescaler;
    uint16_t arr;
    uint16_t cmp;
};

ParseResult<Timer16Config> parseTimer16Config(const std::string& file_name_path);

// Блочная точка входа — см. обоснование в parser/gpio/gpio_parser.h:
// валидирует ОДИН уже извлечённый элемент "peripherals", чтобы
// мультипериферийный пайплайн мог разобрать все блоки этого типа, а не
// только первый, не дублируя правила схемы.
ParseResult<Timer16Config> parseTimer16Block(const nlohmann::json& timer16);

#endif //MIK32_CONFIGURATOR_TIMER16_PARSER_H
