#include "crc_parser.h"
#include "win_file_io.h"
#include "json.hpp"
#include <map>

// Концепция: как и в parser/rtc/rtc_parser.cpp, правила ниже дословно
// повторяют schemas/crc.json.
namespace
{
    struct PresetValues
    {
        uint32_t poly;
        uint32_t init;
        bool input_reverse;
        bool output_reverse;
        bool output_inversion;
    };

    // mik32_hal_crc32.h, строки 240-283 — девять закомментированных блоков
    // под стандартные алгоритмы CRC-32. Единственный источник этих чисел —
    // сам HAL-заголовок, не выдуманные значения.
    const std::map<std::string, PresetValues> kPresets = {
        {"crc32",        {0x04C11DB7, 0xFFFFFFFF, true,  true,  true}},
        {"crc32_bzip2",  {0x04C11DB7, 0xFFFFFFFF, false, false, true}},
        {"crc32c",       {0x1EDC6F41, 0xFFFFFFFF, true,  true,  true}},
        {"crc32d",       {0xA833982B, 0xFFFFFFFF, true,  true,  true}},
        {"crc32_jamcrc", {0x04C11DB7, 0xFFFFFFFF, true,  true,  false}},
        {"crc32_mpeg2",  {0x04C11DB7, 0xFFFFFFFF, false, false, false}},
        {"crc32_posix",  {0x04C11DB7, 0x00000000, false, false, true}},
        {"crc32q",       {0x814141AB, 0x00000000, false, false, false}},
        {"crc32_xfer",   {0x000000AF, 0x00000000, false, false, false}},
    };

    ParseResult<CrcConfig> makeError(const std::string& message)
    {
        return ParseResult<CrcConfig>{false, message, std::nullopt};
    }
}

ParseResult<CrcConfig> parseCrcConfig(const std::string& file_name_path)
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

    const nlohmann::json* crc_node = nullptr;
    for (const auto& peripheral : root["peripherals"])
    {
        if (peripheral.contains("type") && peripheral["type"].is_string()
            && peripheral["type"].get<std::string>() == "crc")
        {
            crc_node = &peripheral;
            break;
        }
    }

    if (crc_node == nullptr)
    {
        return makeError("В конфигурации не найден блок peripherals с type=\"crc\"");
    }

    return parseCrcBlock(*crc_node);
}

ParseResult<CrcConfig> parseCrcBlock(const nlohmann::json& crc)
{
    const bool has_preset = crc.contains("preset");
    const bool has_raw_fields = crc.contains("poly") || crc.contains("init")
        || crc.contains("input_reverse") || crc.contains("output_reverse") || crc.contains("output_inversion");

    if (has_preset && has_raw_fields)
    {
        return makeError("Нельзя одновременно задавать \"preset\" и явные поля "
            "(poly/init/input_reverse/output_reverse/output_inversion) — выберите один способ");
    }

    CrcConfig config;
    config.type = "crc";

    if (has_preset)
    {
        if (!crc["preset"].is_string() || !kPresets.count(crc["preset"].get<std::string>()))
        {
            return makeError("Поле \"preset\" содержит недопустимое значение "
                "(разрешены crc32, crc32_bzip2, crc32c, crc32d, crc32_jamcrc, crc32_mpeg2, crc32_posix, crc32q, crc32_xfer)");
        }
        const std::string preset_name = crc["preset"].get<std::string>();
        const PresetValues& preset = kPresets.at(preset_name);

        config.requested_preset = preset_name;
        config.poly = preset.poly;
        config.init = preset.init;
        config.input_reverse = preset.input_reverse;
        config.output_reverse = preset.output_reverse;
        config.output_inversion = preset.output_inversion;
        return ParseResult<CrcConfig>{true, "", config};
    }

    for (const char* field : {"poly", "init", "input_reverse", "output_reverse", "output_inversion"})
    {
        if (!crc.contains(field))
        {
            return makeError(std::string("Отсутствует обязательное поле \"") + field
                + "\" (либо задайте \"preset\" вместо явных полей)");
        }
    }

    if (!crc["poly"].is_number_integer())
    {
        return makeError("Поле \"poly\" должно быть целым числом");
    }
    const int64_t poly_raw = crc["poly"].get<int64_t>();
    if (poly_raw < 0 || static_cast<uint64_t>(poly_raw) > 4294967295ULL)
    {
        return makeError("Поле \"poly\" вне диапазона 0-4294967295 (32-битный регистр POLY)");
    }
    config.poly = static_cast<uint32_t>(poly_raw);

    if (!crc["init"].is_number_integer())
    {
        return makeError("Поле \"init\" должно быть целым числом");
    }
    const int64_t init_raw = crc["init"].get<int64_t>();
    if (init_raw < 0 || static_cast<uint64_t>(init_raw) > 4294967295ULL)
    {
        return makeError("Поле \"init\" вне диапазона 0-4294967295 (32-битный регистр DATA32)");
    }
    config.init = static_cast<uint32_t>(init_raw);

    if (!crc["input_reverse"].is_boolean())
    {
        return makeError("Поле \"input_reverse\" должно быть булевым");
    }
    config.input_reverse = crc["input_reverse"].get<bool>();

    if (!crc["output_reverse"].is_boolean())
    {
        return makeError("Поле \"output_reverse\" должно быть булевым");
    }
    config.output_reverse = crc["output_reverse"].get<bool>();

    if (!crc["output_inversion"].is_boolean())
    {
        return makeError("Поле \"output_inversion\" должно быть булевым");
    }
    config.output_inversion = crc["output_inversion"].get<bool>();

    return ParseResult<CrcConfig>{true, "", config};
}
