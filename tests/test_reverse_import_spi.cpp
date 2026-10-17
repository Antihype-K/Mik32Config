//
// Тесты spi_reverse_mapper (см. parser/reverse_import/spi_reverse_mapper.h).
// RawInitBlock собран по образцу РЕАЛЬНОГО вывода собственного кодогена —
// generated_projects/lllll/src/mik32_init.c (~строки 37-49), обнаружено при
// импорте PlatformIO-проекта, когда SPI терялся как "неизвестный тип хендла".
//
#include <gtest/gtest.h>

#include <spi_reverse_mapper.h>

#include <algorithm>
#include <string>
#include <vector>

using reverse_import::FieldProvenance;

namespace
{

using reverse_import::RawFieldAssignment;
using reverse_import::RawInitBlock;
using reverse_import::SourceLocation;

RawFieldAssignment makeAssignment(const std::string& field_path, const std::string& raw_value_text, int line)
{
    RawFieldAssignment assignment;
    assignment.field_path = field_path;
    assignment.raw_value_text = raw_value_text;
    assignment.location = SourceLocation{"mik32_init.c", line};
    assignment.full_line_text = "    hspi0." + field_path + " = " + raw_value_text + ";";
    return assignment;
}

RawInitBlock buildRealSpi0MasterBlock()
{
    RawInitBlock block;
    block.handle_name = "hspi0";
    block.handle_type = "SPI_HandleTypeDef";
    block.assignments = {
        makeAssignment("Instance", "SPI_0", 37),
        makeAssignment("Init.SPI_Mode", "HAL_SPI_MODE_MASTER", 39),
        makeAssignment("Init.CLKPhase", "SPI_PHASE_ON", 40),
        makeAssignment("Init.CLKPolarity", "SPI_POLARITY_HIGH", 41),
        makeAssignment("Init.ThresholdTX", "4", 42),
        makeAssignment("Init.BaudRateDiv", "SPI_BAUDRATE_DIV64", 44),
        makeAssignment("Init.Decoder", "SPI_DECODER_NONE", 45),
        makeAssignment("Init.ManualCS", "SPI_MANUALCS_OFF", 46),
        makeAssignment("Init.ChipSelect", "SPI_CS_0", 47),
    };
    block.init_function_name = "HAL_SPI_Init";
    block.init_call_args = {"hspi0"};
    block.init_call_location = SourceLocation{"mik32_init.c", 49};
    return block;
}

} // namespace

using namespace reverse_import;

TEST(SpiReverseMapper, RejectsBlockWithWrongHandleType)
{
    RawInitBlock block;
    block.handle_type = "I2C_HandleTypeDef";
    EXPECT_FALSE(mapSpiBlock(block).has_value());
}

TEST(SpiReverseMapper, MapsRealMasterBlockFromOwnCodegenOutput)
{
    const RawInitBlock block = buildRealSpi0MasterBlock();
    const std::optional<MappedBlock> mapped = mapSpiBlock(block);

    ASSERT_TRUE(mapped.has_value());
    EXPECT_EQ(mapped->json_block.at("type"), "spi");
    EXPECT_EQ(mapped->json_block.at("instance"), 0);
    EXPECT_EQ(mapped->json_block.at("mode"), "master");
    EXPECT_EQ(mapped->json_block.at("clk_phase"), "on");
    EXPECT_EQ(mapped->json_block.at("clk_polarity"), "high");
    EXPECT_EQ(mapped->json_block.at("threshold_tx"), 4);
    EXPECT_EQ(mapped->json_block.at("baud_rate_div"), 64);
    EXPECT_EQ(mapped->json_block.at("decoder"), false);
    EXPECT_EQ(mapped->json_block.at("manual_cs"), false);
    EXPECT_EQ(mapped->json_block.at("chip_select"), "cs0");
}

