//
// mapUartBlock / serializeUartFieldValue — см. uart_reverse_mapper.h за
// контрактом. Разбор основан на реальном примере USART0_Init в
// docs/сделать/mik32_fixed_v3/mik32/controlFirmware/src/main.cpp (строки
// ~475-519) и на именах enum'ов из templates/mik32_sdk_snapshot/hal/
// peripherals/Include/mik32_hal_usart.h (снэпшот реального SDK).
//
// Важное дизайн-решение, общее для нескольких полей (parity, direction):
// одному JSON-полю схемы соответствуют ДВА независимых физических
// присваивания в HAL-структуре (parity <- parity_bit + parity_bit_inversion;
// direction <- transmitting + receiving). serializeUartFieldValue вызывается
// на КАЖДОЕ из них по отдельности с одним и тем же new_value, но без
// признака "какое из двух физических полей это" — у сигнатуры (см. .h)
// только (field_name, new_value, original_raw_value_text). Тексты
// "Enable"/"Disable" у обоих физических полей ОДИНАКОВЫЕ по алфавиту, так
// что once original_raw_value_text совпадает у обеих строк (например, обе
// "Disable" при parity=="none"), различить их НЕЛЬЗЯ достоверно — угадывать
// запрещено правилами проекта (см. поведение i2c clock_speed). Поэтому:
//  - для целевых значений, где оба физических поля должны получить
//    ОДИНАКОВЫЙ текст независимо от того, какое из них сериализуется
//    ("none" -> оба Disable, "odd" -> оба Enable, "duplex" -> оба Enable),
//    serialize возвращает этот текст безусловно — корректно для любой из
//    двух строк;
//  - для целевых значений, где физические поля должны РАЗОЙТИСЬ ("even":
//    parity_bit=Enable, parity_bit_inversion=Disable; "tx"/"rx": ровно одно
//    из transmitting/receiving = Enable), serialize возвращает nullopt —
//    честно отказывается угадывать, какая из двух строк это, вместо того
//    чтобы применить один и тот же (потенциально неверный для половины
//    строк) текст к обеим.
//
#include "uart_reverse_mapper.h"
#include "c_literal_parser.h"

#include <algorithm>
#include <cctype>
#include <regex>
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

// Все присваивания с данным field_path, в порядке появления в файле.
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

// Последнее по порядку появления присваивание с данным field_path —
// «эффективное» значение при нескольких взаимоисключающих (#if/#ifdef)
// или просто повторных присваиваниях одного поля (см. решение для
// clock_speed у I2C — тот же принцип «последнее побеждает»).
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

// "<буквенный префикс><цифры>" -> префикс + число. Используется и для
// "Instance = UART_1" (маппер), и для serializeUartFieldValue (обратно).
struct PrefixedNumber
{
    std::string prefix;
    int value = 0;
};

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
        // Нет ни одной цифры на конце — это не "<префикс><число>"
        // (например, переменная или выражение).
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

// Целочисленный литерал, возможно hex/binary/восьмеричный, с C-суффиксом —
// husart0.baudrate = 38400; обычно десятичный, но общий парсер (см.
// c_literal_parser.h) также покрывает "0x1C200u" без тихого обрезания на 'x'.
std::optional<long long> parsePlainInteger(const std::string& text)
{
    return parseCIntegerLiteral(text);
}

// "Frame_7bit"/"Frame_8bit"/"Frame_9bit" -> 7/8/9. Реализовано через regex,
// а не жёсткую таблицу трёх значений — не отбрасываем и не подменяем,
// если в исходнике встретится нестандартный "Frame_10bit" (гипотетически),
// само число всё равно извлекается как есть (см. правило про baud_rate).
std::optional<int> parseFrameBits(const std::string& text)
{
    static const std::regex kFrame(R"(^Frame_(\d+)bit$)");
    std::smatch match;
    const std::string trimmed = trimText(text);
    if (std::regex_match(trimmed, match, kFrame))
    {
        return std::stoi(match[1].str());
    }
    return std::nullopt;
}

// "StopBit_1"/"StopBit_2" -> 1/2, тем же принципом, что и parseFrameBits.
std::optional<int> parseStopBits(const std::string& text)
{
    static const std::regex kStop(R"(^StopBit_(\d+)$)");
    std::smatch match;
    const std::string trimmed = trimText(text);
    if (std::regex_match(trimmed, match, kStop))
    {
        return std::stoi(match[1].str());
    }
    return std::nullopt;
}

