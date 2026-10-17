#include "c_literal_parser.h"

#include <cctype>
#include <cstddef>

namespace reverse_import
{

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
            return std::nullopt;   // хвост -> это выражение, а не литерал
        }
        return negative ? -value : value;
    }
    catch (const std::exception&)
    {
        return std::nullopt;
    }
}

} // namespace reverse_import
