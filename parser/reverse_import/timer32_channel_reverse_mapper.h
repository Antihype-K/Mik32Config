//
// Маппер для TIMER32_CHANNEL_HandleTypeDef — единственный из типовых
// мапперов, который не производит MappedBlock (отдельный периферийный блок
// верхнего уровня), а фрагмент "channels"-массива ВНУТРИ родительского
// timer32-блока (schemas/timer32.json, поле "channels" — только для
// instance 1/2). Поэтому у него другой контракт: см. reverse_import.cpp,
// где результат вставляется в уже существующий json_block родителя.
//
// Раньше этот тип периферии считался НЕПОДДЕРЖИВАЕМЫМ (см. историю
// timer32_reverse_mapper.h): казалось, что связь "какому htimer32_N
// принадлежит канал" восстановить нельзя, потому что канал ссылается на
// таймер через значение поля (TimerInstance = htimer32_1.Instance), а не
// имя хендла. На практике это неверно: ПРАВАЯ ЧАСТЬ присваивания — это
// ТЕКСТ "htimer32_1.Instance" (см. mik32_fixed_v3, main.cpp:555 и
// аналогичные строки) — имя родительского хендла лежит в этом тексте
// буквально, читать его нужно как текст, а не как C-выражение.
//
#ifndef MIK32_CONFIGURATOR_TIMER32_CHANNEL_REVERSE_MAPPER_H
#define MIK32_CONFIGURATOR_TIMER32_CHANNEL_REVERSE_MAPPER_H

#include <optional>
#include <string>
#include <vector>

#include "json.hpp"
#include "mapped_block.h"
#include "reverse_import_scanner.h"

namespace reverse_import
{

struct MappedTimer32Channel
{
    // Имя хендла родительского Timer32, взятое из текста присваивания
    // "TimerInstance = <parent>.Instance;" — то, что стоит слева от точки.
    std::string parent_handle_name;

    // Форма — ровно item_fields из schemas/timer32.json::channels (index,
    // mode, ocr, pwm_invert, capture_edge, noise_filter).
    nlohmann::json channel_json;

    // По одной записи на КАЖДОЕ поле channel_json (json_field — простое имя
    // поля: "mode", "ocr" и т.п., БЕЗ префикса "channels[N]." — префикс с
    // позицией канала в итоговом массиве добавляет reverse_import.cpp в
    // момент присоединения канала к родителю, эта структура сама не знает
    // свою будущую позицию). Позволяет reverse_import_apply различать
    // правки ОТДЕЛЬНЫХ полей ОТДЕЛЬНЫХ каналов, а не весь массив "channels"
    // одним неделимым полем, как было раньше.
    std::vector<FieldProvenance> provenance;

    std::vector<std::string> notes;
};

// Возвращает nullopt ТОЛЬКО если block.handle_type не
// TIMER32_CHANNEL_HandleTypeDef (защитная проверка, не должна срабатывать
// при правильной диспетчеризации). Если TimerInstance не найден/не
// распознан как "<handle>.Instance", функция ВСЁ РАВНО возвращает значение
// (не nullopt) с ПУСТЫМ parent_handle_name и объясняющей note — иначе эта
// note потерялась бы вместе со всем результатом. Вызывающая сторона
// (reverse_import.cpp) обязана сама проверить
// `mapped_channel->parent_handle_name.empty()` перед поиском родителя.
std::optional<MappedTimer32Channel> mapTimer32ChannelBlock(const RawInitBlock& block);

// Сериализация ОДНОГО поля ОДНОГО канала обратно в C-текст правой части
// присваивания — та же роль, что serialize<Type>FieldValue у остальных
// мапперов, но для полей ВНУТРИ channel_json ("index", "mode", "ocr",
// "pwm_invert", "capture_edge", "noise_filter"), а не для полей самого
// родительского timer32-блока (см. serializeTimer32FieldValue в
// timer32_reverse_mapper.h — та функция про index/prescaler/top/count_mode
// РОДИТЕЛЯ, не про каналы).
std::optional<std::string> serializeTimer32ChannelFieldValue(
    const std::string& field_name,
    const nlohmann::json& new_value,
    const std::string& original_raw_value_text);

} // namespace reverse_import

#endif //MIK32_CONFIGURATOR_TIMER32_CHANNEL_REVERSE_MAPPER_H
