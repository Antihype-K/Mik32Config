//
// RtcConfig / parseRtcConfig — по образцу parser/timer32/timer32_parser.h.
// AUDIT_TZ_PERIPHERY_GAPS.md, Тир 2. RTC — единственный глобальный блок
// (нет instance, см. schemas/rtc.json).
//
#ifndef MIK32_CONFIGURATOR_RTC_PARSER_H
#define MIK32_CONFIGURATOR_RTC_PARSER_H
#include <string>
#include <vector>
#include "parse_result.h"
#include "json.hpp"

struct RtcConfig
{
    std::string type;

    // mik32_hal_rtc.h, RTC_TimeTypeDef — текущее время (устанавливается
    // один раз, дальше RTC считает сама).
    int dow;
    int hours;
    int minutes;
    int seconds;

    // mik32_hal_rtc.h, RTC_DateTypeDef.
    int century;
    int day;
    int month;
    int year;

    // Необязательный будильник (RTC_AlarmTypeDef). has_alarm=false — все
    // поля ниже не читаются кодогеном.
    bool has_alarm = false;
    int alarm_dow = 0;
    int alarm_hours = 0;
    int alarm_minutes = 0;
    int alarm_seconds = 0;
    int alarm_century = 0;
    int alarm_day = 0;
    int alarm_month = 0;
    int alarm_year = 0;

    // Подмножество {"dow","hours","minutes","seconds","century","day","month","year"} —
    // какие поля будильник обязан сравнить, чтобы сработать (MaskAlarmTime/
    // MaskAlarmDate). Дефолт (если alarm задан, но compare_fields нет) — все восемь.
    std::vector<std::string> alarm_compare_fields;
    bool alarm_interrupt_enabled = false;
};

ParseResult<RtcConfig> parseRtcConfig(const std::string& file_name_path);

// Блочная точка входа — см. обоснование в parser/gpio/gpio_parser.h.
ParseResult<RtcConfig> parseRtcBlock(const nlohmann::json& rtc);

#endif //MIK32_CONFIGURATOR_RTC_PARSER_H
