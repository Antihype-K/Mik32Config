//
// PvdConfig / parsePvdConfig — по образцу parser/adc/adc_parser.h.
// AUDIT_TZ_PERIPHERY_GAPS.md, Тир 3 (регистровая работа, нет HAL-обёртки
// конфигурации — только __HAL_PCC_PVD_CONTROL_CLK_ENABLE). PVD — единственный
// глобальный блок (как ADC/DAC), нет instance (см. schemas/pvd.json).
//
// Сознательно не принимает поля "сброс по порогу" (CONFIG.NRESETU/NRESETO) —
// см. _comment_no_reset_fields в schemas/pvd.json и docs/implementations/pvd.md.
//
#ifndef MIK32_CONFIGURATOR_PVD_PARSER_H
#define MIK32_CONFIGURATOR_PVD_PARSER_H
#include <string>
#include "parse_result.h"
#include "json.hpp"

struct PvdConfig
{
    std::string type;

    int under_threshold;  // 0-15, CONFIG.UNDER_THRESH
    int over_threshold;   // 0-15, CONFIG.OVER_THRESH
    int digital_filter_value; // 0-65535, DPF_VALUE
    bool enable_reference_calibration = false; // CONFIG.EN_VREFCLB
};

ParseResult<PvdConfig> parsePvdConfig(const std::string& file_name_path);

// Блочная точка входа — см. обоснование в parser/gpio/gpio_parser.h.
ParseResult<PvdConfig> parsePvdBlock(const nlohmann::json& pvd);

#endif //MIK32_CONFIGURATOR_PVD_PARSER_H
