#include "i2c_parser.h"
#include "win_file_io.h"
#include "json.hpp"
#include <set>

// Концепция: как и в остальных parser/*, правила ниже дословно повторяют
// schemas/i2c.json. У I2C, в отличие от GPIO/UART/Timer16, часть полей
// обязательна только в зависимости от mode/demo — эта условная логика тоже
// зашита здесь и обязана совпадать со схемой.
namespace
{
    constexpr int kMinInstance = 0;
    constexpr int kMaxInstance = 1;

    const std::set<std::string> kValidModes = {"master", "slave"};
    const std::set<int> kValidClockSpeeds = {100000, 400000, 1000000};
    const std::set<std::string> kValidAddressModes = {"7bit", "10bit"};

    const std::set<std::string> kValidMasterDemos = {"scanner", "ping_pong"};
    const std::set<std::string> kValidSlaveDemos = {"receive", "ping_pong", "scanner_target"};

    constexpr int kMaxAddress7Bit = 127;
    constexpr int kMaxAddress10Bit = 1023;
    constexpr int kMaxTargetAddress = 127;

    ParseResult<I2cConfig> makeError(const std::string& message)
    {
        return ParseResult<I2cConfig>{false, message, std::nullopt};
    }
}

ParseResult<I2cConfig> parseI2cConfig(const std::string& file_name_path)
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

    const nlohmann::json* i2c_node = nullptr;
    for (const auto& peripheral : root["peripherals"])
    {
        if (peripheral.contains("type") && peripheral["type"].is_string()
            && peripheral["type"].get<std::string>() == "i2c")
        {
            i2c_node = &peripheral;
            break;
        }
    }

    if (i2c_node == nullptr)
    {
        return makeError("В конфигурации не найден блок peripherals с type=\"i2c\"");
    }

    return parseI2cBlock(*i2c_node);
}

ParseResult<I2cConfig> parseI2cBlock(const nlohmann::json& i2c)
{

    for (const char* field : {"instance", "mode", "clock_speed"})
    {
        if (!i2c.contains(field))
        {
            return makeError(std::string("Отсутствует обязательное поле \"") + field + "\"");
        }
    }

    if (!i2c["instance"].is_number_integer())
    {
        return makeError("Поле \"instance\" должно быть целым числом");
    }
    const int instance = i2c["instance"].get<int>();
    if (instance < kMinInstance || instance > kMaxInstance)
    {
        return makeError("Поле \"instance\" вне диапазона 0-1 (в МИК32 два блока I2C)");
    }

    if (!i2c["mode"].is_string() || !kValidModes.count(i2c["mode"].get<std::string>()))
    {
        return makeError("Поле \"mode\" содержит недопустимое значение (разрешено master, slave)");
    }
    const std::string mode = i2c["mode"].get<std::string>();

    if (!i2c["clock_speed"].is_number_integer()
        || !kValidClockSpeeds.count(i2c["clock_speed"].get<int>()))
    {
        return makeError("Поле \"clock_speed\" содержит недопустимое значение (разрешено 100000, 400000, 1000000)");
    }

    // "demo" необязателен — дефолт зависит от mode (schemas/i2c.json).
    std::string demo;
    if (i2c.contains("demo"))
    {
        if (!i2c["demo"].is_string())
        {
            return makeError("Поле \"demo\" должно быть строкой");
        }
        demo = i2c["demo"].get<std::string>();
    }
    else
    {
        demo = (mode == "master") ? "scanner" : "receive";
    }

    const std::set<std::string>& valid_demos_for_mode = (mode == "master") ? kValidMasterDemos : kValidSlaveDemos;
    if (!valid_demos_for_mode.count(demo))
    {
        return makeError("Поле \"demo\" содержит недопустимое значение для этого mode "
            "(master: scanner, ping_pong; slave: receive, ping_pong)");
    }

    I2cConfig config;
    config.type = "i2c";
    config.instance = instance;
    config.mode = mode;
    config.clock_speed = i2c["clock_speed"].get<int>();
    config.demo = demo;
    config.address_mode = "";
    config.own_address = 0;
    config.target_address = 0;

    if (mode == "slave")
    {
        if (!i2c.contains("address_mode") || !i2c.contains("own_address"))
        {
            return makeError("Для mode=\"slave\" обязательны поля \"address_mode\" и \"own_address\"");
        }
        if (!i2c["address_mode"].is_string() || !kValidAddressModes.count(i2c["address_mode"].get<std::string>()))
        {
            return makeError("Поле \"address_mode\" содержит недопустимое значение (разрешено 7bit, 10bit)");
        }
        const std::string address_mode = i2c["address_mode"].get<std::string>();

        if (!i2c["own_address"].is_number_integer())
        {
            return makeError("Поле \"own_address\" должно быть целым числом");
        }
        const int own_address = i2c["own_address"].get<int>();
        const int max_own_address = (address_mode == "7bit") ? kMaxAddress7Bit : kMaxAddress10Bit;
        if (own_address < 0 || own_address > max_own_address)
        {
            return makeError("Поле \"own_address\" вне диапазона для выбранного address_mode");
        }

        // HAL_I2C_OwnAddress1 (mik32_hal_i2c.c:99-114) сам определяет
        // 7-битный/10-битный режим по величине OwnAddress1 (<=0x7F -> 7 бит),
        // а не по отдельному полю — значит own_address<=127 при заявленном
        // address_mode="10bit" HAL молча трактует как 7-битный адрес вопреки
        // выбору пользователя. Отклоняем такую комбинацию явно, а не
        // тихо генерируем код, который сделает не то, что заявлено в схеме.
        if (address_mode == "10bit" && own_address <= kMaxAddress7Bit)
        {
            return makeError("Поле \"own_address\" при address_mode=\"10bit\" должно быть больше 127 "
                "(иначе HAL_I2C_OwnAddress1 определит его как 7-битный адрес)");
        }

        config.address_mode = address_mode;
        config.own_address = own_address;
    }

    if (mode == "master" && demo == "ping_pong")
    {
        if (!i2c.contains("target_address"))
        {
            return makeError("Для mode=\"master\" с demo=\"ping_pong\" обязательно поле \"target_address\"");
        }
        if (!i2c["target_address"].is_number_integer())
        {
            return makeError("Поле \"target_address\" должно быть целым числом");
        }
        const int target_address = i2c["target_address"].get<int>();
        if (target_address < 0 || target_address > kMaxTargetAddress)
        {
            return makeError("Поле \"target_address\" вне диапазона 0-127");
        }
        config.target_address = target_address;
    }

    return ParseResult<I2cConfig>{true, "", config};
}
