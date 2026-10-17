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
#include "rtc_contribution.h"
#include "crc_contribution.h"
#include "eeprom_contribution.h"
#include "wdt_contribution.h"
#include "dac_contribution.h"
#include "adc_contribution.h"
#include "crypto_contribution.h"
#include "otp_contribution.h"
#include "tsens_contribution.h"
#include "pvd_contribution.h"
#include "bus_watchdog_contribution.h"
#include "dma_contribution.h"
#include "freertos_codegen.h"
#include "user_code_markers.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <regex>
#include <system_error>
#include <set>
#include <sstream>
#include <vector>

namespace fs = std::filesystem;

namespace
{
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

    // SystemClock_Config — изначально зашита дефолтным блоком из
    // docs/examples/mik32-examples/HAL_Blink/src/main.c (строки 27-43);
    // с ночной сессии 12->13.10.2026 (OVERNIGHT_FIELD_REFERENCE.md)
    // OscillatorSystem/делители читаются из ProjectConfig.clock, остальные
    // поля PCC_InitTypeDef остаются тем же хардкодом (FreqMon.ForceOscSys,
    // калибровочные значения HSI32M/LSI32K, RTC — вне скоупа этой сессии).
    // До этого модуля один и тот же блок был скопирован в пять генераторов;
    // теперь он здесь в единственном экземпляре.
    // AUDIT_TZ_PERIPHERY_GAPS.md, Clock/PCC: HAL-имена для oscillator_enable
    // (PCC_OSCILLATORTYPE_*, mik32_hal_pcc.h:20-24) — маска собирается через OR.
    std::string oscillatorEnableMacro(const std::string& name)
    {
        if (name == "hsi32m") return "PCC_OSCILLATORTYPE_HSI32M";
        if (name == "osc32m") return "PCC_OSCILLATORTYPE_OSC32M";
        if (name == "lsi32k") return "PCC_OSCILLATORTYPE_LSI32K";
        return "PCC_OSCILLATORTYPE_OSC32K";
    }

    // mik32_hal_pcc.h, HAL_PCC_RTCClockSourceTypeDef (строки 55-60) и
    // HAL_PCC_FreqMonitorSourceTypeDef (строки 43-48) — те же три значения
    // auto/lsi32k/osc32k, но разные enum'ы (разные макросы).
    std::string rtcClockSourceMacro(const std::string& value)
    {
        if (value == "lsi32k") return "PCC_RTC_CLOCK_SOURCE_LSI32K";
        if (value == "osc32k") return "PCC_RTC_CLOCK_SOURCE_OSC32K";
        return "PCC_RTC_CLOCK_SOURCE_AUTO";
    }

    std::string freqMonitorSourceMacro(const std::string& value)
    {
        if (value == "lsi32k") return "PCC_FREQ_MONITOR_SOURCE_LSI32K";
        if (value == "osc32k") return "PCC_FREQ_MONITOR_SOURCE_OSC32K";
        return "PCC_FREQ_MONITOR_SOURCE_AUTO";
    }

