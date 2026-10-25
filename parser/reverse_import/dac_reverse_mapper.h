//
// Типовой маппер reverse-import для DAC: превращает RawInitBlock, собранный
// reverse_import_scanner.h из "hdac1.Instance_dac = HAL_DACn;" / "hdac1.Init.
// <field> = <value>;" присваиваний вокруг HAL_DAC_Init, в JSON-блок формы
// schemas/dac.json.
//
// DAC — два физических канала (DAC_InitTypeDef.Instance_dac = HAL_DAC0/
// HAL_DAC1, см. schemas/dac.json, в отличие от ADC/CRYPTO — по образцу
// adc_reverse_mapper.h). Нераспознанные значения полей НЕ получают угаданный
// дефолт — см. feedback_reverse_import_unresolved_fields_policy в памяти
// ассистента: ставится placeholder + FieldProvenance::unresolved = true,
// чтобы GUI подсветил поле красным, а кодоген проекта заблокировался, пока
// пользователь не укажет значение явно.
//
#ifndef MIK32_CONFIGURATOR_DAC_REVERSE_MAPPER_H
#define MIK32_CONFIGURATOR_DAC_REVERSE_MAPPER_H

#include <optional>
#include <string>

#include "json.hpp"
#include "mapped_block.h"
#include "reverse_import_scanner.h"

namespace reverse_import
{

// block.handle_type должен быть "DAC_HandleTypeDef", иначе nullopt.
std::optional<MappedBlock> mapDacBlock(const RawInitBlock& block);

std::optional<std::string> serializeDacFieldValue(
    const std::string& field_name,
    const nlohmann::json& new_value,
    const std::string& original_raw_value_text);

} // namespace reverse_import

#endif //MIK32_CONFIGURATOR_DAC_REVERSE_MAPPER_H
