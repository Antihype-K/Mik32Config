#include "timer32_knowledge.h"
#include "win_file_io.h"
#include "json.hpp"

ParseResult<Timer32Knowledge> loadTimer32Knowledge(const std::string& file_path)
{
    const FileReadOutcome file = readUtf8PathFile(file_path);
    if (!file.success)
    {
        return ParseResult<Timer32Knowledge>{false, "Не удалось открыть knowledge_base: " + file_path + file.diagnostic, std::nullopt};
    }

    nlohmann::json root;
    try
    {
        root = nlohmann::json::parse(file.content);
    }
    catch (const nlohmann::json::parse_error& parse_error)
    {
        return ParseResult<Timer32Knowledge>{false, std::string("Ошибка разбора knowledge_base: ") + parse_error.what(), std::nullopt};
    }

    Timer32Knowledge knowledge;
    try
    {
        knowledge.max_instances = root.at("max_instances").get<int>();

        for (auto it = root.at("instance_handle").begin();
             it != root.at("instance_handle").end(); ++it)
        {
            knowledge.instance_handle[std::stoi(it.key())] = it.value().get<std::string>();
        }

        for (auto it = root.at("instance_has_channels").begin();
             it != root.at("instance_has_channels").end(); ++it)
        {
            knowledge.instance_has_channels[std::stoi(it.key())] = it.value().get<bool>();
        }

        for (auto it = root.at("count_mode_hal_enum").begin();
             it != root.at("count_mode_hal_enum").end(); ++it)
        {
            knowledge.count_mode_hal_enum[it.key()] = it.value().get<std::string>();
        }

        for (auto it = root.at("channel_mode_hal_enum").begin();
             it != root.at("channel_mode_hal_enum").end(); ++it)
        {
            knowledge.channel_mode_hal_enum[it.key()] = it.value().get<std::string>();
        }

        for (auto it = root.at("capture_edge_hal_enum").begin();
             it != root.at("capture_edge_hal_enum").end(); ++it)
        {
            knowledge.capture_edge_hal_enum[it.key()] = it.value().get<std::string>();
        }

        knowledge.sys_clk_hz = root.at("sys_clk_hz").get<long>();

        for (auto instance_it = root.at("instance_channel_fixed_pin").begin();
             instance_it != root.at("instance_channel_fixed_pin").end(); ++instance_it)
        {
            std::map<int, Timer32Knowledge::FixedPin> channels;
            for (auto channel_it = instance_it.value().begin();
                 channel_it != instance_it.value().end(); ++channel_it)
            {
                Timer32Knowledge::FixedPin pin;
                pin.gpio_instance = channel_it.value().at("gpio_instance").get<int>();
                pin.pin = channel_it.value().at("pin").get<int>();
                channels[std::stoi(channel_it.key())] = pin;
            }
            knowledge.instance_channel_fixed_pin[std::stoi(instance_it.key())] = channels;
        }
    }
    catch (const nlohmann::json::exception& structure_error)
    {
        return ParseResult<Timer32Knowledge>{false, std::string("knowledge_base/timer32.json не соответствует ожидаемой структуре: ") + structure_error.what(), std::nullopt};
    }

    return ParseResult<Timer32Knowledge>{true, "", knowledge};
}
