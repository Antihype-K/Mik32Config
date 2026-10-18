#include "legacy_main_c_migrator.h"

#include <cctype>
#include <regex>
#include <set>
#include <vector>

namespace
{

// Пропускает //-комментарий, /*...*/-комментарий, "строку" или 'символ',
// начинающийся в позиции pos (с учётом \"-экранирования) — возвращает
// позицию сразу ПОСЛЕ пропущенного, либо pos, если в pos ничего такого нет.
// Нужен, чтобы брейс-мэтчер и построчный разбор не путались от '{'/'}'/';'
// внутри строк и комментариев вендорного кода.
size_t skipNonCode(const std::string& text, size_t pos)
{
    if (pos + 1 < text.size() && text[pos] == '/' && text[pos + 1] == '/')
    {
        size_t end = text.find('\n', pos);
        return end == std::string::npos ? text.size() : end + 1;
    }
    if (pos + 1 < text.size() && text[pos] == '/' && text[pos + 1] == '*')
    {
        size_t end = text.find("*/", pos + 2);
        return end == std::string::npos ? text.size() : end + 2;
    }
    if (text[pos] == '"' || text[pos] == '\'')
    {
        const char quote = text[pos];
        size_t i = pos + 1;
        while (i < text.size() && text[i] != quote)
        {
            i += (text[i] == '\\' && i + 1 < text.size()) ? 2 : 1;
        }
        return i < text.size() ? i + 1 : text.size();
    }
    return pos;
}

// Находит '}', парный '{' в позиции open_pos (само open_pos указывает на
// '{'). npos, если скобки не сбалансированы.
size_t findMatchingBrace(const std::string& text, size_t open_pos)
{
    int depth = 0;
    size_t pos = open_pos;
    while (pos < text.size())
    {
        const size_t skipped_to = skipNonCode(text, pos);
        if (skipped_to != pos)
        {
            pos = skipped_to;
            continue;
        }
        if (text[pos] == '{') ++depth;
        else if (text[pos] == '}')
        {
            --depth;
            if (depth == 0) return pos;
        }
        ++pos;
    }
    return std::string::npos;
}

bool isWordChar(char c)
{
    return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
}

// Один "юнит" верхнего уровня файла вне main(): либо функция (заголовок +
// {...} тело), либо одиночное выражение до ';' (глобальная переменная,
// прототип). name — для функций последний идентификатор перед '(' в
// заголовке; для деклараций — идентификатор перед ';' или '=' (имя
// переменной), пустая строка, если не удалось определить.
struct TopLevelUnit
{
    std::string text;
    std::string name;
    bool is_function = false;
    bool has_initializer = false; // true для "Type name = ...;" деклараций
};

std::string trimmed(const std::string& s)
{
    const size_t begin = s.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) return "";
    const size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(begin, end - begin + 1);
}

// Обрезает только полностью пустые строки с начала и конца многострочного
// текста — в отличие от trimmed(), не трогает отступ первой непустой строки
// (он же первый символ склеенного текста), иначе он молча терялся бы вместе
// с окружающими переводами строк, и первая строка секции визуально "вылезала"
// бы из отступа USER CODE-блока относительно остальных строк той же секции.
std::string trimBlankLines(const std::string& s)
{
    size_t begin = 0;
    while (begin < s.size())
    {
        const size_t line_end = s.find('\n', begin);
        const std::string line = s.substr(begin, line_end == std::string::npos ? std::string::npos : line_end - begin);
        if (!trimmed(line).empty()) break;
        if (line_end == std::string::npos) { begin = s.size(); break; }
        begin = line_end + 1;
    }
    size_t end = s.size();
    while (end > begin)
    {
        size_t line_start = s.rfind('\n', end - 1);
        line_start = (line_start == std::string::npos) ? 0 : line_start + 1;
        const std::string line = s.substr(line_start, end - line_start);
        if (!trimmed(line).empty()) break;
        end = (line_start == 0) ? 0 : line_start - 1;
    }
    return begin < end ? s.substr(begin, end - begin) : "";
}

