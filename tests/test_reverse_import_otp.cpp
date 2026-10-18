//
// Тесты otp_reverse_mapper.{h,cpp} (parser/reverse_import/). Покрывает:
// распознанный ReadMode (docs/examples/mik32-examples/HAL_OTP/src/main.c) и
// нераспознанный ReadMode -> unresolved=true вместо угаданного дефолта.
//
#include <gtest/gtest.h>

#include "../parser/reverse_import/otp_reverse_mapper.h"

using reverse_import::FieldProvenance;
using reverse_import::MappedBlock;
using reverse_import::RawFieldAssignment;
using reverse_import::RawInitBlock;
using reverse_import::SourceLocation;
using reverse_import::mapOtpBlock;

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

// docs/examples/mik32-examples/HAL_OTP/src/main.c.
RawInitBlock buildRealOtpBlock()
{
    const std::string file = "HAL_OTP/src/main.c";
    RawInitBlock block;
    block.handle_name = "hotp";
    block.handle_type = "OTP_HandleTypeDef";
    block.assignments = {
        makeAssignment("hotp", "Instance", "OTP", file, 20),
        makeAssignment("hotp", "ReadMode", "OPT_READ_3STAGES", file, 21),
    };
    block.init_function_name = "HAL_OTP_Init";
    block.init_call_args = {"hotp"};
    block.init_call_location = SourceLocation{file, 22};
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

TEST(OtpReverseMapper, ResolvesReadModeFromRealExample)
{
    const std::optional<MappedBlock> mapped = mapOtpBlock(buildRealOtpBlock());
    ASSERT_TRUE(mapped.has_value());

    EXPECT_EQ(mapped->json_block.at("read_mode").get<std::string>(), "three_stages");

    const FieldProvenance* provenance = findProvenance(*mapped, "read_mode");
    ASSERT_NE(provenance, nullptr);
    EXPECT_FALSE(provenance->unresolved);
}

TEST(OtpReverseMapper, UnrecognizedReadModeMarksUnresolved)
{
    RawInitBlock block = buildRealOtpBlock();
    block.assignments[1].raw_value_text = "some_custom_mode";

    const std::optional<MappedBlock> mapped = mapOtpBlock(block);
    ASSERT_TRUE(mapped.has_value());

    const FieldProvenance* provenance = findProvenance(*mapped, "read_mode");
    ASSERT_NE(provenance, nullptr);
    EXPECT_TRUE(provenance->unresolved);
}

TEST(OtpReverseMapper, MissingReadModeMarksUnresolved)
{
    RawInitBlock block = buildRealOtpBlock();
    block.assignments.resize(1);

    const std::optional<MappedBlock> mapped = mapOtpBlock(block);
    ASSERT_TRUE(mapped.has_value());

    const FieldProvenance* provenance = findProvenance(*mapped, "read_mode");
    ASSERT_NE(provenance, nullptr);
    EXPECT_TRUE(provenance->unresolved);
}

TEST(OtpReverseMapper, RejectsNonOtpHandleType)
{
    RawInitBlock block;
    block.handle_type = "I2C_HandleTypeDef";
    EXPECT_FALSE(mapOtpBlock(block).has_value());
}
