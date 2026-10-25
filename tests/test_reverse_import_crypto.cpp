//
// Тесты crypto_reverse_mapper.{h,cpp} (parser/reverse_import/) — по стилю
// tests/test_reverse_import_adc.cpp. Покрывает: распознанный блок
// (docs/examples/mik32-examples/HAL_DMA_Crypto/src/main.c), отсутствующее
// обязательное поле и нераспознанный литерал — оба последних должны дать
// unresolved=true, а не угаданный дефолт
// (feedback_reverse_import_unresolved_fields_policy). swap_mode/order_mode
// опциональны (schemas/crypto.json default) — отсутствие НЕ unresolved.
//
#include <gtest/gtest.h>

#include "../parser/reverse_import/crypto_reverse_mapper.h"

using reverse_import::FieldProvenance;
using reverse_import::MappedBlock;
using reverse_import::RawFieldAssignment;
using reverse_import::RawInitBlock;
using reverse_import::SourceLocation;
using reverse_import::mapCryptoBlock;

namespace
{

RawFieldAssignment makeAssignment(const std::string& handle, const std::string& field,
                                   const std::string& value, const std::string& file, int line)
{
    RawFieldAssignment assignment;
    assignment.field_path = field;
    assignment.raw_value_text = value;
    assignment.location = SourceLocation{file, line};
    assignment.full_line_text = "    " + handle + "." + field + " = " + value + ";";
    return assignment;
}

// docs/examples/mik32-examples/HAL_DMA_Crypto/src/main.c:166-176 (Crypto_Init).
RawInitBlock buildRealCryptoBlock()
{
    const std::string file = "HAL_DMA_Crypto/src/main.c";
    RawInitBlock block;
    block.handle_name = "hcrypto";
    block.handle_type = "Crypto_HandleTypeDef";
    block.assignments = {
        makeAssignment("hcrypto", "Instance", "CRYPTO", file, 168),
        makeAssignment("hcrypto", "Algorithm", "CRYPTO_ALG_KUZNECHIK", file, 170),
        makeAssignment("hcrypto", "CipherMode", "CRYPTO_CIPHER_MODE_ECB", file, 171),
        makeAssignment("hcrypto", "SwapMode", "CRYPTO_SWAP_MODE_NONE", file, 172),
        makeAssignment("hcrypto", "OrderMode", "CRYPTO_ORDER_MODE_MSW", file, 173),
    };
    block.init_function_name = "HAL_Crypto_Init";
    block.init_call_args = {"hcrypto"};
    block.init_call_location = SourceLocation{file, 175};
    return block;
}

const FieldProvenance* findProvenance(const MappedBlock& mapped, const std::string& field)
{
    for (const auto& provenance : mapped.provenance)
    {
        if (provenance.json_field == field)
        {
            return &provenance;
        }
    }
    return nullptr;
}

} // namespace

TEST(CryptoReverseMapper, WrongHandleTypeReturnsNullopt)
{
    RawInitBlock block;
    block.handle_type = "ADC_HandleTypeDef";
    EXPECT_FALSE(mapCryptoBlock(block).has_value());
}

TEST(CryptoReverseMapper, RealExampleParsesAllFourFields)
{
    const std::optional<MappedBlock> mapped = mapCryptoBlock(buildRealCryptoBlock());
    ASSERT_TRUE(mapped.has_value());

    EXPECT_EQ(mapped->json_block["type"], "crypto");
    EXPECT_EQ(mapped->json_block["instance"], 0);
    EXPECT_EQ(mapped->json_block["algorithm"], "kuznechik");
    EXPECT_EQ(mapped->json_block["cipher_mode"], "ecb");
    EXPECT_EQ(mapped->json_block["swap_mode"], "none");
    EXPECT_EQ(mapped->json_block["order_mode"], "msw");

    for (const char* field : {"algorithm", "cipher_mode", "swap_mode", "order_mode"})
    {
        const FieldProvenance* provenance = findProvenance(*mapped, field);
        ASSERT_NE(provenance, nullptr) << field;
        EXPECT_FALSE(provenance->unresolved) << field;
    }
}

