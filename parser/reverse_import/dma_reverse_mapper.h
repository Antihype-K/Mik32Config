//
// Типовой маппер reverse-import для DMA-канала: превращает RawInitBlock,
// собранный reverse_import_scanner.h из "hdma_ch0.ChannelInit.<field> =
// <value>;" присваиваний, в JSON-блок формы schemas/dma.json.
//
// ВАЖНО: для DMA_ChannelHandleTypeDef в реальном HAL нет функции
// инициализации (mik32_hal_dma.h подтверждён побайтово — есть только
// HAL_DMA_Init для ГЛОБАЛЬНОГО DMA_InitTypeDef, не для канала; см.
// codegen/project/dma_contribution.cpp, тот же факт задокументирован там).
// Поэтому reverse_import_scanner.cpp закрывает блок канала НЕ по вызову
// Init, а в конце файла, как только у хендла накопились присваивания —
// см. ветку "DMA_ChannelHandleTypeDef" в scanFile(). block.init_function_name
// у такого блока пуст, block.init_call_args пуст — это ожидаемо, не ошибка.
//
// buffer_size_bytes в структуре канала вообще не присваивается — реальный
// размер передаётся как аргумент Len в HAL_DMA_Start(...) (например
// "sizeof(plain_text) - 1"), который сканер не разбирает (C-выражение, не
// литерал, и вызов обычно содержит вложенные скобки — вне регулярок
// сканера). Поле ВСЕГДА unresolved — см.
// feedback_reverse_import_unresolved_fields_policy в памяти ассистента.
//
// Глобальный DMA_InitTypeDef (hdma.Instance = DMA_CONFIG; .CurrentValue = ...)
// не имеет отдельного JSON-блока в этом конфигураторе (schemas/dma.json —
// это блок КАНАЛА, instance = номер канала 0-3) — в reverse_import.cpp такой
// блок пропускается с поясняющей note, см. там же.
//
#ifndef MIK32_CONFIGURATOR_DMA_REVERSE_MAPPER_H
#define MIK32_CONFIGURATOR_DMA_REVERSE_MAPPER_H

#include <optional>
#include <string>

#include "json.hpp"
#include "mapped_block.h"
#include "reverse_import_scanner.h"

namespace reverse_import
{

// block.handle_type должен быть "DMA_ChannelHandleTypeDef", иначе nullopt.
std::optional<MappedBlock> mapDmaChannelBlock(const RawInitBlock& block);

// Обратная сериализация для точечной правки — покрывает только instance/
// priority/element_size (прямые однострочные присваивания). direction/
// request списаны с ДВУХ полей (ReadMode+WriteMode) одновременно,
// buffer_size_bytes читается из аргумента вызова HAL_DMA_Start (не поле),
// irq_enable — из отдельного вызова HAL_DMA_LocalIRQEnable — однострочный
// сериализатор поля не может безопасно перезаписать эти формы без риска
// рассинхронизировать пары строк, поэтому они остаются unpatchable
// (см. feedback_reverse_import_unresolved_fields_policy — не угадываем).
std::optional<std::string> serializeDmaFieldValue(
    const std::string& field_name,
    const nlohmann::json& new_value,
    const std::string& original_raw_value_text);

} // namespace reverse_import

#endif //MIK32_CONFIGURATOR_DMA_REVERSE_MAPPER_H
