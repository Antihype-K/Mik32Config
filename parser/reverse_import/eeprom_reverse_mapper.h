//
// Типовой маппер reverse-import для EEPROM: превращает RawInitBlock,
// собранный reverse_import_scanner.h из "heeprom.<field> = <value>;"
// присваиваний, в JSON-блок формы schemas/eeprom.json.
//
// EEPROM — единственный глобальный блок конфигуратора (heeprom.Instance =
// EEPROM_REGS, без нумерации 0/1/2, см. schemas/eeprom.json
// "_comment_max_instances") — как RTC/CRC/CRYPTO. Нет instance в схеме,
// поэтому в mapEepromBlock() синтезируется instance=0 для совпадения с
// конвенцией GUI-дерева (gui/project_document.cpp::defaultBlock()), тот же
// фикс, что уже сделан для ADC/CRYPTO.
//
// ВАЖНО (CLAUDE.md): EEPROM НИКОГДА не тестируется на реальной плате (два
// реальных инцидента, плата переставала отвечать). Это не относится к
// данному модулю — reverse-import — чистый парсер существующего текста
// проекта, ничего не прошивает и не выполняет.
//
#ifndef MIK32_CONFIGURATOR_EEPROM_REVERSE_MAPPER_H
#define MIK32_CONFIGURATOR_EEPROM_REVERSE_MAPPER_H

#include <optional>
#include <string>

#include "json.hpp"
#include "mapped_block.h"
#include "reverse_import_scanner.h"

namespace reverse_import
{

// block.handle_type должен быть "HAL_EEPROM_HandleTypeDef", иначе nullopt.
std::optional<MappedBlock> mapEepromBlock(const RawInitBlock& block);

// Обратная сериализация для точечной правки — см. mapMode/parseEccEnabled/
// parseSingleErrorInterruptEnabled в .cpp за списком распознаваемых HAL-макросов.
std::optional<std::string> serializeEepromFieldValue(
    const std::string& field_name,
    const nlohmann::json& new_value,
    const std::string& original_raw_value_text);

} // namespace reverse_import

#endif //MIK32_CONFIGURATOR_EEPROM_REVERSE_MAPPER_H