TEST(CryptoReverseMapper, MissingRequiredFieldIsUnresolvedNotDefaulted)
{
    const std::string file = "no_algorithm/src/main.c";
    RawInitBlock block;
    block.handle_name = "hcrypto";
    block.handle_type = "Crypto_HandleTypeDef";
    block.assignments = {
        makeAssignment("hcrypto", "CipherMode", "CRYPTO_CIPHER_MODE_ECB", file, 10),
    };
    block.init_function_name = "HAL_Crypto_Init";

    const std::optional<MappedBlock> mapped = mapCryptoBlock(block);
    ASSERT_TRUE(mapped.has_value());

    ASSERT_TRUE(mapped->json_block.contains("algorithm"));

    const FieldProvenance* provenance = findProvenance(*mapped, "algorithm");
    ASSERT_NE(provenance, nullptr);
    EXPECT_TRUE(provenance->unresolved);

    EXPECT_FALSE(mapped->notes.empty());
}

TEST(CryptoReverseMapper, MissingOptionalFieldAppliesSchemaDefaultNotUnresolved)
{
    const std::string file = "no_swap_order/src/main.c";
    RawInitBlock block;
    block.handle_name = "hcrypto";
    block.handle_type = "Crypto_HandleTypeDef";
    block.assignments = {
        makeAssignment("hcrypto", "Algorithm", "CRYPTO_ALG_MAGMA", file, 10),
        makeAssignment("hcrypto", "CipherMode", "CRYPTO_CIPHER_MODE_CBC", file, 11),
    };
    block.init_function_name = "HAL_Crypto_Init";

    const std::optional<MappedBlock> mapped = mapCryptoBlock(block);
    ASSERT_TRUE(mapped.has_value());

    EXPECT_EQ(mapped->json_block["swap_mode"], "none");
    EXPECT_EQ(mapped->json_block["order_mode"], "lsw");
    EXPECT_EQ(findProvenance(*mapped, "swap_mode"), nullptr);
    EXPECT_EQ(findProvenance(*mapped, "order_mode"), nullptr);
}

TEST(CryptoReverseMapper, UnparseableLiteralIsUnresolvedNotDefaulted)
{
    const std::string file = "weird_macro/src/main.c";
    RawInitBlock block;
    block.handle_name = "hcrypto";
    block.handle_type = "Crypto_HandleTypeDef";
    block.assignments = {
        makeAssignment("hcrypto", "Algorithm", "CRYPTO_ALG_AES", file, 10),
        // Выражение, не макрос — не должно дать молчаливый "ecb".
        makeAssignment("hcrypto", "CipherMode", "config.cipher_mode", file, 11),
    };
    block.init_function_name = "HAL_Crypto_Init";

    const std::optional<MappedBlock> mapped = mapCryptoBlock(block);
    ASSERT_TRUE(mapped.has_value());

    const FieldProvenance* cipher_mode_provenance = findProvenance(*mapped, "cipher_mode");
    ASSERT_NE(cipher_mode_provenance, nullptr);
    EXPECT_TRUE(cipher_mode_provenance->unresolved);
    EXPECT_EQ(cipher_mode_provenance->original_raw_value_text, "config.cipher_mode");

    const FieldProvenance* algorithm_provenance = findProvenance(*mapped, "algorithm");
    ASSERT_NE(algorithm_provenance, nullptr);
    EXPECT_FALSE(algorithm_provenance->unresolved);
}

TEST(CryptoReverseMapper, SerializeFieldValueRoundTrip)
{
    using reverse_import::serializeCryptoFieldValue;
    EXPECT_EQ(serializeCryptoFieldValue("algorithm", "magma", ""), "CRYPTO_ALG_MAGMA");
    EXPECT_EQ(serializeCryptoFieldValue("cipher_mode", "ctr", ""), "CRYPTO_CIPHER_MODE_CTR");
    EXPECT_EQ(serializeCryptoFieldValue("swap_mode", "bit", ""), "CRYPTO_SWAP_MODE_BIT");
    EXPECT_EQ(serializeCryptoFieldValue("order_mode", "msw", ""), "CRYPTO_ORDER_MODE_MSW");
    EXPECT_FALSE(serializeCryptoFieldValue("algorithm", "des", "").has_value());
}
