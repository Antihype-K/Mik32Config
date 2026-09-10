//
// "Применить правки из GUI обратно в исходники" — второй (и последний)
// оркестрирующий слой reverse-import, после reverse_import.h. Если тот
// склеивал сканер+мапперы в одну сторону (файлы -> JSON), этот — в
// обратную: JSON ДО и ПОСЛЕ правок пользователя в PropertiesPanel + сохранённый
// при импорте провенанс -> набор LineEditRequest для reverse_patch_writer.
//
// Почему это отдельная функция, а не метод ProjectDocument/GUI-виджета:
// ProjectDocument (gui/project_document.h) намеренно ничего не знает про
// reverse-import (см. его собственный комментарий про границы ядра) — он
// работает с одним json-документом и не хранит "исходное значение на
// момент импорта". Эту разницу (что изменилось) и её причину (можно ли
// записать обратно и почему нет, если нельзя) вычисляет этот модуль,
// принимая оба снимка документа как параметры, а не подглядывая в историю.
//
#ifndef MIK32_CONFIGURATOR_REVERSE_IMPORT_APPLY_H
#define MIK32_CONFIGURATOR_REVERSE_IMPORT_APPLY_H

#include <string>
#include <vector>

#include "json.hpp"
#include "mapped_block.h"
#include "reverse_patch_writer.h"

namespace reverse_import
{

// Поле, которое пользователь поменял в GUI, но которое НЕЛЬЗЯ записать
// обратно в исходник — потому что оно не встретилось при импорте
// (json_field без provenance), потому что ни одна provenance-запись для
// него не editable (синтезированный дефолт/вычисляемое выражение — см.
// FieldProvenance::editable в mapped_block.h), потому что serialize<Type>
// FieldValue отказался (несовместимое значение), либо потому что сам БЛОК
// (не только поле) не сопоставляется 1:1 со снимком на момент импорта
// (пользователь добавил/удалил блок, поменял instance — такие структурные
// правки применения точечных строк не поддерживают, для них нужна полная
// кодогенерация, не reverse-import).
struct UnpatchableChange
{
    int block_index = -1;
    std::string field_name;
    std::string reason;
};

struct ApplyReport
{
    // По одной записи на каждую РЕАЛЬНО попытанную запись строки (см.
    // reverse_patch_writer.h::applyLineEdits — один json_field может дать
    // несколько LineEditRequest, если физически представлен несколькими
    // строками, например parity/direction у uart или TIMINGR у i2c).
    std::vector<PatchResult> patch_results;

    std::vector<UnpatchableChange> unpatchable;
};

// original_peripherals/current_peripherals — document()["peripherals"] на
// момент импорта и сейчас. block_provenance — ImportedProject::block_provenance,
// сохранённый в момент импорта (тот же порядок и размер, что и
// original_peripherals). Ничего не пишет на диск сама — делегирует
// применённые LineEditRequest в reverse_patch_writer::applyLineEdits.
ApplyReport applyImportedChanges(
    const nlohmann::json& original_peripherals,
    const nlohmann::json& current_peripherals,
    const std::vector<std::vector<FieldProvenance>>& block_provenance);

} // namespace reverse_import

#endif //MIK32_CONFIGURATOR_REVERSE_IMPORT_APPLY_H
