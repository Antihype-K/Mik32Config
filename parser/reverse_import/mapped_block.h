//
// Общий контракт между четырьмя типовыми мапперами (uart/i2c/timer32/gpio
// reverse_mapper) и остальной reverse-import цепочкой (оркестратор,
// reverse_patch_writer, GUI). Сам по себе этот файл не содержит логики —
// только формы данных, которые mapUartBlock/mapI2cBlock/mapTimer32Block/
// mapGpioBlock обязаны заполнять одинаково, чтобы оркестратору не пришлось
// знать про различия между типами периферии.
//
#ifndef MIK32_CONFIGURATOR_MAPPED_BLOCK_H
#define MIK32_CONFIGURATOR_MAPPED_BLOCK_H

#include <string>
#include <vector>
#include "json.hpp"
#include "reverse_import_scanner.h"

namespace reverse_import
{

// Откуда взялось одно поле JSON-блока (schemas/<type>.json, верхнеуровневый
// ключ вроде "baud_rate", "pins", "clock_speed") и можно ли его редактировать
// в GUI с последующей записью правки обратно в исходник.
struct FieldProvenance
{
    std::string json_field;

    // location.file пустой = значение синтезировано мапером (поле в
    // исходнике не встретилось — например, GPIO drive_strength в
    // mik32_fixed_v3 нигде не задаётся явно), а не взято из конкретной
    // строки. Писать правку такого поля обратно в файл нельзя — это не
    // "перезаписать строку", а "добавить новую" (не реализовано в v1),
    // поэтому editable для таких полей ниже обязан быть false.
    SourceLocation location;

    // Точный текст правой части присваивания и всей строки, как в
    // исходнике — якорь для reverse_patch_writer::applyLineEdit. Пусто,
    // если location.file пусто (см. выше).
    std::string original_raw_value_text;
    std::string original_full_line_text;

    // true <=> serialize<Type>FieldValue способен построить корректный
    // C-литерал для НОВОГО значения этого поля (см. .cpp каждого маппера).
    // false — поле показывается в GUI как read-only при импортированном
    // проекте: либо потому что оно синтезировано (см. выше), либо потому
    // что исходное значение было вычисляемым выражением, а не литералом
    // (пример — TIMER32.Clock.Prescaler = (clock_frequency / timer_frequency)
    // - 1 в mik32_fixed_v3: локальная переменная, а не число — переписать
    // её "обратно" в исходный смысл кодогена нельзя, не разрушив функцию).
    bool editable = false;
};

struct MappedBlock
{
    // Форма ДОЛЖНА совпадать с соответствующей schemas/<type>.json:
    // обязательные поля из required_fields обязаны присутствовать (даже
    // если значение — синтезированный дефолт из notes), иначе
    // ProjectDocument::validate() отбракует блок при попытке отрисовать
    // его в существующей PropertiesPanel/дереве периферии.
    nlohmann::json json_block;

    std::vector<FieldProvenance> provenance;

    // Человекочитаемые предупреждения на русском: применённые дефолты,
    // нераспознанные значения enum, поля с вычисляемым (не литеральным)
    // значением и т.п. Показываются пользователю в GUI рядом с блоком —
    // не откладывать молча, цель фичи — "вычленять ВСЮ периферию" честно,
    // а не бесшовно.
    std::vector<std::string> notes;
};

} // namespace reverse_import

#endif //MIK32_CONFIGURATOR_MAPPED_BLOCK_H
