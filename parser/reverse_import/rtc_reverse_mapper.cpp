//
// mapRtcBlock — см. rtc_reverse_mapper.h за контрактом. Разбор основан на
// docs/examples/mik32-examples/Hal_RTC/src/main.c и Hal_RTC_IRQ/src/main.c
// (RTC_Init, строки ~102-145): hrtc.Instance = RTC; затем HAL_RTC_SetTime/
// SetDate/SetAlarm(&hrtc, &sTime/&sDate/&sAlarm), HAL_RTC_Enable(&hrtc).
//
#include "rtc_reverse_mapper.h"

#include "c_literal_parser.h"
#include "local_struct_field_resolver.h"

#include <array>

namespace reverse_import
{

namespace
{

std::string trimText(const std::string& text)
{
    const std::size_t begin = text.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) return "";
    const std::size_t end = text.find_last_not_of(" \t\r\n");
    return text.substr(begin, end - begin + 1);
}

const RawFieldAssignment* lastAssignment(
    const std::vector<RawFieldAssignment>& assignments, const std::string& field_path)
{
    const RawFieldAssignment* found = nullptr;
    for (const RawFieldAssignment& assignment : assignments)
    {
        if (assignment.field_path == field_path)
        {
            found = &assignment;
        }
    }
    return found;
}

const RawTrailingCall* findTrailingCall(const RawInitBlock& block, const std::string& function_name)
{
    for (const RawTrailingCall& call : block.trailing_calls)
    {
        if (call.function_name == function_name)
        {
            return &call;
        }
    }
    return nullptr;
}

FieldProvenance makeProvenance(const std::string& json_field, const RawFieldAssignment& assignment, bool editable)
{
    FieldProvenance provenance;
    provenance.json_field = json_field;
    provenance.location = assignment.location;
    provenance.original_raw_value_text = assignment.raw_value_text;
    provenance.original_full_line_text = assignment.full_line_text;
    provenance.editable = editable;
    return provenance;
}

FieldProvenance makeUnresolvedProvenance(const std::string& json_field)
{
    FieldProvenance provenance;
    provenance.json_field = json_field;
    provenance.editable = false;
    provenance.unresolved = true;
    return provenance;
}

// "RTC_WEEKDAY_MONDAY".."RTC_WEEKDAY_SUNDAY" -> 1..7, "RTC_MONTH_JANUARY"..
// "RTC_MONTH_DECEMBER" -> 1..12 (mik32_hal_rtc.h), либо голый десятичный
// литерал — Hal_RTC/Hal_RTC_IRQ используют оба стиля в одном файле
// (Hours/Minutes/Seconds/Century/Day/Year числами, Dow/Month макросами).
std::optional<int> parseRtcIntField(const std::string& text)
{
    const std::string trimmed = trimText(text);

    static const std::array<std::pair<const char*, int>, 7> kWeekdays = {{
        {"RTC_WEEKDAY_MONDAY", 1}, {"RTC_WEEKDAY_TUESDAY", 2}, {"RTC_WEEKDAY_WEDNESDAY", 3},
        {"RTC_WEEKDAY_THURSDAY", 4}, {"RTC_WEEKDAY_FRIDAY", 5}, {"RTC_WEEKDAY_SATURDAY", 6},
        {"RTC_WEEKDAY_SUNDAY", 7},
    }};
    for (const auto& [name, value] : kWeekdays)
    {
        if (trimmed == name) return value;
    }

    static const std::array<std::pair<const char*, int>, 12> kMonths = {{
        {"RTC_MONTH_JANUARY", 1}, {"RTC_MONTH_FEBRUARY", 2}, {"RTC_MONTH_MARCH", 3},
        {"RTC_MONTH_APRIL", 4}, {"RTC_MONTH_MAY", 5}, {"RTC_MONTH_JUNE", 6},
        {"RTC_MONTH_JULY", 7}, {"RTC_MONTH_AUGUST", 8}, {"RTC_MONTH_SEPTEMBER", 9},
        {"RTC_MONTH_OCTOBER", 10}, {"RTC_MONTH_NOVEMBER", 11}, {"RTC_MONTH_DECEMBER", 12},
    }};
    for (const auto& [name, value] : kMonths)
    {
        if (trimmed == name) return value;
    }

    const std::optional<long long> literal = parseCIntegerLiteral(trimmed);
    if (literal.has_value())
    {
        return static_cast<int>(*literal);
    }
    return std::nullopt;
}

// Заполняет один integer-поле nested JSON-объекта (time/date/alarm.time/
// alarm.date) из assignments локальной value-структуры. json_field —
// составной путь для FieldProvenance ("time.dow", "alarm.date.century").
void mapRtcIntField(
    MappedBlock& mapped,
    nlohmann::json& target_object,
    const std::vector<RawFieldAssignment>& local_fields,
    const std::string& struct_field,
    const std::string& json_key,
    const std::string& json_field)
{
    const RawFieldAssignment* assignment = lastAssignment(local_fields, struct_field);
    if (assignment == nullptr)
    {
        target_object[json_key] = 0;
        mapped.notes.push_back(
            "поле " + struct_field + " локальной структуры не найдено — конфигуратор не "
            "смог определить " + json_field + ", укажите значение явно.");
        mapped.provenance.push_back(makeUnresolvedProvenance(json_field));
        return;
    }

    const std::optional<int> value = parseRtcIntField(assignment->raw_value_text);
    if (value.has_value())
    {
        target_object[json_key] = *value;
        mapped.provenance.push_back(makeProvenance(json_field, *assignment, false));
    }
    else
    {
        target_object[json_key] = 0;
        mapped.notes.push_back(
            struct_field + "=\"" + assignment->raw_value_text + "\" (" + assignment->location.fileName() +
            ":" + std::to_string(assignment->location.line) + ") — конфигуратор не смог определить " +
            json_field + ", укажите значение явно.");
        FieldProvenance provenance = makeUnresolvedProvenance(json_field);
        provenance.location = assignment->location;
        provenance.original_raw_value_text = assignment->raw_value_text;
        provenance.original_full_line_text = assignment->full_line_text;
        mapped.provenance.push_back(provenance);
    }
}

// "RTC_TALRM_CDOW_M | RTC_TALRM_CH_M | ..." -> ["dow","hours",...], та же
// идея для RTC_DALRM_* -> century/day/month/year. Разбор по "|", без
// вычисления значений битов (сканер не исполняет C) — только сопоставление
// имени макроса известному полю.
std::vector<std::string> parseCompareFields(const std::string& mask_time_text, const std::string& mask_date_text)
{
    static const std::array<std::pair<const char*, const char*>, 4> kTimeBits = {{
        {"RTC_TALRM_CDOW_M", "dow"}, {"RTC_TALRM_CH_M", "hours"},
        {"RTC_TALRM_CM_M", "minutes"}, {"RTC_TALRM_CS_M", "seconds"},
    }};
    static const std::array<std::pair<const char*, const char*>, 4> kDateBits = {{
        {"RTC_DALRM_CC_M", "century"}, {"RTC_DALRM_CY_M", "year"},
        {"RTC_DALRM_CM_M", "month"}, {"RTC_DALRM_CD_M", "day"},
    }};

    std::vector<std::string> fields;
    for (const auto& [macro, field] : kTimeBits)
    {
        if (mask_time_text.find(macro) != std::string::npos) fields.push_back(field);
    }
    for (const auto& [macro, field] : kDateBits)
    {
        if (mask_date_text.find(macro) != std::string::npos) fields.push_back(field);
    }
    return fields;
}

} // namespace

