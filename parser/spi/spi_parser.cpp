#include "spi_parser.h"
#include "win_file_io.h"
#include "json.hpp"
#include <set>

// Концепция: правила ниже дословно повторяют schemas/spi.json — как и в
// parser/i2c/i2c_parser.cpp, часть полей обязательна только в зависимости от
// mode (master/slave), эта условная логика зашита здесь и обязана совпадать
// со схемой.
namespace
{
    constexpr int kMinInstance = 0;
    constexpr int kMaxInstance = 1;
    constexpr int kMinThresholdTx = 0;
    constexpr int kMaxThresholdTx = 8;

    const std::set<std::string> kValidModes = {"master", "slave"};
    const std::set<std::string> kValidClkPhases = {"off", "on"};
    const std::set<std::string> kValidClkPolarities = {"low", "high"};
    const std::set<int> kValidBaudRateDivs = {4, 8, 16, 32, 64, 128, 256};
    const std::set<std::string> kValidChipSelects = {"cs0", "cs1", "cs2", "cs3"};
    const std::set<std::string> kValidInterrupts = {
        "rx_fifo_not_empty", "tx_fifo_not_full", "rx_overflow", "mode_fail"};

    ParseResult<SpiConfig> makeError(const std::string& message)
    {
        return ParseResult<SpiConfig>{false, message, std::nullopt};
    }
}

ParseResult<SpiConfig> parseSpiConfig(const std::string& file_name_path)
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

    const nlohmann::json* spi_node = nullptr;
    for (const auto& peripheral : root["peripherals"])
    {
        if (peripheral.contains("type") && peripheral["type"].is_string()
            && peripheral["type"].get<std::string>() == "spi")
        {
            spi_node = &peripheral;
            break;
        }
    }

    if (spi_node == nullptr)
    {
        return makeError("В конфигурации не найден блок peripherals с type=\"spi\"");
    }

    return parseSpiBlock(*spi_node);
}

ParseResult<SpiConfig> parseSpiBlock(const nlohmann::json& spi)
{
    for (const char* field : {"instance", "mode", "clk_phase", "clk_polarity", "threshold_tx"})
    {
        if (!spi.contains(field))
        {
            return makeError(std::string("Отсутствует обязательное поле \"") + field + "\"");
        }
    }

    if (!spi["instance"].is_number_integer())
    {
        return makeError("Поле \"instance\" должно быть целым числом");
    }
    const int instance = spi["instance"].get<int>();
    if (instance < kMinInstance || instance > kMaxInstance)
    {
        return makeError("Поле \"instance\" вне диапазона 0-1 (в МИК32 два блока SPI)");
    }

    if (!spi["mode"].is_string() || !kValidModes.count(spi["mode"].get<std::string>()))
    {
        return makeError("Поле \"mode\" содержит недопустимое значение (разрешено master, slave)");
    }
    const std::string mode = spi["mode"].get<std::string>();

    if (!spi["clk_phase"].is_string() || !kValidClkPhases.count(spi["clk_phase"].get<std::string>()))
    {
        return makeError("Поле \"clk_phase\" содержит недопустимое значение (разрешено off, on)");
    }

    if (!spi["clk_polarity"].is_string() || !kValidClkPolarities.count(spi["clk_polarity"].get<std::string>()))
    {
        return makeError("Поле \"clk_polarity\" содержит недопустимое значение (разрешено low, high)");
    }

    if (!spi["threshold_tx"].is_number_integer())
    {
        return makeError("Поле \"threshold_tx\" должно быть целым числом");
    }
    const int threshold_tx = spi["threshold_tx"].get<int>();
    if (threshold_tx < kMinThresholdTx || threshold_tx > kMaxThresholdTx)
    {
        return makeError("Поле \"threshold_tx\" вне диапазона 0-8 "
            "(HAL_SPI_Init возвращает HAL_ERROR при ThresholdTX > 8)");
    }

    SpiConfig config;
    config.type = "spi";
    config.instance = instance;
    config.mode = mode;
    config.clk_phase = spi["clk_phase"].get<std::string>();
    config.clk_polarity = spi["clk_polarity"].get<std::string>();
    config.threshold_tx = threshold_tx;
    config.baud_rate_div = 0;
    config.manual_cs = false;
    config.decoder = false;
    config.chip_select = "";

    if (mode == "master")
    {
        for (const char* field : {"baud_rate_div", "manual_cs", "decoder", "chip_select"})
        {
            if (!spi.contains(field))
            {
                return makeError(std::string("Для mode=\"master\" обязательно поле \"") + field + "\"");
            }
        }

        if (!spi["baud_rate_div"].is_number_integer()
            || !kValidBaudRateDivs.count(spi["baud_rate_div"].get<int>()))
        {
            return makeError("Поле \"baud_rate_div\" содержит недопустимое значение "
                "(разрешено 4, 8, 16, 32, 64, 128, 256)");
        }
        config.baud_rate_div = spi["baud_rate_div"].get<int>();

        if (!spi["manual_cs"].is_boolean())
        {
            return makeError("Поле \"manual_cs\" должно быть булевым");
        }
        config.manual_cs = spi["manual_cs"].get<bool>();

        if (!spi["decoder"].is_boolean())
        {
            return makeError("Поле \"decoder\" должно быть булевым");
        }
        config.decoder = spi["decoder"].get<bool>();

        if (!spi["chip_select"].is_string() || !kValidChipSelects.count(spi["chip_select"].get<std::string>()))
        {
            return makeError("Поле \"chip_select\" содержит недопустимое значение (разрешено cs0, cs1, cs2, cs3)");
        }
        config.chip_select = spi["chip_select"].get<std::string>();
    }

    // "interrupts" — необязательное поле, общее для master/slave (см.
    // schemas/spi.json). Отсутствие поля — старое поведение (режим опроса).
    if (spi.contains("interrupts"))
    {
        if (!spi["interrupts"].is_array())
        {
            return makeError("Поле \"interrupts\" должно быть массивом строк");
        }
        for (const auto& item : spi["interrupts"])
        {
            if (!item.is_string() || !kValidInterrupts.count(item.get<std::string>()))
            {
                return makeError("Поле \"interrupts\" содержит недопустимое значение "
                    "(разрешено rx_fifo_not_empty, tx_fifo_not_full, rx_overflow, mode_fail)");
            }
            config.interrupts.push_back(item.get<std::string>());
        }
    }

    return ParseResult<SpiConfig>{true, "", config};
}
