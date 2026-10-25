//
// mapCryptoBlock — см. crypto_reverse_mapper.h за контрактом. Разбор основан
// на docs/examples/mik32-examples/HAL_DMA_Crypto/src/main.c (Crypto_Init,
// строки ~166-176): hcrypto.Algorithm = CRYPTO_ALG_KUZNECHIK; .CipherMode =
// CRYPTO_CIPHER_MODE_ECB; .SwapMode = CRYPTO_SWAP_MODE_NONE; .OrderMode =
// CRYPTO_ORDER_MODE_MSW.
//
#include "crypto_reverse_mapper.h"

namespace reverse_import
{

namespace
{

std::string trimText(const std::string& text)
{
    const std::size_t begin = text.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos)
    {
        return "";
    }
    const std::size_t end = text.find_last_not_of(" \t\r\n");
    return text.substr(begin, end - begin + 1);
}

const RawFieldAssignment* lastAssignment(const RawInitBlock& block, const std::string& field_path)
{
    const RawFieldAssignment* found = nullptr;
    for (const RawFieldAssignment& assignment : block.assignments)
    {
        if (assignment.field_path == field_path)
        {
            found = &assignment;
        }
    }
    return found;
}

FieldProvenance makeProvenance(const std::string& json_field, const RawFieldAssignment& assignment, bool editable)
{
    FieldProvenance provenance;
    provenance.json_field = json_field;
    provenance.location = assignment.location;
    provenance.original_raw_value_text = assignment.raw_value_text;
    provenance.original_full_line_text = assignment.full_line_text;
    provenance.editable = editable;
    return provenance;
}

// unresolved=true — для ОБЯЗАТЕЛЬНЫХ полей (algorithm/cipher_mode), см.
// feedback_reverse_import_unresolved_fields_policy: ни "поле не найдено", ни
// "поле найдено, но текст не распознан" не получают угаданный дефолт без
// отметки.
FieldProvenance makeUnresolvedProvenance(const std::string& json_field)
{
    FieldProvenance provenance;
    provenance.json_field = json_field;
    provenance.editable = false;
    provenance.unresolved = true;
    return provenance;
}

std::optional<std::string> parseAlgorithm(const std::string& text)
{
    const std::string trimmed = trimText(text);
    if (trimmed == "CRYPTO_ALG_KUZNECHIK")
    {
        return std::string("kuznechik");
    }
    if (trimmed == "CRYPTO_ALG_MAGMA")
    {
        return std::string("magma");
    }
    if (trimmed == "CRYPTO_ALG_AES")
    {
        return std::string("aes");
    }
    return std::nullopt;
}

std::optional<std::string> parseCipherMode(const std::string& text)
{
    const std::string trimmed = trimText(text);
    if (trimmed == "CRYPTO_CIPHER_MODE_ECB")
    {
        return std::string("ecb");
    }
    if (trimmed == "CRYPTO_CIPHER_MODE_CBC")
    {
        return std::string("cbc");
    }
    if (trimmed == "CRYPTO_CIPHER_MODE_CTR")
    {
        return std::string("ctr");
    }
    return std::nullopt;
}

std::optional<std::string> parseSwapMode(const std::string& text)
{
    const std::string trimmed = trimText(text);
    if (trimmed == "CRYPTO_SWAP_MODE_NONE")
    {
        return std::string("none");
    }
    if (trimmed == "CRYPTO_SWAP_MODE_HALFWORD")
    {
        return std::string("halfword");
    }
    if (trimmed == "CRYPTO_SWAP_MODE_BYTE")
    {
        return std::string("byte");
    }
    if (trimmed == "CRYPTO_SWAP_MODE_BIT")
    {
        return std::string("bit");
    }
    return std::nullopt;
}

std::optional<std::string> parseOrderMode(const std::string& text)
{
    const std::string trimmed = trimText(text);
    if (trimmed == "CRYPTO_ORDER_MODE_LSW")
    {
        return std::string("lsw");
    }
    if (trimmed == "CRYPTO_ORDER_MODE_MSW")
    {
        return std::string("msw");
    }
    return std::nullopt;
}

} // namespace

