//
// TsensConfig / parseTsensConfig — по образцу parser/dac/dac_parser.h.
// AUDIT_TZ_PERIPHERY_GAPS.md, Тир 2. TSENS — единственный глобальный блок
// (нет instance). Кодоген генерирует настройку тактирования/порогов/
// прерываний — HAL_TSENS_SingleStart/ContinuousStart остаются за
// пользователем (см. schemas/tsens.json, "_comment_no_auto_start").
//
#ifndef MIK32_CONFIGURATOR_TSENS_PARSER_H
#define MIK32_CONFIGURATOR_TSENS_PARSER_H
#include <string>
#include <vector>
#include "parse_result.h"
#include "json.hpp"

struct TsensConfig
{
    std::string type;

    std::string clock_source; // "sys_clk"|"hclk"|"osc32m"|"hsi32m"|"osc32k"|"lsi32k"
    uint32_t frequency_hz;

    bool has_low_threshold = false;
    int low_threshold_celsius = 0;
    bool has_high_threshold = false;
    int high_threshold_celsius = 0;

    // Подмножество {"low","high","eoc"}.
    std::vector<std::string> interrupts;
};

ParseResult<TsensConfig> parseTsensConfig(const std::string& file_name_path);

// Блочная точка входа — см. обоснование в parser/gpio/gpio_parser.h.
ParseResult<TsensConfig> parseTsensBlock(const nlohmann::json& tsens);

#endif //MIK32_CONFIGURATOR_TSENS_PARSER_H
