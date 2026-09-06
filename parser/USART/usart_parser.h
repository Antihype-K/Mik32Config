//
// UartConfig / parseUsartConfig — по образцу parser/gpio/gpio_parser.h.
//
#ifndef MIK32_CONFIGURATOR_USART_PARSER_H
#define MIK32_CONFIGURATOR_USART_PARSER_H
#include <string>
#include "parse_result.h"

struct UartConfig
{
    std::string type;
    int instance;
    int baud_rate;
    int data_bits;
    int stop_bits;
    std::string parity;
    bool flow_control;
};

ParseResult<UartConfig> parseUsartConfig(const std::string& file_name_path);

#endif //MIK32_CONFIGURATOR_USART_PARSER_H
