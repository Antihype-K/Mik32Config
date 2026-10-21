//
// Маппер для DMA_ChannelHandleTypeDef, который обслуживает UART, а не
// самостоятельную периферию ПДП. У MIK32 нет общего enum "request" для
// UART (schemas/dma.json::_comment_usart_excluded — USART настраивается
// встроенным полем "dma" в schemas/uart.json, а не отдельным блоком "dma"),
// поэтому такой канал нельзя пропускать через dma_reverse_mapper::
// mapDmaChannelBlock — он получил бы нераспознанный request и улетел бы в
// generic "dma"-периферию вместо вложенного uart.dma[].
//
// Распознаётся НЕ по имени хендла (вендорные примеры называют канал
// произвольно, например "hdma_ch0" в
// docs/examples/mik32-examples/HAL_USART_DMA/src/main.c), а по тексту
// ChannelInit.ReadRequest/WriteRequest == "DMA_CHANNEL_USART_<n>_REQUEST" —
// то же значение, которое кодоген сам туда пишет (codegen/project/
// uart_contribution.cpp, dma_request_enum из knowledge_base/uart.json).
//
// Контракт — как у timer32_channel_reverse_mapper.h: результат не отдельный
// MappedBlock, а фрагмент "dma"-массива ВНУТРИ родительского uart-блока;
// reverse_import.cpp сам вставляет его в уже существующий json_block
// родителя по совпадению instance (не handle_name).
//
#ifndef MIK32_CONFIGURATOR_UART_DMA_CHANNEL_REVERSE_MAPPER_H
#define MIK32_CONFIGURATOR_UART_DMA_CHANNEL_REVERSE_MAPPER_H

#include <optional>
#include <vector>

#include "json.hpp"
#include "mapped_block.h"
#include "reverse_import_scanner.h"

namespace reverse_import
{

struct MappedUartDmaChannel
{
    // instance родительского UART (0/1), распознанный из
    // "DMA_CHANNEL_USART_<n>_REQUEST". -1, если распознать не удалось —
    // вызывающая сторона должна проверить это перед поиском родителя.
    int target_uart_instance = -1;

    // Форма — ровно item_fields UartDmaConfig (schemas/uart.json::dma):
    // channel, direction, buffer_type, buffer_size_bytes, element_size,
    // increment.
    nlohmann::json dma_json;

    // Без префикса "dma[N]." — префикс с позицией добавляет
    // reverse_import.cpp в момент присоединения к родителю, как и у
    // timer32-каналов.
    std::vector<FieldProvenance> provenance;

    std::vector<std::string> notes;
};

// Возвращает nullopt, если block не является DMA-каналом, обслуживающим
// UART (ReadRequest/WriteRequest не совпал с "DMA_CHANNEL_USART_<n>_REQUEST").
// Если распознан как UART-канал, но направление/instance не читаются — всё
// равно возвращает значение с объясняющей note, а не nullopt (та же причина,
// что в timer32_channel_reverse_mapper.h: note не должна потеряться).
std::optional<MappedUartDmaChannel> mapUartDmaChannelBlock(const RawInitBlock& block);

} // namespace reverse_import

#endif //MIK32_CONFIGURATOR_UART_DMA_CHANNEL_REVERSE_MAPPER_H
