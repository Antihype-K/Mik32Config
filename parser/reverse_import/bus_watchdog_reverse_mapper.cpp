//
// scanBusWatchdogBlockFromFileContent — см. bus_watchdog_reverse_mapper.h за
// контрактом. Распознаёт ровно тот регистровый паттерн, который генерирует
// codegen/project/bus_watchdog_contribution.cpp:
//
//   WDT_BUS->TIMEOUT = <литерал>u;
//   ...
//   uint32_t bus_watchdog_enable = 0;
//   [bus_watchdog_enable |= (1 << WDT_BUS_EEPROM_S);]
//   [bus_watchdog_enable |= (1 << WDT_BUS_SPIFI_S);]
//   [bus_watchdog_enable |= (1 << WDT_BUS_DOM3_S);]
//   WDT_BUS->ENABLE = bus_watchdog_enable;
//
// Любое отклонение (другая переменная, вычисляемое выражение вместо
// литерала TIMEOUT) -> соответствующее поле unresolved=true.
//
#include "bus_watchdog_reverse_mapper.h"

#include <regex>
#include <sstream>

#include "c_literal_parser.h"

namespace reverse_import
{

namespace
{

FieldProvenance makeUnresolvedProvenance(const std::string& json_field)
{
    FieldProvenance provenance;
    provenance.json_field = json_field;
    provenance.editable = false;
    provenance.unresolved = true;
    return provenance;
}

std::optional<std::pair<std::string, int>> findLineMatch(
    const std::string& file_content, const std::regex& pattern, std::smatch& match_out)
{
    std::istringstream stream(file_content);
    std::string line;
    int line_number = 0;
    while (std::getline(stream, line))
    {
        ++line_number;
        if (std::regex_search(line, match_out, pattern))
        {
            return std::make_pair(line, line_number);
        }
    }
    return std::nullopt;
}

} // namespace

std::optional<MappedBlock> scanBusWatchdogBlockFromFileContent(
    const std::string& file_content, const std::string& file_path)
{
    // Якорь всего блока: без записи ENABLE обратно в регистр это не
    // реальная конфигурация Bus Watchdog.
    static const std::regex kEnableWriteback(
        R"(WDT_BUS\s*->\s*ENABLE\s*=\s*bus_watchdog_enable\s*;)");
    std::smatch writeback_match;
    if (findLineMatch(file_content, kEnableWriteback, writeback_match) == std::nullopt)
    {
        return std::nullopt;
    }

    MappedBlock result;
    result.json_block["type"] = "bus_watchdog";

    // ---- timeout ----
    {
        static const std::regex kTimeout(R"(WDT_BUS\s*->\s*TIMEOUT\s*=\s*([^;]+);)");
        std::smatch match;
        const auto found = findLineMatch(file_content, kTimeout, match);
        if (!found.has_value())
        {
            result.json_block["timeout"] = 0;
            result.notes.push_back(
                "WDT_BUS->TIMEOUT не найден в " + file_path + " — конфигуратор не смог определить "
                "timeout, укажите значение явно.");
            result.provenance.push_back(makeUnresolvedProvenance("timeout"));
        }
        else
        {
            const std::optional<long long> value = parseCIntegerLiteral(match[1].str());
            if (value.has_value())
            {
                result.json_block["timeout"] = static_cast<int>(*value);
                FieldProvenance provenance;
                provenance.json_field = "timeout";
                provenance.location = SourceLocation{file_path, found->second};
                provenance.original_raw_value_text = match[1].str();
                provenance.original_full_line_text = found->first;
                provenance.editable = false;
                result.provenance.push_back(provenance);
            }
            else
            {
                result.json_block["timeout"] = 0;
                result.notes.push_back(
                    "WDT_BUS->TIMEOUT=\"" + match[1].str() + "\" (" + file_path + ":" +
                    std::to_string(found->second) + ") — конфигуратор не смог определить timeout, "
                    "укажите значение явно.");
                FieldProvenance provenance = makeUnresolvedProvenance("timeout");
                provenance.location = SourceLocation{file_path, found->second};
                provenance.original_raw_value_text = match[1].str();
                provenance.original_full_line_text = found->first;
                result.provenance.push_back(provenance);
            }
        }
    }

    // ---- eeprom_enabled / spifi_enabled / dom3_enabled (необязательные) ----
    for (const auto& [macro_suffix, json_field] :
         {std::pair{"EEPROM", "eeprom_enabled"}, std::pair{"SPIFI", "spifi_enabled"},
          std::pair{"DOM3", "dom3_enabled"}})
    {
        const std::regex pattern(
            R"(bus_watchdog_enable\s*\|=\s*\(1\s*<<\s*WDT_BUS_)" + std::string(macro_suffix) +
            R"(_S\)\s*;)");
        std::smatch match;
        result.json_block[json_field] = findLineMatch(file_content, pattern, match).has_value();
    }

    return result;
}

} // namespace reverse_import