// Последний идентификатор перед '(' в заголовке функции — её имя.
std::string lastIdentifierBefore(const std::string& text, size_t before_pos)
{
    size_t end = before_pos;
    while (end > 0 && !isWordChar(text[end - 1])) --end;
    size_t begin = end;
    while (begin > 0 && isWordChar(text[begin - 1])) --begin;
    return text.substr(begin, end - begin);
}

// Разбивает text (срез файла ВНЕ main(), то есть до или после него) на
// TopLevelUnit. depth всегда 0 на входе — вызывающий код гарантирует это,
// передавая только то, что физически лежит за пределами main().
std::vector<TopLevelUnit> splitTopLevelUnits(const std::string& text)
{
    std::vector<TopLevelUnit> units;
    size_t unit_start = 0;
    size_t pos = 0;
    while (pos < text.size())
    {
        const size_t skipped_to = skipNonCode(text, pos);
        if (skipped_to != pos)
        {
            pos = skipped_to;
            continue;
        }
        // Директива препроцессора (#define/#include/...) не оканчивается на
        // ';' — без этой проверки splitTopLevelUnits сливал её со следующим
        // ';'-юнитом (видно на HAL_EEPROM/HAL_I2C_Scan: "#define X 1\nType h;"
        // становился одним юнитом, и extern потом вставлялся перед '#').
        const size_t cur_line_start = text.rfind('\n', pos == 0 ? 0 : pos - 1) + 1;
        const std::string cur_line = trimmed(text.substr(cur_line_start, pos - cur_line_start));
        if (text[pos] == '\n' && cur_line_start >= unit_start && !cur_line.empty() && cur_line[0] == '#')
        {
            TopLevelUnit unit;
            unit.text = text.substr(unit_start, pos + 1 - unit_start);
            unit.is_function = false;
            unit.has_initializer = false;
            units.push_back(unit);
            pos += 1;
            unit_start = pos;
            continue;
        }
        if (text[pos] == '{')
        {
            const size_t close = findMatchingBrace(text, pos);
            if (close == std::string::npos)
            {
                break; // несбалансировано — отдаём остаток как хвост ниже
            }
            // "Type name[...] = {...};" (инициализация массива/структуры)
            // тоже начинается с '{', но это не функция — заголовок перед
            // '{' кончается на '=', а не на ')' как в "Type name(args) {".
            // Без этой проверки сканер принимал список инициализации за
            // тело функции и терял завершающую ';' (видно на HAL_Crypto:
            // "uint32_t crypto_key[...] = {...}" лишался ';').
            const std::string header_before_brace = trimmed(text.substr(unit_start, pos - unit_start));
            if (!header_before_brace.empty() && header_before_brace.back() == '=')
            {
                size_t after = close + 1;
                while (after < text.size() && std::isspace(static_cast<unsigned char>(text[after]))) ++after;
                if (after < text.size() && text[after] == ';')
                {
                    TopLevelUnit unit;
                    unit.text = text.substr(unit_start, after + 1 - unit_start);
                    unit.is_function = false;
                    unit.has_initializer = true;
                    const size_t eq = header_before_brace.size() - 1;
                    unit.name = lastIdentifierBefore(header_before_brace, eq);
                    units.push_back(unit);
                    pos = after + 1;
                    unit_start = pos;
                    continue;
                }
            }
            TopLevelUnit unit;
            unit.text = text.substr(unit_start, close + 1 - unit_start);
            unit.is_function = true;
            // Имя функции — идентификатор перед ПЕРВОЙ '(' в заголовке
            // "RetType name(args) {", а не перед самой '{' — та стоит сразу
            // после ")" или имени параметра, если он последний (как в
            // "HAL_ADC_MspInit(ADC_HandleTypeDef *hadc) {" — там перед '{'
            // лежит "hadc", а не имя функции).
            {
                const std::string header = text.substr(unit_start, pos - unit_start);
                const size_t first_paren = header.find('(');
                unit.name = (first_paren == std::string::npos)
                    ? ""
                    : lastIdentifierBefore(header, first_paren);
            }
            units.push_back(unit);
            pos = close + 1;
            unit_start = pos;
            continue;
        }
        if (text[pos] == ';')
        {
            TopLevelUnit unit;
            unit.text = text.substr(unit_start, pos + 1 - unit_start);
            unit.is_function = false;
            const std::string body = text.substr(unit_start, pos - unit_start);
            const size_t paren = body.find('(');
            if (paren != std::string::npos)
            {
                // Прототип функции "RetType name(args);" — имя перед '(',
                // как у определений функций, а не перед ';' (там бы попал
                // последний токен внутри "(args)", например "void").
                unit.has_initializer = false;
                unit.name = lastIdentifierBefore(body, paren);
            }
            else
            {
                const size_t eq = body.find('=');
                unit.has_initializer = (eq != std::string::npos);
                unit.name = lastIdentifierBefore(body, eq == std::string::npos ? body.size() : eq);
            }
            units.push_back(unit);
            pos += 1;
            unit_start = pos;
            continue;
        }
        ++pos;
    }
    if (unit_start < text.size() && !trimmed(text.substr(unit_start)).empty())
    {
        TopLevelUnit tail;
        tail.text = text.substr(unit_start);
        tail.is_function = false;
        units.push_back(tail);
    }
    return units;
}

