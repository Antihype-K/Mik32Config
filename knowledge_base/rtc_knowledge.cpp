#include "rtc_knowledge.h"
#include "win_file_io.h"
#include "json.hpp"

ParseResult<RtcKnowledge> loadRtcKnowledge(const std::string& file_path)
{
    const FileReadOutcome file = readUtf8PathFile(file_path);
    if (!file.success)
    {
        return ParseResult<RtcKnowledge>{false, "Не удалось открыть knowledge_base: " + file_path + file.diagnostic, std::nullopt};
    }

    nlohmann::json root;
    try
    {
        root = nlohmann::json::parse(file.content);
    }
    catch (const nlohmann::json::parse_error& parse_error)
    {
        return ParseResult<RtcKnowledge>{false, std::string("Ошибка разбора knowledge_base: ") + parse_error.what(), std::nullopt};
    }

    RtcKnowledge knowledge;
    try
    {
        for (auto it = root.at("weekday_hal_enum").begin(); it != root.at("weekday_hal_enum").end(); ++it)
        {
            knowledge.weekday_hal_enum[it.key()] = it.value().get<std::string>();
        }

        for (auto it = root.at("month_hal_enum").begin(); it != root.at("month_hal_enum").end(); ++it)
        {
            knowledge.month_hal_enum[it.key()] = it.value().get<std::string>();
        }

        for (auto it = root.at("alarm_compare_time_mask").begin(); it != root.at("alarm_compare_time_mask").end(); ++it)
        {
            knowledge.alarm_compare_time_mask[it.key()] = it.value().get<std::string>();
        }

        for (auto it = root.at("alarm_compare_date_mask").begin(); it != root.at("alarm_compare_date_mask").end(); ++it)
        {
            knowledge.alarm_compare_date_mask[it.key()] = it.value().get<std::string>();
        }
    }
    catch (const nlohmann::json::exception& structure_error)
    {
        return ParseResult<RtcKnowledge>{false, std::string("knowledge_base/rtc.json не соответствует ожидаемой структуре: ") + structure_error.what(), std::nullopt};
    }

    return ParseResult<RtcKnowledge>{true, "", knowledge};
}
