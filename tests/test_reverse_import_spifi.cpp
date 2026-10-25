//
// Тесты spifi_reverse_mapper.{h,cpp} (parser/reverse_import/) — по стилю
// tests/test_reverse_import_crc.cpp. Покрывает: распознанный структурный
// блок (та же форма, что генерирует codegen/project/spifi_contribution.cpp)
// и нераспознанный литерал -> unresolved=true, а не угаданный дефолт
// (feedback_reverse_import_unresolved_fields_policy).
//
#include <gtest/gtest.h>

#include "../parser/reverse_import/spifi_reverse_mapper.h"

using reverse_import::FieldProvenance;
using reverse_import::MappedBlock;
using reverse_import::RawFieldAssignment;
using reverse_import::RawInitBlock;
using reverse_import::SourceLocation;
using reverse_import::mapSpifiBlock;

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

// Форма, которую генерирует codegen/project/spifi_contribution.cpp (не
// вендорный пример HAL_Blink_SPIFI — см. комментарий в
// spifi_reverse_mapper.h о том, почему LL-паттерн не покрывается).
RawInitBlock buildStructuralSpifiBlock()
{
    const std::string file = "generated/main.c";
    RawInitBlock block;
    block.handle_name = "hspifi";
    block.handle_type = "SPIFI_MemoryModeConfig_HandleTypeDef";
    block.assignments = {
        makeAssignment("hspifi", "Instance", "SPIFI_CONFIG", file, 10),
        makeAssignment("hspifi", "CacheEnable", "SPIFI_CACHE_ENABLE", file, 12),
        makeAssignment("hspifi", "CacheLimit", "0x90000000", file, 13),
        makeAssignment("hspifi", "Command.InterimLength", "0", file, 15),
        makeAssignment("hspifi", "Command.FieldForm", "SPIFI_FIELDFORM_ALL_SERIAL", file, 16),
        makeAssignment("hspifi", "Command.FrameForm", "SPIFI_FRAMEFORM_OPCODE_3ADDR", file, 17),
        makeAssignment("hspifi", "Command.OpCode", "0x03", file, 18),
    };
    block.init_function_name = "HAL_SPIFI_MemoryMode_Init";
    block.init_call_args = {"hspifi"};
    block.init_call_location = SourceLocation{file, 20};
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

TEST(SpifiReverseMapper, ResolvesStructuralMemoryModeInit)
{
    const std::optional<MappedBlock> mapped = mapSpifiBlock(buildStructuralSpifiBlock());
    ASSERT_TRUE(mapped.has_value());

    EXPECT_EQ(mapped->json_block.at("cache_enabled").get<bool>(), true);
    EXPECT_EQ(mapped->json_block.at("cache_limit").get<std::uint32_t>(), 0x90000000u);
    EXPECT_EQ(mapped->json_block.at("cmd_interim_length").get<std::uint32_t>(), 0u);
    EXPECT_EQ(mapped->json_block.at("cmd_field_form").get<std::string>(), "all_serial");
    EXPECT_EQ(mapped->json_block.at("cmd_frame_form").get<std::string>(), "opcode_3addr");
    EXPECT_EQ(mapped->json_block.at("cmd_op_code").get<std::uint32_t>(), 3u);

    for (const char* field : {"cache_enabled", "cache_limit", "cmd_interim_length",
                               "cmd_field_form", "cmd_frame_form", "cmd_op_code"})
    {
        const FieldProvenance* provenance = findProvenance(*mapped, field);
        ASSERT_NE(provenance, nullptr) << field;
        EXPECT_FALSE(provenance->unresolved) << field;
    }
}

TEST(SpifiReverseMapper, UnrecognizedFieldFormMacroMarksUnresolved)
{
    RawInitBlock block = buildStructuralSpifiBlock();
    block.assignments[4].raw_value_text = "some_custom_variable";

    const std::optional<MappedBlock> mapped = mapSpifiBlock(block);
    ASSERT_TRUE(mapped.has_value());

    const FieldProvenance* provenance = findProvenance(*mapped, "cmd_field_form");
    ASSERT_NE(provenance, nullptr);
    EXPECT_TRUE(provenance->unresolved);
}

TEST(SpifiReverseMapper, MissingCacheLimitMarksUnresolved)
{
    RawInitBlock block = buildStructuralSpifiBlock();
    block.assignments.erase(block.assignments.begin() + 2);

    const std::optional<MappedBlock> mapped = mapSpifiBlock(block);
    ASSERT_TRUE(mapped.has_value());
    EXPECT_EQ(mapped->json_block.at("cache_limit").get<std::uint32_t>(), 0u);

    const FieldProvenance* provenance = findProvenance(*mapped, "cache_limit");
    ASSERT_NE(provenance, nullptr);
    EXPECT_TRUE(provenance->unresolved);
}

TEST(SpifiReverseMapper, RejectsNonSpifiHandleType)
{
    RawInitBlock block;
    block.handle_type = "CRC_HandleTypeDef";
    EXPECT_FALSE(mapSpifiBlock(block).has_value());
}

TEST(SpifiReverseMapper, SerializeFieldValueRoundTrip)
{
    using reverse_import::serializeSpifiFieldValue;
    EXPECT_EQ(serializeSpifiFieldValue("cache_enabled", true, ""), "SPIFI_CACHE_ENABLE");
    EXPECT_EQ(serializeSpifiFieldValue("cache_limit", 128, ""), "128");
    EXPECT_EQ(serializeSpifiFieldValue("cmd_field_form", "all_parallel", ""), "SPIFI_FIELDFORM_ALL_PARALLEL");
    EXPECT_EQ(serializeSpifiFieldValue("cmd_frame_form", "addr_3", ""), "SPIFI_FRAMEFORM_3ADDR");
    EXPECT_FALSE(serializeSpifiFieldValue("cmd_field_form", "unknown", "").has_value());
}
