#include "cmake_project_codegen.h"
#include "utf8_path.h"
#include "win_file_io.h"
#include "peripheral_contribution.h"
#include "contribution_utils.h"
#include "gpio_contribution.h"
#include "uart_contribution.h"
#include "timer16_contribution.h"
#include "timer32_contribution.h"
#include "i2c_contribution.h"
#include "spi_contribution.h"
#include "user_code_markers.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <sstream>
#include <system_error>
#include <vector>

namespace fs = std::filesystem;

namespace
{
    // --- Дубликаты небольших утилит из project_codegen.cpp -----------------
    // project_codegen.cpp закреплён HW-подтверждёнными golden-фикстурами
    // PlatformIO-пути и по прямому требованию этой сессии не должен
    // измениться НИ НА СТРОКУ — поэтому его private-хелперы (анонимное
    // пространство имён, т.е. внутренняя линковка) нельзя импортировать
    // напрямую. Функции ниже — независимые копии текста, а не рефакторинг.

    // Копия kDmaInitFunctionText (project_codegen.cpp) — обоснование самого
    // текста (образец docs/examples/mik32-examples/HAL_USART_DMA/src/main.c,
    // строки 103-116) там же, повторно не цитируется.
    const char* kDmaInitFunctionText =
        "void DMA_Init(void)\n"
        "{\n"
        "    hdma.Instance = DMA_CONFIG;\n"
        "    hdma.CurrentValue = DMA_CURRENT_VALUE_ENABLE;\n"
        "    HAL_DMA_Init(&hdma);\n"
        "}\n";

    // Копия findConsoleUartIndex (project_codegen.cpp) — обоснование там же.
    int findConsoleUartIndex(const std::vector<UartConfig>& uart_blocks)
    {
        for (size_t i = 0; i < uart_blocks.size(); ++i)
        {
            if (uart_blocks[i].direction == "tx" || uart_blocks[i].direction == "duplex")
            {
                return static_cast<int>(i);
            }
        }
        return -1;
    }

    // Копия appendUnique (project_codegen.cpp) — порядок первого появления
    // важен для воспроизводимости вывода между прогонами.
    void appendUnique(std::vector<std::string>& target, const std::vector<std::string>& source)
    {
        for (const std::string& value : source)
        {
            if (std::find(target.begin(), target.end(), value) == target.end())
            {
                target.push_back(value);
            }
        }
    }

    ParseResult<GeneratedProjectFirmwareCmake> makeError(const std::string& message)
    {
        return ParseResult<GeneratedProjectFirmwareCmake>{false, message, std::nullopt};
    }

    // --- Перестроение вкладов периферии по типам ----------------------------
    // Дублирует ~60 строк "кто из config.*_blocks какой build<X>Contribution
    // зовёт" из project_codegen.cpp::generateProjectFirmware — обоснование
    // компромисса см. в комментарии над generateProjectFirmwareCmake в .h.
    struct ContributionsByType
    {
        std::vector<PeripheralContribution> gpio;
        std::vector<PeripheralContribution> uart;
        std::vector<PeripheralContribution> spi;
        std::vector<PeripheralContribution> i2c;
        std::vector<PeripheralContribution> timer16;
        std::vector<PeripheralContribution> timer32;
        bool any_uart_dma = false;
    };

    ParseResult<ContributionsByType> makeContributionsError(const std::string& message)
    {
        return ParseResult<ContributionsByType>{false, message, std::nullopt};
    }

