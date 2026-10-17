#include "dac_parser.h"
#include "win_file_io.h"
#include "json.hpp"

namespace
{
    constexpr int kMinInstance = 0;
    constexpr int kMaxInstance = 1;
    constexpr int kMinDiv = 0;
    constexpr int kMaxDiv = 255;

    ParseResult<DacConfig> makeError(const std::string& message)
    {
        return ParseResult<DacConfig>{false, message, std::nullopt};
    }
}

ParseResult<DacConfig> parseDacConfig(const std::string& file_name_path)
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

    const nlohmann::json* dac_node = nullptr;
    for (const auto& peripheral : root["peripherals"])
    {
        if (peripheral.contains("type") && peripheral["type"].is_string()
            && peripheral["type"].get<std::string>() == "dac")
        {
            dac_node = &peripheral;
            break;
        }
    }

    if (dac_node == nullptr)
    {
        return makeError("В конфигурации не найден блок peripherals с type=\"dac\"");
    }

    return parseDacBlock(*dac_node);
}

ParseResult<DacConfig> parseDacBlock(const nlohmann::json& dac)
{
    for (const char* field : {"instance", "div", "ext_ref", "ext_clb"})
    {
        if (!dac.contains(field))
        {
            return makeError(std::string("Отсутствует обязательное поле \"") + field + "\"");
        }
    }

    if (!dac["instance"].is_number_integer())
    {
        return makeError("Поле \"instance\" должно быть целым числом");
    }
    const int instance = dac["instance"].get<int>();
    if (instance < kMinInstance || instance > kMaxInstance)
    {
        return makeError("Поле \"instance\" вне диапазона 0-1 (в МИК32 два канала ЦАП)");
    }

    if (!dac["div"].is_number_integer())
    {
        return makeError("Поле \"div\" должно быть целым числом");
    }
    const int div = dac["div"].get<int>();
    if (div < kMinDiv || div > kMaxDiv)
    {
        return makeError("Поле \"div\" вне диапазона 0-255 (DAC_InitTypeDef.DIV — 8-битное значение)");
    }

    if (!dac["ext_ref"].is_boolean())
    {
        return makeError("Поле \"ext_ref\" должно быть булевым");
    }
    if (!dac["ext_clb"].is_boolean())
    {
        return makeError("Поле \"ext_clb\" должно быть булевым");
    }

    DacConfig config;
    config.type = "dac";
    config.instance = instance;
    config.div = div;
    config.ext_ref = dac["ext_ref"].get<bool>();
    config.ext_clb = dac["ext_clb"].get<bool>();

    return ParseResult<DacConfig>{true, "", config};
}
