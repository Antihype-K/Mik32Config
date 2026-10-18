//
// mapTsensBlock — см. tsens_reverse_mapper.h за контрактом. Разбор основан
// на docs/examples/mik32-examples/HAL_TSENS/src/main.c и HAL_TSENS_IRQ/src/
// main.c (TSENS_Init, htsens.Clock/.Frequency, HAL_TSENS_Set{Low,Hi}
// Threshold, __HAL_TSENS_IRQ_ENABLE_*).
//
#include "tsens_reverse_mapper.h"

#include "c_literal_parser.h"

namespace reverse_import
{

namespace
{

std::string trimText(const std::string& text)
{
    const std::size_t begin = text.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) return "";
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

const RawTrailingCall* findTrailingCall(const RawInitBlock& block, const std::string& function_name)
{
    for (const RawTrailingCall& call : block.trailing_calls)
    {
        if (call.function_name == function_name)
        {
            return &call;
        }
    }
    return nullptr;
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

FieldProvenance makeUnresolvedProvenance(const std::string& json_field)
{
    FieldProvenance provenance;
    provenance.json_field = json_field;
    provenance.editable = false;
    provenance.unresolved = true;
    return provenance;
}

// mik32_hal_tsens.h: HAL_TSENS_ClockTypeDef.
std::optional<std::string> parseClockSource(const std::string& text)
{
    const std::string trimmed = trimText(text);
    if (trimmed == "HAL_TSENS_SYS_CLK") return "sys_clk";
    if (trimmed == "HAL_TSENS_HCLK") return "hclk";
    if (trimmed == "HAL_TSENS_OSC32M") return "osc32m";
    if (trimmed == "HAL_TSENS_HSI32M") return "hsi32m";
    if (trimmed == "HAL_TSENS_OSC32K") return "osc32k";
    if (trimmed == "HAL_TSENS_LSI32K") return "lsi32k";
    return std::nullopt;
}

} // namespace

std::optional<MappedBlock> mapTsensBlock(const RawInitBlock& block)
{
    if (block.handle_type != "TSENS_HandleTypeDef")
    {
        return std::nullopt;
    }

    MappedBlock result;
    result.json_block["type"] = "tsens";

    // ---- clock_source (из "Clock = HAL_TSENS_*;") ----
    {
        const RawFieldAssignment* assignment = lastAssignment(block, "Clock");
        if (assignment == nullptr)
        {
            result.json_block["clock_source"] = "sys_clk";
            result.notes.push_back(
                "Clock не найден — конфигуратор не смог определить источник тактирования "
                "датчика температуры, укажите clock_source явно.");
            result.provenance.push_back(makeUnresolvedProvenance("clock_source"));
        }
        else
        {
            const std::optional<std::string> value = parseClockSource(assignment->raw_value_text);
            if (value.has_value())
            {
                result.json_block["clock_source"] = *value;
                result.provenance.push_back(makeProvenance("clock_source", *assignment, false));
            }
            else
            {
                result.json_block["clock_source"] = "sys_clk";
                result.notes.push_back(
                    "Clock=\"" + assignment->raw_value_text + "\" (" + assignment->location.file + ":" +
                    std::to_string(assignment->location.line) + ") — конфигуратор не смог определить "
                    "clock_source, укажите значение явно.");
                FieldProvenance provenance = makeUnresolvedProvenance("clock_source");
                provenance.location = assignment->location;
                provenance.original_raw_value_text = assignment->raw_value_text;
                provenance.original_full_line_text = assignment->full_line_text;
                result.provenance.push_back(provenance);
            }
        }
    }

    // ---- frequency_hz (из "Frequency = <литерал>;") ----
    {
        const RawFieldAssignment* assignment = lastAssignment(block, "Frequency");
        if (assignment == nullptr)
        {
            result.json_block["frequency_hz"] = 1;
            result.notes.push_back(
                "Frequency не найден — конфигуратор не смог определить частоту датчика "
                "температуры, укажите frequency_hz явно.");
            result.provenance.push_back(makeUnresolvedProvenance("frequency_hz"));
        }
        else
        {
            const std::optional<long long> value = parseCIntegerLiteral(assignment->raw_value_text);
            if (value.has_value())
            {
                result.json_block["frequency_hz"] = static_cast<std::uint32_t>(*value);
                result.provenance.push_back(makeProvenance("frequency_hz", *assignment, false));
            }
            else
            {
                result.json_block["frequency_hz"] = 1;
                result.notes.push_back(
                    "Frequency=\"" + assignment->raw_value_text + "\" (" + assignment->location.file + ":" +
                    std::to_string(assignment->location.line) + ") — конфигуратор не смог определить "
                    "frequency_hz, укажите значение явно.");
                FieldProvenance provenance = makeUnresolvedProvenance("frequency_hz");
                provenance.location = assignment->location;
                provenance.original_raw_value_text = assignment->raw_value_text;
                provenance.original_full_line_text = assignment->full_line_text;
                result.provenance.push_back(provenance);
            }
        }
    }

    // ---- low/high_threshold_celsius (необязательные, из trailing_calls) ----
    for (const auto& [function_name, json_field] :
         {std::pair{"HAL_TSENS_SetLowThreshold", "low_threshold_celsius"},
          std::pair{"HAL_TSENS_SetHiThreshold", "high_threshold_celsius"}})
    {
        const RawTrailingCall* call = findTrailingCall(block, function_name);
        if (call == nullptr || call->args.size() < 2)
        {
            continue;
        }
        const std::optional<long long> value = parseCIntegerLiteral(call->args[1]);
        if (value.has_value())
        {
            result.json_block[json_field] = static_cast<int>(*value);
        }
        else
        {
            result.notes.push_back(
                std::string(function_name) + "(" + call->args[1] + ") (" + call->location.file + ":" +
                std::to_string(call->location.line) + ") — конфигуратор не смог определить " + json_field +
                ", укажите значение явно.");
            result.json_block[json_field] = 0;
            result.provenance.push_back(makeUnresolvedProvenance(json_field));
        }
    }

    // ---- interrupts (необязательный, из __HAL_TSENS_IRQ_ENABLE_*) ----
    {
        nlohmann::json interrupts = nlohmann::json::array();
        if (findTrailingCall(block, "__HAL_TSENS_IRQ_ENABLE_LOWIRQ") != nullptr) interrupts.push_back("low");
        if (findTrailingCall(block, "__HAL_TSENS_IRQ_ENABLE_HIIRQ") != nullptr) interrupts.push_back("high");
        if (findTrailingCall(block, "__HAL_TSENS_IRQ_ENABLE_EOC") != nullptr) interrupts.push_back("eoc");
        if (!interrupts.empty())
        {
            result.json_block["interrupts"] = interrupts;
        }
    }

    return result;
}

} // namespace reverse_import