    ParseResult<ContributionsByType> buildContributionsByType(
        const ProjectConfig& config, const ProjectKnowledge& knowledge, bool emit_demo_loops)
    {
        ContributionsByType result;

        for (const UartConfig& block : config.uart_blocks)
        {
            if (!block.dma.empty())
            {
                result.any_uart_dma = true;
                break;
            }
        }

        // GPIO — группировка по instance, порядок первого появления
        // (см. обоснование в project_codegen.cpp).
        std::vector<int> gpio_instance_order;
        for (const GpioConfig& block : config.gpio_blocks)
        {
            if (std::find(gpio_instance_order.begin(), gpio_instance_order.end(), block.instance)
                == gpio_instance_order.end())
            {
                gpio_instance_order.push_back(block.instance);
            }
        }
        for (int instance : gpio_instance_order)
        {
            std::vector<GpioConfig> blocks_of_instance;
            for (const GpioConfig& block : config.gpio_blocks)
            {
                if (block.instance == instance)
                {
                    blocks_of_instance.push_back(block);
                }
            }
            ParseResult<PeripheralContribution> r =
                buildGpioContribution(blocks_of_instance, knowledge.gpio, emit_demo_loops);
            if (!r.success)
            {
                return makeContributionsError(r.error);
            }
            result.gpio.push_back(*r.config);
        }

        const int console_index = findConsoleUartIndex(config.uart_blocks);
        for (size_t i = 0; i < config.uart_blocks.size(); ++i)
        {
            ParseResult<PeripheralContribution> r = buildUartContribution(
                config.uart_blocks[i], knowledge.uart, static_cast<int>(i) == console_index, emit_demo_loops);
            if (!r.success)
            {
                return makeContributionsError(r.error);
            }
            result.uart.push_back(*r.config);
        }

        const bool console_available = (console_index >= 0);
        for (const SpiConfig& block : config.spi_blocks)
        {
            ParseResult<PeripheralContribution> r =
                buildSpiContribution(block, knowledge.spi, console_available, emit_demo_loops);
            if (!r.success)
            {
                return makeContributionsError(r.error);
            }
            result.spi.push_back(*r.config);
        }

        for (const I2cConfig& block : config.i2c_blocks)
        {
            ParseResult<PeripheralContribution> r =
                buildI2cContribution(block, knowledge.i2c, console_available, emit_demo_loops);
            if (!r.success)
            {
                return makeContributionsError(r.error);
            }
            result.i2c.push_back(*r.config);
        }

        for (const Timer16Config& block : config.timer16_blocks)
        {
            ParseResult<PeripheralContribution> r = buildTimer16Contribution(block, knowledge.timer16);
            if (!r.success)
            {
                return makeContributionsError(r.error);
            }
            result.timer16.push_back(*r.config);
        }

        for (const Timer32Config& block : config.timer32_blocks)
        {
            ParseResult<PeripheralContribution> r = buildTimer32Contribution(block, knowledge.timer32);
            if (!r.success)
            {
                return makeContributionsError(r.error);
            }
            result.timer32.push_back(*r.config);
        }

        return ParseResult<ContributionsByType>{true, "", result};
    }

    struct RenderedPair
    {
        GeneratedFile header;
        GeneratedFile source;
    };

    // Сшивает вклады ОДНОЙ группы (один тип периферии либо timer16+timer32
    // вместе, ТЗ §3.1.7.2) в свою пару <base_name>.h/.c — та же логика
    // сборки секций, что и в project_codegen.cpp::generateProjectFirmware
    // (includes/globals/init_function_text/helper_functions_text), только
    // применённая к подмножеству вкладов вместо всех сразу.
    //
    // extra_* — только для "общего остатка" (DMA_Init/hdma, см. .h) —
    // contributions для него всегда пуст, а extra_* несёт весь контент.
    RenderedPair renderGroupPair(
        const std::string& base_name,
        const std::string& guard_macro,
        const std::vector<PeripheralContribution>& contributions,
        const std::string& extra_prototype,
        const std::vector<std::string>& extra_includes,
        const std::string& extra_globals,
        const std::string& extra_function_text)
    {
        std::ostringstream header;
        header << "#ifndef " << guard_macro << "\n"
                  "#define " << guard_macro << "\n"
                  "\n"
               << extra_prototype;
        for (const PeripheralContribution& contribution : contributions)
        {
            for (const std::string& prototype : contribution.public_prototypes)
            {
                header << prototype << "\n";
            }
        }
        header << "\n#endif //" << guard_macro << "\n";

        std::vector<std::string> includes = extra_includes;
        for (const PeripheralContribution& contribution : contributions)
        {
            appendUnique(includes, contribution.init_c_includes);
        }

        std::ostringstream source;
        source << "/*\n"
                  " * Сгенерировано MIK32-Configurator (CMake, per_type_files=true).\n"
                  " * Файл: " << base_name << ".c";
        if (!contributions.empty())
        {
            source << " — блоков: " << contributions.size() << " —";
            for (size_t i = 0; i < contributions.size(); ++i)
            {
                source << (i == 0 ? " " : ", ") << contributions[i].label;
            }
        }
        source << ".\n"
                  " */\n"
                  "#include \"" << base_name << ".h\"\n";
        for (const std::string& include : includes)
        {
            source << "#include \"" << include << "\"\n";
        }
        source << "\n";

        std::ostringstream globals;
        globals << extra_globals;
        for (const PeripheralContribution& contribution : contributions)
        {
            globals << contribution.global_definitions;
        }
        if (!globals.str().empty())
        {
            source << globals.str() << "\n";
        }

        if (!extra_function_text.empty())
        {
            source << extra_function_text << "\n";
        }

        for (size_t i = 0; i < contributions.size(); ++i)
        {
            if (i > 0)
            {
                source << "\n";
            }
            source << contributions[i].init_function_text;
            if (!contributions[i].helper_functions_text.empty())
            {
                source << "\n" << contributions[i].helper_functions_text;
            }
        }

        RenderedPair result;
        result.header = GeneratedFile{base_name + ".h", header.str()};
        result.source = GeneratedFile{base_name + ".c", source.str()};
        return result;
    }

