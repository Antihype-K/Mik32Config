#include "timer16_knowledge.h"
#include "json.hpp"
#include <fstream>

ParseResult<Timer16Knowledge> loadTimer16Knowledge(const std::string& file_path)
{
    std::ifstream input_file(file_path);
    if (!input_file.is_open())
    {
        return ParseResult<Timer16Knowledge>{false, "Не удалось открыть knowledge_base: " + file_path, std::nullopt};
    }

    nlohmann::json root;
    try
    {
        input_file >> root;
    }
    catch (const nlohmann::json::parse_error& parse_error)
    {
        return ParseResult<Timer16Knowledge>{false, std::string("Ошибка разбора knowledge_base: ") + parse_error.what(), std::nullopt};
    }

    Timer16Knowledge knowledge;
    try
    {
        knowledge.max_instances = root.at("max_instances").get<int>();

        for (auto it = root.at("instance_handle").begin();
             it != root.at("instance_handle").end(); ++it)
        {
            knowledge.instance_handle[std::stoi(it.key())] = it.value().get<std::string>();
        }

        for (auto it = root.at("prescaler_hal_enum").begin();
             it != root.at("prescaler_hal_enum").end(); ++it)
        {
            knowledge.prescaler_hal_enum[it.key()] = it.value().get<std::string>();
        }

        knowledge.sys_clk_hz = root.at("sys_clk_hz").get<long>();

        for (auto it = root.at("instance_fixed_pwm_pin").begin();
             it != root.at("instance_fixed_pwm_pin").end(); ++it)
        {
            Timer16Knowledge::FixedPin pin;
            pin.gpio_instance = it.value().at("gpio_instance").get<int>();
            pin.pin = it.value().at("pin").get<int>();
            knowledge.instance_fixed_pwm_pin[std::stoi(it.key())] = pin;
        }

        for (auto it = root.at("instance_default_trigger_source").begin();
             it != root.at("instance_default_trigger_source").end(); ++it)
        {
            knowledge.instance_default_trigger_source[std::stoi(it.key())] = it.value().get<std::string>();
        }

        for (auto it = root.at("mode_waveform_enable").begin();
             it != root.at("mode_waveform_enable").end(); ++it)
        {
            knowledge.mode_waveform_enable[it.key()] = it.value().get<bool>();
        }
    }
    catch (const nlohmann::json::exception& structure_error)
    {
        return ParseResult<Timer16Knowledge>{false, std::string("knowledge_base/timer16.json не соответствует ожидаемой структуре: ") + structure_error.what(), std::nullopt};
    }

    return ParseResult<Timer16Knowledge>{true, "", knowledge};
}
