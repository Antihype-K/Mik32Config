//
// OtpConfig / parseOtpConfig — по образцу parser/crc/crc_parser.h. AUDIT_TZ_
// PERIPHERY_GAPS.md, Тир 2. OTP — единственный глобальный блок (нет
// instance). Кодоген генерирует только HAL_OTP_Init — запись (HAL_OTP_Write*)
// НИКОГДА не генерируется автоматически: OTP физически необратима (прожигает
// биты), см. schemas/otp.json, "_comment_write_is_destructive".
//
#ifndef MIK32_CONFIGURATOR_OTP_PARSER_H
#define MIK32_CONFIGURATOR_OTP_PARSER_H
#include <string>
#include "parse_result.h"
#include "json.hpp"

struct OtpConfig
{
    std::string type;

    std::string read_mode; // "two_stages" | "three_stages"
};

ParseResult<OtpConfig> parseOtpConfig(const std::string& file_name_path);

// Блочная точка входа — см. обоснование в parser/gpio/gpio_parser.h.
ParseResult<OtpConfig> parseOtpBlock(const nlohmann::json& otp);

#endif //MIK32_CONFIGURATOR_OTP_PARSER_H