// "Enable"/"Disable" -> true/false (HAL_USART_EnableDisable_enum,
// mik32_hal_usart.h). Использовано и для parity_bit/parity_bit_inversion,
// и для transmitting/receiving, и для Modem.rts/Modem.cts — все они одного
// enum-типа в реальном HAL.
std::optional<bool> parseEnableDisable(const std::string& text)
{
    const std::string trimmed = trimText(text);
    if (trimmed == "Enable")
    {
        return true;
    }
    if (trimmed == "Disable")
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

// Поле вовсе не найдено, или найдено, но не распознано — см.
// feedback_reverse_import_unresolved_fields_policy: угадывать "безопасный"
// дефолт без отметки запрещено, поле должно быть подсвечено и блокировать
// кодоген. Если assignment есть, вызывающий код дозаполняет
// location/original_raw_value_text/original_full_line_text после вызова.
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

// enum_int-поле схемы (baud_rate/data_bits/stop_bits), не входящее в
// допустимый набор значений, — сохраняем как есть и честно предупреждаем
// (ТЗ/схема не разрешают подмену "ближайшим" значением, см. .h).
void noteIfOutsideAllowedSet(std::vector<std::string>& notes, const std::string& field_label,
                             int value, const std::vector<int>& allowed_values)
{
    if (std::find(allowed_values.begin(), allowed_values.end(), value) != allowed_values.end())
    {
        return;
    }
    std::string allowed_text;
    for (std::size_t i = 0; i < allowed_values.size(); ++i)
    {
        if (i > 0)
        {
            allowed_text += ", ";
        }
        allowed_text += std::to_string(allowed_values[i]);
    }
    notes.push_back(
        field_label + "=" + std::to_string(value) +
        " не входит в допустимый по schemas/uart.json перечень (" + allowed_text +
        ") — сохранено как есть, значение взято из исходника без искажения.");
}

} // namespace

