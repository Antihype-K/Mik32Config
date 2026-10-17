#include "rtc_parser.h"
#include "win_file_io.h"
#include "json.hpp"
#include <set>

// Концепция: как и в parser/timer32/timer32_parser.cpp, правила ниже
// дословно повторяют schemas/rtc.json. RTC — единственный глобальный блок
// (нет instance) — допустим только один блок type="rtc" на проект, эта
// проверка выполняется на уровне ProjectConfig (codegen/project/project_config.cpp),
// не здесь: parseRtcBlock валидирует ОДИН уже извлечённый элемент.
namespace
{
    const std::set<std::string> kValidCompareFields = {
        "dow", "hours", "minutes", "seconds", "century", "day", "month", "year"};

    ParseResult<RtcConfig> makeError(const std::string& message)
    {
        return ParseResult<RtcConfig>{false, message, std::nullopt};
    }

    // Датащит (раздел RTC_DateTypeDef): day валидируется относительно
    // month/year с учётом високосного года — межполевая проверка, не
    // выражается декларативно в схеме (см. schemas/rtc.json). Реальный год
    // для расчёта високосности — century*100+year.
    bool isLeapYear(int full_year)
    {
        return (full_year % 4 == 0) && (full_year % 100 != 0 || full_year % 400 == 0);
    }

    int daysInMonth(int month, int full_year)
    {
        static const int kDays[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
        if (month == 2 && isLeapYear(full_year))
        {
            return 29;
        }
        return kDays[month - 1];
    }

    // Общая проверка для time/date — используется и для текущего
    // времени/даты, и для времени/даты будильника (идентичная структура).
    std::optional<std::string> parseTimeFields(const nlohmann::json& time_node, int& dow, int& hours, int& minutes, int& seconds)
    {
        for (const char* field : {"dow", "hours", "minutes", "seconds"})
        {
            if (!time_node.contains(field) || !time_node[field].is_number_integer())
            {
                return std::string("поле \"") + field + "\" обязательно и должно быть целым числом";
            }
        }
        dow = time_node["dow"].get<int>();
        if (dow < 1 || dow > 7)
        {
            return std::string("поле \"dow\" вне диапазона 1-7");
        }
        hours = time_node["hours"].get<int>();
        if (hours < 0 || hours > 23)
        {
            return std::string("поле \"hours\" вне диапазона 0-23");
        }
        minutes = time_node["minutes"].get<int>();
        if (minutes < 0 || minutes > 59)
        {
            return std::string("поле \"minutes\" вне диапазона 0-59");
        }
        seconds = time_node["seconds"].get<int>();
        if (seconds < 0 || seconds > 59)
        {
            return std::string("поле \"seconds\" вне диапазона 0-59");
        }
        return std::nullopt;
    }

    std::optional<std::string> parseDateFields(const nlohmann::json& date_node, int& century, int& day, int& month, int& year)
    {
        for (const char* field : {"century", "day", "month", "year"})
        {
            if (!date_node.contains(field) || !date_node[field].is_number_integer())
            {
                return std::string("поле \"") + field + "\" обязательно и должно быть целым числом";
            }
        }
        century = date_node["century"].get<int>();
        if (century < 0 || century > 99)
        {
            return std::string("поле \"century\" вне диапазона 0-99");
        }
        month = date_node["month"].get<int>();
        if (month < 1 || month > 12)
        {
            return std::string("поле \"month\" вне диапазона 1-12");
        }
        year = date_node["year"].get<int>();
        if (year < 0 || year > 99)
        {
            return std::string("поле \"year\" вне диапазона 0-99");
        }
        day = date_node["day"].get<int>();
        const int full_year = century * 100 + year;
        const int max_day = daysInMonth(month, full_year);
        if (day < 1 || day > max_day)
        {
            return std::string("поле \"day\" вне диапазона 1-") + std::to_string(max_day)
                + " для месяца " + std::to_string(month) + " года " + std::to_string(full_year)
                + (max_day == 28 ? " (не високосный — 29 февраля недопустимо)" : "");
        }
        return std::nullopt;
    }
}

ParseResult<RtcConfig> parseRtcConfig(const std::string& file_name_path)
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

    const nlohmann::json* rtc_node = nullptr;
    for (const auto& peripheral : root["peripherals"])
    {
        if (peripheral.contains("type") && peripheral["type"].is_string()
            && peripheral["type"].get<std::string>() == "rtc")
        {
            rtc_node = &peripheral;
            break;
        }
    }

    if (rtc_node == nullptr)
    {
        return makeError("В конфигурации не найден блок peripherals с type=\"rtc\"");
    }

    return parseRtcBlock(*rtc_node);
}

ParseResult<RtcConfig> parseRtcBlock(const nlohmann::json& rtc)
{
    for (const char* field : {"time", "date"})
    {
        if (!rtc.contains(field) || !rtc[field].is_object())
        {
            return makeError(std::string("Отсутствует обязательное поле \"") + field + "\" (или это не объект)");
        }
    }

    RtcConfig config;
    config.type = "rtc";

    if (auto error = parseTimeFields(rtc["time"], config.dow, config.hours, config.minutes, config.seconds))
    {
        return makeError("Поле \"time\": " + *error);
    }
    if (auto error = parseDateFields(rtc["date"], config.century, config.day, config.month, config.year))
    {
        return makeError("Поле \"date\": " + *error);
    }

    if (!rtc.contains("alarm"))
    {
        return ParseResult<RtcConfig>{true, "", config};
    }

    if (!rtc["alarm"].is_object())
    {
        return makeError("Поле \"alarm\" должно быть объектом");
    }
    const nlohmann::json& alarm = rtc["alarm"];

    for (const char* field : {"time", "date"})
    {
        if (!alarm.contains(field) || !alarm[field].is_object())
        {
            return makeError(std::string("Поле \"alarm.") + field + "\" обязательно (или это не объект)");
        }
    }

    config.has_alarm = true;

    if (auto error = parseTimeFields(alarm["time"], config.alarm_dow, config.alarm_hours, config.alarm_minutes, config.alarm_seconds))
    {
        return makeError("Поле \"alarm.time\": " + *error);
    }
    if (auto error = parseDateFields(alarm["date"], config.alarm_century, config.alarm_day, config.alarm_month, config.alarm_year))
    {
        return makeError("Поле \"alarm.date\": " + *error);
    }

    if (alarm.contains("compare_fields"))
    {
        if (!alarm["compare_fields"].is_array() || alarm["compare_fields"].empty())
        {
            return makeError("Поле \"alarm.compare_fields\" должно быть непустым массивом строк");
        }
        for (const auto& item : alarm["compare_fields"])
        {
            if (!item.is_string() || !kValidCompareFields.count(item.get<std::string>()))
            {
                return makeError("Поле \"alarm.compare_fields\" содержит недопустимое значение "
                    "(разрешены dow, hours, minutes, seconds, century, day, month, year)");
            }
            config.alarm_compare_fields.push_back(item.get<std::string>());
        }
    }
    else
    {
        config.alarm_compare_fields = {"dow", "hours", "minutes", "seconds", "century", "day", "month", "year"};
    }

    if (alarm.contains("interrupt_enabled"))
    {
        if (!alarm["interrupt_enabled"].is_boolean())
        {
            return makeError("Поле \"alarm.interrupt_enabled\" должно быть булевым");
        }
        config.alarm_interrupt_enabled = alarm["interrupt_enabled"].get<bool>();
    }

    return ParseResult<RtcConfig>{true, "", config};
}
