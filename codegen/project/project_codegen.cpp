#include "project_codegen.h"
#include "utf8_path.h"
#include "win_file_io.h"
#include "peripheral_contribution.h"
#include "contribution_utils.h"
#include "gpio_contribution.h"
#include "uart_contribution.h"
#include "timer16_contribution.h"
#include "i2c_contribution.h"
#include "timer32_contribution.h"
#include "spi_contribution.h"
#include "freertos_codegen.h"
#include "user_code_markers.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <system_error>
#include <set>
#include <sstream>
#include <vector>

namespace fs = std::filesystem;

namespace
{
    // Задержка в конце тела цикла для модулей, попросивших её через
    // wants_loop_delay. 500 мс — значение из уже проверенной на плате
    // демонстрации GPIO+UART (docs/implementations/gpio_uart_demo.md):
    // мигание различимо глазом, а строки в терминале не сливаются в поток.
    constexpr const char* kLoopDelayMs = "500";

    // Добавляет строки в список, пропуская уже имеющиеся, и сохраняя порядок
    // первого появления. Порядок важен не для компилятора, а для
    // воспроизводимости вывода: golden-файлы в тестах сравниваются побайтово,
    // а std::set переставил бы заголовки в алфавитном порядке, оторвав их от
    // порядка модулей.
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

    std::string renderIncludes(const std::vector<std::string>& includes)
    {
        std::ostringstream out;
        for (const std::string& include : includes)
        {
            out << "#include \"" << include << "\"\n";
        }
        return out.str();
    }

    // SystemClock_Config зашита дефолтным блоком из
    // docs/examples/mik32-examples/HAL_Blink/src/main.c (строки 27-43):
    // конфигуратор пока не умеет настраивать PCC как отдельную периферию, а
    // без HAL_PCC_Config сгенерированный проект не получит корректного
    // тактирования вообще. До этого модуля один и тот же блок был скопирован
    // в пять генераторов; теперь он здесь в единственном экземпляре.
    const char* kSystemClockConfigDefinition =
        "void SystemClock_Config(void)\n"
        "{\n"
        "    PCC_InitTypeDef PCC_OscInit = {0};\n"
        "\n"
        "    PCC_OscInit.OscillatorEnable = PCC_OSCILLATORTYPE_ALL;\n"
        "    PCC_OscInit.FreqMon.OscillatorSystem = PCC_OSCILLATORTYPE_OSC32M;\n"
        "    PCC_OscInit.FreqMon.ForceOscSys = PCC_FORCE_OSC_SYS_UNFIXED;\n"
        "    PCC_OscInit.FreqMon.Force32KClk = PCC_FREQ_MONITOR_SOURCE_OSC32K;\n"
        "    PCC_OscInit.AHBDivider = 0;\n"
        "    PCC_OscInit.APBMDivider = 0;\n"
        "    PCC_OscInit.APBPDivider = 0;\n"
        "    PCC_OscInit.HSI32MCalibrationValue = 128;\n"
        "    PCC_OscInit.LSI32KCalibrationValue = 8;\n"
        "    PCC_OscInit.RTCClockSelection = PCC_RTC_CLOCK_SOURCE_AUTO;\n"
        "    PCC_OscInit.RTCClockCPUSelection = PCC_CPU_RTC_CLOCK_SOURCE_OSC32K;\n"
        "    HAL_PCC_Config(&PCC_OscInit);\n"
        "}\n";

    // Единственный на весь проект DMA_Init — DMA_CONFIG физически один
    // блок с 4 общими каналами (dma_config.h, DMA_CHANNEL_COUNT), а не
    // по одному на периферию, поэтому вызывается один раз, аналогично
    // SystemClock_Config. По образцу docs/examples/mik32-examples/
    // HAL_USART_DMA/src/main.c, функция DMA_Init (строки 103-116):
    // CurrentValue=ENABLE — читать текущее состояние канала, а не значения
    // настройки (иначе HAL_DMA_GetChannelReadyStatus давал бы неактуальные
    // данные). Генерируется, только если хотя бы один блок uart запросил
    // dma — иначе #include "mik32_hal_dma.h" и неиспользуемый глобал
    // были бы мёртвым кодом.
    const char* kDmaInitFunctionText =
        "void DMA_Init(void)\n"
        "{\n"
        "    hdma.Instance = DMA_CONFIG;\n"
        "    hdma.CurrentValue = DMA_CURRENT_VALUE_ENABLE;\n"
        "    HAL_DMA_Init(&hdma);\n"
        "}\n";

