//
// AdcConfig / parseAdcConfig — по образцу parser/dac/dac_parser.h. AUDIT_TZ_
// PERIPHERY_GAPS.md, Тир 2. ADC — единственный глобальный блок (как DAC),
// нет instance (см. schemas/adc.json). Только конфигурация начального
// канала/ИОН — переключение каналов в рантайме остаётся за пользователем.
//
#ifndef MIK32_CONFIGURATOR_ADC_PARSER_H
#define MIK32_CONFIGURATOR_ADC_PARSER_H
#include <string>
#include "parse_result.h"
#include "json.hpp"

struct AdcConfig
{
    std::string type;

    int channel; // 0-7, ADC_InitTypeDef.Sel
    bool ext_ref;
    bool ext_clb;
    bool interrupt_enabled = false; // необязательное, см. schemas/adc.json
};

ParseResult<AdcConfig> parseAdcConfig(const std::string& file_name_path);

// Блочная точка входа — см. обоснование в parser/gpio/gpio_parser.h.
ParseResult<AdcConfig> parseAdcBlock(const nlohmann::json& adc);

#endif //MIK32_CONFIGURATOR_ADC_PARSER_H
