//
// CrcConfig / parseCrcConfig — по образцу parser/rtc/rtc_parser.h.
// AUDIT_TZ_PERIPHERY_GAPS.md, Тир 2. CRC — единственный глобальный блок
// (нет instance, см. schemas/crc.json). Только конфигурация алгоритма —
// HAL_CRC_WriteData/WriteData32/ReadCRC на произвольных пользовательских
// данных остаются за пользователем, кодоген их не вызывает (нечем угадать
// данные наперёд).
//
#ifndef MIK32_CONFIGURATOR_CRC_PARSER_H
#define MIK32_CONFIGURATOR_CRC_PARSER_H
#include <cstdint>
#include <string>
#include "parse_result.h"
#include "json.hpp"

struct CrcConfig
{
    std::string type;

    // mik32_hal_crc32.h, CRC_HandleTypeDef.
    uint32_t poly;
    uint32_t init;
    bool input_reverse;
    bool output_reverse;
    bool output_inversion;

    // Запрошенный "preset" (для комментария в сгенерированном коде — "что
    // запрошено") — пусто, если заданы явные поля. См. schemas/crc.json.
    std::string requested_preset;
};

ParseResult<CrcConfig> parseCrcConfig(const std::string& file_name_path);

// Блочная точка входа — см. обоснование в parser/gpio/gpio_parser.h.
ParseResult<CrcConfig> parseCrcBlock(const nlohmann::json& crc);

#endif //MIK32_CONFIGURATOR_CRC_PARSER_H