    std::string renderSystemClockConfig(const ClockConfig& clock)
    {
        // mik32_hal_pcc.h, HAL_PCC_OscillatorTypeTypeDef.
        const std::string oscillator_system_literal =
            (clock.oscillator_system == "hsi32m") ? "PCC_OSCILLATORTYPE_HSI32M" : "PCC_OSCILLATORTYPE_OSC32M";

        std::ostringstream oscillator_enable_mask;
        for (size_t i = 0; i < clock.oscillator_enable.size(); ++i)
        {
            if (i > 0) oscillator_enable_mask << " | ";
            oscillator_enable_mask << oscillatorEnableMacro(clock.oscillator_enable[i]);
        }

        std::ostringstream out;
        // Errata MIK32V2 v1.0.1, §12.4.1 «Поправочный коэффициент HSI32M»:
        // «Значения... лежат в диапазоне от 16 до 255. При значениях ниже 16
        // работа HSI32M не гарантируется» — Предупреждение (не Ошибка, тот
        // же принцип, что у SPI errata §7.1/Timer32 §9.1): кодоген не
        // блокирует значения 0-15 (HAL-документированный диапазон PCC_InitTypeDef
        // шире, 0-255), но помечает их комментарием.
        if (clock.hsi32m_calibration < 16)
        {
            out << "/*\n"
                   " * Errata п. 12.4.1 (mik32v2errataV1_0_1_1 v1.0.1): HSI32MCalibrationValue="
                << clock.hsi32m_calibration << " ниже рекомендованного минимума 16 —\n"
                   " * «при значениях ниже 16 работа HSI32M не гарантируется». Увеличьте значение\n"
                   " * до диапазона 16-255, если используете HSI32M как источник тактирования.\n"
                   " */\n";
        }
        out << "void SystemClock_Config(void)\n"
               "{\n"
               "    PCC_InitTypeDef PCC_OscInit = {0};\n"
               "\n"
               "    PCC_OscInit.OscillatorEnable = " << oscillator_enable_mask.str() << ";\n"
               "    PCC_OscInit.FreqMon.OscillatorSystem = " << oscillator_system_literal << ";\n"
               "    PCC_OscInit.FreqMon.ForceOscSys = "
            << (clock.force_osc_sys ? "PCC_FORCE_OSC_SYS_FIXED" : "PCC_FORCE_OSC_SYS_UNFIXED") << ";\n"
               "    PCC_OscInit.FreqMon.Force32KClk = " << freqMonitorSourceMacro(clock.force_32k_clk) << ";\n"
               "    PCC_OscInit.AHBDivider = " << clock.ahb_divider << ";\n"
               "    PCC_OscInit.APBMDivider = " << clock.apbm_divider << ";\n"
               "    PCC_OscInit.APBPDivider = " << clock.apbp_divider << ";\n"
               "    PCC_OscInit.HSI32MCalibrationValue = " << clock.hsi32m_calibration << ";\n"
               "    PCC_OscInit.LSI32KCalibrationValue = " << clock.lsi32k_calibration << ";\n"
               "    PCC_OscInit.RTCClockSelection = " << rtcClockSourceMacro(clock.rtc_clock_source) << ";\n"
               "    PCC_OscInit.RTCClockCPUSelection = "
            << (clock.rtc_clock_cpu_source == "lsi32k" ? "PCC_CPU_RTC_CLOCK_SOURCE_LSI32K" : "PCC_CPU_RTC_CLOCK_SOURCE_OSC32K") << ";\n"
               "    HAL_PCC_Config(&PCC_OscInit);\n"
               "}\n";
        return out.str();
    }

    // Единственный на весь проект DMA_Init — DMA_CONFIG физически один
    // блок с 4 общими каналами (dma_config.h, DMA_CHANNEL_COUNT), а не
    // по одному на периферию, поэтому вызывается один раз, аналогично
    // SystemClock_Config. По образцу docs/examples/mik32-examples/
    // HAL_USART_DMA/src/main.c, функция DMA_Init (строки 103-116):
    // CurrentValue=ENABLE — читать текущее состояние канала, а не значения
    // настройки (иначе HAL_DMA_GetChannelReadyStatus давал бы неактуальные
    // данные). Генерируется, только если хотя бы один блок uart запросил
    // dma или есть хотя бы один standalone-блок type="dma" (any_dma_usage) —
    // иначе #include "mik32_hal_dma.h" и неиспользуемый глобал были бы
    // мёртвым кодом.
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

    // OVERNIGHT_FIELD_REFERENCE.md: простой построчный дифф по общему
    // префиксу/суффиксу (не полный Myers/LCS — для типичного случая "поменяли
    // несколько полей конфига" этого достаточно и читается человеком проще,
    // чем символьный дифф). Возвращает удалённые/добавленные строки средней
    // несовпадающей части.
    struct SimpleLineDiff
    {
        std::vector<std::string> removed;
        std::vector<std::string> added;
    };

