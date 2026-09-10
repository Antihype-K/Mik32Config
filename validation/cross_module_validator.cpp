#include "cross_module_validator.h"
#include "win_file_io.h"
#include "json.hpp"
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

    void addTimer16Claim(
        std::map<PinKey, std::vector<std::string>>& claims,
        const Timer16Knowledge::FixedPin& fixed_pin,
        int timer16_instance)
    {
        std::ostringstream owner;
        owner << "Timer16 instance " << timer16_instance << " (PWM)";
        claims[{fixed_pin.gpio_instance, fixed_pin.pin}].push_back(owner.str());
    }

    void addI2cClaim(
        std::map<PinKey, std::vector<std::string>>& claims,
        const I2cKnowledge::FixedPin& fixed_pin,
        int i2c_instance)
    {
        std::ostringstream owner;
        owner << "I2C instance " << i2c_instance << " (SDA/SCL)";
        claims[{fixed_pin.gpio_instance, fixed_pin.pin}].push_back(owner.str());
    }

    void addTimer32Claim(
        std::map<PinKey, std::vector<std::string>>& claims,
        const Timer32Knowledge::FixedPin& fixed_pin,
        int timer32_instance,
        int channel_index)
    {
        std::ostringstream owner;
        owner << "Timer32 instance " << timer32_instance << " (канал " << channel_index << ")";
        claims[{fixed_pin.gpio_instance, fixed_pin.pin}].push_back(owner.str());
    }

    void addSpiClaim(
        std::map<PinKey, std::vector<std::string>>& claims,
        const SpiKnowledge::FixedPin& fixed_pin,
        int spi_instance,
        const char* role)
    {
        std::ostringstream owner;
        owner << "SPI instance " << spi_instance << " (" << role << ")";
        claims[{fixed_pin.gpio_instance, fixed_pin.pin}].push_back(owner.str());
    }
}

CrossModuleValidationResult validatePinConflicts(
    const std::string& config_path,
    const UartKnowledge& uart_knowledge,
    const Timer16Knowledge& timer16_knowledge,
    const I2cKnowledge& i2c_knowledge,
    const Timer32Knowledge& timer32_knowledge,
    const SpiKnowledge& spi_knowledge)
{
    const FileReadOutcome file = readUtf8PathFile(config_path);
    if (!file.success)
    {
        return CrossModuleValidationResult{false, "Не удалось открыть файл конфигурации: " + config_path + file.diagnostic, {}};
    }

    nlohmann::json root;
    try
    {
        root = nlohmann::json::parse(file.content);
    }
    catch (const nlohmann::json::parse_error& parse_error)
    {
        return CrossModuleValidationResult{false, std::string("Ошибка разбора JSON: ") + parse_error.what(), {}};
    }

    return validatePinConflictsInDocument(root, uart_knowledge, timer16_knowledge, i2c_knowledge, timer32_knowledge, spi_knowledge);
}

