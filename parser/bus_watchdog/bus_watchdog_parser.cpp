#include "bus_watchdog_parser.h"
#include "win_file_io.h"
#include "json.hpp"
#include <cstdint>
#include <limits>

namespace
{
    ParseResult<BusWatchdogConfig> makeError(const std::string& message)
    {
        return ParseResult<BusWatchdogConfig>{false, message, std::nullopt};
    }
}

ParseResult<BusWatchdogConfig> parseBusWatchdogConfig(const std::string& file_name_path)
{
    const FileReadOutcome file = readUtf8PathFile(file_name_path);
    if (!file.success)
    {
        return makeError("Не удалось открыть файл конфигурации: " + file_name_path + file.diagnostic);
    }

    nlohmann::json root;
    try
    {
        root = nlohmann::json::parse(file.content);
    }
    catch (const nlohmann::json::parse_error& parse_error)
    {
        return makeError(std::string("Ошибка разбора JSON: ") + parse_error.what());
    }

    if (!root.contains("peripherals") || !root["peripherals"].is_array())
    {
        return makeError("Конфигурация не содержит массив \"peripherals\"");
    }

    const nlohmann::json* node = nullptr;
    for (const auto& peripheral : root["peripherals"])
    {
        if (peripheral.contains("type") && peripheral["type"].is_string()
            && peripheral["type"].get<std::string>() == "bus_watchdog")
        {
            node = &peripheral;
            break;
        }
    }

    if (node == nullptr)
    {
        return makeError("В конфигурации не найден блок peripherals с type=\"bus_watchdog\"");
    }

    return parseBusWatchdogBlock(*node);
}

ParseResult<BusWatchdogConfig> parseBusWatchdogBlock(const nlohmann::json& bus_watchdog)
{
    BusWatchdogConfig config;
    config.type = "bus_watchdog";

    if (bus_watchdog.contains("eeprom_enabled"))
    {
        if (!bus_watchdog["eeprom_enabled"].is_boolean())
        {
            return makeError("Поле \"eeprom_enabled\" должно быть булевым");
        }
        config.eeprom_enabled = bus_watchdog["eeprom_enabled"].get<bool>();
    }

    if (bus_watchdog.contains("spifi_enabled"))
    {
        if (!bus_watchdog["spifi_enabled"].is_boolean())
        {
            return makeError("Поле \"spifi_enabled\" должно быть булевым");
        }
        config.spifi_enabled = bus_watchdog["spifi_enabled"].get<bool>();
    }

    if (bus_watchdog.contains("dom3_enabled"))
    {
        if (!bus_watchdog["dom3_enabled"].is_boolean())
        {
            return makeError("Поле \"dom3_enabled\" должно быть булевым");
        }
        config.dom3_enabled = bus_watchdog["dom3_enabled"].get<bool>();
    }

    if (bus_watchdog.contains("timeout"))
    {
        if (!bus_watchdog["timeout"].is_number_integer() && !bus_watchdog["timeout"].is_number_unsigned())
        {
            return makeError("Поле \"timeout\" должно быть целым числом");
        }
        const int64_t timeout = bus_watchdog["timeout"].get<int64_t>();
        // WDT_BUS->TIMEOUT объявлен в wdt_bus.h как полные 32 бита, но HW-тест
        // 2026-10-14 показал, что реально реализованы только младшие 4 бита:
        // запись 0xFFFFFFFF считалась обратно как 0x0000000F. До этого фикса
        // здесь разрешался диапазон 0-4294967295 — timeout=1000000 (младший
        // нибл = 0) молча обрезался до ЭФФЕКТИВНОГО 0 на линии dom3 (шина APB,
        // через которую работает весь чип) и плата переставала отвечать сразу
        // после прошивки (непрерывный сброс по таймауту шины). См.
        // schemas/bus_watchdog.json, поле timeout, и AUDIT_TZ_PERIPHERY_GAPS.md.
        if (timeout < 0 || timeout > 15)
        {
            return makeError("Поле \"timeout\" вне диапазона 0-15 (WDT_BUS->TIMEOUT реально реализован только "
                              "4 битами, несмотря на 32-битное объявление в wdt_bus.h — подтверждено HW-тестом "
                              "2026-10-14, см. schemas/bus_watchdog.json)");
        }
        config.timeout = static_cast<uint32_t>(timeout);
    }

    if ((config.eeprom_enabled || config.spifi_enabled || config.dom3_enabled) && config.timeout == 0)
    {
        return makeError("Хотя бы одна линия bus_watchdog включена (eeprom_enabled/spifi_enabled/dom3_enabled),"
                          " но поле \"timeout\" равно 0 или не задано — сторожевой таймер шины сработает немедленно");
    }

    return ParseResult<BusWatchdogConfig>{true, "", config};
}
