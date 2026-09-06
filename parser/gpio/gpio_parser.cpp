//
// Created by antihype on 31.07.2026.
//

#include "gpio_parser.h"
#include "json.hpp"
#include <fstream>
#include <set>

// Концепция: правила ниже дословно повторяют schemas/gpio.json (записан
// пользователем). Полноценный универсальный "загрузчик произвольной схемы"
// (упомянутый в §8 архитектурного документа) в этой сессии не строится —
// вместо этого правила захардкожены здесь и обязаны совпадать с файлом схемы;
// расхождение с schemas/gpio.json надо чинить в обоих местах одновременно.
namespace
{
    // enum "mode" разрешён к 5 значениям — ровно 4 функции PAD_CONFIG
    // (Датащит, Таблица 98, с.182-183: 0b00 порт общего назначения,
    // 0b01 последовательный интерфейс, 0b10 последовательный интерфейс/таймер,
    // 0b11 аналоговый сигнал), при этом "порт общего назначения" в схеме
    // разбит на input/output — сама PAD_CONFIG-функция для них одна и та же,
    // разница (вход/выход) кодируется отдельным регистром DIRECTION_IN/OUT
    // модуля GPIO (Таблица 96, с.180-181), а не PAD_CONFIG.
    const std::set<std::string> kValidModes = {
        "input", "output", "serial", "timer_serial", "analog"
    };

    // enum "pull" — Датащит, Таблица 100, с.184-185: значение 0b11
    // зарезервировано, поэтому валидных вариантов только 3, а не 4.
    const std::set<std::string> kValidPulls = {
        "none", "pull_up", "pull_down"
    };

    // enum "drive_strength" — Датащит, Таблица 99, с.183-184: 0b10 и 0b11
    // оба дают 8мА, поэтому в схеме (и здесь) выставлен один пользовательский
    // вариант "8mA", а не два неразличимых.
    const std::set<std::string> kValidDriveStrengths = {
        "2mA", "4mA", "8mA"
    };

    // Датащит, Таблица 2, с.11-12: в карте памяти перечислены ровно три блока
    // GPIO (GPIO_0/1/2) — четвёртого адреса в этой шине нет.
    constexpr int kMinInstance = 0;
    constexpr int kMaxInstance = 2;

    // schemas/gpio.json: "item_min": 0, "item_max": 15 — общая граница для
    // instance 0 и 1 (см. kMaxPinByInstance ниже для instance 2, у которого
    // граница уже).
    constexpr int kMinPin = 0;
    constexpr int kMaxPin = 15;

    // schemas/gpio.json, "pins.item_max_by_instance": Датащит, Таблица 193
    // «Назначение выводов», с.320 — в корпусе разведено только GPIO2_0..
    // GPIO2_7 (8 пинов), у GPIO0/GPIO1 распаяны все 16 (GPIOx_0..GPIOx_15).
    // Подтверждённый факт, не гипотеза — см. docs/implementations/gpio.md.
    int maxPinForInstance(int instance)
    {
        return instance == 2 ? 7 : kMaxPin;
    }

    ParseResult<GpioConfig> makeError(const std::string& message)
    {
        return ParseResult<GpioConfig>{false, message, std::nullopt};
    }
}

