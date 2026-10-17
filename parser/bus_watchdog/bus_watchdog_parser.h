//
// BusWatchdogConfig / parseBusWatchdogConfig — по образцу parser/adc/adc_parser.h.
// AUDIT_TZ_PERIPHERY_GAPS.md, Тир 3 (регистровая работа, нет HAL-обёртки —
// только __HAL_PCC_WDT_BUS_CLK_ENABLE). Единственный глобальный блок, три
// независимые линии (EEPROM/SPIFI/DOM3) настраиваются полями, не instance
// (см. schemas/bus_watchdog.json).
//
#ifndef MIK32_CONFIGURATOR_BUS_WATCHDOG_PARSER_H
#define MIK32_CONFIGURATOR_BUS_WATCHDOG_PARSER_H
#include <cstdint>
#include <string>
#include "parse_result.h"
#include "json.hpp"

struct BusWatchdogConfig
{
    std::string type;

    bool eeprom_enabled = false;
    bool spifi_enabled = false;
    bool dom3_enabled = false;
    uint32_t timeout = 0; // WDT_BUS->TIMEOUT, общий регистр на все линии
};

ParseResult<BusWatchdogConfig> parseBusWatchdogConfig(const std::string& file_name_path);

// Блочная точка входа — см. обоснование в parser/gpio/gpio_parser.h.
ParseResult<BusWatchdogConfig> parseBusWatchdogBlock(const nlohmann::json& bus_watchdog);

#endif //MIK32_CONFIGURATOR_BUS_WATCHDOG_PARSER_H
