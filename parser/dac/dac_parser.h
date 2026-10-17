//
// DacConfig / parseDacConfig — по образцу parser/timer32/timer32_parser.h
// (есть instance, в отличие от RTC/CRC/EEPROM/WDT). AUDIT_TZ_PERIPHERY_GAPS.md,
// Тир 2. Только конфигурация — HAL_DAC_SetValue с собственным значением
// пользователя остаётся за пользователем (кодоген не может угадать его
// наперёд).
//
#ifndef MIK32_CONFIGURATOR_DAC_PARSER_H
#define MIK32_CONFIGURATOR_DAC_PARSER_H
#include <string>
#include "parse_result.h"
#include "json.hpp"

struct DacConfig
{
    std::string type;
    int instance; // 0 | 1

    int div; // 0-255
    bool ext_ref;
    bool ext_clb;
};

ParseResult<DacConfig> parseDacConfig(const std::string& file_name_path);

// Блочная точка входа — см. обоснование в parser/gpio/gpio_parser.h.
ParseResult<DacConfig> parseDacBlock(const nlohmann::json& dac);

#endif //MIK32_CONFIGURATOR_DAC_PARSER_H
