#include "spi_knowledge.h"
#include "win_file_io.h"
#include "json.hpp"

ParseResult<SpiKnowledge> loadSpiKnowledge(const std::string& file_path)
{
    const FileReadOutcome file = readUtf8PathFile(file_path);
    if (!file.success)
    {
        return ParseResult<SpiKnowledge>{false, "Не удалось открыть knowledge_base: " + file_path + file.diagnostic, std::nullopt};
    }

    nlohmann::json root;
    try
    {
        root = nlohmann::json::parse(file.content);
    }
    catch (const nlohmann::json::parse_error& parse_error)
    {
        return ParseResult<SpiKnowledge>{false, std::string("Ошибка разбора knowledge_base: ") + parse_error.what(), std::nullopt};
    }

    SpiKnowledge knowledge;
    try
    {
        knowledge.max_instances = root.at("max_instances").get<int>();

        for (auto it = root.at("instance_handle").begin();
             it != root.at("instance_handle").end(); ++it)
        {
            knowledge.instance_handle[std::stoi(it.key())] = it.value().get<std::string>();
        }

        for (auto instance_it = root.at("instance_fixed_pins").begin();
             instance_it != root.at("instance_fixed_pins").end(); ++instance_it)
        {
            std::vector<SpiKnowledge::FixedPin> pins;
            for (const auto& pin_node : instance_it.value())
            {
                SpiKnowledge::FixedPin pin;
                pin.gpio_instance = pin_node.at("gpio_instance").get<int>();
                pin.pin = pin_node.at("pin").get<int>();
                pins.push_back(pin);
            }
            knowledge.instance_fixed_pins[std::stoi(instance_it.key())] = pins;
        }

        for (auto instance_it = root.at("instance_chip_select_pin").begin();
             instance_it != root.at("instance_chip_select_pin").end(); ++instance_it)
        {
            std::map<std::string, SpiKnowledge::FixedPin> chip_selects;
            for (auto cs_it = instance_it.value().begin();
                 cs_it != instance_it.value().end(); ++cs_it)
            {
                SpiKnowledge::FixedPin pin;
                pin.gpio_instance = cs_it.value().at("gpio_instance").get<int>();
                pin.pin = cs_it.value().at("pin").get<int>();
                chip_selects[cs_it.key()] = pin;
            }
            knowledge.instance_chip_select_pin[std::stoi(instance_it.key())] = chip_selects;
        }
    }
    catch (const nlohmann::json::exception& structure_error)
    {
        return ParseResult<SpiKnowledge>{false, std::string("knowledge_base/spi.json не соответствует ожидаемой структуре: ") + structure_error.what(), std::nullopt};
    }

    return ParseResult<SpiKnowledge>{true, "", knowledge};
}