    // --- Файловые утилиты для writeGeneratedCmakeProject --------------------

    // Каталог-брат output_project_dir, уникальный на каждый вызов (метка
    // времени + счётчик — счётчик страхует от совпадения меток времени при
    // двух вызовах подряд в пределах одного теста).
    std::string uniqueTempDirName(const std::string& output_project_dir)
    {
        static std::atomic<unsigned> counter{0};
        const auto now = std::chrono::steady_clock::now().time_since_epoch();
        const long long ticks = std::chrono::duration_cast<std::chrono::microseconds>(now).count();
        std::ostringstream out;
        out << output_project_dir << ".tmp-generating-" << ticks << "-" << counter.fetch_add(1);
        return out.str();
    }

    // Все файлы с заданным расширением, рекурсивно, реальный обход ФС (а не
    // жёстко зашитый список имён) — тот же каталог SDK может принадлежать
    // разным версиям снэпшота с разным набором файлов.
    std::vector<fs::path> collectFilesByExtension(const fs::path& root, const std::string& extension,
                                                   std::error_code& fs_error)
    {
        std::vector<fs::path> result;
        if (!fs::exists(root, fs_error))
        {
            return result;
        }
        fs::recursive_directory_iterator it(root, fs_error);
        if (fs_error)
        {
            return result;
        }
        for (const fs::recursive_directory_iterator end; it != end; it.increment(fs_error))
        {
            if (fs_error)
            {
                return result;
            }
            if (!it->is_directory(fs_error) && !fs_error && it->path().extension() == extension)
            {
                result.push_back(it->path());
            }
        }
        return result;
    }

    // hal/core/Include, hal/peripherals/Include, hal/utilities/Include —
    // прямые подкаталоги hal/, у которых есть дочерний Include/ (структура
    // ~/.platformio/packages/framework-mik32v2-sdk/hal, сверено вручную:
    // core, peripherals, utilities, у каждого — Include/ и Source/).
    std::vector<fs::path> collectHalIncludeDirs(const fs::path& hal_root, std::error_code& fs_error)
    {
        std::vector<fs::path> result;
        if (!fs::exists(hal_root, fs_error))
        {
            return result;
        }
        fs::directory_iterator it(hal_root, fs_error);
        if (fs_error)
        {
            return result;
        }
        for (const fs::directory_iterator end; it != end; it.increment(fs_error))
        {
            if (fs_error)
            {
                return result;
            }
            if (it->is_directory(fs_error) && !fs_error)
            {
                const fs::path include_dir = it->path() / "Include";
                std::error_code exists_error;
                if (fs::exists(include_dir, exists_error) && !exists_error)
                {
                    result.push_back(include_dir);
                }
            }
        }
        return result;
    }

    // Все каталоги внутри shared/**, рекурсивно (shared/periphery,
    // shared/include, shared/libs, shared/runtime и т.п. — заголовки лежат
    // прямо в них, без единого общего "Include/"), плюс сам shared/.
    std::vector<fs::path> collectAllSubdirsRecursive(const fs::path& root, std::error_code& fs_error)
    {
        std::vector<fs::path> result;
        if (!fs::exists(root, fs_error))
        {
            return result;
        }
        result.push_back(root);
        fs::recursive_directory_iterator it(root, fs_error);
        if (fs_error)
        {
            return result;
        }
        for (const fs::recursive_directory_iterator end; it != end; it.increment(fs_error))
        {
            if (fs_error)
            {
                return result;
            }
            if (it->is_directory(fs_error) && !fs_error)
            {
                result.push_back(it->path());
            }
        }
        return result;
    }

    // Вставляет lines сразу после строки, содержащей marker (текстовый поиск
    // подстроки построчно — без CMake-парсера, ТЗ этой сессии). false, если
    // marker не найден (шаблон CMakeLists.txt повреждён/не тот).
    bool insertAfterMarkerLine(std::string& text, const std::string& marker, const std::vector<std::string>& lines)
    {
        const size_t marker_pos = text.find(marker);
        if (marker_pos == std::string::npos)
        {
            return false;
        }
        size_t line_end = text.find('\n', marker_pos);
        const size_t insert_pos = (line_end == std::string::npos) ? text.size() : line_end + 1;

        std::ostringstream block;
        for (const std::string& line : lines)
        {
            block << "        " << line << "\n";
        }
        text.insert(insert_pos, block.str());
        return true;
    }

