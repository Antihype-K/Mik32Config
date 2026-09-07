#include "project_config.h"
#include "utf8_path.h"
#include "json.hpp"
#include <fstream>
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
    std::ifstream input_file(utf8Path(file_name_path));
    if (!input_file.is_open())
    {
        return makeError("Не удалось открыть файл конфигурации: " + file_name_path);
    }

    nlohmann::json root;
    try
    {
        input_file >> root;
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

    // Занятые instance — только для типов, где повторное вхождение
    // бессмысленно физически (см. комментарий к parseProjectConfig в .h).
    std::set<int> used_uart_instances;
    std::set<int> used_timer16_instances;
    std::set<int> used_i2c_instances;

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
        else
        {
            // Тихо пропускать нельзя: пользователь получил бы прошивку без
            // запрошенной периферии и без единого сообщения об этом.
            // parser/spi и parser/timing — пустые заглушки, их типы сюда
            // попадут именно этой веткой, пока модули не реализованы.
            return makeError("Блок #" + std::to_string(block_index + 1)
                + ": тип периферии \"" + type + "\" пока не поддерживается "
                  "(реализованы: gpio, uart, timer16, i2c)");
        }
    }

    if (project.gpio_blocks.empty() && project.uart_blocks.empty()
        && project.timer16_blocks.empty() && project.i2c_blocks.empty())
    {
        return makeError("Массив \"peripherals\" пуст — нечего генерировать");
    }

    return ParseResult<ProjectConfig>{true, "", project};
}
