#include "project_config.h"
#include "win_file_io.h"
#include "json.hpp"
#include <set>
#include <sstream>

namespace
{
    ParseResult<ProjectConfig> makeError(const std::string& message)
    {
        return ParseResult<ProjectConfig>{false, message, std::nullopt};
    }

    // Номер блока в сообщении — 1-based и по позиции в массиве
    // "peripherals" целиком, а не внутри своего типа: пользователь смотрит
    // в JSON-файл, где блоки идут вперемешку, и ищет «третий сверху».
    std::string blockPrefix(size_t block_index, const std::string& type)
    {
        std::ostringstream out;
        out << "Блок #" << (block_index + 1) << " (type=\"" << type << "\"): ";
        return out.str();
    }
}

ParseResult<ProjectConfig> parseProjectConfig(const std::string& file_name_path)
{
    // Файл открывается вручную (не через nlohmann::json(path)) по той же
    // причине, что и во всех parse<Peripheral>Config: «файла нет» и «файл
    // есть, но JSON битый» — два разных диагноза для пользователя.
    const FileReadOutcome file = readUtf8PathFile(file_name_path);
    if (!file.success)
    {
        return makeError("Не удалось открыть файл конфигурации: " + file_name_path + file.diagnostic);
    }

    nlohmann::json root;
    try
    {
        root = nlohmann::json::parse(file.content);
    }
    catch (const nlohmann::json::parse_error& parse_error)
    {
        return makeError(std::string("Ошибка разбора JSON: ") + parse_error.what());
    }

    return parseProjectDocument(root);
}

