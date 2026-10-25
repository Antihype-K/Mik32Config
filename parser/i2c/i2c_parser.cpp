#include "i2c_parser.h"
#include "win_file_io.h"
#include "json.hpp"
#include <set>

// Концепция: как и в остальных parser/*, правила ниже дословно повторяют
// schemas/i2c.json. У I2C, в отличие от GPIO/UART/Timer16, часть полей
// обязательна только в зависимости от mode — эта условная логика тоже
// зашита здесь и обязана совпадать со схемой.
namespace
{
    constexpr int kMinInstance = 0;
    constexpr int kMaxInstance = 1;

    const std::set<std::string> kValidModes = {"master", "slave"};
    const std::set<int> kValidClockSpeeds = {100000, 400000, 1000000};
    const std::set<std::string> kValidAddressModes = {"7bit", "10bit"};

    constexpr int kMaxAddress7Bit = 127;
    constexpr int kMaxAddress10Bit = 1023;

    const std::set<int> kValidDigitalFilters = {0, 1, 2, 15};
    constexpr int kMaxOwnAddress2 = 127;
    const std::set<std::string> kValidOwnAddress2Masks = {
        "none", "111111x", "11111xx", "1111xxx", "111xxxx", "11xxxxx", "1xxxxxx", "1111111"};

    constexpr int kMinDmaChannel = 0;
    constexpr int kMaxDmaChannel = 3;
    const std::set<std::string> kValidDmaDirection = {"tx", "rx"};

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

    I2cConfig config;
    config.type = "i2c";
    config.instance = instance;
    config.mode = mode;
    config.clock_speed = i2c["clock_speed"].get<int>();
    config.address_mode = "";
    config.own_address = 0;
    config.digital_filter = 0;
    config.analog_filter = false;
    config.general_call = false;
    config.no_stretch_mode = false;
    config.dual_address_enabled = false;
    config.own_address_2 = 0;
    config.own_address_2_mask = "none";

    // Общие поля master/slave, обе — необязательны, дефолт сохраняет прежнее
    // хардкод-поведение кодогена (schemas/i2c.json: digital_filter/analog_filter).
    if (i2c.contains("digital_filter"))
    {
        if (!i2c["digital_filter"].is_number_integer()
            || !kValidDigitalFilters.count(i2c["digital_filter"].get<int>()))
        {
            return makeError("Поле \"digital_filter\" содержит недопустимое значение (разрешено 0, 1, 2, 15)");
        }
        config.digital_filter = i2c["digital_filter"].get<int>();
    }

    if (i2c.contains("analog_filter"))
    {
        if (!i2c["analog_filter"].is_boolean())
        {
            return makeError("Поле \"analog_filter\" должно быть булевым");
        }
        config.analog_filter = i2c["analog_filter"].get<bool>();
    }

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

        // general_call / no_stretch_mode / dual_address* — необязательны,
        // дефолт сохраняет прежний хардкод (schemas/i2c.json).
        if (i2c.contains("general_call"))
        {
            if (!i2c["general_call"].is_boolean())
            {
                return makeError("Поле \"general_call\" должно быть булевым");
            }
            config.general_call = i2c["general_call"].get<bool>();
        }

        if (i2c.contains("no_stretch_mode"))
        {
            if (!i2c["no_stretch_mode"].is_boolean())
            {
                return makeError("Поле \"no_stretch_mode\" должно быть булевым");
            }
            config.no_stretch_mode = i2c["no_stretch_mode"].get<bool>();
        }

        if (i2c.contains("dual_address_enabled"))
        {
            if (!i2c["dual_address_enabled"].is_boolean())
            {
                return makeError("Поле \"dual_address_enabled\" должно быть булевым");
            }
            config.dual_address_enabled = i2c["dual_address_enabled"].get<bool>();
        }

