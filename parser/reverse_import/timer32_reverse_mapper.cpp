// ОГРАНИЧЕНИЕ (см. .h): TIMER32_CHANNEL_HandleTypeDef (ШИМ/захват/сравнение-
// каналы Timer32_1/Timer32_2) сознательно НЕ обрабатывается этим маппером —
// mapTimer32Block() возвращает nullopt для таких блоков, "channels" в
// JSON никогда не заполняется. Причина: канал ссылается на родительский
// таймер через указатель (TimerInstance = htimer32_1.Instance), а не через
// имя хендла — надёжного текстового способа сопоставить канал с одним из
// htimer32_0/1/2 нет без интерпретации присваивания как выражения, что
// сканер (см. reverse_import_scanner.h) сознательно не делает.

#include "timer32_reverse_mapper.h"

#include <cstdint>
#include <cstdio>
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

// Правило для ВСЕХ полей во всех reverse-мапперах (см. текст задания к
// фиче): raw_value_text может быть C-выражением/именем локальной переменной
// (пример — TIMER32.Clock.Prescaler = prescaler; в mik32_fixed_v3), а не
// только литералом — такие значения НЕ вычисляются, только литералы
// (decimal, 0x-hex) распознаются как числа.
std::optional<long long> tryParseIntLiteral(const std::string& raw_value_text)
{
    std::string text = trim(raw_value_text);
    while (!text.empty() && (text.back() == 'u' || text.back() == 'U' ||
                              text.back() == 'l' || text.back() == 'L'))
    {
        text.pop_back();
    }
    if (text.empty())
    {
        return std::nullopt;
    }

    static const std::regex kHex(R"(^-?0[xX][0-9a-fA-F]+$)");
    static const std::regex kDec(R"(^-?[0-9]+$)");

    try
    {
        if (std::regex_match(text, kHex))
        {
            return std::stoll(text, nullptr, 16);
        }
        if (std::regex_match(text, kDec))
        {
            return std::stoll(text, nullptr, 10);
        }
    }
    catch (const std::exception&)
    {
        return std::nullopt;
    }
    return std::nullopt;
}

bool looksLikeHex(const std::string& raw_value_text)
{
    const std::string text = trim(raw_value_text);
    return text.size() > 1 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X');
}

// TIMER32_0/1/2 — см. templates/mik32_sdk_snapshot/shared/include/mik32_memory_map.h:93-95
// (общий указательный макрос на базовый адрес периферии), не поле структуры.
const std::regex kTimer32InstanceValue(R"(^TIMER32_(\d)$)");

std::optional<std::string> mapCountModeEnum(const std::string& raw)
{
    const std::string v = trim(raw);
    if (v == "TIMER32_COUNTMODE_FORWARD") return "forward";
    if (v == "TIMER32_COUNTMODE_REVERSE") return "reverse";
    if (v == "TIMER32_COUNTMODE_BIDIRECTIONAL") return "bidirectional";
    return std::nullopt;
}

FieldProvenance synthesizedProvenance(const std::string& json_field)
{
    FieldProvenance provenance;
    provenance.json_field = json_field;
    provenance.editable = false;
    return provenance;
}

// Общая обработка "числового поля, которое в реальном коде может оказаться
// вычисляемым выражением, а не литералом" — Clock.Prescaler и Top оба этому
// правилу подчиняются (см. комментарий в задании к фиче и в .h).
void mapNumericField(
    MappedBlock& mapped,
    const RawFieldAssignment& assignment,
    const std::string& json_field,
    const std::string& init_function_hint)
{
    FieldProvenance provenance;
    provenance.json_field = json_field;
    provenance.location = assignment.location;
    provenance.original_raw_value_text = assignment.raw_value_text;
    provenance.original_full_line_text = assignment.full_line_text;

    std::optional<long long> value = tryParseIntLiteral(assignment.raw_value_text);
    if (value.has_value())
    {
        mapped.json_block[json_field] = static_cast<std::uint32_t>(*value);
        provenance.editable = true;
    }
    else
    {
        mapped.json_block[json_field] = 0;
        provenance.editable = false;
        mapped.notes.push_back(
            "поле " + assignment.field_path + " в исходнике — вычисляемое выражение (\"" +
            assignment.raw_value_text + "\"), не число — показано как 0, редактирование "
            "недоступно, значение нужно менять в аргументах вызова " + init_function_hint +
            " в исходнике руками");
    }
    mapped.provenance.push_back(provenance);
}

} // namespace

