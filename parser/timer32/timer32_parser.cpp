#include "timer32_parser.h"
#include "win_file_io.h"
#include "json.hpp"
#include <cmath>
#include <set>

// Концепция: как и в parser/timer16/timer16_parser.cpp, правила ниже дословно
// повторяют schemas/timer32.json, включая условные требования поля "channels"
// (допустимо только при instance=1/2 — см. комментарий в схеме).
namespace
{
    // ТЗ §3.1.5: «МК МИК32 «Амур» содержит 3 модуля Timer32».
    constexpr int kMinInstance = 0;
    constexpr int kMaxInstance = 2;

    constexpr uint32_t kMaxRegisterValue = 4294967295u;

    const std::set<std::string> kValidCountModes = {"forward", "reverse", "bidirectional"};
    const std::set<std::string> kValidClockSources = {
        "prescaler", "tim1_sys_clk", "tim1_hclk", "tim2_osc32k", "tim2_lsi32k", "tx_pad"};
    const std::set<std::string> kValidChannelModes = {"pwm", "capture", "compare"};
    const std::set<std::string> kValidCaptureEdges = {"rising", "falling"};

    constexpr int kMinChannelIndex = 0;
    constexpr int kMaxChannelIndex = 3;
    constexpr size_t kMaxChannels = 4;

    // OVERNIGHT_FIELD_REFERENCE.md: knowledge_base/timer32.json, sys_clk_hz —
    // ТЗ §3.1.8, валидно при clock.ahb_divider=0 (см. то же ограничение у
    // Timer16 — Timer32Config парсится без доступа к top-level "clock").
    constexpr double kSysClkHz = 32000000.0;

    ParseResult<Timer32Config> makeError(const std::string& message)
    {
        return ParseResult<Timer32Config>{false, message, std::nullopt};
    }

    // Возвращает nullptr при успехе, иначе сообщение об ошибке — вызывающая
    // сторона (parseTimer32Block) знает номер канала для контекста сообщения.
    // known_top — итоговое значение top родительского Timer32Config (уже
    // посчитанное к моменту вызова, в т.ч. из frequency_hz, если он был
    // задан) — нужно для пересчёта duty_percent -> ocr.
    std::optional<std::string> parseChannel(const nlohmann::json& channel_node, uint32_t known_top, Timer32ChannelConfig& out)
    {
        for (const char* field : {"index", "mode"})
        {
            if (!channel_node.contains(field))
            {
                return std::string("отсутствует обязательное поле \"") + field + "\"";
            }
        }

        if (!channel_node["index"].is_number_integer())
        {
            return std::string("поле \"index\" должно быть целым числом");
        }
        const int index = channel_node["index"].get<int>();
        if (index < kMinChannelIndex || index > kMaxChannelIndex)
        {
            return std::string("поле \"index\" вне диапазона 0-3 (у Timer32_1/Timer32_2 ровно 4 канала)");
        }

        if (!channel_node["mode"].is_string() || !kValidChannelModes.count(channel_node["mode"].get<std::string>()))
        {
            return std::string("поле \"mode\" содержит недопустимое значение (разрешены pwm, capture, compare)");
        }
        const std::string mode = channel_node["mode"].get<std::string>();

        uint32_t ocr = 0;
        bool has_duty_percent = false;
        double requested_duty_percent = 0.0;
        if (mode == "pwm" || mode == "compare")
        {
            const bool has_ocr_field = channel_node.contains("ocr");
            const bool has_duty_field = (mode == "pwm") && channel_node.contains("duty_percent");

            if (has_ocr_field && has_duty_field)
            {
                return std::string("нельзя одновременно задавать \"ocr\" и \"duty_percent\" — выберите один способ");
            }

            if (has_duty_field)
            {
                if (!channel_node["duty_percent"].is_number())
                {
                    return std::string("поле \"duty_percent\" должно быть числом");
                }
                requested_duty_percent = channel_node["duty_percent"].get<double>();
                if (requested_duty_percent < 0.0 || requested_duty_percent > 100.0)
                {
                    return std::string("поле \"duty_percent\" вне диапазона 0-100");
                }
                // period_ticks = top+1 (по аналогии с Timer16 ARR+1).
                const double period_ticks = static_cast<double>(known_top) + 1.0;
                const double ocr_value = std::round(period_ticks * requested_duty_percent / 100.0);
                ocr = static_cast<uint32_t>(std::min(ocr_value, static_cast<double>(known_top)));
                has_duty_percent = true;
            }
            else if (has_ocr_field)
            {
                if (!channel_node["ocr"].is_number_integer())
                {
                    return std::string("поле \"ocr\" должно быть целым числом");
                }
                const int64_t ocr_raw = channel_node["ocr"].get<int64_t>();
                if (ocr_raw < 0 || static_cast<uint64_t>(ocr_raw) > kMaxRegisterValue)
                {
                    return std::string("поле \"ocr\" вне диапазона 0-4294967295 (регистр OCR 32-битный)");
                }
                ocr = static_cast<uint32_t>(ocr_raw);
            }
            else
            {
                return std::string("поле \"ocr\" (или \"duty_percent\" для mode=pwm) обязательно для mode=") + mode;
            }
        }

        bool pwm_invert = false;
        if (mode == "pwm" && channel_node.contains("pwm_invert"))
        {
            if (!channel_node["pwm_invert"].is_boolean())
            {
                return std::string("поле \"pwm_invert\" должно быть булевым");
            }
            pwm_invert = channel_node["pwm_invert"].get<bool>();
        }

        std::string capture_edge;
        bool noise_filter = false;
        if (mode == "capture")
        {
            if (!channel_node.contains("capture_edge") || !channel_node["capture_edge"].is_string()
                || !kValidCaptureEdges.count(channel_node["capture_edge"].get<std::string>()))
            {
                return std::string("поле \"capture_edge\" обязательно для mode=capture (разрешены rising, falling)");
            }
            capture_edge = channel_node["capture_edge"].get<std::string>();
        }

        // HAL_TIMER32_CHANNEL_NoiseTypeDef Noise — поле общее для всех режимов
        // канала (mik32_hal_timer32.h:138, не только capture), реальный
        // вендорный код выставляет его и на PWM-каналах.
        if (channel_node.contains("noise_filter"))
        {
            if (!channel_node["noise_filter"].is_boolean())
            {
                return std::string("поле \"noise_filter\" должно быть булевым");
            }
            noise_filter = channel_node["noise_filter"].get<bool>();
        }

        out.index = index;
        out.mode = mode;
        out.ocr = ocr;
        out.pwm_invert = pwm_invert;
        out.capture_edge = capture_edge;
        out.noise_filter = noise_filter;
        out.has_duty_percent = has_duty_percent;
        out.requested_duty_percent = requested_duty_percent;
        return std::nullopt;
    }
}

