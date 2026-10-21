#include "spifi_parser.h"
#include "win_file_io.h"
#include "json.hpp"
#include <map>

// Концепция: как и в parser/crc/crc_parser.cpp, правила ниже дословно
// повторяют schemas/spifi.json.
namespace
{
    // mik32_hal_spifi.h:63-72. Регистровые значения MCMD.FRAMEFORM 0 и 1
    // зарезервированы — не входят в enum (см. schemas/spifi.json).
    const std::map<std::string, std::string> kFrameFormMacros = {
        {"opcode", "SPIFI_FRAMEFORM_OPCODE"},
        {"opcode_1addr", "SPIFI_FRAMEFORM_OPCODE_1ADDR"},
        {"opcode_2addr", "SPIFI_FRAMEFORM_OPCODE_2ADDR"},
        {"opcode_3addr", "SPIFI_FRAMEFORM_OPCODE_3ADDR"},
        {"opcode_4addr", "SPIFI_FRAMEFORM_OPCODE_4ADDR"},
        {"addr_3", "SPIFI_FRAMEFORM_3ADDR"},
        {"addr_4", "SPIFI_FRAMEFORM_4ADDR"},
    };

    // mik32_hal_spifi.h:55-61.
    const std::map<std::string, std::string> kFieldFormMacros = {
        {"all_serial", "SPIFI_FIELDFORM_ALL_SERIAL"},
        {"data_parallel", "SPIFI_FIELDFORM_DATA_PARALLEL"},
        {"opcode_serial", "SPIFI_FIELDFORM_OPCODE_SERIAL"},
        {"all_parallel", "SPIFI_FIELDFORM_ALL_PARALLEL"},
    };

    ParseResult<SpifiConfig> makeError(const std::string& message)
    {
        return ParseResult<SpifiConfig>{false, message, std::nullopt};
    }
}

ParseResult<SpifiConfig> parseSpifiConfig(const std::string& file_name_path)
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

    const nlohmann::json* spifi_node = nullptr;
    for (const auto& peripheral : root["peripherals"])
    {
        if (peripheral.contains("type") && peripheral["type"].is_string()
            && peripheral["type"].get<std::string>() == "spifi")
        {
            spifi_node = &peripheral;
            break;
        }
    }

    if (spifi_node == nullptr)
    {
        return makeError("В конфигурации не найден блок peripherals с type=\"spifi\"");
    }

    return parseSpifiBlock(*spifi_node);
}

ParseResult<SpifiConfig> parseSpifiBlock(const nlohmann::json& spifi)
{
    for (const char* field : {"cache_enabled", "cache_limit", "cmd_frame_form", "cmd_field_form", "cmd_op_code"})
    {
        if (!spifi.contains(field))
        {
            return makeError(std::string("Отсутствует обязательное поле \"") + field + "\"");
        }
    }

    SpifiConfig config;
    config.type = "spifi";

    if (!spifi["cache_enabled"].is_boolean())
    {
        return makeError("Поле \"cache_enabled\" должно быть булевым");
    }
    config.cache_enabled = spifi["cache_enabled"].get<bool>();

    if (!spifi["cache_limit"].is_number_integer())
    {
        return makeError("Поле \"cache_limit\" должно быть целым числом");
    }
    const int64_t cache_limit_raw = spifi["cache_limit"].get<int64_t>();
    if (cache_limit_raw < 0 || static_cast<uint64_t>(cache_limit_raw) > 4294967295ULL)
    {
        return makeError("Поле \"cache_limit\" вне диапазона 0-4294967295 (32-битный регистр CLIMIT)");
    }
    config.cache_limit = static_cast<uint32_t>(cache_limit_raw);

    if (!spifi["cmd_frame_form"].is_string() || !kFrameFormMacros.count(spifi["cmd_frame_form"].get<std::string>()))
    {
        return makeError("Поле \"cmd_frame_form\" содержит недопустимое значение "
            "(разрешены opcode, opcode_1addr, opcode_2addr, opcode_3addr, opcode_4addr, addr_3, addr_4)");
    }
    config.cmd_frame_form = spifi["cmd_frame_form"].get<std::string>();

    if (!spifi["cmd_field_form"].is_string() || !kFieldFormMacros.count(spifi["cmd_field_form"].get<std::string>()))
    {
        return makeError("Поле \"cmd_field_form\" содержит недопустимое значение "
            "(разрешены all_serial, data_parallel, opcode_serial, all_parallel)");
    }
    config.cmd_field_form = spifi["cmd_field_form"].get<std::string>();

    if (!spifi["cmd_op_code"].is_number_integer())
    {
        return makeError("Поле \"cmd_op_code\" должно быть целым числом");
    }
    const int64_t op_code_raw = spifi["cmd_op_code"].get<int64_t>();
    if (op_code_raw < 0 || op_code_raw > 255)
    {
        return makeError("Поле \"cmd_op_code\" вне диапазона 0-255 (8-битное поле MCMD.OPCODE)");
    }
    config.cmd_op_code = static_cast<uint8_t>(op_code_raw);

    config.cmd_interim_length = 0;
    if (spifi.contains("cmd_interim_length"))
    {
        if (!spifi["cmd_interim_length"].is_number_integer())
        {
            return makeError("Поле \"cmd_interim_length\" должно быть целым числом");
        }
        const int64_t interim_length_raw = spifi["cmd_interim_length"].get<int64_t>();
        if (interim_length_raw < 0 || interim_length_raw > 7)
        {
            return makeError("Поле \"cmd_interim_length\" вне диапазона 0-7 (3-битное поле MCMD.INTLEN)");
        }
        config.cmd_interim_length = static_cast<uint8_t>(interim_length_raw);
    }

    return ParseResult<SpifiConfig>{true, "", config};
}

std::string spifiFrameFormMacro(const std::string& json_value)
{
    return kFrameFormMacros.at(json_value);
}

std::string spifiFieldFormMacro(const std::string& json_value)
{
    return kFieldFormMacros.at(json_value);
}
