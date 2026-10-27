//
// Инструмент для прямой кодогенерации из JSON-конфига, полученного
// из reverse-import: принимает JSON-файл и генерирует project структуру
// с использованием того же пайплайна, что и src/main.cpp.
//
// Использование: forward-codegen-from-json <json-конфиг> <dir-вывода>
//
#include <iostream>
#include <fstream>
#include <sstream>
#include <filesystem>
#include "utf8_arguments.h"
#include "project_config.h"
#include "project_codegen.h"
#include "gpio_knowledge.h"
#include "uart_knowledge.h"
#include "timer16_knowledge.h"
#include "i2c_knowledge.h"
#include "timer32_knowledge.h"
#include "spi_knowledge.h"
#include "rtc_knowledge.h"
#include "dac_knowledge.h"
#include "cross_module_validator.h"
#include "cmake_project_codegen.h"
#include "arduino_project_codegen.h"
#include "json.hpp"

namespace
{
    bool loadAllKnowledge(ProjectKnowledge& knowledge)
    {
        ParseResult<GpioKnowledge> gpio = loadGpioKnowledge("knowledge_base/gpio.json");
        if (!gpio.success)
        {
            std::cerr << "Ошибка knowledge_base/gpio.json: " << gpio.error << "\n";
            return false;
        }
        ParseResult<UartKnowledge> uart = loadUartKnowledge("knowledge_base/uart.json");
        if (!uart.success)
        {
            std::cerr << "Ошибка knowledge_base/uart.json: " << uart.error << "\n";
            return false;
        }
        ParseResult<Timer16Knowledge> timer16 = loadTimer16Knowledge("knowledge_base/timer16.json");
        if (!timer16.success)
        {
            std::cerr << "Ошибка knowledge_base/timer16.json: " << timer16.error << "\n";
            return false;
        }
        ParseResult<I2cKnowledge> i2c = loadI2cKnowledge("knowledge_base/i2c.json");
        if (!i2c.success)
        {
            std::cerr << "Ошибка knowledge_base/i2c.json: " << i2c.error << "\n";
            return false;
        }
        ParseResult<Timer32Knowledge> timer32 = loadTimer32Knowledge("knowledge_base/timer32.json");
        if (!timer32.success)
        {
            std::cerr << "Ошибка knowledge_base/timer32.json: " << timer32.error << "\n";
            return false;
        }
        ParseResult<SpiKnowledge> spi = loadSpiKnowledge("knowledge_base/spi.json");
        if (!spi.success)
        {
            std::cerr << "Ошибка knowledge_base/spi.json: " << spi.error << "\n";
            return false;
        }
        ParseResult<RtcKnowledge> rtc = loadRtcKnowledge("knowledge_base/rtc.json");
        if (!rtc.success)
        {
            std::cerr << "Ошибка knowledge_base/rtc.json: " << rtc.error << "\n";
            return false;
        }
        ParseResult<DacKnowledge> dac = loadDacKnowledge("knowledge_base/dac.json");
        if (!dac.success)
        {
            std::cerr << "Ошибка knowledge_base/dac.json: " << dac.error << "\n";
            return false;
        }

        knowledge.gpio = *gpio.config;
        knowledge.uart = *uart.config;
        knowledge.timer16 = *timer16.config;
        knowledge.i2c = *i2c.config;
        knowledge.timer32 = *timer32.config;
        knowledge.spi = *spi.config;
        knowledge.rtc = *rtc.config;
        knowledge.dac = *dac.config;
        return true;
    }

    bool reportPinConflicts(const CrossModuleValidationResult& validation)
    {
        if (validation.conflicts.empty())
        {
            return false;
        }
        std::cerr << "Обнаружены конфликты назначений пинов (ТЗ §3.1.6.3, блокирует кодогенерацию):\n";
        for (const PinConflict& conflict : validation.conflicts)
        {
            std::cerr << "  GPIO_" << conflict.gpio_instance << " pin " << conflict.pin << ": ";
            for (size_t i = 0; i < conflict.conflicting_owners.size(); ++i)
            {
                if (i > 0)
                {
                    std::cerr << " vs ";
                }
                std::cerr << conflict.conflicting_owners[i];
            }
            std::cerr << "\n";
        }
        return true;
    }
}

