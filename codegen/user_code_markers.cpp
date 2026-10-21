#include "user_code_markers.h"
#include "legacy_main_c_migrator.h"
#include <regex>
#include <map>
#include <stdexcept>

namespace
{
    // Формат маркера жёстко задан ТЗ, §3.1.7.5 (по аналогии с форматом
    // errata-комментария): "/* USER CODE BEGIN <ID> */" ... "/* USER CODE END <ID> */".
    // \1 в конце — обратная ссылка на тот же ID, что и в BEGIN, чтобы блок с
    // ID="Loop" не мог случайно закрыться маркером END другого ID.
    const std::regex kMarkerBlock(
        R"(/\* USER CODE BEGIN (\S+) \*/([\s\S]*?)/\* USER CODE END \1 \*/)");

    // Ловит BEGIN-маркеры независимо от того, нашёлся ли у них парный END —
    // нужен только для сверки счётчиков ниже (найти осиротевший BEGIN).
    const std::regex kBeginMarker(R"(/\* USER CODE BEGIN (\S+) \*/)");

    std::map<std::string, std::string> extractSections(const std::string& text)
    {
        std::map<std::string, std::string> sections;
        std::map<std::string, int> matched_counts;
        auto begin = std::sregex_iterator(text.begin(), text.end(), kMarkerBlock);
        auto end = std::sregex_iterator();
        for (auto it = begin; it != end; ++it)
        {
            sections[(*it)[1].str()] = (*it)[2].str();
            ++matched_counts[(*it)[1].str()];
        }

        // Баг найден 2026-10-19 (CLAUDE.md, «Планы/что дальше»): если у
        // BEGIN <N> повреждён/стёрт парный END <N>, kMarkerBlock его просто
        // не матчит — секция тихо считалась отсутствующей, и пользовательский
        // код между маркерами терялся без ошибки. Вместо этого — явный отказ.
        std::map<std::string, int> begin_counts;
        for (auto it = std::sregex_iterator(text.begin(), text.end(), kBeginMarker); it != std::sregex_iterator(); ++it)
        {
            ++begin_counts[(*it)[1].str()];
        }
        for (const auto& [id, count] : begin_counts)
        {
            if (matched_counts[id] != count)
            {
                throw std::runtime_error(
                    "USER CODE BEGIN " + id + " без парного USER CODE END " + id +
                    " — отказ от слияния, чтобы не потерять пользовательский код молча");
            }
        }
        return sections;
    }
}

std::string mergeUserCodeSections(
    const std::string& newly_generated_content,
    const std::string& existing_file_content,
    const std::string& newly_generated_init_c)
{
    std::map<std::string, std::string> existing_sections = extractSections(existing_file_content);
    if (existing_sections.empty())
    {
        if (existing_file_content.empty())
        {
            // Файла ещё не было вовсе — сохранять нечего.
            return newly_generated_content;
        }

        // Файл был (например, вендорный пример или проект, никогда не
        // проходивший через наш генератор), но в нём ни одного маркера —
        // раньше в этом случае он тёрся целиком. Пользователь не должен
        // сам раскладывать старый код по секциям — extractLegacyMainCSections
        // (codegen/legacy_main_c_migrator.h) делает это автоматически по
        // CubeMX-подобному набору ID (Header/Includes/PV/PFP/"0"/Init/"3"/"4").
        existing_sections = extractLegacyMainCSections(
            newly_generated_content, existing_file_content, newly_generated_init_c);
        if (existing_sections.empty())
        {
            // Авторазбор не нашёл int main() в одном из текстов — не
            // угадываем структуру, просто не теряем файл: целиком в
            // "#if 0 ... #endif" (переживает "/* */" и #ifdef внутри
            // оригинала, обычный блочный комментарий сломался бы на первом
            // же "*/") внутри секции "4" — дальше обычный маркер.
            existing_sections["4"] =
                "#if 0\n" + existing_file_content + "\n#endif";
        }
    }

    // Прототипы legacy-функций (extractLegacyMainCSections) живут в обычной
    // секции PFP — как любой другой ID, через общий regex-проход ниже.
    const std::string& content = newly_generated_content;

    std::string result;
    result.reserve(content.size());

    auto begin = std::sregex_iterator(content.begin(), content.end(), kMarkerBlock);
    auto end = std::sregex_iterator();
    size_t last_copied = 0;

    for (auto it = begin; it != end; ++it)
    {
        const std::smatch& match = *it;
        const std::string id = match[1].str();

        // Копируем всё до начала текущего блока (включая саму строку
        // "/* USER CODE BEGIN <id> */", которая — часть match.prefix, если
        // считать от последней позиции) без изменений.
        result.append(content, last_copied, match.position(0) - last_copied);

        auto existing_it = existing_sections.find(id);
        const std::string& body = (existing_it != existing_sections.end())
            ? existing_it->second
            : match[2].str();

        result += "/* USER CODE BEGIN " + id + " */";
        result += body;
        result += "/* USER CODE END " + id + " */";

        last_copied = match.position(0) + match.length(0);
    }
    result.append(content, last_copied, std::string::npos);

    return result;
}