std::optional<MappedBlock> mapUartBlock(const RawInitBlock& block)
{
    if (block.handle_type != "USART_HandleTypeDef")
    {
        return std::nullopt;
    }

    MappedBlock result;
    result.json_block["type"] = "uart";

    // ---- instance (из "Instance = UART_N;") ----
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
                        " вне диапазона 0-1 по schemas/uart.json (max_instances=2) — сохранено как есть.");
                }
            }
            else
            {
                result.json_block["instance"] = 0;
                result.notes.push_back(
                    "Не удалось разобрать номер instance из \"" + effective->raw_value_text +
                    "\" (" + effective->location.file + ":" + std::to_string(effective->location.line) +
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
                    locations += "\"" + occurrence->raw_value_text + "\" (" + occurrence->location.file + ":" +
                                 std::to_string(occurrence->location.line) + ")";
                }
                result.notes.push_back(
                    "Instance присваивается несколько раз с разными значениями (вероятно, "
                    "взаимоисключающие ветки #if/#ifdef, которые сканер не вычисляет): " +
                    locations + ". Использовано последнее по порядку появления в файле.");
            }

            // Каждая физическая строка "Instance = ..." получает свою
            // provenance-запись (а не только "победившая") — редактирование
            // instance в GUI безопасно применить к КАЖДОЙ из них одним и тем
            // же текстом ("<префикс><новый номер>"), в отличие от parity/
            // direction ниже, где две строки должны получить РАЗНЫЙ текст.
            for (const RawFieldAssignment* occurrence : occurrences)
            {
                const bool parsed_ok = parsePrefixedNumber(occurrence->raw_value_text).has_value();
                if (parsed_ok)
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

    // ---- data_bits (из "frame = Frame_Nbit;") ----
    {
        const RawFieldAssignment* assignment = lastAssignment(block, "frame");
        if (assignment == nullptr)
        {
            result.json_block["data_bits"] = 8;
            result.notes.push_back(
                "frame не найден — конфигуратор не смог определить data_bits, укажите значение явно.");
            result.provenance.push_back(makeUnresolvedProvenance("data_bits"));
        }
        else
        {
            const std::optional<int> bits = parseFrameBits(assignment->raw_value_text);
            if (bits.has_value())
            {
                result.json_block["data_bits"] = *bits;
                noteIfOutsideAllowedSet(result.notes, "data_bits", *bits, {7, 8, 9});
                result.provenance.push_back(makeProvenance("data_bits", *assignment, true));
            }
            else
            {
                result.json_block["data_bits"] = 8;
                result.notes.push_back(
                    "Не удалось разобрать frame=\"" + assignment->raw_value_text + "\" (" +
                    assignment->location.file + ":" + std::to_string(assignment->location.line) +
                    ") — конфигуратор не смог определить data_bits, укажите значение явно.");
                result.provenance.push_back(makeUnresolvedProvenance("data_bits", *assignment));
            }
        }
    }

    // ---- stop_bits (из "stop_bit = StopBit_N;") ----
    {
        const RawFieldAssignment* assignment = lastAssignment(block, "stop_bit");
        if (assignment == nullptr)
        {
            result.json_block["stop_bits"] = 1;
            result.notes.push_back(
                "stop_bit не найден — конфигуратор не смог определить stop_bits, укажите значение явно.");
            result.provenance.push_back(makeUnresolvedProvenance("stop_bits"));
        }
        else
        {
            const std::optional<int> bits = parseStopBits(assignment->raw_value_text);
            if (bits.has_value())
            {
                result.json_block["stop_bits"] = *bits;
                noteIfOutsideAllowedSet(result.notes, "stop_bits", *bits, {1, 2});
                result.provenance.push_back(makeProvenance("stop_bits", *assignment, true));
            }
            else
            {
                result.json_block["stop_bits"] = 1;
                result.notes.push_back(
                    "Не удалось разобрать stop_bit=\"" + assignment->raw_value_text + "\" (" +
                    assignment->location.file + ":" + std::to_string(assignment->location.line) +
                    ") — конфигуратор не смог определить stop_bits, укажите значение явно.");
                result.provenance.push_back(makeUnresolvedProvenance("stop_bits", *assignment));
            }
        }
    }

    // ---- baud_rate (из "baudrate = <число>;") ----
    {
        const RawFieldAssignment* assignment = lastAssignment(block, "baudrate");
        if (assignment == nullptr)
        {
            result.json_block["baud_rate"] = 115200;
            result.notes.push_back(
                "baudrate не найден — конфигуратор не смог определить baud_rate, укажите значение явно.");
            result.provenance.push_back(makeUnresolvedProvenance("baud_rate"));
        }
        else
        {
            const std::optional<long long> value = parsePlainInteger(assignment->raw_value_text);
            if (value.has_value())
            {
                result.json_block["baud_rate"] = *value;
                noteIfOutsideAllowedSet(result.notes, "baud_rate", static_cast<int>(*value),
                                        {9600, 19200, 38400, 57600, 115200, 230400, 460800, 921600});
                result.provenance.push_back(makeProvenance("baud_rate", *assignment, true));
            }
            else
            {
                result.json_block["baud_rate"] = 115200;
                result.notes.push_back(
                    "baudrate=\"" + assignment->raw_value_text + "\" (" + assignment->location.file + ":" +
                    std::to_string(assignment->location.line) +
                    ") — не числовой литерал (вычисляемое выражение/переменная?), конфигуратор не смог "
                    "определить baud_rate, укажите значение явно.");
                result.provenance.push_back(makeUnresolvedProvenance("baud_rate", *assignment));
            }
        }
    }

    // ---- parity (из parity_bit + parity_bit_inversion, см. комментарий наверху файла) ----
    {
        const RawFieldAssignment* pb_assignment = lastAssignment(block, "parity_bit");
        const RawFieldAssignment* pbi_assignment = lastAssignment(block, "parity_bit_inversion");
        const std::optional<bool> pb = pb_assignment ? parseEnableDisable(pb_assignment->raw_value_text) : std::nullopt;
        const std::optional<bool> pbi = pbi_assignment ? parseEnableDisable(pbi_assignment->raw_value_text) : std::nullopt;

        std::string parity_value;
        bool resolved = true;
        if (pb.has_value() && !*pb)
        {
            parity_value = "none";
        }
        else if (pb.has_value() && *pb && pbi.has_value())
        {
            parity_value = *pbi ? "odd" : "even";
        }
        else if (!pb_assignment && !pbi_assignment)
        {
            parity_value = "none";
            resolved = false;
            result.notes.push_back(
                "parity_bit/parity_bit_inversion не найдены — конфигуратор не смог определить parity, "
                "укажите значение явно.");
        }
        else
        {
            parity_value = "even";
            resolved = false;
            result.notes.push_back(
                "Не удалось однозначно определить чётность по parity_bit/parity_bit_inversion "
                "(бит четности включён, но полярность не распознана) — конфигуратор не смог определить "
                "parity, укажите значение явно.");
        }
        result.json_block["parity"] = parity_value;

        if (pb_assignment != nullptr)
        {
            result.provenance.push_back(
                resolved && pb.has_value() ? makeProvenance("parity", *pb_assignment, false)
                                            : makeUnresolvedProvenance("parity", *pb_assignment));
        }
        if (pbi_assignment != nullptr)
        {
            result.provenance.push_back(
                resolved && pbi.has_value() ? makeProvenance("parity", *pbi_assignment, false)
                                             : makeUnresolvedProvenance("parity", *pbi_assignment));
        }
        if (pb_assignment == nullptr && pbi_assignment == nullptr)
        {
            result.provenance.push_back(makeUnresolvedProvenance("parity"));
        }
    }

    // ---- direction (из transmitting + receiving, см. комментарий наверху файла) ----
    {
        const RawFieldAssignment* tx_assignment = lastAssignment(block, "transmitting");
        const RawFieldAssignment* rx_assignment = lastAssignment(block, "receiving");
        const std::optional<bool> tx = tx_assignment ? parseEnableDisable(tx_assignment->raw_value_text) : std::nullopt;
        const std::optional<bool> rx = rx_assignment ? parseEnableDisable(rx_assignment->raw_value_text) : std::nullopt;

        const bool tx_enabled = tx.value_or(false);
        const bool rx_enabled = rx.value_or(false);

        bool direction_resolved = true;
        if (!tx.has_value())
        {
            direction_resolved = false;
            result.notes.push_back(
                tx_assignment == nullptr
                    ? std::string("transmitting не найден — конфигуратор не смог определить direction, "
                                   "укажите значение явно.")
                    : "Не удалось разобрать transmitting=\"" + tx_assignment->raw_value_text +
                          "\" — конфигуратор не смог определить direction, укажите значение явно.");
        }
        if (!rx.has_value())
        {
            direction_resolved = false;
            result.notes.push_back(
                rx_assignment == nullptr
                    ? std::string("receiving не найден — конфигуратор не смог определить direction, "
                                   "укажите значение явно.")
                    : "Не удалось разобрать receiving=\"" + rx_assignment->raw_value_text +
                          "\" — конфигуратор не смог определить direction, укажите значение явно.");
        }

        std::string direction_value;
        if (tx_enabled && rx_enabled)
        {
            direction_value = "duplex";
        }
        else if (tx_enabled)
        {
            direction_value = "tx";
        }
        else if (rx_enabled)
        {
            direction_value = "rx";
        }
        else
        {
            direction_value = "duplex";
            direction_resolved = false;
            result.notes.push_back(
                "Ни transmitting, ни receiving не Enable — конфигуратор не смог определить direction, "
                "укажите значение явно.");
        }
        result.json_block["direction"] = direction_value;

        if (tx_assignment != nullptr)
        {
            result.provenance.push_back(
                direction_resolved ? makeProvenance("direction", *tx_assignment, false)
                                    : makeUnresolvedProvenance("direction", *tx_assignment));
        }
        if (rx_assignment != nullptr)
        {
            result.provenance.push_back(
                direction_resolved ? makeProvenance("direction", *rx_assignment, false)
                                    : makeUnresolvedProvenance("direction", *rx_assignment));
        }
        if (tx_assignment == nullptr && rx_assignment == nullptr)
        {
            result.provenance.push_back(makeUnresolvedProvenance("direction"));
        }
    }

    // ---- flow_control (из Modem.rts + Modem.cts) ----
    {
        const RawFieldAssignment* rts_assignment = lastAssignment(block, "Modem.rts");
        const RawFieldAssignment* cts_assignment = lastAssignment(block, "Modem.cts");
        const std::optional<bool> rts = rts_assignment ? parseEnableDisable(rts_assignment->raw_value_text) : std::nullopt;
        const std::optional<bool> cts = cts_assignment ? parseEnableDisable(cts_assignment->raw_value_text) : std::nullopt;

        bool flow_control = false;
        bool flow_resolved = true;
        if (rts.has_value() && cts.has_value())
        {
            flow_control = (*rts && *cts);
            // rts != cts — вместе они не образуют ни чистый "включено", ни
            // чистый "выключено", оба физических бита разъехались, поэтому
            // flow_control всё равно нельзя уверенно определить.
            flow_resolved = (*rts == *cts);
            if (!flow_resolved)
            {
                result.notes.push_back(
                    "Modem.rts и Modem.cts не совпадают (rts=" + std::string(*rts ? "Enable" : "Disable") +
                    ", cts=" + std::string(*cts ? "Enable" : "Disable") +
                    ") — конфигуратор не смог определить flow_control, укажите значение явно.");
            }
        }
        else
        {
            flow_resolved = false;
            result.notes.push_back(
                "Modem.rts/Modem.cts не найдены или не распознаны — конфигуратор не смог определить "
                "flow_control, укажите значение явно.");
        }
        result.json_block["flow_control"] = flow_control;

        if (rts_assignment != nullptr)
        {
            result.provenance.push_back(
                flow_resolved ? makeProvenance("flow_control", *rts_assignment, false)
                              : makeUnresolvedProvenance("flow_control", *rts_assignment));
        }
        if (cts_assignment != nullptr)
        {
            result.provenance.push_back(
                flow_resolved ? makeProvenance("flow_control", *cts_assignment, false)
                              : makeUnresolvedProvenance("flow_control", *cts_assignment));
        }
        if (rts_assignment == nullptr && cts_assignment == nullptr)
        {
            result.provenance.push_back(makeUnresolvedProvenance("flow_control"));
        }
    }

    return result;
}

