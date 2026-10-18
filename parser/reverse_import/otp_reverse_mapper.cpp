//
// mapOtpBlock — см. otp_reverse_mapper.h за контрактом. Разбор основан на
// docs/examples/mik32-examples/HAL_OTP/src/main.c: hotp.ReadMode =
// OPT_READ_3STAGES; HAL_OTP_Init(&hotp);
//
#include "otp_reverse_mapper.h"

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

// mik32_hal_otp.h: OPT_READ_2STAGES (0) / OPT_READ_3STAGES (1).
std::optional<std::string> parseReadMode(const std::string& text)
{
    const std::string trimmed = trimText(text);
    if (trimmed == "OPT_READ_2STAGES") return "two_stages";
    if (trimmed == "OPT_READ_3STAGES") return "three_stages";
    return std::nullopt;
}

} // namespace

std::optional<MappedBlock> mapOtpBlock(const RawInitBlock& block)
{
    if (block.handle_type != "OTP_HandleTypeDef")
    {
        return std::nullopt;
    }

    MappedBlock result;
    result.json_block["type"] = "otp";

    const RawFieldAssignment* assignment = lastAssignment(block, "ReadMode");
    if (assignment == nullptr)
    {
        result.json_block["read_mode"] = "three_stages";
        result.notes.push_back(
            "ReadMode не найден — конфигуратор не смог определить режим чтения OTP, "
            "укажите read_mode явно.");
        result.provenance.push_back(makeUnresolvedProvenance("read_mode"));
        return result;
    }

    const std::optional<std::string> value = parseReadMode(assignment->raw_value_text);
    if (value.has_value())
    {
        result.json_block["read_mode"] = *value;
        result.provenance.push_back(makeProvenance("read_mode", *assignment, false));
    }
    else
    {
        result.json_block["read_mode"] = "three_stages";
        result.notes.push_back(
            "ReadMode=\"" + assignment->raw_value_text + "\" (" + assignment->location.file + ":" +
            std::to_string(assignment->location.line) + ") — конфигуратор не смог определить "
            "read_mode, укажите значение явно.");
        FieldProvenance provenance = makeUnresolvedProvenance("read_mode");
        provenance.location = assignment->location;
        provenance.original_raw_value_text = assignment->raw_value_text;
        provenance.original_full_line_text = assignment->full_line_text;
        result.provenance.push_back(provenance);
    }

    return result;
}

} // namespace reverse_import
