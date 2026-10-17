#include "timer16_reverse_mapper.h"

#include "c_literal_parser.h"

#include <optional>
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

FieldProvenance synthesizedProvenance(const std::string& json_field)
{
    FieldProvenance provenance;
    provenance.json_field = json_field;
    provenance.editable = false;
    return provenance;
}

// TIMER16_0/1/2 — базовый указатель периферии (см. аналогичный приём у
// timer32_reverse_mapper.cpp для TIMER32_<N>), не поле структуры.
const std::regex kInstanceValue(R"(^TIMER16_(\d)$)");

std::optional<int> mapPrescalerEnum(const std::string& raw)
{
    const std::string v = trim(raw);
    if (v == "TIMER16_PRESCALER_1") return 1;
    if (v == "TIMER16_PRESCALER_2") return 2;
    if (v == "TIMER16_PRESCALER_4") return 4;
    if (v == "TIMER16_PRESCALER_8") return 8;
    if (v == "TIMER16_PRESCALER_16") return 16;
    if (v == "TIMER16_PRESCALER_32") return 32;
    if (v == "TIMER16_PRESCALER_64") return 64;
    if (v == "TIMER16_PRESCALER_128") return 128;
    return std::nullopt;
}

std::optional<std::string> serializePrescalerEnum(int value)
{
    switch (value)
    {
        case 1: return "TIMER16_PRESCALER_1";
        case 2: return "TIMER16_PRESCALER_2";
        case 4: return "TIMER16_PRESCALER_4";
        case 8: return "TIMER16_PRESCALER_8";
        case 16: return "TIMER16_PRESCALER_16";
        case 32: return "TIMER16_PRESCALER_32";
        case 64: return "TIMER16_PRESCALER_64";
        case 128: return "TIMER16_PRESCALER_128";
        default: return std::nullopt;
    }
}

// "HAL_Timer16_StartPWM_IT" -> "HAL_Timer16_StartPWM" и т.п. — _IT-варианты
// (mik32_hal_timer16.h:315-317) настраивают то же mode/arr/cmp, разница
// только во включении прерывания, которое schemas/timer16.json не
// моделирует (см. её комментарий про Errata п.8.2) — поэтому обрабатываются
// как обычные, без отдельной ветки.
std::string stripInterruptSuffix(const std::string& function_name)
{
    static const std::string kSuffix = "_IT";
    if (function_name.size() > kSuffix.size() &&
        function_name.compare(function_name.size() - kSuffix.size(), kSuffix.size(), kSuffix) == 0)
    {
        return function_name.substr(0, function_name.size() - kSuffix.size());
    }
    return function_name;
}

} // namespace