// Имена всех вызовов вида "Имя();" в начале main() ДО цикла while — то,
// что уже регенерировано шаблоном (SystemClock_Config, *_Init контрибуций).
std::set<std::string> collectRegeneratedCallNames(const std::string& pre_loop_body)
{
    std::set<std::string> names;
    static const std::regex kBareCall(R"(^\s*([A-Za-z_]\w*)\s*\(\s*\)\s*;\s*$)");
    size_t line_start = 0;
    while (line_start <= pre_loop_body.size())
    {
        const size_t line_end = pre_loop_body.find('\n', line_start);
        const std::string line = pre_loop_body.substr(
            line_start, (line_end == std::string::npos ? pre_loop_body.size() : line_end) - line_start);
        std::smatch match;
        if (std::regex_match(line, match, kBareCall))
        {
            names.insert(match[1].str());
        }
        if (line_end == std::string::npos) break;
        line_start = line_end + 1;
    }
    return names;
}

struct MainBounds
{
    size_t main_open = std::string::npos;  // позиция '{' функции main
    size_t main_close = std::string::npos; // позиция парного '}'
};

MainBounds findMainBounds(const std::string& text)
{
    static const std::regex kMainHeader(R"(\bint\s+main\s*\([^)]*\)\s*\{)");
    std::smatch match;
    if (!std::regex_search(text, match, kMainHeader))
    {
        return {};
    }
    const size_t open = match.position(0) + match.length(0) - 1;
    const size_t close = findMatchingBrace(text, open);
    if (close == std::string::npos)
    {
        return {};
    }
    return {open, close};
}

// Первый while(...) { на верхнем уровне main_body (depth относительно
// начала main_body, не всего файла).
struct LoopBounds
{
    size_t while_pos = std::string::npos;
    size_t open_brace = std::string::npos;
    size_t close_brace = std::string::npos;
    bool bodyless = false;   // true для "while (1);" без блока {...}
    size_t semicolon_pos = std::string::npos; // валиден только если bodyless
};

