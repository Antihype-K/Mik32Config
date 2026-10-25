//
// Тесты dma_reverse_mapper.{h,cpp} (parser/reverse_import/) — по стилю
// tests/test_reverse_import_adc.cpp. Фикстуры: hdma_ch0/hdma_ch1 из
// docs/examples/mik32-examples/HAL_DMA_SPI_Master/src/main.c (обе стороны
// direction) и hdma_ch0 из HAL_DMA_Memory_Memor_IRQ/src/main.c (irq_enable).
//
#include <gtest/gtest.h>

#include "../parser/reverse_import/dma_reverse_mapper.h"

using reverse_import::FieldProvenance;
using reverse_import::MappedBlock;
using reverse_import::RawFieldAssignment;
using reverse_import::RawInitBlock;
using reverse_import::RawTrailingCall;
using reverse_import::SourceLocation;
using reverse_import::mapDmaChannelBlock;

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

// docs/examples/mik32-examples/HAL_DMA_SPI_Master/src/main.c:121-136 (hdma_ch0,
// DMA_CH0_Init) — направление peripheral_to_memory... нет, Read=MEMORY/Write=PERIPHERY
// => memory_to_peripheral.
RawInitBlock buildRealDmaCh0Block()
{
    const std::string file = "HAL_DMA_SPI_Master/src/main.c";
    RawInitBlock block;
    block.handle_name = "hdma_ch0";
    block.handle_type = "DMA_ChannelHandleTypeDef";
    block.assignments = {
        makeAssignment("hdma_ch0", "ChannelInit.Channel", "DMA_CHANNEL_0", file, 121),
        makeAssignment("hdma_ch0", "ChannelInit.Priority", "DMA_CHANNEL_PRIORITY_VERY_HIGH", file, 122),
        makeAssignment("hdma_ch0", "ChannelInit.ReadMode", "DMA_CHANNEL_MODE_MEMORY", file, 124),
        makeAssignment("hdma_ch0", "ChannelInit.ReadSize", "DMA_CHANNEL_SIZE_BYTE", file, 126),
        makeAssignment("hdma_ch0", "ChannelInit.WriteMode", "DMA_CHANNEL_MODE_PERIPHERY", file, 131),
        makeAssignment("hdma_ch0", "ChannelInit.WriteSize", "DMA_CHANNEL_SIZE_BYTE", file, 133),
        makeAssignment("hdma_ch0", "ChannelInit.WriteRequest", "DMA_CHANNEL_SPI_0_REQUEST", file, 135),
    };
    return block;
}

// docs/examples/mik32-examples/HAL_DMA_SPI_Master/src/main.c:144-158 (hdma_ch1,
// DMA_CH1_Init) — Read=PERIPHERY/Write=MEMORY => peripheral_to_memory.
RawInitBlock buildRealDmaCh1Block()
{
    const std::string file = "HAL_DMA_SPI_Master/src/main.c";
    RawInitBlock block;
    block.handle_name = "hdma_ch1";
    block.handle_type = "DMA_ChannelHandleTypeDef";
    block.assignments = {
        makeAssignment("hdma_ch1", "ChannelInit.Channel", "DMA_CHANNEL_1", file, 144),
        makeAssignment("hdma_ch1", "ChannelInit.Priority", "DMA_CHANNEL_PRIORITY_VERY_HIGH", file, 145),
        makeAssignment("hdma_ch1", "ChannelInit.ReadMode", "DMA_CHANNEL_MODE_PERIPHERY", file, 147),
        makeAssignment("hdma_ch1", "ChannelInit.ReadSize", "DMA_CHANNEL_SIZE_BYTE", file, 149),
        makeAssignment("hdma_ch1", "ChannelInit.ReadRequest", "DMA_CHANNEL_SPI_0_REQUEST", file, 151),
        makeAssignment("hdma_ch1", "ChannelInit.WriteMode", "DMA_CHANNEL_MODE_MEMORY", file, 154),
        makeAssignment("hdma_ch1", "ChannelInit.WriteSize", "DMA_CHANNEL_SIZE_BYTE", file, 156),
    };
    return block;
}