    // Экранирование для строкового литерала CMake ( set(VAR "...") ) —
    // обратный слэш и кавычка ломают литерал, если их не экранировать.
    // Актуально для compiler_path на Windows ("C:\tools\...").
    std::string escapeForCMakeString(const std::string& value)
    {
        std::string result;
        result.reserve(value.size());
        for (char c : value)
        {
            if (c == '\\' || c == '"')
            {
                result.push_back('\\');
            }
            result.push_back(c);
        }
        return result;
    }

    // Оборачивает путь в кавычки для передачи в системную оболочку — путь
    // временного каталога может содержать пробелы (домашний каталог
    // пользователя, репозиторий в "Documents", как в этом самом дереве).
    // Экранирует только двойные кавычки внутри пути: put'ём одиночные
    // кавычки в путях этот проект не поддерживает нигде (win_file_io.h тоже
    // работает с путями без такого экранирования).
    std::string quoteForShell(const std::string& path)
    {
        std::string escaped;
        escaped.reserve(path.size());
        for (char c : path)
        {
            if (c == '"')
            {
                escaped += "\\\"";
            }
            else
            {
                escaped.push_back(c);
            }
        }
        return "\"" + escaped + "\"";
    }

    // Перенос дерева tmp_root поверх уже существующего to_root, файл за
    // файлом. protected_relative_paths — файлы, которые НЕ перезаписываются,
    // если уже есть в to_root (CMakeLists.txt, cmake/mik32-riscv-toolchain.cmake
    // — пользователь мог отредактировать их руками после первой генерации,
    // тот же принцип, что и platformio.ini в project_codegen.cpp).
    //
    // Не полностью атомарно (см. .h, «Известные ограничения») — но каждый
    // отдельный rename/copy_file здесь выполняется уже ПОСЛЕ того, как все
    // проверки (структура HAL, маркеры CMakeLists.txt, git clone) успешно
    // прошли во временном каталоге, поэтому вероятность падения на этом шаге
    // минимальна на практике.
    bool mergeMoveInto(const fs::path& tmp_root, const fs::path& to_root,
                       const std::vector<std::string>& protected_relative_paths,
                       std::error_code& fs_error)
    {
        fs::recursive_directory_iterator it(tmp_root, fs_error);
        if (fs_error)
        {
            return false;
        }
        for (const fs::recursive_directory_iterator end; it != end; it.increment(fs_error))
        {
            if (fs_error)
            {
                return false;
            }
            const fs::path relative = fs::relative(it->path(), tmp_root, fs_error);
            if (fs_error)
            {
                return false;
            }
            const std::string relative_generic = relative.generic_string();
            const fs::path destination = to_root / relative;
            const bool is_protected = std::find(protected_relative_paths.begin(), protected_relative_paths.end(),
                                                relative_generic) != protected_relative_paths.end();

            if (it->is_directory(fs_error) && !fs_error)
            {
                fs::create_directories(destination, fs_error);
            }
            else if (!fs_error)
            {
                if (is_protected)
                {
                    std::error_code exists_error;
                    if (fs::exists(destination, exists_error))
                    {
                        // Пользовательский файл уже есть — оставляем как есть,
                        // временную копию просто не переносим.
                        continue;
                    }
                }
                fs::create_directories(destination.parent_path(), fs_error);
                if (fs_error)
                {
                    return false;
                }
                std::error_code remove_error;
                fs::remove(destination, remove_error);
                fs::rename(it->path(), destination, fs_error);
                if (fs_error)
                {
                    // rename может отказать на разных ФС — фолбэк на copy+remove.
                    fs_error.clear();
                    fs::copy_file(it->path(), destination, fs::copy_options::overwrite_existing, fs_error);
                }
            }
            if (fs_error)
            {
                return false;
            }
        }
        return true;
    }
}