std::optional<MappedBlock> mapCryptoBlock(const RawInitBlock& block)
{
    if (block.handle_type != "Crypto_HandleTypeDef")
    {
        return std::nullopt;
    }

    MappedBlock result;
    result.json_block["type"] = "crypto";
    // CRYPTO — глобальный блок без instance в schemas/crypto.json, но GUI
    // (project_document.cpp: blockIndices/isEnabled/defaultBlock) сопоставляет
    // любой блок с узлом дерева по паре (type, instance) — без явного
    // instance=0 дерево видит блок как несуществующий, хотя он реально
    // импортирован (см. тот же фикс в adc_reverse_mapper.cpp). Не HAL-поле,
    // не editable, не unresolved — синтетическая GUI-привязка.
    result.json_block["instance"] = 0;

    // ---- algorithm (обязательное, из "Algorithm = CRYPTO_ALG_*;") ----
    {
        const RawFieldAssignment* assignment = lastAssignment(block, "Algorithm");
        if (assignment == nullptr)
        {
            result.json_block["algorithm"] = "kuznechik";
            result.notes.push_back(
                "Algorithm не найден — конфигуратор не смог определить алгоритм шифрования, "
                "укажите algorithm явно.");
            result.provenance.push_back(makeUnresolvedProvenance("algorithm"));
        }
        else
        {
            const std::optional<std::string> value = parseAlgorithm(assignment->raw_value_text);
            if (value.has_value())
            {
                result.json_block["algorithm"] = *value;
                result.provenance.push_back(makeProvenance("algorithm", *assignment, false));
            }
            else
            {
                result.json_block["algorithm"] = "kuznechik";
                result.notes.push_back(
                    "Algorithm=\"" + assignment->raw_value_text + "\" (" + assignment->location.fileName() +
                    ":" + std::to_string(assignment->location.line) +
                    ") — конфигуратор не смог определить алгоритм шифрования, укажите "
                    "algorithm явно.");
                FieldProvenance provenance = makeUnresolvedProvenance("algorithm");
                provenance.location = assignment->location;
                provenance.original_raw_value_text = assignment->raw_value_text;
                provenance.original_full_line_text = assignment->full_line_text;
                result.provenance.push_back(provenance);
            }
        }
    }

    // ---- cipher_mode (обязательное, из "CipherMode = CRYPTO_CIPHER_MODE_*;") ----
    {
        const RawFieldAssignment* assignment = lastAssignment(block, "CipherMode");
        if (assignment == nullptr)
        {
            result.json_block["cipher_mode"] = "ecb";
            result.notes.push_back(
                "CipherMode не найден — конфигуратор не смог определить режим шифрования, "
                "укажите cipher_mode явно.");
            result.provenance.push_back(makeUnresolvedProvenance("cipher_mode"));
        }
        else
        {
            const std::optional<std::string> value = parseCipherMode(assignment->raw_value_text);
            if (value.has_value())
            {
                result.json_block["cipher_mode"] = *value;
                result.provenance.push_back(makeProvenance("cipher_mode", *assignment, false));
            }
            else
            {
                result.json_block["cipher_mode"] = "ecb";
                result.notes.push_back(
                    "CipherMode=\"" + assignment->raw_value_text + "\" (" + assignment->location.fileName() +
                    ":" + std::to_string(assignment->location.line) +
                    ") — конфигуратор не смог определить режим шифрования, укажите "
                    "cipher_mode явно.");
                FieldProvenance provenance = makeUnresolvedProvenance("cipher_mode");
                provenance.location = assignment->location;
                provenance.original_raw_value_text = assignment->raw_value_text;
                provenance.original_full_line_text = assignment->full_line_text;
                result.provenance.push_back(provenance);
            }
        }
    }

    // ---- swap_mode (опциональное, дефолт "none" из schemas/crypto.json — это
    // документированное поведение HAL, а не угаданный дефолт, unresolved не
    // ставится, если поле отсутствует) ----
    {
        const RawFieldAssignment* assignment = lastAssignment(block, "SwapMode");
        if (assignment == nullptr)
        {
            result.json_block["swap_mode"] = "none";
        }
        else
        {
            const std::optional<std::string> value = parseSwapMode(assignment->raw_value_text);
            if (value.has_value())
            {
                result.json_block["swap_mode"] = *value;
                result.provenance.push_back(makeProvenance("swap_mode", *assignment, false));
            }
            else
            {
                result.json_block["swap_mode"] = "none";
                result.notes.push_back(
                    "SwapMode=\"" + assignment->raw_value_text + "\" (" + assignment->location.fileName() +
                    ":" + std::to_string(assignment->location.line) +
                    ") — конфигуратор не смог определить режим перестановки байт, укажите "
                    "swap_mode явно.");
                FieldProvenance provenance = makeUnresolvedProvenance("swap_mode");
                provenance.location = assignment->location;
                provenance.original_raw_value_text = assignment->raw_value_text;
                provenance.original_full_line_text = assignment->full_line_text;
                result.provenance.push_back(provenance);
            }
        }
    }

    // ---- order_mode (опциональное, дефолт "lsw" из schemas/crypto.json) ----
    {
        const RawFieldAssignment* assignment = lastAssignment(block, "OrderMode");
        if (assignment == nullptr)
        {
            result.json_block["order_mode"] = "lsw";
        }
        else
        {
            const std::optional<std::string> value = parseOrderMode(assignment->raw_value_text);
            if (value.has_value())
            {
                result.json_block["order_mode"] = *value;
                result.provenance.push_back(makeProvenance("order_mode", *assignment, false));
            }
            else
            {
                result.json_block["order_mode"] = "lsw";
                result.notes.push_back(
                    "OrderMode=\"" + assignment->raw_value_text + "\" (" + assignment->location.fileName() +
                    ":" + std::to_string(assignment->location.line) +
                    ") — конфигуратор не смог определить порядок слов, укажите order_mode "
                    "явно.");
                FieldProvenance provenance = makeUnresolvedProvenance("order_mode");
                provenance.location = assignment->location;
                provenance.original_raw_value_text = assignment->raw_value_text;
                provenance.original_full_line_text = assignment->full_line_text;
                result.provenance.push_back(provenance);
            }
        }
    }

    return result;
}

