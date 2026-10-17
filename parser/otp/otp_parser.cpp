#include "otp_parser.h"
#include "win_file_io.h"
#include "json.hpp"
#include <set>

namespace
{
    const std::set<std::string> kValidReadModes = {"two_stages", "three_stages"};

    ParseResult<OtpConfig> makeError(const std::string& message)
    {
        return ParseResult<OtpConfig>{false, message, std::nullopt};
    }
}

ParseResult<OtpConfig> parseOtpConfig(const std::string& file_name_path)
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

    const nlohmann::json* otp_node = nullptr;
    for (const auto& peripheral : root["peripherals"])
    {
        if (peripheral.contains("type") && peripheral["type"].is_string()
            && peripheral["type"].get<std::string>() == "otp")
        {
            otp_node = &peripheral;
            break;
        }
    }

    if (otp_node == nullptr)
    {
        return makeError("В конфигурации не найден блок peripherals с type=\"otp\"");
    }

    return parseOtpBlock(*otp_node);
}

ParseResult<OtpConfig> parseOtpBlock(const nlohmann::json& otp)
{
    if (!otp.contains("read_mode"))
    {
        return makeError("Отсутствует обязательное поле \"read_mode\"");
    }
    if (!otp["read_mode"].is_string() || !kValidReadModes.count(otp["read_mode"].get<std::string>()))
    {
        return makeError("Поле \"read_mode\" содержит недопустимое значение (разрешены two_stages, three_stages)");
    }

    OtpConfig config;
    config.type = "otp";
    config.read_mode = otp["read_mode"].get<std::string>();

    return ParseResult<OtpConfig>{true, "", config};
}