ParseResult<GeneratedProjectFirmwareCmake> generateProjectFirmwareCmake(
    const ProjectConfig& config,
    const ProjectKnowledge& knowledge,
    bool emit_demo_loops)
{
    // Валидация конфигурации (типы периферии, конфликты instance/DMA/GPIO
    // IRQ, I2C-сканер без консоли и т.д.) и сборка main.c переиспользуются
    // целиком из уже существующего generateProjectFirmware — не
    // передублированы здесь ни строкой (см. .h).
    ParseResult<GeneratedProjectFirmware> base = generateProjectFirmware(config, knowledge, emit_demo_loops);
    if (!base.success)
    {
        return makeError(base.error);
    }

    GeneratedProjectFirmwareCmake result;

    if (!config.per_type_files)
    {
        result.init_files.push_back(GeneratedFile{"mik32_init.h", base.config->mik32_init_h});
        result.init_files.push_back(GeneratedFile{"mik32_init.c", base.config->mik32_init_c});
        result.main_c = base.config->main_c;
        return ParseResult<GeneratedProjectFirmwareCmake>{true, "", result};
    }

    ParseResult<ContributionsByType> grouped = buildContributionsByType(config, knowledge, emit_demo_loops);
    if (!grouped.success)
    {
        return makeError(grouped.error);
    }
    const ContributionsByType& g = *grouped.config;

    // Заголовки для замены единственного "#include \"mik32_init.h\"" в
    // main.c (взятом как есть из base.config->main_c) — по порядку, в
    // котором сгенерированы сами группы.
    std::vector<std::string> header_includes_for_main;

    if (!g.gpio.empty())
    {
        RenderedPair pair = renderGroupPair("mik32_gpio_init", "MIK32_GPIO_INIT_H", g.gpio, "", {}, "", "");
        header_includes_for_main.push_back(pair.header.relative_path);
        result.init_files.push_back(pair.header);
        result.init_files.push_back(pair.source);
    }

    if (!g.uart.empty())
    {
        // hdma определён в "общем остатке" (mik32_init.c, ниже) — здесь
        // только внешняя ссылка + заголовок с типом DMA_ChannelHandleTypeDef.
        // Само право ссылаться появляется только если хотя бы один блок uart
        // реально запросил dma (g.any_uart_dma); при false extra_* пусты и
        // UART-файл ничего про DMA не знает, как и раньше.
        const std::vector<std::string> extra_includes =
            g.any_uart_dma ? std::vector<std::string>{"mik32_hal_dma.h"} : std::vector<std::string>{};
        const std::string extra_globals = g.any_uart_dma ? "extern DMA_InitTypeDef hdma;\n" : "";
        RenderedPair pair = renderGroupPair("mik32_usart_init", "MIK32_USART_INIT_H", g.uart, "",
            extra_includes, extra_globals, "");
        header_includes_for_main.push_back(pair.header.relative_path);
        result.init_files.push_back(pair.header);
        result.init_files.push_back(pair.source);
    }

    // Timer16+Timer32 в один файл — ТЗ §3.1.7.2 буквально требует объединять
    // оба таймера, а не заводить mik32_timer16_init.c/mik32_timer32_init.c
    // по отдельности.
    std::vector<PeripheralContribution> timer_group = g.timer16;
    timer_group.insert(timer_group.end(), g.timer32.begin(), g.timer32.end());
    if (!timer_group.empty())
    {
        RenderedPair pair = renderGroupPair("mik32_timer_init", "MIK32_TIMER_INIT_H", timer_group, "", {}, "", "");
        header_includes_for_main.push_back(pair.header.relative_path);
        result.init_files.push_back(pair.header);
        result.init_files.push_back(pair.source);
    }

    if (!g.i2c.empty())
    {
        RenderedPair pair = renderGroupPair("mik32_i2c_init", "MIK32_I2C_INIT_H", g.i2c, "", {}, "", "");
        header_includes_for_main.push_back(pair.header.relative_path);
        result.init_files.push_back(pair.header);
        result.init_files.push_back(pair.source);
    }

    if (!g.spi.empty())
    {
        RenderedPair pair = renderGroupPair("mik32_spi_init", "MIK32_SPI_INIT_H", g.spi, "", {}, "", "");
        header_includes_for_main.push_back(pair.header.relative_path);
        result.init_files.push_back(pair.header);
        result.init_files.push_back(pair.source);
    }

    // "Общий остаток" — DMA_Init/hdma, не привязан ни к одному типу
    // периферии (SystemClock_Config сюда не входит: он живёт в main.c и
    // при разбиении на файлы не переносился — main.c не делится на части).
    // Файл существует, только если он реально нужен (any_uart_dma) — пустой
    // mik32_init.h/.c без единого символа был бы мёртвым кодом.
    if (g.any_uart_dma)
    {
        RenderedPair pair = renderGroupPair("mik32_init", "MIK32_INIT_H", {}, "void DMA_Init(void);\n",
            {"mik32_hal_dma.h"}, "DMA_InitTypeDef hdma;\n", kDmaInitFunctionText);
        header_includes_for_main.push_back(pair.header.relative_path);
        result.init_files.push_back(pair.header);
        result.init_files.push_back(pair.source);
    }

    std::string main_c = base.config->main_c;
    const std::string marker = "#include \"mik32_init.h\"\n";
    const size_t marker_pos = main_c.find(marker);
    if (marker_pos != std::string::npos)
    {
        std::ostringstream replacement;
        for (const std::string& header : header_includes_for_main)
        {
            replacement << "#include \"" << header << "\"\n";
        }
        main_c.replace(marker_pos, marker.size(), replacement.str());
    }
    result.main_c = main_c;

    return ParseResult<GeneratedProjectFirmwareCmake>{true, "", result};
}

