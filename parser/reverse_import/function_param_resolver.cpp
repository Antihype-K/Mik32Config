#include "function_param_resolver.h"

#include "c_literal_parser.h"

#include <fstream>
#include <regex>
#include <sstream>
#include <vector>

namespace reverse_import
{

namespace
{

std::string trim(const std::string& text)
{
    const std::size_t begin = text.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) return "";
    const std::size_t end = text.find_last_not_of(" \t\r\n");
    return text.substr(begin, end - begin + 1);
}

// Вырезает "// ..." до конца строки — хватает для однострочных комментариев,
// как и остальные reverse-import парсеры (см. ограничение в scanner.h).
std::string stripLineComment(const std::string& line)
{
    const std::size_t pos = line.find("//");
    return pos == std::string::npos ? line : line.substr(0, pos);
}

std::vector<std::string> readLines(const std::string& file_path)
{
    std::ifstream in(file_path);
    std::vector<std::string> lines;
    std::string line;
    while (std::getline(in, line))
    {
        lines.push_back(line);
    }
    return lines;
}

struct EnclosingFunction
{
    std::string name;
    std::vector<std::string> param_names;   // в порядке объявления
    int body_start_line = 0;                // 0-based индекс строки с "{"
};

const std::regex kFunctionSignature(
    R"(^[\w][\w\s\*]*\b(\w+)\s*\(([^()]*)\)\s*\{?\s*$)");

bool isControlKeyword(const std::string& name)
{
    return name == "if" || name == "for" || name == "while" ||
           name == "switch" || name == "else" || name == "do";
}

std::vector<std::string> extractParamNames(const std::string& params_text)
{
    std::vector<std::string> names;
    std::stringstream ss(params_text);
    std::string part;
    while (std::getline(ss, part, ','))
    {
        const std::string trimmed = trim(part);
        if (trimmed.empty() || trimmed == "void") continue;
        static const std::regex kLastIdentifier(R"(([A-Za-z_]\w*)\s*$)");
        std::smatch match;
        if (std::regex_search(trimmed, match, kLastIdentifier))
        {
            names.push_back(match[1].str());
        }
        else
        {
            names.push_back("");   // не распознано — позиция сохраняется, имя пустое
        }
    }
    return names;
}

// Ищет ближайшую функцию, объемлющую line_idx (0-based), пропуская
// не-функциональные блоки (if/for/...) по пути наружу — см. .h.
std::optional<EnclosingFunction> findEnclosingFunction(
    const std::vector<std::string>& lines, int line_idx)
{
    int depth = 0;
    for (int i = line_idx; i >= 0; --i)
    {
        const std::string line = stripLineComment(lines[static_cast<std::size_t>(i)]);
        for (int c = static_cast<int>(line.size()) - 1; c >= 0; --c)
        {
            const char ch = line[static_cast<std::size_t>(c)];
            if (ch == '}')
            {
                ++depth;
            }
            else if (ch == '{')
            {
                if (depth == 0)
                {
                    // Стиль "сигнатура на отдельной строке, '{' на следующей"
                    // (как в mik32_fixed_v3) — если перед '{' на этой же
                    // строке пусто, сигнатуру ищем на предыдущей строке.
                    std::string header = trim(line.substr(0, static_cast<std::size_t>(c)));
                    if (header.empty() && i > 0)
                    {
                        header = trim(stripLineComment(lines[static_cast<std::size_t>(i - 1)]));
                    }
                    const std::string header_with_brace = header + " {";
                    std::smatch match;
                    if (std::regex_match(header_with_brace, match, kFunctionSignature) &&
                        !isControlKeyword(match[1].str()))
                    {
                        EnclosingFunction result;
                        result.name = match[1].str();
                        result.param_names = extractParamNames(match[2].str());
                        result.body_start_line = i;
                        return result;
                    }
                    // Не сигнатура функции (if/for/...) — брэйс "использован",
                    // продолжаем искать следующую объемлющую, depth остаётся 0.
                }
                else
                {
                    --depth;
                }
            }
        }
    }
    return std::nullopt;
}

// Объявление "ТИП имя = ВЫРАЖЕНИЕ;" одной строкой в диапазоне [from, to].
std::optional<std::string> findLocalDeclarationExpr(
    const std::vector<std::string>& lines, int from, int to,
    const std::string& variable_name)
{
    const std::regex decl(
        R"(^\s*[\w][\w\s\*]*\b)" + variable_name + R"(\s*=\s*([^;]+);)");
    for (int i = from; i <= to && i < static_cast<int>(lines.size()); ++i)
    {
        std::smatch match;
        const std::string line = stripLineComment(lines[static_cast<std::size_t>(i)]);
        if (std::regex_match(line, match, decl))
        {
            return trim(match[1].str());
        }
    }
    return std::nullopt;
}

// Единственный вызов function_name(...) с литеральными аргументами во всём
// файле, за исключением строки сигнатуры (definition_line). Больше одного
// совпадения или аргумент не-литерал -> nullopt (см. ограничения в .h).
std::optional<std::vector<long long>> findSingleCallLiteralArgs(
    const std::vector<std::string>& lines, const std::string& function_name,
    int definition_line)
{
    const std::regex call(R"(\b)" + function_name + R"(\s*\(([^()]*)\)\s*;)");
    std::optional<std::vector<long long>> found;

    for (int i = 0; i < static_cast<int>(lines.size()); ++i)
    {
        if (i == definition_line) continue;
        const std::string line = stripLineComment(lines[static_cast<std::size_t>(i)]);
        std::smatch match;
        if (!std::regex_search(line, match, call)) continue;

        std::vector<long long> args;
        std::stringstream ss(match[1].str());
        std::string part;
        bool all_literals = true;
        while (std::getline(ss, part, ','))
        {
            const std::optional<long long> value = parseCIntegerLiteral(trim(part));
            if (!value.has_value())
            {
                all_literals = false;
                break;
            }
            args.push_back(*value);
        }
        if (!all_literals) continue;   // объявление/форвард с типами, не вызов

        if (found.has_value())
        {
            return std::nullopt;   // больше одного вызова — неоднозначно
        }
        found = args;
    }
    return found;
}

// Подставляет известные параметры как целые числа в текст выражения.
// Если в выражении остался нераспознанный идентификатор — nullopt.
std::optional<std::string> substituteParams(
    const std::string& expr, const std::vector<std::string>& param_names,
    const std::vector<long long>& param_values)
{
    std::string result = expr;
    for (std::size_t i = 0; i < param_names.size() && i < param_values.size(); ++i)
    {
        if (param_names[i].empty()) continue;
        const std::regex word(R"(\b)" + param_names[i] + R"(\b)");
        result = std::regex_replace(result, word, std::to_string(param_values[i]));
    }
    if (std::regex_search(result, std::regex(R"([A-Za-z_])")))
    {
        return std::nullopt;   // остался нераспознанный идентификатор
    }
    return result;
}

// Минимальный вычислитель целых выражений: + - * / ( ), унарный минус.
class IntExprEvaluator
{
public:
    explicit IntExprEvaluator(const std::string& text) : text_(text), pos_(0) {}

