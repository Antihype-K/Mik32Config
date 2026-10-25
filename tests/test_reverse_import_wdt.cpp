//
// Тесты wdt_reverse_mapper.{h,cpp} (parser/reverse_import/). Покрывает:
// распознанный блок (docs/examples/mik32-examples/HAL_WDT/src/main.c) и
// нераспознанный Init.Clock -> unresolved=true вместо угаданного дефолта.
//
#include <gtest/gtest.h>

#include "../parser/reverse_import/wdt_reverse_mapper.h"

using reverse_import::FieldProvenance;
using reverse_import::MappedBlock;
using reverse_import::RawFieldAssignment;
using reverse_import::RawInitBlock;
using reverse_import::SourceLocation;
using reverse_import::mapWdtBlock;

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

// docs/examples/mik32-examples/HAL_WDT/src/main.c.
RawInitBlock buildRealWdtBlock()
{
    const std::string file = "HAL_WDT/src/main.c";
    RawInitBlock block;
    block.handle_name = "hwdt";
    block.handle_type = "WDT_HandleTypeDef";
    block.assignments = {
        makeAssignment("hwdt", "Instance", "WDT", file, 18),
        makeAssignment("hwdt", "Init.Clock", "HAL_WDT_OSC32K", file, 19),
        makeAssignment("hwdt", "Init.ReloadMs", "1000", file, 20),
    };
    block.init_function_name = "HAL_WDT_Init";
    block.init_call_args = {"hwdt", "WDT_TIMEOUT_DEFAULT"};
    block.init_call_location = SourceLocation{file, 21};
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

TEST(WdtReverseMapper, ResolvesClockAndReloadFromRealExample)
{
    const std::optional<MappedBlock> mapped = mapWdtBlock(buildRealWdtBlock());
    ASSERT_TRUE(mapped.has_value());

    EXPECT_EQ(mapped->json_block.at("clock_source").get<std::string>(), "osc32k");
    EXPECT_EQ(mapped->json_block.at("reload_ms").get<std::uint32_t>(), 1000u);

    for (const char* field : {"clock_source", "reload_ms"})
    {
        const FieldProvenance* provenance = findProvenance(*mapped, field);
        ASSERT_NE(provenance, nullptr) << field;
        EXPECT_FALSE(provenance->unresolved) << field;
    }
}

TEST(WdtReverseMapper, UnrecognizedClockMacroMarksUnresolved)
{
    RawInitBlock block = buildRealWdtBlock();
    block.assignments[1].raw_value_text = "some_custom_clock";

    const std::optional<MappedBlock> mapped = mapWdtBlock(block);
    ASSERT_TRUE(mapped.has_value());

    const FieldProvenance* provenance = findProvenance(*mapped, "clock_source");
    ASSERT_NE(provenance, nullptr);
    EXPECT_TRUE(provenance->unresolved);
}

TEST(WdtReverseMapper, MissingReloadMsMarksUnresolved)
{
    RawInitBlock block = buildRealWdtBlock();
    block.assignments.resize(2);

    const std::optional<MappedBlock> mapped = mapWdtBlock(block);
    ASSERT_TRUE(mapped.has_value());

    const FieldProvenance* provenance = findProvenance(*mapped, "reload_ms");
    ASSERT_NE(provenance, nullptr);
    EXPECT_TRUE(provenance->unresolved);
}

TEST(WdtReverseMapper, RejectsNonWdtHandleType)
{
    RawInitBlock block;
    block.handle_type = "I2C_HandleTypeDef";
    EXPECT_FALSE(mapWdtBlock(block).has_value());
}

TEST(WdtReverseMapper, SerializesClockSourceAndReloadMs)
{
    using reverse_import::serializeWdtFieldValue;

    EXPECT_EQ(serializeWdtFieldValue("clock_source", "osc32m", "HAL_WDT_OSC32K"), "HAL_WDT_OSC32M");
    EXPECT_EQ(serializeWdtFieldValue("reload_ms", 2500, "1000"), "2500");
}

TEST(WdtReverseMapper, SerializeRejectsUnrecognizedFieldOrType)
{
    using reverse_import::serializeWdtFieldValue;

    // Нет такого enum-значения clock_source — угадывать не из чего.
    EXPECT_FALSE(serializeWdtFieldValue("clock_source", "warp_drive", "HAL_WDT_OSC32K").has_value());
    // reload_ms должен быть целым числом.
    EXPECT_FALSE(serializeWdtFieldValue("reload_ms", "1000", "1000").has_value());
    // Неизвестное поле.
    EXPECT_FALSE(serializeWdtFieldValue("instance", 0, "0").has_value());
}
