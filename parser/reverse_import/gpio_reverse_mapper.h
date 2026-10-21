//
// Типовой маппер reverse-import для GPIO: превращает один RawInitBlock со
// handle_type == "GPIO_InitTypeDef" (см. reverse_import_scanner.h) в
// MappedBlock формы schemas/gpio.json (см. mapped_block.h — контракт форм
// данных общий для всех четырёх типовых мапперов).
//
// Особенность GPIO среди всей периферии: "порт" (instance) физически не
// является полем структуры GPIO_InitTypeDef — порт передаётся отдельным
// первым аргументом в HAL_GPIO_Init(GPIO_x, &GPIO_InitStruct), поэтому
// mapGpioBlock() читает block.init_call_args[0], а не block.assignments.
//
#ifndef MIK32_CONFIGURATOR_GPIO_REVERSE_MAPPER_H
#define MIK32_CONFIGURATOR_GPIO_REVERSE_MAPPER_H

#include <optional>
#include <string>
#include <unordered_map>

#include "json.hpp"
#include "mapped_block.h"
#include "reverse_import_scanner.h"

namespace reverse_import
{

// Разбирает один Init-блок GPIO_InitTypeDef. Возвращает nullopt только если
// block.handle_type не GPIO_InitTypeDef (маппер вызван на чужом типе блока
// оркестратором по ошибке) — во всех остальных случаях, включая
// нераспознанные значения полей, возвращает MappedBlock с notes,
// объясняющими примененные заглушки (цель фичи — вычленять ВСЮ периферию
// честно, а не тихо пропускать то, что не удалось разобрать до конца).
//
// macro_aliases — project-wide словарь "#define NAME IDENT" (ScannedProject::
// macro_aliases), используется ТОЛЬКО как подсказка при разборе instance/
// pins, если буквальный текст не распознан напрямую (пример: "#define
// STATUS_LED_PORT GPIO_2" в HAL_EEPROM). original_raw_value_text/
// original_full_line_text в provenance остаются как в исходнике — иначе
// reverse_patch_writer потеряет якорь для правки.
std::optional<MappedBlock> mapGpioBlock(
    const RawInitBlock& block,
    const std::unordered_map<std::string, std::string>& macro_aliases = {});

// Строит новый C-литерал для правой части присваивания поля json_field
// == field_name ("pins", "mode", "pull", "drive_strength"; "instance"
// поддержан для полноты, хотя в MappedBlock его provenance.editable сейчас
// всегда false — см. .cpp) при новом значении new_value. original_raw_value_text
// используется только для угадывания стиля разделителя "|" у pins (сохраняем
// эстетику исходника — " | " или "|" без пробелов), на сам разбор не влияет.
// Возвращает nullopt, если new_value не подходящего типа/значения для этого
// поля — вызывающая сторона (reverse_patch_writer) не должна писать в файл
// в этом случае.
std::optional<std::string> serializeGpioFieldValue(
    const std::string& field_name,
    const nlohmann::json& new_value,
    const std::string& original_raw_value_text);

} // namespace reverse_import

#endif //MIK32_CONFIGURATOR_GPIO_REVERSE_MAPPER_H