ParseResult<Timer32Config> parseTimer32Config(const std::string& file_name_path)
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

    const nlohmann::json* timer32_node = nullptr;
    for (const auto& peripheral : root["peripherals"])
    {
        if (peripheral.contains("type") && peripheral["type"].is_string()
            && peripheral["type"].get<std::string>() == "timer32")
        {
            timer32_node = &peripheral;
            break;
        }
    }

    if (timer32_node == nullptr)
    {
        return makeError("В конфигурации не найден блок peripherals с type=\"timer32\"");
    }

    return parseTimer32Block(*timer32_node);
}

ParseResult<Timer32Config> parseTimer32Block(const nlohmann::json& timer32)
{
    for (const char* field : {"instance", "count_mode"})
    {
        if (!timer32.contains(field))
        {
            return makeError(std::string("Отсутствует обязательное поле \"") + field + "\"");
        }
    }

    if (!timer32["instance"].is_number_integer())
    {
        return makeError("Поле \"instance\" должно быть целым числом");
    }
    const int instance = timer32["instance"].get<int>();
    if (instance < kMinInstance || instance > kMaxInstance)
    {
        return makeError("Поле \"instance\" вне диапазона 0-2 (в МИК32 три блока Timer32)");
    }

    if (!timer32["count_mode"].is_string() || !kValidCountModes.count(timer32["count_mode"].get<std::string>()))
    {
        return makeError("Поле \"count_mode\" содержит недопустимое значение (разрешены forward, reverse, bidirectional)");
    }

    Timer32Config config;
    config.type = "timer32";
    config.instance = instance;
    config.count_mode = timer32["count_mode"].get<std::string>();

    // AUDIT_TZ_PERIPHERY_GAPS.md, Timer32 — необязательные поля, общие для
    // любого instance/набора каналов, парсятся до instance==0 раннего
    // возврата ниже, чтобы применяться и к базовому счётчику.
    if (timer32.contains("clock_source"))
    {
        if (!timer32["clock_source"].is_string() || !kValidClockSources.count(timer32["clock_source"].get<std::string>()))
        {
            return makeError("Поле \"clock_source\" содержит недопустимое значение "
                "(разрешены prescaler, tim1_sys_clk, tim1_hclk, tim2_osc32k, tim2_lsi32k, tx_pad)");
        }
        config.clock_source = timer32["clock_source"].get<std::string>();
    }

    if (timer32.contains("overflow_interrupt"))
    {
        if (!timer32["overflow_interrupt"].is_boolean())
        {
            return makeError("Поле \"overflow_interrupt\" должно быть булевым");
        }
        config.overflow_interrupt = timer32["overflow_interrupt"].get<bool>();
    }

    if (timer32.contains("underflow_interrupt"))
    {
        if (!timer32["underflow_interrupt"].is_boolean())
        {
            return makeError("Поле \"underflow_interrupt\" должно быть булевым");
        }
        config.underflow_interrupt = timer32["underflow_interrupt"].get<bool>();
    }

    // OVERNIGHT_FIELD_REFERENCE.md: frequency_hz — альтернатива prescaler+top,
    // взаимоисключающая (как у Timer16). prescaler фиксируется в 0 (делитель
    // 1 — максимальное разрешение TOP); при sys_clk=32МГц диапазон
    // достижимых частот при prescaler=0 и 32-битном TOP — от ~0.0075Гц до
    // 32МГц, этого достаточно без перебора других делителей.
    const bool has_frequency_field = timer32.contains("frequency_hz");
    const bool has_raw_registers = timer32.contains("prescaler") || timer32.contains("top");

    if (has_frequency_field && has_raw_registers)
    {
        return makeError("Нельзя одновременно задавать \"frequency_hz\" и \"prescaler\"/\"top\" — выберите один способ");
    }

    if (has_frequency_field)
    {
        if (!timer32["frequency_hz"].is_number())
        {
            return makeError("Поле \"frequency_hz\" должно быть числом");
        }
        const double frequency_hz = timer32["frequency_hz"].get<double>();
        if (frequency_hz <= 0.0)
        {
            return makeError("Поле \"frequency_hz\" должно быть положительным");
        }

        const double period_ticks = std::round(kSysClkHz / frequency_hz);
        if (period_ticks < 1.0 || period_ticks > static_cast<double>(kMaxRegisterValue) + 1.0)
        {
            return makeError("Поле \"frequency_hz\" недостижимо при prescaler=0 "
                              "(слишком высокая или слишком низкая частота для sys_clk=32МГц и 32-битного TOP)");
        }

        config.has_frequency = true;
        config.requested_frequency_hz = frequency_hz;
        config.prescaler = 0;
        config.top = static_cast<uint32_t>(period_ticks - 1.0);
    }
    else
    {
        for (const char* field : {"prescaler", "top"})
        {
            if (!timer32.contains(field))
            {
                return makeError(std::string("Отсутствует обязательное поле \"") + field + "\"");
            }
        }

        if (!timer32["prescaler"].is_number_integer())
        {
            return makeError("Поле \"prescaler\" должно быть целым числом");
        }
        const int64_t prescaler_raw = timer32["prescaler"].get<int64_t>();
        if (prescaler_raw < 0 || static_cast<uint64_t>(prescaler_raw) > kMaxRegisterValue)
        {
            return makeError("Поле \"prescaler\" вне диапазона 0-4294967295 (регистр PRESCALER 32-битный)");
        }

        if (!timer32["top"].is_number_integer())
        {
            return makeError("Поле \"top\" должно быть целым числом");
        }
        const int64_t top_raw = timer32["top"].get<int64_t>();
        if (top_raw < 0 || static_cast<uint64_t>(top_raw) > kMaxRegisterValue)
        {
            return makeError("Поле \"top\" вне диапазона 0-4294967295 (регистр TOP 32-битный)");
        }

        config.prescaler = static_cast<uint32_t>(prescaler_raw);
        config.top = static_cast<uint32_t>(top_raw);
    }

    const bool has_channels_field = timer32.contains("channels");
    if (instance == 0)
    {
        // ТЗ §3.1.5: «Timer32_0 (шина APB_M) – базовый счётчик без режимов
        // ШИМ, захвата и сравнения» — подтверждено HAL (см.
        // knowledge_base/timer32.json, instance_has_channels): все
        // Channel_*-функции сразу возвращают HAL_ERROR для TIMER32_0.
        if (has_channels_field && !timer32["channels"].empty())
        {
            return makeError("Поле \"channels\" недопустимо для instance=0 (Timer32_0 — базовый счётчик без каналов)");
        }
        return ParseResult<Timer32Config>{true, "", config};
    }

    // instance == 1 || instance == 2: каналы необязательны (пользователь
    // может использовать Timer32_1/2 просто как счётчик), но если заданы —
    // валидируются по item_fields схемы.
    if (!has_channels_field)
    {
        return ParseResult<Timer32Config>{true, "", config};
    }

    if (!timer32["channels"].is_array())
    {
        return makeError("Поле \"channels\" должно быть массивом");
    }
    if (timer32["channels"].size() > kMaxChannels)
    {
        return makeError("Поле \"channels\" содержит больше 4 элементов (у Timer32_1/Timer32_2 ровно 4 канала)");
    }

    std::set<int> used_indices;
    for (size_t i = 0; i < timer32["channels"].size(); ++i)
    {
        Timer32ChannelConfig channel;
        std::optional<std::string> channel_error = parseChannel(timer32["channels"][i], config.top, channel);
        if (channel_error)
        {
            return makeError("Канал #" + std::to_string(i + 1) + ": " + *channel_error);
        }
        if (!used_indices.insert(channel.index).second)
        {
            return makeError("Канал #" + std::to_string(i + 1) + ": index " + std::to_string(channel.index)
                + " уже занят другим каналом этого же блока timer32");
        }
        config.channels.push_back(channel);
    }

    return ParseResult<Timer32Config>{true, "", config};
}
