//
// mapI2cBlock / serializeI2cFieldValue — см. i2c_reverse_mapper.h за
// контрактом. Разбор основан на реальном примере I2C0_Init/I2C1_Init в
// docs/сделать/mik32_fixed_v3/mik32/controlFirmware/src/main.cpp (строки
// ~312-473) и на именах enum'ов из templates/mik32_sdk_snapshot/hal/
// peripherals/Include/mik32_hal_i2c.h (снэпшот реального SDK).
//
// РЕШЕНИЕ ПО clock_speed (задание явно предлагало выбрать между двумя
// вариантами и объяснить выбор здесь):
//
// В реальном коде clock_speed не хранится отдельным полем — вместо этого
// напрямую присваиваются 5 полей Clock.PRESC/SCLDEL/SDADEL/SCLH/SCLL
// (регистр TIMINGR), причём делается это внутри if/switch по частоте, так
// что сканер (не умеющий вычислять условия) видит ВСЕ ветки подряд как
// последовательные присваивания одних и тех же 5 путей. mapI2cBlock всё
// равно ПЫТАЕТСЯ распознать clock_speed — берёт последнюю по порядку
// появления пятёрку значений (тем же принципом "последнее побеждает", что
// и для instance при #ifdef в uart_reverse_mapper.cpp) и сравнивает её с
// таблицей известных комбинаций при HSI=32МГц из задания. Если совпало —
// clock_speed выставляется точно, с примечанием, где взято; если не
// совпало — используется дефолт 100000 с честным предупреждением.
//
// НО в любом случае (совпало или нет) editable для clock_speed всегда
// false, а serializeI2cFieldValue для "clock_speed" всегда возвращает
// nullopt. Причина: одно JSON-значение clock_speed физически представлено
// ПЯТЬЮ независимыми C-строками одновременно (не одной и не двумя, как
// parity/direction в UART) — чтобы отредактировать его в GUI, пришлось бы
// завести 5 provenance-записей с одним и тем же json_field="clock_speed" и
// научить serializeI2cFieldValue возвращать РАЗНОЕ число для каждой из
// пяти строк по одному и тому же original_raw_value_text (само число, без
// имени физического поля, а PRESC/SCLDEL/SDADEL/SCLH/SCLL не пересекаются
// по значениям между строками таблицы никак не гарантированно — например,
// у 100 кГц и 1 МГц оба SDADEL=2, так что по original_raw_value_text=2
// нельзя понять, SDADEL это или что-то другое, если бы значения совпали).
// Задание прямо разрешает в этом случае выбрать более простой и честный
// путь v1: clock_speed — read-only после reverse-import независимо от
// того, распознана комбинация или нет. Отредактировать конфиг можно после
// импорта, просто выставив clock_speed заново в GUI при следующей ПОЛНОЙ
// перегенерации кода (не патчем существующих строк) — это ограничение
// зафиксировано в notes на каждый блок.
//
#include "i2c_reverse_mapper.h"

#include "c_literal_parser.h"

#include <array>
#include <cctype>
#include <string>
#include <vector>

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

std::vector<const RawFieldAssignment*> allAssignments(const RawInitBlock& block, const std::string& field_path)
{
    std::vector<const RawFieldAssignment*> result;
    for (const RawFieldAssignment& assignment : block.assignments)
    {
        if (assignment.field_path == field_path)
        {
            result.push_back(&assignment);
        }
    }
    return result;
}

struct PrefixedNumber
{
    std::string prefix;
    int value = 0;
};

// "<буквенный префикс><цифры>" -> префикс + число, например "I2C_0" -> {"I2C_", 0}.
std::optional<PrefixedNumber> parsePrefixedNumber(const std::string& text)
{
    const std::string trimmed = trimText(text);
    if (trimmed.empty())
    {
        return std::nullopt;
    }
    std::size_t split = trimmed.size();
    while (split > 0 && std::isdigit(static_cast<unsigned char>(trimmed[split - 1])))
    {
        --split;
    }
    if (split == trimmed.size())
    {
        return std::nullopt;
    }
    const std::string digits = trimmed.substr(split);
    const std::string prefix = trimmed.substr(0, split);
    try
    {
        return PrefixedNumber{prefix, std::stoi(digits)};
    }
    catch (const std::exception&)
    {
        return std::nullopt;
    }
}

