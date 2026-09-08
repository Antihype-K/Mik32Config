#include "timer16_parser.h"
#include "win_file_io.h"
#include "json.hpp"
#include <set>

// Концепция: как и в parser/gpio/gpio_parser.cpp и parser/USART/usart_parser.cpp,
// правила ниже дословно повторяют schemas/timer16.json.
namespace
{
    // ТЗ §3.1.4: «МК МИК32 «Амур» содержит 3 модуля Timer16».
    constexpr int kMinInstance = 0;
    constexpr int kMaxInstance = 2;

    // schemas/timer16.json: "mode" — ТЗ §3.1.4 п.2, три режима работы.
    const std::set<std::string> kValidModes = {"period", "one_shot", "external_event_count"};

    // schemas/timer16.json: "prescaler" enum_int — mik32_hal_timer16.h,
    // HAL_Timer16_PrescalerTypeDef (8 значений, степени двойки 1..128).
    const std::set<int> kValidPrescalers = {1, 2, 4, 8, 16, 32, 64, 128};

    // schemas/timer16.json: "arr"/"cmp" — регистры Timer16 16-битные.
    constexpr int kMinRegisterValue = 0;
    constexpr int kMaxRegisterValue = 65535;

    ParseResult<Timer16Config> makeError(const std::string& message)
    {
        return ParseResult<Timer16Config>{false, message, std::nullopt};
    }
}

ParseResult<Timer16Config> parseTimer16Config(const std::string& file_name_path)
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

    const nlohmann::json* timer16_node = nullptr;
    for (const auto& peripheral : root["peripherals"])
    {
        if (peripheral.contains("type") && peripheral["type"].is_string()
            && peripheral["type"].get<std::string>() == "timer16")
        {
            timer16_node = &peripheral;
            break;
        }
    }

    if (timer16_node == nullptr)
    {
        return makeError("В конфигурации не найден блок peripherals с type=\"timer16\"");
    }

    return parseTimer16Block(*timer16_node);
}

ParseResult<Timer16Config> parseTimer16Block(const nlohmann::json& timer16)
{

    for (const char* field : {"instance", "mode", "prescaler", "arr", "cmp"})
    {
        if (!timer16.contains(field))
        {
            return makeError(std::string("Отсутствует обязательное поле \"") + field + "\"");
        }
    }

    if (!timer16["instance"].is_number_integer())
    {
        return makeError("Поле \"instance\" должно быть целым числом");
    }
    const int instance = timer16["instance"].get<int>();
    if (instance < kMinInstance || instance > kMaxInstance)
    {
        return makeError("Поле \"instance\" вне диапазона 0-2 (в МИК32 три блока Timer16)");
    }

    if (!timer16["mode"].is_string() || !kValidModes.count(timer16["mode"].get<std::string>()))
    {
        return makeError("Поле \"mode\" содержит недопустимое значение (разрешено period, one_shot, external_event_count)");
    }

    if (!timer16["prescaler"].is_number_integer()
        || !kValidPrescalers.count(timer16["prescaler"].get<int>()))
    {
        return makeError("Поле \"prescaler\" содержит недопустимое значение (разрешены степени двойки 1-128)");
    }

    if (!timer16["arr"].is_number_integer())
    {
        return makeError("Поле \"arr\" должно быть целым числом");
    }
    const int arr = timer16["arr"].get<int>();
    if (arr < kMinRegisterValue || arr > kMaxRegisterValue)
    {
        return makeError("Поле \"arr\" вне диапазона 0-65535 (регистр ARR 16-битный)");
    }

    if (!timer16["cmp"].is_number_integer())
    {
        return makeError("Поле \"cmp\" должно быть целым числом");
    }
    const int cmp = timer16["cmp"].get<int>();
    // schemas/timer16.json: "max_field": "arr" — верхняя граница CMP не
    // фиксированное число, а значение ARR того же блока (ТЗ §3.1.4 п.2:
    // «диапазон 0-ARR»).
    if (cmp < kMinRegisterValue || cmp > arr)
    {
        return makeError("Поле \"cmp\" вне диапазона 0-ARR для этого блока");
    }

    Timer16Config config;
    config.type = "timer16";
    config.instance = instance;
    config.mode = timer16["mode"].get<std::string>();
    config.prescaler = timer16["prescaler"].get<int>();
    config.arr = static_cast<uint16_t>(arr);
    config.cmp = static_cast<uint16_t>(cmp);

    return ParseResult<Timer16Config>{true, "", config};
}
