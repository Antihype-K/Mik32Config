#include "uart_dma_channel_reverse_mapper.h"

#include <regex>
#include <string>

namespace reverse_import
{

namespace
{

std::string trim(const std::string& text)
{
    const std::size_t begin = text.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos)
    {
        return "";
    }
    const std::size_t end = text.find_last_not_of(" \t\r\n");
    return text.substr(begin, end - begin + 1);
}

const RawFieldAssignment* lastAssignment(const RawInitBlock& block, const std::string& field_path)
{
    const RawFieldAssignment* found = nullptr;
    for (const RawFieldAssignment& assignment : block.assignments)
    {
        if (assignment.field_path == field_path)
        {
            found = &assignment;
        }
    }
    return found;
}

// "DMA_CHANNEL_USART_0_REQUEST" -> 0. Та же строка, что codegen пишет из
// knowledge_base/uart.json::dma_request_enum.
const std::regex kUsartRequest(R"(^DMA_CHANNEL_USART_([01])_REQUEST$)");

std::optional<int> usartInstanceFromRequest(const std::string& text)
{
    std::smatch match;
    const std::string trimmed = trim(text);
    if (std::regex_match(trimmed, match, kUsartRequest))
    {
        return std::stoi(match[1].str());
    }
    return std::nullopt;
}

std::optional<std::string> parseMode(const std::string& text)
{
    const std::string trimmed = trim(text);
    if (trimmed == "DMA_CHANNEL_MODE_MEMORY") return std::string("memory");
    if (trimmed == "DMA_CHANNEL_MODE_PERIPHERY") return std::string("periphery");
    return std::nullopt;
}

std::optional<std::string> parseSize(const std::string& text)
{
    const std::string trimmed = trim(text);
    if (trimmed == "DMA_CHANNEL_SIZE_BYTE") return std::string("byte");
    if (trimmed == "DMA_CHANNEL_SIZE_HALFWORD") return std::string("halfword");
    if (trimmed == "DMA_CHANNEL_SIZE_WORD") return std::string("word");
    return std::nullopt;
}

std::optional<bool> parseInc(const std::string& text)
{
    const std::string trimmed = trim(text);
    if (trimmed == "DMA_CHANNEL_INC_ENABLE") return true;
    if (trimmed == "DMA_CHANNEL_INC_DISABLE") return false;
    return std::nullopt;
}

const std::regex kChannelIndex(R"(^DMA_CHANNEL_([0-3])$)");

std::optional<int> parseChannel(const std::string& text)
{
    std::smatch match;
    const std::string trimmed = trim(text);
    if (std::regex_match(trimmed, match, kChannelIndex))
    {
        return match[1].str()[0] - '0';
    }
    return std::nullopt;
}

FieldProvenance fromAssignment(const std::string& json_field, const RawFieldAssignment& assignment, bool editable)
{
    FieldProvenance provenance;
    provenance.json_field = json_field;
    provenance.location = assignment.location;
    provenance.original_raw_value_text = assignment.raw_value_text;
    provenance.original_full_line_text = assignment.full_line_text;
    provenance.editable = editable;
    provenance.unresolved = !editable;
    return provenance;
}

// feedback_reverse_import_unresolved_fields_policy: поле не найдено/не
// распознано — не угадываем, помечаем явно.
FieldProvenance unresolvedProvenance(const std::string& json_field)
{
    FieldProvenance provenance;
    provenance.json_field = json_field;
    provenance.editable = false;
    provenance.unresolved = true;
    return provenance;
}

} // namespace

