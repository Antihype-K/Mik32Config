#include "user_code_markers_rust.h"
#include <regex>
#include <map>

namespace
{
    // "// USER CODE BEGIN <ID>" ... "// USER CODE END <ID>" — тот же формат,
    // что codegen/user_code_markers.cpp, только с "//" вместо "/* */".
    // \1 — обратная ссылка на тот же ID, по той же причине, что в оригинале.
    const std::regex kMarkerBlock(
        R"(// USER CODE BEGIN (\S+)([\s\S]*?)// USER CODE END \1)");

    std::map<std::string, std::string> extractSections(const std::string& text)
    {
        std::map<std::string, std::string> sections;
        auto begin = std::sregex_iterator(text.begin(), text.end(), kMarkerBlock);
        auto end = std::sregex_iterator();
        for (auto it = begin; it != end; ++it)
        {
            sections[(*it)[1].str()] = (*it)[2].str();
        }
        return sections;
    }
}

std::string mergeUserCodeSectionsRust(
    const std::string& newly_generated_content,
    const std::string& existing_file_content)
{
    const std::map<std::string, std::string> existing_sections = extractSections(existing_file_content);
    if (existing_sections.empty())
    {
        return newly_generated_content;
    }

    std::string result;
    result.reserve(newly_generated_content.size());

    auto begin = std::sregex_iterator(newly_generated_content.begin(), newly_generated_content.end(), kMarkerBlock);
    auto end = std::sregex_iterator();
    size_t last_copied = 0;

    for (auto it = begin; it != end; ++it)
    {
        const std::smatch& match = *it;
        const std::string id = match[1].str();

        result.append(newly_generated_content, last_copied, match.position(0) - last_copied);

        auto existing_it = existing_sections.find(id);
        const std::string& body = (existing_it != existing_sections.end())
            ? existing_it->second
            : match[2].str();

        result += "// USER CODE BEGIN " + id;
        result += body;
        result += "// USER CODE END " + id;

        last_copied = match.position(0) + match.length(0);
    }
    result.append(newly_generated_content, last_copied, std::string::npos);

    return result;
}
