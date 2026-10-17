#include "dac_knowledge.h"
#include "win_file_io.h"
#include "json.hpp"

ParseResult<DacKnowledge> loadDacKnowledge(const std::string& file_path)
{
    const FileReadOutcome file = readUtf8PathFile(file_path);
    if (!file.success)
    {
        return ParseResult<DacKnowledge>{false, "Не удалось открыть knowledge_base: " + file_path + file.diagnostic, std::nullopt};
    }

    nlohmann::json root;
    try
    {
        root = nlohmann::json::parse(file.content);
    }
    catch (const nlohmann::json::parse_error& parse_error)
    {
        return ParseResult<DacKnowledge>{false, std::string("Ошибка разбора knowledge_base: ") + parse_error.what(), std::nullopt};
    }

    DacKnowledge knowledge;
    try
    {
        for (auto it = root.at("instance_handle").begin(); it != root.at("instance_handle").end(); ++it)
        {
            knowledge.instance_handle[std::stoi(it.key())] = it.value().get<std::string>();
        }

        for (auto it = root.at("instance_fixed_pin").begin(); it != root.at("instance_fixed_pin").end(); ++it)
        {
            DacKnowledge::FixedPin pin;
            pin.gpio_instance = it.value().at("gpio_instance").get<int>();
            pin.pin = it.value().at("pin").get<int>();
            knowledge.instance_fixed_pin[std::stoi(it.key())] = pin;
        }

        knowledge.dac_ref_pin.gpio_instance = root.at("dac_ref_pin").at("gpio_instance").get<int>();
        knowledge.dac_ref_pin.pin = root.at("dac_ref_pin").at("pin").get<int>();
    }
    catch (const nlohmann::json::exception& structure_error)
    {
        return ParseResult<DacKnowledge>{false, std::string("knowledge_base/dac.json не соответствует ожидаемой структуре: ") + structure_error.what(), std::nullopt};
    }

    return ParseResult<DacKnowledge>{true, "", knowledge};
}