std::optional<MappedBlock> mapTimer32Block(const RawInitBlock& block)
{
    // Это же условие отсекает TIMER32_CHANNEL_HandleTypeDef — см. ограничение
    // в .h/наверху этого файла: канал заведён как отдельный тип хендла и
    // сюда никогда не попадает как "обычный" Timer32-блок.
    if (block.handle_type != "TIMER32_HandleTypeDef")
    {
        return std::nullopt;
    }

    MappedBlock mapped;
    mapped.json_block["type"] = "timer32";

    // Timer32_0_Init/Timer32_1_Init — общая подсказка в notes про "где
    // руками менять" для полей с вычисляемым значением; в реальном коде
    // это ИМЕННО те две функции (см. main.cpp mik32_fixed_v3), но маппер
    // не пытается угадать точное имя функции этого конкретного блока —
    // init_function_name у Timer32 всегда "HAL_Timer32_Init", а не
    // "Timer32_0_Init" (последнее — обёртка приложения, не HAL), поэтому
    // подсказка называет обе известные обёртки текстом, а не программно.
    static const std::string kInitHint = "Timer32_0_Init/Timer32_1_Init";

    bool has_instance = false, has_prescaler = false, has_count_mode = false, has_top = false;

    for (const RawFieldAssignment& assignment : block.assignments)
    {
        if (assignment.field_path == "Instance")
        {
            has_instance = true;
            std::smatch match;
            const std::string value = trim(assignment.raw_value_text);

            FieldProvenance provenance;
            provenance.json_field = "instance";
            provenance.location = assignment.location;
            provenance.original_raw_value_text = assignment.raw_value_text;
            provenance.original_full_line_text = assignment.full_line_text;

            if (std::regex_match(value, match, kTimer32InstanceValue))
            {
                mapped.json_block["instance"] = std::stoi(match[1].str());
                provenance.editable = true;
            }
            else
            {
                mapped.json_block["instance"] = 0;
                provenance.editable = false;
                mapped.notes.push_back(
                    "не удалось определить instance из Instance = \"" + assignment.raw_value_text +
                    "\" — подставлена заглушка 0, проверьте блок руками");
            }
            mapped.provenance.push_back(provenance);
            continue;
        }

        if (assignment.field_path == "Clock.Prescaler")
        {
            has_prescaler = true;
            mapNumericField(mapped, assignment, "prescaler", kInitHint);
            continue;
        }

        if (assignment.field_path == "Top")
        {
            has_top = true;
            mapNumericField(mapped, assignment, "top", kInitHint);
            continue;
        }

        if (assignment.field_path == "CountMode")
        {
            has_count_mode = true;
            std::optional<std::string> mode = mapCountModeEnum(assignment.raw_value_text);

            FieldProvenance provenance;
            provenance.json_field = "count_mode";
            provenance.location = assignment.location;
            provenance.original_raw_value_text = assignment.raw_value_text;
            provenance.original_full_line_text = assignment.full_line_text;

            if (mode.has_value())
            {
                mapped.json_block["count_mode"] = *mode;
                provenance.editable = true;
            }
            else
            {
                mapped.json_block["count_mode"] = "forward";
                provenance.editable = false;
                mapped.notes.push_back(
                    "не удалось распознать count_mode из \"" + assignment.raw_value_text +
                    "\" — подставлен дефолт \"forward\", проверьте блок руками");
            }
            mapped.provenance.push_back(provenance);
            continue;
        }

        // Clock.Source, State, InterruptMask и прочие поля TIMER32_HandleTypeDef
        // не входят в schemas/timer32.json — молча игнорируем, это не потеря
        // периферии, а поля, которые кодогенератор этого конфигуратора не
        // параметризует (см. schemas/timer32.json fields).
    }

    if (!has_instance)
    {
        mapped.json_block["instance"] = 0;
        mapped.provenance.push_back(synthesizedProvenance("instance"));
        mapped.notes.push_back(
            "в блоке не найдено присваивание Instance — подставлена заглушка instance=0, "
            "проверьте блок руками");
    }
    if (!has_prescaler)
    {
        mapped.json_block["prescaler"] = 0;
        mapped.provenance.push_back(synthesizedProvenance("prescaler"));
        mapped.notes.push_back(
            "в блоке не найдено присваивание Clock.Prescaler — подставлена заглушка "
            "prescaler=0, проверьте блок руками");
    }
    if (!has_count_mode)
    {
        mapped.json_block["count_mode"] = "forward";
        mapped.provenance.push_back(synthesizedProvenance("count_mode"));
        mapped.notes.push_back(
            "count_mode не задан явно в исходнике, применён дефолт \"forward\"");
    }
    if (!has_top)
    {
        mapped.json_block["top"] = 0;
        mapped.provenance.push_back(synthesizedProvenance("top"));
        mapped.notes.push_back(
            "в блоке не найдено присваивание Top — подставлена заглушка top=0, проверьте "
            "блок руками");
    }

    // "channels" сознательно не заполняется — см. ограничение в .h.
    return mapped;
}

std::optional<std::string> serializeTimer32FieldValue(
    const std::string& field_name,
    const nlohmann::json& new_value,
    const std::string& original_raw_value_text)
{
    if (field_name == "instance")
    {
        if (!new_value.is_number_integer()) return std::nullopt;
        const int instance = new_value.get<int>();
        if (instance < 0 || instance > 2) return std::nullopt;
        return "TIMER32_" + std::to_string(instance);
    }

    if (field_name == "prescaler" || field_name == "top")
    {
        if (!new_value.is_number_integer()) return std::nullopt;
        const long long value = new_value.get<long long>();
        if (value < 0 || value > 4294967295LL) return std::nullopt;

        if (looksLikeHex(original_raw_value_text))
        {
            char buffer[2 + 16 + 1];
            std::snprintf(buffer, sizeof(buffer), "0x%llX", static_cast<unsigned long long>(value));
            return std::string(buffer);
        }
        return std::to_string(value);
    }

    if (field_name == "count_mode")
    {
        if (!new_value.is_string()) return std::nullopt;
        const std::string v = new_value.get<std::string>();
        if (v == "forward") return "TIMER32_COUNTMODE_FORWARD";
        if (v == "reverse") return "TIMER32_COUNTMODE_REVERSE";
        if (v == "bidirectional") return "TIMER32_COUNTMODE_BIDIRECTIONAL";
        return std::nullopt;
    }

    // "channels" — не поддержано (см. ограничение выше), любое другое имя
    // поля тоже нераспознано.
    return std::nullopt;
}

} // namespace reverse_import
