//
// Загрузчик knowledge_base/rtc.json — по образцу timer32_knowledge.h.
//
#ifndef MIK32_CONFIGURATOR_RTC_KNOWLEDGE_H
#define MIK32_CONFIGURATOR_RTC_KNOWLEDGE_H

#include <string>
#include <map>
#include "parse_result.h"

struct RtcKnowledge
{
    // "1".."7" -> "RTC_WEEKDAY_MONDAY".."SUNDAY".
    std::map<std::string, std::string> weekday_hal_enum;
    // "1".."12" -> "RTC_MONTH_JANUARY".."DECEMBER".
    std::map<std::string, std::string> month_hal_enum;

    // "dow"/"hours"/"minutes"/"seconds" -> "RTC_TALRM_C*_M".
    std::map<std::string, std::string> alarm_compare_time_mask;
    // "century"/"day"/"month"/"year" -> "RTC_DALRM_C*_M".
    std::map<std::string, std::string> alarm_compare_date_mask;
};

ParseResult<RtcKnowledge> loadRtcKnowledge(const std::string& file_path);

#endif //MIK32_CONFIGURATOR_RTC_KNOWLEDGE_H
