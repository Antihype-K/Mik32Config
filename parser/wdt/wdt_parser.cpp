#include "wdt_parser.h"
#include "win_file_io.h"
#include "json.hpp"
#include <set>

namespace
{
    const std::set<std::string> kValidClockSources = {"osc32m", "hsi32m", "osc32k", "lsi32k"};

    // mik32_hal_wdt.h: WDT_MAX_VALUE_CLOCK32M=524, WDT_MAX_VALUE_CLOCK32K=511875.
    constexpr uint32_t kMaxReloadMs32MClock = 524;
    constexpr uint32_t kMaxReloadMs32KClock = 511875;

    bool isClock32M(const std::string& clock_source)
    {
        return clock_source == "osc32m" || clock_source == "hsi32m";
    }

    ParseResult<WdtConfig> makeError(const std::string& message)
    {
        return ParseResult<WdtConfig>{false, message, std::nullopt};
    }
}

ParseResult<WdtConfig> parseWdtConfig(const std::string& file_name_path)
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

    const nlohmann::json* wdt_node = nullptr;
    for (const auto& peripheral : root["peripherals"])
    {
        if (peripheral.contains("type") && peripheral["type"].is_string()
            && peripheral["type"].get<std::string>() == "wdt")
        {
            wdt_node = &peripheral;
            break;
        }
    }

    if (wdt_node == nullptr)
    {
        return makeError("В конфигурации не найден блок peripherals с type=\"wdt\"");
    }

    return parseWdtBlock(*wdt_node);
}

ParseResult<WdtConfig> parseWdtBlock(const nlohmann::json& wdt)
{
    for (const char* field : {"clock_source", "reload_ms"})
    {
        if (!wdt.contains(field))
        {
            return makeError(std::string("Отсутствует обязательное поле \"") + field + "\"");
        }
    }

    if (!wdt["clock_source"].is_string() || !kValidClockSources.count(wdt["clock_source"].get<std::string>()))
    {
        return makeError("Поле \"clock_source\" содержит недопустимое значение (разрешены osc32m, hsi32m, osc32k, lsi32k)");
    }
    const std::string clock_source = wdt["clock_source"].get<std::string>();

    if (!wdt["reload_ms"].is_number_integer())
    {
        return makeError("Поле \"reload_ms\" должно быть целым числом");
    }
    const int64_t reload_ms_raw = wdt["reload_ms"].get<int64_t>();
    if (reload_ms_raw < 1)
    {
        return makeError("Поле \"reload_ms\" должно быть положительным");
    }

    // mik32_hal_wdt.h: верхняя граница зависит от clock_source — 524мс для
    // 32МГц-источников (osc32m/hsi32m), 511875мс для 32кГц-источников
    // (osc32k/lsi32k). Межполевая проверка, не выражается декларативно в
    // schemas/wdt.json (тот же приём, что cmp/max_field у Timer16/Timer32).
    const uint32_t max_reload_ms = isClock32M(clock_source) ? kMaxReloadMs32MClock : kMaxReloadMs32KClock;
    if (static_cast<uint64_t>(reload_ms_raw) > max_reload_ms)
    {
        return makeError("Поле \"reload_ms\" вне диапазона для clock_source=\"" + clock_source + "\" "
            "(максимум " + std::to_string(max_reload_ms) + "мс — mik32_hal_wdt.h, WDT_MAX_VALUE_CLOCK32"
            + (isClock32M(clock_source) ? "M" : "K") + ")");
    }

    WdtConfig config;
    config.type = "wdt";
    config.clock_source = clock_source;
    config.reload_ms = static_cast<uint32_t>(reload_ms_raw);

    return ParseResult<WdtConfig>{true, "", config};
}