LoopBounds findTopLevelWhileLoop(const std::string& main_body)
{
    int depth = 0;
    size_t pos = 0;
    while (pos < main_body.size())
    {
        const size_t skipped_to = skipNonCode(main_body, pos);
        if (skipped_to != pos)
        {
            pos = skipped_to;
            continue;
        }
        if (main_body[pos] == '{')
        {
            ++depth;
            ++pos;
            continue;
        }
        if (main_body[pos] == '}')
        {
            --depth;
            ++pos;
            continue;
        }
        if (depth == 0 && isWordChar(main_body[pos]) &&
            (pos == 0 || !isWordChar(main_body[pos - 1])) &&
            main_body.compare(pos, 5, "while") == 0 &&
            (pos + 5 >= main_body.size() || !isWordChar(main_body[pos + 5])))
        {
            size_t paren = main_body.find('(', pos + 5);
            if (paren == std::string::npos) break;
            size_t paren_depth = 0;
            size_t i = paren;
            for (; i < main_body.size(); ++i)
            {
                if (main_body[i] == '(') ++paren_depth;
                else if (main_body[i] == ')')
                {
                    --paren_depth;
                    if (paren_depth == 0) break;
                }
            }
            if (i >= main_body.size()) break;
            // Пропускаем пробелы/комментарии между ')' условия и телом —
            // "while (1)\n  ;" (бестелый цикл-заглушка, частый в вендорных
            // примерах финальный "висни тут") так же валиден, как "while(1);".
            size_t after_cond = i + 1;
            while (after_cond < main_body.size())
            {
                const size_t skipped_to = skipNonCode(main_body, after_cond);
                if (skipped_to != after_cond) { after_cond = skipped_to; continue; }
                if (std::isspace(static_cast<unsigned char>(main_body[after_cond]))) { ++after_cond; continue; }
                break;
            }
            if (after_cond < main_body.size() && main_body[after_cond] == ';')
            {
                return {pos, std::string::npos, std::string::npos, true, after_cond};
            }
            size_t brace = main_body.find('{', i + 1);
            if (brace == std::string::npos) break;
            const size_t close = findMatchingBrace(main_body, brace);
            if (close == std::string::npos) break;
            return {pos, brace, close};
        }
        ++pos;
    }
    return {};
}

// Вставляет "extern " сразу перед первым кодовым токеном unit_text, пропуская
// ведущие комментарии/пробелы — иначе "extern" оказался бы ПЕРЕД вендорским
// комментарием, разрывая его смысловую привязку к объявлению под ним.
std::string insertExternBeforeCode(const std::string& unit_text)
{
    size_t pos = 0;
    while (pos < unit_text.size())
    {
        const size_t skipped_to = skipNonCode(unit_text, pos);
        if (skipped_to != pos)
        {
            pos = skipped_to;
            continue;
        }
        if (std::isspace(static_cast<unsigned char>(unit_text[pos])))
        {
            ++pos;
            continue;
        }
        break;
    }
    return unit_text.substr(0, pos) + "extern " + unit_text.substr(pos);
}

// Возвращает только ведущие комментарии/пробелы unit_text (без самого кода).
// Нужно, чтобы при выбрасывании regenerated-юнита (прототип SystemClock_Config
// и т.п.) не терять привязанный к нему вендорский комментарий молча.
std::string leadingComment(const std::string& unit_text)
{
    size_t pos = 0;
    while (pos < unit_text.size())
    {
        const size_t skipped_to = skipNonCode(unit_text, pos);
        if (skipped_to != pos)
        {
            pos = skipped_to;
            continue;
        }
        if (std::isspace(static_cast<unsigned char>(unit_text[pos])))
        {
            ++pos;
            continue;
        }
        break;
    }
    return unit_text.substr(0, pos);
}

bool matchesMspInitPattern(const std::string& name)
{
    static const std::regex kMspInit(R"(^HAL_\w+_MspInit$)");
    return std::regex_match(name, kMspInit);
}

