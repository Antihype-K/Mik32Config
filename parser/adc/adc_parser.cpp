#include "adc_parser.h"
#include "win_file_io.h"
#include "json.hpp"

namespace
{
    constexpr int kMinChannel = 0;
    constexpr int kMaxChannel = 7;

    ParseResult<AdcConfig> makeError(const std::string& message)
    {
        return ParseResult<AdcConfig>{false, message, std::nullopt};
    }
}

ParseResult<AdcConfig> parseAdcConfig(const std::string& file_name_path)
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

    const nlohmann::json* adc_node = nullptr;
    for (const auto& peripheral : root["peripherals"])
    {
        if (peripheral.contains("type") && peripheral["type"].is_string()
            && peripheral["type"].get<std::string>() == "adc")
        {
            adc_node = &peripheral;
            break;
        }
    }

    if (adc_node == nullptr)
    {
        return makeError("В конфигурации не найден блок peripherals с type=\"adc\"");
    }

    return parseAdcBlock(*adc_node);
}

ParseResult<AdcConfig> parseAdcBlock(const nlohmann::json& adc)
{
    for (const char* field : {"channel", "ext_ref", "ext_clb"})
    {
        if (!adc.contains(field))
        {
            return makeError(std::string("Отсутствует обязательное поле \"") + field + "\"");
        }
    }

    if (!adc["channel"].is_number_integer())
    {
        return makeError("Поле \"channel\" должно быть целым числом");
    }
    const int channel = adc["channel"].get<int>();
    if (channel < kMinChannel || channel > kMaxChannel)
    {
        return makeError("Поле \"channel\" вне диапазона 0-7 (8-канальный мультиплексор АЦП)");
    }

    if (!adc["ext_ref"].is_boolean())
    {
        return makeError("Поле \"ext_ref\" должно быть булевым");
    }
    if (!adc["ext_clb"].is_boolean())
    {
        return makeError("Поле \"ext_clb\" должно быть булевым");
    }
    if (adc.contains("interrupt_enabled") && !adc["interrupt_enabled"].is_boolean())
    {
        return makeError("Поле \"interrupt_enabled\" должно быть булевым");
    }

    AdcConfig config;
    config.type = "adc";
    config.channel = channel;
    config.ext_ref = adc["ext_ref"].get<bool>();
    config.ext_clb = adc["ext_clb"].get<bool>();
    config.interrupt_enabled = adc.value("interrupt_enabled", false);

    return ParseResult<AdcConfig>{true, "", config};
}
