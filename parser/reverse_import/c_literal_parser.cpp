#include "c_literal_parser.h"

#include <cctype>
#include <cstddef>

namespace reverse_import
{

namespace
{

// shortcut: только один оператор без приоритета/скобок — этого достаточно
// для реальных ARR/CMP-выражений вендорных примеров ("0xFFFF / 2",
// "7544 >> 1"); многооператорные выражения и переменные остаются unresolved.
std::optional<long long> tryParseBinaryExpr(const std::string& core)
{
    static const char* kOps[] = {">>", "<<", "/"};
    for (const char* op : kOps)
    {
        const std::size_t pos = core.find(op);
        if (pos == std::string::npos || pos == 0)
        {
            continue;
        }
        const std::string lhs_text = core.substr(0, pos);
        const std::string rhs_text = core.substr(pos + std::string(op).size());
        const auto lhs = parseCIntegerLiteral(lhs_text);
        const auto rhs = parseCIntegerLiteral(rhs_text);
        if (!lhs || !rhs)
        {
            continue;
        }
        if (std::string(op) == "/")
        {
            if (*rhs == 0)
            {
                return std::nullopt;
            }
            return *lhs / *rhs;
        }
        if (std::string(op) == ">>")
        {
            return *lhs >> *rhs;
        }
        return *lhs << *rhs;   // "<<"
    }
    return std::nullopt;
}

} // namespace

std::optional<long long> parseCIntegerLiteral(const std::string& text)
{
    std::size_t begin = text.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos)
    {
        return std::nullopt;
    }
    const std::size_t last = text.find_last_not_of(" \t\r\n");
    std::string core = text.substr(begin, last - begin + 1);

    // Суффикс — только u/U/l/L с конца; hex-цифры A..F не трогаются.
    std::size_t end = core.size();
    while (end > 0)
    {
        const char c = core[end - 1];
        if (c == 'u' || c == 'U' || c == 'l' || c == 'L')
        {
            --end;
        }
        else
        {
            break;
        }
    }
    core.resize(end);
    if (core.empty())
    {
        return std::nullopt;
    }

    // Знак отделяется вручную: 0b-ветка ниже базу не угадывает.
    bool negative = false;
    std::size_t digits_at = 0;
    if (core[0] == '+' || core[0] == '-')
    {
        negative = core[0] == '-';
        digits_at = 1;
    }

    int base = 0;   // 0 -> stoll сам определяет 0x / восьмеричное / десятичное
    if (core.size() >= digits_at + 2 && core[digits_at] == '0' &&
        (core[digits_at + 1] == 'b' || core[digits_at + 1] == 'B'))
    {
        base = 2;
        digits_at += 2;   // "0b" — расширение GCC, stoll его не понимает
    }

    const std::string digits = core.substr(digits_at);
    if (digits.empty())
    {
        return std::nullopt;
    }

    try
    {
        std::size_t consumed = 0;
        const long long value = std::stoll(digits, &consumed, base);
        if (consumed != digits.size())
        {
            return tryParseBinaryExpr(core);   // хвост -> возможно "лит OP лит"
        }
        return negative ? -value : value;
    }
    catch (const std::exception&)
    {
        return tryParseBinaryExpr(core);
    }
}

} // namespace reverse_import