// Переименовывает все вхождения идентификатора old_name (как отдельного
// слова) на new_name в тексте — для переноса вендорской функции, имя
// которой совпало с регенерированным (см. renamed_collisions ниже).
std::string renameIdentifier(const std::string& text, const std::string& old_name, const std::string& new_name)
{
    const std::regex word_boundary("\\b" + old_name + "\\b");
    return std::regex_replace(text, word_boundary, new_name);
}

// Имена, которые совпадают с collectRegeneratedCallNames, но у которых
// вендор ОПРЕДЕЛИЛ свою функцию после main() (не просто вызов с тем же
// именем, как GPIO_Init/UART_Init, а полноценное тело). Это не
// "регенерированное" в смысле «значит, уже сделано шаблоном» —
// совпадение имени тут случайное (частый вендорский выбор назвать свою
// функцию так же, как ТЗ называет глобальную контрибуцию, пример —
// DMA_Init: ТЗ §3.1.3.3 требует ровно одну глобальную DMA_Init(void) на
// проект, и вендор исторического примера называет свой хелпер точно так
// же). Раньше такая функция тихо выбрасывалась целиком (как при
// SystemClock_Config/MspInit, для которых совпадение ДЕЙСТВИТЕЛЬНО
// означает «содержимое заменено один-в-один») — здесь тело вендора может
// делать больше, чем регенерированная версия (HAL_DMA_Crypto:
// вендорский DMA_Init вызывает DMA_CH0_Init/DMA_CH1_Init, которых
// регенерированная DMA_Init не знает). Такие имена не выбрасываются, а
// переименовываются (renameIdentifier, см. extractLegacyMainCSections) —
// иначе была бы ещё и ошибка компоновки: "static" определение после
// "extern" объявления с тем же именем в mik32_init.h.
std::set<std::string> collectRenamedCollisions(
    const std::string& old_after_main, const std::set<std::string>& regenerated_names)
{
    std::set<std::string> collisions;
    for (const TopLevelUnit& unit : splitTopLevelUnits(old_after_main))
    {
        if (unit.is_function && unit.name != "SystemClock_Config" && !matchesMspInitPattern(unit.name)
            && regenerated_names.count(unit.name) > 0)
        {
            collisions.insert(unit.name);
        }
    }
    return collisions;
}

// #include-строки до "int main(" в тексте — используется и для нового, и
// для старого файла, чтобы вычислить разницу.
std::set<std::string> collectIncludeLines(const std::string& before_main)
{
    std::set<std::string> includes;
    static const std::regex kInclude(R"(^\s*#include\s+.*$)");
    size_t line_start = 0;
    while (line_start <= before_main.size())
    {
        const size_t line_end = before_main.find('\n', line_start);
        const std::string line = before_main.substr(
            line_start, (line_end == std::string::npos ? before_main.size() : line_end) - line_start);
        if (std::regex_match(line, kInclude))
        {
            includes.insert(trimmed(line));
        }
        if (line_end == std::string::npos) break;
        line_start = line_end + 1;
    }
    return includes;
}

} // namespace

