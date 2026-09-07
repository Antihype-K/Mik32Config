#include "i2c_knowledge.h"
#include "utf8_path.h"
#include "json.hpp"
#include <fstream>

ParseResult<I2cKnowledge> loadI2cKnowledge(const std::string& file_path)
{
    std::ifstream input_file(utf8Path(file_path));
    if (!input_file.is_open())
    {
        return ParseResult<I2cKnowledge>{false, "Не удалось открыть knowledge_base: " + file_path, std::nullopt};
    }

    nlohmann::json root;
    try
    {
        input_file >> root;
    }
    catch (const nlohmann::json::parse_error& parse_error)
    {
        return ParseResult<I2cKnowledge>{false, std::string("Ошибка разбора knowledge_base: ") + parse_error.what(), std::nullopt};
    }

    I2cKnowledge knowledge;
    try
    {
        knowledge.max_instances = root.at("max_instances").get<int>();

        for (auto it = root.at("instance_handle").begin();
             it != root.at("instance_handle").end(); ++it)
        {
            knowledge.instance_handle[std::stoi(it.key())] = it.value().get<std::string>();
        }

        for (auto it = root.at("instance_fixed_pins").begin();
             it != root.at("instance_fixed_pins").end(); ++it)
        {
            std::vector<I2cKnowledge::FixedPin> pins;
            for (const auto& pin_node : it.value())
            {
                I2cKnowledge::FixedPin pin;
                pin.gpio_instance = pin_node.at("gpio_instance").get<int>();
                pin.pin = pin_node.at("pin").get<int>();
                pins.push_back(pin);
            }
            knowledge.instance_fixed_pins[std::stoi(it.key())] = pins;
        }

        knowledge.duty_factor_default = root.at("duty_factor_default").get<int>();
        knowledge.sys_clk_hz = root.at("sys_clk_hz").get<long>();
    }
    catch (const nlohmann::json::exception& structure_error)
    {
        return ParseResult<I2cKnowledge>{false, std::string("knowledge_base/i2c.json не соответствует ожидаемой структуре: ") + structure_error.what(), std::nullopt};
    }

    return ParseResult<I2cKnowledge>{true, "", knowledge};
}
