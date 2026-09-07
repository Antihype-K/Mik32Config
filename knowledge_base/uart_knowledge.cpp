#include "uart_knowledge.h"
#include "utf8_path.h"
#include "json.hpp"
#include <fstream>

namespace
{
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

ParseResult<UartKnowledge> loadUartKnowledge(const std::string& file_path)
{
    std::ifstream input_file(utf8Path(file_path));
    if (!input_file.is_open())
    {
        return ParseResult<UartKnowledge>{false, "Не удалось открыть knowledge_base: " + file_path, std::nullopt};
    }

    nlohmann::json root;
    try
    {
        input_file >> root;
    }
    catch (const nlohmann::json::parse_error& parse_error)
    {
        return ParseResult<UartKnowledge>{false, std::string("Ошибка разбора knowledge_base: ") + parse_error.what(), std::nullopt};
    }

    UartKnowledge knowledge;
    try
    {
        knowledge.max_instances = root.at("max_instances").get<int>();

        for (auto it = root.at("instance_handle").begin();
             it != root.at("instance_handle").end(); ++it)
        {
            knowledge.instance_handle[std::stoi(it.key())] = it.value().get<std::string>();
        }

        knowledge.data_bits_hal_enum = extractHalEnumMap(root.at("data_bits_encoding"));
        knowledge.stop_bits_hal_enum = extractHalEnumMap(root.at("stop_bits_encoding"));

        for (auto it = root.at("parity_encoding").begin();
             it != root.at("parity_encoding").end(); ++it)
        {
            UartKnowledge::ParitySetting setting;
            setting.parity_bit = it.value().at("parity_bit").get<std::string>();
            setting.parity_bit_inversion = it.value().at("parity_bit_inversion").get<std::string>();
            knowledge.parity_hal_setting[it.key()] = setting;
        }

        knowledge.hsi_hz = root.at("hsi_hz").get<long>();

        auto readFixedPin = [](const nlohmann::json& node) {
            UartKnowledge::FixedPin pin;
            pin.gpio_instance = node.at("gpio_instance").get<int>();
            pin.pin = node.at("pin").get<int>();
            return pin;
        };
        for (auto it = root.at("instance_fixed_pins").begin();
             it != root.at("instance_fixed_pins").end(); ++it)
        {
            UartKnowledge::InstanceFixedPins pins;
            pins.tx = readFixedPin(it.value().at("tx"));
            pins.rx = readFixedPin(it.value().at("rx"));
            pins.rts = readFixedPin(it.value().at("rts"));
            pins.cts = readFixedPin(it.value().at("cts"));
            knowledge.instance_fixed_pins[std::stoi(it.key())] = pins;
        }
    }
    catch (const nlohmann::json::exception& structure_error)
    {
        return ParseResult<UartKnowledge>{false, std::string("knowledge_base/uart.json не соответствует ожидаемой структуре: ") + structure_error.what(), std::nullopt};
    }

    return ParseResult<UartKnowledge>{true, "", knowledge};
}