// Целое число: десятичное или "0x.."/"0X..", с необязательным C-суффиксом
// (u/U/l/L). Используется для Init.OwnAddress1 и для Clock.PRESC и т.п.
// Отбрасывание суффикса по признаку "последний символ — буква" съедало
// hex-цифры: "0xFFFF" -> "0x" -> nullopt, "0x1F" -> "0x1" -> ТИХО неверное 1.
// Вынесено в общий c_literal_parser.h.
std::optional<long long> parseIntegerLiteral(const std::string& text)
{
    return parseCIntegerLiteral(text);
}

std::optional<bool> parseI2cMode(const std::string& text)
{
    const std::string trimmed = trimText(text);
    if (trimmed == "HAL_I2C_MODE_MASTER")
    {
        return true; // true = master, чтобы не путать с bool "is_slave"
    }
    if (trimmed == "HAL_I2C_MODE_SLAVE")
    {
        return false;
    }
    return std::nullopt;
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

FieldProvenance makeSyntheticProvenance(const std::string& json_field)
{
    FieldProvenance provenance;
    provenance.json_field = json_field;
    provenance.editable = false;
    return provenance;
}

// feedback_reverse_import_unresolved_fields_policy: поле не найдено или не
// распознано — вместо тихого дефолта подсвечиваем и блокируем кодоген.
FieldProvenance makeUnresolvedProvenance(const std::string& json_field)
{
    FieldProvenance provenance;
    provenance.json_field = json_field;
    provenance.editable = false;
    provenance.unresolved = true;
    return provenance;
}

FieldProvenance makeUnresolvedProvenance(const std::string& json_field, const RawFieldAssignment& assignment)
{
    FieldProvenance provenance = makeUnresolvedProvenance(json_field);
    provenance.location = assignment.location;
    provenance.original_raw_value_text = assignment.raw_value_text;
    provenance.original_full_line_text = assignment.full_line_text;
    return provenance;
}

// Таблица известных комбинаций TIMINGR при HSI=32МГц (задание, проверено
// по mik32_fixed_v3): {PRESC, SCLDEL, SDADEL, SCLH, SCLL} -> clock_speed.
struct TimingRow
{
    int presc;
    int scldel;
    int sdadel;
    int sclh;
    int scll;
    int clock_speed;
};

constexpr std::array<TimingRow, 4> kKnownTimings = {
    TimingRow{0, 8, 2, 144, 166, 100000},
    TimingRow{0, 3, 2, 31, 35, 400000},
    TimingRow{0, 1, 2, 8, 9, 1000000},
    // Собственный кодоген конфигуратора (codegen/project/i2c_contribution.cpp,
    // calcI2cTimingr) для 100 кГц не считает по формуле, а всегда выдаёт этот
    // HW-подтверждённый пресет (два независимых рабочих примера на 1T Rex
    // Board, см. комментарий у calcI2cTimingr) — другая, но тоже настоящая,
    // 100 кГц комбинация, отличная от найденной в mik32_fixed_v3.
    TimingRow{1, 15, 15, 75, 75, 100000},
};

} // namespace

