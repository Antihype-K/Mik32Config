//
// mapDmaChannelBlock — см. dma_reverse_mapper.h за контрактом. Разбор основан
// на полях ChannelInit во всех 8 вендорных примерах
// docs/examples/mik32-examples/HAL_DMA_* (Crypto/I2C_Master/I2C_Slave/
// Memory_Memory/Memory_Memor_IRQ/SPI_Master/SPI_Slave/Timer32) — таблицы
// request/size/priority сверены с codegen/project/dma_contribution.cpp
// (kRequestEnum/kSizeEnum/kPriorityEnum), чтобы остаться обратимыми 1:1.
//
#include "dma_reverse_mapper.h"

#include <cctype>
#include <string>
#include <vector>

namespace reverse_import
{

namespace
{

std::string trimText(const std::string& text)
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

FieldProvenance makeProvenance(const std::string& json_field, const RawFieldAssignment& assignment, bool editable)
{
    FieldProvenance provenance;
    provenance.json_field = json_field;
    provenance.location = assignment.location;
    provenance.original_raw_value_text = assignment.raw_value_text;
    provenance.original_full_line_text = assignment.full_line_text;
    provenance.editable = editable;
    return provenance;
}

// unresolved=true — см. feedback_reverse_import_unresolved_fields_policy:
// ни "поле не найдено", ни "поле найдено, но текст не распознан" не
// получают угаданный дефолт без отметки.
FieldProvenance makeUnresolvedProvenance(const std::string& json_field)
{
    FieldProvenance provenance;
    provenance.json_field = json_field;
    provenance.editable = false;
    provenance.unresolved = true;
    return provenance;
}

// "DMA_CHANNEL_n" -> n (0-3). mik32_hal_dma.h: HAL_DMA_ChannelIndexTypeDef.
std::optional<int> parseChannel(const std::string& text)
{
    const std::string trimmed = trimText(text);
    const std::string prefix = "DMA_CHANNEL_";
    if (trimmed.rfind(prefix, 0) != 0)
    {
        return std::nullopt;
    }
    const std::string digits = trimmed.substr(prefix.size());
    if (digits.size() != 1 || digits.find_first_not_of("0123") != std::string::npos)
    {
        return std::nullopt;
    }
    return digits[0] - '0';
}

// "DMA_CHANNEL_MODE_MEMORY" / "DMA_CHANNEL_MODE_PERIPHERY".
std::optional<std::string> parseMode(const std::string& text)
{
    const std::string trimmed = trimText(text);
    if (trimmed == "DMA_CHANNEL_MODE_MEMORY")
    {
        return std::string("memory");
    }
    if (trimmed == "DMA_CHANNEL_MODE_PERIPHERY")
    {
        return std::string("periphery");
    }
    return std::nullopt;
}

// "DMA_CHANNEL_SIZE_BYTE/HALFWORD/WORD" -> schemas/dma.json "element_size".
std::optional<std::string> parseSize(const std::string& text)
{
    const std::string trimmed = trimText(text);
    if (trimmed == "DMA_CHANNEL_SIZE_BYTE")
    {
        return std::string("byte");
    }
    if (trimmed == "DMA_CHANNEL_SIZE_HALFWORD")
    {
        return std::string("halfword");
    }
    if (trimmed == "DMA_CHANNEL_SIZE_WORD")
    {
        return std::string("word");
    }
    return std::nullopt;
}

// "DMA_CHANNEL_PRIORITY_LOW/MEDIUM/HIGH/VERY_HIGH" -> schemas/dma.json "priority".
std::optional<std::string> parsePriority(const std::string& text)
{
    const std::string trimmed = trimText(text);
    if (trimmed == "DMA_CHANNEL_PRIORITY_LOW")
    {
        return std::string("low");
    }
    if (trimmed == "DMA_CHANNEL_PRIORITY_MEDIUM")
    {
        return std::string("medium");
    }
    if (trimmed == "DMA_CHANNEL_PRIORITY_HIGH")
    {
        return std::string("high");
    }
    if (trimmed == "DMA_CHANNEL_PRIORITY_VERY_HIGH")
    {
        return std::string("very_high");
    }
    return std::nullopt;
}

// "DMA_CHANNEL_<NAME>_REQUEST" -> lowercase(<NAME>), проверено по
// kRequestEnum в codegen/project/dma_contribution.cpp (crypto, spi_0, spi_1,
// i2c_0, i2c_1, spifi, timer32_0/1/2, dac_0/1). "0" (буквальный ноль,
// memory_to_memory-примеры) сюда не доходит — вызывающий код обращается к
// Request только когда хотя бы одна сторона PERIPHERY.
std::optional<std::string> parseRequest(const std::string& text)
{
    static const std::vector<std::string> kKnown = {
        "crypto", "spi_0", "spi_1", "i2c_0", "i2c_1", "spifi",
        "timer32_0", "timer32_1", "timer32_2", "dac_0", "dac_1"};

    const std::string trimmed = trimText(text);
    const std::string prefix = "DMA_CHANNEL_";
    const std::string suffix = "_REQUEST";
    if (trimmed.rfind(prefix, 0) != 0 ||
        trimmed.size() <= prefix.size() + suffix.size() ||
        trimmed.compare(trimmed.size() - suffix.size(), suffix.size(), suffix) != 0)
    {
        return std::nullopt;
    }
    std::string middle = trimmed.substr(prefix.size(), trimmed.size() - prefix.size() - suffix.size());
    for (char& c : middle)
    {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    for (const std::string& known : kKnown)
    {
        if (known == middle)
        {
            return middle;
        }
    }
    return std::nullopt;
}

bool hasLocalIrqEnableCall(const RawInitBlock& block)
{
    for (const RawTrailingCall& call : block.trailing_calls)
    {
        if (call.function_name == "HAL_DMA_LocalIRQEnable" &&
            !call.args.empty() && call.args[0] == block.handle_name)
        {
            return true;
        }
    }
    return false;
}

} // namespace

std::optional<MappedBlock> mapDmaChannelBlock(const RawInitBlock& block)
{
    if (block.handle_type != "DMA_ChannelHandleTypeDef")
    {
        return std::nullopt;
    }

    MappedBlock result;
    result.json_block["type"] = "dma";

    // ---- instance (из "ChannelInit.Channel = DMA_CHANNEL_n;") ----
    {
        const RawFieldAssignment* assignment = lastAssignment(block, "ChannelInit.Channel");
        if (assignment == nullptr)
        {
            result.json_block["instance"] = 0;
            result.notes.push_back(
                "ChannelInit.Channel не найден — конфигуратор не смог определить номер "
                "канала ПДП, укажите instance явно.");
            result.provenance.push_back(makeUnresolvedProvenance("instance"));
        }
        else
        {
            const std::optional<int> value = parseChannel(assignment->raw_value_text);
            if (value.has_value())
            {
                result.json_block["instance"] = *value;
                result.provenance.push_back(makeProvenance("instance", *assignment, false));
            }
            else
            {
                result.json_block["instance"] = 0;
                result.notes.push_back(
                    "ChannelInit.Channel=\"" + assignment->raw_value_text + "\" (" +
                    assignment->location.fileName() + ":" + std::to_string(assignment->location.line) +
                    ") — конфигуратор не смог определить номер канала ПДП, укажите instance "
                    "явно.");
                FieldProvenance provenance = makeUnresolvedProvenance("instance");
                provenance.location = assignment->location;
                provenance.original_raw_value_text = assignment->raw_value_text;
                provenance.original_full_line_text = assignment->full_line_text;
                result.provenance.push_back(provenance);
            }
        }
    }

    // ---- priority (из "ChannelInit.Priority = DMA_CHANNEL_PRIORITY_*;") ----
    {
        const RawFieldAssignment* assignment = lastAssignment(block, "ChannelInit.Priority");
        if (assignment == nullptr)
        {
            result.json_block["priority"] = "very_high";
            result.notes.push_back(
                "ChannelInit.Priority не найден — конфигуратор не смог определить приоритет "
                "канала, укажите priority явно.");
            result.provenance.push_back(makeUnresolvedProvenance("priority"));
        }
        else
        {
            const std::optional<std::string> value = parsePriority(assignment->raw_value_text);
            if (value.has_value())
            {
                result.json_block["priority"] = *value;
                result.provenance.push_back(makeProvenance("priority", *assignment, false));
            }
            else
            {
                result.json_block["priority"] = "very_high";
                result.notes.push_back(
                    "ChannelInit.Priority=\"" + assignment->raw_value_text + "\" (" +
                    assignment->location.fileName() + ":" + std::to_string(assignment->location.line) +
                    ") — конфигуратор не смог определить приоритет канала, укажите priority "
                    "явно.");
                FieldProvenance provenance = makeUnresolvedProvenance("priority");
                provenance.location = assignment->location;
                provenance.original_raw_value_text = assignment->raw_value_text;
                provenance.original_full_line_text = assignment->full_line_text;
                result.provenance.push_back(provenance);
            }
        }
    }

    // ---- element_size (из "ChannelInit.ReadSize", дублируется в WriteSize —
    // берём ReadSize, как единственный источник схемы) ----
    {
        const RawFieldAssignment* assignment = lastAssignment(block, "ChannelInit.ReadSize");
        if (assignment == nullptr)
        {
            result.json_block["element_size"] = "byte";
            result.notes.push_back(
                "ChannelInit.ReadSize не найден — конфигуратор не смог определить размер "
                "элемента передачи, укажите element_size явно.");
            result.provenance.push_back(makeUnresolvedProvenance("element_size"));
        }
        else
        {
            const std::optional<std::string> value = parseSize(assignment->raw_value_text);
            if (value.has_value())
            {
                result.json_block["element_size"] = *value;
                result.provenance.push_back(makeProvenance("element_size", *assignment, false));
            }
            else
            {
                result.json_block["element_size"] = "byte";
                result.notes.push_back(
                    "ChannelInit.ReadSize=\"" + assignment->raw_value_text + "\" (" +
                    assignment->location.fileName() + ":" + std::to_string(assignment->location.line) +
                    ") — конфигуратор не смог определить размер элемента передачи, укажите "
                    "element_size явно.");
                FieldProvenance provenance = makeUnresolvedProvenance("element_size");
                provenance.location = assignment->location;
                provenance.original_raw_value_text = assignment->raw_value_text;
                provenance.original_full_line_text = assignment->full_line_text;
                result.provenance.push_back(provenance);
            }
        }
    }

    // ---- direction + request (из связки ChannelInit.ReadMode/WriteMode,
    // см. schemas/dma.json "direction": peripheral_to_memory (Read=PERIPHERY/
    // Write=MEMORY) | memory_to_peripheral (Read=MEMORY/Write=PERIPHERY) |
    // memory_to_memory (обе MEMORY, request не читается — см.
    // dma_reverse_mapper.h) ----
    {
        const RawFieldAssignment* read_mode_assignment = lastAssignment(block, "ChannelInit.ReadMode");
        const RawFieldAssignment* write_mode_assignment = lastAssignment(block, "ChannelInit.WriteMode");

        const std::optional<std::string> read_mode =
            read_mode_assignment != nullptr ? parseMode(read_mode_assignment->raw_value_text) : std::nullopt;
        const std::optional<std::string> write_mode =
            write_mode_assignment != nullptr ? parseMode(write_mode_assignment->raw_value_text) : std::nullopt;

        if (!read_mode.has_value() || !write_mode.has_value())
        {
            result.json_block["direction"] = "memory_to_memory";
            result.notes.push_back(
                "ChannelInit.ReadMode/WriteMode не найдены или не распознаны — конфигуратор "
                "не смог определить направление передачи ПДП, укажите direction явно.");
            result.provenance.push_back(makeUnresolvedProvenance("direction"));
        }
        else if (*read_mode == "memory" && *write_mode == "memory")
        {
            result.json_block["direction"] = "memory_to_memory";
            result.provenance.push_back(makeProvenance("direction", *write_mode_assignment, false));
        }
        else if (*read_mode == "periphery" && *write_mode == "memory")
        {
            result.json_block["direction"] = "peripheral_to_memory";
            result.provenance.push_back(makeProvenance("direction", *read_mode_assignment, false));

            const std::optional<std::string> request_value =
                parseRequest(lastAssignment(block, "ChannelInit.ReadRequest") != nullptr
                                  ? lastAssignment(block, "ChannelInit.ReadRequest")->raw_value_text
                                  : "");
            if (request_value.has_value())
            {
                result.json_block["request"] = *request_value;
                result.provenance.push_back(
                    makeProvenance("request", *lastAssignment(block, "ChannelInit.ReadRequest"), false));
            }
            else
            {
                result.json_block["request"] = "crypto";
                result.notes.push_back(
                    "ChannelInit.ReadRequest не распознан для direction=peripheral_to_memory — "
                    "конфигуратор не смог определить источник запроса ПДП, укажите request "
                    "явно.");
                result.provenance.push_back(makeUnresolvedProvenance("request"));
            }
        }
        else if (*read_mode == "memory" && *write_mode == "periphery")
        {
            result.json_block["direction"] = "memory_to_peripheral";
            result.provenance.push_back(makeProvenance("direction", *write_mode_assignment, false));

            const std::optional<std::string> request_value =
                parseRequest(lastAssignment(block, "ChannelInit.WriteRequest") != nullptr
                                  ? lastAssignment(block, "ChannelInit.WriteRequest")->raw_value_text
                                  : "");
            if (request_value.has_value())
            {
                result.json_block["request"] = *request_value;
                result.provenance.push_back(
                    makeProvenance("request", *lastAssignment(block, "ChannelInit.WriteRequest"), false));
            }
            else
            {
                result.json_block["request"] = "crypto";
                result.notes.push_back(
                    "ChannelInit.WriteRequest не распознан для direction=memory_to_peripheral — "
                    "конфигуратор не смог определить источник запроса ПДП, укажите request "
                    "явно.");
                result.provenance.push_back(makeUnresolvedProvenance("request"));
            }
        }
        else
        {
            // PERIPHERY+PERIPHERY — HAL такого не допускает, но если встретилось,
            // не угадываем.
            result.json_block["direction"] = "memory_to_memory";
            result.notes.push_back(
                "ChannelInit.ReadMode=PERIPHERY и WriteMode=PERIPHERY одновременно — "
                "конфигуратор не смог определить направление передачи ПДП, укажите "
                "direction явно.");
            result.provenance.push_back(makeUnresolvedProvenance("direction"));
        }
    }

    // ---- buffer_size_bytes — значение передаётся как аргумент Len в
    // HAL_DMA_Start(...), а не присваиванием поля. Узкий паттерн (см.
    // reverse_import_scanner.h resolved_buffer_size_bytes): прямой вызов
    // HAL_DMA_Start(&<этот_канал>, ..., sizeof(<массив>) [+-N] | литерал)
    // В ТОМ ЖЕ файле — разобран сканером заранее. Межфункциональная связка
    // (значение приходит через ДРУГОЙ хендл, например I2C DMA-обёртки
    // HAL_I2C_Master_Transmit_DMA) не поддерживается — остаётся unresolved. ----
    if (block.resolved_buffer_size_bytes.has_value())
    {
        result.json_block["buffer_size_bytes"] = *block.resolved_buffer_size_bytes;
        FieldProvenance provenance;
        provenance.json_field = "buffer_size_bytes";
        provenance.location = block.resolved_buffer_size_bytes_location;
        provenance.original_raw_value_text = block.resolved_buffer_size_bytes_source_text;
        provenance.original_full_line_text = block.resolved_buffer_size_bytes_source_text;
        provenance.editable = false;
        result.provenance.push_back(provenance);
    }
    else
    {
        result.json_block["buffer_size_bytes"] = 1;
        result.notes.push_back(
            "Размер буфера передаётся аргументом HAL_DMA_Start(...), а не присваиванием "
            "поля — конфигуратор не читает тело функции (кроме узкого случая прямого "
            "sizeof(массив) в вызове HAL_DMA_Start с этим же каналом), укажите "
            "buffer_size_bytes явно.");
        result.provenance.push_back(makeUnresolvedProvenance("buffer_size_bytes"));
    }

    // ---- irq_enable (из вызова "HAL_DMA_LocalIRQEnable(&<handle>, ...);"
    // после присваиваний — опциональное поле, schemas/dma.json default false,
    // отсутствие вызова = документированный дефолт, не unresolved) ----
    result.json_block["irq_enable"] = hasLocalIrqEnableCall(block);

    return result;
}

std::optional<std::string> serializeDmaFieldValue(
    const std::string& field_name,
    const nlohmann::json& new_value,
    const std::string& /*original_raw_value_text*/)
{
    if (field_name == "instance")
    {
        if (!new_value.is_number_integer()) return std::nullopt;
        const int instance = new_value.get<int>();
        if (instance < 0 || instance > 3) return std::nullopt;
        return "DMA_CHANNEL_" + std::to_string(instance);
    }
    if (field_name == "priority")
    {
        if (!new_value.is_string()) return std::nullopt;
        const std::string v = new_value.get<std::string>();
        if (v == "low") return "DMA_CHANNEL_PRIORITY_LOW";
        if (v == "medium") return "DMA_CHANNEL_PRIORITY_MEDIUM";
        if (v == "high") return "DMA_CHANNEL_PRIORITY_HIGH";
        if (v == "very_high") return "DMA_CHANNEL_PRIORITY_VERY_HIGH";
        return std::nullopt;
    }
    if (field_name == "element_size")
    {
        if (!new_value.is_string()) return std::nullopt;
        const std::string v = new_value.get<std::string>();
        if (v == "byte") return "DMA_CHANNEL_SIZE_BYTE";
        if (v == "halfword") return "DMA_CHANNEL_SIZE_HALFWORD";
        if (v == "word") return "DMA_CHANNEL_SIZE_WORD";
        return std::nullopt;
    }
    // direction/request/buffer_size_bytes/irq_enable — см. комментарий в .h.
    return std::nullopt;
}

} // namespace reverse_import