ParseResult<ProjectConfig> parseProjectDocument(const nlohmann::json& root)
{
    if (!root.contains("peripherals") || !root["peripherals"].is_array())
    {
        return makeError("Конфигурация не содержит массив \"peripherals\"");
    }

    ProjectConfig project;
    // mcu/version — информационные поля верхнего уровня («Архитектура.md»,
    // «Общий пайплайн»). Кодоген их пока не использует ни для чего, кроме
    // шапки комментария, поэтому их отсутствие не ошибка.
    if (root.contains("mcu") && root["mcu"].is_string())
    {
        project.mcu = root["mcu"].get<std::string>();
    }
    if (root.contains("version") && root["version"].is_string())
    {
        project.version = root["version"].get<std::string>();
    }

    // --- toolchain / hal_source_mode / hal_source_local_path /
    // compiler_path / per_type_files ---------------------------------------
    // Все пять — необязательные поля верхнего уровня (codegen/project/
    // cmake_project_codegen.h). Отсутствие поля молча оставляет дефолт из
    // struct ProjectConfig — это обязано работать на всех существующих
    // фикстурах tests/fixtures/*.json, ни одна из которых их не содержит
    // (обратная совместимость, ProjectConfigParse.KeepsDefaultsWhenNewFieldsAbsent).
    // Если поле ЕСТЬ, но не входит в перечисленный набор строк — это ошибка
    // парсинга по тому же паттерну, что и enum-строки в parseUartBlock
    // (parser/USART/usart_parser.cpp, "direction"/"parity"): молчаливый
    // дефолт при опечатке пользователя ("plarformio" вместо "platformio")
    // подсунул бы не тот тулчейн без единого сообщения об этом.
    // --- language ---------------------------------------------------------
    // Разбирается ДО "toolchain": language=rust однозначно определяет
    // toolchain=Cargo (см. OutputLanguage в project_config.h), поэтому
    // порядок важен — иначе пришлось бы откатывать уже разобранный toolchain.
    if (root.contains("language"))
    {
        if (!root["language"].is_string())
        {
            return makeError("Поле \"language\" должно быть строкой (\"c\" или \"rust\")");
        }
        const std::string language = root["language"].get<std::string>();
        if (language == "c")
        {
            project.language = OutputLanguage::C;
        }
        else if (language == "rust")
        {
            project.language = OutputLanguage::Rust;
        }
        else
        {
            return makeError("Поле \"language\" содержит недопустимое значение \"" + language
                + "\" (разрешено c, rust)");
        }
    }

    if (root.contains("toolchain"))
    {
        if (!root["toolchain"].is_string())
        {
            return makeError("Поле \"toolchain\" должно быть строкой (\"platformio\", \"cmake\" или \"cargo\")");
        }
        const std::string toolchain = root["toolchain"].get<std::string>();
        if (toolchain == "platformio")
        {
            project.toolchain = ToolchainKind::PlatformIO;
        }
        else if (toolchain == "cmake")
        {
            project.toolchain = ToolchainKind::CMake;
        }
        else if (toolchain == "cargo")
        {
            project.toolchain = ToolchainKind::Cargo;
        }
        else
        {
            return makeError("Поле \"toolchain\" содержит недопустимое значение \"" + toolchain
                + "\" (разрешено platformio, cmake, cargo)");
        }

        // Явное несоответствие toolchain/language — не тихо игнорировать
        // (иначе пользователь, написавший language=rust, toolchain=cmake,
        // получил бы C-пайплайн без единого предупреждения, либо наоборот).
        if (project.language == OutputLanguage::Rust && project.toolchain != ToolchainKind::Cargo)
        {
            return makeError("Поле \"toolchain\" содержит \"" + toolchain
                + "\", но \"language\" установлен в \"rust\" — единственный допустимый toolchain для Rust это cargo"
                  " (можно просто не указывать поле \"toolchain\" явно)");
        }
        if (project.language == OutputLanguage::C && project.toolchain == ToolchainKind::Cargo)
        {
            return makeError("Поле \"toolchain\" установлено в \"cargo\", но \"language\" не \"rust\""
                " (toolchain=cargo допустим только при language=rust)");
        }
    }
    else if (project.language == OutputLanguage::Rust)
    {
        // Поле "toolchain" не указано вовсе — обычный случай для Rust-проекта
        // (пользователь задаёт только "language":"rust"), Cargo подставляется
        // автоматически, как и решено в /grill-me сессии
        // (docs/implementations/rust_codegen.md, «Язык → автовыбор toolchain»).
        project.toolchain = ToolchainKind::Cargo;
    }

    if (root.contains("rust_hal_source_mode"))
    {
        if (!root["rust_hal_source_mode"].is_string())
        {
            return makeError("Поле \"rust_hal_source_mode\" должно быть строкой (\"bundled\", \"network\" или \"local_path\")");
        }
        const std::string rust_hal_source_mode = root["rust_hal_source_mode"].get<std::string>();
        if (rust_hal_source_mode == "bundled")
        {
            project.rust_hal_source_mode = HalSourceMode::Bundled;
        }
        else if (rust_hal_source_mode == "network")
        {
            project.rust_hal_source_mode = HalSourceMode::Network;
        }
        else if (rust_hal_source_mode == "local_path")
        {
            project.rust_hal_source_mode = HalSourceMode::LocalPath;
        }
        else
        {
            return makeError("Поле \"rust_hal_source_mode\" содержит недопустимое значение \"" + rust_hal_source_mode
                + "\" (разрешено bundled, network, local_path)");
        }
    }

    if (root.contains("rust_hal_source_local_path"))
    {
        if (!root["rust_hal_source_local_path"].is_string())
        {
            return makeError("Поле \"rust_hal_source_local_path\" должно быть строкой");
        }
        project.rust_hal_source_local_path = root["rust_hal_source_local_path"].get<std::string>();
    }

    if (root.contains("hal_source_mode"))
    {
        if (!root["hal_source_mode"].is_string())
        {
            return makeError("Поле \"hal_source_mode\" должно быть строкой (\"bundled\", \"network\" или \"local_path\")");
        }
        const std::string hal_source_mode = root["hal_source_mode"].get<std::string>();
        if (hal_source_mode == "bundled")
        {
            project.hal_source_mode = HalSourceMode::Bundled;
        }
        else if (hal_source_mode == "network")
        {
            project.hal_source_mode = HalSourceMode::Network;
        }
        else if (hal_source_mode == "local_path")
        {
            project.hal_source_mode = HalSourceMode::LocalPath;
        }
        else
        {
            return makeError("Поле \"hal_source_mode\" содержит недопустимое значение \"" + hal_source_mode
                + "\" (разрешено bundled, network, local_path)");
        }
    }

    if (root.contains("hal_source_local_path"))
    {
        if (!root["hal_source_local_path"].is_string())
        {
            return makeError("Поле \"hal_source_local_path\" должно быть строкой");
        }
        project.hal_source_local_path = root["hal_source_local_path"].get<std::string>();
    }

    if (root.contains("compiler_path"))
    {
        if (!root["compiler_path"].is_string())
        {
            return makeError("Поле \"compiler_path\" должно быть строкой");
        }
        project.compiler_path = root["compiler_path"].get<std::string>();
    }

    if (root.contains("per_type_files"))
    {
        if (!root["per_type_files"].is_boolean())
        {
            return makeError("Поле \"per_type_files\" должно быть булевым значением");
        }
        project.per_type_files = root["per_type_files"].get<bool>();
    }

    // --- freertos ------------------------------------------------------
    // Объект уровня проекта, не элемент peripherals[] — см. parser/freertos/
    // freertos_parser.h. parseFreeRtosBlock сама возвращает FreeRtosConfig{}
    // (enabled=false) без ошибки, если узла нет — здесь достаточно вызвать
    // её безусловно на root.value("freertos", nlohmann::json::object()).
    if (root.contains("freertos"))
    {
        ParseResult<FreeRtosConfig> freertos_result = parseFreeRtosBlock(root["freertos"]);
        if (!freertos_result.success)
        {
            return makeError(freertos_result.error);
        }
        project.freertos = *freertos_result.config;
    }

    // Занятые instance — только для типов, где повторное вхождение
    // бессмысленно физически (см. комментарий к parseProjectConfig в .h).
    std::set<int> used_uart_instances;
    std::set<int> used_timer16_instances;
    std::set<int> used_i2c_instances;
    std::set<int> used_timer32_instances;
    std::set<int> used_spi_instances;
    std::set<int> used_gpio_irq_lines;
    std::set<int> used_dma_channels;

    const nlohmann::json& peripherals = root["peripherals"];
    for (size_t block_index = 0; block_index < peripherals.size(); ++block_index)
    {
        const nlohmann::json& block = peripherals[block_index];

        if (!block.contains("type") || !block["type"].is_string())
        {
            std::ostringstream error;
            error << "Блок #" << (block_index + 1) << ": отсутствует строковое поле \"type\"";
            return makeError(error.str());
        }
        const std::string type = block["type"].get<std::string>();

        if (type == "gpio")
        {
            ParseResult<GpioConfig> block_result = parseGpioBlock(block);
            if (!block_result.success)
            {
                return makeError(blockPrefix(block_index, type) + block_result.error);
            }
            // ТЗ §3.1.2 п.5 / §3.1.6 п.2: у контроллера GPIO IRQ ровно 8
            // каналов НА ВЕСЬ конфиг (не на блок и не на instance) — тот же
            // класс "глобального пула", что и 4 канала DMA. Внутри одного
            // блока уникальность уже проверена в parseGpioBlock; здесь —
            // конфликт МЕЖДУ блоками (в т.ч. с разными instance).
            for (const GpioInterruptConfig& irq : block_result.config->interrupts)
            {
                if (!used_gpio_irq_lines.insert(irq.line).second)
                {
                    return makeError(blockPrefix(block_index, type) + "line " + std::to_string(irq.line)
                        + " уже занята прерыванием другого блока gpio (у контроллера GPIO IRQ всего 8 каналов на весь конфиг)");
                }
            }
            // Дубликат instance для GPIO разрешён намеренно — см. .h.
            project.gpio_blocks.push_back(*block_result.config);
        }
        else if (type == "uart")
        {
            ParseResult<UartConfig> block_result = parseUartBlock(block);
            if (!block_result.success)
            {
                return makeError(blockPrefix(block_index, type) + block_result.error);
            }
            if (!used_uart_instances.insert(block_result.config->instance).second)
            {
                return makeError(blockPrefix(block_index, type) + "instance "
                    + std::to_string(block_result.config->instance)
                    + " уже настроен другим блоком uart (один физический UART нельзя настроить дважды)");
            }
            // ТЗ §3.1.3.3 / §3.1.6 п.2: «конфликты каналов DMA (4 канала на
            // все периферийные запросы)» — буквально названный в ТЗ пример
            // глобального пула, в отличие от которого GPIO IRQ (8 линий)
            // выше был расширением по аналогии. Пул общий для ВСЕХ типов
            // периферии, но пока DMA настраивает только UART.
            for (const UartDmaConfig& dma : block_result.config->dma)
            {
                if (!used_dma_channels.insert(dma.channel).second)
                {
                    return makeError(blockPrefix(block_index, type) + "DMA channel " + std::to_string(dma.channel)
                        + " уже занят другим блоком (у контроллера DMA всего 4 канала на весь конфиг)");
                }
            }
            project.uart_blocks.push_back(*block_result.config);
        }
        else if (type == "timer16")
        {
            ParseResult<Timer16Config> block_result = parseTimer16Block(block);
            if (!block_result.success)
            {
                return makeError(blockPrefix(block_index, type) + block_result.error);
            }
            if (!used_timer16_instances.insert(block_result.config->instance).second)
            {
                return makeError(blockPrefix(block_index, type) + "instance "
                    + std::to_string(block_result.config->instance)
                    + " уже настроен другим блоком timer16 (один физический Timer16 нельзя настроить дважды)");
            }
            project.timer16_blocks.push_back(*block_result.config);
        }
        else if (type == "i2c")
        {
            ParseResult<I2cConfig> block_result = parseI2cBlock(block);
            if (!block_result.success)
            {
                return makeError(blockPrefix(block_index, type) + block_result.error);
            }
            if (!used_i2c_instances.insert(block_result.config->instance).second)
            {
                return makeError(blockPrefix(block_index, type) + "instance "
                    + std::to_string(block_result.config->instance)
                    + " уже настроен другим блоком i2c (один физический I2C нельзя настроить дважды)");
            }
            project.i2c_blocks.push_back(*block_result.config);
        }
        else if (type == "timer32")
        {
            ParseResult<Timer32Config> block_result = parseTimer32Block(block);
            if (!block_result.success)
            {
                return makeError(blockPrefix(block_index, type) + block_result.error);
            }
            if (!used_timer32_instances.insert(block_result.config->instance).second)
            {
                return makeError(blockPrefix(block_index, type) + "instance "
                    + std::to_string(block_result.config->instance)
                    + " уже настроен другим блоком timer32 (один физический Timer32 нельзя настроить дважды)");
            }
            project.timer32_blocks.push_back(*block_result.config);
        }
        else if (type == "spi")
        {
            ParseResult<SpiConfig> block_result = parseSpiBlock(block);
            if (!block_result.success)
            {
                return makeError(blockPrefix(block_index, type) + block_result.error);
            }
            if (!used_spi_instances.insert(block_result.config->instance).second)
            {
                return makeError(blockPrefix(block_index, type) + "instance "
                    + std::to_string(block_result.config->instance)
                    + " уже настроен другим блоком spi (один физический SPI нельзя настроить дважды)");
            }
            project.spi_blocks.push_back(*block_result.config);
        }
        else
        {
            // Тихо пропускать нельзя: пользователь получил бы прошивку без
            // запрошенной периферии и без единого сообщения об этом.
            // parser/timing — пустая заглушка, её тип сюда попадёт именно
            // этой веткой, пока модуль не реализован.
            return makeError("Блок #" + std::to_string(block_index + 1)
                + ": тип периферии \"" + type + "\" пока не поддерживается "
                  "(реализованы: gpio, uart, timer16, i2c, timer32, spi)");
        }
    }

    // freertos.enabled=true — легитимный случай нулевой периферии (проект
    // из одних задач/очередей без единого аппаратного блока), поэтому пустой
    // peripherals — ошибка, только если FreeRTOS тоже не сконфигурирован.
    if (!project.freertos.enabled
        && project.gpio_blocks.empty() && project.uart_blocks.empty()
        && project.timer16_blocks.empty() && project.i2c_blocks.empty()
        && project.timer32_blocks.empty() && project.spi_blocks.empty())
    {
        return makeError("Массив \"peripherals\" пуст — нечего генерировать");
    }

    return ParseResult<ProjectConfig>{true, "", project};
}
