#include "gpio_knowledge.h"
#include "json.hpp"
#include <fstream>

namespace
{
    // Концепция: knowledge_base/gpio.json хранит для каждого перечисления
    // (mode/pull/drive_strength) объект { "<строка-схемы>": { "hal_enum": "...", "bits": "..." } }.
    // Нам из каждой записи нужно только поле "hal_enum" — "bits" в этом файле
    // существует исключительно как параллельная датащит-цитата для человека,
    // читающего knowledge_base напрямую, и кодогенератору не нужен.
    std::map<std::string, std::string> extractHalEnumMap(const nlohmann::json& encoding_object)
    {
        std::map<std::string, std::string> result;
        for (auto it = encoding_object.begin(); it != encoding_object.end(); ++it)
        {
            result[it.key()] = it.value().at("hal_enum").get<std::string>();
        }
        return result;
    }
}

ParseResult<GpioKnowledge> loadGpioKnowledge(const std::string& file_path)
{
    std::ifstream input_file(file_path);
    if (!input_file.is_open())
    {
        return ParseResult<GpioKnowledge>{false, "Не удалось открыть knowledge_base: " + file_path, std::nullopt};
    }

    nlohmann::json root;
    try
    {
        input_file >> root;
    }
    catch (const nlohmann::json::parse_error& parse_error)
    {
        return ParseResult<GpioKnowledge>{false, std::string("Ошибка разбора knowledge_base: ") + parse_error.what(), std::nullopt};
    }

    GpioKnowledge knowledge;
    try
    {
        knowledge.max_instances = root.at("max_instances").get<int>();
        knowledge.pins_per_instance = root.at("pins_per_instance").get<int>();

        // Ключи JSON-объектов всегда строки — instance хранится в файле как
        // "0"/"1"/"2", здесь переводим обратно в int, которым оперирует
        // GpioConfig::instance.
        for (auto it = root.at("instance_clock_enable_macro").begin();
             it != root.at("instance_clock_enable_macro").end(); ++it)
        {
            knowledge.instance_clock_enable_macro[std::stoi(it.key())] = it.value().get<std::string>();
        }
        for (auto it = root.at("instance_gpio_handle").begin();
             it != root.at("instance_gpio_handle").end(); ++it)
        {
            knowledge.instance_gpio_handle[std::stoi(it.key())] = it.value().get<std::string>();
        }

        knowledge.mode_hal_enum = extractHalEnumMap(root.at("mode_encoding"));
        knowledge.pull_hal_enum = extractHalEnumMap(root.at("pull_encoding"));
        knowledge.drive_strength_hal_enum = extractHalEnumMap(root.at("drive_strength_encoding"));
    }
    catch (const nlohmann::json::exception& structure_error)
    {
        return ParseResult<GpioKnowledge>{false, std::string("knowledge_base/gpio.json не соответствует ожидаемой структуре: ") + structure_error.what(), std::nullopt};
    }

    return ParseResult<GpioKnowledge>{true, "", knowledge};
}
