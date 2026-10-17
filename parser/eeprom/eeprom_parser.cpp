#include "eeprom_parser.h"
#include "win_file_io.h"
#include "json.hpp"
#include <set>

namespace
{
    const std::set<std::string> kValidModes = {"two_stage", "three_stage"};

    ParseResult<EepromConfig> makeError(const std::string& message)
    {
        return ParseResult<EepromConfig>{false, message, std::nullopt};
    }
}

ParseResult<EepromConfig> parseEepromConfig(const std::string& file_name_path)
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

    const nlohmann::json* eeprom_node = nullptr;
    for (const auto& peripheral : root["peripherals"])
    {
        if (peripheral.contains("type") && peripheral["type"].is_string()
            && peripheral["type"].get<std::string>() == "eeprom")
        {
            eeprom_node = &peripheral;
            break;
        }
    }

    if (eeprom_node == nullptr)
    {
        return makeError("В конфигурации не найден блок peripherals с type=\"eeprom\"");
    }

    return parseEepromBlock(*eeprom_node);
}

ParseResult<EepromConfig> parseEepromBlock(const nlohmann::json& eeprom)
{
    if (!eeprom.contains("mode"))
    {
        return makeError("Отсутствует обязательное поле \"mode\"");
    }
    if (!eeprom["mode"].is_string() || !kValidModes.count(eeprom["mode"].get<std::string>()))
    {
        return makeError("Поле \"mode\" содержит недопустимое значение (разрешены two_stage, three_stage)");
    }

    EepromConfig config;
    config.type = "eeprom";
    config.mode = eeprom["mode"].get<std::string>();

    if (eeprom.contains("ecc_enabled"))
    {
        if (!eeprom["ecc_enabled"].is_boolean())
        {
            return makeError("Поле \"ecc_enabled\" должно быть булевым");
        }
        config.ecc_enabled = eeprom["ecc_enabled"].get<bool>();
    }

    if (eeprom.contains("single_error_interrupt_enabled"))
    {
        if (!eeprom["single_error_interrupt_enabled"].is_boolean())
        {
            return makeError("Поле \"single_error_interrupt_enabled\" должно быть булевым");
        }
        config.single_error_interrupt_enabled = eeprom["single_error_interrupt_enabled"].get<bool>();
    }

    return ParseResult<EepromConfig>{true, "", config};
}
