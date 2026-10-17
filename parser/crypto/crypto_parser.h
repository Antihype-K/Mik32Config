//
// CryptoConfig / parseCryptoConfig — по образцу parser/crc/crc_parser.h.
// AUDIT_TZ_PERIPHERY_GAPS.md, Тир 2. CRYPTO — единственный глобальный блок
// (нет instance, см. schemas/crypto.json). Сознательно без ключа/IV —
// секретные данные времени выполнения, не место для них в JSON-конфиге
// (см. schemas/crypto.json, "_comment_no_key_no_iv").
//
#ifndef MIK32_CONFIGURATOR_CRYPTO_PARSER_H
#define MIK32_CONFIGURATOR_CRYPTO_PARSER_H
#include <string>
#include "parse_result.h"
#include "json.hpp"

struct CryptoConfig
{
    std::string type;

    std::string algorithm;   // "kuznechik" | "magma" | "aes"
    std::string cipher_mode; // "ecb" | "cbc" | "ctr"
    std::string swap_mode = "none";   // "none" | "halfword" | "byte" | "bit"
    std::string order_mode = "lsw";   // "lsw" | "msw"
};

ParseResult<CryptoConfig> parseCryptoConfig(const std::string& file_name_path);

// Блочная точка входа — см. обоснование в parser/gpio/gpio_parser.h.
ParseResult<CryptoConfig> parseCryptoBlock(const nlohmann::json& crypto);

#endif //MIK32_CONFIGURATOR_CRYPTO_PARSER_H
