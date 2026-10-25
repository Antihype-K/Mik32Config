//
// mapSpifiBlock — см. spifi_reverse_mapper.h за контрактом. Разбор основан на
// mik32_hal_spifi.c:19-39 (HAL_SPIFI_MemoryMode_Init) и mik32_hal_spifi.h —
// структурные поля SPIFI_MemoryModeConfig_HandleTypeDef/
// SPIFI_MemoryCommandTypeDef, та же форма присваиваний, что генерирует
// codegen/project/spifi_contribution.cpp.
//
#include "spifi_reverse_mapper.h"

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

void markUnresolved(MappedBlock& mapped, const std::string& struct_field, const std::string& json_field,
                     const nlohmann::json& default_value, const RawFieldAssignment* assignment)
{
    mapped.json_block[json_field] = default_value;
    if (assignment == nullptr)
    {
        mapped.notes.push_back(
            struct_field + " не найден — конфигуратор не смог определить " + json_field +
            ", укажите значение явно.");
        mapped.provenance.push_back(makeUnresolvedProvenance(json_field));
        return;
    }
    mapped.notes.push_back(
        struct_field + "=\"" + assignment->raw_value_text + "\" (" + assignment->location.fileName() +
        ":" + std::to_string(assignment->location.line) + ") — конфигуратор не смог определить " +
        json_field + ", укажите значение явно.");
    FieldProvenance provenance = makeUnresolvedProvenance(json_field);
    provenance.location = assignment->location;
    provenance.original_raw_value_text = assignment->raw_value_text;
    provenance.original_full_line_text = assignment->full_line_text;
    mapped.provenance.push_back(provenance);
}

void mapIntField(
    MappedBlock& mapped, const RawInitBlock& block, const std::string& struct_field, const std::string& json_field)
{
    const RawFieldAssignment* assignment = lastAssignment(block, struct_field);
    if (assignment == nullptr)
    {
        markUnresolved(mapped, struct_field, json_field, 0, nullptr);
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
        markUnresolved(mapped, struct_field, json_field, 0, assignment);
    }
}

std::optional<bool> parseBoolMacro(
    const std::string& text, const std::string& false_macro, const std::string& true_macro)
{
    const std::string trimmed = trimText(text);
    if (trimmed == false_macro) return false;
    if (trimmed == true_macro) return true;
    return std::nullopt;
}

void mapBoolField(
    MappedBlock& mapped, const RawInitBlock& block, const std::string& struct_field,
    const std::string& json_field, const std::string& false_macro, const std::string& true_macro)
{
    const RawFieldAssignment* assignment = lastAssignment(block, struct_field);
    if (assignment == nullptr)
    {
        markUnresolved(mapped, struct_field, json_field, false, nullptr);
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
        markUnresolved(mapped, struct_field, json_field, false, assignment);
    }
}

// Строковый enum (cmd_frame_form/cmd_field_form) — значение присвоено одним
// из документированных макросов HAL (mik32_hal_spifi.h), сопоставление по
// точному текстовому совпадению, как у mapBoolField, просто больше двух
// вариантов.
void mapEnumField(
    MappedBlock& mapped, const RawInitBlock& block, const std::string& struct_field,
    const std::string& json_field, const std::vector<std::pair<std::string, std::string>>& macro_to_json)
{
    const RawFieldAssignment* assignment = lastAssignment(block, struct_field);
    if (assignment == nullptr)
    {
        markUnresolved(mapped, struct_field, json_field, "", nullptr);
        return;
    }

    const std::string trimmed = trimText(assignment->raw_value_text);
    for (const auto& [macro, json_value] : macro_to_json)
    {
        if (trimmed == macro)
        {
            mapped.json_block[json_field] = json_value;
            mapped.provenance.push_back(makeProvenance(json_field, *assignment, false));
            return;
        }
    }
    markUnresolved(mapped, struct_field, json_field, "", assignment);
}

} // namespace

