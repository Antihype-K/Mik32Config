#include "crypto_parser.h"
#include "win_file_io.h"
#include "json.hpp"
#include <set>

namespace
{
    const std::set<std::string> kValidAlgorithms = {"kuznechik", "magma", "aes"};
    const std::set<std::string> kValidCipherModes = {"ecb", "cbc", "ctr"};
    const std::set<std::string> kValidSwapModes = {"none", "halfword", "byte", "bit"};
    const std::set<std::string> kValidOrderModes = {"lsw", "msw"};

    ParseResult<CryptoConfig> makeError(const std::string& message)
    {
        return ParseResult<CryptoConfig>{false, message, std::nullopt};
    }
}

ParseResult<CryptoConfig> parseCryptoConfig(const std::string& file_name_path)
{
    const FileReadOutcome file = readUtf8PathFile(file_name_path);
    if (!file.success)
    {
        return makeError("Не удалось открыть файл конфигурации: " + file_name_path + file.diagnostic);
    }

    nlohmann::json root;
    try
    {
        root = nlohmann::json::parse(file.content);
    }
    catch (const nlohmann::json::parse_error& parse_error)
    {
        return makeError(std::string("Ошибка разбора JSON: ") + parse_error.what());
    }

    if (!root.contains("peripherals") || !root["peripherals"].is_array())
    {
        return makeError("Конфигурация не содержит массив \"peripherals\"");
    }

    const nlohmann::json* crypto_node = nullptr;
    for (const auto& peripheral : root["peripherals"])
    {
        if (peripheral.contains("type") && peripheral["type"].is_string()
            && peripheral["type"].get<std::string>() == "crypto")
        {
            crypto_node = &peripheral;
            break;
        }
    }

    if (crypto_node == nullptr)
    {
        return makeError("В конфигурации не найден блок peripherals с type=\"crypto\"");
    }

    return parseCryptoBlock(*crypto_node);
}

ParseResult<CryptoConfig> parseCryptoBlock(const nlohmann::json& crypto)
{
    for (const char* field : {"algorithm", "cipher_mode"})
    {
        if (!crypto.contains(field))
        {
            return makeError(std::string("Отсутствует обязательное поле \"") + field + "\"");
        }
    }

    if (!crypto["algorithm"].is_string() || !kValidAlgorithms.count(crypto["algorithm"].get<std::string>()))
    {
        return makeError("Поле \"algorithm\" содержит недопустимое значение (разрешены kuznechik, magma, aes)");
    }
    if (!crypto["cipher_mode"].is_string() || !kValidCipherModes.count(crypto["cipher_mode"].get<std::string>()))
    {
        return makeError("Поле \"cipher_mode\" содержит недопустимое значение (разрешены ecb, cbc, ctr)");
    }

    CryptoConfig config;
    config.type = "crypto";
    config.algorithm = crypto["algorithm"].get<std::string>();
    config.cipher_mode = crypto["cipher_mode"].get<std::string>();

    if (crypto.contains("swap_mode"))
    {
        if (!crypto["swap_mode"].is_string() || !kValidSwapModes.count(crypto["swap_mode"].get<std::string>()))
        {
            return makeError("Поле \"swap_mode\" содержит недопустимое значение (разрешены none, halfword, byte, bit)");
        }
        config.swap_mode = crypto["swap_mode"].get<std::string>();
    }

    if (crypto.contains("order_mode"))
    {
        if (!crypto["order_mode"].is_string() || !kValidOrderModes.count(crypto["order_mode"].get<std::string>()))
        {
            return makeError("Поле \"order_mode\" содержит недопустимое значение (разрешены lsw, msw)");
        }
        config.order_mode = crypto["order_mode"].get<std::string>();
    }

    return ParseResult<CryptoConfig>{true, "", config};
}
