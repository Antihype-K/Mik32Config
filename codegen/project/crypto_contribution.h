//
// Вклад CRYPTO в общий сгенерированный проект (см. концепцию в
// codegen/project/peripheral_contribution.h). Единственный глобальный блок,
// без knowledge_base — все четыре enum-поля отображаются в фиксированные
// HAL-константы напрямую. Сознательно без ключа/IV — см.
// parser/crypto/crypto_parser.h.
//
#ifndef MIK32_CONFIGURATOR_CRYPTO_CONTRIBUTION_H
#define MIK32_CONFIGURATOR_CRYPTO_CONTRIBUTION_H

#include "crypto_parser.h"
#include "parse_result.h"
#include "peripheral_contribution.h"

ParseResult<PeripheralContribution> buildCryptoContribution(const CryptoConfig& config);

#endif //MIK32_CONFIGURATOR_CRYPTO_CONTRIBUTION_H