    std::vector<std::string> splitLines(const std::string& text)
    {
        std::vector<std::string> lines;
        std::istringstream iss(text);
        std::string line;
        while (std::getline(iss, line))
        {
            lines.push_back(line);
        }
        return lines;
    }

    SimpleLineDiff computeSimpleLineDiff(const std::string& old_text, const std::string& new_text)
    {
        const std::vector<std::string> old_lines = splitLines(old_text);
        const std::vector<std::string> new_lines = splitLines(new_text);

        size_t prefix = 0;
        while (prefix < old_lines.size() && prefix < new_lines.size()
               && old_lines[prefix] == new_lines[prefix])
        {
            ++prefix;
        }

        size_t old_suffix = old_lines.size();
        size_t new_suffix = new_lines.size();
        while (old_suffix > prefix && new_suffix > prefix
               && old_lines[old_suffix - 1] == new_lines[new_suffix - 1])
        {
            --old_suffix;
            --new_suffix;
        }

        SimpleLineDiff diff;
        diff.removed.assign(old_lines.begin() + static_cast<long>(prefix), old_lines.begin() + static_cast<long>(old_suffix));
        diff.added.assign(new_lines.begin() + static_cast<long>(prefix), new_lines.begin() + static_cast<long>(new_suffix));
        return diff;
    }

