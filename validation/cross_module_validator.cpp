#include "cross_module_validator.h"
#include "json.hpp"
#include <fstream>
#include <map>
#include <sstream>

namespace
{
    using PinKey = std::pair<int, int>;  // (gpio_instance, pin)

    void addUartClaim(
        std::map<PinKey, std::vector<std::string>>& claims,
        const UartKnowledge::FixedPin& fixed_pin,
        int uart_instance,
        const char* role)
    {
        std::ostringstream owner;
        owner << "UART instance " << uart_instance << " (" << role << ")";
        claims[{fixed_pin.gpio_instance, fixed_pin.pin}].push_back(owner.str());
    }
}

CrossModuleValidationResult validatePinConflicts(
    const std::string& config_path,
    const UartKnowledge& uart_knowledge)
{
    std::ifstream input_file(config_path);
    if (!input_file.is_open())
    {
        return CrossModuleValidationResult{false, "Не удалось открыть файл конфигурации: " + config_path, {}};
    }

    nlohmann::json root;
    try
    {
        input_file >> root;
    }
    catch (const nlohmann::json::parse_error& parse_error)
    {
        return CrossModuleValidationResult{false, std::string("Ошибка разбора JSON: ") + parse_error.what(), {}};
    }

    if (!root.contains("peripherals") || !root["peripherals"].is_array())
    {
        return CrossModuleValidationResult{false, "Конфигурация не содержит массив \"peripherals\"", {}};
    }

    // (gpio_instance, pin) -> список описаний блоков, претендующих на этот пин.
    std::map<PinKey, std::vector<std::string>> claims;

    int gpio_block_index = 0;
    int uart_block_index = 0;

    for (const auto& peripheral : root["peripherals"])
    {
        if (!peripheral.contains("type") || !peripheral["type"].is_string())
        {
            continue;
        }
        const std::string type = peripheral["type"].get<std::string>();

        if (type == "gpio")
        {
            ++gpio_block_index;
            if (!peripheral.contains("instance") || !peripheral["instance"].is_number_integer()
                || !peripheral.contains("pins") || !peripheral["pins"].is_array())
            {
                // Блок не прошёл бы parseGpioConfig — пропускаем здесь,
                // диагностику этой ошибки даёт сам parseGpioConfig раньше
                // в пайплайне (per-module валидация всегда идёт первой).
                continue;
            }
            const int instance = peripheral["instance"].get<int>();
            std::ostringstream owner;
            owner << "GPIO instance " << instance << " (блок #" << gpio_block_index << ")";
            for (const auto& pin_value : peripheral["pins"])
            {
                if (!pin_value.is_number_integer())
                {
                    continue;
                }
                claims[{instance, pin_value.get<int>()}].push_back(owner.str());
            }
        }
        else if (type == "uart")
        {
            ++uart_block_index;
            if (!peripheral.contains("instance") || !peripheral["instance"].is_number_integer())
            {
                continue;
            }
            const int instance = peripheral["instance"].get<int>();
            auto fixed_pins_it = uart_knowledge.instance_fixed_pins.find(instance);
            if (fixed_pins_it == uart_knowledge.instance_fixed_pins.end())
            {
                continue;  // невалидный instance — та же логика: parseUsartConfig это отловит раньше.
            }
            const UartKnowledge::InstanceFixedPins& fixed = fixed_pins_it->second;

            // TX/RX заняты всегда, независимо от direction — HAL_USART_Init
            // настраивает оба пина в serial-режим согласно transmitting/
            // receiving (mik32_hal_usart.c), но даже "закрытый" по
            // direction пин физически остаётся замультиплексирован на
            // UART и не может параллельно использоваться GPIO.
            addUartClaim(claims, fixed.tx, instance, "TX");
            addUartClaim(claims, fixed.rx, instance, "RX");

            const bool flow_control = peripheral.contains("flow_control")
                && peripheral["flow_control"].is_boolean()
                && peripheral["flow_control"].get<bool>();
            if (flow_control)
            {
                addUartClaim(claims, fixed.rts, instance, "RTS");
                addUartClaim(claims, fixed.cts, instance, "CTS");
            }
        }
    }

    CrossModuleValidationResult result{true, "", {}};
    for (const auto& [pin_key, owners] : claims)
    {
        if (owners.size() > 1)
        {
            result.conflicts.push_back(PinConflict{pin_key.first, pin_key.second, owners});
        }
    }

    return result;
}
