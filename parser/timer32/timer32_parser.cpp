#include "timer32_parser.h"
#include "win_file_io.h"
#include "json.hpp"
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
    const std::set<std::string> kValidChannelModes = {"pwm", "capture", "compare"};
    const std::set<std::string> kValidCaptureEdges = {"rising", "falling"};

    constexpr int kMinChannelIndex = 0;
    constexpr int kMaxChannelIndex = 3;
    constexpr size_t kMaxChannels = 4;

    ParseResult<Timer32Config> makeError(const std::string& message)
    {
        return ParseResult<Timer32Config>{false, message, std::nullopt};
    }

    // Возвращает nullptr при успехе, иначе сообщение об ошибке — вызывающая
    // сторона (parseTimer32Block) знает номер канала для контекста сообщения.
    std::optional<std::string> parseChannel(const nlohmann::json& channel_node, Timer32ChannelConfig& out)
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
        if (mode == "pwm" || mode == "compare")
        {
            if (!channel_node.contains("ocr") || !channel_node["ocr"].is_number_integer())
            {
                return std::string("поле \"ocr\" обязательно и должно быть целым числом для mode=") + mode;
            }
            const int64_t ocr_raw = channel_node["ocr"].get<int64_t>();
            if (ocr_raw < 0 || static_cast<uint64_t>(ocr_raw) > kMaxRegisterValue)
            {
                return std::string("поле \"ocr\" вне диапазона 0-4294967295 (регистр OCR 32-битный)");
            }
            ocr = static_cast<uint32_t>(ocr_raw);
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

            if (channel_node.contains("noise_filter"))
            {
                if (!channel_node["noise_filter"].is_boolean())
                {
                    return std::string("поле \"noise_filter\" должно быть булевым");
                }
                noise_filter = channel_node["noise_filter"].get<bool>();
            }
        }

        out.index = index;
        out.mode = mode;
        out.ocr = ocr;
        out.pwm_invert = pwm_invert;
        out.capture_edge = capture_edge;
        out.noise_filter = noise_filter;
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
    for (const char* field : {"instance", "prescaler", "count_mode", "top"})
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

    if (!timer32["prescaler"].is_number_integer())
    {
        return makeError("Поле \"prescaler\" должно быть целым числом");
    }
    const int64_t prescaler_raw = timer32["prescaler"].get<int64_t>();
    if (prescaler_raw < 0 || static_cast<uint64_t>(prescaler_raw) > kMaxRegisterValue)
    {
        return makeError("Поле \"prescaler\" вне диапазона 0-4294967295 (регистр PRESCALER 32-битный)");
    }

    if (!timer32["count_mode"].is_string() || !kValidCountModes.count(timer32["count_mode"].get<std::string>()))
    {
        return makeError("Поле \"count_mode\" содержит недопустимое значение (разрешены forward, reverse, bidirectional)");
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

    Timer32Config config;
    config.type = "timer32";
    config.instance = instance;
    config.prescaler = static_cast<uint32_t>(prescaler_raw);
    config.count_mode = timer32["count_mode"].get<std::string>();
    config.top = static_cast<uint32_t>(top_raw);

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
        std::optional<std::string> channel_error = parseChannel(timer32["channels"][i], channel);
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