    std::optional<long long> evaluate()
    {
        const std::optional<long long> value = parseExpr();
        if (!value.has_value()) return std::nullopt;
        skipSpaces();
        if (pos_ != text_.size()) return std::nullopt;   // хвост не разобран
        return value;
    }

private:
    void skipSpaces()
    {
        while (pos_ < text_.size() && std::isspace(static_cast<unsigned char>(text_[pos_]))) ++pos_;
    }

    std::optional<long long> parseExpr()
    {
        std::optional<long long> left = parseTerm();
        if (!left.has_value()) return std::nullopt;
        for (;;)
        {
            skipSpaces();
            if (pos_ < text_.size() && (text_[pos_] == '+' || text_[pos_] == '-'))
            {
                const char op = text_[pos_++];
                const std::optional<long long> right = parseTerm();
                if (!right.has_value()) return std::nullopt;
                left = (op == '+') ? (*left + *right) : (*left - *right);
            }
            else
            {
                break;
            }
        }
        return left;
    }

    std::optional<long long> parseTerm()
    {
        std::optional<long long> left = parseFactor();
        if (!left.has_value()) return std::nullopt;
        for (;;)
        {
            skipSpaces();
            if (pos_ < text_.size() && (text_[pos_] == '*' || text_[pos_] == '/'))
            {
                const char op = text_[pos_++];
                const std::optional<long long> right = parseFactor();
                if (!right.has_value()) return std::nullopt;
                if (op == '/')
                {
                    if (*right == 0) return std::nullopt;
                    left = *left / *right;
                }
                else
                {
                    left = *left * *right;
                }
            }
            else
            {
                break;
            }
        }
        return left;
    }

    std::optional<long long> parseFactor()
    {
        skipSpaces();
        if (pos_ < text_.size() && text_[pos_] == '-')
        {
            ++pos_;
            const std::optional<long long> value = parseFactor();
            if (!value.has_value()) return std::nullopt;
            return -*value;
        }
        if (pos_ < text_.size() && text_[pos_] == '(')
        {
            ++pos_;
            const std::optional<long long> value = parseExpr();
            if (!value.has_value()) return std::nullopt;
            skipSpaces();
            if (pos_ >= text_.size() || text_[pos_] != ')') return std::nullopt;
            ++pos_;
            return value;
        }
        const std::size_t start = pos_;
        while (pos_ < text_.size() && std::isdigit(static_cast<unsigned char>(text_[pos_]))) ++pos_;
        if (pos_ == start) return std::nullopt;
        return std::stoll(text_.substr(start, pos_ - start));
    }

    std::string text_;
    std::size_t pos_;
};

} // namespace

std::optional<long long> resolveLocalVariableFromFunctionParams(
    const std::string& file_path, const std::string& variable_name, int assignment_line)
{
    const std::vector<std::string> lines = readLines(file_path);
    if (lines.empty()) return std::nullopt;

    const int line_idx = assignment_line - 1;
    if (line_idx < 0 || line_idx >= static_cast<int>(lines.size())) return std::nullopt;

    const std::optional<EnclosingFunction> function = findEnclosingFunction(lines, line_idx);
    if (!function.has_value()) return std::nullopt;

    const std::optional<std::string> expr = findLocalDeclarationExpr(
        lines, function->body_start_line, line_idx, variable_name);
    if (!expr.has_value()) return std::nullopt;

    const std::optional<std::vector<long long>> call_args =
        findSingleCallLiteralArgs(lines, function->name, function->body_start_line);
    if (!call_args.has_value() || call_args->size() != function->param_names.size())
    {
        return std::nullopt;
    }

    const std::optional<std::string> substituted =
        substituteParams(*expr, function->param_names, *call_args);
    if (!substituted.has_value()) return std::nullopt;

    return IntExprEvaluator(*substituted).evaluate();
}

} // namespace reverse_import
