//
// Типовой маппер reverse-import для I2C: превращает RawInitBlock, собранный
// reverse_import_scanner.h из "hi2c0.<field> = <value>;"-присваиваний вокруг
// HAL_I2C_Init, в JSON-блок формы schemas/i2c.json — см. mapped_block.h за
// контрактом MappedBlock/FieldProvenance.
//
// Особый случай clock_speed (PRESC/SCLDEL/SDADEL/SCLH/SCLL, он же TIMINGR) —
// решение по нему и его обоснование см. в комментарии наверху .cpp.
//
#ifndef MIK32_CONFIGURATOR_I2C_REVERSE_MAPPER_H
#define MIK32_CONFIGURATOR_I2C_REVERSE_MAPPER_H

#include <optional>
#include <string>

#include "json.hpp"
#include "mapped_block.h"
#include "reverse_import_scanner.h"

namespace reverse_import
{

// block.handle_type должен быть "I2C_HandleTypeDef", иначе nullopt (это
// вообще не про I2C). instance не определён -> instance=0 + notes, блок
// всё равно возвращается.
std::optional<MappedBlock> mapI2cBlock(const RawInitBlock& block);

// См. uart_reverse_mapper.h за общим смыслом контракта. Для "clock_speed"
// всегда возвращает nullopt — обоснование в комментарии наверху .cpp.
std::optional<std::string> serializeI2cFieldValue(
    const std::string& field_name,
    const nlohmann::json& new_value,
    const std::string& original_raw_value_text);

} // namespace reverse_import

#endif //MIK32_CONFIGURATOR_I2C_REVERSE_MAPPER_H
