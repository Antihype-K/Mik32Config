#include "timer32_channel_reverse_mapper.h"

#include "c_literal_parser.h"

#include <algorithm>
#include <regex>

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

std::optional<long long> tryParseIntLiteral(const std::string& raw_value_text)
{
    // Раньше принимались только десятичные литералы (regex ^-?[0-9]+$) —
    // "0x1000" уходил в nullopt как "не число". Общий разборщик понимает и
    // hex/oct/bin с C-суффиксом, контракт (nullopt на выражение) тот же.
    return parseCIntegerLiteral(raw_value_text);
}

// "htimer32_1.Instance" -> "htimer32_1". Единственная форма, которую пишет
// и собственный кодоген (не применимо — он не порождает отдельных хендлов
// канала), и mik32_fixed_v3 (main.cpp:555 и аналогичные) — присвоение поля
// TimerInstance значением "<родительский хендл>.Instance" текстом.
const std::regex kParentFromInstanceField(R"(^(\w+)\.Instance$)");

const std::regex kChannelIndexValue(R"(^TIMER32_CHANNEL_([0-3])$)");

std::optional<std::string> mapModeEnum(const std::string& raw)
{
    const std::string v = trim(raw);
    if (v == "TIMER32_CHANNEL_MODE_PWM") return "pwm";
    if (v == "TIMER32_CHANNEL_MODE_CAPTURE") return "capture";
    if (v == "TIMER32_CHANNEL_MODE_COMPARE") return "compare";
    return std::nullopt;
}

std::optional<bool> mapPwmInvertEnum(const std::string& raw)
{
    const std::string v = trim(raw);
    if (v == "TIMER32_CHANNEL_NON_INVERTED_PWM") return false;
    if (v == "TIMER32_CHANNEL_INVERTED_PWM") return true;
    return std::nullopt;
}

std::optional<std::string> mapCaptureEdgeEnum(const std::string& raw)
{
    const std::string v = trim(raw);
    if (v == "TIMER32_CHANNEL_CAPTUREEDGE_RISING") return "rising";
    if (v == "TIMER32_CHANNEL_CAPTUREEDGE_FALLING") return "falling";
    return std::nullopt;
}

std::optional<bool> mapNoiseFilterEnum(const std::string& raw)
{
    const std::string v = trim(raw);
    if (v == "TIMER32_CHANNEL_FILTER_OFF") return false;
    if (v == "TIMER32_CHANNEL_FILTER_ON") return true;
    return std::nullopt;
}

// feedback_reverse_import_unresolved_fields_policy: поле не найдено вовсе —
// вместо тихого дефолта подсвечиваем и блокируем кодоген.
FieldProvenance synthesizedProvenance(const std::string& json_field)
{
    FieldProvenance provenance;
    provenance.json_field = json_field;
    provenance.editable = false;
    provenance.unresolved = true;
    return provenance;
}

FieldProvenance fromAssignmentProvenance(const std::string& json_field, const RawFieldAssignment& assignment, bool editable)
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

} // namespace