std::optional<MappedBlock> mapI2cBlock(const RawInitBlock& block)
{
    if (block.handle_type != "I2C_HandleTypeDef")
    {
        return std::nullopt;
    }

    MappedBlock result;
    result.json_block["type"] = "i2c";

    // ---- instance (из "Instance = I2C_N;") ----
    {
        const std::vector<const RawFieldAssignment*> occurrences = allAssignments(block, "Instance");
        if (occurrences.empty())
        {
            result.json_block["instance"] = 0;
            result.notes.push_back(
                "instance не найден: среди присваиваний блока нет строки \"Instance = ...;\" — "
                "конфигуратор не смог определить instance, укажите значение явно.");
            result.provenance.push_back(makeUnresolvedProvenance("instance"));
        }
        else
        {
            bool ambiguous = false;
            for (std::size_t i = 1; i < occurrences.size(); ++i)
            {
                if (trimText(occurrences[i]->raw_value_text) != trimText(occurrences[0]->raw_value_text))
                {
                    ambiguous = true;
                }
            }
            const RawFieldAssignment* effective = occurrences.back();
            const std::optional<PrefixedNumber> parsed = parsePrefixedNumber(effective->raw_value_text);
            if (parsed.has_value())
            {
                result.json_block["instance"] = parsed->value;
                if (parsed->value < 0 || parsed->value > 1)
                {
                    result.notes.push_back(
                        "instance=" + std::to_string(parsed->value) +
                        " вне диапазона 0-1 по schemas/i2c.json (max_instances=2) — сохранено как есть.");
                }
            }
            else
            {
                result.json_block["instance"] = 0;
                result.notes.push_back(
                    "Не удалось разобрать номер instance из \"" + effective->raw_value_text +
                    "\" (" + effective->location.fileName() + ":" + std::to_string(effective->location.line) +
                    ") — конфигуратор не смог определить instance, укажите значение явно.");
            }
            if (ambiguous)
            {
                std::string locations;
                for (const RawFieldAssignment* occurrence : occurrences)
                {
                    if (!locations.empty())
                    {
                        locations += "; ";
                    }
                    locations += "\"" + occurrence->raw_value_text + "\" (" + occurrence->location.fileName() + ":" +
                                 std::to_string(occurrence->location.line) + ")";
                }
                result.notes.push_back(
                    "Instance присваивается несколько раз с разными значениями (вероятно, "
                    "взаимоисключающие ветки #if/#ifdef, которые сканер не вычисляет): " +
                    locations + ". Использовано последнее по порядку появления в файле.");
            }
            for (const RawFieldAssignment* occurrence : occurrences)
            {
                if (parsePrefixedNumber(occurrence->raw_value_text).has_value())
                {
                    result.provenance.push_back(makeProvenance("instance", *occurrence, true));
                }
                else
                {
                    result.provenance.push_back(makeUnresolvedProvenance("instance", *occurrence));
                }
            }
        }
    }

    // ---- mode (из "Init.Mode = HAL_I2C_MODE_MASTER/SLAVE;") ----
    bool is_master = true;
    {
        const RawFieldAssignment* assignment = lastAssignment(block, "Init.Mode");
        if (assignment == nullptr)
        {
            result.json_block["mode"] = "master";
            result.notes.push_back(
                "Init.Mode не найден — конфигуратор не смог определить mode, укажите значение явно.");
            result.provenance.push_back(makeUnresolvedProvenance("mode"));
        }
        else
        {
            const std::optional<bool> master = parseI2cMode(assignment->raw_value_text);
            if (master.has_value())
            {
                is_master = *master;
                result.json_block["mode"] = is_master ? "master" : "slave";
                result.provenance.push_back(makeProvenance("mode", *assignment, true));
            }
            else
            {
                result.json_block["mode"] = "master";
                result.notes.push_back(
                    "Init.Mode=\"" + assignment->raw_value_text + "\" не распознан как HAL_I2C_MODE_MASTER/SLAVE "
                    "— конфигуратор не смог определить mode, укажите значение явно.");
                result.provenance.push_back(makeUnresolvedProvenance("mode", *assignment));
            }
        }
    }

    // ---- address_mode / own_address (только для mode == "slave") ----
    if (!is_master)
    {
        const RawFieldAssignment* assignment = lastAssignment(block, "Init.OwnAddress1");
        if (assignment == nullptr)
        {
            result.json_block["own_address"] = 0;
            result.json_block["address_mode"] = "7bit";
            result.notes.push_back(
                "mode=\"slave\", но Init.OwnAddress1 не найден — конфигуратор не смог определить "
                "own_address/address_mode, укажите значения явно.");
            result.provenance.push_back(makeUnresolvedProvenance("own_address"));
            result.provenance.push_back(makeUnresolvedProvenance("address_mode"));
        }
        else
        {
            const std::optional<long long> address = parseIntegerLiteral(assignment->raw_value_text);
            if (address.has_value())
            {
                result.json_block["own_address"] = static_cast<int>(*address);
                result.provenance.push_back(makeProvenance("own_address", *assignment, true));

                // address_mode в реальном HAL не отдельное поле — выводится
                // из ВЕЛИЧИНЫ OwnAddress1 (mik32_hal_i2c.c, HAL_I2C_OwnAddress1):
                // <=127 -> 7-битный адрес, иначе 10-битный. Поэтому у
                // address_mode нет своей C-строки — редактировать его
                // отдельно от own_address нельзя (см. serialize ниже),
                // provenance указывает на ТУ ЖЕ строку, что и own_address.
                if (*address <= 127)
                {
                    result.json_block["address_mode"] = "7bit";
                }
                else if (*address <= 1023)
                {
                    result.json_block["address_mode"] = "10bit";
                }
                else
                {
                    result.json_block["address_mode"] = "10bit";
                    result.notes.push_back(
                        "own_address=" + std::to_string(*address) +
                        " превышает максимум 10-битного адреса (1023) по schemas/i2c.json — сохранено как есть.");
                }
                result.provenance.push_back(makeProvenance("address_mode", *assignment, false));
            }
            else
            {
                result.json_block["own_address"] = 0;
                result.json_block["address_mode"] = "7bit";
                result.notes.push_back(
                    "Init.OwnAddress1=\"" + assignment->raw_value_text + "\" (" + assignment->location.fileName() + ":" +
                    std::to_string(assignment->location.line) +
                    ") — не числовой литерал, конфигуратор не смог определить own_address/address_mode, "
                    "укажите значения явно.");
                result.provenance.push_back(makeUnresolvedProvenance("own_address", *assignment));
                result.provenance.push_back(makeUnresolvedProvenance("address_mode", *assignment));
            }
        }
    }

    // ---- demo (не аппаратное поле HAL — переключатель поведения main.c, синтезируется) ----
    // Без note: это всегда один и тот же текст на каждом импорте I2C,
    // не actionable для пользователя (нечего "исправлять" по месту).
    {
        result.json_block["demo"] = is_master ? "scanner" : "receive";
        result.provenance.push_back(makeSyntheticProvenance("demo"));
    }

    // ---- clock_speed (TIMINGR, см. большой комментарий наверху файла) ----
    {
        const RawFieldAssignment* presc = lastAssignment(block, "Clock.PRESC");
        const RawFieldAssignment* scldel = lastAssignment(block, "Clock.SCLDEL");
        const RawFieldAssignment* sdadel = lastAssignment(block, "Clock.SDADEL");
        const RawFieldAssignment* sclh = lastAssignment(block, "Clock.SCLH");
        const RawFieldAssignment* scll = lastAssignment(block, "Clock.SCLL");

        if (presc == nullptr || scldel == nullptr || sdadel == nullptr || sclh == nullptr || scll == nullptr)
        {
            result.json_block["clock_speed"] = 100000;
            result.notes.push_back(
                "Не найдены все 5 полей Clock.PRESC/SCLDEL/SDADEL/SCLH/SCLL — конфигуратор не смог "
                "определить clock_speed, укажите значение явно.");
            result.provenance.push_back(makeUnresolvedProvenance("clock_speed"));
        }
        else
        {
            const std::optional<long long> presc_v = parseIntegerLiteral(presc->raw_value_text);
            const std::optional<long long> scldel_v = parseIntegerLiteral(scldel->raw_value_text);
            const std::optional<long long> sdadel_v = parseIntegerLiteral(sdadel->raw_value_text);
            const std::optional<long long> sclh_v = parseIntegerLiteral(sclh->raw_value_text);
            const std::optional<long long> scll_v = parseIntegerLiteral(scll->raw_value_text);

            const TimingRow* matched = nullptr;
            if (presc_v && scldel_v && sdadel_v && sclh_v && scll_v)
            {
                for (const TimingRow& row : kKnownTimings)
                {
                    if (row.presc == *presc_v && row.scldel == *scldel_v && row.sdadel == *sdadel_v &&
                        row.sclh == *sclh_v && row.scll == *scll_v)
                    {
                        matched = &row;
                        break;
                    }
                }
            }

            const std::string quintet_text =
                "PRESC=" + presc->raw_value_text + ", SCLDEL=" + scldel->raw_value_text +
                ", SDADEL=" + sdadel->raw_value_text + ", SCLH=" + sclh->raw_value_text +
                ", SCLL=" + scll->raw_value_text + " (" + scll->location.fileName() + ":" +
                std::to_string(presc->location.line) + "-" + std::to_string(scll->location.line) + ")";

            if (matched != nullptr)
            {
                result.json_block["clock_speed"] = matched->clock_speed;
                result.notes.push_back(
                    "clock_speed=" + std::to_string(matched->clock_speed) +
                    " определён по последней по порядку появления пятёрке TIMINGR: " + quintet_text +
                    ". Поле read-only после импорта — см. обоснование в i2c_reverse_mapper.cpp (одно "
                    "JSON-значение представлено пятью независимыми C-строками одновременно).");
            }
            else
            {
                result.json_block["clock_speed"] = 100000;
                result.notes.push_back(
                    "Не удалось определить clock_speed по TIMINGR — последняя по порядку появления "
                    "пятёрка не совпала ни с одной известной комбинацией (HSI=32МГц): " + quintet_text +
                    ". Конфигуратор не смог определить clock_speed, укажите значение явно.");
            }

            // editable=false ВСЕГДА для clock_speed, независимо от того,
            // совпала пятёрка с таблицей или нет — см. обоснование наверху
            // файла. location указывает на строку SCLL (последнюю из
            // пятёрки) как самую близкую к "текущему" эффективному
            // значению — исключительно для навигации в GUI ("перейти к
            // исходнику"), не для записи правки.
            if (matched != nullptr)
            {
                result.provenance.push_back(makeProvenance("clock_speed", *scll, false));
            }
            else
            {
                result.provenance.push_back(makeUnresolvedProvenance("clock_speed", *scll));
            }
        }
    }

    return result;
}

