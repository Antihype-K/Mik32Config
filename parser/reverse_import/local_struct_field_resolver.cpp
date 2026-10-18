#include "local_struct_field_resolver.h"

#include <fstream>
#include <regex>

namespace reverse_import
{

namespace
{

std::string stripLineComment(const std::string& line)
{
    const std::size_t pos = line.find("//");
    return pos == std::string::npos ? line : line.substr(0, pos);
}

} // namespace

std::vector<RawFieldAssignment> resolveLocalStructFields(
    const std::string& file_path, const std::string& variable_name)
{
    std::vector<RawFieldAssignment> result;

    const std::regex assignment_pattern(
        "^\\s*" + variable_name + R"(\.([A-Za-z_][\w.\[\]]*)\s*=\s*(.+?)\s*;\s*$)");

    std::ifstream stream(file_path);
    std::string raw_line;
    int line_number = 0;
    while (std::getline(stream, raw_line))
    {
        ++line_number;
        const std::string sanitized = stripLineComment(raw_line);
        std::smatch match;
        if (!std::regex_match(sanitized, match, assignment_pattern))
        {
            continue;
        }

        RawFieldAssignment assignment;
        assignment.field_path = match[1].str();
        assignment.raw_value_text = match[2].str();
        assignment.location = SourceLocation{file_path, line_number};
        assignment.full_line_text = raw_line;
        result.push_back(assignment);
    }

    return result;
}

} // namespace reverse_import