std::optional<MappedTimer32Channel> mapTimer32ChannelBlock(const RawInitBlock& block)
{
    if (block.handle_type != "TIMER32_CHANNEL_HandleTypeDef")
    {
        return std::nullopt;
    }

    MappedTimer32Channel mapped;
    bool has_parent = false, has_index = false, has_mode = false;
    std::string mode = "pwm";

    for (const RawFieldAssignment& assignment : block.assignments)
    {
        if (assignment.field_path == "TimerInstance")
        {
            std::smatch match;
            const std::string value = trim(assignment.raw_value_text);
            if (std::regex_match(value, match, kParentFromInstanceField))
            {
                mapped.parent_handle_name = match[1].str();
                has_parent = true;
            }
            else
            {
                mapped.notes.push_back(
                    "TimerInstance канала \"" + block.handle_name + "\" = \"" +
                    assignment.raw_value_text + "\" не похоже на \"<родительский хендл>.Instance\" — "
                    "родитель не определён, канал не может быть привязан ни к одному Timer32");
            }
            continue;
        }

        if (assignment.field_path == "ChannelIndex")
        {
            std::smatch match;
            const std::string value = trim(assignment.raw_value_text);
            if (std::regex_match(value, match, kChannelIndexValue))
            {
                mapped.channel_json["index"] = std::stoi(match[1].str());
                mapped.provenance.push_back(fromAssignmentProvenance("index", assignment, true));
                has_index = true;
            }
            else
            {
                mapped.notes.push_back(
                    "не удалось распознать ChannelIndex = \"" + assignment.raw_value_text +
                    "\" у канала \"" + block.handle_name + "\"");
                mapped.provenance.push_back(fromAssignmentProvenance("index", assignment, false));
            }
            continue;
        }

        if (assignment.field_path == "Mode")
        {
            const std::optional<std::string> value = mapModeEnum(assignment.raw_value_text);
            if (value.has_value())
            {
                mode = *value;
                mapped.channel_json["mode"] = mode;
                mapped.provenance.push_back(fromAssignmentProvenance("mode", assignment, true));
                has_mode = true;
            }
            else
            {
                mapped.notes.push_back(
                    "не удалось распознать Mode = \"" + assignment.raw_value_text +
                    "\" у канала \"" + block.handle_name + "\" — конфигуратор не смог "
                    "определить mode, укажите значение явно");
                mapped.channel_json["mode"] = mode;
                mapped.provenance.push_back(fromAssignmentProvenance("mode", assignment, false));
                has_mode = true;
            }
            continue;
        }

        if (assignment.field_path == "OCR")
        {
            const std::optional<long long> value = tryParseIntLiteral(assignment.raw_value_text);
            if (value.has_value())
            {
                mapped.channel_json["ocr"] = static_cast<long long>(*value);
                mapped.provenance.push_back(fromAssignmentProvenance("ocr", assignment, true));
            }
            else
            {
                mapped.notes.push_back(
                    "поле OCR канала \"" + block.handle_name + "\" в исходнике — вычисляемое "
                    "выражение (\"" + assignment.raw_value_text + "\"), не число — конфигуратор не "
                    "смог определить ocr, укажите значение явно");
                mapped.channel_json["ocr"] = 0;
                mapped.provenance.push_back(fromAssignmentProvenance("ocr", assignment, false));
            }
            continue;
        }

        if (assignment.field_path == "PWM_Invert")
        {
            const std::optional<bool> value = mapPwmInvertEnum(assignment.raw_value_text);
            mapped.channel_json["pwm_invert"] = value.value_or(false);
            mapped.provenance.push_back(fromAssignmentProvenance("pwm_invert", assignment, value.has_value()));
            if (!value.has_value())
            {
                mapped.notes.push_back(
                    "не удалось распознать PWM_Invert = \"" + assignment.raw_value_text +
                    "\" у канала \"" + block.handle_name + "\" — конфигуратор не смог "
                    "определить pwm_invert, укажите значение явно");
            }
            continue;
        }

        if (assignment.field_path == "CaptureEdge")
        {
            const std::optional<std::string> value = mapCaptureEdgeEnum(assignment.raw_value_text);
            if (value.has_value())
            {
                mapped.channel_json["capture_edge"] = *value;
                mapped.provenance.push_back(fromAssignmentProvenance("capture_edge", assignment, true));
            }
            else
            {
                mapped.notes.push_back(
                    "не удалось распознать CaptureEdge = \"" + assignment.raw_value_text +
                    "\" у канала \"" + block.handle_name + "\" — конфигуратор не смог "
                    "определить capture_edge, укажите значение явно");
                mapped.channel_json["capture_edge"] = "rising";
                mapped.provenance.push_back(fromAssignmentProvenance("capture_edge", assignment, false));
            }
            continue;
        }

        if (assignment.field_path == "Noise")
        {
            const std::optional<bool> value = mapNoiseFilterEnum(assignment.raw_value_text);
            mapped.channel_json["noise_filter"] = value.value_or(false);
            mapped.provenance.push_back(fromAssignmentProvenance("noise_filter", assignment, value.has_value()));
            if (!value.has_value())
            {
                mapped.notes.push_back(
                    "не удалось распознать Noise = \"" + assignment.raw_value_text +
                    "\" у канала \"" + block.handle_name + "\" — конфигуратор не смог "
                    "определить noise_filter, укажите значение явно");
            }
            continue;
        }

        // Прочие поля TIMER32_CHANNEL_HandleTypeDef вне schemas/timer32.json
        // (если появятся) — молча игнорируются, как и у остальных мапперов.
    }

    if (!has_parent)
    {
        // ВАЖНО: не return std::nullopt здесь — nullopt из этой функции
        // означает ТОЛЬКО "block.handle_type не тот" (см. .h), а не "нет
        // родителя". Если бы here тоже возвращался nullopt, notes выше
        // (про нераспознанный TimerInstance) потерялись бы вместе с
        // остальным mapped — вызывающая сторона (reverse_import.cpp) сама
        // проверяет mapped_channel->parent_handle_name.empty().
        if (block.assignments.end() == std::find_if(
                block.assignments.begin(), block.assignments.end(),
                [](const RawFieldAssignment& a) { return a.field_path == "TimerInstance"; }))
        {
            mapped.notes.push_back(
                "у канала \"" + block.handle_name + "\" нет присваивания TimerInstance вовсе — "
                "родитель не определён, канал не может быть привязан ни к одному Timer32");
        }
    }
    if (!has_index)
    {
        mapped.channel_json["index"] = 0;
        mapped.provenance.push_back(synthesizedProvenance("index"));
        mapped.notes.push_back(
            "не найдено присваивание ChannelIndex у канала \"" + block.handle_name +
            "\" — конфигуратор не смог определить index, укажите значение явно");
    }
    if (!has_mode)
    {
        mapped.channel_json["mode"] = "pwm";
        mapped.provenance.push_back(synthesizedProvenance("mode"));
        mapped.notes.push_back(
            "не найдено присваивание Mode у канала \"" + block.handle_name +
            "\" — конфигуратор не смог определить mode, укажите значение явно");
    }

    // ocr требуется схемой только для pwm/compare — capture его не требует
    // (см. schemas/timer32.json). Не синтезируем ocr для capture, если его
    // не было в исходнике — честное отсутствие поля, а не 0-заглушка.
    if (!mapped.channel_json.contains("ocr") && mode != "capture")
    {
        mapped.channel_json["ocr"] = 0;
        mapped.provenance.push_back(synthesizedProvenance("ocr"));
        mapped.notes.push_back(
            "не найдено присваивание OCR у канала \"" + block.handle_name +
            "\" (mode=" + mode + ") — конфигуратор не смог определить ocr, укажите значение явно");
    }
    if (!mapped.channel_json.contains("capture_edge") && mode == "capture")
    {
        mapped.channel_json["capture_edge"] = "rising";
        mapped.provenance.push_back(synthesizedProvenance("capture_edge"));
        mapped.notes.push_back(
            "не найдено присваивание CaptureEdge у канала \"" + block.handle_name +
            "\" (mode=capture) — конфигуратор не смог определить capture_edge, "
            "укажите значение явно");
    }
    if (!mapped.channel_json.contains("pwm_invert"))
    {
        mapped.channel_json["pwm_invert"] = false;
        mapped.provenance.push_back(synthesizedProvenance("pwm_invert"));
    }
    if (!mapped.channel_json.contains("noise_filter"))
    {
        mapped.channel_json["noise_filter"] = false;
        mapped.provenance.push_back(synthesizedProvenance("noise_filter"));
    }

    return mapped;
}