    ParseResult<GeneratedProjectFirmware> makeError(const std::string& message)
    {
        return ParseResult<GeneratedProjectFirmware>{false, message, std::nullopt};
    }

    // Консолью xprintf назначается первый по порядку в конфиге блок uart, у
    // которого включена передача: xputc печатает через HAL_USART_Transmit, а
    // при direction="rx" передатчик выключен и печать молча пропала бы.
    // Возвращает индекс в config.uart_blocks либо -1.
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

    // Копирование скелета проекта «поверх» каталога назначения, без затирания
    // файлов, которые пользователь мог отредактировать после первой генерации.
    //
    // Здесь нельзя обойтись одним вызовом
    //   fs::copy(from, to, recursive | skip_existing):
    // если каталог `to` уже создан, libstdc++ из MinGW GCC 11.2.0 возвращает
    // ошибку "File exists", тогда как GCC 13.3.0 на Linux тот же вызов
    // выполняет молча. Это проверено отдельным репро-тестом: при
    // несуществующем `to` обе реализации дают OK, при заранее созданном `to`
    // виндовая сборка падает. По [fs.op.copy] опция skip_existing описана для
    // файлов, а не для самого каталога назначения, поэтому рассчитывать на
    // снисходительность конкретной реализации — ошибка переносимости.
    //
    // Поэтому обходим дерево скелета сами: каталоги создаём через
    // create_directories (идемпотентно везде), файлы копируем поштучно с
    // skip_existing — семантика «не трогать уже существующее» сохраняется.
    bool copySkeletonInto(const std::string& skeleton_dir,
                          const std::string& output_project_dir,
                          std::error_code& fs_error)
    {
        fs::recursive_directory_iterator it(utf8Path(skeleton_dir), fs_error);
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

            // Путь элемента относительно корня скелета переносится один в один
            // внутрь каталога проекта: templates/pio_skeleton/.vscode/x.json
            // -> <проект>/.vscode/x.json.
            const fs::path relative = fs::relative(it->path(), utf8Path(skeleton_dir), fs_error);
            if (fs_error)
            {
                return false;
            }
            const fs::path destination = utf8Path(output_project_dir) / relative;

            if (it->is_directory(fs_error))
            {
                fs::create_directories(destination, fs_error);
            }
            else if (!fs_error)
            {
                fs::copy_file(it->path(), destination, fs::copy_options::skip_existing, fs_error);
            }

            if (fs_error)
            {
                return false;
            }
        }

