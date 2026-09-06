// Временная точка входа в консольном режиме — заменит собой Qt GUI,
// когда он появится (см. docs/implementations/gpio.md). Позволяет уже
// сейчас реально прогнать пайплайн parse -> codegen -> запись файла,
// не дожидаясь GUI, и провести аппаратную проверку на плате.
//
// Запускать из корня репозитория конфигуратора: пути к knowledge_base/ и
// templates/ ниже заданы относительно текущего каталога.
#include <iostream>
#include "gpio_parser.h"
#include "gpio_knowledge.h"
#include "gpio_codegen.h"

int main(int argc, char** argv)
{
    if (argc < 3)
    {
        std::cerr << "Использование: " << argv[0]
                  << " <путь-к-json-конфигу> <имя-проекта-в-generated_projects>\n";
        return 1;
    }

    const std::string config_path = argv[1];
    const std::string project_name = argv[2];

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

    const std::string output_dir = "generated_projects/" + project_name;
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
