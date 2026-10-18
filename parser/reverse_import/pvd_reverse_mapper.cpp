//
// scanPvdBlockFromFileContent — см. pvd_reverse_mapper.h за контрактом.
// Распознаёт ровно тот регистровый паттерн, который генерирует
// codegen/project/pvd_contribution.cpp (единственный известный проекту
// шаблон — нет вендорного примера для сверки):
//
//   PVD_CONTROL->DPF_VALUE = <литерал>;
//   ...
//   pvd_config |= (((uint32_t)<литерал> << PVD_CONFIG_UNDER_THRESH_S) & PVD_CONFIG_UNDER_THRESH_M);
//   pvd_config |= (((uint32_t)<литерал> << PVD_CONFIG_OVER_THRESH_S) & PVD_CONFIG_OVER_THRESH_M);
//   [pvd_config |= PVD_CONFIG_EN_VREFCLB_M;]
//   PVD_CONTROL->CONFIG = pvd_config;
//
// Любое отклонение от этой формы (другое имя локальной переменной, другой
// порядок, вычисляемое выражение вместо литерала) -> соответствующее поле
// unresolved=true, без угадывания — как и везде в reverse-import.
//
#include "pvd_reverse_mapper.h"

#include <regex>

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
    const std::string& file_content, const std::string& file_path, const std::regex& pattern,
    std::smatch& match_out)
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

std::optional<MappedBlock> scanPvdBlockFromFileContent(
    const std::string& file_content, const std::string& file_path)
{
    // Якорь всего блока: без записи CONFIG обратно в регистр ни один из
    // остальных кусков не является реальной конфигурацией PVD (например,
    // чтение PVD_CONTROL->STATUS в обработчике прерывания — не настройка).
    static const std::regex kConfigWriteback(R"(PVD_CONTROL\s*->\s*CONFIG\s*=\s*pvd_config\s*;)");
    std::smatch writeback_match;
    if (findLineMatch(file_content, file_path, kConfigWriteback, writeback_match) == std::nullopt)
    {
        return std::nullopt;
    }

    MappedBlock result;
    result.json_block["type"] = "pvd";
    // Глобальный блок (один на проект) — blockIndices() ищет "pvd" по
    // instance=0 (properties_panel.cpp:3151), см. wdt/tsens-мапперы.
    result.json_block["instance"] = 0;

    // ---- digital_filter_value ----
    {
        static const std::regex kDpf(R"(PVD_CONTROL\s*->\s*DPF_VALUE\s*=\s*([^;]+);)");
        std::smatch match;
        const auto found = findLineMatch(file_content, file_path, kDpf, match);
        if (!found.has_value())
        {
            result.json_block["digital_filter_value"] = 0;
            result.notes.push_back(
                "PVD_CONTROL->DPF_VALUE не найден в " + file_path + " — конфигуратор не смог "
                "определить digital_filter_value, укажите значение явно.");
            result.provenance.push_back(makeUnresolvedProvenance("digital_filter_value"));
        }
        else
        {
            const std::string raw_value = match[1].str();
            const std::optional<long long> value = parseCIntegerLiteral(raw_value);
            if (value.has_value())
            {
                result.json_block["digital_filter_value"] = static_cast<std::uint32_t>(*value);
                FieldProvenance provenance;
                provenance.json_field = "digital_filter_value";
                provenance.location = SourceLocation{file_path, found->second};
                provenance.original_raw_value_text = raw_value;
                provenance.original_full_line_text = found->first;
                provenance.editable = false;
                result.provenance.push_back(provenance);
            }
            else
            {
                result.json_block["digital_filter_value"] = 0;
                result.notes.push_back(
                    "PVD_CONTROL->DPF_VALUE=\"" + raw_value + "\" (" + file_path + ":" +
                    std::to_string(found->second) + ") — конфигуратор не смог определить "
                    "digital_filter_value, укажите значение явно.");
                FieldProvenance provenance = makeUnresolvedProvenance("digital_filter_value");
                provenance.location = SourceLocation{file_path, found->second};
                provenance.original_raw_value_text = raw_value;
                provenance.original_full_line_text = found->first;
                result.provenance.push_back(provenance);
            }
        }
    }

    // ---- under_threshold / over_threshold ----
    for (const auto& [macro_suffix, json_field] :
         {std::pair{"UNDER_THRESH", "under_threshold"}, std::pair{"OVER_THRESH", "over_threshold"}})
    {
        const std::regex pattern(
            R"(pvd_config\s*\|=\s*\(\(\(uint32_t\)([^<]+)<<\s*PVD_CONFIG_)" + std::string(macro_suffix) +
            R"(_S\)\s*&\s*PVD_CONFIG_)" + std::string(macro_suffix) + R"(_M\)\s*;)");
        std::smatch match;
        const auto found = findLineMatch(file_content, file_path, pattern, match);
        if (!found.has_value())
        {
            result.json_block[json_field] = 0;
            result.notes.push_back(
                "pvd_config |= (...PVD_CONFIG_" + std::string(macro_suffix) + "...) не найден в " +
                file_path + " — конфигуратор не смог определить " + json_field +
                ", укажите значение явно.");
            result.provenance.push_back(makeUnresolvedProvenance(json_field));
            continue;
        }

        std::string raw_value = match[1].str();
        // Пробел перед "<<", захваченный группой ([^<]+) без \s в конце.
        while (!raw_value.empty() && (raw_value.back() == ' ' || raw_value.back() == '\t'))
        {
            raw_value.pop_back();
        }

        const std::optional<long long> value = parseCIntegerLiteral(raw_value);
        if (value.has_value())
        {
            result.json_block[json_field] = static_cast<int>(*value);
            FieldProvenance provenance;
            provenance.json_field = json_field;
            provenance.location = SourceLocation{file_path, found->second};
            provenance.original_raw_value_text = raw_value;
            provenance.original_full_line_text = found->first;
            provenance.editable = false;
            result.provenance.push_back(provenance);
        }
        else
        {
            result.json_block[json_field] = 0;
            result.notes.push_back(
                "pvd_config |= (...)" + raw_value + "...) (" + file_path + ":" +
                std::to_string(found->second) + ") — конфигуратор не смог определить " + json_field +
                ", укажите значение явно (вычисляемое выражение вместо литерала не поддерживается).");
            FieldProvenance provenance = makeUnresolvedProvenance(json_field);
            provenance.location = SourceLocation{file_path, found->second};
            provenance.original_raw_value_text = raw_value;
            provenance.original_full_line_text = found->first;
            result.provenance.push_back(provenance);
        }
    }

    // ---- enable_reference_calibration (необязательное) ----
    {
        static const std::regex kVrefClb(R"(pvd_config\s*\|=\s*PVD_CONFIG_EN_VREFCLB_M\s*;)");
        std::smatch match;
        result.json_block["enable_reference_calibration"] =
            findLineMatch(file_content, file_path, kVrefClb, match).has_value();
    }

    return result;
}

} // namespace reverse_import
