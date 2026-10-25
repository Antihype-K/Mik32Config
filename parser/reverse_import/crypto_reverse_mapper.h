//
// Типовой маппер reverse-import для CRYPTO: превращает RawInitBlock,
// собранный reverse_import_scanner.h из "hcrypto.<field> = <value>;"
// присваиваний вокруг HAL_Crypto_Init, в JSON-блок формы schemas/crypto.json.
//
// CRYPTO — единственный глобальный блок (Crypto_HandleTypeDef.Instance =
// CRYPTO, нет instance в schemas/crypto.json), по образцу
// adc_reverse_mapper.h (synthetic instance=0 для GUI, см. .cpp). Ключ/IV
// сознательно НЕ читаются и не попадают в JSON (schemas/crypto.json,
// "_comment_no_key_no_iv") — секретные данные времени выполнения.
// Нераспознанные значения полей НЕ получают угаданный дефолт — см.
// feedback_reverse_import_unresolved_fields_policy в памяти ассистента:
// ставится placeholder + FieldProvenance::unresolved = true.
//
#ifndef MIK32_CONFIGURATOR_CRYPTO_REVERSE_MAPPER_H
#define MIK32_CONFIGURATOR_CRYPTO_REVERSE_MAPPER_H

#include <optional>
#include <string>

#include "json.hpp"
#include "mapped_block.h"
#include "reverse_import_scanner.h"

namespace reverse_import
{

// block.handle_type должен быть "Crypto_HandleTypeDef", иначе nullopt.
std::optional<MappedBlock> mapCryptoBlock(const RawInitBlock& block);

// Обратная сериализация для точечной правки — ключ/IV сюда никогда не
// попадают (см. комментарий выше про "_comment_no_key_no_iv").
std::optional<std::string> serializeCryptoFieldValue(
    const std::string& field_name,
    const nlohmann::json& new_value,
    const std::string& original_raw_value_text);

} // namespace reverse_import

#endif //MIK32_CONFIGURATOR_CRYPTO_REVERSE_MAPPER_H
