//
// Типовой маппер reverse-import для RTC: превращает RawInitBlock в JSON-блок
// формы schemas/rtc.json ("time"/"date"/необязательный "alarm").
//
// RTC — единственная периферия без HAL_RTC_Init (см. reverse_import_scanner.
// cpp: HAL_RTC_Enable используется как закрывающий блок вызов вместо Init).
// Сами время/дата/будильник передаются не присваиваниями полей хендла, а
// через локальные структуры-значения (RTC_TimeTypeDef sTime; sTime.Dow = ...;
// HAL_RTC_SetTime(&hrtc, &sTime);) — их поля читает local_struct_field_
// resolver.h по имени переменной из trailing_calls, а не сам сканер.
//
#ifndef MIK32_CONFIGURATOR_RTC_REVERSE_MAPPER_H
#define MIK32_CONFIGURATOR_RTC_REVERSE_MAPPER_H

#include <optional>

#include "mapped_block.h"
#include "reverse_import_scanner.h"

namespace reverse_import
{

// block.handle_type должен быть "RTC_HandleTypeDef", иначе nullopt.
std::optional<MappedBlock> mapRtcBlock(const RawInitBlock& block);

// Обратная сериализация для точечной правки. field_name — ПОСЛЕДНИЙ сегмент
// составного json_field (вызывающий код в reverse_import_apply.cpp обрезает
// префикс "time."/"date."/"alarm.time."/"alarm.date."). compare_fields не
// поддерживается (значение размазано по битовой маске из нескольких
// макросов OR'ом — небезопасно восстанавливать без угадывания порядка/набора).
std::optional<std::string> serializeRtcFieldValue(
    const std::string& field_name,
    const nlohmann::json& new_value,
    const std::string& original_raw_value_text);

} // namespace reverse_import

#endif //MIK32_CONFIGURATOR_RTC_REVERSE_MAPPER_H
