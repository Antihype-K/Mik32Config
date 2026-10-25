#include "timer16_knowledge.h"
#include "win_file_io.h"
#include "json.hpp"

ParseResult<Timer16Knowledge> loadTimer16Knowledge(const std::string& file_path)
{
    const FileReadOutcome file = readUtf8PathFile(file_path);
    if (!file.success)
    {
        return ParseResult<Timer16Knowledge>{false, "Не удалось открыть knowledge_base: " + file_path + file.diagnostic, std::nullopt};
    }

    nlohmann::json root;
    try
    {
        root = nlohmann::json::parse(file.content);
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

        for (auto it = root.at("instance_fixed_input1_pin").begin();
             it != root.at("instance_fixed_input1_pin").end(); ++it)
        {
            Timer16Knowledge::FixedPin pin;
            pin.gpio_instance = it.value().at("gpio_instance").get<int>();
            pin.pin = it.value().at("pin").get<int>();
            knowledge.instance_fixed_input1_pin[std::stoi(it.key())] = pin;
        }

        for (auto it = root.at("instance_fixed_input2_pin").begin();
             it != root.at("instance_fixed_input2_pin").end(); ++it)
        {
            Timer16Knowledge::FixedPin pin;
            pin.gpio_instance = it.value().at("gpio_instance").get<int>();
            pin.pin = it.value().at("pin").get<int>();
            knowledge.instance_fixed_input2_pin[std::stoi(it.key())] = pin;
        }

        for (auto it = root.at("instance_default_trigger_source").begin();
             it != root.at("instance_default_trigger_source").end(); ++it)
        {
            knowledge.instance_default_trigger_source[std::stoi(it.key())] = it.value().get<std::string>();
        }

        for (auto it = root.at("trigger_source_hal_enum").begin();
             it != root.at("trigger_source_hal_enum").end(); ++it)
        {
            std::map<std::string, std::string> per_instance;
            for (auto inner = it.value().begin(); inner != it.value().end(); ++inner)
            {
                per_instance[inner.key()] = inner.value().get<std::string>();
            }
            knowledge.trigger_source_hal_enum[std::stoi(it.key())] = per_instance;
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
