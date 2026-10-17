//
// Тесты eeprom_reverse_mapper.{h,cpp} (parser/reverse_import/) — по стилю
// tests/test_reverse_import_crypto.cpp. Покрывает: распознанный блок
// (docs/examples/mik32-examples/HAL_EEPROM/src/main.c), отсутствующее
// обязательное поле и нераспознанный литерал — оба должны дать
// unresolved=true, а не угаданный дефолт
// (feedback_reverse_import_unresolved_fields_policy); отдельно —
// отсутствующие опциональные поля применяют документированный дефолт схемы
// без unresolved/без provenance вовсе.
//
#include <gtest/gtest.h>

#include "../parser/reverse_import/eeprom_reverse_mapper.h"

using reverse_import::FieldProvenance;
using reverse_import::MappedBlock;
using reverse_import::RawFieldAssignment;
using reverse_import::RawInitBlock;
using reverse_import::SourceLocation;
using reverse_import::mapEepromBlock;

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

// docs/examples/mik32-examples/HAL_EEPROM/src/main.c:237-246 (EEPROM_Init).
RawInitBlock buildRealEepromBlock()
{
    const std::string file = "HAL_EEPROM/src/main.c";
    RawInitBlock block;
    block.handle_name = "heeprom";
    block.handle_type = "HAL_EEPROM_HandleTypeDef";
    block.assignments = {
        makeAssignment("heeprom", "Instance", "EEPROM_REGS", file, 239),
        makeAssignment("heeprom", "Mode", "HAL_EEPROM_MODE_TWO_STAGE", file, 240),
        makeAssignment("heeprom", "ErrorCorrection", "HAL_EEPROM_ECC_ENABLE", file, 241),
        makeAssignment("heeprom", "EnableInterrupt", "HAL_EEPROM_SERR_DISABLE", file, 242),
    };
    block.init_function_name = "HAL_EEPROM_Init";
    block.init_call_args = {"heeprom"};
    block.init_call_location = SourceLocation{file, 244};
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

TEST(EepromReverseMapper, WrongHandleTypeReturnsNullopt)
{
    RawInitBlock block;
    block.handle_type = "ADC_HandleTypeDef";
    EXPECT_FALSE(mapEepromBlock(block).has_value());
}

TEST(EepromReverseMapper, RealExampleParsesAllFields)
{
    const std::optional<MappedBlock> mapped = mapEepromBlock(buildRealEepromBlock());
    ASSERT_TRUE(mapped.has_value());

    EXPECT_EQ(mapped->json_block["type"], "eeprom");
    EXPECT_EQ(mapped->json_block["instance"], 0);
    EXPECT_EQ(mapped->json_block["mode"], "two_stage");
    EXPECT_EQ(mapped->json_block["ecc_enabled"], true);
    EXPECT_EQ(mapped->json_block["single_error_interrupt_enabled"], false);

    for (const char* field : {"mode", "ecc_enabled", "single_error_interrupt_enabled"})
    {
        const FieldProvenance* provenance = findProvenance(*mapped, field);
        ASSERT_NE(provenance, nullptr) << field;
        EXPECT_FALSE(provenance->unresolved) << field;
    }
}

TEST(EepromReverseMapper, MissingRequiredFieldIsUnresolvedNotDefaulted)
{
    const std::string file = "no_mode/src/main.c";
    RawInitBlock block;
    block.handle_name = "heeprom";
    block.handle_type = "HAL_EEPROM_HandleTypeDef";
    block.assignments = {
        makeAssignment("heeprom", "ErrorCorrection", "HAL_EEPROM_ECC_ENABLE", file, 10),
    };
    block.init_function_name = "HAL_EEPROM_Init";

    const std::optional<MappedBlock> mapped = mapEepromBlock(block);
    ASSERT_TRUE(mapped.has_value());

    const FieldProvenance* provenance = findProvenance(*mapped, "mode");
    ASSERT_NE(provenance, nullptr);
    EXPECT_TRUE(provenance->unresolved);
    EXPECT_FALSE(mapped->notes.empty());
}

TEST(EepromReverseMapper, MissingOptionalFieldsApplySchemaDefaultNotUnresolved)
{
    const std::string file = "no_optional_fields/src/main.c";
    RawInitBlock block;
    block.handle_name = "heeprom";
    block.handle_type = "HAL_EEPROM_HandleTypeDef";
    block.assignments = {
        makeAssignment("heeprom", "Mode", "HAL_EEPROM_MODE_THREE_STAGE", file, 10),
    };
    block.init_function_name = "HAL_EEPROM_Init";

    const std::optional<MappedBlock> mapped = mapEepromBlock(block);
    ASSERT_TRUE(mapped.has_value());

    EXPECT_EQ(mapped->json_block["ecc_enabled"], true);
    EXPECT_EQ(mapped->json_block["single_error_interrupt_enabled"], false);
    EXPECT_EQ(findProvenance(*mapped, "ecc_enabled"), nullptr);
    EXPECT_EQ(findProvenance(*mapped, "single_error_interrupt_enabled"), nullptr);
}

TEST(EepromReverseMapper, UnparseableLiteralIsUnresolvedNotDefaulted)
{
    const std::string file = "weird_macro/src/main.c";
    RawInitBlock block;
    block.handle_name = "heeprom";
    block.handle_type = "HAL_EEPROM_HandleTypeDef";
    block.assignments = {
        makeAssignment("heeprom", "Mode", "HAL_EEPROM_MODE_TWO_STAGE", file, 10),
        makeAssignment("heeprom", "ErrorCorrection", "config.ecc_mode", file, 11),
    };
    block.init_function_name = "HAL_EEPROM_Init";

    const std::optional<MappedBlock> mapped = mapEepromBlock(block);
    ASSERT_TRUE(mapped.has_value());

    const FieldProvenance* ecc_provenance = findProvenance(*mapped, "ecc_enabled");
    ASSERT_NE(ecc_provenance, nullptr);
    EXPECT_TRUE(ecc_provenance->unresolved);
    EXPECT_EQ(ecc_provenance->original_raw_value_text, "config.ecc_mode");

    const FieldProvenance* mode_provenance = findProvenance(*mapped, "mode");
    ASSERT_NE(mode_provenance, nullptr);
    EXPECT_FALSE(mode_provenance->unresolved);
}