int main(int argc, char** argv)
{
    utf8_arguments::configureConsole();
    const std::vector<std::string> arguments = utf8_arguments::toUtf8(argc, argv);

    if (argc != 3 && argc != 4)
    {
        std::cerr << "Использование: " << arguments[0]
                  << " <json-конфиг> <dir-вывода> [legacy-main.c]\n";
        return 1;
    }

    const std::string config_path = arguments[1];
    const std::string output_dir = arguments[2];
    // shortcut: переносит прикладной код только из main.c, не из других
    // вендорных файлов (xprintf.c и т.п.) — этого достаточно для методики
    // построчной сверки одного примера, расширять при появлении второго кейса.
    if (argc == 4)
    {
        const std::string legacy_main_c_path = arguments[3];
        std::ifstream legacy_in(legacy_main_c_path, std::ios::binary);
        if (!legacy_in)
        {
            std::cerr << "Не удалось открыть legacy main.c: " << legacy_main_c_path << "\n";
            return 1;
        }
        std::stringstream legacy_buffer;
        legacy_buffer << legacy_in.rdbuf();

        std::filesystem::path src_dir = std::filesystem::path(output_dir) / "src";
        std::filesystem::create_directories(src_dir);
        std::ofstream seeded_main(src_dir / "main.c", std::ios::binary);
        seeded_main << legacy_buffer.str();
    }

    // 1. Разбор и schema-валидация всех блоков конфигурации.
    ParseResult<ProjectConfig> config_result = parseProjectConfig(config_path);
    if (!config_result.success)
    {
        std::cerr << "Ошибка парсинга: " << config_result.error << "\n";
        return 1;
    }

    // 2. База знаний МИК32 (аппаратные факты из даташита/errata).
    ProjectKnowledge knowledge;
    if (!loadAllKnowledge(knowledge))
    {
        return 1;
    }

    // 3. Кросс-модульная валидация: конфликты выводов между блоками разных типов.
    CrossModuleValidationResult validation =
        validatePinConflicts(config_path, knowledge.uart, knowledge.timer16, knowledge.i2c, knowledge.timer32, knowledge.spi);
    if (!validation.success)
    {
        std::cerr << "Ошибка кросс-модульной валидации: " << validation.error << "\n";
        return 1;
    }
    if (reportPinConflicts(validation))
    {
        return 1;
    }

    // 4-5. Сборка и запись проекта. Поле "toolchain" конфига решает ветку —
    // то же решение, что в gui/main_window.cpp (иначе CLI молча игнорирует
    // выбор CMake и всегда пишет PlatformIO).
    if (config_result.config->toolchain == ToolchainKind::CMake)
    {
        ParseResult<GeneratedProjectFirmwareCmake> firmware_result =
            generateProjectFirmwareCmake(*config_result.config, knowledge);
        if (!firmware_result.success)
        {
            std::cerr << "Ошибка кодогенерации: " << firmware_result.error << "\n";
            return 1;
        }
        std::ifstream config_in(config_path, std::ios::binary);
        nlohmann::json config_json_copy;
        config_in >> config_json_copy;
        CodegenWriteResult write_result = writeGeneratedCmakeProject(
            *firmware_result.config, "templates/cmake_skeleton",
            "templates/mik32_sdk_snapshot", output_dir, *config_result.config,
            config_json_copy);
        if (!write_result.success)
        {
            std::cerr << "Ошибка записи проекта: " << write_result.error << "\n";
            return 1;
        }
        std::cout << "Проект сгенерирован в " << output_dir << "\n";
        return 0;
    }

    // language=arduino — тот же паттерн, что toolchain=CMake выше (иначе CLI
    // молча пишет C/HAL-проект вместо Arduino-ветки). Rust здесь не
    // диспетчеризуется — этому инструменту он пока не понадобился.
    if (config_result.config->language == OutputLanguage::Arduino)
    {
        ParseResult<GeneratedProjectFirmwareArduino> arduino_result =
            generateProjectFirmwareArduino(*config_result.config);
        if (!arduino_result.success)
        {
            std::cerr << "Ошибка кодогенерации: " << arduino_result.error << "\n";
            return 1;
        }
        std::ifstream config_in(config_path, std::ios::binary);
        nlohmann::json config_json_copy;
        config_in >> config_json_copy;
        CodegenWriteResult write_result = writeGeneratedArduinoProject(
            *arduino_result.config, "templates/pio_skeleton_arduino",
            "templates/mik32_arduino_core_snapshot", output_dir, config_json_copy);
        if (!write_result.success)
        {
            std::cerr << "Ошибка записи проекта: " << write_result.error << "\n";
            return 1;
        }
        std::cout << "Проект сгенерирован в " << output_dir << "\n";
        return 0;
    }

    ParseResult<GeneratedProjectFirmware> firmware_result =
        generateProjectFirmware(*config_result.config, knowledge);
    if (!firmware_result.success)
    {
        std::cerr << "Ошибка кодогенерации: " << firmware_result.error << "\n";
        return 1;
    }

    // 5. Запись на диск (PlatformIO-путь по умолчанию, как в src/main.cpp).
    const std::string skeleton_dir = config_result.config->freertos.enabled
        ? "templates/pio_skeleton_freertos"
        : "templates/pio_skeleton";
    CodegenWriteResult write_result = writeGeneratedProject(
        *firmware_result.config, skeleton_dir, output_dir,
        config_result.config->freertos, "templates/freertos_kernel_snapshot");
    if (!write_result.success)
    {
        std::cerr << "Ошибка записи проекта: " << write_result.error << "\n";
        return 1;
    }

    // Если legacy-файл лежал внутри самой output_dir/src под другим именем
    // (типичный случай — импорт вендорного C++-проекта с main.cpp вместо
    // main.c), его содержимое уже смигрировано в только что записанный
    // main.c строкой выше — удаляем старый файл, иначе PlatformIO увидит
    // два int main() (main.c + main.cpp) и упадёт на линковке.
    if (argc == 4)
    {
        const std::filesystem::path legacy_path =
            std::filesystem::absolute(std::filesystem::path(arguments[3]));
        const std::filesystem::path generated_main_c_path =
            std::filesystem::absolute(std::filesystem::path(output_dir) / "src" / "main.c");
        if (legacy_path != generated_main_c_path)
        {
            std::error_code remove_error;
            std::filesystem::remove(legacy_path, remove_error);
        }
    }

    std::cout << "Проект сгенерирован в " << output_dir << "\n";
    return 0;
}
