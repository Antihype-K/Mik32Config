//
// Типовой маппер reverse-import для CRC: превращает RawInitBlock, собранный
// вокруг HAL_CRC_Init, в JSON-блок формы schemas/crc.json. Всегда
// восстанавливает явную группу полей (poly/init/input_reverse/output_reverse/
// output_inversion), не "preset" — preset в парсере (crc_parser.cpp) это
// просто сахар, разворачиваемый в те же поля ДО кодогена, так что явные
// поля — полный и достаточный эквивалент.
//
// CRC — единственный глобальный блок (CRC_HandleTypeDef.Instance = CRC, нет
// instance, см. schemas/crc.json), как и RTC.
//
#ifndef MIK32_CONFIGURATOR_CRC_REVERSE_MAPPER_H
#define MIK32_CONFIGURATOR_CRC_REVERSE_MAPPER_H

#include <optional>
#include <string>

#include "json.hpp"
#include "mapped_block.h"
#include "reverse_import_scanner.h"

namespace reverse_import
{

// block.handle_type должен быть "CRC_HandleTypeDef", иначе nullopt.
std::optional<MappedBlock> mapCrcBlock(const RawInitBlock& block);

std::optional<std::string> serializeCrcFieldValue(
    const std::string& field_name,
    const nlohmann::json& new_value,
    const std::string& original_raw_value_text);

} // namespace reverse_import

#endif //MIK32_CONFIGURATOR_CRC_REVERSE_MAPPER_H
