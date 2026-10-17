#include "tsens_parser.h"
#include "win_file_io.h"
#include "json.hpp"
#include <set>

namespace
{
    const std::set<std::string> kValidClockSources = {"sys_clk", "hclk", "osc32m", "hsi32m", "osc32k", "lsi32k"};
    const std::set<std::string> kValidInterrupts = {"low", "high", "eoc"};
    constexpr int kMinThresholdCelsius = -40;
    constexpr int kMaxThresholdCelsius = 125;

    ParseResult<TsensConfig> makeError(const std::string& message)
    {
        return ParseResult<TsensConfig>{false, message, std::nullopt};
    }
}

ParseResult<TsensConfig> parseTsensConfig(const std::string& file_name_path)
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

    const nlohmann::json* tsens_node = nullptr;
    for (const auto& peripheral : root["peripherals"])
    {
        if (peripheral.contains("type") && peripheral["type"].is_string()
            && peripheral["type"].get<std::string>() == "tsens")
        {
            tsens_node = &peripheral;
            break;
        }
    }

    if (tsens_node == nullptr)
    {
        return makeError("В конфигурации не найден блок peripherals с type=\"tsens\"");
    }

    return parseTsensBlock(*tsens_node);
}

ParseResult<TsensConfig> parseTsensBlock(const nlohmann::json& tsens)
{
    for (const char* field : {"clock_source", "frequency_hz"})
    {
        if (!tsens.contains(field))
        {
            return makeError(std::string("Отсутствует обязательное поле \"") + field + "\"");
        }
    }

    if (!tsens["clock_source"].is_string() || !kValidClockSources.count(tsens["clock_source"].get<std::string>()))
    {
        return makeError("Поле \"clock_source\" содержит недопустимое значение "
            "(разрешены sys_clk, hclk, osc32m, hsi32m, osc32k, lsi32k)");
    }

    if (!tsens["frequency_hz"].is_number_integer())
    {
        return makeError("Поле \"frequency_hz\" должно быть целым числом");
    }
    const int64_t frequency_raw = tsens["frequency_hz"].get<int64_t>();
    if (frequency_raw < 1 || frequency_raw > 100000)
    {
        return makeError("Поле \"frequency_hz\" вне диапазона 1-100000 "
            "(mik32_hal_tsens.h: «не больше 100кГц»)");
    }

    TsensConfig config;
    config.type = "tsens";
    config.clock_source = tsens["clock_source"].get<std::string>();
    config.frequency_hz = static_cast<uint32_t>(frequency_raw);

    if (tsens.contains("low_threshold_celsius"))
    {
        if (!tsens["low_threshold_celsius"].is_number_integer())
        {
            return makeError("Поле \"low_threshold_celsius\" должно быть целым числом");
        }
        const int value = tsens["low_threshold_celsius"].get<int>();
        if (value < kMinThresholdCelsius || value > kMaxThresholdCelsius)
        {
            return makeError("Поле \"low_threshold_celsius\" вне диапазона от -40 до 125");
        }
        config.has_low_threshold = true;
        config.low_threshold_celsius = value;
    }

    if (tsens.contains("high_threshold_celsius"))
    {
        if (!tsens["high_threshold_celsius"].is_number_integer())
        {
            return makeError("Поле \"high_threshold_celsius\" должно быть целым числом");
        }
        const int value = tsens["high_threshold_celsius"].get<int>();
        if (value < kMinThresholdCelsius || value > kMaxThresholdCelsius)
        {
            return makeError("Поле \"high_threshold_celsius\" вне диапазона от -40 до 125");
        }
        config.has_high_threshold = true;
        config.high_threshold_celsius = value;
    }

    if (tsens.contains("interrupts"))
    {
        if (!tsens["interrupts"].is_array())
        {
            return makeError("Поле \"interrupts\" должно быть массивом строк");
        }
        for (const auto& item : tsens["interrupts"])
        {
            if (!item.is_string() || !kValidInterrupts.count(item.get<std::string>()))
            {
                return makeError("Поле \"interrupts\" содержит недопустимое значение (разрешены low, high, eoc)");
            }
            config.interrupts.push_back(item.get<std::string>());
        }
    }

    return ParseResult<TsensConfig>{true, "", config};
}