        return true;
    }

    // Транзакционная запись src/: тот же принцип, что и у нового CMake-пути
    // (docs/implementations/dma.md, cmake_project_codegen.cpp — пишем во
    // временный каталог-брат, при успехе переносим, при ошибке удаляем
    // временный каталог целиком), перенесённый на уже существующий
    // PlatformIO-путь. До этой правки mik32_init.h/.c/main.c писались
    // последовательно прямо в src_dir: сбой на третьем файле (main.c) уже
    // оставлял бы src_dir с двумя перезаписанными файлами и без отката —
    // нарушение ТЗ §3.2.2 (транзакционность записи).

    // Уникальное имя временного каталога-брата src/ — sibling внутри
    // output_project_dir, а не подкаталог src/, поэтому его создание не
    // зависит от того, существует ли (или чем является) сам src_dir на
    // диске: если src — обычный файл, а не каталог (сценарий "сбой на
    // середине" в тестах), запись во временный каталог всё равно проходит
    // успешно, и ошибка проявляется только на шаге переноса в src_dir —
    // именно там, где она проявилась бы и в реальности.
    fs::path makeUniqueTempSiblingDir(const fs::path& src_dir)
    {
        static std::atomic<unsigned long long> counter{0};
        const auto now_ns = static_cast<unsigned long long>(
            std::chrono::steady_clock::now().time_since_epoch().count());
        fs::path candidate;
        do
        {
            const unsigned long long suffix = now_ns ^ (++counter);
            candidate = src_dir;
            candidate += ("_tmp_" + std::to_string(suffix));
        }
        while (fs::exists(candidate));
        return candidate;
    }

    // Пишет mik32_init.h/.c/main.c в temp_dir. При первой же ошибке
    // возвращает диагностику — вызывающий код удаляет temp_dir целиком, не
    // трогая src_dir вообще (записи в src_dir на этом шаге ещё не было).
    CodegenWriteResult writeFilesIntoTempDir(
        const fs::path& temp_dir,
        const GeneratedProjectFirmware& firmware,
        const std::string& main_c_to_write)
    {
        std::vector<std::pair<const char*, const std::string*>> files_to_write = {
            {"mik32_init.h", &firmware.mik32_init_h},
            {"mik32_init.c", &firmware.mik32_init_c},
            {"main.c", &main_c_to_write},
        };
        // FreeRTOSConfig.h — часть той же транзакции, только когда FreeRTOS
        // включён (freertos_config_h непусто ровно в этом случае, см.
        // generateProjectFirmware); без FreeRTOS список остаётся прежним
        // из трёх файлов, полностью совместимым с golden-тестами PlatformIO-пути.
        if (!firmware.freertos_config_h.empty())
        {
            files_to_write.push_back({"FreeRTOSConfig.h", &firmware.freertos_config_h});
        }

        for (const auto& [file_name, content] : files_to_write)
        {
            // Запись всегда бинарная (см. writeUtf8PathFile/readUtf8PathFile):
            // текстовый режим на Windows разворачивает каждый '\n' в CRLF, и
            // один и тот же конфиг даёт на Windows и на Linux побайтово
            // разные mik32_init.c/main.c (проверено сравнением выводов двух
            // сборок — расходились ровно переводы строк). Кодогенератор
            // обязан быть детерминированным относительно платформы:
            // golden-фикстуры tests/fixtures/expected_*.c хранятся с LF, а
            // сгенерированный проект пользователь кладёт под контроль версий.
            const FileWriteOutcome write_result =
                writeUtf8PathFile((temp_dir / file_name).u8string(), *content);
            if (!write_result.success)
            {
                return CodegenWriteResult{
                    false, std::string("Не удалось записать файл src/") + file_name + write_result.diagnostic};
            }
        }

        return CodegenWriteResult{true, ""};
    }

    // Переносит уже полностью записанные файлы из temp_dir в src_dir.
    //
    // ОГРАНИЧЕНИЕ (та же оговорка, что и у cmake_project_codegen.cpp,
    // docs/implementations/dma.md): полная атомарность гарантирована только
    // при первой генерации в новый/пустой src_dir. При регенерации поверх
    // уже существующих файлов перенос идёт файл за файлом через fs::rename;
    // если он оборвётся на середине (например, второй файл не переносится
    // из-за внешнего вмешательства в файловую систему между вызовами), уже
    // перенесённые файлы останутся перенесёнными — это НЕ облачная
    // транзакция с откатом уже перемещённого. Что гарантировано в любом
    // случае: пока сама запись во временный каталог не завершилась успехом
    // целиком, src_dir не тронут вообще (см. writeFilesIntoTempDir выше).
    CodegenWriteResult moveTempDirFilesIntoSrcDir(const fs::path& temp_dir, const fs::path& src_dir)
    {
        std::error_code fs_error;
        fs::create_directories(src_dir, fs_error);
        if (fs_error)
        {
            return CodegenWriteResult{false, "Не удалось создать каталог src/: " + fs_error.message()};
        }

        // Список файлов берётся из содержимого temp_dir (не фиксированным
        // массивом): FreeRTOSConfig.h присутствует там, только если её
        // записал writeFilesIntoTempDir (FreeRTOS включён) — этот шаг обязан
        // перенести ровно то, что реально было записано. Ручной цикл с
        // it.increment(fs_error), а не range-based for: у второго между
        // итерациями вызывается бросающий исключение operator++(), а весь
        // остальной код файла принципиально работает через error_code
        // (см. copySkeletonInto выше).
        fs::directory_iterator it(temp_dir, fs_error);
        if (fs_error)
        {
            return CodegenWriteResult{false, "Не удалось перечислить временный каталог: " + fs_error.message()};
        }
        for (const fs::directory_iterator end; it != end; it.increment(fs_error))
        {
            if (fs_error)
            {
                return CodegenWriteResult{false, "Не удалось перечислить временный каталог: " + fs_error.message()};
            }
            const fs::path file_name = it->path().filename();
            fs::rename(it->path(), src_dir / file_name, fs_error);
            if (fs_error)
            {
                return CodegenWriteResult{
                    false, "Не удалось перенести файл src/" + file_name.u8string() + ": " + fs_error.message()};
            }
        }

        return CodegenWriteResult{true, ""};
    }
}

