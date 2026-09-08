#include "package_knowledge.h"
#include "win_file_io.h"
#include "json.hpp"

const PackagePin* PackageKnowledge::findPin(int pin_number) const
{
    for (const PackagePin& pin : pins)
    {
        if (pin.pin == pin_number)
        {
            return &pin;
        }
    }
    return nullptr;
}

const PackagePin* PackageKnowledge::findByGpio(int gpio_instance_value, int gpio_pin_value) const
{
    for (const PackagePin& pin : pins)
    {
        if (pin.gpio_instance && *pin.gpio_instance == gpio_instance_value
            && pin.gpio_pin && *pin.gpio_pin == gpio_pin_value)
        {
            return &pin;
        }
    }
    return nullptr;
}

ParseResult<PackageKnowledge> loadPackageKnowledge(const std::string& file_path)
{
    const FileReadOutcome file = readUtf8PathFile(file_path);
    if (!file.success)
    {
        return ParseResult<PackageKnowledge>{false, "Не удалось открыть knowledge_base: " + file_path + file.diagnostic, std::nullopt};
    }

    nlohmann::json root;
    try
    {
        root = nlohmann::json::parse(file.content);
    }
    catch (const nlohmann::json::parse_error& parse_error)
    {
        return ParseResult<PackageKnowledge>{
            false, std::string("Ошибка разбора knowledge_base: ") + parse_error.what(), std::nullopt};
    }

    PackageKnowledge knowledge;
    try
    {
        knowledge.package = root.at("package").get<std::string>();
        knowledge.pin_count = root.at("pin_count").get<int>();

        for (const auto& pin_node : root.at("pins"))
        {
            PackagePin pin;
            pin.pin = pin_node.at("pin").get<int>();
            pin.name = pin_node.at("name").get<std::string>();
            // gpio_instance/gpio_pin в JSON равны null для служебных выводов —
            // is_null() отличает «служебный вывод» от «повреждённый файл».
            if (!pin_node.at("gpio_instance").is_null())
            {
                pin.gpio_instance = pin_node.at("gpio_instance").get<int>();
                pin.gpio_pin = pin_node.at("gpio_pin").get<int>();
            }
            knowledge.pins.push_back(pin);
        }
    }
    catch (const nlohmann::json::exception& json_error)
    {
        return ParseResult<PackageKnowledge>{
            false, std::string("Некорректная структура knowledge_base: ") + json_error.what(), std::nullopt};
    }

    // Проверка полноты: файл описывает корпус целиком, частично заполненная
    // карта выводов молча дала бы GUI дырявую схему вместо ошибки.
    if (static_cast<int>(knowledge.pins.size()) != knowledge.pin_count)
    {
        return ParseResult<PackageKnowledge>{
            false,
            "В knowledge_base/package.json описано " + std::to_string(knowledge.pins.size())
                + " выводов при заявленных pin_count=" + std::to_string(knowledge.pin_count),
            std::nullopt};
    }

    return ParseResult<PackageKnowledge>{true, "", knowledge};
}
