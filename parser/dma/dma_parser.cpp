#include "dma_parser.h"
#include "win_file_io.h"
#include "json.hpp"
#include <set>

namespace
{
    constexpr int kMinInstance = 0;
    constexpr int kMaxInstance = 3;

    const std::set<std::string> kValidRequests = {
        "crypto", "spi_0", "spi_1", "i2c_0", "i2c_1", "spifi",
        "timer32_0", "timer32_1", "timer32_2", "dac_0", "dac_1"};
    const std::set<std::string> kValidDirections = {
        "peripheral_to_memory", "memory_to_peripheral", "memory_to_memory"};
    const std::set<std::string> kValidElementSizes = {"byte", "halfword", "word"};
    const std::set<std::string> kValidPriorities = {"low", "medium", "high", "very_high"};

    ParseResult<DmaConfig> makeError(const std::string& message)
    {
        return ParseResult<DmaConfig>{false, message, std::nullopt};
    }
}

ParseResult<DmaConfig> parseDmaConfig(const std::string& file_name_path)
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

    const nlohmann::json* dma_node = nullptr;
    for (const auto& peripheral : root["peripherals"])
    {
        if (peripheral.contains("type") && peripheral["type"].is_string()
            && peripheral["type"].get<std::string>() == "dma")
        {
            dma_node = &peripheral;
            break;
        }
    }

    if (dma_node == nullptr)
    {
        return makeError("В конфигурации не найден блок peripherals с type=\"dma\"");
    }

    return parseDmaBlock(*dma_node);
}

ParseResult<DmaConfig> parseDmaBlock(const nlohmann::json& dma)
{
    for (const char* field : {"instance", "direction", "buffer_size_bytes", "element_size"})
    {
        if (!dma.contains(field))
        {
            return makeError(std::string("Отсутствует обязательное поле \"") + field + "\"");
        }
    }

    if (!dma["instance"].is_number_integer())
    {
        return makeError("Поле \"instance\" должно быть целым числом");
    }
    const int instance = dma["instance"].get<int>();
    if (instance < kMinInstance || instance > kMaxInstance)
    {
        return makeError("Поле \"instance\" вне диапазона 0-3 (DMA_CHANNEL_COUNT=4 — всего 4 канала ПДП)");
    }

    if (!dma["direction"].is_string() || !kValidDirections.count(dma["direction"].get<std::string>()))
    {
        return makeError("Поле \"direction\" содержит недопустимое значение "
                          "(разрешено peripheral_to_memory, memory_to_peripheral, memory_to_memory)");
    }
    const std::string direction = dma["direction"].get<std::string>();

    std::string request;
    if (direction == "memory_to_memory")
    {
        if (dma.contains("request"))
        {
            return makeError("Поле \"request\" не имеет смысла при direction=\"memory_to_memory\" "
                              "(обе стороны канала — память, периферийная линия запроса не используется)");
        }
    }
    else
    {
        if (!dma.contains("request"))
        {
            return makeError("Отсутствует обязательное поле \"request\" (нужно для direction=\""
                + direction + "\")");
        }
        if (!dma["request"].is_string() || !kValidRequests.count(dma["request"].get<std::string>()))
        {
            return makeError("Поле \"request\" содержит недопустимое значение — разрешены crypto, spi_0, "
                              "spi_1, i2c_0, i2c_1, spifi, timer32_0, timer32_1, timer32_2, dac_0, dac_1 "
                              "(USART0/USART1 настраиваются встроенным полем \"dma\" у type=\"uart\")");
        }
        request = dma["request"].get<std::string>();
    }

    if (!dma["buffer_size_bytes"].is_number_integer())
    {
        return makeError("Поле \"buffer_size_bytes\" должно быть целым числом");
    }
    const int buffer_size_bytes = dma["buffer_size_bytes"].get<int>();
    if (buffer_size_bytes < 1)
    {
        return makeError("Поле \"buffer_size_bytes\" должно быть не меньше 1");
    }

    if (!dma["element_size"].is_string() || !kValidElementSizes.count(dma["element_size"].get<std::string>()))
    {
        return makeError("Поле \"element_size\" содержит недопустимое значение (разрешено byte, halfword, word)");
    }

    std::string priority = "very_high";
    if (dma.contains("priority"))
    {
        if (!dma["priority"].is_string() || !kValidPriorities.count(dma["priority"].get<std::string>()))
        {
            return makeError("Поле \"priority\" содержит недопустимое значение (разрешено low, medium, high, very_high)");
        }
        priority = dma["priority"].get<std::string>();
    }

    bool irq_enable = false;
    if (dma.contains("irq_enable"))
    {
        if (!dma["irq_enable"].is_boolean())
        {
            return makeError("Поле \"irq_enable\" должно быть булевым");
        }
        irq_enable = dma["irq_enable"].get<bool>();
    }

    DmaConfig config;
    config.type = "dma";
    config.instance = instance;
    config.request = request;
    config.direction = direction;
    config.buffer_size_bytes = buffer_size_bytes;
    config.element_size = dma["element_size"].get<std::string>();
    config.priority = priority;
    config.irq_enable = irq_enable;

    return ParseResult<DmaConfig>{true, "", config};
}