CodegenWriteResult writeGeneratedCmakeProject(
    const GeneratedProjectFirmwareCmake& firmware,
    const std::string& cmake_skeleton_dir,
    const std::string& sdk_snapshot_dir,
    const std::string& output_project_dir,
    const ProjectConfig& config,
    const nlohmann::json& project_json_copy)
{
    const fs::path tmp_dir = utf8Path(uniqueTempDirName(output_project_dir));

    // На случай осколка от предыдущего упавшего прогона — не должно
    // происходить в норме (см. cleanup ниже), но не должно и мешать новому.
    std::error_code cleanup_leftover_error;
    fs::remove_all(tmp_dir, cleanup_leftover_error);

    auto fail = [&](const std::string& message) -> CodegenWriteResult
    {
        std::error_code ignore;
        fs::remove_all(tmp_dir, ignore);
        return CodegenWriteResult{false, message};
    };

    std::error_code fs_error;
    fs::create_directories(tmp_dir, fs_error);
    if (fs_error)
    {
        return fail("Не удалось создать временный каталог " + tmp_dir.u8string() + ": " + fs_error.message());
    }

    // --- 2a. Скелет CMake-проекта -------------------------------------------
    if (!copyDirectoryTreeSkipExisting(cmake_skeleton_dir, tmp_dir.u8string(), fs_error))
    {
        return fail("Не удалось скопировать шаблон " + cmake_skeleton_dir + ": " + fs_error.message());
    }

    // --- 2b. HAL-исходники ---------------------------------------------------
    const fs::path sdk_dest = tmp_dir / "mik32-sdk";
    fs::create_directories(sdk_dest, fs_error);
    if (fs_error)
    {
        return fail("Не удалось создать " + sdk_dest.u8string() + ": " + fs_error.message());
    }

    if (config.hal_source_mode == HalSourceMode::Bundled)
    {
        const fs::path hal_src = utf8Path(sdk_snapshot_dir) / "hal";
        const fs::path shared_src = utf8Path(sdk_snapshot_dir) / "shared";
        std::error_code exists_error;
        if (!fs::exists(hal_src, exists_error) || !fs::exists(shared_src, exists_error))
        {
            return fail("Снэпшот HAL SDK не найден или не содержит hal/+shared/: " + sdk_snapshot_dir);
        }
        if (!copyDirectoryTreeSkipExisting(hal_src.u8string(), (sdk_dest / "hal").u8string(), fs_error)
            || !copyDirectoryTreeSkipExisting(shared_src.u8string(), (sdk_dest / "shared").u8string(), fs_error))
        {
            return fail("Не удалось скопировать снэпшот HAL SDK из " + sdk_snapshot_dir + ": " + fs_error.message());
        }
    }
    else if (config.hal_source_mode == HalSourceMode::LocalPath)
    {
        if (config.hal_source_local_path.empty())
        {
            return fail("hal_source_mode=local_path, но hal_source_local_path не задан");
        }
        const fs::path local_root = utf8Path(config.hal_source_local_path);
        const fs::path hal_src = local_root / "hal";
        const fs::path shared_src = local_root / "shared";
        std::error_code exists_error;
        if (!fs::exists(local_root, exists_error) || !fs::exists(hal_src, exists_error)
            || !fs::exists(shared_src, exists_error))
        {
            return fail("Каталог hal_source_local_path (" + config.hal_source_local_path
                + ") не существует или не содержит подкаталоги hal/ и shared/");
        }
        if (!copyDirectoryTreeSkipExisting(hal_src.u8string(), (sdk_dest / "hal").u8string(), fs_error)
            || !copyDirectoryTreeSkipExisting(shared_src.u8string(), (sdk_dest / "shared").u8string(), fs_error))
        {
            return fail("Не удалось скопировать HAL из " + config.hal_source_local_path + ": " + fs_error.message());
        }
    }
    else // HalSourceMode::Network
    {
        // Единственный сетевой режим, без offline-фолбэка — осознанное
        // решение (Bundled и LocalPath уже покрывают offline-сценарии), см.
        // docs/implementations/cmake_toolchain.md.
        const fs::path clone_dir = tmp_dir / "mik32-sdk-full";
        const std::string command = "git clone --branch 0.2.2 --depth 1 "
            "https://github.com/MikronMIK32/framework-mik32v2-sdk.git " + quoteForShell(clone_dir.u8string());
        const int clone_status = std::system(command.c_str());
        if (clone_status != 0)
        {
            return fail("git clone framework-mik32v2-sdk не удался (код " + std::to_string(clone_status)
                + ") — проверьте, что git установлен и есть сетевой доступ, "
                  "либо используйте hal_source_mode=bundled/local_path для offline-сборки");
        }
        std::error_code exists_error;
        if (!fs::exists(clone_dir / "hal", exists_error) || !fs::exists(clone_dir / "shared", exists_error))
        {
            return fail("Клонированный репозиторий framework-mik32v2-sdk не содержит ожидаемую структуру hal/+shared/");
        }
        if (!copyDirectoryTreeSkipExisting((clone_dir / "hal").u8string(), (sdk_dest / "hal").u8string(), fs_error)
            || !copyDirectoryTreeSkipExisting((clone_dir / "shared").u8string(), (sdk_dest / "shared").u8string(), fs_error))
        {
            return fail("Не удалось скопировать HAL из клона framework-mik32v2-sdk: " + fs_error.message());
        }
        std::error_code remove_clone_error;
        fs::remove_all(clone_dir, remove_clone_error); // best-effort, не критично для успеха генерации
    }

    // --- 2c. Подстановка в CMakeLists.txt -----------------------------------
    const fs::path cmakelists_path = tmp_dir / "CMakeLists.txt";
    const FileReadOutcome cmakelists_read = readUtf8PathFile(cmakelists_path.u8string());
    if (!cmakelists_read.success)
    {
        return fail("Не удалось прочитать " + cmakelists_path.u8string() + " из шаблона: " + cmakelists_read.diagnostic);
    }

    std::vector<std::string> source_lines;
    for (const GeneratedFile& file : firmware.init_files)
    {
        if (file.relative_path.size() > 2 && file.relative_path.substr(file.relative_path.size() - 2) == ".c")
        {
            source_lines.push_back("src/" + file.relative_path);
        }
    }
    source_lines.push_back("src/main.c");

    // .c- и .S-файлы HAL — реальный обход только что скопированного дерева,
    // не жёстко зашитый список ("реальный список файлов, не выдуманный").
    // .S (crt0.S — стартап-ассемблер, точка входа _start до main) обязателен:
    // без него сборка компилируется, но не линкуется (нет точки входа) —
    // при интеграции параллельных сессий это было замечено как открытый
    // пробел (docs/implementations/cmake_toolchain.md) и теперь закрыто.
    for (const char* extension : {".c", ".S"})
    {
        for (const fs::path& src_file : collectFilesByExtension(sdk_dest, extension, fs_error))
        {
            const fs::path relative = fs::relative(src_file, tmp_dir, fs_error);
            if (fs_error)
            {
                return fail("Не удалось построить относительный путь для " + src_file.u8string() + ": " + fs_error.message());
            }
            source_lines.push_back(relative.generic_string());
        }
    }

    std::vector<std::string> include_lines = {"src"};
    for (const fs::path& include_dir : collectHalIncludeDirs(sdk_dest / "hal", fs_error))
    {
        const fs::path relative = fs::relative(include_dir, tmp_dir, fs_error);
        if (fs_error)
        {
            return fail("Не удалось построить относительный путь для " + include_dir.u8string() + ": " + fs_error.message());
        }
        include_lines.push_back(relative.generic_string());
    }
    for (const fs::path& include_dir : collectAllSubdirsRecursive(sdk_dest / "shared", fs_error))
    {
        const fs::path relative = fs::relative(include_dir, tmp_dir, fs_error);
        if (fs_error)
        {
            return fail("Не удалось построить относительный путь для " + include_dir.u8string() + ": " + fs_error.message());
        }
        include_lines.push_back(relative.generic_string());
    }

    std::string cmakelists_content = cmakelists_read.content;
    if (!insertAfterMarkerLine(cmakelists_content, "# MIK32_CONFIGURATOR: SOURCES", source_lines))
    {
        return fail("Маркер \"# MIK32_CONFIGURATOR: SOURCES\" не найден в шаблоне " + cmakelists_path.u8string());
    }
    if (!insertAfterMarkerLine(cmakelists_content, "# MIK32_CONFIGURATOR: INCLUDE_DIRS", include_lines))
    {
        return fail("Маркер \"# MIK32_CONFIGURATOR: INCLUDE_DIRS\" не найден в шаблоне " + cmakelists_path.u8string());
    }

    if (!config.compiler_path.empty())
    {
        // Вставляется в самое начало файла, до include(cmake/mik32-riscv-
        // toolchain.cmake) — переопределяет автопоиск компилятора, который
        // toolchain-файл (агент Б) делает через проверку этой же переменной.
        cmakelists_content = "set(MIK32_COMPILER_PATH \"" + escapeForCMakeString(config.compiler_path)
            + "\" CACHE FILEPATH \"Путь к RISC-V тулчейну, заданный пользователем в конфигураторе\")\n"
            + cmakelists_content;
    }

    const FileWriteOutcome cmakelists_write = writeUtf8PathFile(cmakelists_path.u8string(), cmakelists_content);
    if (!cmakelists_write.success)
    {
        return fail("Не удалось записать " + cmakelists_path.u8string() + ": " + cmakelists_write.diagnostic);
    }

    // --- 2d. src/ --------------------------------------------------------
    const fs::path tmp_src = tmp_dir / "src";
    fs::create_directories(tmp_src, fs_error);
    if (fs_error)
    {
        return fail("Не удалось создать " + tmp_src.u8string() + ": " + fs_error.message());
    }

    for (const GeneratedFile& file : firmware.init_files)
    {
        const FileWriteOutcome write_result = writeUtf8PathFile((tmp_src / file.relative_path).u8string(), file.content);
        if (!write_result.success)
        {
            return fail("Не удалось записать src/" + file.relative_path + write_result.diagnostic);
        }
    }

    // USER CODE секции сливаются с УЖЕ СУЩЕСТВУЮЩИМ (не временным) main.c —
    // временный каталог всегда пуст, поэтому смотреть в нём не на что.
    std::string main_c_to_write = firmware.main_c;
    const fs::path existing_main_c_path = utf8Path(output_project_dir) / "src" / "main.c";
    if (fs::exists(existing_main_c_path))
    {
        const FileReadOutcome existing = readUtf8PathFile(existing_main_c_path.u8string());
        if (existing.success)
        {
            main_c_to_write = mergeUserCodeSections(firmware.main_c, existing.content);
        }
    }
    const FileWriteOutcome main_c_write = writeUtf8PathFile((tmp_src / "main.c").u8string(), main_c_to_write);
    if (!main_c_write.success)
    {
        return fail("Не удалось записать src/main.c: " + main_c_write.diagnostic);
    }

    // --- 2e. project.mik32 -------------------------------------------------
    const FileWriteOutcome project_write =
        writeUtf8PathFile((tmp_dir / "project.mik32").u8string(), project_json_copy.dump(4) + "\n");
    if (!project_write.success)
    {
        return fail("Не удалось записать project.mik32: " + project_write.diagnostic);
    }

    // --- 3/4. Перенос из временного каталога в output_project_dir ----------
    // CMakeLists.txt и cmake/mik32-riscv-toolchain.cmake — защищены (см.
    // .h): не перезаписываются, если уже существуют в output_project_dir.
    // Всё остальное (mik32-sdk/, src/, project.mik32, прочие файлы шаблона)
    // перезаписывается каждой генерацией.
    static const std::vector<std::string> kProtectedRelativePaths = {
        "CMakeLists.txt", "cmake/mik32-riscv-toolchain.cmake"};

    std::error_code output_exists_error;
    const bool output_exists = fs::exists(utf8Path(output_project_dir), output_exists_error);

    if (!output_exists)
    {
        // Первая генерация в чистый каталог — единственный случай, когда
        // перенос по-настоящему атомарен (одно переименование каталога).
        std::error_code rename_error;
        fs::rename(tmp_dir, utf8Path(output_project_dir), rename_error);
        if (rename_error)
        {
            // rename каталога может отказать при пересечении границы ФС —
            // фолбэк на копирование с последующей очисткой временного.
            std::error_code copy_error;
            if (!copyDirectoryTreeSkipExisting(tmp_dir.u8string(), output_project_dir, copy_error))
            {
                return fail("Не удалось перенести временный каталог в " + output_project_dir + ": " + copy_error.message());
            }
            std::error_code cleanup_error;
            fs::remove_all(tmp_dir, cleanup_error);
        }
    }
    else
    {
        std::error_code merge_error;
        if (!mergeMoveInto(tmp_dir, utf8Path(output_project_dir), kProtectedRelativePaths, merge_error))
        {
            // См. .h — с этой точки полная откатываемость уже не
            // гарантирована (известное и задокументированное ограничение).
            return CodegenWriteResult{false, "Ошибка при переносе файлов в " + output_project_dir
                + " (часть файлов уже могла быть перенесена): " + merge_error.message()};
        }
        std::error_code cleanup_error;
        fs::remove_all(tmp_dir, cleanup_error);
    }

    return CodegenWriteResult{true, ""};
}
