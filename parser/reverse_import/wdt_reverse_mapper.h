//
// Типовой маппер reverse-import для WDT (сторожевой таймер): превращает
// RawInitBlock вокруг HAL_WDT_Init в JSON-блок формы schemas/wdt.json.
// Поля лежат в hwdt.Init.Clock/.ReloadMs, см. docs/examples/mik32-examples/
// HAL_WDT/src/main.c. Разбирается только конфигурация Init — HAL_WDT_Start/
// Refresh не входят в schemas/wdt.json (форвард-кодоген их тоже сознательно
// не генерирует, см. wdt_contribution.cpp) и здесь не разбираются.
//
// WDT — единственный глобальный блок (как ADC/DAC/RTC/CRC/TSENS/OTP).
//
#ifndef MIK32_CONFIGURATOR_WDT_REVERSE_MAPPER_H
#define MIK32_CONFIGURATOR_WDT_REVERSE_MAPPER_H

#include <optional>

#include "mapped_block.h"
#include "reverse_import_scanner.h"

namespace reverse_import
{

// block.handle_type должен быть "WDT_HandleTypeDef", иначе nullopt.
std::optional<MappedBlock> mapWdtBlock(const RawInitBlock& block);

// Обратная сериализация для точечной правки (reverse_import_apply.cpp).
// Поддерживает clock_source/reload_ms — ровно те поля, для которых mapWdtBlock
// находит literal/enum-присваивание (editable=true).
std::optional<std::string> serializeWdtFieldValue(
    const std::string& field_name,
    const nlohmann::json& new_value,
    const std::string& original_raw_value_text);

} // namespace reverse_import

#endif //MIK32_CONFIGURATOR_WDT_REVERSE_MAPPER_H