ParseResult<GeneratedProjectFirmware> generateProjectFirmware(
    const ProjectConfig& config,
    const ProjectKnowledge& knowledge,
    bool emit_demo_loops)
{
    std::vector<PeripheralContribution> contributions;

    // ТЗ §3.1.3.3: DMA настраивается на блоках uart, но сам DMA_CONFIG —
    // один физический блок на весь МК (4 общих канала), поэтому его
    // инициализация не делегируется contribution-у конкретного блока (см.
    // kDmaInitFunctionText ниже).
    bool any_uart_dma = false;
    for (const UartConfig& block : config.uart_blocks)
    {
        if (!block.dma.empty())
        {
            any_uart_dma = true;
            break;
        }
    }

    // --- GPIO: блоки группируются по instance -------------------------------
    // Обоснование группировки — codegen/project/gpio_contribution.h: несколько
    // блоков на один порт задают разным пинам разные режимы, и все они должны
    // попасть в одну функцию GPIO_<instance>_Init. Порядок instance берётся по
    // первому появлению в конфиге, а не сортировкой: пользователь читает свой
    // JSON сверху вниз и ожидает такой же порядок в сгенерированном коде.
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
        ParseResult<PeripheralContribution> result =
            buildGpioContribution(blocks_of_instance, knowledge.gpio, emit_demo_loops);
        if (!result.success)
        {
            return makeError(result.error);
        }
        contributions.push_back(*result.config);
    }

    // --- UART ---------------------------------------------------------------
    const int console_index = findConsoleUartIndex(config.uart_blocks);
    for (size_t i = 0; i < config.uart_blocks.size(); ++i)
    {
        ParseResult<PeripheralContribution> result = buildUartContribution(
            config.uart_blocks[i], knowledge.uart,
            static_cast<int>(i) == console_index, emit_demo_loops);
        if (!result.success)
        {
            return makeError(result.error);
        }
        contributions.push_back(*result.config);
    }

    // --- SPI ------------------------------------------------------------
    const bool console_available = (console_index >= 0);
    for (const SpiConfig& block : config.spi_blocks)
    {
        ParseResult<PeripheralContribution> result =
            buildSpiContribution(block, knowledge.spi, console_available, emit_demo_loops);
        if (!result.success)
        {
            return makeError(result.error);
        }
        contributions.push_back(*result.config);
    }

    // --- I2C ----------------------------------------------------------------
    for (const I2cConfig& block : config.i2c_blocks)
    {
        ParseResult<PeripheralContribution> result =
            buildI2cContribution(block, knowledge.i2c, console_available, emit_demo_loops);
        if (!result.success)
        {
            return makeError(result.error);
        }
        contributions.push_back(*result.config);
    }

    // --- Timer16 ------------------------------------------------------------
    for (const Timer16Config& block : config.timer16_blocks)
    {
        ParseResult<PeripheralContribution> result =
            buildTimer16Contribution(block, knowledge.timer16);
        if (!result.success)
        {
            return makeError(result.error);
        }
        contributions.push_back(*result.config);
    }

    // --- Timer32 ------------------------------------------------------------
    for (const Timer32Config& block : config.timer32_blocks)
    {
        ParseResult<PeripheralContribution> result =
            buildTimer32Contribution(block, knowledge.timer32);
        if (!result.success)
        {
            return makeError(result.error);
        }
        contributions.push_back(*result.config);
    }

    // freertos.enabled=true допускает нулевую периферию (см. project_config.cpp)
    // — тогда contributions пуст на законных основаниях, mik32_init.h/.c
    // получатся пустыми (безвредно), а main.c соберёт FreeRTOS-ветка ниже.
    if (contributions.empty() && !config.freertos.enabled)
    {
        return makeError("Нет ни одного блока периферии для генерации");
    }

    // Сортировка стабильная: внутри одного init_order (например, три блока
    // Timer16) сохраняется порядок, в котором они шли в конфиге.
    std::stable_sort(contributions.begin(), contributions.end(),
        [](const PeripheralContribution& left, const PeripheralContribution& right)
        {
            return left.init_order < right.init_order;
        });

    GeneratedProjectFirmware firmware;

    // --- mik32_init.h -------------------------------------------------------
    std::ostringstream init_h;
    init_h << "#ifndef MIK32_INIT_H\n"
              "#define MIK32_INIT_H\n"
              "\n";
    if (any_uart_dma)
    {
        init_h << "void DMA_Init(void);\n";
    }
    for (const PeripheralContribution& contribution : contributions)
    {
        for (const std::string& prototype : contribution.public_prototypes)
        {
            init_h << prototype << "\n";
        }
    }
    init_h << "\n#endif //MIK32_INIT_H\n";
    firmware.mik32_init_h = init_h.str();

    // --- mik32_init.c -------------------------------------------------------
    std::vector<std::string> init_c_includes;
    if (any_uart_dma)
    {
        init_c_includes.push_back("mik32_hal_dma.h");
    }
    for (const PeripheralContribution& contribution : contributions)
    {
        appendUnique(init_c_includes, contribution.init_c_includes);
    }

    std::ostringstream init_c;
    init_c << "/*\n"
              " * Сгенерировано MIK32-Configurator.\n";
    if (!config.mcu.empty())
    {
        init_c << " * МК: " << config.mcu;
        if (!config.version.empty())
        {
            init_c << ", версия конфигурации: " << config.version;
        }
        init_c << ".\n";
    }
    init_c << " * Блоков периферии: " << contributions.size() << " —";
    for (size_t i = 0; i < contributions.size(); ++i)
    {
        init_c << (i == 0 ? " " : ", ") << contributions[i].label;
    }
    init_c << ".\n"
              " */\n"
              "#include \"mik32_init.h\"\n"
           << renderIncludes(init_c_includes)
           << "\n";

    // Глобальные хендлы — единым блоком перед функциями, как во всех
    // вендорных примерах (docs/examples/mik32-examples/*/src/main.c).
    std::ostringstream globals;
    if (any_uart_dma)
    {
        // Объявлен раньше per-contribution хендлов: DMA_ChannelHandleTypeDef
        // блоков uart (codegen/project/uart_contribution.cpp) ссылается на
        // &hdma в своём global_definitions, а C транслирует файл сверху вниз.
        globals << "DMA_InitTypeDef hdma;\n";
    }
    for (const PeripheralContribution& contribution : contributions)
    {
        globals << contribution.global_definitions;
    }
    if (!globals.str().empty())
    {
        init_c << globals.str() << "\n";
    }

    if (any_uart_dma)
    {
        init_c << kDmaInitFunctionText << "\n";
    }

    for (size_t i = 0; i < contributions.size(); ++i)
    {
        if (i > 0)
        {
            init_c << "\n";
        }
        init_c << contributions[i].init_function_text;
        if (!contributions[i].helper_functions_text.empty())
        {
            init_c << "\n" << contributions[i].helper_functions_text;
        }
    }
    firmware.mik32_init_c = init_c.str();

    // --- main.c -------------------------------------------------------------
    // mik32_hal_pcc.h нужен всегда — SystemClock_Config вызывает
    // HAL_PCC_Config и использует PCC_InitTypeDef.
    std::vector<std::string> main_c_includes = {"mik32_hal_pcc.h"};
    for (const PeripheralContribution& contribution : contributions)
    {
        appendUnique(main_c_includes, contribution.main_c_includes);
    }
    appendUnique(main_c_includes, {"mik32_init.h"});

    std::ostringstream externs;
    for (const PeripheralContribution& contribution : contributions)
    {
        externs << contribution.extern_declarations;
    }

    // --- ветка FreeRTOS (grill-сессия 2026-09-10) ---------------------------
    // buildFreeRtosProjectPieces вызывается ДО принятия решения про
    // loop_body/wants_loop_delay ниже: FreeRTOS-веткой они не используются
    // вовсе (demo-цикл через while(1) не генерируется, пока планировщик не
    // возвращается) — это не отдельный флаг, а прямое следствие того, что
    // FreeRTOS-ветка main.c просто не читает contribution.loop_body.
    FreeRtosProjectPieces freertos_pieces;
    if (config.freertos.enabled)
    {
        std::vector<std::string> gpio_irq_handler_function_names;
        for (const PeripheralContribution& contribution : contributions)
        {
            if (!contribution.irq_handler_function_name.empty())
            {
                gpio_irq_handler_function_names.push_back(contribution.irq_handler_function_name);
            }
        }
        ParseResult<FreeRtosProjectPieces> freertos_result =
            buildFreeRtosProjectPieces(config.freertos, console_available, gpio_irq_handler_function_names);
        if (!freertos_result.success)
        {
            return makeError(freertos_result.error);
        }
        freertos_pieces = *freertos_result.config;
        firmware.freertos_config_h = freertos_pieces.freertos_config_h;
        appendUnique(main_c_includes, freertos_pieces.main_c_includes);
    }

    std::ostringstream main_c;
    main_c << renderIncludes(main_c_includes)
           << "\n"
              "/* USER CODE BEGIN Includes */\n"
              "/* USER CODE END Includes */\n"
              "\n";
    if (!externs.str().empty())
    {
        main_c << externs.str() << "\n";
    }
    if (config.freertos.enabled)
    {
        main_c << freertos_pieces.globals_and_prototypes;
    }
    main_c << "void SystemClock_Config(void);\n"
              "\n"
              "int main()\n"
              "{\n"
              "    SystemClock_Config();\n"
              "\n";
    if (any_uart_dma)
    {
        // До UART_x_Init: HAL_DMA_Init(&hdma) должен отработать раньше, чем
        // любой контроллер начнёт ссылаться на &hdma в своём канале.
        main_c << "    DMA_Init();\n";
    }
    for (const PeripheralContribution& contribution : contributions)
    {
        main_c << "    " << contribution.init_function_name << "();\n";
    }
    main_c << "\n"
              "    /* USER CODE BEGIN Init */\n"
              "    /* USER CODE END Init */\n"
              "\n";

    if (config.freertos.enabled)
    {
        // Ни demo-цикла, ни while(1): vTaskStartScheduler() не возвращается
        // в штатной работе (grill-решение 2026-09-10) — peripheral
        // contribution.loop_body здесь сознательно не читается.
        main_c << freertos_pieces.object_and_task_creation
               << "\n"
                  "    vTaskStartScheduler();\n"
                  "\n"
                  "    while (1)\n"
                  "    {\n"
                  "    }\n"
                  "}\n"
                  "\n"
               << kSystemClockConfigDefinition
               << "\n"
               << freertos_pieces.task_and_timer_definitions
               << "\n"
               << freertos_pieces.trap_handler_glue;
        firmware.main_c = main_c.str();
        return ParseResult<GeneratedProjectFirmware>{true, "", firmware};
    }

    bool any_loop_delay = false;
    std::ostringstream loop_bodies;
    for (const PeripheralContribution& contribution : contributions)
    {
        if (contribution.loop_body.empty())
        {
            continue;
        }
        // Своя область видимости на каждый вклад — обоснование в
        // codegen/project/peripheral_contribution.h (иначе одноимённые
        // локальные переменные двух demo-логик столкнулись бы).
        loop_bodies << "        /* " << contribution.label << " */\n"
                       "        {\n"
                    << contribution.loop_body
                    << "        }\n";
        any_loop_delay = any_loop_delay || contribution.wants_loop_delay;
    }

    main_c << "    while (1)\n"
              "    {\n"
              // Маркеры ТЗ §3.1.7.3: всё тело цикла (включая demo-логику)
              // лежит ВНУТРИ секции, чтобы пользователь мог заменить его
              // целиком своим кодом, и замена пережила бы регенерацию.
              "        /* USER CODE BEGIN Loop */\n"
           << loop_bodies.str();
    if (any_loop_delay)
    {
        main_c << "        HAL_DelayMs(" << kLoopDelayMs << ");\n";
    }
    main_c << "        /* USER CODE END Loop */\n"
              "    }\n"
              "}\n"
              "\n"
           << kSystemClockConfigDefinition;
    firmware.main_c = main_c.str();

    return ParseResult<GeneratedProjectFirmware>{true, "", firmware};
}

