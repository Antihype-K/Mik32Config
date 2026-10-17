//
// EepromConfig / parseEepromConfig — по образцу parser/crc/crc_parser.h.
// AUDIT_TZ_PERIPHERY_GAPS.md, Тир 2. EEPROM-контроллер — единственный
// глобальный блок (нет instance, см. schemas/eeprom.json). Только
// конфигурация режима/ECC/прерывания — HAL_EEPROM_Erase/Write/Read на
// произвольных пользовательских адресах/данных остаются за пользователем.
//
#ifndef MIK32_CONFIGURATOR_EEPROM_PARSER_H
#define MIK32_CONFIGURATOR_EEPROM_PARSER_H
#include <string>
#include "parse_result.h"
#include "json.hpp"

struct EepromConfig
{
    std::string type;

    std::string mode; // "two_stage" | "three_stage"
    bool ecc_enabled = true;
    bool single_error_interrupt_enabled = false;
};

ParseResult<EepromConfig> parseEepromConfig(const std::string& file_name_path);

// Блочная точка входа — см. обоснование в parser/gpio/gpio_parser.h.
ParseResult<EepromConfig> parseEepromBlock(const nlohmann::json& eeprom);

#endif //MIK32_CONFIGURATOR_EEPROM_PARSER_H