std::optional<std::string> serializeUartFieldValue(
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
            // Исходный текст не имеет вида "<префикс><число>" — не угадываем
            // префикс макроса.
            return std::nullopt;
        }
        return parsed->prefix + std::to_string(new_value.get<int>());
    }

    if (field_name == "baud_rate")
    {
        if (!new_value.is_number_integer())
        {
            return std::nullopt;
        }
        return std::to_string(new_value.get<long long>());
    }

    if (field_name == "data_bits")
    {
        if (!new_value.is_number_integer())
        {
            return std::nullopt;
        }
        return "Frame_" + std::to_string(new_value.get<int>()) + "bit";
    }

    if (field_name == "stop_bits")
    {
        if (!new_value.is_number_integer())
        {
            return std::nullopt;
        }
        return "StopBit_" + std::to_string(new_value.get<int>());
    }

    if (field_name == "flow_control")
    {
        if (!new_value.is_boolean())
        {
            return std::nullopt;
        }
        // rts и cts всегда получают ОДИНАКОВЫЙ текст для true/false —
        // безопасно для любой из двух физических строк без знания, какая
        // это из них (см. комментарий наверху файла).
        return new_value.get<bool>() ? std::string("Enable") : std::string("Disable");
    }

    if (field_name == "parity")
    {
        if (!new_value.is_string())
        {
            return std::nullopt;
        }
        const std::string value = new_value.get<std::string>();
        if (value == "none")
        {
            return std::string("Disable"); // и parity_bit, и parity_bit_inversion -> Disable
        }
        if (value == "odd")
        {
            return std::string("Enable"); // и parity_bit, и parity_bit_inversion -> Enable
        }
        if (value == "even")
        {
            // parity_bit должен стать Enable, а parity_bit_inversion —
            // Disable: РАЗНЫЙ текст для двух физических строк. Сигнатура
            // не сообщает, какая из них сериализуется сейчас, а
            // original_raw_value_text может совпадать у обеих (например,
            // обе были "Disable" при исходном parity=="none") — угадывать
            // запрещено, честно отказываемся.
            (void)original_raw_value_text;
            return std::nullopt;
        }
        return std::nullopt;
    }

    if (field_name == "direction")
    {
        if (!new_value.is_string())
        {
            return std::nullopt;
        }
        const std::string value = new_value.get<std::string>();
        if (value == "duplex")
        {
            return std::string("Enable"); // и transmitting, и receiving -> Enable
        }
        if (value == "tx" || value == "rx")
        {
            // transmitting и receiving должны РАЗОЙТИСЬ — та же проблема,
            // что и с parity=="even" выше.
            return std::nullopt;
        }
        return std::nullopt;
    }

    return std::nullopt;
}

} // namespace reverse_import
