#include "project_codegen.h"
#include "peripheral_contribution.h"
#include "contribution_utils.h"
#include "gpio_contribution.h"
#include "uart_contribution.h"
#include "timer16_contribution.h"
#include "i2c_contribution.h"
#include "user_code_markers.h"
#include <algorithm>
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
        fs::recursive_directory_iterator it(skeleton_dir, fs_error);
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
            const fs::path relative = fs::relative(it->path(), skeleton_dir, fs_error);
            if (fs_error)
            {
                return false;
            }
            const fs::path destination = fs::path(output_project_dir) / relative;

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
}

ParseResult<GeneratedProjectFirmware> generateProjectFirmware(
    const ProjectConfig& config,
    const ProjectKnowledge& knowledge,
    bool emit_demo_loops)
{
    std::vector<PeripheralContribution> contributions;

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

    // --- I2C ----------------------------------------------------------------
    const bool console_available = (console_index >= 0);
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

    if (contributions.empty())
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
    for (const PeripheralContribution& contribution : contributions)
    {
        globals << contribution.global_definitions;
    }
    if (!globals.str().empty())
    {
        init_c << globals.str() << "\n";
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
    main_c << "void SystemClock_Config(void);\n"
              "\n"
              "int main()\n"
              "{\n"
              "    SystemClock_Config();\n"
              "\n";
    for (const PeripheralContribution& contribution : contributions)
    {
        main_c << "    " << contribution.init_function_name << "();\n";
    }
    main_c << "\n"
              "    /* USER CODE BEGIN Init */\n"
              "    /* USER CODE END Init */\n"
              "\n"
              "    while (1)\n"
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
    const std::string& output_project_dir)
{
    std::error_code fs_error;

    // Скелет (platformio.ini/.vscode/lib/test) копируется только один раз —
    // повторная генерация в тот же каталог не должна затирать файлы, которые
    // пользователь мог отредактировать вручную после первого запуска.
    if (!fs::exists(fs::path(output_project_dir) / "platformio.ini"))
    {
        // Корень проекта создаём явно: generated_projects/ в свежем
        // репозитории ещё не существует, а copySkeletonInto создаёт только
        // подкаталоги, найденные внутри скелета.
        fs::create_directories(output_project_dir, fs_error);
        if (fs_error)
        {
            return CodegenWriteResult{false, "Не удалось создать " + output_project_dir + ": " + fs_error.message()};
        }

        if (!copySkeletonInto(skeleton_dir, output_project_dir, fs_error))
        {
            return CodegenWriteResult{false, "Не удалось скопировать " + skeleton_dir + ": " + fs_error.message()};
        }
    }

    const fs::path src_dir = fs::path(output_project_dir) / "src";
    fs::create_directories(src_dir, fs_error);
    if (fs_error)
    {
        return CodegenWriteResult{false, "Не удалось создать каталог src/: " + fs_error.message()};
    }

    std::string main_c_to_write = firmware.main_c;
    const fs::path main_c_path = src_dir / "main.c";
    if (fs::exists(main_c_path))
    {
        std::ifstream existing(main_c_path, std::ios::binary);
        std::ostringstream existing_buffer;
        existing_buffer << existing.rdbuf();
        main_c_to_write = mergeUserCodeSections(firmware.main_c, existing_buffer.str());
    }

    const std::pair<const char*, const std::string*> files_to_write[] = {
        {"mik32_init.h", &firmware.mik32_init_h},
        {"mik32_init.c", &firmware.mik32_init_c},
        {"main.c", &main_c_to_write},
    };

    for (const auto& [file_name, content] : files_to_write)
    {
        // std::ios::binary обязателен: без него на Windows текстовый режим
        // разворачивает каждый '\n' в CRLF, и один и тот же конфиг даёт на
        // Windows и на Linux побайтово разные mik32_init.c/main.c (проверено
        // сравнением выводов двух сборок — расходились ровно переводы строк).
        // Кодогенератор обязан быть детерминированным относительно платформы:
        // golden-фикстуры tests/fixtures/expected_*.c хранятся с LF, а
        // сгенерированный проект пользователь кладёт под контроль версий.
        std::ofstream out(src_dir / file_name, std::ios::trunc | std::ios::binary);
        if (!out.is_open())
        {
            return CodegenWriteResult{false, std::string("Не удалось записать файл src/") + file_name};
        }
        out << *content;
    }

    return CodegenWriteResult{true, ""};
}