// docs/examples/mik32-examples/HAL_DMA_Memory_Memor_IRQ/src/main.c:72-90 —
// memory_to_memory + HAL_DMA_LocalIRQEnable(&hdma_ch0, ...).
RawInitBlock buildRealDmaMemoryIrqBlock()
{
    const std::string file = "HAL_DMA_Memory_Memor_IRQ/src/main.c";
    RawInitBlock block;
    block.handle_name = "hdma_ch0";
    block.handle_type = "DMA_ChannelHandleTypeDef";
    block.assignments = {
        makeAssignment("hdma_ch0", "ChannelInit.Channel", "DMA_CHANNEL_0", file, 72),
        makeAssignment("hdma_ch0", "ChannelInit.Priority", "DMA_CHANNEL_PRIORITY_VERY_HIGH", file, 73),
        makeAssignment("hdma_ch0", "ChannelInit.ReadMode", "DMA_CHANNEL_MODE_MEMORY", file, 75),
        makeAssignment("hdma_ch0", "ChannelInit.ReadSize", "DMA_CHANNEL_SIZE_BYTE", file, 77),
        makeAssignment("hdma_ch0", "ChannelInit.WriteMode", "DMA_CHANNEL_MODE_MEMORY", file, 82),
        makeAssignment("hdma_ch0", "ChannelInit.WriteSize", "DMA_CHANNEL_SIZE_BYTE", file, 84),
    };
    RawTrailingCall call;
    call.function_name = "HAL_DMA_LocalIRQEnable";
    call.args = {"hdma_ch0", "DMA_IRQ_ENABLE"};
    block.trailing_calls = {call};
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

TEST(DmaReverseMapper, WrongHandleTypeReturnsNullopt)
{
    RawInitBlock block;
    block.handle_type = "DMA_InitTypeDef";
    EXPECT_FALSE(mapDmaChannelBlock(block).has_value());
}

TEST(DmaReverseMapper, MemoryToPeripheralDirectionParsesAllFields)
{
    const std::optional<MappedBlock> mapped = mapDmaChannelBlock(buildRealDmaCh0Block());
    ASSERT_TRUE(mapped.has_value());

    EXPECT_EQ(mapped->json_block["type"], "dma");
    EXPECT_EQ(mapped->json_block["instance"], 0);
    EXPECT_EQ(mapped->json_block["priority"], "very_high");
    EXPECT_EQ(mapped->json_block["element_size"], "byte");
    EXPECT_EQ(mapped->json_block["direction"], "memory_to_peripheral");
    EXPECT_EQ(mapped->json_block["request"], "spi_0");

    for (const char* field : {"instance", "priority", "element_size", "direction", "request"})
    {
        const FieldProvenance* provenance = findProvenance(*mapped, field);
        ASSERT_NE(provenance, nullptr) << field;
        EXPECT_FALSE(provenance->unresolved) << field;
    }
}

TEST(DmaReverseMapper, PeripheralToMemoryDirectionParsesAllFields)
{
    const std::optional<MappedBlock> mapped = mapDmaChannelBlock(buildRealDmaCh1Block());
    ASSERT_TRUE(mapped.has_value());

    EXPECT_EQ(mapped->json_block["instance"], 1);
    EXPECT_EQ(mapped->json_block["direction"], "peripheral_to_memory");
    EXPECT_EQ(mapped->json_block["request"], "spi_0");

    const FieldProvenance* direction_provenance = findProvenance(*mapped, "direction");
    ASSERT_NE(direction_provenance, nullptr);
    EXPECT_FALSE(direction_provenance->unresolved);
}

TEST(DmaReverseMapper, MemoryToMemoryDirectionHasNoRequestField)
{
    const std::optional<MappedBlock> mapped = mapDmaChannelBlock(buildRealDmaMemoryIrqBlock());
    ASSERT_TRUE(mapped.has_value());

    EXPECT_EQ(mapped->json_block["direction"], "memory_to_memory");
    EXPECT_FALSE(mapped->json_block.contains("request"));

    const FieldProvenance* direction_provenance = findProvenance(*mapped, "direction");
    ASSERT_NE(direction_provenance, nullptr);
    EXPECT_FALSE(direction_provenance->unresolved);
}

TEST(DmaReverseMapper, LocalIrqEnableCallSetsIrqEnableTrue)
{
    const std::optional<MappedBlock> mapped = mapDmaChannelBlock(buildRealDmaMemoryIrqBlock());
    ASSERT_TRUE(mapped.has_value());
    EXPECT_EQ(mapped->json_block["irq_enable"], true);
}

TEST(DmaReverseMapper, NoLocalIrqEnableCallSetsIrqEnableFalse)
{
    const std::optional<MappedBlock> mapped = mapDmaChannelBlock(buildRealDmaCh0Block());
    ASSERT_TRUE(mapped.has_value());
    EXPECT_EQ(mapped->json_block["irq_enable"], false);
}

TEST(DmaReverseMapper, BufferSizeBytesIsAlwaysUnresolved)
{
    const std::optional<MappedBlock> mapped = mapDmaChannelBlock(buildRealDmaCh0Block());
    ASSERT_TRUE(mapped.has_value());

    const FieldProvenance* provenance = findProvenance(*mapped, "buffer_size_bytes");
    ASSERT_NE(provenance, nullptr);
    EXPECT_TRUE(provenance->unresolved);
}

TEST(DmaReverseMapper, MissingChannelFieldIsUnresolvedNotDefaulted)
{
    const std::string file = "no_channel/src/main.c";
    RawInitBlock block;
    block.handle_name = "hdma_ch0";
    block.handle_type = "DMA_ChannelHandleTypeDef";
    block.assignments = {
        makeAssignment("hdma_ch0", "ChannelInit.Priority", "DMA_CHANNEL_PRIORITY_VERY_HIGH", file, 10),
        makeAssignment("hdma_ch0", "ChannelInit.ReadMode", "DMA_CHANNEL_MODE_MEMORY", file, 11),
        makeAssignment("hdma_ch0", "ChannelInit.WriteMode", "DMA_CHANNEL_MODE_MEMORY", file, 12),
    };

    const std::optional<MappedBlock> mapped = mapDmaChannelBlock(block);
    ASSERT_TRUE(mapped.has_value());

    const FieldProvenance* provenance = findProvenance(*mapped, "instance");
    ASSERT_NE(provenance, nullptr);
    EXPECT_TRUE(provenance->unresolved);
    EXPECT_FALSE(mapped->notes.empty());
}

TEST(DmaReverseMapper, UnparseableRequestLiteralIsUnresolvedNotDefaulted)
{
    const std::string file = "weird_request/src/main.c";
    RawInitBlock block;
    block.handle_name = "hdma_ch0";
    block.handle_type = "DMA_ChannelHandleTypeDef";
    block.assignments = {
        makeAssignment("hdma_ch0", "ChannelInit.Channel", "DMA_CHANNEL_0", file, 10),
        makeAssignment("hdma_ch0", "ChannelInit.Priority", "DMA_CHANNEL_PRIORITY_VERY_HIGH", file, 11),
        makeAssignment("hdma_ch0", "ChannelInit.ReadMode", "DMA_CHANNEL_MODE_PERIPHERY", file, 12),
        makeAssignment("hdma_ch0", "ChannelInit.ReadRequest", "config.custom_request", file, 13),
        makeAssignment("hdma_ch0", "ChannelInit.WriteMode", "DMA_CHANNEL_MODE_MEMORY", file, 14),
    };

    const std::optional<MappedBlock> mapped = mapDmaChannelBlock(block);
    ASSERT_TRUE(mapped.has_value());

    EXPECT_EQ(mapped->json_block["direction"], "peripheral_to_memory");
    const FieldProvenance* direction_provenance = findProvenance(*mapped, "direction");
    ASSERT_NE(direction_provenance, nullptr);
    EXPECT_FALSE(direction_provenance->unresolved);

    const FieldProvenance* request_provenance = findProvenance(*mapped, "request");
    ASSERT_NE(request_provenance, nullptr);
    EXPECT_TRUE(request_provenance->unresolved);
}

TEST(DmaReverseMapper, SerializeFieldValueRoundTrip)
{
    using reverse_import::serializeDmaFieldValue;
    EXPECT_EQ(serializeDmaFieldValue("instance", 2, ""), "DMA_CHANNEL_2");
    EXPECT_EQ(serializeDmaFieldValue("priority", "high", ""), "DMA_CHANNEL_PRIORITY_HIGH");
    EXPECT_EQ(serializeDmaFieldValue("element_size", "word", ""), "DMA_CHANNEL_SIZE_WORD");
    EXPECT_FALSE(serializeDmaFieldValue("instance", 7, "").has_value());
    // direction/request/buffer_size_bytes/irq_enable — не однострочные поля,
    // остаются unpatchable (см. dma_reverse_mapper.h).
    EXPECT_FALSE(serializeDmaFieldValue("direction", "memory_to_memory", "").has_value());
}
