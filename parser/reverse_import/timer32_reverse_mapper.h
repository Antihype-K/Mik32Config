//
// Типовой маппер reverse-import для Timer32: превращает один RawInitBlock со
// handle_type == "TIMER32_HandleTypeDef" (см. reverse_import_scanner.h) в
// MappedBlock формы schemas/timer32.json (см. mapped_block.h — контракт
// форм данных общий для всех четырёх типовых мапперов).
//
// ОГРАНИЧЕНИЕ (сознательное, не забытое): ШИМ/захват/сравнение-каналы
// Timer32_1/Timer32_2 в реальном коде (mik32_fixed_v3) — ОТДЕЛЬНЫЕ
// RawInitBlock с handle_type == "TIMER32_CHANNEL_HandleTypeDef"
// (TIMER32_CHANNEL_HandleTypeDef, не TIMER32_HandleTypeDef), заведённые
// через отдельные переменные (motor_htimer32_1_channel0 и т.п.), без
// надёжного текстового способа связать их с родительским таймером —
// TimerInstance у канала — это указатель (htimer32_1.Instance), а не имя
// хендла таймера, сканер не выполняет присваивания и не умеет сопоставить
// "какому из htimer32_0/1/2 принадлежит этот канал" не выдумывая связь.
// mapTimer32Block() поэтому возвращает nullopt для блоков с
// handle_type == "TIMER32_CHANNEL_HandleTypeDef" — они вычленены сканером,
// но НЕ отображаются в JSON поле "channels" в этой версии. См. отчёт
// автора маппера (текст задания к этой фиче) для подробностей решения.
//
#ifndef MIK32_CONFIGURATOR_TIMER32_REVERSE_MAPPER_H
#define MIK32_CONFIGURATOR_TIMER32_REVERSE_MAPPER_H

#include <optional>
#include <string>

#include "json.hpp"
#include "mapped_block.h"
#include "reverse_import_scanner.h"

namespace reverse_import
{

// Разбирает один Init-блок TIMER32_HandleTypeDef. Возвращает nullopt, если
// block.handle_type не TIMER32_HandleTypeDef — в т.ч. для
// TIMER32_CHANNEL_HandleTypeDef (см. ограничение выше). Во всех остальных
// случаях, включая нераспознанные/вычисляемые (не литеральные) значения
// полей, возвращает MappedBlock с notes, объясняющими примененные заглушки.
std::optional<MappedBlock> mapTimer32Block(const RawInitBlock& block);

// Строит новый C-литерал для правой части присваивания поля json_field ==
// field_name ("instance", "prescaler", "count_mode", "top"; "channels" не
// поддержан — см. ограничение выше, всегда возвращает nullopt) при новом
// значении new_value. Для чисел original_raw_value_text определяет формат
// вывода: если исходное значение было в hex ("0x..."), новое число тоже
// печатается в hex — сохраняем стиль исходника (см. Top = 0xFFFFFFFF в
// mik32_fixed_v3). Возвращает nullopt при несовместимом типе/значении.
std::optional<std::string> serializeTimer32FieldValue(
    const std::string& field_name,
    const nlohmann::json& new_value,
    const std::string& original_raw_value_text);

} // namespace reverse_import

#endif //MIK32_CONFIGURATOR_TIMER32_REVERSE_MAPPER_H
