#include "project_config.h"
#include "win_file_io.h"
#include "json.hpp"
#include <algorithm>
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

    // --- clock (OVERNIGHT_FIELD_REFERENCE.md) ---------------------------
    // Необязательный top-level объект. Источник — mik32_hal_pcc.h,
    // PCC_InitTypeDef. Отсутствие поля -> ClockConfig{} (дефолты project_config.h),
    // воспроизводящие прежний хардкод в project_codegen.cpp побайтово.
    if (root.contains("clock"))
    {
        if (!root["clock"].is_object())
        {
            return makeError("Поле \"clock\" должно быть объектом");
        }
        const nlohmann::json& clock = root["clock"];

        if (clock.contains("oscillator_system"))
        {
            if (!clock["oscillator_system"].is_string())
            {
                return makeError("Поле \"clock.oscillator_system\" должно быть строкой");
            }
            const std::string oscillator_system = clock["oscillator_system"].get<std::string>();
            if (oscillator_system != "hsi32m" && oscillator_system != "osc32m")
            {
                return makeError("Поле \"clock.oscillator_system\" содержит недопустимое значение (разрешено hsi32m, osc32m)");
            }
            project.clock.oscillator_system = oscillator_system;
        }

        // AHB/APB_M/APB_P — делители частоты, PCC_InitTypeDef: "число между
        // Min = 0 и Max = 255" (mik32_hal_pcc.h, комментарии к AHBDivider/
        // APBMDivider/APBPDivider).
        struct DividerField { const char* json_name; int* target; };
        const DividerField divider_fields[] = {
            {"ahb_divider", &project.clock.ahb_divider},
            {"apbm_divider", &project.clock.apbm_divider},
            {"apbp_divider", &project.clock.apbp_divider},
        };
        for (const DividerField& field : divider_fields)
        {
            if (!clock.contains(field.json_name))
            {
                continue;
            }
            if (!clock[field.json_name].is_number_integer())
            {
                return makeError(std::string("Поле \"clock.") + field.json_name + "\" должно быть целым числом");
            }
            const int value = clock[field.json_name].get<int>();
            if (value < 0 || value > 255)
            {
                return makeError(std::string("Поле \"clock.") + field.json_name + "\" вне диапазона 0-255 (PCC_InitTypeDef)");
            }
            *field.target = value;
        }

        // AUDIT_TZ_PERIPHERY_GAPS.md, Clock/PCC — добавлено 2026-10-14.
        if (clock.contains("hsi32m_calibration"))
        {
            if (!clock["hsi32m_calibration"].is_number_integer())
            {
                return makeError("Поле \"clock.hsi32m_calibration\" должно быть целым числом");
            }
            const int value = clock["hsi32m_calibration"].get<int>();
            if (value < 0 || value > 255)
            {
                return makeError("Поле \"clock.hsi32m_calibration\" вне диапазона 0-255 (PCC_InitTypeDef.HSI32MCalibrationValue)");
            }
            project.clock.hsi32m_calibration = value;
        }

        if (clock.contains("lsi32k_calibration"))
        {
            if (!clock["lsi32k_calibration"].is_number_integer())
            {
                return makeError("Поле \"clock.lsi32k_calibration\" должно быть целым числом");
            }
            const int value = clock["lsi32k_calibration"].get<int>();
            if (value < 0 || value > 15)
            {
                return makeError("Поле \"clock.lsi32k_calibration\" вне диапазона 0-15 (PCC_InitTypeDef.LSI32KCalibrationValue)");
            }
            project.clock.lsi32k_calibration = value;
        }

        if (clock.contains("rtc_clock_source"))
        {
            if (!clock["rtc_clock_source"].is_string())
            {
                return makeError("Поле \"clock.rtc_clock_source\" должно быть строкой");
            }
            const std::string value = clock["rtc_clock_source"].get<std::string>();
            if (value != "auto" && value != "lsi32k" && value != "osc32k")
            {
                return makeError("Поле \"clock.rtc_clock_source\" содержит недопустимое значение (разрешено auto, lsi32k, osc32k)");
            }
            project.clock.rtc_clock_source = value;
        }

        if (clock.contains("rtc_clock_cpu_source"))
        {
            if (!clock["rtc_clock_cpu_source"].is_string())
            {
                return makeError("Поле \"clock.rtc_clock_cpu_source\" должно быть строкой");
            }
            const std::string value = clock["rtc_clock_cpu_source"].get<std::string>();
            if (value != "osc32k" && value != "lsi32k")
            {
                return makeError("Поле \"clock.rtc_clock_cpu_source\" содержит недопустимое значение (разрешено osc32k, lsi32k)");
            }
            project.clock.rtc_clock_cpu_source = value;
        }

        if (clock.contains("oscillator_enable"))
        {
            if (!clock["oscillator_enable"].is_array() || clock["oscillator_enable"].empty())
            {
                return makeError("Поле \"clock.oscillator_enable\" должно быть непустым массивом строк");
            }
            std::vector<std::string> enabled;
            const std::set<std::string> valid_oscillators = {"hsi32m", "osc32m", "lsi32k", "osc32k"};
            for (const auto& item : clock["oscillator_enable"])
            {
                if (!item.is_string() || !valid_oscillators.count(item.get<std::string>()))
                {
                    return makeError("Поле \"clock.oscillator_enable\" содержит недопустимое значение "
                        "(разрешено hsi32m, osc32m, lsi32k, osc32k)");
                }
                enabled.push_back(item.get<std::string>());
            }
            project.clock.oscillator_enable = enabled;
        }

        // Защита от "выключил источник, от которого сам тактируешься" —
        // HAL_PCC_Config настраивает систему на oscillator_system, а ПОТОМ
        // выключает всё, что не попало в OscillatorEnable (mik32_hal_pcc.c:
        // 345-370) — если oscillator_system не в маске, сгенерированная
        // прошивка заглушит себе кристалл сразу после HAL_PCC_Config.
        if (std::find(project.clock.oscillator_enable.begin(), project.clock.oscillator_enable.end(),
                project.clock.oscillator_system) == project.clock.oscillator_enable.end())
        {
            return makeError("Поле \"clock.oscillator_enable\" не содержит \"" + project.clock.oscillator_system
                + "\" (clock.oscillator_system) — HAL_PCC_Config выключит именно тот генератор, "
                  "от которого тактируется ядро, и прошивка зависнет сразу после SystemClock_Config()");
        }

        if (clock.contains("force_osc_sys"))
        {
            if (!clock["force_osc_sys"].is_boolean())
            {
                return makeError("Поле \"clock.force_osc_sys\" должно быть булевым");
            }
            project.clock.force_osc_sys = clock["force_osc_sys"].get<bool>();
        }

        if (clock.contains("force_32k_clk"))
        {
            if (!clock["force_32k_clk"].is_string())
            {
                return makeError("Поле \"clock.force_32k_clk\" должно быть строкой");
            }
            const std::string value = clock["force_32k_clk"].get<std::string>();
            if (value != "auto" && value != "lsi32k" && value != "osc32k")
            {
                return makeError("Поле \"clock.force_32k_clk\" содержит недопустимое значение (разрешено auto, lsi32k, osc32k)");
            }
            project.clock.force_32k_clk = value;
        }
    }

    // Занятые instance — только для типов, где повторное вхождение
    // бессмысленно физически (см. комментарий к parseProjectConfig в .h).
    std::set<int> used_uart_instances;
    std::set<int> used_timer16_instances;
    std::set<int> used_i2c_instances;
    std::set<int> used_timer32_instances;
    std::set<int> used_spi_instances;
    std::set<int> used_dac_instances;
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
        else if (type == "rtc")
        {
            // AUDIT_TZ_PERIPHERY_GAPS.md, Тир 2: RTC — единственный глобальный
            // блок (нет instance), допустим максимум один на проект.
            if (project.rtc_block.has_value())
            {
                return makeError(blockPrefix(block_index, type)
                    + "в проекте уже есть блок rtc — RTC в МИК32 один физический блок, "
                      "второй блок type=\"rtc\" не имеет смысла");
            }
            ParseResult<RtcConfig> block_result = parseRtcBlock(block);
            if (!block_result.success)
            {
                return makeError(blockPrefix(block_index, type) + block_result.error);
            }
            project.rtc_block = *block_result.config;
        }
        else if (type == "crc")
        {
            if (project.crc_block.has_value())
            {
                return makeError(blockPrefix(block_index, type)
                    + "в проекте уже есть блок crc — CRC в МИК32 один физический блок, "
                      "второй блок type=\"crc\" не имеет смысла");
            }
            ParseResult<CrcConfig> block_result = parseCrcBlock(block);
            if (!block_result.success)
            {
                return makeError(blockPrefix(block_index, type) + block_result.error);
            }
            project.crc_block = *block_result.config;
        }
        else if (type == "eeprom")
        {
            if (project.eeprom_block.has_value())
            {
                return makeError(blockPrefix(block_index, type)
                    + "в проекте уже есть блок eeprom — EEPROM в МИК32 один физический блок, "
                      "второй блок type=\"eeprom\" не имеет смысла");
            }
            ParseResult<EepromConfig> block_result = parseEepromBlock(block);
            if (!block_result.success)
            {
                return makeError(blockPrefix(block_index, type) + block_result.error);
            }
            project.eeprom_block = *block_result.config;
        }
        else if (type == "wdt")
        {
            if (project.wdt_block.has_value())
            {
                return makeError(blockPrefix(block_index, type)
                    + "в проекте уже есть блок wdt — WDT в МИК32 один физический блок, "
                      "второй блок type=\"wdt\" не имеет смысла");
            }
            ParseResult<WdtConfig> block_result = parseWdtBlock(block);
            if (!block_result.success)
            {
                return makeError(blockPrefix(block_index, type) + block_result.error);
            }
            project.wdt_block = *block_result.config;
        }
        else if (type == "dac")
        {
            ParseResult<DacConfig> block_result = parseDacBlock(block);
            if (!block_result.success)
            {
                return makeError(blockPrefix(block_index, type) + block_result.error);
            }
            if (!used_dac_instances.insert(block_result.config->instance).second)
            {
                return makeError(blockPrefix(block_index, type) + "instance "
                    + std::to_string(block_result.config->instance)
                    + " уже настроен другим блоком dac (один физический канал ЦАП нельзя настроить дважды)");
            }
            project.dac_blocks.push_back(*block_result.config);
        }
        else if (type == "adc")
        {
            if (project.adc_block.has_value())
            {
                return makeError(blockPrefix(block_index, type)
                    + "в проекте уже есть блок adc — АЦП в МИК32 один физический блок, "
                      "второй блок type=\"adc\" не имеет смысла");
            }
            ParseResult<AdcConfig> block_result = parseAdcBlock(block);
            if (!block_result.success)
            {
                return makeError(blockPrefix(block_index, type) + block_result.error);
            }
            project.adc_block = *block_result.config;
        }
        else if (type == "crypto")
        {
            if (project.crypto_block.has_value())
            {
                return makeError(blockPrefix(block_index, type)
                    + "в проекте уже есть блок crypto — CRYPTO в МИК32 один физический блок, "
                      "второй блок type=\"crypto\" не имеет смысла");
            }
            ParseResult<CryptoConfig> block_result = parseCryptoBlock(block);
            if (!block_result.success)
            {
                return makeError(blockPrefix(block_index, type) + block_result.error);
            }
            project.crypto_block = *block_result.config;
        }
        else if (type == "otp")
        {
            if (project.otp_block.has_value())
            {
                return makeError(blockPrefix(block_index, type)
                    + "в проекте уже есть блок otp — OTP в МИК32 один физический блок, "
                      "второй блок type=\"otp\" не имеет смысла");
            }
            ParseResult<OtpConfig> block_result = parseOtpBlock(block);
            if (!block_result.success)
            {
                return makeError(blockPrefix(block_index, type) + block_result.error);
            }
            project.otp_block = *block_result.config;
        }
        else if (type == "tsens")
        {
            if (project.tsens_block.has_value())
            {
                return makeError(blockPrefix(block_index, type)
                    + "в проекте уже есть блок tsens — датчик температуры в МИК32 один физический блок, "
                      "второй блок type=\"tsens\" не имеет смысла");
            }
            ParseResult<TsensConfig> block_result = parseTsensBlock(block);
            if (!block_result.success)
            {
                return makeError(blockPrefix(block_index, type) + block_result.error);
            }
            project.tsens_block = *block_result.config;
        }
        else if (type == "pvd")
        {
            if (project.pvd_block.has_value())
            {
                return makeError(blockPrefix(block_index, type)
                    + "в проекте уже есть блок pvd — монитор напряжения в МИК32 один физический блок, "
                      "второй блок type=\"pvd\" не имеет смысла");
            }
            ParseResult<PvdConfig> block_result = parsePvdBlock(block);
            if (!block_result.success)
            {
                return makeError(blockPrefix(block_index, type) + block_result.error);
            }
            project.pvd_block = *block_result.config;
        }
        else if (type == "bus_watchdog")
        {
            if (project.bus_watchdog_block.has_value())
            {
                return makeError(blockPrefix(block_index, type)
                    + "в проекте уже есть блок bus_watchdog — сторожевой таймер шины в МИК32 один физический блок, "
                      "второй блок type=\"bus_watchdog\" не имеет смысла");
            }
            ParseResult<BusWatchdogConfig> block_result = parseBusWatchdogBlock(block);
            if (!block_result.success)
            {
                return makeError(blockPrefix(block_index, type) + block_result.error);
            }
            project.bus_watchdog_block = *block_result.config;
        }
        else if (type == "dma")
        {
            ParseResult<DmaConfig> block_result = parseDmaBlock(block);
            if (!block_result.success)
            {
                return makeError(blockPrefix(block_index, type) + block_result.error);
            }
            // Тот же глобальный пул из 4 каналов, что и у встроенного поля
            // "dma" у UART (см. ветку type == "uart" выше, used_dma_channels) —
            // канал нельзя занять дважды ни внутри одного механизма, ни между
            // ними (docs/implementations/dma.md, schemas/dma.json).
            if (!used_dma_channels.insert(block_result.config->instance).second)
            {
                return makeError(blockPrefix(block_index, type) + "DMA channel "
                    + std::to_string(block_result.config->instance)
                    + " уже занят другим блоком (у контроллера DMA всего 4 канала на весь конфиг)");
            }
            project.dma_blocks.push_back(*block_result.config);
        }
        else if (type == "spifi")
        {
            if (project.spifi_block.has_value())
            {
                return makeError(blockPrefix(block_index, type)
                    + "в проекте уже есть блок spifi — контроллер внешней памяти в МИК32 один физический блок, "
                      "второй блок type=\"spifi\" не имеет смысла");
            }
            ParseResult<SpifiConfig> block_result = parseSpifiBlock(block);
            if (!block_result.success)
            {
                return makeError(blockPrefix(block_index, type) + block_result.error);
            }
            project.spifi_block = *block_result.config;
        }
        else
        {
            // Тихо пропускать нельзя: пользователь получил бы прошивку без
            // запрошенной периферии и без единого сообщения об этом.
            // parser/timing — пустая заглушка, её тип сюда попадёт именно
            // этой веткой, пока модуль не реализован.
            return makeError("Блок #" + std::to_string(block_index + 1)
                + ": тип периферии \"" + type + "\" пока не поддерживается "
                  "(реализованы: gpio, uart, timer16, i2c, timer32, spi, rtc, crc, eeprom, wdt, dac, adc, crypto, otp, tsens, pvd, bus_watchdog, dma, spifi)");
        }
    }

    // freertos.enabled=true — легитимный случай нулевой периферии (проект
    // из одних задач/очередей без единого аппаратного блока), поэтому пустой
    // peripherals — ошибка, только если FreeRTOS тоже не сконфигурирован.
    if (!project.freertos.enabled
        && project.gpio_blocks.empty() && project.uart_blocks.empty()
        && project.timer16_blocks.empty() && project.i2c_blocks.empty()
        && project.timer32_blocks.empty() && project.spi_blocks.empty()
        && !project.rtc_block.has_value() && !project.crc_block.has_value() && !project.eeprom_block.has_value() && !project.wdt_block.has_value() && project.dac_blocks.empty()
        && !project.adc_block.has_value() && !project.crypto_block.has_value()
        && !project.otp_block.has_value() && !project.tsens_block.has_value()
        && !project.pvd_block.has_value() && !project.bus_watchdog_block.has_value()
        && project.dma_blocks.empty() && !project.spifi_block.has_value())
    {
        return makeError("Массив \"peripherals\" пуст — нечего генерировать");
    }

    return ParseResult<ProjectConfig>{true, "", project};
}
