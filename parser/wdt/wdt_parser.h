//
// WdtConfig / parseWdtConfig — по образцу parser/crc/crc_parser.h.
// AUDIT_TZ_PERIPHERY_GAPS.md, Тир 2. WDT — единственный глобальный блок
// (нет instance, см. schemas/wdt.json). Кодоген генерирует только
// HAL_WDT_Init — HAL_WDT_Start/HAL_WDT_Refresh сознательно не вызываются
// автоматически (см. docs/implementations/wdt.md, почему автозапуск опасен).
//
#ifndef MIK32_CONFIGURATOR_WDT_PARSER_H
#define MIK32_CONFIGURATOR_WDT_PARSER_H
#include <cstdint>
#include <string>
#include "parse_result.h"
#include "json.hpp"

struct WdtConfig
{
    std::string type;

    std::string clock_source; // "osc32m" | "hsi32m" | "osc32k" | "lsi32k"
    uint32_t reload_ms;
};

ParseResult<WdtConfig> parseWdtConfig(const std::string& file_name_path);

// Блочная точка входа — см. обоснование в parser/gpio/gpio_parser.h.
ParseResult<WdtConfig> parseWdtBlock(const nlohmann::json& wdt);

#endif //MIK32_CONFIGURATOR_WDT_PARSER_H