std::optional<MappedBlock> mapSpifiBlock(const RawInitBlock& block)
{
    if (block.handle_type != "SPIFI_MemoryModeConfig_HandleTypeDef")
    {
        return std::nullopt;
    }

    MappedBlock result;
    result.json_block["type"] = "spifi";

    mapBoolField(result, block, "CacheEnable", "cache_enabled", "SPIFI_CACHE_DISABLE", "SPIFI_CACHE_ENABLE");
    mapIntField(result, block, "CacheLimit", "cache_limit");
    mapIntField(result, block, "Command.InterimLength", "cmd_interim_length");
    mapIntField(result, block, "Command.OpCode", "cmd_op_code");

    // mik32_hal_spifi.h:55-61.
    mapEnumField(result, block, "Command.FieldForm", "cmd_field_form", {
        {"SPIFI_FIELDFORM_ALL_SERIAL", "all_serial"},
        {"SPIFI_FIELDFORM_DATA_PARALLEL", "data_parallel"},
        {"SPIFI_FIELDFORM_OPCODE_SERIAL", "opcode_serial"},
        {"SPIFI_FIELDFORM_ALL_PARALLEL", "all_parallel"},
    });

    // mik32_hal_spifi.h:63-72.
    mapEnumField(result, block, "Command.FrameForm", "cmd_frame_form", {
        {"SPIFI_FRAMEFORM_OPCODE", "opcode"},
        {"SPIFI_FRAMEFORM_OPCODE_1ADDR", "opcode_1addr"},
        {"SPIFI_FRAMEFORM_OPCODE_2ADDR", "opcode_2addr"},
        {"SPIFI_FRAMEFORM_OPCODE_3ADDR", "opcode_3addr"},
        {"SPIFI_FRAMEFORM_OPCODE_4ADDR", "opcode_4addr"},
        {"SPIFI_FRAMEFORM_3ADDR", "addr_3"},
        {"SPIFI_FRAMEFORM_4ADDR", "addr_4"},
    });

    return result;
}

std::optional<std::string> serializeSpifiFieldValue(
    const std::string& field_name,
    const nlohmann::json& new_value,
    const std::string& /*original_raw_value_text*/)
{
    if (field_name == "cache_enabled")
    {
        if (!new_value.is_boolean()) return std::nullopt;
        return new_value.get<bool>() ? "SPIFI_CACHE_ENABLE" : "SPIFI_CACHE_DISABLE";
    }
    if (field_name == "cache_limit" || field_name == "cmd_interim_length" || field_name == "cmd_op_code")
    {
        if (!new_value.is_number_integer()) return std::nullopt;
        return std::to_string(new_value.get<long long>());
    }
    if (field_name == "cmd_field_form")
    {
        if (!new_value.is_string()) return std::nullopt;
        const std::string v = new_value.get<std::string>();
        if (v == "all_serial") return "SPIFI_FIELDFORM_ALL_SERIAL";
        if (v == "data_parallel") return "SPIFI_FIELDFORM_DATA_PARALLEL";
        if (v == "opcode_serial") return "SPIFI_FIELDFORM_OPCODE_SERIAL";
        if (v == "all_parallel") return "SPIFI_FIELDFORM_ALL_PARALLEL";
        return std::nullopt;
    }
    if (field_name == "cmd_frame_form")
    {
        if (!new_value.is_string()) return std::nullopt;
        const std::string v = new_value.get<std::string>();
        if (v == "opcode") return "SPIFI_FRAMEFORM_OPCODE";
        if (v == "opcode_1addr") return "SPIFI_FRAMEFORM_OPCODE_1ADDR";
        if (v == "opcode_2addr") return "SPIFI_FRAMEFORM_OPCODE_2ADDR";
        if (v == "opcode_3addr") return "SPIFI_FRAMEFORM_OPCODE_3ADDR";
        if (v == "opcode_4addr") return "SPIFI_FRAMEFORM_OPCODE_4ADDR";
        if (v == "addr_3") return "SPIFI_FRAMEFORM_3ADDR";
        if (v == "addr_4") return "SPIFI_FRAMEFORM_4ADDR";
        return std::nullopt;
    }
    return std::nullopt;
}

} // namespace reverse_import
