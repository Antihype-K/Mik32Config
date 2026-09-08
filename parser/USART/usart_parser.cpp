//
// Created by antihype on 31.07.2026.
//

#include "usart_parser.h"
#include "win_file_io.h"
#include "json.hpp"
#include <set>

// Концепция: как и в parser/gpio/gpio_parser.cpp, правила ниже дословно
// повторяют schemas/uart.json — универсальный schema-driven загрузчик не
// строится в этой сессии, расхождение чинится в обоих местах сразу.
namespace
{
    // Датащит, Таблица 2, с.11-12: в карте памяти APB_P — ровно два блока
    // USART (USART_0 +0x0400, USART_1 +0x0800), в отличие от трёх GPIO.
    constexpr int kMinInstance = 0;
    constexpr int kMaxInstance = 1;

    // schemas/uart.json: "baud_rate" enum_int — ТЗ §3.1.3.2 фиксирует ряд
    // стандартных скоростей, а не произвольное целое.
    const std::set<int> kValidBaudRates = {9600, 19200, 38400, 57600, 115200, 230400, 460800, 921600};

    // schemas/uart.json: "data_bits" enum_int [7, 8, 9] — датащит, Таблица 18
    // (CONTROL1, биты M1:M0), с.39-40: 0b00=8, 0b01=9, 0b10=7. 9 бит добавлен
    // по ТЗ §3.1.3.2 (реально поддерживается железом).
    const std::set<int> kValidDataBits = {7, 8, 9};

    // schemas/uart.json: "stop_bits" enum_int [1, 2] — mik32_hal_usart.h,
    // HAL_USART_StopBit_enum содержит только StopBit_1/StopBit_2. ТЗ §3.1.3.2
    // требует ещё 0.5/1.5, но датащит (регистр CONTROL1, бит STOP_1) прямо
    // говорит: один бит, только "1 стоп бит"/"2 стоп бита" — дробных
    // стоп-битов на этом железе не существует, ТЗ здесь ошибочно.
    const std::set<int> kValidStopBits = {1, 2};

    // schemas/uart.json: "parity" — Таблица 18, биты PCE (10)/PS (9), с.40.
    const std::set<std::string> kValidParity = {"none", "even", "odd"};

    // schemas/uart.json: "direction" — ТЗ §3.1.3.2.
    const std::set<std::string> kValidDirection = {"tx", "rx", "duplex"};

    // schemas/uart.json: "dma" — ТЗ §3.1.3.3.
    const std::set<std::string> kValidDmaDirection = {"tx", "rx"};
    const std::set<std::string> kValidBufferType = {"linear", "circular"};
    const std::set<std::string> kValidElementSize = {"byte", "halfword", "word"};
    constexpr int kMinDmaChannel = 0;
    constexpr int kMaxDmaChannel = 3;

    ParseResult<UartConfig> makeError(const std::string& message)
    {
        return ParseResult<UartConfig>{false, message, std::nullopt};
    }
}

ParseResult<UartConfig> parseUsartConfig(const std::string& file_name_path)
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

    const nlohmann::json* uart_node = nullptr;
    for (const auto& peripheral : root["peripherals"])
    {
        if (peripheral.contains("type") && peripheral["type"].is_string()
            && peripheral["type"].get<std::string>() == "uart")
        {
            uart_node = &peripheral;
            break;
        }
    }

    if (uart_node == nullptr)
    {
        return makeError("В конфигурации не найден блок peripherals с type=\"uart\"");
    }

    return parseUartBlock(*uart_node);
}