std::optional<std::string> serializeI2cFieldValue(
    const std::string& field_name,
    const nlohmann::json& new_value,
    const std::string& original_raw_value_text)
{
    if (field_name == "instance")
    {
        if (!new_value.is_number_integer())
        {
            return std::nullopt;
        }
        const std::optional<PrefixedNumber> parsed = parsePrefixedNumber(original_raw_value_text);
        if (!parsed.has_value())
        {
            return std::nullopt;
        }
        return parsed->prefix + std::to_string(new_value.get<int>());
    }

    if (field_name == "mode")
    {
        if (!new_value.is_string())
        {
            return std::nullopt;
        }
        const std::string value = new_value.get<std::string>();
        if (value == "master")
        {
            return std::string("HAL_I2C_MODE_MASTER");
        }
        if (value == "slave")
        {
            return std::string("HAL_I2C_MODE_SLAVE");
        }
        return std::nullopt;
    }

    if (field_name == "own_address")
    {
        if (!new_value.is_number_integer())
        {
            return std::nullopt;
        }
        return std::to_string(new_value.get<long long>());
    }

    if (field_name == "address_mode")
    {
        // Нет отдельной C-строки: address_mode выводится из ВЕЛИЧИНЫ
        // own_address, а не хранится независимо (см. mapI2cBlock выше) —
        // изменить его без изменения own_address физически нечего
        // переписывать, поэтому всегда nullopt.
        (void)new_value;
        (void)original_raw_value_text;
        return std::nullopt;
    }

    if (field_name == "clock_speed")
    {
        // См. подробное обоснование наверху файла: одно значение
        // физически размазано по 5 независимым C-строкам — v1 не
        // поддерживает запись правки обратно.
        (void)new_value;
        (void)original_raw_value_text;
        return std::nullopt;
    }

    if (field_name == "demo")
    {
        // Не аппаратное поле HAL — нет соответствующей C-строки вообще
        // (см. mapI2cBlock: всегда синтезируется, provenance без location).
        return std::nullopt;
    }

    return std::nullopt;
}

} // namespace reverse_import