    // Текст CHANGELOG_GENERATED.md — перезаписывается при каждой генерации
    // (не накопительный лог), отражает только дифф с НЕПОСРЕДСТВЕННО
    // предыдущей генерацией этого же каталога. first_generation=true, если
    // mik32_init.c не существовал до этого вызова (обычный файл-маркер
    // "раньше тут ничего не было", см. вызывающий код).
    std::string renderChangelog(
        bool first_generation,
        const SimpleLineDiff& init_h_diff,
        const SimpleLineDiff& init_c_diff,
        const SimpleLineDiff& main_c_diff)
    {
        if (first_generation)
        {
            return "# CHANGELOG_GENERATED.md\n\nПервая генерация этого проекта — сравнивать не с чем.\n";
        }

        auto anyChanges = [](const SimpleLineDiff& diff) { return !diff.removed.empty() || !diff.added.empty(); };
        if (!anyChanges(init_h_diff) && !anyChanges(init_c_diff) && !anyChanges(main_c_diff))
        {
            return "# CHANGELOG_GENERATED.md\n\nПо сравнению с предыдущей генерацией ничего не изменилось "
                   "(конфиг и/или код сгенерировали побайтово тот же результат).\n";
        }

        std::ostringstream out;
        out << "# CHANGELOG_GENERATED.md\n\n"
               "Дифф с непосредственно предыдущей генерацией этого же проекта (не весь"
               " накопленный лог — только последнее изменение). Автоматический построчный"
               " дифф по сгенерированным файлам, не по исходному JSON-конфигу.\n\n";

        auto renderFileDiff = [&out](const char* file_name, const SimpleLineDiff& diff)
        {
            if (diff.removed.empty() && diff.added.empty())
            {
                return;
            }
            out << "## " << file_name << "\n\n";
            for (const std::string& line : diff.removed)
            {
                out << "- " << line << "\n";
            }
            for (const std::string& line : diff.added)
            {
                out << "+ " << line << "\n";
            }
            out << "\n";
        };

        renderFileDiff("mik32_init.h", init_h_diff);
        renderFileDiff("mik32_init.c", init_c_diff);
        renderFileDiff("main.c", main_c_diff);

        return out.str();
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
    const ProjectKnowledge& knowledge)
{
    std::vector<PeripheralContribution> contributions;

    // ТЗ §3.1.3.3: DMA настраивается на блоках uart ИЛИ standalone-блоками
    // type="dma" (docs/implementations/dma.md), но сам DMA_CONFIG — один
    // физический блок на весь МК (4 общих канала), поэтому его
    // инициализация не делегируется contribution-у конкретного блока (см.
    // kDmaInitFunctionText ниже). any_dma_usage объединяет оба механизма —
    // общий hdma/DMA_Init() должен попасть в прошивку один раз независимо
    // от того, кто именно из них запросил DMA.
    bool any_dma_usage = !config.dma_blocks.empty();
    for (const UartConfig& block : config.uart_blocks)
    {
        if (!block.dma.empty())
        {
            any_dma_usage = true;
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
            buildGpioContribution(blocks_of_instance, knowledge.gpio);
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
            static_cast<int>(i) == console_index);
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
            buildSpiContribution(block, knowledge.spi);
        if (!result.success)
        {
            return makeError(result.error);
        }
        contributions.push_back(*result.config);
    }

    // --- RTC (AUDIT_TZ_PERIPHERY_GAPS.md, Тир 2) — optional, не vector -------
    if (config.rtc_block.has_value())
    {
        ParseResult<PeripheralContribution> result =
            buildRtcContribution(*config.rtc_block, knowledge.rtc);
        if (!result.success)
        {
            return makeError(result.error);
        }
        contributions.push_back(*result.config);
    }

    // --- CRC (AUDIT_TZ_PERIPHERY_GAPS.md, Тир 2) — optional, не vector -------
    if (config.crc_block.has_value())
    {
        ParseResult<PeripheralContribution> result = buildCrcContribution(*config.crc_block);
        if (!result.success)
        {
            return makeError(result.error);
        }
        contributions.push_back(*result.config);
    }

    // --- EEPROM (AUDIT_TZ_PERIPHERY_GAPS.md, Тир 2) — optional, не vector ----
    if (config.eeprom_block.has_value())
    {
        ParseResult<PeripheralContribution> result = buildEepromContribution(*config.eeprom_block);
        if (!result.success)
        {
            return makeError(result.error);
        }
        contributions.push_back(*result.config);
    }

    // --- WDT (AUDIT_TZ_PERIPHERY_GAPS.md, Тир 2) — optional, не vector -------
    if (config.wdt_block.has_value())
    {
        ParseResult<PeripheralContribution> result = buildWdtContribution(*config.wdt_block);
        if (!result.success)
        {
            return makeError(result.error);
        }
        contributions.push_back(*result.config);
    }

    // --- DAC (AUDIT_TZ_PERIPHERY_GAPS.md, Тир 2) ------------------------------
    for (const DacConfig& block : config.dac_blocks)
    {
        ParseResult<PeripheralContribution> result = buildDacContribution(block, knowledge.dac);
        if (!result.success)
        {
            return makeError(result.error);
        }
        contributions.push_back(*result.config);
    }

    // --- ADC/CRYPTO/OTP/TSENS (AUDIT_TZ_PERIPHERY_GAPS.md, Тир 2) ------------
    if (config.adc_block.has_value())
    {
        ParseResult<PeripheralContribution> result = buildAdcContribution(*config.adc_block);
        if (!result.success) return makeError(result.error);
        contributions.push_back(*result.config);
    }
    if (config.crypto_block.has_value())
    {
        ParseResult<PeripheralContribution> result = buildCryptoContribution(*config.crypto_block);
        if (!result.success) return makeError(result.error);
        contributions.push_back(*result.config);
    }
    if (config.otp_block.has_value())
    {
        ParseResult<PeripheralContribution> result = buildOtpContribution(*config.otp_block);
        if (!result.success) return makeError(result.error);
        contributions.push_back(*result.config);
    }
    if (config.tsens_block.has_value())
    {
        ParseResult<PeripheralContribution> result = buildTsensContribution(*config.tsens_block);
        if (!result.success) return makeError(result.error);
        contributions.push_back(*result.config);
    }
    if (config.pvd_block.has_value())
    {
        ParseResult<PeripheralContribution> result = buildPvdContribution(*config.pvd_block);
        if (!result.success) return makeError(result.error);
        contributions.push_back(*result.config);
    }
    if (config.bus_watchdog_block.has_value())
    {
        ParseResult<PeripheralContribution> result = buildBusWatchdogContribution(*config.bus_watchdog_block);
        if (!result.success) return makeError(result.error);
        contributions.push_back(*result.config);
    }
    // --- standalone DMA-каналы (docs/implementations/dma.md) ----------------
    for (const DmaConfig& block : config.dma_blocks)
    {
        ParseResult<PeripheralContribution> result = buildDmaContribution(block);
        if (!result.success) return makeError(result.error);
        contributions.push_back(*result.config);
    }

    // --- I2C ----------------------------------------------------------------
    for (const I2cConfig& block : config.i2c_blocks)
    {
        ParseResult<PeripheralContribution> result =
            buildI2cContribution(block, knowledge.i2c);
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

    // HAL-заголовки всех использованных блоков. Нужны и mik32_init.c, и
    // mik32_init.h: заголовок подключает их сам (как main.h в STM32CubeMX)
    // вместе с mik32_hal.h, иначе код пользователя в main.c между маркерами USER CODE не видит
    // HAL-API — например, HAL_GPIO_TogglePin(GPIO_2, GPIO_PIN_6) не
    // компилируется: в main.c подключены только mik32_hal_pcc.h и mik32_init.h.
    std::vector<std::string> init_c_includes;
    if (any_dma_usage)
    {
        init_c_includes.push_back("mik32_hal_dma.h");
    }
    for (const PeripheralContribution& contribution : contributions)
    {
        appendUnique(init_c_includes, contribution.init_c_includes);
    }

    // --- mik32_init.h -------------------------------------------------------
    std::ostringstream init_h;
    init_h << "#ifndef MIK32_INIT_H\n"
              "#define MIK32_INIT_H\n"
              "\n";
    // mik32_hal.h — общий HAL (HAL_DelayMs/HAL_DelayUs и т.п.): код
    // пользователя в main.c почти всегда ждёт, а сами блоки его не тянут.
    std::vector<std::string> init_h_includes = {"mik32_hal.h"};
    appendUnique(init_h_includes, init_c_includes);
    init_h << renderIncludes(init_h_includes) << "\n";
    if (any_dma_usage)
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
    if (any_dma_usage)
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

    if (any_dma_usage)
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
    if (any_dma_usage)
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
        // Ни while(1), ни USER CODE Loop: vTaskStartScheduler() не
        // возвращается в штатной работе (grill-решение 2026-09-10).
        main_c << freertos_pieces.object_and_task_creation
               << "\n"
                  "    vTaskStartScheduler();\n"
                  "\n"
                  "    while (1)\n"
                  "    {\n"
                  "    }\n"
                  "}\n"
                  "\n"
               << renderSystemClockConfig(config.clock)
               << "\n"
               << freertos_pieces.task_and_timer_definitions
               << "\n"
               << freertos_pieces.trap_handler_glue;
        firmware.main_c = main_c.str();
        return ParseResult<GeneratedProjectFirmware>{true, "", firmware};
    }

    main_c << "    while (1)\n"
              "    {\n"
              // Маркеры ТЗ §3.1.7.3: тело цикла целиком лежит ВНУТРИ секции,
              // чтобы пользователь мог заполнить его своим кодом, и
              // содержимое пережило бы регенерацию.
              "        /* USER CODE BEGIN Loop */\n"
              "        /* USER CODE END Loop */\n"
              "    }\n"
              "}\n"
              "\n"
           << renderSystemClockConfig(config.clock);
    firmware.main_c = main_c.str();

    return ParseResult<GeneratedProjectFirmware>{true, "", firmware};
}

namespace
{
    // Порт программатора, на который всегда прошивает сгенерированный проект.
    const char* const kUploadPort = "/dev/ttyUSB0";

