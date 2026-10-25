//
// Тесты adc_reverse_mapper.{h,cpp} (parser/reverse_import/) — по стилю
// tests/test_reverse_import_uart_i2c.cpp. Покрывает: распознанный блок
// (docs/examples/mik32-examples/HAL_ADC/src/main.c), отсутствующее поле и
// нераспознанный литерал — оба последних должны дать unresolved=true, а не
// угаданный дефолт (feedback_reverse_import_unresolved_fields_policy).
//
#include <gtest/gtest.h>

#include "../parser/reverse_import/adc_reverse_mapper.h"

using reverse_import::FieldProvenance;
using reverse_import::MappedBlock;
using reverse_import::RawFieldAssignment;
using reverse_import::RawInitBlock;
using reverse_import::SourceLocation;
using reverse_import::mapAdcBlock;

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

// docs/examples/mik32-examples/HAL_ADC/src/main.c:79-86 (ADC_Init).
RawInitBlock buildRealAdcBlock()
{
    const std::string file = "HAL_ADC/src/main.c";
    RawInitBlock block;
    block.handle_name = "hadc";
    block.handle_type = "ADC_HandleTypeDef";
    block.assignments = {
        makeAssignment("hadc", "Instance", "ANALOG_REG", file, 80),
        makeAssignment("hadc", "Init.Sel", "ADC_CHANNEL0", file, 82),
        makeAssignment("hadc", "Init.EXTRef", "ADC_EXTREF_OFF", file, 83),
        makeAssignment("hadc", "Init.EXTClb", "ADC_EXTCLB_ADCREF", file, 84),
    };
    block.init_function_name = "HAL_ADC_Init";
    block.init_call_args = {"hadc"};
    block.init_call_location = SourceLocation{file, 86};
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

TEST(AdcReverseMapper, WrongHandleTypeReturnsNullopt)
{
    RawInitBlock block;
    block.handle_type = "USART_HandleTypeDef";
    EXPECT_FALSE(mapAdcBlock(block).has_value());
}

TEST(AdcReverseMapper, RealExampleParsesAllThreeFields)
{
    const std::optional<MappedBlock> mapped = mapAdcBlock(buildRealAdcBlock());
    ASSERT_TRUE(mapped.has_value());

    EXPECT_EQ(mapped->json_block["type"], "adc");
    EXPECT_EQ(mapped->json_block["channel"], 0);
    EXPECT_EQ(mapped->json_block["ext_ref"], false);
    EXPECT_EQ(mapped->json_block["ext_clb"], true);

    for (const char* field : {"channel", "ext_ref", "ext_clb"})
    {
        const FieldProvenance* provenance = findProvenance(*mapped, field);
        ASSERT_NE(provenance, nullptr) << field;
        EXPECT_FALSE(provenance->unresolved) << field;
    }
}

TEST(AdcReverseMapper, MissingFieldIsUnresolvedNotDefaulted)
{
    const std::string file = "no_channel/src/main.c";
    RawInitBlock block;
    block.handle_name = "hadc";
    block.handle_type = "ADC_HandleTypeDef";
    block.assignments = {
        makeAssignment("hadc", "Init.EXTRef", "ADC_EXTREF_OFF", file, 10),
        makeAssignment("hadc", "Init.EXTClb", "ADC_EXTCLB_CLBREF", file, 11),
    };
    block.init_function_name = "HAL_ADC_Init";

    const std::optional<MappedBlock> mapped = mapAdcBlock(block);
    ASSERT_TRUE(mapped.has_value());

    // Поле всё равно присутствует в JSON (placeholder), иначе schema
    // validate отбракует блок (required_fields в schemas/adc.json).
    ASSERT_TRUE(mapped->json_block.contains("channel"));

    const FieldProvenance* provenance = findProvenance(*mapped, "channel");
    ASSERT_NE(provenance, nullptr);
    EXPECT_TRUE(provenance->unresolved);

    EXPECT_FALSE(mapped->notes.empty());
}

TEST(AdcReverseMapper, UnparseableLiteralIsUnresolvedNotDefaulted)
{
    const std::string file = "weird_macro/src/main.c";
    RawInitBlock block;
    block.handle_name = "hadc";
    block.handle_type = "ADC_HandleTypeDef";
    block.assignments = {
        makeAssignment("hadc", "Init.Sel", "ADC_CHANNEL0", file, 10),
        // Выражение, не литерал макроса — не должно дать молчаливый false.
        makeAssignment("hadc", "Init.EXTRef", "config.use_external_ref", file, 11),
        makeAssignment("hadc", "Init.EXTClb", "ADC_EXTCLB_CLBREF", file, 12),
    };
    block.init_function_name = "HAL_ADC_Init";

    const std::optional<MappedBlock> mapped = mapAdcBlock(block);
    ASSERT_TRUE(mapped.has_value());

    const FieldProvenance* ext_ref_provenance = findProvenance(*mapped, "ext_ref");
    ASSERT_NE(ext_ref_provenance, nullptr);
    EXPECT_TRUE(ext_ref_provenance->unresolved);
    EXPECT_EQ(ext_ref_provenance->original_raw_value_text, "config.use_external_ref");

    const FieldProvenance* channel_provenance = findProvenance(*mapped, "channel");
    ASSERT_NE(channel_provenance, nullptr);
    EXPECT_FALSE(channel_provenance->unresolved);
}

TEST(AdcReverseMapper, SerializeFieldRoundTrip)
{
    using reverse_import::serializeAdcFieldValue;
    EXPECT_EQ(serializeAdcFieldValue("channel", 5, ""), "ADC_CHANNEL5");
    EXPECT_EQ(serializeAdcFieldValue("ext_ref", true, ""), "ADC_EXTREF_ON");
    EXPECT_EQ(serializeAdcFieldValue("ext_ref", false, ""), "ADC_EXTREF_OFF");
    EXPECT_EQ(serializeAdcFieldValue("ext_clb", true, ""), "ADC_EXTCLB_ADCREF");
    EXPECT_EQ(serializeAdcFieldValue("ext_clb", false, ""), "ADC_EXTCLB_CLBREF");
}

TEST(AdcReverseMapper, SerializeFieldRejectsOutOfRangeOrWrongType)
{
    using reverse_import::serializeAdcFieldValue;
    EXPECT_FALSE(serializeAdcFieldValue("channel", 8, "").has_value());
    EXPECT_FALSE(serializeAdcFieldValue("channel", "0", "").has_value());
    EXPECT_FALSE(serializeAdcFieldValue("unknown_field", 1, "").has_value());
}
