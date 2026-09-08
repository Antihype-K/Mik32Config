// Временная точка входа в консольном режиме — заменит собой Qt GUI, когда он
// появится. Позволяет уже сейчас прогнать полный пайплайн
// parse -> validate -> codegen -> запись проекта и провести аппаратную
// проверку на плате.
//
// Запускать из корня репозитория конфигуратора: пути к knowledge_base/ и
// templates/ ниже заданы относительно текущего каталога.
//
// До межмодульной генерации здесь было пять отдельных пайплайнов
// (runGpioPipeline, runUartPipeline, runTimer16Pipeline, runI2cPipeline,
// runGpioUartDemoPipeline) и лесенка функций-«разведчиков», заглядывавших в
// JSON, чтобы решить, какой из них звать: hasBothGpioAndUart, hasI2c, hasUart,
// peekFirstPeripheralType. Каждая новая комбинация периферии требовала нового
// пайплайна и новой ветки в этой лесенке. Теперь пайплайн один, а какая
// именно периферия в конфиге — забота сборщика
// (codegen/project/project_codegen.h).
#include <iostream>
#include "utf8_arguments.h"
#include <vector>
#include "project_config.h"
#include "project_codegen.h"
#include "gpio_knowledge.h"
#include "uart_knowledge.h"
#include "timer16_knowledge.h"
#include "i2c_knowledge.h"
#include "timer32_knowledge.h"
#include "spi_knowledge.h"
#include "cross_module_validator.h"

namespace
{
    // Все шесть баз знаний читаются всегда, независимо от содержимого
    // конфига: кросс-модульная валидация пинов (ТЗ §3.1.6.2) обходит блоки
    // всех типов сразу и не может работать с частично загруженной базой.
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

        knowledge.gpio = *gpio.config;
        knowledge.uart = *uart.config;
        knowledge.timer16 = *timer16.config;
        knowledge.i2c = *i2c.config;
        knowledge.timer32 = *timer32.config;
        knowledge.spi = *spi.config;
        return true;
    }

    // ТЗ §3.1.6.3: конфликт назначений выводов — Ошибка, блокирующая
    // кодогенерацию целиком, а не предупреждение. §3.1.6.4: сообщение должно
    // называть номер вывода и наименования всех конфликтующих блоков.
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
    // Консоль Windows по умолчанию в CP866 — без этого русские сообщения
    // выводятся кракозябрами (наблюдалось на реальной Windows).
    utf8_arguments::configureConsole();

    // Приводим аргументы к UTF-8: на Windows CRT отдаёт их в ANSI-кодировке,
    // а весь остальной код принимает пути как UTF-8 (см. include/utf8_path.h).
    // Без этого шага путь с кириллицей, набранный в консоли, не открылся бы.
    const std::vector<std::string> arguments = utf8_arguments::toUtf8(argc, argv);

    if (argc < 3)
    {
        std::cerr << "Использование: " << arguments[0]
                  << " <путь-к-json-конфигу> <имя-проекта-в-generated_projects> [--demo]\n"
                     "  --demo — добавить в тело while(1) демонстрационную логику\n"
                     "           периферии (мигание, эхо по UART, сканер I2C и т.п.).\n"
                     "           Без флага генерируется только инициализация,\n"
                     "           с пустым телом while(1) под маркерами USER CODE.\n";
        return 1;
    }

    const std::string config_path = arguments[1];
    const std::string output_dir = "generated_projects/" + arguments[2];

    bool emit_demo_loops = false;
    for (int i = 3; i < argc; ++i)
    {
        if (arguments[static_cast<size_t>(i)] == "--demo")
        {
            emit_demo_loops = true;
        }
        else
        {
            std::cerr << "Неизвестный аргумент: " << arguments[static_cast<size_t>(i)] << "\n";
            return 1;
        }
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

    // 3. Кросс-модульная валидация: конфликты выводов между блоками разных
    // типов. Идёт строго после per-module валидации (шаг 1) и строго до
    // кодогенерации — порядок задан ТЗ §3.1.6.
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

    // 4. Сборка одного проекта из вкладов всех блоков.
    ParseResult<GeneratedProjectFirmware> firmware_result =
        generateProjectFirmware(*config_result.config, knowledge, emit_demo_loops);
    if (!firmware_result.success)
    {
        std::cerr << "Ошибка кодогенерации: " << firmware_result.error << "\n";
        return 1;
    }

    // 5. Запись на диск поверх скелета PlatformIO.
    CodegenWriteResult write_result =
        writeGeneratedProject(*firmware_result.config, "templates/pio_skeleton", output_dir);
    if (!write_result.success)
    {
        std::cerr << "Ошибка записи проекта: " << write_result.error << "\n";
        return 1;
    }

    std::cout << "Проект сгенерирован в " << output_dir << "\n";
    return 0;
}