std::optional<MappedBlock> mapRtcBlock(const RawInitBlock& block)
{
    if (block.handle_type != "RTC_HandleTypeDef")
    {
        return std::nullopt;
    }

    MappedBlock result;
    result.json_block["type"] = "rtc";

    const std::string& source_file = block.init_call_location.file;

    // ---- time (из HAL_RTC_SetTime(&hrtc, &sTime)) ----
    {
        nlohmann::json time_object = nlohmann::json::object();
        const RawTrailingCall* call = findTrailingCall(block, "HAL_RTC_SetTime");
        if (call == nullptr || call->args.size() < 2)
        {
            time_object["dow"] = 1;
            time_object["hours"] = 0;
            time_object["minutes"] = 0;
            time_object["seconds"] = 0;
            result.notes.push_back(
                "HAL_RTC_SetTime не найден — конфигуратор не смог определить время RTC, "
                "укажите time явно.");
            for (const char* field : {"dow", "hours", "minutes", "seconds"})
            {
                result.provenance.push_back(makeUnresolvedProvenance(std::string("time.") + field));
            }
        }
        else
        {
            const std::vector<RawFieldAssignment> local_fields =
                resolveLocalStructFields(source_file, call->args[1]);
            mapRtcIntField(result, time_object, local_fields, "Dow", "dow", "time.dow");
            mapRtcIntField(result, time_object, local_fields, "Hours", "hours", "time.hours");
            mapRtcIntField(result, time_object, local_fields, "Minutes", "minutes", "time.minutes");
            mapRtcIntField(result, time_object, local_fields, "Seconds", "seconds", "time.seconds");
        }
        result.json_block["time"] = time_object;
    }

    // ---- date (из HAL_RTC_SetDate(&hrtc, &sDate)) ----
    {
        nlohmann::json date_object = nlohmann::json::object();
        const RawTrailingCall* call = findTrailingCall(block, "HAL_RTC_SetDate");
        if (call == nullptr || call->args.size() < 2)
        {
            date_object["century"] = 0;
            date_object["day"] = 1;
            date_object["month"] = 1;
            date_object["year"] = 0;
            result.notes.push_back(
                "HAL_RTC_SetDate не найден — конфигуратор не смог определить дату RTC, "
                "укажите date явно.");
            for (const char* field : {"century", "day", "month", "year"})
            {
                result.provenance.push_back(makeUnresolvedProvenance(std::string("date.") + field));
            }
        }
        else
        {
            const std::vector<RawFieldAssignment> local_fields =
                resolveLocalStructFields(source_file, call->args[1]);
            mapRtcIntField(result, date_object, local_fields, "Century", "century", "date.century");
            mapRtcIntField(result, date_object, local_fields, "Day", "day", "date.day");
            mapRtcIntField(result, date_object, local_fields, "Month", "month", "date.month");
            mapRtcIntField(result, date_object, local_fields, "Year", "year", "date.year");
        }
        result.json_block["date"] = date_object;
    }

    // ---- alarm (необязательный, из HAL_RTC_SetAlarm(&hrtc, &sAlarm)) ----
    const RawTrailingCall* alarm_call = findTrailingCall(block, "HAL_RTC_SetAlarm");
    if (alarm_call != nullptr && alarm_call->args.size() >= 2)
    {
        const std::vector<RawFieldAssignment> local_fields =
            resolveLocalStructFields(source_file, alarm_call->args[1]);

        nlohmann::json alarm_object = nlohmann::json::object();

        nlohmann::json alarm_time = nlohmann::json::object();
        mapRtcIntField(result, alarm_time, local_fields, "AlarmTime.Dow", "dow", "alarm.time.dow");
        mapRtcIntField(result, alarm_time, local_fields, "AlarmTime.Hours", "hours", "alarm.time.hours");
        mapRtcIntField(result, alarm_time, local_fields, "AlarmTime.Minutes", "minutes", "alarm.time.minutes");
        mapRtcIntField(result, alarm_time, local_fields, "AlarmTime.Seconds", "seconds", "alarm.time.seconds");
        alarm_object["time"] = alarm_time;

        nlohmann::json alarm_date = nlohmann::json::object();
        mapRtcIntField(result, alarm_date, local_fields, "AlarmDate.Century", "century", "alarm.date.century");
        mapRtcIntField(result, alarm_date, local_fields, "AlarmDate.Day", "day", "alarm.date.day");
        mapRtcIntField(result, alarm_date, local_fields, "AlarmDate.Month", "month", "alarm.date.month");
        mapRtcIntField(result, alarm_date, local_fields, "AlarmDate.Year", "year", "alarm.date.year");
        alarm_object["date"] = alarm_date;

        const RawFieldAssignment* mask_time = lastAssignment(local_fields, "MaskAlarmTime");
        const RawFieldAssignment* mask_date = lastAssignment(local_fields, "MaskAlarmDate");
        if (mask_time != nullptr || mask_date != nullptr)
        {
            alarm_object["compare_fields"] = parseCompareFields(
                mask_time != nullptr ? mask_time->raw_value_text : "",
                mask_date != nullptr ? mask_date->raw_value_text : "");
        }

        // Interrupts.Alarm — поле самого хендла hrtc, не sAlarm (см.
        // Hal_RTC_IRQ/src/main.c:140: "hrtc.Interrupts.Alarm = RTC_ALARM_
        // IRQ_ENABLE;" перед HAL_RTC_InterruptInit(&hrtc)).
        const RawFieldAssignment* interrupts = lastAssignment(block.assignments, "Interrupts.Alarm");
        if (interrupts != nullptr)
        {
            const bool enabled = trimText(interrupts->raw_value_text) == "RTC_ALARM_IRQ_ENABLE";
            alarm_object["interrupt_enabled"] = enabled;
            result.provenance.push_back(makeProvenance("alarm.interrupt_enabled", *interrupts, false));
        }

        result.json_block["alarm"] = alarm_object;
    }

    return result;
}

} // namespace reverse_import