std::optional<std::string> serializeCryptoFieldValue(
    const std::string& field_name,
    const nlohmann::json& new_value,
    const std::string& /*original_raw_value_text*/)
{
    if (!new_value.is_string()) return std::nullopt;
    const std::string v = new_value.get<std::string>();
    if (field_name == "algorithm")
    {
        if (v == "kuznechik") return "CRYPTO_ALG_KUZNECHIK";
        if (v == "magma") return "CRYPTO_ALG_MAGMA";
        if (v == "aes") return "CRYPTO_ALG_AES";
        return std::nullopt;
    }
    if (field_name == "cipher_mode")
    {
        if (v == "ecb") return "CRYPTO_CIPHER_MODE_ECB";
        if (v == "cbc") return "CRYPTO_CIPHER_MODE_CBC";
        if (v == "ctr") return "CRYPTO_CIPHER_MODE_CTR";
        return std::nullopt;
    }
    if (field_name == "swap_mode")
    {
        if (v == "none") return "CRYPTO_SWAP_MODE_NONE";
        if (v == "halfword") return "CRYPTO_SWAP_MODE_HALFWORD";
        if (v == "byte") return "CRYPTO_SWAP_MODE_BYTE";
        if (v == "bit") return "CRYPTO_SWAP_MODE_BIT";
        return std::nullopt;
    }
    if (field_name == "order_mode")
    {
        if (v == "lsw") return "CRYPTO_ORDER_MODE_LSW";
        if (v == "msw") return "CRYPTO_ORDER_MODE_MSW";
        return std::nullopt;
    }
    return std::nullopt;
}

} // namespace reverse_import
