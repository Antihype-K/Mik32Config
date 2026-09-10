//
// Типовой маппер reverse-import для SPI: превращает один RawInitBlock со
// handle_type == "SPI_HandleTypeDef" (см. reverse_import_scanner.h) в
// MappedBlock формы schemas/spi.json.
//
// Самый простой из пяти типовых мапперов: в отличие от I2C (clock_speed
// размазан по 5 строкам), GPIO (instance — аргумент вызова, не поле) и
// Timer16 (mode/arr/cmp — аргументы вызова ПОСЛЕ Init), у SPI ВСЕ нужные
// схеме поля — обычные литеральные присваивания полей структуры,
// проверено по реальному выводу собственного кодогена (generated_projects/
// lllll/src/mik32_init.c) — обнаружено при импорте PlatformIO-проекта,
// когда SPI попадал в notes как "неизвестный тип хендла".
//
// baud_rate_div/manual_cs/decoder/chip_select — по schemas/spi.json только
// для mode=master (см. её комментарий: HAL_SPI_Init форсирует их для slave
// независимо от структуры) — для slave-блоков эти поля НЕ синтезируются
// заглушками, а просто отсутствуют в json_block (required_fields схемы их
// не требует).
//
#ifndef MIK32_CONFIGURATOR_SPI_REVERSE_MAPPER_H
#define MIK32_CONFIGURATOR_SPI_REVERSE_MAPPER_H

#include <optional>
#include <string>

#include "json.hpp"
#include "mapped_block.h"
#include "reverse_import_scanner.h"

namespace reverse_import
{

std::optional<MappedBlock> mapSpiBlock(const RawInitBlock& block);

std::optional<std::string> serializeSpiFieldValue(
    const std::string& field_name,
    const nlohmann::json& new_value,
    const std::string& original_raw_value_text);

} // namespace reverse_import

#endif //MIK32_CONFIGURATOR_SPI_REVERSE_MAPPER_H