CrossModuleValidationResult validatePinConflictsInDocument(
    const nlohmann::json& root,
    const UartKnowledge& uart_knowledge,
    const Timer16Knowledge& timer16_knowledge,
    const I2cKnowledge& i2c_knowledge,
    const Timer32Knowledge& timer32_knowledge,
    const SpiKnowledge& spi_knowledge)
{
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
        else if (type == "timer16")
        {
            if (!peripheral.contains("instance") || !peripheral["instance"].is_number_integer()
                || !peripheral.contains("mode") || !peripheral["mode"].is_string())
            {
                continue;
            }
            const int instance = peripheral["instance"].get<int>();
            const std::string mode = peripheral["mode"].get<std::string>();

            // ТЗ §3.1.4 п.4: пин резервируется "при включении экземпляра
            // Timer16" — но только когда выбранный режим реально
            // задействует ШИМ-вывод (period/one_shot); external_event_count
            // ничего не мультиплексирует на этот пин (см.
            // knowledge_base/timer16.json, mode_waveform_enable, и
            // codegen/project/timer16_contribution.cpp).
            auto waveform_it = timer16_knowledge.mode_waveform_enable.find(mode);
            if (waveform_it == timer16_knowledge.mode_waveform_enable.end() || !waveform_it->second)
            {
                continue;
            }

            auto fixed_pin_it = timer16_knowledge.instance_fixed_pwm_pin.find(instance);
            if (fixed_pin_it == timer16_knowledge.instance_fixed_pwm_pin.end())
            {
                continue;  // невалидный instance — parseTimer16Config это отловит раньше.
            }
            addTimer16Claim(claims, fixed_pin_it->second, instance);
        }
        else if (type == "i2c")
        {
            if (!peripheral.contains("instance") || !peripheral["instance"].is_number_integer())
            {
                continue;
            }
            const int instance = peripheral["instance"].get<int>();
            auto fixed_pins_it = i2c_knowledge.instance_fixed_pins.find(instance);
            if (fixed_pins_it == i2c_knowledge.instance_fixed_pins.end())
            {
                continue;  // невалидный instance — parseI2cConfig это отловит раньше.
            }
            for (const I2cKnowledge::FixedPin& pin : fixed_pins_it->second)
            {
                addI2cClaim(claims, pin, instance);
            }
        }
        else if (type == "timer32")
        {
            if (!peripheral.contains("instance") || !peripheral["instance"].is_number_integer()
                || !peripheral.contains("channels") || !peripheral["channels"].is_array())
            {
                continue;  // без channels нечего резервировать; instance=0 сюда и не попадёт по схеме.
            }
            const int instance = peripheral["instance"].get<int>();
            auto instance_map_it = timer32_knowledge.instance_channel_fixed_pin.find(instance);
            if (instance_map_it == timer32_knowledge.instance_channel_fixed_pin.end())
            {
                continue;  // Timer32_0 или невалидный instance — parseTimer32Config это отловит раньше.
            }
            for (const auto& channel : peripheral["channels"])
            {
                if (!channel.contains("index") || !channel["index"].is_number_integer())
                {
                    continue;
                }
                const int channel_index = channel["index"].get<int>();
                auto fixed_pin_it = instance_map_it->second.find(channel_index);
                if (fixed_pin_it == instance_map_it->second.end())
                {
                    continue;  // невалидный index — parseTimer32Config это отловит раньше.
                }
                addTimer32Claim(claims, fixed_pin_it->second, instance, channel_index);
            }
        }
        else if (type == "spi")
        {
            if (!peripheral.contains("instance") || !peripheral["instance"].is_number_integer())
            {
                continue;
            }
            const int instance = peripheral["instance"].get<int>();
            auto fixed_pins_it = spi_knowledge.instance_fixed_pins.find(instance);
            if (fixed_pins_it == spi_knowledge.instance_fixed_pins.end())
            {
                continue;  // невалидный instance — parseSpiConfig это отловит раньше.
            }
            // SCK/MOSI/MISO/SS_IN заняты всегда, независимо от mode —
            // HAL_SPI_MspInit настраивает их безусловно (knowledge_base/
            // spi.json, _comment_instance_fixed_pins).
            for (const SpiKnowledge::FixedPin& pin : fixed_pins_it->second)
            {
                addSpiClaim(claims, pin, instance, "SCK/MOSI/MISO/SS_IN");
            }

            // CS-пин заявляется только для mode=master — у slave поле
            // Init.ChipSelect не заполняется реальным HAL, ни один вывод не
            // резервируется (knowledge_base/spi.json,
            // _comment_instance_chip_select_pin).
            const std::string mode = peripheral.value("mode", std::string());
            if (mode == "master" && peripheral.contains("chip_select") && peripheral["chip_select"].is_string())
            {
                const std::string chip_select = peripheral["chip_select"].get<std::string>();
                auto instance_cs_it = spi_knowledge.instance_chip_select_pin.find(instance);
                if (instance_cs_it != spi_knowledge.instance_chip_select_pin.end())
                {
                    auto cs_pin_it = instance_cs_it->second.find(chip_select);
                    if (cs_pin_it != instance_cs_it->second.end())
                    {
                        addSpiClaim(claims, cs_pin_it->second, instance, ("CS " + chip_select).c_str());
                    }
                }
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

namespace
{
    // MVP-классификация (см. cross_module_validator.h, комментарий у
    // ClassifiedPinConflict::errata_note, и docs/implementations/
    // cross_module_validation.md, раздел "MVP-ограничение классификации"):
    // ни knowledge_base, ни PinConflict сейчас не несут структурного
    // признака "этот конфликт — известная errata-совместимая ситуация".
    // Единственный задокументированный кандидат на Warning — SPI Errata
    // п.7.1 (instance=0, mode=master, manual_cs=false, chip_select=cs3,
    // см. codegen/project/spi_contribution.cpp) — не порождает коллизию
    // (gpio_instance, pin) в принципе на текущих данных knowledge_base/
    // spi.json (cs2 и cs3 у instance=0 — разные физические пины), поэтому
    // честная реализация без выдуманной эвристики — классифицировать
    // ВСЕ найденные конфликты как Error. Как только в knowledge_base
    // появится явный маркер вида "errata_compatible_conflict": true на
    // конкретной комбинации параметров, эту функцию нужно будет расширить
    // веткой Warning, читающей этот маркер — не гадать по побочным полям.
    ClassifiedPinConflict classifyConflict(const PinConflict& conflict)
    {
        ClassifiedPinConflict classified;
        classified.gpio_instance = conflict.gpio_instance;
        classified.pin = conflict.pin;
        classified.conflicting_owners = conflict.conflicting_owners;
        classified.severity = ConflictSeverity::Error;
        classified.errata_note = "";
        return classified;
    }

    ClassifiedCrossModuleValidationResult classifyResult(const CrossModuleValidationResult& raw)
    {
        ClassifiedCrossModuleValidationResult classified{raw.success, raw.error, {}};
        classified.conflicts.reserve(raw.conflicts.size());
        for (const PinConflict& conflict : raw.conflicts)
        {
            classified.conflicts.push_back(classifyConflict(conflict));
        }
        return classified;
    }
}

ClassifiedCrossModuleValidationResult classifyPinConflicts(
    const std::string& config_path,
    const UartKnowledge& uart_knowledge,
    const Timer16Knowledge& timer16_knowledge,
    const I2cKnowledge& i2c_knowledge,
    const Timer32Knowledge& timer32_knowledge,
    const SpiKnowledge& spi_knowledge)
{
    return classifyResult(validatePinConflicts(
        config_path, uart_knowledge, timer16_knowledge, i2c_knowledge, timer32_knowledge, spi_knowledge));
}

ClassifiedCrossModuleValidationResult classifyPinConflictsInDocument(
    const nlohmann::json& root,
    const UartKnowledge& uart_knowledge,
    const Timer16Knowledge& timer16_knowledge,
    const I2cKnowledge& i2c_knowledge,
    const Timer32Knowledge& timer32_knowledge,
    const SpiKnowledge& spi_knowledge)
{
    return classifyResult(validatePinConflictsInDocument(
        root, uart_knowledge, timer16_knowledge, i2c_knowledge, timer32_knowledge, spi_knowledge));
}
