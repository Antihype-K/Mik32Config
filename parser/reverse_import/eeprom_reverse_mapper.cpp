//
// mapEepromBlock — см. eeprom_reverse_mapper.h за контрактом. Разбор основан
// на docs/examples/mik32-examples/HAL_EEPROM/src/main.c (EEPROM_Init,
// строки 237-246): heeprom.Mode = HAL_EEPROM_MODE_TWO_STAGE;
// .ErrorCorrection = HAL_EEPROM_ECC_ENABLE; .EnableInterrupt =
// HAL_EEPROM_SERR_DISABLE. Значения констант — mik32_hal_eeprom.h:30-44.
//
#include "eeprom_reverse_mapper.h"

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

// "HAL_EEPROM_MODE_TWO_STAGE" / "HAL_EEPROM_MODE_THREE_STAGE" -> schemas/eeprom.json "mode".
std::optional<std::string> parseMode(const std::string& text)
{
    const std::string trimmed = trimText(text);
    if (trimmed == "HAL_EEPROM_MODE_TWO_STAGE")
    {
        return std::string("two_stage");
    }
    if (trimmed == "HAL_EEPROM_MODE_THREE_STAGE")
    {
        return std::string("three_stage");
    }
    return std::nullopt;
}

// "HAL_EEPROM_ECC_ENABLE"=0b00 -> true, "HAL_EEPROM_ECC_DISABLE"=0b01 -> false.
// Имена HAL-констант инвертированы относительно обычного булева смысла
// (ENABLE=0), см. schemas/eeprom.json "_comment" у ecc_enabled.
std::optional<bool> parseEccEnabled(const std::string& text)
{
    const std::string trimmed = trimText(text);
    if (trimmed == "HAL_EEPROM_ECC_ENABLE")
    {
        return true;
    }
    if (trimmed == "HAL_EEPROM_ECC_DISABLE")
    {
        return false;
    }
    return std::nullopt;
}

// "HAL_EEPROM_SERR_ENABLE"=0b01 -> true, "HAL_EEPROM_SERR_DISABLE"=0b00 -> false.
std::optional<bool> parseSingleErrorInterruptEnabled(const std::string& text)
{
    const std::string trimmed = trimText(text);
    if (trimmed == "HAL_EEPROM_SERR_ENABLE")
    {
        return true;
    }
    if (trimmed == "HAL_EEPROM_SERR_DISABLE")
    {
        return false;
    }
    return std::nullopt;
}

} // namespace

std::optional<MappedBlock> mapEepromBlock(const RawInitBlock& block)
{
    if (block.handle_type != "HAL_EEPROM_HandleTypeDef")
    {
        return std::nullopt;
    }

    MappedBlock result;
    result.json_block["type"] = "eeprom";
    // EEPROM — глобальный блок без instance в schemas/eeprom.json, но GUI
    // (project_document.cpp: blockIndices/isEnabled/defaultBlock) сопоставляет
    // любой блок с узлом дерева по паре (type, instance) — тот же фикс, что
    // уже сделан для ADC/CRYPTO. Не HAL-поле, не editable, не unresolved.
    result.json_block["instance"] = 0;

    // ---- mode (обязательное, из "Mode = HAL_EEPROM_MODE_*;") ----
    {
        const RawFieldAssignment* assignment = lastAssignment(block, "Mode");
        if (assignment == nullptr)
        {
            result.json_block["mode"] = "two_stage";
            result.notes.push_back(
                "Mode не найден — конфигуратор не смог определить режим EEPROM, укажите "
                "mode явно.");
            result.provenance.push_back(makeUnresolvedProvenance("mode"));
        }
        else
        {
            const std::optional<std::string> value = parseMode(assignment->raw_value_text);
            if (value.has_value())
            {
                result.json_block["mode"] = *value;
                result.provenance.push_back(makeProvenance("mode", *assignment, false));
            }
            else
            {
                result.json_block["mode"] = "two_stage";
                result.notes.push_back(
                    "Mode=\"" + assignment->raw_value_text + "\" (" + assignment->location.fileName() +
                    ":" + std::to_string(assignment->location.line) +
                    ") — конфигуратор не смог определить режим EEPROM, укажите mode явно.");
                FieldProvenance provenance = makeUnresolvedProvenance("mode");
                provenance.location = assignment->location;
                provenance.original_raw_value_text = assignment->raw_value_text;
                provenance.original_full_line_text = assignment->full_line_text;
                result.provenance.push_back(provenance);
            }
        }
    }

    // ---- ecc_enabled (опциональное, дефолт true из schemas/eeprom.json —
    // документированное поведение HAL, отсутствие поля не unresolved) ----
    {
        const RawFieldAssignment* assignment = lastAssignment(block, "ErrorCorrection");
        if (assignment == nullptr)
        {
            result.json_block["ecc_enabled"] = true;
        }
        else
        {
            const std::optional<bool> value = parseEccEnabled(assignment->raw_value_text);
            if (value.has_value())
            {
                result.json_block["ecc_enabled"] = *value;
                result.provenance.push_back(makeProvenance("ecc_enabled", *assignment, false));
            }
            else
            {
                result.json_block["ecc_enabled"] = true;
                result.notes.push_back(
                    "ErrorCorrection=\"" + assignment->raw_value_text + "\" (" +
                    assignment->location.fileName() + ":" + std::to_string(assignment->location.line) +
                    ") — конфигуратор не смог определить коррекцию ошибок, укажите "
                    "ecc_enabled явно.");
                FieldProvenance provenance = makeUnresolvedProvenance("ecc_enabled");
                provenance.location = assignment->location;
                provenance.original_raw_value_text = assignment->raw_value_text;
                provenance.original_full_line_text = assignment->full_line_text;
                result.provenance.push_back(provenance);
            }
        }
    }

    // ---- single_error_interrupt_enabled (опциональное, дефолт false из
    // schemas/eeprom.json) ----
    {
        const RawFieldAssignment* assignment = lastAssignment(block, "EnableInterrupt");
        if (assignment == nullptr)
        {
            result.json_block["single_error_interrupt_enabled"] = false;
        }
        else
        {
            const std::optional<bool> value = parseSingleErrorInterruptEnabled(assignment->raw_value_text);
            if (value.has_value())
            {
                result.json_block["single_error_interrupt_enabled"] = *value;
                result.provenance.push_back(
                    makeProvenance("single_error_interrupt_enabled", *assignment, false));
            }
            else
            {
                result.json_block["single_error_interrupt_enabled"] = false;
                result.notes.push_back(
                    "EnableInterrupt=\"" + assignment->raw_value_text + "\" (" +
                    assignment->location.fileName() + ":" + std::to_string(assignment->location.line) +
                    ") — конфигуратор не смог определить прерывание одиночной ошибки, "
                    "укажите single_error_interrupt_enabled явно.");
                FieldProvenance provenance = makeUnresolvedProvenance("single_error_interrupt_enabled");
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
