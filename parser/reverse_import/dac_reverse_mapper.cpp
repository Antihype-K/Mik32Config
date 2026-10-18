//
// mapDacBlock — см. dac_reverse_mapper.h за контрактом. Разбор основан на
// docs/examples/mik32-examples/HAL_DAC/src/main.c (DAC_Init, строки ~54-67):
// hdac1.Instance_dac = HAL_DAC0; .Init.DIV = 31; .Init.EXTRef =
// DAC_EXTREF_OFF; .Init.EXTClb = DAC_EXTCLB_DACREF.
//
#include "dac_reverse_mapper.h"
#include "c_literal_parser.h"

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

// unresolved=true всегда — см. feedback_reverse_import_unresolved_fields_
// policy: ни "поле вовсе не найдено", ни "поле найдено, но текст не
// распознан" не получают угаданный дефолт без отметки.
FieldProvenance makeUnresolvedProvenance(const std::string& json_field)
{
    FieldProvenance provenance;
    provenance.json_field = json_field;
    provenance.editable = false;
    provenance.unresolved = true;
    return provenance;
}

// "HAL_DACn" -> n (0-1). См. mik32_hal_dac.h: DAC_InitTypeDef.Instance_dac.
std::optional<int> parseDacInstance(const std::string& text)
{
    const std::string trimmed = trimText(text);
    if (trimmed == "HAL_DAC0")
    {
        return 0;
    }
    if (trimmed == "HAL_DAC1")
    {
        return 1;
    }
    return std::nullopt;
}

// Целый литерал 0-255 (DIV — делитель, не enum-макрос, в отличие от ADC).
std::optional<int> parseDiv(const std::string& text)
{
    const std::optional<long long> value = parseCIntegerLiteral(text);
    if (!value.has_value() || *value < 0 || *value > 255)
    {
        return std::nullopt;
    }
    return static_cast<int>(*value);
}

std::optional<bool> parseExtRef(const std::string& text)
{
    const std::string trimmed = trimText(text);
    if (trimmed == "DAC_EXTREF_ON")
    {
        return true;
    }
    if (trimmed == "DAC_EXTREF_OFF")
    {
        return false;
    }
    return std::nullopt;
}

std::optional<bool> parseExtClb(const std::string& text)
{
    const std::string trimmed = trimText(text);
    if (trimmed == "DAC_EXTCLB_DACREF")
    {
        return true;
    }
    if (trimmed == "DAC_EXTCLB_CLBREF")
    {
        return false;
    }
    return std::nullopt;
}

} // namespace

