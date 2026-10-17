//
// Вклад OTP в общий сгенерированный проект (см. концепцию в
// codegen/project/peripheral_contribution.h). Единственный глобальный блок,
// без knowledge_base. ВАЖНО: генерирует только HAL_OTP_Init — запись
// (HAL_OTP_Write*) НИКОГДА не генерируется (см. parser/otp/otp_parser.h —
// OTP физически необратима).
//
#ifndef MIK32_CONFIGURATOR_OTP_CONTRIBUTION_H
#define MIK32_CONFIGURATOR_OTP_CONTRIBUTION_H

#include "otp_parser.h"
#include "parse_result.h"
#include "peripheral_contribution.h"

ParseResult<PeripheralContribution> buildOtpContribution(const OtpConfig& config);

#endif //MIK32_CONFIGURATOR_OTP_CONTRIBUTION_H
