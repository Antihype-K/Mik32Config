#include "gpio_reverse_mapper.h"

#include <algorithm>
#include <regex>
#include <set>

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

// GPIO_PIN_<N> — битовая маска (1 << N) в mik32_hal_gpio.h, а не число N
// напрямую (см. templates/mik32_sdk_snapshot/.../mik32_hal_gpio.h:35-51),
// поэтому номер пина всегда берём из текста токена регэкспом, а не через
// арифметику со значением маски.
const std::regex kGpioPinToken(R"(GPIO_PIN_(\d+))");

// Достаёт все номера пинов из "GPIO_PIN_6" или "GPIO_PIN_0 | GPIO_PIN_1".
// Возвращает пустой вектор, если ни один токен не найден (raw_value_text —
// что-то совсем другое, например макрос/переменная).
std::vector<int> extractPinNumbers(const std::string& raw_value_text)
{
    std::vector<int> pins;
    auto begin = std::sregex_iterator(raw_value_text.begin(), raw_value_text.end(), kGpioPinToken);
    auto end = std::sregex_iterator();
    for (auto it = begin; it != end; ++it)
    {
        pins.push_back(std::stoi((*it)[1].str()));
    }
    return pins;
}

// instance берётся из аргумента вызова HAL_GPIO_Init ("GPIO_0"/"GPIO_1"/
// "GPIO_2" — см. templates/mik32_sdk_snapshot/shared/include/mik32_memory_map.h:93-95),
// а не из полей структуры — в GPIO_InitTypeDef физически нет поля "порт".
const std::regex kGpioPortArg(R"(^GPIO_(\d+)$)");

std::optional<int> tryParseIntLiteral(const std::string& raw_value_text)
{
    std::string text = trim(raw_value_text);
    // Снимаем распространённые целочисленные суффиксы (0xFFu, 100UL и т.п.) —
    // в реальном коде mik32_fixed_v3 не встречаются, но это не повод падать
    // на них, если они появятся.
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
            return static_cast<int>(std::stoll(text, nullptr, 16));
        }
        if (std::regex_match(text, kDec))
        {
            return static_cast<int>(std::stoll(text, nullptr, 10));
        }
    }
    catch (const std::exception&)
    {
        return std::nullopt;
    }
    return std::nullopt;
}

// mode/pull — прямое отображение HAL-enum -> строка схемы, см. задание:
// сверено с mik32_hal_gpio.h заказчиком заранее, здесь только таблица.
std::optional<std::string> mapModeEnum(const std::string& raw)
{
    const std::string v = trim(raw);
    if (v == "HAL_GPIO_MODE_GPIO_INPUT") return "input";
    if (v == "HAL_GPIO_MODE_GPIO_OUTPUT") return "output";
    if (v == "HAL_GPIO_MODE_SERIAL") return "serial";
    if (v == "HAL_GPIO_MODE_TIMER_SERIAL") return "timer_serial";
    if (v == "HAL_GPIO_MODE_ANALOG") return "analog";
    return std::nullopt;
}

std::optional<std::string> mapPullEnum(const std::string& raw)
{
    const std::string v = trim(raw);
    if (v == "HAL_GPIO_PULL_NONE") return "none";
    if (v == "HAL_GPIO_PULL_UP") return "pull_up";
    if (v == "HAL_GPIO_PULL_DOWN") return "pull_down";
    return std::nullopt;
}

std::optional<std::string> mapDriveStrengthEnum(const std::string& raw)
{
    const std::string v = trim(raw);
    if (v == "HAL_GPIO_DS_2MA") return "2mA";
    if (v == "HAL_GPIO_DS_4MA") return "4mA";
    if (v == "HAL_GPIO_DS_8MA") return "8mA";
    return std::nullopt;
}

// feedback_reverse_import_unresolved_fields_policy: поле не найдено в блоке
// вовсе — вместо тихого дефолта подсвечиваем и блокируем кодоген.
FieldProvenance synthesizedProvenance(const std::string& json_field)
{
    // location.file пустой => синтезировано мапером, не взято из строки
    // исходника — см. mapped_block.h, комментарий у FieldProvenance::location.
    FieldProvenance provenance;
    provenance.json_field = json_field;
    provenance.editable = false;
    provenance.unresolved = true;
    return provenance;
}

} // namespace

