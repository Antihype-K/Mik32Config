#include "reverse_import_scanner.h"

#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace reverse_import
{

namespace
{

// Каталоги, не имеющие смысла сканировать: система контроля версий, кэши
// сборки/IDE. mik32_fixed_v3/mik32/controlFirmware/.pio/build содержит
// СОТНИ сгенерированных промежуточных файлов — без этого фильтра обход
// стал бы на порядки медленнее и нашёл бы мусорные "декларации" из копий
// заголовков HAL внутри .pio.
bool isIgnoredDirectoryName(const std::string& name)
{
    static const std::unordered_set<std::string> kIgnored = {
        ".git", ".pio", ".idea", ".vscode", "build", "cmake-build-debug",
        "cmake-build-release", "node_modules",
        // Собственный CMake-тулчейн этого конфигуратора (codegen/project/
        // cmake_project_codegen.cpp:704,753) копирует/клонирует ВЕСЬ SDK
        // рядом с прошивкой при hal_source_mode=bundled/network — без этого
        // фильтра импорт такого проекта тонет в десятках ложных "периферий",
        // найденных внутри исходников самого HAL, а не прикладного кода
        // (обнаружено на generated_projects/n422e2: 45 найденных блоков
        // вместо 1 реального — 44 из них из mik32-sdk/).
        "mik32-sdk", "mik32-sdk-full"
    };
    return kIgnored.count(name) > 0;
}

bool hasSourceExtension(const std::filesystem::path& path)
{
    static const std::unordered_set<std::string> kExtensions = {
        ".c", ".cpp", ".cc", ".cxx", ".h", ".hpp"
    };
    return kExtensions.count(path.extension().string()) > 0;
}

// Убирает "// ..." до конца строки, однострочные "/* ... */" И содержимое
// строковых/символьных литералов (заменяет их на пробелы, сохраняя сами
// кавычки) — ОДНИМ проходом, с правильным приоритетом (см. ниже почему это
// не может быть тремя независимыми regex-проходами).
//
// Баг, из-за которого этот проход появился: "xprintf(\"HAL_I2C_Init(&hi2c0)
// HAL_ERROR\\n\");" в mik32_fixed_v3 (main.cpp:389) — простая строка ЛОГА
// ОШИБКИ, а не вызов. Прежняя версия резала только "//"-комментарии и
// вырезала /* */, но НЕ маскировала содержимое строковых литералов — из-за
// этого kInitCall находил внутри строки текст "HAL_I2C_Init(&hi2c0)" и
// заводил на него ЛОЖНЫЙ RawInitBlock без единого присваивания поля (что и
// давало note "не имеет ни одного распознанного присваивания перед ним").
// На реальном проекте это привело к трём "блокам" I2C_0 вместо одного —
// parseProjectDocument (ТЗ §3.1.6.2) справедливо ругался на дублирующийся
// instance, хотя дублирования на самом деле не было.
//
// Почему один проход, а не отдельно regex для строк + отдельно для "//": "//"
// внутри строкового литерала (например "http://") иначе обрывал бы строку
// как комментарий, а закрывающая '"' внутри "//"-комментария (иначе
// невозможный в C/C++ код, но программно нужно от чего-то отталкиваться)
// сбила бы состояние "в строке". Однопроходный автомат с явным состоянием
// (обычный текст / в строке / в símbol-литерале) не имеет этой двусмысленности.
std::string sanitizeLine(const std::string& line)
{
    std::string result;
    result.reserve(line.size());

    bool in_string = false;
    bool in_char = false;

    for (std::size_t i = 0; i < line.size(); ++i)
    {
        const char c = line[i];

        if (in_string || in_char)
        {
            const char closing_quote = in_string ? '"' : '\'';
            if (c == '\\' && i + 1 < line.size())
            {
                // Экранированный символ (\", \\, \n и т.п.) — оба символа
                // маскируются, чтобы escape-последовательность не могла
                // случайно совпасть с закрывающей кавычкой или регуляркой.
                result += ' ';
                result += ' ';
                ++i;
                continue;
            }
            if (c == closing_quote)
            {
                in_string = false;
                in_char = false;
                result += c;
                continue;
            }
            result += ' ';
            continue;
        }

        if (c == '"')
        {
            in_string = true;
            result += c;
            continue;
        }
        if (c == '\'')
        {
            in_char = true;
            result += c;
            continue;
        }
        if (c == '/' && i + 1 < line.size() && line[i + 1] == '/')
        {
            break;  // однострочный комментарий — остаток строки не нужен
        }
        if (c == '/' && i + 1 < line.size() && line[i + 1] == '*')
        {
            const std::size_t close = line.find("*/", i + 2);
            if (close == std::string::npos)
            {
                // Незакрытый на этой строке /* — многострочные блочные
                // комментарии не поддерживаются (см. ограничения в .h),
                // консервативно считаем остаток строки комментарием.
                break;
            }
            for (std::size_t j = i; j < close + 2; ++j)
            {
                result += ' ';
            }
            i = close + 1;
            continue;
        }

        result += c;
    }

    return result;
}

std::string trim(const std::string& text)
{
    const std::size_t begin = text.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos)
    {
        return "";
    }
    const std::size_t end = text.find_last_not_of(" \t\r\n");
    return text.substr(begin, end - begin + 1);
}

std::string stripAmpersand(const std::string& arg)
{
    const std::string trimmed = trim(arg);
    if (!trimmed.empty() && trimmed.front() == '&')
    {
        return trim(trimmed.substr(1));
    }
    return trimmed;
}

std::vector<std::string> splitArgs(const std::string& args_text)
{
    std::vector<std::string> parts;
    std::stringstream stream(args_text);
    std::string part;
    while (std::getline(stream, part, ','))
    {
        parts.push_back(stripAmpersand(part));
    }
    return parts;
}

struct PendingHandle
{
    std::string handle_type;
    SourceLocation declared_at;
    std::vector<RawFieldAssignment> pending_assignments;
};

void scanFile(const std::filesystem::path& file_path, ScannedProject& out)
{
    std::ifstream stream(file_path);
    if (!stream.is_open())
    {
        out.unsupported_notes.push_back("Не удалось открыть файл: " + file_path.string());
        return;
    }

    // Тип хендла — любой идентификатор, заканчивающийся на HandleTypeDef или
    // InitTypeDef: I2C_HandleTypeDef, USART_HandleTypeDef, TIMER32_HandleTypeDef,
    // GPIO_InitTypeDef и т.п. — общий шаблон именования во всех *_hal_*.h МИК32.
    // Инициализатор после имени ("= {0}") — необязателен и не разбирается,
    // только пропускается: собственный кодоген конфигуратора всегда пишет
    // "GPIO_InitTypeDef GPIO_InitStruct = {0};" (см. codegen/project/
    // gpio_contribution.cpp) — без поддержки "= ..." декларация в СВОИХ ЖЕ
    // сгенерированных проектах не находилась бы вообще.
    static const std::regex kDeclaration(
        R"(^\s*([A-Za-z_]\w*(?:HandleTypeDef|InitTypeDef))\s+([A-Za-z_]\w*)\s*(?:=\s*.*)?;\s*$)");

    // "<handle>.<field.path> = <value>;" — допускает вложенность через точку
    // (Init.Mode, Clock.PRESC) и одиночный индекс массива ([0]) в field_path.
    static const std::regex kAssignment(
        R"(^\s*([A-Za-z_]\w*)\.([A-Za-z_][\w.\[\]]*)\s*=\s*(.+?)\s*;\s*$)");

    // Присваивание, растянутое на несколько строк (нет ';' на этой строке) —
    // распознаём сам факт, чтобы не промолчать о пропуске.
    static const std::regex kUnterminatedAssignment(
        R"(^\s*([A-Za-z_]\w*)\.([A-Za-z_][\w.\[\]]*)\s*=\s*[^;]*$)");

    static const std::regex kInitCall(R"(HAL_(\w+)_Init\s*\(([^()]*)\))");

    // Любой вызов вида "имя(аргументы)" — используется ТОЛЬКО на строках, где
    // не нашлось ни одного HAL_*_Init (см. matched_any_init ниже), чтобы
    // поймать "донастройку после Init" (HAL_Timer16_StartPWM и т.п., см.
    // RawTrailingCall в .h) без дублирования уже разобранных Init-вызовов.
    static const std::regex kGenericCall(R"((\w+)\s*\(([^()]*)\))");

    std::unordered_map<std::string, PendingHandle> handles;

    // Индекс (в ОБЩЕМ, межфайловом out.blocks) последнего блока, созданного
    // для этого имени хендла в ЭТОМ файле — куда цеплять trailing_calls.
    // Хранить индекс, а не указатель: out.blocks.push_back может
    // реаллоцировать вектор, инвалидируя указатели, но не индексы.
    std::unordered_map<std::string, std::size_t> last_block_index;

    // Вызовы, ссылающиеся на объявленный хендл, встреченные ДО его первого
    // HAL_*_Init. Нужны, потому что в вендорных примерах "донастройка" сплошь
    // стоит в main(), а сам Init — в статической функции НИЖЕ по файлу:
    // docs/examples/mik32-examples/HAL_Timer16/src/main.c вызывает
    // HAL_Timer16_StartPWM(&htimer16_1, 0xFFFF, 0xFFFF / 2) на строке 34, а
    // HAL_Timer16_Init(&htimer16_1) — только на строке 107, внутри
    // Timer16_1_Init(). Без этого буфера timer16-маппер не находил ни одного
    // Start-вызова и подставлял заглушки mode=period/arr=0/cmp=0 (ровно то
    // ограничение "межфункциональная связка Init/Start", что было отмечено
    // открытым). Текстовый сканер не знает порядка ВЫЗОВА функций, но для
    // восстановления конфигурации он и не нужен: важно лишь, что Start
    // относится к этому хендлу.
    std::unordered_map<std::string, std::vector<RawTrailingCall>> pre_init_calls;

    std::string raw_line;
    int line_number = 0;
    while (std::getline(stream, raw_line))
    {
        ++line_number;
        const std::string full_line_text = raw_line;
        const std::string code = sanitizeLine(raw_line);

        std::smatch match;
        if (std::regex_match(code, match, kDeclaration))
        {
            const std::string handle_type = match[1].str();
            const std::string handle_name = match[2].str();
            // Повторная декларация с тем же именем перезатирает предыдущую —
            // это либо новый файл (мы уже начинаем с чистой map на файл),
            // либо переменная переиспользована под другой смысл; последнее
            // объявление ближе к месту использования, поэтому и побеждает.
            handles[handle_name] = PendingHandle{handle_type, SourceLocation{file_path.string(), line_number}, {}};
            continue;
        }

        if (std::regex_match(code, match, kAssignment))
        {
            const std::string object_name = match[1].str();
            const std::string field_path = match[2].str();
            const std::string value_text = trim(match[3].str());

            auto it = handles.find(object_name);
            if (it != handles.end())
            {
                it->second.pending_assignments.push_back(RawFieldAssignment{
                    field_path, value_text,
                    SourceLocation{file_path.string(), line_number},
                    full_line_text});
            }
            else if (field_path == "Instance")
            {
                // ".Instance = I2C_0;"-подобная строка — надёжный отпечаток
                // HAL-хендла (см. .h, п. про Instance). Хендл не объявлен В
                // ЭТОМ ФАЙЛЕ — вероятно, объявлен в заголовке, а Init вызван
                // в другом .cpp. Такую периферию сканер не вычленит молча.
                out.unsupported_notes.push_back(
                    "Похоже на HAL-хендл \"" + object_name + "\" (поле Instance), но "
                    "он не объявлен в этом же файле — кросс-файловые хендлы не "
                    "поддерживаются: " + file_path.string() + ":" + std::to_string(line_number));
            }
            continue;
        }

        // Только если это НЕ было успешно разобрано как обычное присваивание
        // (иначе валидная короткая строка "x.y = 1;" ложно попала бы и сюда,
        // т.к. regex_match для kUnterminatedAssignment тоже мог совпасть на
        // усечённом варианте — но благодаря `continue` выше сюда не дойдём).
        if (std::regex_match(code, match, kUnterminatedAssignment))
        {
            const std::string object_name = match[1].str();
            if (handles.count(object_name) > 0)
            {
                out.unsupported_notes.push_back(
                    "Присваивание поля \"" + match[2].str() + "\" хендла \"" + object_name +
                    "\" растянуто на несколько строк — не распознано: " +
                    file_path.string() + ":" + std::to_string(line_number));
            }
            continue;
        }

        // Вызов(ы) HAL_*_Init могут встретиться внутри условия (if (...)) —
        // поэтому поиск, а не полное совпадение строки.
        auto begin = std::sregex_iterator(code.begin(), code.end(), kInitCall);
        auto end = std::sregex_iterator();
        bool matched_any_init = false;
        for (auto init_it = begin; init_it != end; ++init_it)
        {
            matched_any_init = true;
            const std::string init_function_name = "HAL_" + (*init_it)[1].str() + "_Init";
            const std::vector<std::string> args = splitArgs((*init_it)[2].str());

            std::string matched_handle_name;
            for (const std::string& arg : args)
            {
                if (handles.count(arg) > 0)
                {
                    matched_handle_name = arg;
                    break;
                }
            }

            if (matched_handle_name.empty())
            {
                // Init на хендл, который сканер не видел объявленным в этом
                // файле — тот же кросс-файловый случай, что и для Instance
                // выше, но с другой стороны (нашли Init, а не декларацию).
                out.unsupported_notes.push_back(
                    "Вызов " + init_function_name + "(" + (*init_it)[2].str() +
                    ") ссылается на хендл, не объявленный в этом файле: " +
                    file_path.string() + ":" + std::to_string(line_number));
                continue;
            }

            PendingHandle& handle = handles[matched_handle_name];
            RawInitBlock block;
            block.handle_name = matched_handle_name;
            block.handle_type = handle.handle_type;
            block.assignments = std::move(handle.pending_assignments);
            handle.pending_assignments.clear();
            block.init_function_name = init_function_name;
            block.init_call_args = args;
            block.init_call_location = SourceLocation{file_path.string(), line_number};

            // Переиспользование одной и той же структуры для нескольких
            // Init-вызовов подряд, без переприсваивания полей между ними:
            //   GPIO_InitStruct.Pin = GPIO_PIN_3; ... ;
            //   HAL_GPIO_Init(GPIO_0, &GPIO_InitStruct);
            //   HAL_GPIO_Init(GPIO_1, &GPIO_InitStruct);
            // (docs/examples/mik32-examples/HAL_Blink/src/main.c:56-57). По
            // семантике C структура сохраняет значения, поэтому второй вызов
            // настраивает GPIO_1 РОВНО так же, как первый — GPIO_1, пин 3,
            // выход. Раньше второй блок уходил в маппер пустым, и тот
            // подставлял фиктивные pins=[0] + mode="input", то есть
            // восстанавливал конфигурацию, которой в прошивке нет. Копируем
            // присваивания предыдущего блока того же хендла.
            const auto previous_block_it = last_block_index.find(matched_handle_name);
            const bool inherited_from_previous_init =
                block.assignments.empty() && previous_block_it != last_block_index.end() &&
                !out.blocks[previous_block_it->second].assignments.empty();

            if (inherited_from_previous_init)
            {
                block.assignments = out.blocks[previous_block_it->second].assignments;
                out.unsupported_notes.push_back(
                    "Вызов " + init_function_name + " для \"" + matched_handle_name +
                    "\" переиспользует структуру предыдущего вызова без переприсваивания "
                    "полей (" + file_path.string() + ":" + std::to_string(line_number) +
                    ") — конфигурация восстановлена по предыдущему блоку, но ОБА блока "
                    "ссылаются на одни и те же строки исходника: правка такого блока в "
                    "GUI затронет и парный блок.");
            }
            else if (block.assignments.empty())
            {
                out.unsupported_notes.push_back(
                    "Вызов " + init_function_name + " для \"" + matched_handle_name +
                    "\" не имеет ни одного распознанного присваивания полей перед ним: " +
                    file_path.string() + ":" + std::to_string(line_number));
            }

            // Донастройка, встреченная выше по файлу, чем сам Init — отдаётся
            // ПЕРВОМУ блоку этого хендла (см. pre_init_calls выше).
            auto pre_init_it = pre_init_calls.find(matched_handle_name);
            if (pre_init_it != pre_init_calls.end())
            {
                block.trailing_calls = std::move(pre_init_it->second);
                pre_init_calls.erase(pre_init_it);
            }

            out.blocks.push_back(std::move(block));
            last_block_index[matched_handle_name] = out.blocks.size() - 1;
        }

        if (matched_any_init)
        {
            continue;
        }

        // Ни одного HAL_*_Init на этой строке — проверяем, не "донастройка"
        // ли это уже созданного блока (см. kGenericCall выше).
        auto call_begin = std::sregex_iterator(code.begin(), code.end(), kGenericCall);
        auto call_end = std::sregex_iterator();
        for (auto call_it = call_begin; call_it != call_end; ++call_it)
        {
            const std::string function_name = (*call_it)[1].str();
            const std::vector<std::string> args = splitArgs((*call_it)[2].str());

            for (const std::string& arg : args)
            {
                auto block_it = last_block_index.find(arg);
                if (block_it != last_block_index.end())
                {
                    out.blocks[block_it->second].trailing_calls.push_back(RawTrailingCall{
                        function_name, args, SourceLocation{file_path.string(), line_number},
                        full_line_text});
                    break;  // один вызов -> одна запись, даже если хендл встретился дважды в аргументах
                }

                // Блока ещё нет, но хендл с таким именем уже объявлен — Init
                // будет ниже по файлу (типичная раскладка вендорных примеров:
                // main() сверху, *_Init() снизу). Запоминаем до появления блока.
                if (handles.count(arg) > 0)
                {
                    pre_init_calls[arg].push_back(RawTrailingCall{
                        function_name, args, SourceLocation{file_path.string(), line_number},
                        full_line_text});
                    break;
                }
            }
        }
    }

    // Хендл объявлен, его поля присваиваются, но HAL_*_Init для него так и не
    // встретился — блок не создан, и раньше об этом НИЧЕГО не сообщалось.
    // Так настроена вся RTC-периферия вендорных примеров: Hal_RTC/src/main.c
    // пишет hrtc.Instance = RTC и вызывает HAL_RTC_SetTime/SetDate, никакого
    // HAL_RTC_Init нет — пример молча давал 0 блоков и 0 notes, что прямо
    // противоречит контракту "сканер обязан объяснять пропуски".
    for (const auto& [handle_name, handle] : handles)
    {
        // PCC_InitTypeDef — тактирование, настраивается через HAL_PCC_Config,
        // не HAL_*_Init; это отдельная вкладка GUI (clock_config_screen), а
        // не периферия reverse-import. Без этого исключения note печатается
        // на каждом из 53 вендорных примеров подряд и забивает реальные notes.
        if (handle.handle_type == "PCC_InitTypeDef")
        {
            continue;
        }
        // DMA_ChannelHandleTypeDef — особый случай: в реальном HAL для
        // канала ПДП нет функции инициализации вообще (mik32_hal_dma.h —
        // есть только HAL_DMA_Init для ГЛОБАЛЬНОГО DMA_InitTypeDef, см.
        // dma_reverse_mapper.h), конфигурация канала — это просто
        // присваивания ChannelInit.* без закрывающего вызова. Без этой
        // ветки каждый канал ПДП во всех вендорных HAL_DMA_* примерах падал
        // бы в unsupported_notes вместо реального блока — тот же класс
        // ограничения, что уже задокументирован для RTC (SetTime/SetDate
        // без Init), но для DMA-канала есть маппер (dma_reverse_mapper.cpp),
        // поэтому блок нужен, а не просто note.
        if (handle.handle_type == "DMA_ChannelHandleTypeDef" && !handle.pending_assignments.empty())
        {
            RawInitBlock block;
            block.handle_name = handle_name;
            block.handle_type = handle.handle_type;
            block.assignments = handle.pending_assignments;
            block.init_call_location = handle.declared_at;
            auto trailing_it = pre_init_calls.find(handle_name);
            if (trailing_it != pre_init_calls.end())
            {
                block.trailing_calls = std::move(trailing_it->second);
                pre_init_calls.erase(trailing_it);
            }
            out.blocks.push_back(std::move(block));
            continue;
        }
        if (!handle.pending_assignments.empty())
        {
            out.unsupported_notes.push_back(
                "Хендл \"" + handle_name + "\" (" + handle.handle_type + ", объявлен в " +
                handle.declared_at.file + ":" +
                std::to_string(handle.declared_at.line) + ") имеет " +
                std::to_string(handle.pending_assignments.size()) +
                " присваиваний полей, но вызова HAL_*_Init для него в этом файле нет — "
                "периферия настраивается другим способом, блок не создан.");
        }
    }
}

} // namespace

ScannedProject scanDirectoryForHalHandles(const std::string& root_dir)
{
    ScannedProject result;

    std::error_code walk_error;
    for (auto it = std::filesystem::recursive_directory_iterator(
             root_dir, std::filesystem::directory_options::skip_permission_denied, walk_error);
         it != std::filesystem::recursive_directory_iterator(); it.increment(walk_error))
    {
        if (walk_error)
        {
            result.unsupported_notes.push_back("Ошибка обхода каталога: " + walk_error.message());
            walk_error.clear();
            continue;
        }

        const std::filesystem::directory_entry& entry = *it;
        if (entry.is_directory())
        {
            if (isIgnoredDirectoryName(entry.path().filename().string()))
            {
                it.disable_recursion_pending();
            }
            continue;
        }

        if (!entry.is_regular_file() || !hasSourceExtension(entry.path()))
        {
            continue;
        }

        result.files_scanned.push_back(entry.path().string());
        scanFile(entry.path(), result);
    }

    return result;
}

} // namespace reverse_import