std::map<std::string, std::string> extractLegacyMainCSections(
    const std::string& newly_generated_main_c,
    const std::string& existing_main_c)
{
    const MainBounds new_main = findMainBounds(newly_generated_main_c);
    const MainBounds old_main = findMainBounds(existing_main_c);
    if (new_main.main_open == std::string::npos || old_main.main_open == std::string::npos)
    {
        // Не нашли int main() в одном из текстов — сигнализируем вызывающей
        // стороне "не справились", а не угадываем структуру наугад.
        return {};
    }

    const std::string new_main_body =
        newly_generated_main_c.substr(new_main.main_open + 1, new_main.main_close - new_main.main_open - 1);
    const std::string old_main_body =
        existing_main_c.substr(old_main.main_open + 1, old_main.main_close - old_main.main_open - 1);

    const LoopBounds new_loop = findTopLevelWhileLoop(new_main_body);
    const LoopBounds old_loop = findTopLevelWhileLoop(old_main_body);

    const std::string new_pre_loop = new_loop.while_pos == std::string::npos
        ? new_main_body
        : new_main_body.substr(0, new_loop.while_pos);
    const std::set<std::string> regenerated_names = collectRegeneratedCallNames(new_pre_loop);
    const std::string old_after_main = existing_main_c.substr(old_main.main_close + 1);
    const std::set<std::string> renamed_collisions = collectRenamedCollisions(old_after_main, regenerated_names);

    const std::string old_pre_loop = old_loop.while_pos == std::string::npos
        ? old_main_body
        : old_main_body.substr(0, old_loop.while_pos);

    // Init: строки до цикла, кроме избыточных повторных вызовов "Имя();",
    // которые уже есть в регенерированном main() (см. комментарий в .h).
    std::string init_extra;
    {
        static const std::regex kBareCall(R"(^\s*([A-Za-z_]\w*)\s*\(\s*\)\s*;\s*$)");
        // Вызов renamed-collision функции может быть и С аргументами
        // (SPIFI_Init(&spifi); у вендора, в отличие от GPIO_Init(); без
        // них) — kBareCall такое не матчит, и вызов остаётся нацеленным на
        // старое имя, которое теперь либо регенерировано с другой
        // сигнатурой, либо переименовано в legacy_* без синхронизации
        // вызова (HAL_SPIFI_Flash_Init: "error: too many arguments").
        static const std::regex kAnyCall(R"(^\s*([A-Za-z_]\w*)\s*\(.*\)\s*;\s*$)");
        size_t line_start = 0;
        while (line_start <= old_pre_loop.size())
        {
            const size_t line_end = old_pre_loop.find('\n', line_start);
            const std::string line = old_pre_loop.substr(
                line_start, (line_end == std::string::npos ? old_pre_loop.size() : line_end) - line_start);
            std::smatch match;
            const bool is_bare_call = std::regex_match(line, match, kBareCall);
            std::smatch any_match;
            const bool is_renamed_collision =
                std::regex_match(line, any_match, kAnyCall) && renamed_collisions.count(any_match[1].str()) > 0;
            const bool is_redundant_init_call =
                is_bare_call && !is_renamed_collision && regenerated_names.count(match[1].str()) > 0;
            if (is_renamed_collision)
            {
                // Вызов вендорской функции с именем, совпавшим с
                // регенерированной (см. collectRenamedCollisions) — её
                // определение тоже переименовано ниже, здесь синхронно
                // переименовывается вызов, иначе он указывал бы мимо.
                init_extra += renameIdentifier(line, any_match[1].str(), "legacy_" + any_match[1].str());
                init_extra += "\n";
            }
            else if (!is_redundant_init_call)
            {
                init_extra += line;
                init_extra += "\n";
            }
            if (line_end == std::string::npos) break;
            line_start = line_end + 1;
        }
    }

    std::map<std::string, std::string> sections;
    sections["Init"] = "\n" + trimBlankLines(init_extra) + "\n";
    if (old_loop.while_pos != std::string::npos)
    {
        // "while (1);" без блока — тело пустое по построению, переносить
        // нечего (эквивалент уже даёт сгенерированный шаблонный while(1){}).
        std::string loop_body = old_loop.bodyless
            ? std::string()
            : old_main_body.substr(old_loop.open_brace + 1, old_loop.close_brace - old_loop.open_brace - 1);
        const size_t after_loop = old_loop.bodyless ? old_loop.semicolon_pos : old_loop.close_brace;
        // Хвост main() после while(1){...}/while(1); — редкость (обычно
        // while(1) не возвращается), но если есть, не теряем его: в "3".
        const std::string post_loop = trimmed(old_main_body.substr(after_loop + 1));
        if (!post_loop.empty())
        {
            loop_body += "\n/* (код после основного цикла while в исходнике) */\n" + post_loop;
        }
        // CubeMX-соответствие (2026-10-19): тело цикла -> "3", "WHILE" сама
        // по себе остаётся структурной (всегда пустой) секцией шаблона.
        sections["3"] = loop_body;
    }

    // Includes: чужие #include + всё, что лежало до main() и не является
    // ни include, ни чем-то, что мы решили выбросить как заведомо
    // регенерированное (прототип SystemClock_Config/контрибуций).
    const std::string new_before_main = newly_generated_main_c.substr(0, newly_generated_main_c.find("int main"));
    const std::string old_before_main = existing_main_c.substr(0, existing_main_c.find("int main"));
    const std::set<std::string> new_includes = collectIncludeLines(new_before_main);
    const std::set<std::string> old_includes = collectIncludeLines(old_before_main);

    std::string includes_extra;
    for (const std::string& include_line : old_includes)
    {
        if (new_includes.count(include_line) == 0)
        {
            includes_extra += include_line + "\n";
        }
    }

    // CubeMX-соответствие (2026-10-19, по разбору reverse_import_audit):
    // #include остаются в Includes, но глобальные переменные (PV),
    // прототипы legacy-функций (PFP), функции ДО main() ("0") и вендорский
    // файловый комментарий (Header) — отдельные секции, не Includes. Раньше
    // всё это сваливалось в один Includes-бакет вперемешку с #include.
    std::string header_extra;
    std::string pv_extra;
    std::string zero_functions;

    // Прототипы legacy-функций (определение которых окажется в "4",
    // физически ПОСЛЕ main()) — это не вендорский "USER CODE"-контент по
    // смыслу, а чисто компиляционная связка (нужна декларация до
    // использования в Init). Копятся отдельно и попадают в обычную секцию
    // PFP — как содержимое маркера, не "мимо" маркеров особым ключом.
    std::string legacy_prototypes;

    // Остальное до main() (не #include) — через тот же разбор на юниты,
    // что и "после main()": прототипы регенерированных функций выбрасываем
    // (их ведущий комментарий уходит в Header), "голые" глобальные
    // переменные (с инициализатором и без) -> PV, функции -> "0", чужие
    // прототипы -> PFP.
    {
        std::string before_main_no_includes;
        static const std::regex kInclude(R"(^\s*#include\s+.*$)");
        size_t line_start = 0;
        while (line_start <= old_before_main.size())
        {
            const size_t line_end = old_before_main.find('\n', line_start);
            const std::string line = old_before_main.substr(
                line_start, (line_end == std::string::npos ? old_before_main.size() : line_end) - line_start);
            if (!std::regex_match(line, kInclude))
            {
                before_main_no_includes += line;
                before_main_no_includes += "\n";
            }
            if (line_end == std::string::npos) break;
            line_start = line_end + 1;
        }

        for (const TopLevelUnit& unit : splitTopLevelUnits(before_main_no_includes))
        {
            if (unit.is_function)
            {
                // Функция, ОПРЕДЕЛЁННАЯ (не просто объявленная) до main() —
                // CubeMX-аналог: пользовательские статические хелперы в
                // "USER CODE BEGIN 0" перед int main().
                zero_functions += unit.text + "\n";
                continue;
            }
            if (!unit.name.empty() && renamed_collisions.count(unit.name) > 0)
            {
                // Прототип вендорской функции, чьё имя совпало со
                // регенерированным, но определение переименовано (не
                // выброшено) — см. collectRenamedCollisions. Прототип тоже
                // переименовывается, иначе он объявлял бы несуществующий
                // (старое имя) символ.
                legacy_prototypes += renameIdentifier(unit.text, unit.name, "legacy_" + unit.name) + "\n";
                continue;
            }
            if (!unit.name.empty() && (regenerated_names.count(unit.name) > 0 || unit.name == "SystemClock_Config"))
            {
                // Прототип "void SystemClock_Config(void);" и подобные —
                // регенерированный main() сам содержит актуальный прототип.
                // Привязанный к прототипу вендорский комментарий — файловое
                // описание по смыслу, уходит в Header, а не Includes.
                const std::string comment = trimmed(leadingComment(unit.text));
                if (!comment.empty())
                {
                    header_extra += comment + "\n";
                }
                continue;
            }
            if (!unit.name.empty() && unit.text.find('(') == std::string::npos)
            {
                // "Type name;" или "Type name = init;" — глобальная
                // переменная -> PV. Без инициализатора превращаем в
                // "extern Type name;" (определение уже в mik32_init.c,
                // иначе "multiple definition" на линковке); с
                // инициализатором (пользовательская, не дублирующая
                // mik32_init.c) копируем как есть. "extern" вставляется
                // после ведущих комментариев (insertExternBeforeCode), а не
                // перед ними — иначе он разрывал бы вендорский комментарий.
                const bool already_extern = trimmed(unit.text).rfind("extern", 0) == 0;
                const bool needs_extern = !unit.has_initializer && !already_extern;
                pv_extra += trimmed(needs_extern ? insertExternBeforeCode(unit.text) : unit.text) + "\n";
            }
            else if (unit.text.find('(') != std::string::npos)
            {
                // Прототип функции, не являющийся regenerated — её определение
                // будет в "4", см. комментарий у legacy_prototypes.
                legacy_prototypes += unit.text + "\n";
            }
            else
            {
                // Комментарий/прочее без имени, не распознанное как var/proto.
                header_extra += unit.text + "\n";
            }
        }
    }
    sections["Header"] = "\n" + trimBlankLines(header_extra) + "\n";
    sections["Includes"] = "\n" + trimBlankLines(includes_extra) + "\n";
    sections["PV"] = "\n" + trimBlankLines(pv_extra) + "\n";
    sections["0"] = "\n" + trimBlankLines(zero_functions) + "\n";

    // "4": функции после main(), не совпадающие с тем, что уже
    // регенерировано (SystemClock_Config, вызовы контрибуций, HAL_*_MspInit
    // — последнее всегда замещено сгенерированными GPIO/*_Init). У МИК32
    // нет отдельного it.c-файла как в CubeMX — единственный вектор
    // прерываний RISC-V у вендорных примеров всегда называется "trap_handler"
    // (подтверждено на 15 примерах с прерываниями, 2026-10-19), поэтому он
    // выделен в свою секцию IrqHandlers, а не смешан с прочими legacy-функциями.
    std::string legacy_functions;
    std::string irq_handlers;
    for (const TopLevelUnit& unit : splitTopLevelUnits(old_after_main))
    {
        if (renamed_collisions.count(unit.name) > 0)
        {
            // Имя совпало с регенерированным, но тело вендора может делать
            // больше (см. collectRenamedCollisions) — не выбрасывается,
            // переименовывается целиком (включая внутренние само-ссылки).
            legacy_functions += renameIdentifier(unit.text, unit.name, "legacy_" + unit.name) + "\n\n";
            continue;
        }
        const bool is_superseded = unit.name == "SystemClock_Config" ||
            regenerated_names.count(unit.name) > 0 || matchesMspInitPattern(unit.name);
        if (is_superseded)
        {
            const std::string comment = trimmed(leadingComment(unit.text));
            if (!comment.empty())
            {
                legacy_functions += comment + "\n\n";
            }
            continue;
        }
        if (unit.name == "trap_handler")
        {
            irq_handlers += unit.text + "\n\n";
            continue;
        }
        legacy_functions += unit.text + "\n\n";
    }
    sections["4"] = "\n" + trimBlankLines(legacy_functions) + "\n";
    sections["IrqHandlers"] = "\n" + trimBlankLines(irq_handlers) + "\n";
    sections["PFP"] = "\n" + trimBlankLines(legacy_prototypes) + "\n";

    return sections;
}