std::optional<MappedBlock> mapTimer16Block(const RawInitBlock& block)
{
    if (block.handle_type != "Timer16_HandleTypeDef")
    {
        return std::nullopt;
    }

    MappedBlock mapped;
    mapped.json_block["type"] = "timer16";

    bool has_instance = false;
    bool has_prescaler = false;

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

            if (std::regex_match(value, match, kInstanceValue))
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
            const std::optional<int> value = mapPrescalerEnum(assignment.raw_value_text);

            FieldProvenance provenance;
            provenance.json_field = "prescaler";
            provenance.location = assignment.location;
            provenance.original_raw_value_text = assignment.raw_value_text;
            provenance.original_full_line_text = assignment.full_line_text;

            if (value.has_value())
            {
                mapped.json_block["prescaler"] = *value;
                provenance.editable = true;
            }
            else
            {
                mapped.json_block["prescaler"] = 1;
                provenance.editable = false;
                mapped.notes.push_back(
                    "не удалось распознать Clock.Prescaler = \"" + assignment.raw_value_text +
                    "\" — подставлена заглушка prescaler=1, проверьте блок руками");
            }
            mapped.provenance.push_back(provenance);
            continue;
        }

        // Clock.Source, CountMode, ActiveEdge, Preload, Trigger.*, Filter.*,
        // EncoderMode, Waveform.* — не входят в schemas/timer16.json,
        // молча игнорируются (кодогенератор этого конфигуратора всегда
        // пишет одни и те же значения для них, см. codegen/project/
        // timer16_contribution.cpp — параметризованы только instance,
        // prescaler, mode, arr, cmp).
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
        mapped.json_block["prescaler"] = 1;
        mapped.provenance.push_back(synthesizedProvenance("prescaler"));
        mapped.notes.push_back(
            "в блоке не найдено присваивание Clock.Prescaler — подставлена заглушка "
            "prescaler=1, проверьте блок руками");
    }

    // mode/arr/cmp — из ПОСЛЕДНЕГО (см. .h про "last wins") распознанного
    // HAL_Timer16_StartPWM/StartOneShot/Counter_Start среди trailing_calls,
    // а не из assignments (см. .h). ВСЕГДА editable=false — reverse_patch_
    // writer не умеет переписывать аргумент вызова без "=" в строке.
    bool found_mode = false;
    int matches_seen = 0;
    for (const RawTrailingCall& call : block.trailing_calls)
    {
        const std::string function_name = stripInterruptSuffix(call.function_name);

        std::string mode;
        bool has_compare = false;
        if (function_name == "HAL_Timer16_StartPWM")
        {
            mode = "period";
            has_compare = true;
        }
        else if (function_name == "HAL_Timer16_StartOneShot")
        {
            mode = "one_shot";
            has_compare = true;
        }
        else if (function_name == "HAL_Timer16_Counter_Start")
        {
            mode = "external_event_count";
            has_compare = false;
        }
        else
        {
            continue;  // вызов не про mode/arr/cmp (например прикладной код, использующий хендл) — игнорируем
        }

        ++matches_seen;
        found_mode = true;

        // args[0] — сам хендл (тот же, что связал этот вызов с блоком в
        // сканере), ARR — следующий аргумент, CMP — необязательный третий
        // (mik32_hal_timer16.h: StartPWM/StartOneShot(handle, Period, Compare),
        // Counter_Start(handle, Period) — без Compare).
        // "last wins" (см. .h) — на каждой следующей подходящей trailing_call
        // значения ниже просто перезатираются; провенанс добавляется один
        // раз, после цикла, независимо от того, сколько раз он перезатёрся.
        mapped.json_block["mode"] = mode;

        if (call.args.size() >= 2)
        {
            const std::optional<long long> arr_value = parseCIntegerLiteral(call.args[1]);
            if (arr_value.has_value())
            {
                mapped.json_block["arr"] = *arr_value;
            }
            else
            {
                mapped.json_block["arr"] = 0;
                mapped.notes.push_back(
                    "Не удалось разобрать аргумент ARR вызова " + function_name + "(\"" + call.args[1]
                    + "\") как целочисленный литерал (не поддерживаются арифметические выражения/"
                      "макросы) — использован дефолт 0, проверьте блок руками.");
            }
        }
        if (has_compare && call.args.size() >= 3)
        {
            const std::optional<long long> cmp_value = parseCIntegerLiteral(call.args[2]);
            if (cmp_value.has_value())
            {
                mapped.json_block["cmp"] = *cmp_value;
            }
            else
            {
                mapped.json_block["cmp"] = 0;
                mapped.notes.push_back(
                    "Не удалось разобрать аргумент CMP вызова " + function_name + "(\"" + call.args[2]
                    + "\") как целочисленный литерал (не поддерживаются арифметические выражения/"
                      "макросы) — использован дефолт 0, проверьте блок руками.");
            }
        }
        else
        {
            // external_event_count: cmp игнорируется кодогеном (см. schemas/timer16.json,
            // cmp._comment), но required_fields требует поле — 0 как нейтральная заглушка.
            mapped.json_block["cmp"] = 0;
        }
    }

    if (found_mode)
    {
        if (matches_seen > 1)
        {
            mapped.notes.push_back(
                "найдено больше одного вызова HAL_Timer16_StartPWM/StartOneShot/"
                "Counter_Start для этого блока — использован ПОСЛЕДНИЙ по порядку "
                "появления в файле, проверьте блок руками");
        }
        mapped.provenance.push_back(synthesizedProvenance("mode"));
        mapped.provenance.push_back(synthesizedProvenance("arr"));
        mapped.provenance.push_back(synthesizedProvenance("cmp"));
        mapped.notes.push_back(
            "mode/arr/cmp определены по вызову HAL_Timer16_StartPWM/StartOneShot/"
            "Counter_Start после Init, а не по присваиванию поля — редактирование этих "
            "полей для Timer16 через \"Сгенерировать код\" пока не реализовано (в вызове "
            "нет символа '=', который умеет переписывать патчер), меняйте аргументы "
            "вызова в исходнике руками.");
    }
    else
    {
        mapped.json_block["mode"] = "period";
        mapped.json_block["arr"] = 0;
        mapped.json_block["cmp"] = 0;
        mapped.provenance.push_back(synthesizedProvenance("mode"));
        mapped.provenance.push_back(synthesizedProvenance("arr"));
        mapped.provenance.push_back(synthesizedProvenance("cmp"));
        mapped.notes.push_back(
            "не найден вызов HAL_Timer16_StartPWM/StartOneShot/Counter_Start после "
            "Init для этого блока — mode/arr/cmp не определены, подставлены заглушки "
            "(period/0/0), проверьте блок руками.");
    }

    return mapped;
}

std::optional<std::string> serializeTimer16FieldValue(
    const std::string& field_name,
    const nlohmann::json& new_value,
    const std::string& /*original_raw_value_text*/)
{
    if (field_name == "instance")
    {
        if (!new_value.is_number_integer()) return std::nullopt;
        const int instance = new_value.get<int>();
        if (instance < 0 || instance > 2) return std::nullopt;
        return "TIMER16_" + std::to_string(instance);
    }

    if (field_name == "prescaler")
    {
        if (!new_value.is_number_integer()) return std::nullopt;
        return serializePrescalerEnum(new_value.get<int>());
    }

    // mode/arr/cmp — см. .h, всегда nullopt.
    return std::nullopt;
}

} // namespace reverse_import