TEST(SpiReverseMapper, SlaveBlockDoesNotSynthesizeMasterOnlyFields)
{
    RawInitBlock block;
    block.handle_name = "hspi0";
    block.handle_type = "SPI_HandleTypeDef";
    block.assignments = {
        makeAssignment("Instance", "SPI_0", 10),
        makeAssignment("Init.SPI_Mode", "HAL_SPI_MODE_SLAVE", 11),
        makeAssignment("Init.CLKPhase", "SPI_PHASE_OFF", 12),
        makeAssignment("Init.CLKPolarity", "SPI_POLARITY_LOW", 13),
        makeAssignment("Init.ThresholdTX", "4", 14),
    };

    const std::optional<MappedBlock> mapped = mapSpiBlock(block);
    ASSERT_TRUE(mapped.has_value());
    EXPECT_EQ(mapped->json_block.at("mode"), "slave");
    EXPECT_FALSE(mapped->json_block.contains("baud_rate_div"));
    EXPECT_FALSE(mapped->json_block.contains("manual_cs"));
    EXPECT_FALSE(mapped->json_block.contains("decoder"));
    EXPECT_FALSE(mapped->json_block.contains("chip_select"));
}

TEST(SpiReverseMapper, SerializeAllFieldsRoundTrip)
{
    EXPECT_EQ(serializeSpiFieldValue("instance", 1, ""), "SPI_1");
    EXPECT_EQ(serializeSpiFieldValue("mode", "slave", ""), "HAL_SPI_MODE_SLAVE");
    EXPECT_EQ(serializeSpiFieldValue("clk_phase", "on", ""), "SPI_PHASE_ON");
    EXPECT_EQ(serializeSpiFieldValue("clk_polarity", "high", ""), "SPI_POLARITY_HIGH");
    EXPECT_EQ(serializeSpiFieldValue("threshold_tx", 8, ""), "8");
    EXPECT_EQ(serializeSpiFieldValue("baud_rate_div", 256, ""), "SPI_BAUDRATE_DIV256");
    EXPECT_EQ(serializeSpiFieldValue("decoder", true, ""), "SPI_DECODER_USE");
    EXPECT_EQ(serializeSpiFieldValue("manual_cs", true, ""), "SPI_MANUALCS_ON");
    EXPECT_EQ(serializeSpiFieldValue("chip_select", "cs3", ""), "SPI_CS_3");
    EXPECT_FALSE(serializeSpiFieldValue("threshold_tx", 9, "").has_value());
    EXPECT_FALSE(serializeSpiFieldValue("baud_rate_div", 3, "").has_value());
}

namespace
{
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

// ---- unresolved-политика (feedback_reverse_import_unresolved_fields_policy) ----

TEST(SpiReverseMapper, MissingInstanceIsUnresolvedNotDefaulted)
{
    RawInitBlock block;
    block.handle_name = "hspi0";
    block.handle_type = "SPI_HandleTypeDef";
    block.assignments = {
        makeAssignment("Init.SPI_Mode", "HAL_SPI_MODE_MASTER", 1),
    };

    const std::optional<MappedBlock> mapped = mapSpiBlock(block);
    ASSERT_TRUE(mapped.has_value());

    const FieldProvenance* provenance = findProvenance(*mapped, "instance");
    ASSERT_NE(provenance, nullptr);
    EXPECT_TRUE(provenance->unresolved);
}

TEST(SpiReverseMapper, UnparseableChipSelectIsUnresolvedNotDefaulted)
{
    RawInitBlock block;
    block.handle_name = "hspi0";
    block.handle_type = "SPI_HandleTypeDef";
    block.assignments = {
        makeAssignment("Instance", "SPI_0", 1),
        makeAssignment("Init.SPI_Mode", "HAL_SPI_MODE_MASTER", 2),
        makeAssignment("Init.ChipSelect", "config.cs", 3),
    };

    const std::optional<MappedBlock> mapped = mapSpiBlock(block);
    ASSERT_TRUE(mapped.has_value());

    const FieldProvenance* provenance = findProvenance(*mapped, "chip_select");
    ASSERT_NE(provenance, nullptr);
    EXPECT_TRUE(provenance->unresolved);
}

TEST(SpiReverseMapper, MissingMasterOnlyFieldIsUnresolvedNotDefaulted)
{
    RawInitBlock block;
    block.handle_name = "hspi0";
    block.handle_type = "SPI_HandleTypeDef";
    block.assignments = {
        makeAssignment("Instance", "SPI_0", 1),
        makeAssignment("Init.SPI_Mode", "HAL_SPI_MODE_MASTER", 2),
    };

    const std::optional<MappedBlock> mapped = mapSpiBlock(block);
    ASSERT_TRUE(mapped.has_value());

    const FieldProvenance* provenance = findProvenance(*mapped, "baud_rate_div");
    ASSERT_NE(provenance, nullptr);
    EXPECT_TRUE(provenance->unresolved);
}
