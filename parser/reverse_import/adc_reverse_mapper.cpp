//
// mapAdcBlock — см. adc_reverse_mapper.h за контрактом. Разбор основан на
// docs/examples/mik32-examples/HAL_ADC/src/main.c (ADC_Init, строки ~79-86):
// hadc.Init.Sel = ADC_CHANNEL0; .EXTRef = ADC_EXTREF_OFF; .EXTClb =
// ADC_EXTCLB_ADCREF.
//
#include "adc_reverse_mapper.h"

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

// unresolved=true всегда — используется и когда assignment вовсе не
// найден (location остаётся пустой), и когда assignment был, но текст не
// распознан (location/original_raw_value_text заполняются ВЫЗЫВАЮЩИМ после
// получения этого объекта, см. ниже) — см. feedback_reverse_import_
// unresolved_fields_policy: ни один из этих случаев не получает угаданный
// дефолт без отметки.
FieldProvenance makeUnresolvedProvenance(const std::string& json_field)
{
    FieldProvenance provenance;
    provenance.json_field = json_field;
    provenance.editable = false;
    provenance.unresolved = true;
    return provenance;
}

// "ADC_CHANNELn" -> n (0-7). См. mik32_hal_adc.h: ADC_InitTypeDef.Sel.
std::optional<int> parseAdcChannel(const std::string& text)
{
    const std::string trimmed = trimText(text);
    const std::string prefix = "ADC_CHANNEL";
    if (trimmed.rfind(prefix, 0) != 0)
    {
        return std::nullopt;
    }
    const std::string digits = trimmed.substr(prefix.size());
    if (digits.empty() || digits.find_first_not_of("0123456789") != std::string::npos)
    {
        return std::nullopt;
    }
    const int value = std::stoi(digits);
    if (value < 0 || value > 7)
    {
        return std::nullopt;
    }
    return value;
}

std::optional<bool> parseExtRef(const std::string& text)
{
    const std::string trimmed = trimText(text);
    if (trimmed == "ADC_EXTREF_ON")
    {
        return true;
    }
    if (trimmed == "ADC_EXTREF_OFF")
    {
        return false;
    }
    return std::nullopt;
}

std::optional<bool> parseExtClb(const std::string& text)
{
    const std::string trimmed = trimText(text);
    if (trimmed == "ADC_EXTCLB_ADCREF")
    {
        return true;
    }
    if (trimmed == "ADC_EXTCLB_CLBREF")
    {
        return false;
    }
    return std::nullopt;
}

} // namespace

std::optional<std::string> serializeAdcFieldValue(
    const std::string& field_name,
    const nlohmann::json& new_value,
    const std::string& /*original_raw_value_text*/)
{
    if (field_name == "channel")
    {
        if (!new_value.is_number_integer()) return std::nullopt;
        const int value = new_value.get<int>();
        if (value < 0 || value > 7) return std::nullopt;
        return "ADC_CHANNEL" + std::to_string(value);
    }
    if (field_name == "ext_ref")
    {
        if (!new_value.is_boolean()) return std::nullopt;
        return new_value.get<bool>() ? "ADC_EXTREF_ON" : "ADC_EXTREF_OFF";
    }
    if (field_name == "ext_clb")
    {
        if (!new_value.is_boolean()) return std::nullopt;
        return new_value.get<bool>() ? "ADC_EXTCLB_ADCREF" : "ADC_EXTCLB_CLBREF";
    }
    return std::nullopt;
}

std::optional<MappedBlock> mapAdcBlock(const RawInitBlock& block)
{
    if (block.handle_type != "ADC_HandleTypeDef")
    {
        return std::nullopt;
    }

    MappedBlock result;
    result.json_block["type"] = "adc";
    // ADC в МК один физический блок, но GUI (project_document.cpp:
    // blockIndices/isEnabled) сопоставляет блок с узлом дерева по паре
    // (type, instance), сравнивая с дефолтом -1 — без явного instance=0
    // (как у GUI-шного defaultBlock() при ручном "Включить") дерево видит
    // блок как несуществующий, даже если он реально импортирован.
    // Не HAL-поле, не editable, не unresolved — синтетическая GUI-привязка.
    result.json_block["instance"] = 0;

    // ---- channel (из "Init.Sel = ADC_CHANNELn;") ----
    {
        const RawFieldAssignment* assignment = lastAssignment(block, "Init.Sel");
        if (assignment == nullptr)
        {
            result.json_block["channel"] = 0;
            result.notes.push_back(
                "Init.Sel не найден — конфигуратор не смог определить канал АЦП, "
                "укажите channel явно.");
            result.provenance.push_back(makeUnresolvedProvenance("channel"));
        }
        else
        {
            const std::optional<int> value = parseAdcChannel(assignment->raw_value_text);
            if (value.has_value())
            {
                result.json_block["channel"] = *value;
                result.provenance.push_back(makeProvenance("channel", *assignment, false));
            }
            else
            {
                result.json_block["channel"] = 0;
                result.notes.push_back(
                    "Init.Sel=\"" + assignment->raw_value_text + "\" (" + assignment->location.fileName() +
                    ":" + std::to_string(assignment->location.line) +
                    ") — конфигуратор не смог определить канал АЦП, укажите channel явно.");
                FieldProvenance provenance = makeUnresolvedProvenance("channel");
                provenance.location = assignment->location;
                provenance.original_raw_value_text = assignment->raw_value_text;
                provenance.original_full_line_text = assignment->full_line_text;
                result.provenance.push_back(provenance);
            }
        }
    }

    // ---- ext_ref (из "Init.EXTRef = ADC_EXTREF_ON/OFF;") ----
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
                    "Init.EXTRef=\"" + assignment->raw_value_text + "\" (" + assignment->location.fileName() +
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

    // ---- ext_clb (из "Init.EXTClb = ADC_EXTCLB_ADCREF/CLBREF;") ----
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
                    "Init.EXTClb=\"" + assignment->raw_value_text + "\" (" + assignment->location.fileName() +
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
