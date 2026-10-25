//
// Типовой маппер reverse-import для SPIFI: превращает RawInitBlock, собранный
// вокруг HAL_SPIFI_MemoryMode_Init, в JSON-блок формы schemas/spifi.json.
//
// Покрывает только каноническую, структурную форму вызова
// (SPIFI_MemoryModeConfig_HandleTypeDef hspifi; hspifi.CacheEnable = ...;
// HAL_SPIFI_MemoryMode_Init(&hspifi);) — ту же, что генерирует
// codegen/project/spifi_contribution.cpp. Вендорный пример
// docs/examples/mik32-examples/HAL_Blink_SPIFI/src/main.c использует другой,
// низкоуровневый паттерн (HAL_SPIFI_MemoryMode_Init_LL с сырыми значениями
// регистров CTRL/MCMD, собранными в локальной переменной) — этот паттерн
// reverse-import намеренно не распознаёт (не RawInitBlock вокруг хендла, см.
// docs/implementations/reverse_import_audit_2026-10-18.md): декомпозиция
// совмещённого битового литерала типа 0x400FFFFF обратно в отдельные
// документированные поля была бы угадыванием семантики, не чтением, —
// тот же риск, что уже поймал feedback_no_invented_register_bit_polarity.
//
// SPIFI — единственный глобальный блок (нет instance, см. schemas/spifi.json),
// как CRC/RTC.
//
#ifndef MIK32_CONFIGURATOR_SPIFI_REVERSE_MAPPER_H
#define MIK32_CONFIGURATOR_SPIFI_REVERSE_MAPPER_H

#include <optional>
#include <string>

#include "json.hpp"
#include "mapped_block.h"
#include "reverse_import_scanner.h"

namespace reverse_import
{

// block.handle_type должен быть "SPIFI_MemoryModeConfig_HandleTypeDef",
// иначе nullopt.
std::optional<MappedBlock> mapSpifiBlock(const RawInitBlock& block);

// Обратная сериализация для точечной правки — покрывает только поля,
// которые mapSpifiBlock умеет читать (см. .cpp, mapBoolField/mapIntField/
// mapEnumField): cache_enabled, cache_limit, cmd_interim_length, cmd_op_code,
// cmd_field_form, cmd_frame_form.
std::optional<std::string> serializeSpifiFieldValue(
    const std::string& field_name,
    const nlohmann::json& new_value,
    const std::string& original_raw_value_text);

} // namespace reverse_import

#endif //MIK32_CONFIGURATOR_SPIFI_REVERSE_MAPPER_H
