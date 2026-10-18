//
// mapWdtBlock — см. wdt_reverse_mapper.h за контрактом. Разбор основан на
// docs/examples/mik32-examples/HAL_WDT/src/main.c: hwdt.Init.Clock =
// HAL_WDT_OSC32K; hwdt.Init.ReloadMs = 1000; HAL_WDT_Init(&hwdt, ...).
//
#include "wdt_reverse_mapper.h"

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

// mik32_hal_wdt.h: HAL_WDT_Clocks.
std::optional<std::string> parseClockSource(const std::string& text)
{
    const std::string trimmed = trimText(text);
    if (trimmed == "HAL_WDT_OSC32M") return "osc32m";
    if (trimmed == "HAL_WDT_HSI32M") return "hsi32m";
    if (trimmed == "HAL_WDT_OSC32K") return "osc32k";
    if (trimmed == "HAL_WDT_LSI32K") return "lsi32k";
    return std::nullopt;
}

} // namespace

std::optional<MappedBlock> mapWdtBlock(const RawInitBlock& block)
{
    if (block.handle_type != "WDT_HandleTypeDef")
    {
        return std::nullopt;
    }

    MappedBlock result;
    result.json_block["type"] = "wdt";
    // WDT глобален (один блок на проект), но ProjectDocument::blockIndices()
    // ищет "wdt" по instance=0 (properties_panel.cpp:2718, peripheral_tree.cpp:54) —
    // без этого поля блок есть в peripherals[], но дерево/форма его не находят.
    result.json_block["instance"] = 0;

    // ---- clock_source (из "Init.Clock = HAL_WDT_*;") ----
    {
        const RawFieldAssignment* assignment = lastAssignment(block, "Init.Clock");
        if (assignment == nullptr)
        {
            result.json_block["clock_source"] = "osc32k";
            result.notes.push_back(
                "Init.Clock не найден — конфигуратор не смог определить источник тактирования "
                "сторожевого таймера, укажите clock_source явно.");
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
                result.json_block["clock_source"] = "osc32k";
                result.notes.push_back(
                    "Init.Clock=\"" + assignment->raw_value_text + "\" (" + assignment->location.file +
                    ":" + std::to_string(assignment->location.line) + ") — конфигуратор не смог "
                    "определить clock_source, укажите значение явно.");
                FieldProvenance provenance = makeUnresolvedProvenance("clock_source");
                provenance.location = assignment->location;
                provenance.original_raw_value_text = assignment->raw_value_text;
                provenance.original_full_line_text = assignment->full_line_text;
                result.provenance.push_back(provenance);
            }
        }
    }

    // ---- reload_ms (из "Init.ReloadMs = <литерал>;") ----
    {
        const RawFieldAssignment* assignment = lastAssignment(block, "Init.ReloadMs");
        if (assignment == nullptr)
        {
            result.json_block["reload_ms"] = 1000;
            result.notes.push_back(
                "Init.ReloadMs не найден — конфигуратор не смог определить время перезагрузки "
                "сторожевого таймера, укажите reload_ms явно.");
            result.provenance.push_back(makeUnresolvedProvenance("reload_ms"));
        }
        else
        {
            const std::optional<long long> value = parseCIntegerLiteral(assignment->raw_value_text);
            if (value.has_value())
            {
                result.json_block["reload_ms"] = static_cast<std::uint32_t>(*value);
                result.provenance.push_back(makeProvenance("reload_ms", *assignment, false));
            }
            else
            {
                result.json_block["reload_ms"] = 1000;
                result.notes.push_back(
                    "Init.ReloadMs=\"" + assignment->raw_value_text + "\" (" + assignment->location.file +
                    ":" + std::to_string(assignment->location.line) + ") — конфигуратор не смог "
                    "определить reload_ms, укажите значение явно.");
                FieldProvenance provenance = makeUnresolvedProvenance("reload_ms");
                provenance.location = assignment->location;
                provenance.original_raw_value_text = assignment->raw_value_text;
                provenance.original_full_line_text = assignment->full_line_text;
                result.provenance.push_back(provenance);
            }
        }
    }

    return result;
}

} // namespace reverse_import
