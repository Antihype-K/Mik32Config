// Временная точка входа в консольном режиме — заменит собой Qt GUI,
// когда он появится (см. docs/implementations/gpio.md). Позволяет уже
// сейчас реально прогнать пайплайн parse -> codegen -> запись файла,
// не дожидаясь GUI, и провести аппаратную проверку на плате.
//
// Запускать из корня репозитория конфигуратора: пути к knowledge_base/ и
// templates/ ниже заданы относительно текущего каталога.
#include <iostream>
#include <fstream>
#include "json.hpp"
#include "gpio_parser.h"
#include "gpio_knowledge.h"
#include "gpio_codegen.h"
#include "usart_parser.h"
#include "uart_knowledge.h"
#include "uart_codegen.h"

namespace
{
    // Разные конфиги диспетчеризуются по полю "type" первого элемента
    // "peripherals" — тот же принцип, что каждый parse<Peripheral>Config
    // использует внутри себя (ищет первый блок нужного типа), только здесь
    // нужно решить, какой парсер вообще вызывать, до самого парсинга.
    std::string peekFirstPeripheralType(const std::string& config_path)
    {
        std::ifstream input_file(config_path);
        if (!input_file.is_open())
        {
            return "";
        }
        nlohmann::json root;
        try
        {
            input_file >> root;
        }
        catch (const nlohmann::json::parse_error&)
        {
            return "";
        }
        if (!root.contains("peripherals") || !root["peripherals"].is_array() || root["peripherals"].empty())
        {
            return "";
        }
        const auto& first = root["peripherals"][0];
        if (!first.contains("type") || !first["type"].is_string())
        {
            return "";
        }
        return first["type"].get<std::string>();
    }

    int runGpioPipeline(const std::string& config_path, const std::string& output_dir)
    {
        ParseResult<GpioConfig> parse_result = parseGpioConfig(config_path);
        if (!parse_result.success)
        {
            std::cerr << "Ошибка парсинга: " << parse_result.error << "\n";
            return 1;
        }

        ParseResult<GpioKnowledge> knowledge_result = loadGpioKnowledge("knowledge_base/gpio.json");
        if (!knowledge_result.success)
        {
            std::cerr << "Ошибка knowledge_base: " << knowledge_result.error << "\n";
            return 1;
        }

        ParseResult<GeneratedGpioFirmware> firmware_result =
            generateGpioFirmware(*parse_result.config, *knowledge_result.config);
        if (!firmware_result.success)
        {
            std::cerr << "Ошибка кодогенерации: " << firmware_result.error << "\n";
            return 1;
        }

        CodegenWriteResult write_result = writeGeneratedGpioProject(
            *firmware_result.config, "templates/pio_skeleton", output_dir);
        if (!write_result.success)
        {
            std::cerr << "Ошибка записи проекта: " << write_result.error << "\n";
            return 1;
        }

        std::cout << "Проект сгенерирован в " << output_dir << "\n";
        return 0;
    }

    int runUartPipeline(const std::string& config_path, const std::string& output_dir)
    {
        ParseResult<UartConfig> parse_result = parseUsartConfig(config_path);
        if (!parse_result.success)
        {
            std::cerr << "Ошибка парсинга: " << parse_result.error << "\n";
            return 1;
        }

        ParseResult<UartKnowledge> knowledge_result = loadUartKnowledge("knowledge_base/uart.json");
        if (!knowledge_result.success)
        {
            std::cerr << "Ошибка knowledge_base: " << knowledge_result.error << "\n";
            return 1;
        }

        ParseResult<GeneratedUartFirmware> firmware_result =
            generateUartFirmware(*parse_result.config, *knowledge_result.config);
        if (!firmware_result.success)
        {
            std::cerr << "Ошибка кодогенерации: " << firmware_result.error << "\n";
            return 1;
        }

        CodegenWriteResult write_result = writeGeneratedUartProject(
            *firmware_result.config, "templates/pio_skeleton", output_dir);
        if (!write_result.success)
        {
            std::cerr << "Ошибка записи проекта: " << write_result.error << "\n";
            return 1;
        }

        std::cout << "Проект сгенерирован в " << output_dir << "\n";
        return 0;
    }
}

int main(int argc, char** argv)
{
    if (argc < 3)
    {
        std::cerr << "Использование: " << argv[0]
                  << " <путь-к-json-конфигу> <имя-проекта-в-generated_projects>\n";
        return 1;
    }

    const std::string config_path = argv[1];
    const std::string output_dir = "generated_projects/" + std::string(argv[2]);

    const std::string peripheral_type = peekFirstPeripheralType(config_path);
    if (peripheral_type == "gpio")
    {
        return runGpioPipeline(config_path, output_dir);
    }
    if (peripheral_type == "uart")
    {
        return runUartPipeline(config_path, output_dir);
    }

    std::cerr << "Неизвестный или отсутствующий \"type\" первого блока peripherals"
                 " (поддерживаются: gpio, uart): " << config_path << "\n";
    return 1;
}
