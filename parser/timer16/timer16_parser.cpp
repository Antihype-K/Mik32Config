#include "timer16_parser.h"
#include "win_file_io.h"
#include "json.hpp"
#include <algorithm>
#include <cmath>
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

    // OVERNIGHT_FIELD_REFERENCE.md: knowledge_base/timer16.json, sys_clk_hz —
    // ТЗ §3.1.8, HSI 32МГц, валидно при clock.ahb_divider=0 (дефолт). Если
    // пользователь задаст ненулевой ahb_divider в top-level "clock", эта
    // формула НЕ учитывает делитель (Timer16Config парсится из одного блока
    // peripherals[], без доступа к top-level "clock") — известное
    // ограничение этой сессии, см. OVERNIGHT_FIELD_REFERENCE.md.
    constexpr double kSysClkHz = 32000000.0;

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
    for (const char* field : {"instance", "mode"})
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

    Timer16Config config;
    config.type = "timer16";
    config.instance = instance;
    config.mode = timer16["mode"].get<std::string>();

    // OVERNIGHT_FIELD_REFERENCE.md: два взаимоисключающих способа задать
    // период/скважность — либо прямые регистры (prescaler+arr+cmp, как
    // раньше), либо frequency_hz+duty_percent (парсер сам считает регистры).
    const bool has_frequency_field = timer16.contains("frequency_hz");
    const bool has_duty_field = timer16.contains("duty_percent");
    const bool has_raw_registers = timer16.contains("prescaler") || timer16.contains("arr") || timer16.contains("cmp");

    if (has_frequency_field != has_duty_field)
    {
        return makeError("Поля \"frequency_hz\" и \"duty_percent\" обязаны присутствовать вместе");
    }

    if (has_frequency_field && has_raw_registers)
    {
        return makeError("Нельзя одновременно задавать \"frequency_hz\"/\"duty_percent\" и \"prescaler\"/\"arr\"/\"cmp\" — выберите один способ");
    }

    if (has_frequency_field)
    {
        if (!timer16["frequency_hz"].is_number())
        {
            return makeError("Поле \"frequency_hz\" должно быть числом");
        }
        const double frequency_hz = timer16["frequency_hz"].get<double>();
        if (frequency_hz <= 0.0)
        {
            return makeError("Поле \"frequency_hz\" должно быть положительным");
        }

        if (!timer16["duty_percent"].is_number())
        {
            return makeError("Поле \"duty_percent\" должно быть числом");
        }
        const double duty_percent = timer16["duty_percent"].get<double>();
        if (duty_percent < 0.0 || duty_percent > 100.0)
        {
            return makeError("Поле \"duty_percent\" вне диапазона 0-100");
        }

        // Подбор наименьшего (= максимальное разрешение ARR) делителя из
        // восьми аппаратных значений, при котором период укладывается в
        // 16-битный ARR. kSysClkHz — см. комментарий у константы выше.
        bool found_prescaler = false;
        int chosen_prescaler = 0;
        long chosen_period_ticks = 0;
        for (const int candidate : {1, 2, 4, 8, 16, 32, 64, 128})
        {
            const double tick_freq = kSysClkHz / candidate;
            const long period_ticks = std::lround(tick_freq / frequency_hz);
            // period_ticks = ARR+1 -> ARR = period_ticks-1, должно попасть в 1..65536.
            if (period_ticks >= 1 && period_ticks <= 65536)
            {
                found_prescaler = true;
                chosen_prescaler = candidate;
                chosen_period_ticks = period_ticks;
                break;
            }
        }
        if (!found_prescaler)
        {
            return makeError("Поле \"frequency_hz\" недостижимо ни одним из 8 аппаратных делителей "
                              "(слишком низкая частота для 16-битного ARR даже при prescaler=128, "
                              "либо слишком высокая для частоты шины)");
        }

        const long arr_value = chosen_period_ticks - 1;
        const long cmp_value = std::lround(static_cast<double>(chosen_period_ticks) * duty_percent / 100.0);

        config.has_frequency_duty = true;
        config.requested_frequency_hz = frequency_hz;
        config.requested_duty_percent = duty_percent;
        config.prescaler = chosen_prescaler;
        config.arr = static_cast<uint16_t>(arr_value);
        config.cmp = static_cast<uint16_t>(std::min<long>(cmp_value, arr_value));

        return ParseResult<Timer16Config>{true, "", config};
    }

    // Путь без frequency_hz/duty_percent — prescaler/arr/cmp обязательны,
    // поведение идентично версии до этой сессии (обратная совместимость).
    for (const char* field : {"prescaler", "arr", "cmp"})
    {
        if (!timer16.contains(field))
        {
            return makeError(std::string("Отсутствует обязательное поле \"") + field + "\"");
        }
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

    config.prescaler = timer16["prescaler"].get<int>();
    config.arr = static_cast<uint16_t>(arr);
    config.cmp = static_cast<uint16_t>(cmp);

    return ParseResult<Timer16Config>{true, "", config};
}
