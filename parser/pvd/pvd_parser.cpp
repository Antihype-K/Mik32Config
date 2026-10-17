#include "pvd_parser.h"
#include "win_file_io.h"
#include "json.hpp"

namespace
{
    constexpr int kMinThreshold = 0;
    constexpr int kMaxThreshold = 15;
    constexpr int kMinFilter = 0;
    constexpr int kMaxFilter = 65535;

    ParseResult<PvdConfig> makeError(const std::string& message)
    {
        return ParseResult<PvdConfig>{false, message, std::nullopt};
    }
}

ParseResult<PvdConfig> parsePvdConfig(const std::string& file_name_path)
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

    const nlohmann::json* pvd_node = nullptr;
    for (const auto& peripheral : root["peripherals"])
    {
        if (peripheral.contains("type") && peripheral["type"].is_string()
            && peripheral["type"].get<std::string>() == "pvd")
        {
            pvd_node = &peripheral;
            break;
        }
    }

    if (pvd_node == nullptr)
    {
        return makeError("В конфигурации не найден блок peripherals с type=\"pvd\"");
    }

    return parsePvdBlock(*pvd_node);
}

ParseResult<PvdConfig> parsePvdBlock(const nlohmann::json& pvd)
{
    for (const char* field : {"under_threshold", "over_threshold", "digital_filter_value"})
    {
        if (!pvd.contains(field))
        {
            return makeError(std::string("Отсутствует обязательное поле \"") + field + "\"");
        }
    }

    if (!pvd["under_threshold"].is_number_integer())
    {
        return makeError("Поле \"under_threshold\" должно быть целым числом");
    }
    const int under_threshold = pvd["under_threshold"].get<int>();
    if (under_threshold < kMinThreshold || under_threshold > kMaxThreshold)
    {
        return makeError("Поле \"under_threshold\" вне диапазона 0-15 (CONFIG.UNDER_THRESH — 4 бита)");
    }

    if (!pvd["over_threshold"].is_number_integer())
    {
        return makeError("Поле \"over_threshold\" должно быть целым числом");
    }
    const int over_threshold = pvd["over_threshold"].get<int>();
    if (over_threshold < kMinThreshold || over_threshold > kMaxThreshold)
    {
        return makeError("Поле \"over_threshold\" вне диапазона 0-15 (CONFIG.OVER_THRESH — 4 бита)");
    }

    if (!pvd["digital_filter_value"].is_number_integer())
    {
        return makeError("Поле \"digital_filter_value\" должно быть целым числом");
    }
    const int digital_filter_value = pvd["digital_filter_value"].get<int>();
    if (digital_filter_value < kMinFilter || digital_filter_value > kMaxFilter)
    {
        return makeError("Поле \"digital_filter_value\" вне диапазона 0-65535 (DPF_VALUE — 16 бит)");
    }

    bool enable_reference_calibration = false;
    if (pvd.contains("enable_reference_calibration"))
    {
        if (!pvd["enable_reference_calibration"].is_boolean())
        {
            return makeError("Поле \"enable_reference_calibration\" должно быть булевым");
        }
        enable_reference_calibration = pvd["enable_reference_calibration"].get<bool>();
    }

    PvdConfig config;
    config.type = "pvd";
    config.under_threshold = under_threshold;
    config.over_threshold = over_threshold;
    config.digital_filter_value = digital_filter_value;
    config.enable_reference_calibration = enable_reference_calibration;

    return ParseResult<PvdConfig>{true, "", config};
}
