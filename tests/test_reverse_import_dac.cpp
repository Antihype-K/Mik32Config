//
// Тесты dac_reverse_mapper.{h,cpp} (parser/reverse_import/) — по стилю
// tests/test_reverse_import_adc.cpp. Покрывает: распознанный блок
// (docs/examples/mik32-examples/HAL_DAC/src/main.c), отсутствующее поле и
// нераспознанный литерал — оба последних должны дать unresolved=true, а не
// угаданный дефолт (feedback_reverse_import_unresolved_fields_policy).
//
#include <gtest/gtest.h>

#include "../parser/reverse_import/dac_reverse_mapper.h"

using reverse_import::FieldProvenance;
using reverse_import::MappedBlock;
using reverse_import::RawFieldAssignment;
using reverse_import::RawInitBlock;
using reverse_import::SourceLocation;
using reverse_import::mapDacBlock;

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

// docs/examples/mik32-examples/HAL_DAC/src/main.c:54-67 (DAC_Init).
RawInitBlock buildRealDacBlock()
{
    const std::string file = "HAL_DAC/src/main.c";
    RawInitBlock block;
    block.handle_name = "hdac1";
    block.handle_type = "DAC_HandleTypeDef";
    block.assignments = {
        makeAssignment("hdac1", "Instance", "ANALOG_REG", file, 56),
        makeAssignment("hdac1", "Instance_dac", "HAL_DAC0", file, 58),
        makeAssignment("hdac1", "Init.DIV", "31", file, 60),
        makeAssignment("hdac1", "Init.EXTRef", "DAC_EXTREF_OFF", file, 62),
        makeAssignment("hdac1", "Init.EXTClb", "DAC_EXTCLB_DACREF", file, 64),
    };
    block.init_function_name = "HAL_DAC_Init";
    block.init_call_args = {"hdac1"};
    block.init_call_location = SourceLocation{file, 66};
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

TEST(DacReverseMapper, WrongHandleTypeReturnsNullopt)
{
    RawInitBlock block;
    block.handle_type = "ADC_HandleTypeDef";
    EXPECT_FALSE(mapDacBlock(block).has_value());
}

TEST(DacReverseMapper, RealExampleParsesAllFourFields)
{
    const std::optional<MappedBlock> mapped = mapDacBlock(buildRealDacBlock());
    ASSERT_TRUE(mapped.has_value());

    EXPECT_EQ(mapped->json_block["type"], "dac");
    EXPECT_EQ(mapped->json_block["instance"], 0);
    EXPECT_EQ(mapped->json_block["div"], 31);
    EXPECT_EQ(mapped->json_block["ext_ref"], false);
    EXPECT_EQ(mapped->json_block["ext_clb"], true);

    for (const char* field : {"instance", "div", "ext_ref", "ext_clb"})
    {
        const FieldProvenance* provenance = findProvenance(*mapped, field);
        ASSERT_NE(provenance, nullptr) << field;
        EXPECT_FALSE(provenance->unresolved) << field;
    }
}

TEST(DacReverseMapper, MissingFieldIsUnresolvedNotDefaulted)
{
    const std::string file = "no_instance/src/main.c";
    RawInitBlock block;
    block.handle_name = "hdac1";
    block.handle_type = "DAC_HandleTypeDef";
    block.assignments = {
        makeAssignment("hdac1", "Init.DIV", "31", file, 10),
        makeAssignment("hdac1", "Init.EXTRef", "DAC_EXTREF_OFF", file, 11),
        makeAssignment("hdac1", "Init.EXTClb", "DAC_EXTCLB_CLBREF", file, 12),
    };
    block.init_function_name = "HAL_DAC_Init";

    const std::optional<MappedBlock> mapped = mapDacBlock(block);
    ASSERT_TRUE(mapped.has_value());

    ASSERT_TRUE(mapped->json_block.contains("instance"));

    const FieldProvenance* provenance = findProvenance(*mapped, "instance");
    ASSERT_NE(provenance, nullptr);
    EXPECT_TRUE(provenance->unresolved);

    EXPECT_FALSE(mapped->notes.empty());
}

TEST(DacReverseMapper, UnparseableLiteralIsUnresolvedNotDefaulted)
{
    const std::string file = "weird_macro/src/main.c";
    RawInitBlock block;
    block.handle_name = "hdac1";
    block.handle_type = "DAC_HandleTypeDef";
    block.assignments = {
        makeAssignment("hdac1", "Instance_dac", "HAL_DAC0", file, 10),
        // Выражение, не числовой литерал — не должно дать молчаливый 0.
        makeAssignment("hdac1", "Init.DIV", "config.dac_div", file, 11),
        makeAssignment("hdac1", "Init.EXTRef", "DAC_EXTREF_OFF", file, 12),
        makeAssignment("hdac1", "Init.EXTClb", "DAC_EXTCLB_CLBREF", file, 13),
    };
    block.init_function_name = "HAL_DAC_Init";

    const std::optional<MappedBlock> mapped = mapDacBlock(block);
    ASSERT_TRUE(mapped.has_value());

    const FieldProvenance* div_provenance = findProvenance(*mapped, "div");
    ASSERT_NE(div_provenance, nullptr);
    EXPECT_TRUE(div_provenance->unresolved);
    EXPECT_EQ(div_provenance->original_raw_value_text, "config.dac_div");

    const FieldProvenance* instance_provenance = findProvenance(*mapped, "instance");
    ASSERT_NE(instance_provenance, nullptr);
    EXPECT_FALSE(instance_provenance->unresolved);
}