std::optional<std::string> serializeTimer32ChannelFieldValue(
    const std::string& field_name,
    const nlohmann::json& new_value,
    const std::string& /*original_raw_value_text*/)
{
    if (field_name == "index")
    {
        if (!new_value.is_number_integer()) return std::nullopt;
        const int index = new_value.get<int>();
        if (index < 0 || index > 3) return std::nullopt;
        return "TIMER32_CHANNEL_" + std::to_string(index);
    }
    if (field_name == "mode")
    {
        if (!new_value.is_string()) return std::nullopt;
        const std::string v = new_value.get<std::string>();
        if (v == "pwm") return "TIMER32_CHANNEL_MODE_PWM";
        if (v == "capture") return "TIMER32_CHANNEL_MODE_CAPTURE";
        if (v == "compare") return "TIMER32_CHANNEL_MODE_COMPARE";
        return std::nullopt;
    }
    if (field_name == "ocr")
    {
        if (!new_value.is_number_integer()) return std::nullopt;
        const long long value = new_value.get<long long>();
        if (value < 0 || value > 4294967295LL) return std::nullopt;
        return std::to_string(value);
    }
    if (field_name == "pwm_invert")
    {
        if (!new_value.is_boolean()) return std::nullopt;
        return new_value.get<bool>() ? "TIMER32_CHANNEL_INVERTED_PWM" : "TIMER32_CHANNEL_NON_INVERTED_PWM";
    }
    if (field_name == "capture_edge")
    {
        if (!new_value.is_string()) return std::nullopt;
        const std::string v = new_value.get<std::string>();
        if (v == "rising") return "TIMER32_CHANNEL_CAPTUREEDGE_RISING";
        if (v == "falling") return "TIMER32_CHANNEL_CAPTUREEDGE_FALLING";
        return std::nullopt;
    }
    if (field_name == "noise_filter")
    {
        if (!new_value.is_boolean()) return std::nullopt;
        return new_value.get<bool>() ? "TIMER32_CHANNEL_FILTER_ON" : "TIMER32_CHANNEL_FILTER_OFF";
    }

    return std::nullopt;
}

} // namespace reverse_import