std::optional<MappedBlock> mapGpioBlock(const RawInitBlock& block)
{
    if (block.handle_type != "GPIO_InitTypeDef")
    {
        return std::nullopt;
    }

    MappedBlock mapped;
    mapped.json_block["type"] = "gpio";

    // --- instance: из первого аргумента HAL_GPIO_Init, не из полей ---
    {
        std::smatch match;
        bool recognized = false;
        std::string arg_text;
        if (!block.init_call_args.empty())
        {
            arg_text = trim(block.init_call_args[0]);
            if (std::regex_match(arg_text, match, kGpioPortArg))
            {
                recognized = true;
            }
        }

        FieldProvenance provenance;
        provenance.json_field = "instance";
        // Не editable: значение сидит в аргументе вызова, а не в отдельной
        // "field = value;" строке, которую reverse_patch_writer умеет
        // переписывать построчно — писать правку обратно в этом виде v1 не
        // реализовано (см. serializeGpioFieldValue для деталей формата,
        // который потребуется, когда патч-writer научится это делать).
        provenance.editable = false;

        if (recognized)
        {
            mapped.json_block["instance"] = std::stoi(match[1].str());
            provenance.location = block.init_call_location;
            provenance.original_raw_value_text = arg_text;
        }
        else
        {
            mapped.json_block["instance"] = 0;
            provenance.unresolved = true;
            mapped.notes.push_back(
                "не удалось определить instance порта из аргумента HAL_GPIO_Init — "
                "конфигуратор не смог определить instance, укажите значение явно"
                + (block.init_call_args.empty()
                       ? std::string(" (вызов не имеет аргументов)")
                       : (" (получено: \"" + arg_text + "\")")));
        }
        mapped.provenance.push_back(provenance);
    }

    // --- mode / pull / drive_strength / pins: из присваиваний полей ---
    bool has_pin = false, has_mode = false, has_pull = false, has_ds = false;

    for (const RawFieldAssignment& assignment : block.assignments)
    {
        if (assignment.field_path == "Pin")
        {
            has_pin = true;
            std::vector<int> pins = extractPinNumbers(assignment.raw_value_text);

            FieldProvenance provenance;
            provenance.json_field = "pins";
            provenance.location = assignment.location;
            provenance.original_raw_value_text = assignment.raw_value_text;
            provenance.original_full_line_text = assignment.full_line_text;

            if (!pins.empty())
            {
                std::sort(pins.begin(), pins.end());
                pins.erase(std::unique(pins.begin(), pins.end()), pins.end());
                mapped.json_block["pins"] = pins;
                provenance.editable = true;
            }
            else
            {
                // Ничего не разобрали: pins — обязательное непустое поле
                // (schemas/gpio.json, required_fields + min_length:1), но
                // писать туда честный [] нельзя — блок отбракует валидатор.
                // Самый честный вариант — явная, крупно отмеченная заглушка,
                // а не тихая потеря блока.
                mapped.json_block["pins"] = std::vector<int>{0};
                provenance.editable = false;
                provenance.unresolved = true;
                mapped.notes.push_back(
                    "не удалось разобрать номер(а) пина из \"" +
                    assignment.raw_value_text + "\" (" + assignment.location.file + ":" +
                    std::to_string(assignment.location.line) + ") — конфигуратор не смог "
                    "определить pins, укажите значение явно");
            }
            mapped.provenance.push_back(provenance);
            continue;
        }

        if (assignment.field_path == "Mode")
        {
            has_mode = true;
            std::optional<std::string> mode = mapModeEnum(assignment.raw_value_text);

            FieldProvenance provenance;
            provenance.json_field = "mode";
            provenance.location = assignment.location;
            provenance.original_raw_value_text = assignment.raw_value_text;
            provenance.original_full_line_text = assignment.full_line_text;

            if (mode.has_value())
            {
                mapped.json_block["mode"] = *mode;
                provenance.editable = true;
            }
            else
            {
                mapped.json_block["mode"] = "input";
                provenance.editable = false;
                provenance.unresolved = true;
                mapped.notes.push_back(
                    "не удалось распознать mode из \"" + assignment.raw_value_text +
                    "\" — конфигуратор не смог определить mode, укажите значение явно");
            }
            mapped.provenance.push_back(provenance);
            continue;
        }

        if (assignment.field_path == "Pull")
        {
            has_pull = true;
            std::optional<std::string> pull = mapPullEnum(assignment.raw_value_text);

            FieldProvenance provenance;
            provenance.json_field = "pull";
            provenance.location = assignment.location;
            provenance.original_raw_value_text = assignment.raw_value_text;
            provenance.original_full_line_text = assignment.full_line_text;

            if (pull.has_value())
            {
                mapped.json_block["pull"] = *pull;
                provenance.editable = true;
            }
            else
            {
                mapped.json_block["pull"] = "none";
                provenance.editable = false;
                provenance.unresolved = true;
                mapped.notes.push_back(
                    "не удалось распознать pull из \"" + assignment.raw_value_text +
                    "\" — конфигуратор не смог определить pull, укажите значение явно");
            }
            mapped.provenance.push_back(provenance);
            continue;
        }

        if (assignment.field_path == "DS")
        {
            has_ds = true;
            std::optional<std::string> ds = mapDriveStrengthEnum(assignment.raw_value_text);

            FieldProvenance provenance;
            provenance.json_field = "drive_strength";
            provenance.location = assignment.location;
            provenance.original_raw_value_text = assignment.raw_value_text;
            provenance.original_full_line_text = assignment.full_line_text;

            if (ds.has_value())
            {
                mapped.json_block["drive_strength"] = *ds;
                provenance.editable = true;
            }
            else
            {
                mapped.json_block["drive_strength"] = "4mA";
                provenance.editable = false;
                provenance.unresolved = true;
                mapped.notes.push_back(
                    "не удалось распознать drive_strength из \"" + assignment.raw_value_text +
                    "\" — конфигуратор не смог определить drive_strength, укажите значение явно");
            }
            mapped.provenance.push_back(provenance);
            continue;
        }
        // Прочие поля (не Pin/Mode/Pull/DS) в GPIO_InitTypeDef не встречаются —
        // молча игнорируем незнакомые field_path (могут быть последствием
        // ложного совпадения регэкспа сканера, не наша забота здесь).
    }

    if (!has_pin)
    {
        mapped.json_block["pins"] = std::vector<int>{0};
        mapped.provenance.push_back(synthesizedProvenance("pins"));
        mapped.notes.push_back(
            "ВНИМАНИЕ: в блоке не найдено присваивание Pin — подставлено ФИКТИВНОЕ "
            "значение pins=[0], проверьте блок руками");
    }
    if (!has_mode)
    {
        mapped.json_block["mode"] = "input";
        mapped.provenance.push_back(synthesizedProvenance("mode"));
        mapped.notes.push_back("mode не задан явно в исходнике, применён дефолт \"input\"");
    }
    if (!has_pull)
    {
        mapped.json_block["pull"] = "none";
        mapped.provenance.push_back(synthesizedProvenance("pull"));
        mapped.notes.push_back("pull не задан явно в исходнике, применён дефолт \"none\"");
    }
    if (!has_ds)
    {
        // Задание: в mik32_fixed_v3 drive_strength нигде явно не задаётся —
        // это системная норма для этого файла, а не ошибка сканера.
        mapped.json_block["drive_strength"] = "4mA";
        mapped.provenance.push_back(synthesizedProvenance("drive_strength"));
        mapped.notes.push_back(
            "drive_strength не задан явно в исходнике, применён дефолт 4mA — при "
            "необходимости добавьте явное присваивание в исходный код руками");
    }

    return mapped;
}