ParseResult<GpioConfig> parseGpioConfig(const std::string& file_name_path)
{
    // Концепция: открываем файл конфигурации пользователя вручную (а не
    // передаём путь напрямую в nlohmann::json), чтобы отличить "файла нет"
    // от "файл есть, но в нём невалидный JSON" — это два разных, отдельно
    // диагностируемых случая для пользователя конфигуратора.
    std::ifstream input_file(file_name_path);
    if (!input_file.is_open())
    {
        return makeError("Не удалось открыть файл конфигурации: " + file_name_path);
    }

    nlohmann::json root;
    try
    {
        input_file >> root;
    }
    catch (const nlohmann::json::parse_error& parse_error)
    {
        return makeError(std::string("Ошибка разбора JSON: ") + parse_error.what());
    }

    // Формат верхнего уровня {mcu, version, peripherals: [...]} задан
    // архитектурным документом, §"Общий пайплайн" — каждый элемент
    // peripherals диспетчеризуется по полю "type".
    if (!root.contains("peripherals") || !root["peripherals"].is_array())
    {
        return makeError("Конфигурация не содержит массив \"peripherals\"");
    }

    const nlohmann::json* gpio_node = nullptr;
    for (const auto& peripheral : root["peripherals"])
    {
        if (peripheral.contains("type") && peripheral["type"].is_string()
            && peripheral["type"].get<std::string>() == "gpio")
        {
            gpio_node = &peripheral;
            break;
        }
    }

    if (gpio_node == nullptr)
    {
        return makeError("В конфигурации не найден блок peripherals с type=\"gpio\"");
    }

    const nlohmann::json& gpio = *gpio_node;

    // schemas/gpio.json: "required_fields" — все пять полей обязательны,
    // отсутствие любого делает блок невалидным целиком.
    for (const char* field : {"instance", "mode", "pull", "drive_strength", "pins"})
    {
        if (!gpio.contains(field))
        {
            return makeError(std::string("Отсутствует обязательное поле \"") + field + "\"");
        }
    }

    if (!gpio["instance"].is_number_integer())
    {
        return makeError("Поле \"instance\" должно быть целым числом");
    }
    const int instance = gpio["instance"].get<int>();
    if (instance < kMinInstance || instance > kMaxInstance)
    {
        return makeError("Поле \"instance\" вне диапазона 0-2 (в МИК32 три блока GPIO)");
    }

    if (!gpio["mode"].is_string() || !kValidModes.count(gpio["mode"].get<std::string>()))
    {
        return makeError("Поле \"mode\" содержит недопустимое значение");
    }

    if (!gpio["pull"].is_string() || !kValidPulls.count(gpio["pull"].get<std::string>()))
    {
        return makeError("Поле \"pull\" содержит недопустимое значение");
    }

    if (!gpio["drive_strength"].is_string()
        || !kValidDriveStrengths.count(gpio["drive_strength"].get<std::string>()))
    {
        return makeError("Поле \"drive_strength\" содержит недопустимое значение");
    }

    if (!gpio["pins"].is_array())
    {
        return makeError("Поле \"pins\" должно быть массивом");
    }

    // schemas/gpio.json: "min_length": 1 — блок GPIO без единого пина
    // бессмыслен (нечего инициализировать), поэтому запрещён явно, а не
    // тихо генерирует пустой HAL_GPIO_Init.
    if (gpio["pins"].empty())
    {
        return makeError("Поле \"pins\" не может быть пустым");
    }

    const int max_pin = maxPinForInstance(instance);

    std::vector<uint8_t> pins;
    std::set<int> seen_pins;
    for (const auto& pin_value : gpio["pins"])
    {
        if (!pin_value.is_number_integer())
        {
            return makeError("Элемент массива \"pins\" должен быть целым числом");
        }
        const int pin = pin_value.get<int>();
        if (pin < kMinPin || pin > max_pin)
        {
            return makeError("Номер пина вне диапазона 0-" + std::to_string(max_pin)
                + " для instance " + std::to_string(instance));
        }
        // schemas/gpio.json: "allow_duplicates": false — один и тот же пин
        // не может быть настроен дважды с потенциально разными атрибутами
        // в рамках одного блока.
        if (!seen_pins.insert(pin).second)
        {
            return makeError("Массив \"pins\" содержит повторяющийся номер пина");
        }
        pins.push_back(static_cast<uint8_t>(pin));
    }

    GpioConfig config;
    config.type = "gpio";
    config.instance = instance;
    config.mode = gpio["mode"].get<std::string>();
    config.pull = gpio["pull"].get<std::string>();
    config.drive_strength = gpio["drive_strength"].get<std::string>();
    config.pins = std::move(pins);

    return ParseResult<GpioConfig>{true, "", config};
}

