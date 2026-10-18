//
// mapCrcBlock — см. crc_reverse_mapper.h за контрактом. Разбор основан на
// docs/examples/mik32-examples/HAL_CRC32/src/main.c (CRC_Init, строки
// ~75-83): hcrc.Poly/.Init/.InputReverse/.OutputReverse/.OutputInversion.
//
#include "crc_reverse_mapper.h"

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

// mik32_hal_crc32.h: CRC_REFIN_FALSE/CRC_REFIN_TRUE, CRC_REFOUT_FALSE/
// CRC_REFOUT_TRUE, CRC_OUTPUTINVERSION_OFF/ON — см. комментарии в
// schemas/crc.json: HAL документирует только эти две пары значений на поле.
std::optional<bool> parseBoolMacro(
    const std::string& text, const std::string& false_macro, const std::string& true_macro)
{
    const std::string trimmed = trimText(text);
    if (trimmed == false_macro) return false;
    if (trimmed == true_macro) return true;
    return std::nullopt;
}

void mapIntField(
    MappedBlock& mapped, const RawInitBlock& block, const std::string& struct_field, const std::string& json_field)
{
    const RawFieldAssignment* assignment = lastAssignment(block, struct_field);
    if (assignment == nullptr)
    {
        mapped.json_block[json_field] = 0;
        mapped.notes.push_back(
            struct_field + " не найден — конфигуратор не смог определить " + json_field +
            ", укажите значение явно.");
        mapped.provenance.push_back(makeUnresolvedProvenance(json_field));
        return;
    }

    const std::optional<long long> value = parseCIntegerLiteral(assignment->raw_value_text);
    if (value.has_value())
    {
        mapped.json_block[json_field] = static_cast<std::uint32_t>(*value);
        mapped.provenance.push_back(makeProvenance(json_field, *assignment, false));
    }
    else
    {
        mapped.json_block[json_field] = 0;
        mapped.notes.push_back(
            struct_field + "=\"" + assignment->raw_value_text + "\" (" + assignment->location.file +
            ":" + std::to_string(assignment->location.line) + ") — конфигуратор не смог определить " +
            json_field + ", укажите значение явно.");
        FieldProvenance provenance = makeUnresolvedProvenance(json_field);
        provenance.location = assignment->location;
        provenance.original_raw_value_text = assignment->raw_value_text;
        provenance.original_full_line_text = assignment->full_line_text;
        mapped.provenance.push_back(provenance);
    }
}

void mapBoolField(
    MappedBlock& mapped, const RawInitBlock& block, const std::string& struct_field,
    const std::string& json_field, const std::string& false_macro, const std::string& true_macro)
{
    const RawFieldAssignment* assignment = lastAssignment(block, struct_field);
    if (assignment == nullptr)
    {
        mapped.json_block[json_field] = false;
        mapped.notes.push_back(
            struct_field + " не найден — конфигуратор не смог определить " + json_field +
            ", укажите значение явно.");
        mapped.provenance.push_back(makeUnresolvedProvenance(json_field));
        return;
    }

    const std::optional<bool> value = parseBoolMacro(assignment->raw_value_text, false_macro, true_macro);
    if (value.has_value())
    {
        mapped.json_block[json_field] = *value;
        mapped.provenance.push_back(makeProvenance(json_field, *assignment, false));
    }
    else
    {
        mapped.json_block[json_field] = false;
        mapped.notes.push_back(
            struct_field + "=\"" + assignment->raw_value_text + "\" (" + assignment->location.file +
            ":" + std::to_string(assignment->location.line) + ") — конфигуратор не смог определить " +
            json_field + ", укажите значение явно.");
        FieldProvenance provenance = makeUnresolvedProvenance(json_field);
        provenance.location = assignment->location;
        provenance.original_raw_value_text = assignment->raw_value_text;
        provenance.original_full_line_text = assignment->full_line_text;
        mapped.provenance.push_back(provenance);
    }
}

} // namespace

std::optional<MappedBlock> mapCrcBlock(const RawInitBlock& block)
{
    if (block.handle_type != "CRC_HandleTypeDef")
    {
        return std::nullopt;
    }

    MappedBlock result;
    result.json_block["type"] = "crc";

    mapIntField(result, block, "Poly", "poly");
    mapIntField(result, block, "Init", "init");
    mapBoolField(result, block, "InputReverse", "input_reverse", "CRC_REFIN_FALSE", "CRC_REFIN_TRUE");
    mapBoolField(result, block, "OutputReverse", "output_reverse", "CRC_REFOUT_FALSE", "CRC_REFOUT_TRUE");
    mapBoolField(
        result, block, "OutputInversion", "output_inversion",
        "CRC_OUTPUTINVERSION_OFF", "CRC_OUTPUTINVERSION_ON");

    return result;
}

} // namespace reverse_import
