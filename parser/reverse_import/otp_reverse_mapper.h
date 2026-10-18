//
// Типовой маппер reverse-import для OTP (однократно программируемая память):
// превращает RawInitBlock вокруг HAL_OTP_Init в JSON-блок формы
// schemas/otp.json. OTP_HandleTypeDef не имеет отдельного InitTypeDef — поле
// ReadMode лежит прямо на хендле, см. docs/examples/mik32-examples/HAL_OTP/
// src/main.c. OTP read-only по дизайну (AUDIT_TZ_PERIPHERY_GAPS.md) — маппер
// читает только ReadMode, HAL_OTP_Write*/ReadTest* не разбираются и не нужны.
//
// OTP — единственный глобальный блок (как ADC/DAC/RTC/CRC/TSENS).
//
#ifndef MIK32_CONFIGURATOR_OTP_REVERSE_MAPPER_H
#define MIK32_CONFIGURATOR_OTP_REVERSE_MAPPER_H

#include <optional>

#include "mapped_block.h"
#include "reverse_import_scanner.h"

namespace reverse_import
{

// block.handle_type должен быть "OTP_HandleTypeDef", иначе nullopt.
std::optional<MappedBlock> mapOtpBlock(const RawInitBlock& block);

} // namespace reverse_import

#endif //MIK32_CONFIGURATOR_OTP_REVERSE_MAPPER_H
