//
// Типовой маппер reverse-import для ADC: превращает RawInitBlock, собранный
// reverse_import_scanner.h из "hadc.Init.<field> = <value>;" присваиваний
// вокруг HAL_ADC_Init, в JSON-блок формы schemas/adc.json.
//
// ADC — единственный глобальный блок (ADC_HandleTypeDef.Instance = ANALOG_REG,
// нет instance, см. schemas/adc.json). Нераспознанные значения полей НЕ
// получают угаданный дефолт — см. feedback_reverse_import_unresolved_fields_
// policy в памяти ассистента: ставится placeholder + FieldProvenance::
// unresolved = true, чтобы GUI подсветил поле красным, а кодоген проекта
// заблокировался, пока пользователь не укажет значение явно.
//
#ifndef MIK32_CONFIGURATOR_ADC_REVERSE_MAPPER_H
#define MIK32_CONFIGURATOR_ADC_REVERSE_MAPPER_H

#include <optional>
#include <string>

#include "json.hpp"
#include "mapped_block.h"
#include "reverse_import_scanner.h"

namespace reverse_import
{

// block.handle_type должен быть "ADC_HandleTypeDef", иначе nullopt.
std::optional<MappedBlock> mapAdcBlock(const RawInitBlock& block);

std::optional<std::string> serializeAdcFieldValue(
    const std::string& field_name,
    const nlohmann::json& new_value,
    const std::string& original_raw_value_text);

} // namespace reverse_import

#endif //MIK32_CONFIGURATOR_ADC_REVERSE_MAPPER_H