    // Приводит --com=... в upload_command к kUploadPort. Применяется и к
    // свежескопированному скелету, и к platformio.ini, оставшемуся от прошлой
    // генерации: иначе старые проекты так и прошивали бы на прежний порт.
    CodegenWriteResult enforceUploadPort(const std::string& output_project_dir)
    {
        const std::string ini_path = (utf8Path(output_project_dir) / "platformio.ini").u8string();
        const FileReadOutcome ini = readUtf8PathFile(ini_path);
        if (!ini.success)
        {
            return CodegenWriteResult{false, "Не удалось прочитать " + ini_path + ini.diagnostic};
        }
        static const std::regex com_option(R"(--com=\S+)");
        const std::string patched =
            std::regex_replace(ini.content, com_option, std::string("--com=") + kUploadPort);
        if (patched == ini.content)
        {
            return CodegenWriteResult{true, ""};
        }
        const FileWriteOutcome written = writeUtf8PathFile(ini_path, patched);
        if (!written.success)
        {
            return CodegenWriteResult{false, "Не удалось записать " + ini_path + written.diagnostic};
        }
        return CodegenWriteResult{true, ""};
    }
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

    const CodegenWriteResult port_result = enforceUploadPort(output_project_dir);
    if (!port_result.success)
    {
        return port_result;
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
    // OVERNIGHT_FIELD_REFERENCE.md: содержимое ДО этой генерации — база для
    // CHANGELOG_GENERATED.md ниже. Читается тем же readUtf8PathFile, что и
    // слияние USER CODE чуть выше — отдельное чтение, не переиспользование
    // "existing", потому что main_c_to_write после слияния — это уже
    // результат мерджа, а для диффа нужен ИСХОДНЫЙ файл с диска.
    const bool is_first_generation = !fs::exists(main_c_path);
    std::string previous_main_c;
    std::string previous_init_h;
    std::string previous_init_c;
    if (fs::exists(main_c_path))
    {
        const FileReadOutcome existing = readUtf8PathFile(main_c_path.u8string());
        if (existing.success)
        {
            main_c_to_write = mergeUserCodeSections(firmware.main_c, existing.content);
            previous_main_c = existing.content;
        }
    }
    const fs::path init_h_path = src_dir / "mik32_init.h";
    if (fs::exists(init_h_path))
    {
        const FileReadOutcome existing = readUtf8PathFile(init_h_path.u8string());
        if (existing.success)
        {
            previous_init_h = existing.content;
        }
    }
    const fs::path init_c_path = src_dir / "mik32_init.c";
    if (fs::exists(init_c_path))
    {
        const FileReadOutcome existing = readUtf8PathFile(init_c_path.u8string());
        if (existing.success)
        {
            previous_init_c = existing.content;
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

    // OVERNIGHT_FIELD_REFERENCE.md: CHANGELOG_GENERATED.md — после каждой
    // генерации, в корне проекта (сосед src/), не внутри транзакции выше
    // (это вторичный, не критичный для сборки файл — ошибка его записи не
    // должна откатывать уже успешно записанный src/).
    const SimpleLineDiff init_h_diff = computeSimpleLineDiff(previous_init_h, firmware.mik32_init_h);
    const SimpleLineDiff init_c_diff = computeSimpleLineDiff(previous_init_c, firmware.mik32_init_c);
    const SimpleLineDiff main_c_diff = computeSimpleLineDiff(previous_main_c, main_c_to_write);
    const std::string changelog = renderChangelog(is_first_generation, init_h_diff, init_c_diff, main_c_diff);
    writeUtf8PathFile((utf8Path(output_project_dir) / "CHANGELOG_GENERATED.md").u8string(), changelog);

    return CodegenWriteResult{true, ""};
}