std::optional<std::string> serializeGpioFieldValue(
    const std::string& field_name,
    const nlohmann::json& new_value,
    const std::string& original_raw_value_text)
{
    if (field_name == "pins")
    {
        if (!new_value.is_array() || new_value.empty())
        {
            return std::nullopt;
        }
        std::set<int> pins;
        for (const nlohmann::json& item : new_value)
        {
            if (!item.is_number_integer())
            {
                return std::nullopt;
            }
            pins.insert(item.get<int>());
        }

        // Угадываем стиль разделителя у оригинала: " | " (с пробелами,
        // как везде в mik32_fixed_v3) — дефолт, если не удалось определить.
        std::string separator = " | ";
        if (original_raw_value_text.find(" | ") == std::string::npos &&
            original_raw_value_text.find('|') != std::string::npos)
        {
            separator = "|";
        }

        std::string result;
        bool first = true;
        for (int pin : pins)
        {
            if (!first)
            {
                result += separator;
            }
            result += "GPIO_PIN_" + std::to_string(pin);
            first = false;
        }
        return result;
    }

    if (field_name == "mode")
    {
        if (!new_value.is_string()) return std::nullopt;
        const std::string v = new_value.get<std::string>();
        if (v == "input") return "HAL_GPIO_MODE_GPIO_INPUT";
        if (v == "output") return "HAL_GPIO_MODE_GPIO_OUTPUT";
        if (v == "serial") return "HAL_GPIO_MODE_SERIAL";
        if (v == "timer_serial") return "HAL_GPIO_MODE_TIMER_SERIAL";
        if (v == "analog") return "HAL_GPIO_MODE_ANALOG";
        return std::nullopt;
    }

    if (field_name == "pull")
    {
        if (!new_value.is_string()) return std::nullopt;
        const std::string v = new_value.get<std::string>();
        if (v == "none") return "HAL_GPIO_PULL_NONE";
        if (v == "pull_up") return "HAL_GPIO_PULL_UP";
        if (v == "pull_down") return "HAL_GPIO_PULL_DOWN";
        return std::nullopt;
    }

    if (field_name == "drive_strength")
    {
        if (!new_value.is_string()) return std::nullopt;
        const std::string v = new_value.get<std::string>();
        if (v == "2mA") return "HAL_GPIO_DS_2MA";
        if (v == "4mA") return "HAL_GPIO_DS_4MA";
        if (v == "8mA") return "HAL_GPIO_DS_8MA";
        return std::nullopt;
    }

    if (field_name == "instance")
    {
        // Поддержано для полноты контракта; MappedBlock.provenance для
        // instance сейчас всегда editable=false (см. mapGpioBlock) — это
        // задел на будущее, когда reverse_patch_writer научится
        // переписывать аргумент вызова, а не только "field = value;".
        if (!new_value.is_number_integer()) return std::nullopt;
        const int instance = new_value.get<int>();
        if (instance < 0 || instance > 2) return std::nullopt;
        return "GPIO_" + std::to_string(instance);
    }

    return std::nullopt;
}

} // namespace reverse_import
