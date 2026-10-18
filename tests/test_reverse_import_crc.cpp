//
// Тесты crc_reverse_mapper.{h,cpp} (parser/reverse_import/) — по стилю
// tests/test_reverse_import_adc.cpp. Покрывает: распознанный блок
// (docs/examples/mik32-examples/HAL_CRC32/src/main.c, алгоритм CRC-32Q) и
// нераспознанный литерал -> unresolved=true, а не угаданный дефолт
// (feedback_reverse_import_unresolved_fields_policy).
//
#include <gtest/gtest.h>

#include "../parser/reverse_import/crc_reverse_mapper.h"

using reverse_import::FieldProvenance;
using reverse_import::MappedBlock;
using reverse_import::RawFieldAssignment;
using reverse_import::RawInitBlock;
using reverse_import::SourceLocation;
using reverse_import::mapCrcBlock;

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

// docs/examples/mik32-examples/HAL_CRC32/src/main.c:75-83 (CRC-32Q).
RawInitBlock buildRealCrcBlock()
{
    const std::string file = "HAL_CRC32/src/main.c";
    RawInitBlock block;
    block.handle_name = "hcrc";
    block.handle_type = "CRC_HandleTypeDef";
    block.assignments = {
        makeAssignment("hcrc", "Instance", "CRC", file, 77),
        makeAssignment("hcrc", "Poly", "0x814141AB", file, 79),
        makeAssignment("hcrc", "Init", "0x00000000", file, 80),
        makeAssignment("hcrc", "InputReverse", "CRC_REFIN_FALSE", file, 81),
        makeAssignment("hcrc", "OutputReverse", "CRC_REFOUT_FALSE", file, 82),
        makeAssignment("hcrc", "OutputInversion", "CRC_OUTPUTINVERSION_OFF", file, 83),
    };
    block.init_function_name = "HAL_CRC_Init";
    block.init_call_args = {"hcrc"};
    block.init_call_location = SourceLocation{file, 85};
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

TEST(CrcReverseMapper, ResolvesCrc32QFromRealExample)
{
    const std::optional<MappedBlock> mapped = mapCrcBlock(buildRealCrcBlock());
    ASSERT_TRUE(mapped.has_value());

    EXPECT_EQ(mapped->json_block.at("poly").get<std::uint32_t>(), 0x814141ABu);
    EXPECT_EQ(mapped->json_block.at("init").get<std::uint32_t>(), 0u);
    EXPECT_EQ(mapped->json_block.at("input_reverse").get<bool>(), false);
    EXPECT_EQ(mapped->json_block.at("output_reverse").get<bool>(), false);
    EXPECT_EQ(mapped->json_block.at("output_inversion").get<bool>(), false);

    for (const char* field : {"poly", "init", "input_reverse", "output_reverse", "output_inversion"})
    {
        const FieldProvenance* provenance = findProvenance(*mapped, field);
        ASSERT_NE(provenance, nullptr) << field;
        EXPECT_FALSE(provenance->unresolved) << field;
    }
}

TEST(CrcReverseMapper, UnrecognizedInputReverseMacroMarksUnresolved)
{
    RawInitBlock block = buildRealCrcBlock();
    block.assignments[3].raw_value_text = "some_custom_variable";

    const std::optional<MappedBlock> mapped = mapCrcBlock(block);
    ASSERT_TRUE(mapped.has_value());

    const FieldProvenance* provenance = findProvenance(*mapped, "input_reverse");
    ASSERT_NE(provenance, nullptr);
    EXPECT_TRUE(provenance->unresolved);
}

TEST(CrcReverseMapper, MissingPolyMarksUnresolved)
{
    RawInitBlock block = buildRealCrcBlock();
    block.assignments.erase(block.assignments.begin() + 1);

    const std::optional<MappedBlock> mapped = mapCrcBlock(block);
    ASSERT_TRUE(mapped.has_value());
    EXPECT_EQ(mapped->json_block.at("poly").get<std::uint32_t>(), 0u);

    const FieldProvenance* provenance = findProvenance(*mapped, "poly");
    ASSERT_NE(provenance, nullptr);
    EXPECT_TRUE(provenance->unresolved);
}

TEST(CrcReverseMapper, RejectsNonCrcHandleType)
{
    RawInitBlock block;
    block.handle_type = "I2C_HandleTypeDef";
    EXPECT_FALSE(mapCrcBlock(block).has_value());
}
