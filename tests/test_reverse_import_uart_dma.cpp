//
// Тесты uart_dma_channel_reverse_mapper.{h,cpp} (parser/reverse_import/) —
// по стилю tests/test_reverse_import_dma.cpp. Фикстура: hdma_ch0/DMA_CH0_Init
// из docs/examples/mik32-examples/HAL_USART_DMA/src/main.c:80-101 — единственный
// реальный вендорный пример UART+DMA, канонический источник шаблона в
// codegen/project/uart_contribution.cpp.
//
#include <gtest/gtest.h>

#include "../parser/reverse_import/uart_dma_channel_reverse_mapper.h"

using reverse_import::RawFieldAssignment;
using reverse_import::RawInitBlock;
using reverse_import::SourceLocation;
using reverse_import::mapUartDmaChannelBlock;

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

// HAL_USART_DMA/src/main.c:80-101 (hdma_ch0, DMA_CH0_Init) — TX: Read=MEMORY
// (массив array), Write=PERIPHERY (UART_0->TXDATA), ReadRequest/WriteRequest
// == DMA_CHANNEL_USART_0_REQUEST.
RawInitBlock buildRealUsartDmaTxBlock()
{
    const std::string file = "HAL_USART_DMA/src/main.c";
    RawInitBlock block;
    block.handle_name = "hdma_ch0";
    block.handle_type = "DMA_ChannelHandleTypeDef";
    block.assignments = {
        makeAssignment("hdma_ch0", "ChannelInit.Channel", "DMA_CHANNEL_0", file, 82),
        makeAssignment("hdma_ch0", "ChannelInit.Priority", "DMA_CHANNEL_PRIORITY_VERY_HIGH", file, 83),
        makeAssignment("hdma_ch0", "ChannelInit.ReadMode", "DMA_CHANNEL_MODE_MEMORY", file, 85),
        makeAssignment("hdma_ch0", "ChannelInit.ReadInc", "DMA_CHANNEL_INC_ENABLE", file, 86),
        makeAssignment("hdma_ch0", "ChannelInit.ReadSize", "DMA_CHANNEL_SIZE_BYTE", file, 87),
        makeAssignment("hdma_ch0", "ChannelInit.ReadRequest", "DMA_CHANNEL_USART_0_REQUEST", file, 89),
        makeAssignment("hdma_ch0", "ChannelInit.ReadAck", "DMA_CHANNEL_ACK_DISABLE", file, 90),
        makeAssignment("hdma_ch0", "ChannelInit.WriteMode", "DMA_CHANNEL_MODE_PERIPHERY", file, 92),
        makeAssignment("hdma_ch0", "ChannelInit.WriteInc", "DMA_CHANNEL_INC_DISABLE", file, 93),
        makeAssignment("hdma_ch0", "ChannelInit.WriteSize", "DMA_CHANNEL_SIZE_BYTE", file, 94),
        makeAssignment("hdma_ch0", "ChannelInit.WriteRequest", "DMA_CHANNEL_USART_0_REQUEST", file, 96),
        makeAssignment("hdma_ch0", "ChannelInit.WriteAck", "DMA_CHANNEL_ACK_ENABLE", file, 97),
    };
    return block;
}

TEST(UartDmaChannelReverseMapper, UsartChannelRecognizedBeforeGenericMapperWouldMisrouteIt)
{
    // mapDmaChannelBlock сам по себе не фильтрует по request (принимает любой
    // DMA_ChannelHandleTypeDef) — отделение канала UART от generic "dma"
    // происходит в reverse_import.cpp диспетчеризацией: mapUartDmaChannelBlock
    // проверяется и перехватывает блок РАНЬШЕ, чем вызывается mapDmaChannelBlock
    // (см. reverse_import.cpp). Здесь фиксируем только то, что
    // mapUartDmaChannelBlock действительно распознаёт этот блок — без этого
    // условия диспетчер его не перехватит и он уйдёт в generic-маппер с
    // нераспознанным request-ом.
    const RawInitBlock block = buildRealUsartDmaTxBlock();
    EXPECT_TRUE(mapUartDmaChannelBlock(block).has_value());
}

TEST(UartDmaChannelReverseMapper, RecognizesTxDirectionAndInstance)
{
    const RawInitBlock block = buildRealUsartDmaTxBlock();
    const auto mapped = mapUartDmaChannelBlock(block);
    ASSERT_TRUE(mapped.has_value());
    EXPECT_EQ(mapped->target_uart_instance, 0);
    EXPECT_EQ(mapped->dma_json.at("direction"), "tx");
    EXPECT_EQ(mapped->dma_json.at("channel"), 0);
    EXPECT_EQ(mapped->dma_json.at("element_size"), "byte");
    EXPECT_EQ(mapped->dma_json.at("increment"), true);
}

TEST(UartDmaChannelReverseMapper, BufferTypeAndUnresolvedLenStayUnresolved)
{
    // Вендорный пример вызывает HAL_DMA_Start(..., strlen(array)-1) — это не
    // sizeof(массив)/литерал/макрос, узкий паттерн сканера его не берёт, поле
    // обязано остаться unresolved (см. feedback_reverse_import_unresolved_fields_policy),
    // а не угадываться.
    const RawInitBlock block = buildRealUsartDmaTxBlock();
    const auto mapped = mapUartDmaChannelBlock(block);
    ASSERT_TRUE(mapped.has_value());

    bool buffer_size_unresolved = false;
    bool buffer_type_unresolved = false;
    for (const auto& provenance : mapped->provenance)
    {
        if (provenance.json_field == "buffer_size_bytes")
        {
            buffer_size_unresolved = provenance.unresolved;
        }
        if (provenance.json_field == "buffer_type")
        {
            buffer_type_unresolved = provenance.unresolved;
        }
    }
    EXPECT_TRUE(buffer_size_unresolved);
    EXPECT_TRUE(buffer_type_unresolved);
}

TEST(UartDmaChannelReverseMapper, NonUsartRequestReturnsNullopt)
{
    // hdma_ch1 из HAL_DMA_SPI_Master — не UART-канал, должен уйти в
    // generic-маппер, а не сюда.
    RawInitBlock block;
    block.handle_name = "hdma_ch1";
    block.handle_type = "DMA_ChannelHandleTypeDef";
    block.assignments = {
        makeAssignment("hdma_ch1", "ChannelInit.WriteRequest", "DMA_CHANNEL_SPI_0_REQUEST",
                        "HAL_DMA_SPI_Master/src/main.c", 135),
    };
    EXPECT_FALSE(mapUartDmaChannelBlock(block).has_value());
}

} // namespace