ParseResult<UartConfig> parseUartBlock(const nlohmann::json& uart)
{

    // schemas/uart.json: "required_fields" — все семь полей обязательны
    // (tx_pin/rx_pin из черновика архитектуры §5.3 сюда сознательно не
    // включены: они жёстко фиксированы датащитом по instance, а не
    // настраиваемы пользователем — см. docs/implementations/uart.md).
    for (const char* field : {"instance", "baud_rate", "data_bits", "stop_bits", "parity", "flow_control", "direction"})
    {
        if (!uart.contains(field))
        {
            return makeError(std::string("Отсутствует обязательное поле \"") + field + "\"");
        }
    }

    if (!uart["instance"].is_number_integer())
    {
        return makeError("Поле \"instance\" должно быть целым числом");
    }
    const int instance = uart["instance"].get<int>();
    if (instance < kMinInstance || instance > kMaxInstance)
    {
        return makeError("Поле \"instance\" вне диапазона 0-1 (в МИК32 два блока USART)");
    }

    if (!uart["baud_rate"].is_number_integer()
        || !kValidBaudRates.count(uart["baud_rate"].get<int>()))
    {
        return makeError("Поле \"baud_rate\" содержит недопустимое значение (см. ТЗ §3.1.3.2 — фиксированный ряд стандартных скоростей)");
    }

    if (!uart["data_bits"].is_number_integer()
        || !kValidDataBits.count(uart["data_bits"].get<int>()))
    {
        return makeError("Поле \"data_bits\" содержит недопустимое значение (разрешено 7, 8 или 9)");
    }

    if (!uart["stop_bits"].is_number_integer()
        || !kValidStopBits.count(uart["stop_bits"].get<int>()))
    {
        return makeError("Поле \"stop_bits\" содержит недопустимое значение (разрешено 1 или 2)");
    }

    if (!uart["parity"].is_string() || !kValidParity.count(uart["parity"].get<std::string>()))
    {
        return makeError("Поле \"parity\" содержит недопустимое значение");
    }

    if (!uart["flow_control"].is_boolean())
    {
        return makeError("Поле \"flow_control\" должно быть булевым значением");
    }

    if (!uart["direction"].is_string() || !kValidDirection.count(uart["direction"].get<std::string>()))
    {
        return makeError("Поле \"direction\" содержит недопустимое значение (разрешено tx, rx или duplex)");
    }

    UartConfig config;
    config.type = "uart";
    config.instance = instance;
    config.baud_rate = uart["baud_rate"].get<int>();
    config.data_bits = uart["data_bits"].get<int>();
    config.stop_bits = uart["stop_bits"].get<int>();
    config.parity = uart["parity"].get<std::string>();
    config.flow_control = uart["flow_control"].get<bool>();
    config.direction = uart["direction"].get<std::string>();

    // ТЗ §3.1.3.3 — необязательное поле, отсутствие ничего не меняет
    // в поведении блока (обычный случай, как и GPIO "interrupts").
    if (uart.contains("dma"))
    {
        if (!uart["dma"].is_array())
        {
            return makeError("Поле \"dma\" должно быть массивом");
        }
        if (uart["dma"].size() > 2)
        {
            return makeError("Поле \"dma\" содержит больше 2 элементов (не более одного на каждое направление tx/rx)");
        }

        std::set<std::string> seen_directions;
        std::set<int> seen_channels;
        for (size_t i = 0; i < uart["dma"].size(); ++i)
        {
            const nlohmann::json& dma_node = uart["dma"][i];
            const std::string prefix = "dma[" + std::to_string(i) + "]: ";

            for (const char* field : {"channel", "direction", "buffer_type", "buffer_size_bytes", "element_size", "increment"})
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

            // ТЗ §3.1.3.3: «направление (память → периферия для TX / периферия
            // → память для RX)» — DMA-передача имеет смысл только в ту
            // сторону, которая у этого же блока реально включена.
            if (dma_direction == "tx" && config.direction != "tx" && config.direction != "duplex")
            {
                return makeError(prefix + "direction=\"tx\" требует, чтобы у этого же блока uart передача была включена (direction=tx или duplex)");
            }
            if (dma_direction == "rx" && config.direction != "rx" && config.direction != "duplex")
            {
                return makeError(prefix + "direction=\"rx\" требует, чтобы у этого же блока uart приём был включён (direction=rx или duplex)");
            }
            if (!seen_directions.insert(dma_direction).second)
            {
                return makeError(prefix + "направление \"" + dma_direction + "\" уже задано другим элементом массива \"dma\" этого же блока");
            }

            if (!dma_node["buffer_type"].is_string() || !kValidBufferType.count(dma_node["buffer_type"].get<std::string>()))
            {
                return makeError(prefix + "поле \"buffer_type\" содержит недопустимое значение (разрешены linear, circular)");
            }

            if (!dma_node["buffer_size_bytes"].is_number_integer())
            {
                return makeError(prefix + "поле \"buffer_size_bytes\" должно быть целым числом");
            }
            const int buffer_size_bytes = dma_node["buffer_size_bytes"].get<int>();
            if (buffer_size_bytes < 1)
            {
                return makeError(prefix + "поле \"buffer_size_bytes\" должно быть положительным");
            }

            if (!dma_node["element_size"].is_string() || !kValidElementSize.count(dma_node["element_size"].get<std::string>()))
            {
                return makeError(prefix + "поле \"element_size\" содержит недопустимое значение (разрешены byte, halfword, word)");
            }

            if (!dma_node["increment"].is_boolean())
            {
                return makeError(prefix + "поле \"increment\" должно быть булевым");
            }

            UartDmaConfig dma_config;
            dma_config.channel = channel;
            dma_config.direction = dma_direction;
            dma_config.buffer_type = dma_node["buffer_type"].get<std::string>();
            dma_config.buffer_size_bytes = buffer_size_bytes;
            dma_config.element_size = dma_node["element_size"].get<std::string>();
            dma_config.increment = dma_node["increment"].get<bool>();
            config.dma.push_back(dma_config);
        }
    }

    return ParseResult<UartConfig>{true, "", config};
}
