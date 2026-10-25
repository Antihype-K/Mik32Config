//
// Тесты tsens_reverse_mapper.{h,cpp} (parser/reverse_import/). Покрывает:
// распознанный блок без порогов/прерываний (HAL_TSENS/src/main.c) и блок с
// порогами + прерыванием (HAL_TSENS_IRQ/src/main.c), плюс нераспознанный
// Clock -> unresolved=true вместо угаданного дефолта.
//
#include <gtest/gtest.h>

#include "../parser/reverse_import/tsens_reverse_mapper.h"

using reverse_import::FieldProvenance;
using reverse_import::MappedBlock;
using reverse_import::RawFieldAssignment;
using reverse_import::RawInitBlock;
using reverse_import::RawTrailingCall;
using reverse_import::SourceLocation;
using reverse_import::mapTsensBlock;

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

// docs/examples/mik32-examples/HAL_TSENS/src/main.c:75-83.
RawInitBlock buildRealTsensBlock()
{
    const std::string file = "HAL_TSENS/src/main.c";
    RawInitBlock block;
    block.handle_name = "htsens";
    block.handle_type = "TSENS_HandleTypeDef";
    block.assignments = {
        makeAssignment("htsens", "Instance", "ANALOG_REG", file, 76),
        makeAssignment("htsens", "Clock", "HAL_TSENS_OSC32K", file, 78),
        makeAssignment("htsens", "Frequency", "3000", file, 79),
    };
    block.init_function_name = "HAL_TSENS_Init";
    block.init_call_args = {"htsens"};
    block.init_call_location = SourceLocation{file, 81};
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

TEST(TsensReverseMapper, ResolvesClockAndFrequencyWithoutOptionalFields)
{
    const std::optional<MappedBlock> mapped = mapTsensBlock(buildRealTsensBlock());
    ASSERT_TRUE(mapped.has_value());

    EXPECT_EQ(mapped->json_block.at("clock_source").get<std::string>(), "osc32k");
    EXPECT_EQ(mapped->json_block.at("frequency_hz").get<std::uint32_t>(), 3000u);
    EXPECT_FALSE(mapped->json_block.contains("low_threshold_celsius"));
    EXPECT_FALSE(mapped->json_block.contains("high_threshold_celsius"));
    EXPECT_FALSE(mapped->json_block.contains("interrupts"));

    for (const char* field : {"clock_source", "frequency_hz"})
    {
        const FieldProvenance* provenance = findProvenance(*mapped, field);
        ASSERT_NE(provenance, nullptr) << field;
        EXPECT_FALSE(provenance->unresolved) << field;
    }
}

// docs/examples/mik32-examples/HAL_TSENS_IRQ/src/main.c:47,69.
TEST(TsensReverseMapper, ResolvesThresholdsAndInterruptsFromTrailingCalls)
{
    RawInitBlock block = buildRealTsensBlock();
    block.trailing_calls = {
        RawTrailingCall{"HAL_TSENS_SetHiThreshold", {"htsens", "35"}, SourceLocation{"f", 47}, ""},
        RawTrailingCall{"__HAL_TSENS_IRQ_ENABLE_HIIRQ", {"htsens"}, SourceLocation{"f", 69}, ""},
    };

    const std::optional<MappedBlock> mapped = mapTsensBlock(block);
    ASSERT_TRUE(mapped.has_value());

    EXPECT_EQ(mapped->json_block.at("high_threshold_celsius").get<int>(), 35);
    EXPECT_FALSE(mapped->json_block.contains("low_threshold_celsius"));
    ASSERT_TRUE(mapped->json_block.contains("interrupts"));
    EXPECT_EQ(mapped->json_block.at("interrupts").get<std::vector<std::string>>(),
              std::vector<std::string>{"high"});
}

TEST(TsensReverseMapper, UnrecognizedClockMacroMarksUnresolved)
{
    RawInitBlock block = buildRealTsensBlock();
    block.assignments[1].raw_value_text = "some_custom_clock";

    const std::optional<MappedBlock> mapped = mapTsensBlock(block);
    ASSERT_TRUE(mapped.has_value());

    const FieldProvenance* provenance = findProvenance(*mapped, "clock_source");
    ASSERT_NE(provenance, nullptr);
    EXPECT_TRUE(provenance->unresolved);
}

TEST(TsensReverseMapper, RejectsNonTsensHandleType)
{
    RawInitBlock block;
    block.handle_type = "I2C_HandleTypeDef";
    EXPECT_FALSE(mapTsensBlock(block).has_value());
}

TEST(TsensReverseMapper, SerializeFieldRoundTrip)
{
    using reverse_import::serializeTsensFieldValue;
    EXPECT_EQ(serializeTsensFieldValue("clock_source", "hsi32m", ""), "HAL_TSENS_HSI32M");
    EXPECT_EQ(serializeTsensFieldValue("frequency_hz", 40000, ""), "40000");
}

TEST(TsensReverseMapper, SerializeFieldRejectsOutOfRangeOrWrongType)
{
    using reverse_import::serializeTsensFieldValue;
    EXPECT_FALSE(serializeTsensFieldValue("clock_source", "pll", "").has_value());
    EXPECT_FALSE(serializeTsensFieldValue("frequency_hz", 100001, "").has_value());
    EXPECT_FALSE(serializeTsensFieldValue("frequency_hz", 0, "").has_value());
}
