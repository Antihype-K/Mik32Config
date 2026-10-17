#include "crypto_contribution.h"
#include "contribution_utils.h"
#include <sstream>

namespace
{
    std::string algorithmMacro(const std::string& value)
    {
        if (value == "kuznechik") return "CRYPTO_ALG_KUZNECHIK";
        if (value == "magma") return "CRYPTO_ALG_MAGMA";
        return "CRYPTO_ALG_AES";
    }
    std::string cipherModeMacro(const std::string& value)
    {
        if (value == "ecb") return "CRYPTO_CIPHER_MODE_ECB";
        if (value == "cbc") return "CRYPTO_CIPHER_MODE_CBC";
        return "CRYPTO_CIPHER_MODE_CTR";
    }
    std::string swapModeMacro(const std::string& value)
    {
        if (value == "none") return "CRYPTO_SWAP_MODE_NONE";
        if (value == "halfword") return "CRYPTO_SWAP_MODE_HALFWORD";
        if (value == "byte") return "CRYPTO_SWAP_MODE_BYTE";
        return "CRYPTO_SWAP_MODE_BIT";
    }
    std::string orderModeMacro(const std::string& value)
    {
        return value == "msw" ? "CRYPTO_ORDER_MODE_MSW" : "CRYPTO_ORDER_MODE_LSW";
    }
}

// Концепция: CRYPTO — единственный глобальный блок (hcrypto.Instance = CRYPTO,
// mik32_memory_map.h), как RTC/CRC/EEPROM/WDT. HAL_Crypto_Init сама вызывает
// HAL_CRYPTO_MspInit (clock-enable) — безопасный случай, как у CRC/EEPROM/WDT/DAC/ADC.
// Ключ (HAL_Crypto_SetKey) и вектор инициализации (HAL_Crypto_SetIV) — секретные
// данные времени выполнения, сознательно НЕ генерируются (см. parser/crypto/crypto_parser.h).
ParseResult<PeripheralContribution> buildCryptoContribution(const CryptoConfig& config)
{
    PeripheralContribution contribution;
    contribution.label = "CRYPTO";
    contribution.init_order = kInitOrderCrypto;
    contribution.init_function_name = "CRYPTO_Init";
    contribution.init_c_includes = {"mik32_hal_crypto.h", "mik32_hal_pcc.h"};
    contribution.public_prototypes = {"void " + contribution.init_function_name + "(void);"};
    contribution.global_definitions = "Crypto_HandleTypeDef hcrypto;\n";

    std::ostringstream init_c;
    init_c << "/*\n"
              " * CRYPTO: algorithm=" << config.algorithm << ", cipher_mode=" << config.cipher_mode
           << ", swap_mode=" << config.swap_mode << ", order_mode=" << config.order_mode << ".\n"
              " * Ключ (HAL_Crypto_SetKey) и вектор инициализации (HAL_Crypto_SetIV) — секретные\n"
              " * данные времени выполнения, этот кодоген их не генерирует и не хранит —\n"
              " * задайте их сами в своём коде перед HAL_Crypto_Encode/Decode (см.\n"
              " * docs/implementations/crypto.md).\n"
              " */\n"
              "void " << contribution.init_function_name << "(void)\n"
              "{\n"
              "    hcrypto.Instance = CRYPTO;\n"
              "\n"
              "    hcrypto.Algorithm = " << algorithmMacro(config.algorithm) << ";\n"
              "    hcrypto.CipherMode = " << cipherModeMacro(config.cipher_mode) << ";\n"
              "    hcrypto.SwapMode = " << swapModeMacro(config.swap_mode) << ";\n"
              "    hcrypto.OrderMode = " << orderModeMacro(config.order_mode) << ";\n"
              "\n"
              "    HAL_Crypto_Init(&hcrypto);\n"
              "}\n";
    contribution.init_function_text = init_c.str();

    return ParseResult<PeripheralContribution>{true, "", contribution};
}
