//
// SpifiConfig / parseSpifiConfig — по образцу parser/crc/crc_parser.h.
// SPIFI — единственный глобальный блок (нет instance, см. schemas/spifi.json).
// Покрывает только Memory Mode (HAL_SPIFI_MemoryMode_Init,
// mik32_hal_spifi.h/.c) — периферийный режим (HAL_SPIFI_SendCommand на
// произвольных командах/адресах МФП) остаётся за пользователем, кодогену
// нечем угадать последовательность команд конкретной флеш-микросхемы
// наперёд (см. docs/implementations/spifi.md).
//
#ifndef MIK32_CONFIGURATOR_SPIFI_PARSER_H
#define MIK32_CONFIGURATOR_SPIFI_PARSER_H
#include <cstdint>
#include <string>
#include "parse_result.h"
#include "json.hpp"

struct SpifiConfig
{
    std::string type;

    // SPIFI_MemoryModeConfig_HandleTypeDef (mik32_hal_spifi.h).
    bool cache_enabled;
    uint32_t cache_limit;

    // SPIFI_MemoryCommandTypeDef (вложена в .Command выше). InterimData
    // сознательно не вынесена в JSON — HAL_SPIFI_MemoryMode_Init
    // (mik32_hal_spifi.c:19-39) её не использует (ни один регистр не
    // пишется из этого поля), выставлять настраиваемое поле без эффекта
    // на прошивку значило бы вводить пользователя в заблуждение.
    std::string cmd_frame_form;
    std::string cmd_field_form;
    uint8_t cmd_op_code;
    uint8_t cmd_interim_length;
};

ParseResult<SpifiConfig> parseSpifiConfig(const std::string& file_name_path);

// Блочная точка входа — см. обоснование в parser/gpio/gpio_parser.h.
ParseResult<SpifiConfig> parseSpifiBlock(const nlohmann::json& spifi);

// "opcode_3addr" -> "SPIFI_FRAMEFORM_OPCODE_3ADDR" / "all_serial" ->
// "SPIFI_FIELDFORM_ALL_SERIAL" (mik32_hal_spifi.h) — общие для парсера
// (валидация) и codegen/project/spifi_contribution.cpp (генерация текста),
// единственный источник соответствия строка JSON <-> имя макроса HAL.
std::string spifiFrameFormMacro(const std::string& json_value);
std::string spifiFieldFormMacro(const std::string& json_value);

#endif //MIK32_CONFIGURATOR_SPIFI_PARSER_H