std::optional<MappedBlock> mapDacBlock(const RawInitBlock& block)
{
    if (block.handle_type != "DAC_HandleTypeDef")
    {
        return std::nullopt;
    }

    MappedBlock result;
    result.json_block["type"] = "dac";

    // ---- instance (из "Instance_dac = HAL_DACn;") ----
    {
        const RawFieldAssignment* assignment = lastAssignment(block, "Instance_dac");
        if (assignment == nullptr)
        {
            result.json_block["instance"] = 0;
            result.notes.push_back(
                "Instance_dac не найден — конфигуратор не смог определить канал ЦАП, "
                "укажите instance явно.");
            result.provenance.push_back(makeUnresolvedProvenance("instance"));
        }
        else
        {
            const std::optional<int> value = parseDacInstance(assignment->raw_value_text);
            if (value.has_value())
            {
                result.json_block["instance"] = *value;
                result.provenance.push_back(makeProvenance("instance", *assignment, false));
            }
            else
            {
                result.json_block["instance"] = 0;
                result.notes.push_back(
                    "Instance_dac=\"" + assignment->raw_value_text + "\" (" + assignment->location.file +
                    ":" + std::to_string(assignment->location.line) +
                    ") — конфигуратор не смог определить канал ЦАП, укажите instance явно.");
                FieldProvenance provenance = makeUnresolvedProvenance("instance");
                provenance.location = assignment->location;
                provenance.original_raw_value_text = assignment->raw_value_text;
                provenance.original_full_line_text = assignment->full_line_text;
                result.provenance.push_back(provenance);
            }
        }
    }

    // ---- div (из "Init.DIV = <число>;") ----
    {
        const RawFieldAssignment* assignment = lastAssignment(block, "Init.DIV");
        if (assignment == nullptr)
        {
            result.json_block["div"] = 0;
            result.notes.push_back(
                "Init.DIV не найден — конфигуратор не смог определить делитель частоты ЦАП, "
                "укажите div явно.");
            result.provenance.push_back(makeUnresolvedProvenance("div"));
        }
        else
        {
            const std::optional<int> value = parseDiv(assignment->raw_value_text);
            if (value.has_value())
            {
                result.json_block["div"] = *value;
                result.provenance.push_back(makeProvenance("div", *assignment, false));
            }
            else
            {
                result.json_block["div"] = 0;
                result.notes.push_back(
                    "Init.DIV=\"" + assignment->raw_value_text + "\" (" + assignment->location.file +
                    ":" + std::to_string(assignment->location.line) +
                    ") — конфигуратор не смог определить делитель частоты ЦАП, укажите div явно.");
                FieldProvenance provenance = makeUnresolvedProvenance("div");
                provenance.location = assignment->location;
                provenance.original_raw_value_text = assignment->raw_value_text;
                provenance.original_full_line_text = assignment->full_line_text;
                result.provenance.push_back(provenance);
            }
        }
    }

    // ---- ext_ref (из "Init.EXTRef = DAC_EXTREF_ON/OFF;") ----
    {
        const RawFieldAssignment* assignment = lastAssignment(block, "Init.EXTRef");
        if (assignment == nullptr)
        {
            result.json_block["ext_ref"] = false;
            result.notes.push_back(
                "Init.EXTRef не найден — конфигуратор не смог определить источник опорного "
                "напряжения, укажите ext_ref явно.");
            result.provenance.push_back(makeUnresolvedProvenance("ext_ref"));
        }
        else
        {
            const std::optional<bool> value = parseExtRef(assignment->raw_value_text);
            if (value.has_value())
            {
                result.json_block["ext_ref"] = *value;
                result.provenance.push_back(makeProvenance("ext_ref", *assignment, false));
            }
            else
            {
                result.json_block["ext_ref"] = false;
                result.notes.push_back(
                    "Init.EXTRef=\"" + assignment->raw_value_text + "\" (" + assignment->location.file +
                    ":" + std::to_string(assignment->location.line) +
                    ") — конфигуратор не смог определить источник опорного напряжения, "
                    "укажите ext_ref явно.");
                FieldProvenance provenance = makeUnresolvedProvenance("ext_ref");
                provenance.location = assignment->location;
                provenance.original_raw_value_text = assignment->raw_value_text;
                provenance.original_full_line_text = assignment->full_line_text;
                result.provenance.push_back(provenance);
            }
        }
    }

    // ---- ext_clb (из "Init.EXTClb = DAC_EXTCLB_DACREF/CLBREF;") ----
    {
        const RawFieldAssignment* assignment = lastAssignment(block, "Init.EXTClb");
        if (assignment == nullptr)
        {
            result.json_block["ext_clb"] = false;
            result.notes.push_back(
                "Init.EXTClb не найден — конфигуратор не смог определить источник внешнего "
                "опорного напряжения, укажите ext_clb явно.");
            result.provenance.push_back(makeUnresolvedProvenance("ext_clb"));
        }
        else
        {
            const std::optional<bool> value = parseExtClb(assignment->raw_value_text);
            if (value.has_value())
            {
                result.json_block["ext_clb"] = *value;
                result.provenance.push_back(makeProvenance("ext_clb", *assignment, false));
            }
            else
            {
                result.json_block["ext_clb"] = false;
                result.notes.push_back(
                    "Init.EXTClb=\"" + assignment->raw_value_text + "\" (" + assignment->location.file +
                    ":" + std::to_string(assignment->location.line) +
                    ") — конфигуратор не смог определить источник внешнего опорного "
                    "напряжения, укажите ext_clb явно.");
                FieldProvenance provenance = makeUnresolvedProvenance("ext_clb");
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
