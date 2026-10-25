#include "reverse_block_insert.h"

#include <filesystem>
#include <regex>
#include <sstream>
#include <utility>

#include "project_config.h"
#include "contribution_utils.h"
#include "i2c_contribution.h"
#include "spi_contribution.h"
#include "timer16_contribution.h"
#include "timer32_contribution.h"
#include "dac_contribution.h"
#include "adc_contribution.h"
#include "crc_contribution.h"
#include "crypto_contribution.h"
#include "pvd_contribution.h"
#include "otp_contribution.h"
#include "bus_watchdog_contribution.h"
#include "tsens_contribution.h"
#include "rtc_contribution.h"
#include "gpio_contribution.h"
#include "eeprom_contribution.h"
#include "wdt_contribution.h"
#include "uart_contribution.h"
#include "spifi_contribution.h"
#include "utf8_path.h"
#include "win_file_io.h"

namespace reverse_import
{
namespace
{

namespace fs = std::filesystem;

// Та же атомарная запись, что reverse_patch_writer.cpp (не экспортирована
// оттуда — живёт в его анонимном namespace, см. комментарий там) — короткая
// копия проще, чем менять существующий, уже протестированный файл ради
// одного экспорта.
bool writeFileAtomically(const std::string& path, const std::string& content, std::string& error_out)
{
    const fs::path target = utf8Path(path);
    fs::path temporary = target;
    temporary += ".tmp";

    const FileWriteOutcome write_result = writeUtf8PathFile(temporary.u8string(), content);
    if (!write_result.success)
    {
        std::error_code ignored;
        fs::remove(temporary, ignored);
        error_out = "не удалось записать временный файл: " + temporary.u8string() + write_result.diagnostic;
        return false;
    }
    std::error_code rename_error;
    fs::rename(temporary, target, rename_error);
    if (rename_error)
    {
        std::error_code ignored;
        fs::remove(temporary, ignored);
        error_out = "не удалось заменить файл: " + target.u8string() + " (" + rename_error.message() + ")";
        return false;
    }
    return true;
}

// Находит единственный файл среди files_scanned с данным именем (basename).
// Несколько совпадений (вложенные каталоги с одноимёнными файлами) —
// неоднозначность, берём первый по порядку сканирования; не наш случай в
// типовом проекте (один main.c на прошивку).
std::string findFileByBasename(const std::vector<std::string>& files_scanned, const std::string& basename)
{
    for (const std::string& file : files_scanned)
    {
        if (utf8Path(file).filename().u8string() == basename)
        {
            return file;
        }
    }
    return {};
}

std::vector<std::string> splitLines(const std::string& text)
{
    std::vector<std::string> lines;
    std::size_t start = 0;
    while (start <= text.size())
    {
        const std::size_t newline = text.find('\n', start);
        if (newline == std::string::npos)
        {
            lines.push_back(text.substr(start));
            break;
        }
        lines.push_back(text.substr(start, newline - start));
        start = newline + 1;
    }
    return lines;
}

std::string joinLines(const std::vector<std::string>& lines)
{
    std::string result;
    for (std::size_t i = 0; i < lines.size(); ++i)
    {
        result += lines[i];
        if (i + 1 < lines.size())
        {
            result += "\n";
        }
    }
    return result;
}

// Границы тела функции main() (включительно, [первая строка тела, строка с
// закрывающей "}"]) — подсчётом фигурных скобок от строки "int/void main(".
// Возвращает {-1, -1}, если main() не найдена или скобки не сбалансированы.
std::pair<int, int> findMainFunctionBodyRange(const std::vector<std::string>& lines)
{
    static const std::regex kMainFnPattern(R"(^\s*(int|void)\s+main\s*\()");
    int main_fn_line = -1;
    for (std::size_t i = 0; i < lines.size(); ++i)
    {
        if (std::regex_search(lines[i], kMainFnPattern))
        {
            main_fn_line = static_cast<int>(i);
            break;
        }
    }
    if (main_fn_line < 0)
    {
        return {-1, -1};
    }

    int depth = 0;
    bool seen_open_brace = false;
    for (std::size_t i = static_cast<std::size_t>(main_fn_line); i < lines.size(); ++i)
    {
        for (char ch : lines[i])
        {
            if (ch == '{')
            {
                ++depth;
                seen_open_brace = true;
            }
            else if (ch == '}')
            {
                --depth;
            }
        }
        if (seen_open_brace && depth == 0)
        {
            return {main_fn_line, static_cast<int>(i)};
        }
    }
    return {-1, -1}; // незакрытая main() — не наш случай, не угадываем.
}

// Последняя строка ВНУТРИ тела main() (не всего файла — иначе якорь может
// уехать в постороннюю функцию ниже по файлу, например в HAL_*_MspInit,
// если там тоже есть строка вида "<Имя>_Init(...);") вне
// /* USER CODE BEGIN */.../* USER CODE END */ диапазонов, похожая на вызов
// "<Имя>_Init(...);" — якорь для вставки нового вызова. Возвращает -1, если
// main() не найдена или подходящей строки в её теле нет.
int findLastInitCallLine(const std::vector<std::string>& lines)
{
    const std::pair<int, int> main_range = findMainFunctionBodyRange(lines);
    if (main_range.first < 0)
    {
        return -1;
    }

    static const std::regex kInitCallPattern(R"(^\s*[A-Za-z_][A-Za-z0-9_]*_Init\s*\([^;]*\)\s*;\s*$)");
    bool in_user_code = false;
    int last_match = -1;
    for (int i = main_range.first; i <= main_range.second; ++i)
    {
        const std::string& line = lines[static_cast<std::size_t>(i)];
        if (line.find("USER CODE BEGIN") != std::string::npos)
        {
            in_user_code = true;
        }
        const bool was_in_user_code = in_user_code;
        if (line.find("USER CODE END") != std::string::npos)
        {
            in_user_code = false;
        }
        if (!was_in_user_code && std::regex_match(line, kInitCallPattern))
        {
            last_match = i;
        }
    }
    return last_match;
}

std::string indentOf(const std::string& line)
{
    std::size_t i = 0;
    while (i < line.size() && (line[i] == ' ' || line[i] == '\t'))
    {
        ++i;
    }
    return line.substr(0, i);
}

// Вставляет недостающие #include из `includes` сразу после последней уже
// существующей строки "#include" файла (дедуплицирует по имени заголовка —
// две разные периферии часто делят один and тот же HAL-заголовок).
void insertMissingIncludes(std::vector<std::string>& lines, const std::vector<std::string>& includes)
{
    int last_include_line = -1;
    for (std::size_t i = 0; i < lines.size(); ++i)
    {
        const std::string trimmed = lines[i].substr(indentOf(lines[i]).size());
        if (trimmed.rfind("#include", 0) == 0)
        {
            last_include_line = static_cast<int>(i);
        }
    }
    if (last_include_line < 0)
    {
        return; // не нашли ни одного #include — не угадываем, куда вставлять.
    }

    std::vector<std::string> to_insert;
    for (const std::string& header : includes)
    {
        const std::string needle = "\"" + header + "\"";
        bool already_present = false;
        for (const std::string& line : lines)
        {
            if (line.find(needle) != std::string::npos || line.find("<" + header + ">") != std::string::npos)
            {
                already_present = true;
                break;
            }
        }
        if (!already_present)
        {
            to_insert.push_back("#include \"" + header + "\"");
        }
    }
    lines.insert(lines.begin() + last_include_line + 1, to_insert.begin(), to_insert.end());
}

ParseResult<PeripheralContribution> buildContributionForNewBlock(
    const std::string& type,
    int instance,
    const ProjectConfig& config,
    const ProjectKnowledge& knowledge)
{
    if (type == "i2c")
    {
        for (const I2cConfig& block : config.i2c_blocks)
        {
            if (block.instance == instance)
            {
                return buildI2cContribution(block, knowledge.i2c);
            }
        }
    }
    else if (type == "spi")
    {
        for (const SpiConfig& block : config.spi_blocks)
        {
            if (block.instance == instance)
            {
                return buildSpiContribution(block, knowledge.spi);
            }
        }
    }
    else if (type == "timer16")
    {
        for (const Timer16Config& block : config.timer16_blocks)
        {
            if (block.instance == instance)
            {
                return buildTimer16Contribution(block, knowledge.timer16);
            }
        }
    }
    else if (type == "timer32")
    {
        for (const Timer32Config& block : config.timer32_blocks)
        {
            if (block.instance == instance)
            {
                return buildTimer32Contribution(block, knowledge.timer32);
            }
        }
    }
    else if (type == "dac")
    {
        for (const DacConfig& block : config.dac_blocks)
        {
            if (block.instance == instance)
            {
                return buildDacContribution(block, knowledge.dac);
            }
        }
    }
    // Единственные глобальные блоки (нет instance, см. ProjectConfig::*_block
    // в project_config.h) — instance у них не используется, конфиг один.
    else if (type == "adc" && config.adc_block)
    {
        return buildAdcContribution(*config.adc_block);
    }
    else if (type == "crc" && config.crc_block)
    {
        return buildCrcContribution(*config.crc_block);
    }
    else if (type == "crypto" && config.crypto_block)
    {
        return buildCryptoContribution(*config.crypto_block);
    }
    else if (type == "pvd" && config.pvd_block)
    {
        return buildPvdContribution(*config.pvd_block);
    }
    else if (type == "otp" && config.otp_block)
    {
        return buildOtpContribution(*config.otp_block);
    }
    else if (type == "bus_watchdog" && config.bus_watchdog_block)
    {
        return buildBusWatchdogContribution(*config.bus_watchdog_block);
    }
    else if (type == "tsens" && config.tsens_block)
    {
        return buildTsensContribution(*config.tsens_block);
    }
    else if (type == "eeprom" && config.eeprom_block)
    {
        return buildEepromContribution(*config.eeprom_block);
    }
    else if (type == "wdt" && config.wdt_block)
    {
        return buildWdtContribution(*config.wdt_block);
    }
    else if (type == "spifi" && config.spifi_block)
    {
        return buildSpifiContribution(*config.spifi_block);
    }
    else if (type == "rtc" && config.rtc_block)
    {
        return buildRtcContribution(*config.rtc_block, knowledge.rtc);
    }
    else if (type == "gpio")
    {
        // buildGpioContribution ждёт ВСЕ блоки одного instance сразу (несколько
        // gpio-блоков на один порт — штатно, см. gpio_contribution.h) — здесь
        // вставляется один конкретный новый блок, поэтому вектор из одного
        // элемента; слияние с чужими блоками того же instance, если они уже
        // были в проекте до импорта, не наша забота (их бы и так требовалось
        // мерджить руками — ИИ не вправе угадывать итоговую комбинацию пинов).
        for (const GpioConfig& block : config.gpio_blocks)
        {
            if (block.instance == instance)
            {
                return buildGpioContribution({block}, knowledge.gpio);
            }
        }
    }
    else if (type == "uart" || type == "usart")
    {
        for (const UartConfig& block : config.uart_blocks)
        {
            if (block.instance == instance)
            {
                // is_console=false: точечно добавленный UART не переназначает
                // xputc консоли — одна прошивка может иметь только одну
                // консоль (см. uart_contribution.h), угадывать здесь, что
                // пользователь хотел сменить консольный UART, нельзя.
                return buildUartContribution(block, knowledge.uart, /*is_console=*/false);
            }
        }
    }
    return {false, "точечная вставка нового блока типа \"" + type + "\" не поддержана (поддержаны: "
        "i2c, spi, timer16, timer32, dac, adc, crc, crypto, pvd, otp, bus_watchdog, tsens, "
        "eeprom, wdt, spifi, rtc, gpio, uart/usart — dma не поддержан: общий DMA_Init() "
        "собирается отдельно в project_codegen.cpp на основе ВСЕХ DMA-источников проекта "
        "сразу, а не из одного contribution — точечная вставка не может безопасно "
        "воспроизвести эту сборку без риска получить не ту прошивку, что ожидает "
        "пользователь; нужна полная кодогенерация, см. \"Перегенерировать проект\")",
        std::nullopt};
}

} // namespace

BlockInsertResult insertNewPeripheralBlock(
    const std::string& root_dir,
    const std::vector<std::string>& files_scanned,
    const nlohmann::json& current_document,
    int block_index,
    const ProjectKnowledge& knowledge)
{
    (void)root_dir;

    if (block_index < 0 || block_index >= static_cast<int>(current_document.value(
            "peripherals", nlohmann::json::array()).size()))
    {
        return {false, "некорректный индекс блока"};
    }

    const nlohmann::json& new_block = current_document["peripherals"][block_index];
    const std::string type = new_block.value("type", std::string());
    const int instance = new_block.value("instance", -1);

    ParseResult<ProjectConfig> parsed = parseProjectDocument(current_document);
    if (!parsed.success)
    {
        return {false, "не удалось разобрать текущую конфигурацию: " + parsed.error};
    }

    ParseResult<PeripheralContribution> contribution_result =
        buildContributionForNewBlock(type, instance, *parsed.config, knowledge);
    if (!contribution_result.success)
    {
        return {false, contribution_result.error};
    }
    const PeripheralContribution& contribution = *contribution_result.config;

    const std::string main_c_path = findFileByBasename(files_scanned, "main.c");
    if (main_c_path.empty())
    {
        return {false, "в импортированном проекте не найден main.c — точечная вставка "
            "нового блока невозможна без входной точки прошивки"};
    }
    const std::string init_c_path = findFileByBasename(files_scanned, "mik32_init.c");
    const std::string init_h_path = findFileByBasename(files_scanned, "mik32_init.h");
    const bool has_standard_init_files = !init_c_path.empty() && !init_h_path.empty();

    // --- main.c: якорь для вызова Init() -----------------------------------
    const FileReadOutcome main_c_read = readUtf8PathFile(main_c_path);
    if (!main_c_read.success)
    {
        return {false, "не удалось прочитать main.c: " + main_c_read.diagnostic};
    }
    std::vector<std::string> main_lines = splitLines(main_c_read.content);
    const int anchor_line = findLastInitCallLine(main_lines);
    if (anchor_line < 0)
    {
        return {false, "в main.c не найдено ни одного существующего вызова *_Init() вне "
            "USER CODE блоков — не могу определить, куда вставить вызов нового блока"};
    }

    const std::string indent = indentOf(main_lines[static_cast<std::size_t>(anchor_line)]);
    std::vector<std::string> call_lines;
    if (!contribution.extern_declarations.empty())
    {
        for (const std::string& extern_line : splitLines(contribution.extern_declarations))
        {
            if (!extern_line.empty())
            {
                call_lines.push_back(indent + extern_line);
            }
        }
    }
    {
        // Та же маркировка "<label>_Call", что ставит project_codegen.cpp при
        // полной генерации — точечная вставка должна давать идентичный
        // результат, иначе повторная перегенерация не найдёт секцию.
        const std::string wrapped = wrapInitCallWithUserCodeMarkers(
            contribution.label, indent + contribution.init_function_name + "();", indent);
        std::vector<std::string> wrapped_lines = splitLines(wrapped);
        if (!wrapped_lines.empty() && wrapped_lines.back().empty())
        {
            wrapped_lines.pop_back(); // splitLines оставляет пустой "хвост" после финального \n
        }
        call_lines.insert(call_lines.end(), wrapped_lines.begin(), wrapped_lines.end());
    }
    main_lines.insert(main_lines.begin() + anchor_line + 1, call_lines.begin(), call_lines.end());

    insertMissingIncludes(main_lines, contribution.main_c_includes);

    if (!has_standard_init_files)
    {
        // Вендорский проект без mik32_init.c/h (нет общего стандарта, куда класть
        // инициализацию) — функцию определяем прямо в main.c перед int main(...),
        // без отдельного заголовка: определение выше вызова не требует прототипа.
        static const std::regex kMainFnPattern(R"(^\s*(int|void)\s+main\s*\()");
        int main_fn_line = -1;
        for (std::size_t i = 0; i < main_lines.size(); ++i)
        {
            if (std::regex_search(main_lines[i], kMainFnPattern))
            {
                main_fn_line = static_cast<int>(i);
                break;
            }
        }
        if (main_fn_line < 0)
        {
            return {false, "в main.c не найдена функция main() — не могу определить, куда "
                "вставить определение новой функции инициализации"};
        }

        std::string appended_code;
        if (!contribution.global_definitions.empty())
        {
            appended_code += contribution.global_definitions + "\n";
        }
        appended_code += wrapInitFunctionWithUserCodeMarkers(contribution.label, contribution.init_function_text);
        if (!contribution.helper_functions_text.empty())
        {
            appended_code += "\n" + contribution.helper_functions_text;
        }
        appended_code += "\n";
        std::vector<std::string> function_lines = splitLines(appended_code);
        main_lines.insert(main_lines.begin() + main_fn_line, function_lines.begin(), function_lines.end());

        std::string write_error;
        if (!writeFileAtomically(main_c_path, joinLines(main_lines), write_error))
        {
            return {false, "не удалось записать main.c: " + write_error};
        }
        return {true, ""};
    }

    // --- mik32_init.c: новая функция в конец файла --------------------------
    const FileReadOutcome init_c_read = readUtf8PathFile(init_c_path);
    if (!init_c_read.success)
    {
        return {false, "не удалось прочитать mik32_init.c: " + init_c_read.diagnostic};
    }
    std::vector<std::string> init_c_lines = splitLines(init_c_read.content);
    insertMissingIncludes(init_c_lines, contribution.init_c_includes);

    std::string appended_code = "\n";
    if (!contribution.global_definitions.empty())
    {
        appended_code += contribution.global_definitions + "\n";
    }
    appended_code += wrapInitFunctionWithUserCodeMarkers(contribution.label, contribution.init_function_text);
    if (!contribution.helper_functions_text.empty())
    {
        appended_code += "\n" + contribution.helper_functions_text;
    }
    std::string init_c_content = joinLines(init_c_lines);
    init_c_content += appended_code;

    // --- mik32_init.h: прототип перед последним #endif -----------------------
    const FileReadOutcome init_h_read = readUtf8PathFile(init_h_path);
    if (!init_h_read.success)
    {
        return {false, "не удалось прочитать mik32_init.h: " + init_h_read.diagnostic};
    }
    std::vector<std::string> init_h_lines = splitLines(init_h_read.content);
    int last_endif_line = -1;
    for (std::size_t i = 0; i < init_h_lines.size(); ++i)
    {
        std::string trimmed = init_h_lines[i];
        const std::size_t first_non_space = trimmed.find_first_not_of(" \t");
        if (first_non_space != std::string::npos && trimmed.compare(first_non_space, 6, "#endif") == 0)
        {
            last_endif_line = static_cast<int>(i);
        }
    }
    if (last_endif_line < 0)
    {
        return {false, "в mik32_init.h не найден #endif — не могу определить, куда вставить "
            "прототип нового блока"};
    }
    std::vector<std::string> prototype_lines(
        contribution.public_prototypes.begin(), contribution.public_prototypes.end());
    init_h_lines.insert(init_h_lines.begin() + last_endif_line, prototype_lines.begin(), prototype_lines.end());

    std::string write_error;
    if (!writeFileAtomically(main_c_path, joinLines(main_lines), write_error))
    {
        return {false, "не удалось записать main.c: " + write_error};
    }
    if (!writeFileAtomically(init_c_path, init_c_content, write_error))
    {
        return {false, "не удалось записать mik32_init.c: " + write_error};
    }
    if (!writeFileAtomically(init_h_path, joinLines(init_h_lines), write_error))
    {
        return {false, "не удалось записать mik32_init.h: " + write_error};
    }

    return {true, ""};
}

} // namespace reverse_import