CodegenWriteResult writeGeneratedProject(
    const GeneratedProjectFirmware& firmware,
    const std::string& skeleton_dir,
    const std::string& output_project_dir,
    const FreeRtosConfig& freertos_config,
    const std::string& freertos_kernel_snapshot_dir)
{
    std::error_code fs_error;

    // Скелет (platformio.ini/.vscode/lib/test) копируется только один раз —
    // повторная генерация в тот же каталог не должна затирать файлы, которые
    // пользователь мог отредактировать вручную после первого запуска.
    if (!fs::exists(utf8Path(output_project_dir) / "platformio.ini"))
    {
        // Корень проекта создаём явно: generated_projects/ в свежем
        // репозитории ещё не существует, а copySkeletonInto создаёт только
        // подкаталоги, найденные внутри скелета.
        fs::create_directories(utf8Path(output_project_dir), fs_error);
        if (fs_error)
        {
            return CodegenWriteResult{false, "Не удалось создать " + output_project_dir + ": " + fs_error.message()};
        }

        if (!copySkeletonInto(skeleton_dir, output_project_dir, fs_error))
        {
            return CodegenWriteResult{false, "Не удалось скопировать " + skeleton_dir + ": " + fs_error.message()};
        }
    }

    // FreeRTOS-Kernel — независимая от src/-транзакции копия (как и сам
    // skeleton_dir выше): идемпотентна по построению (copyDirectoryTreeSkipExisting),
    // поэтому без внешнего "уже скопировано" guard'а — повторные вызовы либо
    // ничего не делают (всё уже на месте), либо докатывают то, чего не было.
    if (freertos_config.enabled)
    {
        const CodegenWriteResult kernel_write =
            writeFreeRtosKernelSource(freertos_config, freertos_kernel_snapshot_dir, output_project_dir);
        if (!kernel_write.success)
        {
            return kernel_write;
        }
    }

    const fs::path src_dir = utf8Path(output_project_dir) / "src";

    // USER CODE-слияние читает СУЩЕСТВУЮЩИЙ main.c из настоящего src_dir —
    // это чтение, а не запись, поэтому вне транзакции. Если src_dir ещё не
    // существует или существует, но не как каталог (например, обычный файл
    // с этим именем — сценарий сбоя, проверяемый тестами), fs::exists на
    // составном пути src_dir/main.c просто возвращает false, и слияние
    // отрабатывает так же, как для первой генерации — исключений нет.
    std::string main_c_to_write = firmware.main_c;
    const fs::path main_c_path = src_dir / "main.c";
    if (fs::exists(main_c_path))
    {
        const FileReadOutcome existing = readUtf8PathFile(main_c_path.u8string());
        if (existing.success)
        {
            main_c_to_write = mergeUserCodeSections(firmware.main_c, existing.content);
        }
    }

    // --- Транзакция: временный каталог-брат src/ ---------------------------
    // 1) пишем все три файла во временный каталог; 2) при любой ошибке —
    // удаляем временный каталог целиком и выходим, не тронув src_dir; 3) при
    // полном успехе — переносим файлы во src_dir (создавая его при
    // необходимости) и убираем временный каталог.
    const fs::path temp_dir = makeUniqueTempSiblingDir(src_dir);
    fs::create_directories(temp_dir, fs_error);
    if (fs_error)
    {
        return CodegenWriteResult{false, "Не удалось создать временный каталог для записи src/: " + fs_error.message()};
    }

    const CodegenWriteResult write_result = writeFilesIntoTempDir(temp_dir, firmware, main_c_to_write);
    if (!write_result.success)
    {
        // Откат: временный каталог не пережил ни одного файла в src_dir —
        // src_dir остаётся ровно таким, каким был до вызова.
        std::error_code cleanup_error;
        fs::remove_all(temp_dir, cleanup_error);
        return write_result;
    }

    const CodegenWriteResult move_result = moveTempDirFilesIntoSrcDir(temp_dir, src_dir);
    std::error_code cleanup_error;
    fs::remove_all(temp_dir, cleanup_error);
    if (!move_result.success)
    {
        return move_result;
    }

    return CodegenWriteResult{true, ""};
}