std::optional<MappedUartDmaChannel> mapUartDmaChannelBlock(const RawInitBlock& block)
{
    if (block.handle_type != "DMA_ChannelHandleTypeDef")
    {
        return std::nullopt;
    }

    const RawFieldAssignment* read_request = lastAssignment(block, "ChannelInit.ReadRequest");
    const RawFieldAssignment* write_request = lastAssignment(block, "ChannelInit.WriteRequest");

    std::optional<int> instance;
    const RawFieldAssignment* request_assignment = nullptr;
    if (read_request != nullptr)
    {
        instance = usartInstanceFromRequest(read_request->raw_value_text);
        if (instance.has_value())
        {
            request_assignment = read_request;
        }
    }
    if (!instance.has_value() && write_request != nullptr)
    {
        instance = usartInstanceFromRequest(write_request->raw_value_text);
        if (instance.has_value())
        {
            request_assignment = write_request;
        }
    }

    if (!instance.has_value())
    {
        // Ни ReadRequest, ни WriteRequest не похожи на UART-запрос — это не
        // UART-канал, пусть обрабатывается generic-маппером (dma_reverse_mapper).
        return std::nullopt;
    }

    MappedUartDmaChannel mapped;
    mapped.target_uart_instance = *instance;

    // ---- направление: Read=MEMORY/Write=PERIPHERY -> tx (память шлёт в
    // периферию), Read=PERIPHERY/Write=MEMORY -> rx ----
    const RawFieldAssignment* read_mode_assignment = lastAssignment(block, "ChannelInit.ReadMode");
    const RawFieldAssignment* write_mode_assignment = lastAssignment(block, "ChannelInit.WriteMode");
    const std::optional<std::string> read_mode =
        read_mode_assignment != nullptr ? parseMode(read_mode_assignment->raw_value_text) : std::nullopt;
    const std::optional<std::string> write_mode =
        write_mode_assignment != nullptr ? parseMode(write_mode_assignment->raw_value_text) : std::nullopt;

    std::string direction;
    const RawFieldAssignment* memory_side_inc_assignment = nullptr;
    if (read_mode == std::string("memory") && write_mode == std::string("periphery"))
    {
        direction = "tx";
        mapped.dma_json["direction"] = "tx";
        mapped.provenance.push_back(fromAssignment("direction", *write_mode_assignment, false));
        memory_side_inc_assignment = lastAssignment(block, "ChannelInit.ReadInc");
    }
    else if (read_mode == std::string("periphery") && write_mode == std::string("memory"))
    {
        direction = "rx";
        mapped.dma_json["direction"] = "rx";
        mapped.provenance.push_back(fromAssignment("direction", *write_mode_assignment, false));
        memory_side_inc_assignment = lastAssignment(block, "ChannelInit.WriteInc");
    }
    else
    {
        mapped.dma_json["direction"] = "tx";
        mapped.notes.push_back(
            "ChannelInit.ReadMode/WriteMode у UART-канала ПДП \"" + block.handle_name +
            "\" не образуют известную комбинацию (память<->периферия) — конфигуратор не "
            "смог определить направление (tx/rx), укажите его явно.");
        mapped.provenance.push_back(unresolvedProvenance("direction"));
        direction = "tx";
    }

    // ---- channel ----
    const RawFieldAssignment* channel_assignment = lastAssignment(block, "ChannelInit.Channel");
    if (channel_assignment == nullptr)
    {
        mapped.dma_json["channel"] = 0;
        mapped.notes.push_back(
            "ChannelInit.Channel не найден у UART-канала ПДП \"" + block.handle_name +
            "\" — укажите номер канала явно.");
        mapped.provenance.push_back(unresolvedProvenance("channel"));
    }
    else
    {
        const std::optional<int> value = parseChannel(channel_assignment->raw_value_text);
        if (value.has_value())
        {
            mapped.dma_json["channel"] = *value;
            mapped.provenance.push_back(fromAssignment("channel", *channel_assignment, false));
        }
        else
        {
            mapped.dma_json["channel"] = 0;
            mapped.notes.push_back(
                "ChannelInit.Channel=\"" + channel_assignment->raw_value_text + "\" у UART-канала "
                "ПДП \"" + block.handle_name + "\" не распознан — укажите номер канала явно.");
            mapped.provenance.push_back(unresolvedProvenance("channel"));
        }
    }

    // ---- element_size (ReadSize=WriteSize всегда, см. dma_reverse_mapper.cpp) ----
    const RawFieldAssignment* size_assignment = lastAssignment(block, "ChannelInit.ReadSize");
    if (size_assignment == nullptr)
    {
        mapped.dma_json["element_size"] = "byte";
        mapped.notes.push_back(
            "ChannelInit.ReadSize не найден у UART-канала ПДП \"" + block.handle_name +
            "\" — укажите размер элемента передачи явно.");
        mapped.provenance.push_back(unresolvedProvenance("element_size"));
    }
    else
    {
        const std::optional<std::string> value = parseSize(size_assignment->raw_value_text);
        if (value.has_value())
        {
            mapped.dma_json["element_size"] = *value;
            mapped.provenance.push_back(fromAssignment("element_size", *size_assignment, false));
        }
        else
        {
            mapped.dma_json["element_size"] = "byte";
            mapped.notes.push_back(
                "ChannelInit.ReadSize=\"" + size_assignment->raw_value_text + "\" у UART-канала "
                "ПДП \"" + block.handle_name + "\" не распознан — укажите размер элемента явно.");
            mapped.provenance.push_back(unresolvedProvenance("element_size"));
        }
    }

    // ---- increment: читаем Inc-поле СТОРОНЫ ПАМЯТИ (периферийная сторона
    // у UART всегда DISABLE — адрес TXDATA/RXDATA фиксирован, см.
    // uart_contribution.cpp) ----
    if (memory_side_inc_assignment == nullptr)
    {
        mapped.dma_json["increment"] = false;
        mapped.notes.push_back(
            "Inc-поле стороны памяти не найдено у UART-канала ПДП \"" + block.handle_name +
            "\" — укажите increment явно.");
        mapped.provenance.push_back(unresolvedProvenance("increment"));
    }
    else
    {
        const std::optional<bool> value = parseInc(memory_side_inc_assignment->raw_value_text);
        if (value.has_value())
        {
            mapped.dma_json["increment"] = *value;
            mapped.provenance.push_back(fromAssignment("increment", *memory_side_inc_assignment, false));
        }
        else
        {
            mapped.dma_json["increment"] = false;
            mapped.notes.push_back(
                "Inc-поле стороны памяти у UART-канала ПДП \"" + block.handle_name +
                "\" не распознано — укажите increment явно.");
            mapped.provenance.push_back(unresolvedProvenance("increment"));
        }
    }

    // ---- buffer_size_bytes: см. dma_reverse_mapper.cpp — тот же узкий
    // паттерн (sizeof(массив)/литерал/макрос в HAL_DMA_Start этого же
    // канала, уже разобран сканером заранее) ----
    if (block.resolved_buffer_size_bytes.has_value())
    {
        mapped.dma_json["buffer_size_bytes"] = *block.resolved_buffer_size_bytes;
        FieldProvenance provenance;
        provenance.json_field = "buffer_size_bytes";
        provenance.location = block.resolved_buffer_size_bytes_location;
        provenance.original_raw_value_text = block.resolved_buffer_size_bytes_source_text;
        provenance.original_full_line_text = block.resolved_buffer_size_bytes_source_text;
        provenance.editable = false;
        mapped.provenance.push_back(provenance);
    }
    else
    {
        mapped.dma_json["buffer_size_bytes"] = 1;
        mapped.notes.push_back(
            "Размер буфера UART-канала ПДП \"" + block.handle_name + "\" передаётся аргументом "
            "HAL_DMA_Start(...), который конфигуратор не смог разобрать (не прямой "
            "sizeof(массив)/литерал/макрос) — укажите buffer_size_bytes явно.");
        mapped.provenance.push_back(unresolvedProvenance("buffer_size_bytes"));
    }

    // ---- buffer_type: не кодируется присваиванием поля — только C-
    // комментарием в сгенерированном коде (codegen/project/uart_contribution.cpp,
    // dma_helpers), который сканер не читает как семантику. Не угадываем. ----
    mapped.dma_json["buffer_type"] = "linear";
    mapped.notes.push_back(
        "buffer_type (linear/circular) UART-канала ПДП \"" + block.handle_name + "\" нигде не "
        "присваивается явно в коде инициализации — конфигуратор не может его восстановить, "
        "укажите явно.");
    mapped.provenance.push_back(unresolvedProvenance("buffer_type"));

    return mapped;
}

} // namespace reverse_import