        if (config.dual_address_enabled)
        {
            // mik32_hal_i2c.h, DualAddressMode: "Можно использовать, если
            // длина основного адреса 7 бит" — отклоняем несовместимую
            // комбинацию явно, а не тихо генерируем код с недокументированным
            // поведением HAL при 10-битном основном адресе.
            if (address_mode != "7bit")
            {
                return makeError("Поле \"dual_address_enabled\"=true требует address_mode=\"7bit\" "
                    "(HAL поддерживает второй адрес только при 7-битном основном)");
            }

            if (!i2c.contains("own_address_2"))
            {
                return makeError("Для dual_address_enabled=true обязательно поле \"own_address_2\"");
            }
            if (!i2c["own_address_2"].is_number_integer())
            {
                return makeError("Поле \"own_address_2\" должно быть целым числом");
            }
            const int own_address_2 = i2c["own_address_2"].get<int>();
            if (own_address_2 < 0 || own_address_2 > kMaxOwnAddress2)
            {
                return makeError("Поле \"own_address_2\" вне диапазона 0-127 (второй адрес всегда 7-битный)");
            }
            config.own_address_2 = own_address_2;

            if (i2c.contains("own_address_2_mask"))
            {
                if (!i2c["own_address_2_mask"].is_string()
                    || !kValidOwnAddress2Masks.count(i2c["own_address_2_mask"].get<std::string>()))
                {
                    return makeError("Поле \"own_address_2_mask\" содержит недопустимое значение");
                }
                config.own_address_2_mask = i2c["own_address_2_mask"].get<std::string>();
            }
        }
    }

    // dma — необязательное поле, см. schemas/i2c.json и обоснование в
    // parser/i2c/i2c_parser.h у I2cDmaConfig. По образцу parser/USART/
    // usart_parser.cpp, но без buffer_type/buffer_size_bytes/element_size/
    // increment (не нужны этому HAL — см. комментарий у I2cDmaConfig).
    if (i2c.contains("dma"))
    {
        if (!i2c["dma"].is_array())
        {
            return makeError("Поле \"dma\" должно быть массивом");
        }
        if (i2c["dma"].size() > 2)
        {
            return makeError("Поле \"dma\" содержит больше 2 элементов (не более одного на каждое направление tx/rx)");
        }

        std::set<std::string> seen_directions;
        std::set<int> seen_channels;
        for (size_t i = 0; i < i2c["dma"].size(); ++i)
        {
            const nlohmann::json& dma_node = i2c["dma"][i];
            const std::string prefix = "dma[" + std::to_string(i) + "]: ";

            for (const char* field : {"channel", "direction"})
            {
                if (!dma_node.contains(field))
                {
                    return makeError(prefix + "отсутствует обязательное поле \"" + field + "\"");
                }
            }

            if (!dma_node["channel"].is_number_integer())
            {
                return makeError(prefix + "поле \"channel\" должно быть целым числом");
            }
            const int channel = dma_node["channel"].get<int>();
            if (channel < kMinDmaChannel || channel > kMaxDmaChannel)
            {
                return makeError(prefix + "поле \"channel\" вне диапазона 0-3 (у контроллера DMA 4 канала)");
            }
            if (!seen_channels.insert(channel).second)
            {
                return makeError(prefix + "channel " + std::to_string(channel) + " уже занят другим элементом массива \"dma\" этого же блока");
            }

            if (!dma_node["direction"].is_string() || !kValidDmaDirection.count(dma_node["direction"].get<std::string>()))
            {
                return makeError(prefix + "поле \"direction\" содержит недопустимое значение (разрешены tx, rx)");
            }
            const std::string dma_direction = dma_node["direction"].get<std::string>();
            if (!seen_directions.insert(dma_direction).second)
            {
                return makeError(prefix + "направление \"" + dma_direction + "\" уже задано другим элементом массива \"dma\" этого же блока");
            }

            I2cDmaConfig dma_config;
            dma_config.channel = channel;
            dma_config.direction = dma_direction;
            config.dma.push_back(dma_config);
        }
    }

    // interrupt_mode — необязательное поле, см. schemas/i2c.json.
    if (i2c.contains("interrupt_mode"))
    {
        if (!i2c["interrupt_mode"].is_boolean())
        {
            return makeError("Поле \"interrupt_mode\" должно быть булевым");
        }
        config.interrupt_mode = i2c["interrupt_mode"].get<bool>();
    }

    return ParseResult<I2cConfig>{true, "", config};
}
