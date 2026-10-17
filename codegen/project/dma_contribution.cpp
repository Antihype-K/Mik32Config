#include "dma_contribution.h"
#include "contribution_utils.h"
#include <map>
#include <sstream>

namespace
{
    // mik32_hal_dma.h, HAL_DMA_ChannelRequestTypeDef (строки 90-111) — без
    // DMA_CHANNEL_USART_0/1_REQUEST, см. schemas/dma.json, _comment_usart_excluded.
    const std::map<std::string, std::string> kRequestEnum = {
        {"crypto", "DMA_CHANNEL_CRYPTO_REQUEST"},
        {"spi_0", "DMA_CHANNEL_SPI_0_REQUEST"},
        {"spi_1", "DMA_CHANNEL_SPI_1_REQUEST"},
        {"i2c_0", "DMA_CHANNEL_I2C_0_REQUEST"},
        {"i2c_1", "DMA_CHANNEL_I2C_1_REQUEST"},
        {"spifi", "DMA_CHANNEL_SPIFI_REQUEST"},
        {"timer32_0", "DMA_CHANNEL_TIMER32_0_REQUEST"},
        {"timer32_1", "DMA_CHANNEL_TIMER32_1_REQUEST"},
        {"timer32_2", "DMA_CHANNEL_TIMER32_2_REQUEST"},
        {"dac_0", "DMA_CHANNEL_DAC_0_REQUEST"},
        {"dac_1", "DMA_CHANNEL_DAC_1_REQUEST"},
    };

    const std::map<int, std::string> kChannelEnum = {
        {0, "DMA_CHANNEL_0"}, {1, "DMA_CHANNEL_1"}, {2, "DMA_CHANNEL_2"}, {3, "DMA_CHANNEL_3"},
    };

    const std::map<std::string, std::string> kSizeEnum = {
        {"byte", "DMA_CHANNEL_SIZE_BYTE"},
        {"halfword", "DMA_CHANNEL_SIZE_HALFWORD"},
        {"word", "DMA_CHANNEL_SIZE_WORD"},
    };

    const std::map<std::string, std::string> kPriorityEnum = {
        {"low", "DMA_CHANNEL_PRIORITY_LOW"},
        {"medium", "DMA_CHANNEL_PRIORITY_MEDIUM"},
        {"high", "DMA_CHANNEL_PRIORITY_HIGH"},
        {"very_high", "DMA_CHANNEL_PRIORITY_VERY_HIGH"},
    };
}

// Концепция: та же, что у UART-встроенного DMA (codegen/project/
// uart_contribution.cpp) — поля ChannelInit присваиваются напрямую,
// без отдельного вызова "инициализации канала" (его в реальном HAL нет,
// mik32_hal_dma.h подтверждён побайтово с ~/.platformio/.../mik32v2-sdk).
// Общий hdma уже инициализирован раньше (project_codegen.cpp, any_dma_usage) —
// этот канал ссылается на него через ChannelInit-независимое поле .dma.
ParseResult<PeripheralContribution> buildDmaContribution(const DmaConfig& config)
{
    const std::string* channel_enum = findOrNull(kChannelEnum, config.instance);
    const std::string* size_enum = findOrNull(kSizeEnum, config.element_size);
    const std::string* priority_enum = findOrNull(kPriorityEnum, config.priority);
    if (!channel_enum || !size_enum || !priority_enum)
    {
        return ParseResult<PeripheralContribution>{
            false, "DmaConfig ссылается на instance/element_size/priority без известного HAL-перечисления",
            std::nullopt};
    }

    const std::string* request_enum = nullptr;
    if (config.direction != "memory_to_memory")
    {
        request_enum = findOrNull(kRequestEnum, config.request);
        if (!request_enum)
        {
            return ParseResult<PeripheralContribution>{
                false, "DmaConfig ссылается на request без известного HAL-перечисления", std::nullopt};
        }
    }

    const std::string channel_handle = "hdma_channel" + std::to_string(config.instance);
    const std::string buffer_name = "dma_channel" + std::to_string(config.instance) + "_buffer";
    const bool read_is_memory = (config.direction != "peripheral_to_memory");
    const bool write_is_memory = (config.direction == "peripheral_to_memory"
        || config.direction == "memory_to_memory");

    PeripheralContribution contribution;
    contribution.label = "DMA_Channel" + std::to_string(config.instance);
    contribution.init_order = kInitOrderDma;
    contribution.init_function_name = "DMA_Channel" + std::to_string(config.instance) + "_Init";
    contribution.init_c_includes = {"mik32_hal_dma.h"};
    contribution.public_prototypes = {
        "void " + contribution.init_function_name + "(void);"};
    contribution.global_definitions = "DMA_ChannelHandleTypeDef " + channel_handle + ";\n"
        "uint8_t " + buffer_name + "[" + std::to_string(config.buffer_size_bytes) + "];\n";
    // extern в main.c — иначе статический буфер, объявленный в mik32_init.c,
    // не виден прикладному коду (та же причина, что у UART-DMA, см.
    // docs/implementations/dma.md, пункт 5 раздела call_shape).
    contribution.extern_declarations = "extern uint8_t " + buffer_name + "["
        + std::to_string(config.buffer_size_bytes) + "];\n";

    std::ostringstream init_c;
    init_c << "/*\n"
              " * DMA channel " << config.instance << ": direction=" << config.direction;
    if (!config.request.empty())
    {
        init_c << ", request=" << config.request;
    }
    init_c << ", element_size=" << config.element_size << ", buffer_size_bytes="
           << config.buffer_size_bytes << ".\n"
              " * Эта функция только настраивает канал (ChannelInit) — сам запуск передачи\n"
              " * (HAL_DMA_Start(&" << channel_handle << ", source, destination, len)) остаётся\n"
              " * за пользователем: конкретный адрес регистра данных периферии (TXDATA/RXDATA/\n"
              " * иной FIFO-регистр) для request=" << (config.request.empty() ? "(memory_to_memory)" : config.request)
           << " не выводится автоматически — у разных периферий\n"
              " * разное имя регистра и структура HAL-хендла, инвентировать адрес здесь\n"
              " * означало бы угадывать его вместо того, чтобы взять из датащита/HAL-\n"
              " * заголовка нужной периферии. Буфер " << buffer_name << " уже объявлен\n"
              " * и виден (extern) в main.c. HAL_DMA_Start — всегда одноразовая передача,\n"
              " * для повторного/периодического запуска вызовите её снова (например, из\n"
              " * обработчика прерывания DMA — HAL_DMA_LocalIRQEnable/HAL_DMA_GetChannelIrq),\n"
              " * автоматического зацикливания в реальном HAL нет (docs/implementations/dma.md).\n"
              " */\n"
              "void " << contribution.init_function_name << "(void)\n"
              "{\n"
              "    " << channel_handle << ".dma = &hdma;\n"
              "    " << channel_handle << ".ChannelInit.Channel = " << *channel_enum << ";\n"
              "    " << channel_handle << ".ChannelInit.Priority = " << *priority_enum << ";\n"
              "    " << channel_handle << ".ChannelInit.ReadMode = "
           << (read_is_memory ? "DMA_CHANNEL_MODE_MEMORY" : "DMA_CHANNEL_MODE_PERIPHERY") << ";\n"
              "    " << channel_handle << ".ChannelInit.ReadInc = "
           << (read_is_memory ? "DMA_CHANNEL_INC_ENABLE" : "DMA_CHANNEL_INC_DISABLE") << ";\n"
              "    " << channel_handle << ".ChannelInit.ReadSize = " << *size_enum << ";\n"
              "    " << channel_handle << ".ChannelInit.ReadBurstSize = 0;\n";
    if (!read_is_memory)
    {
        init_c << "    " << channel_handle << ".ChannelInit.ReadRequest = " << *request_enum << ";\n";
    }
    init_c << "    " << channel_handle << ".ChannelInit.ReadAck = "
           << (read_is_memory ? "DMA_CHANNEL_ACK_DISABLE" : "DMA_CHANNEL_ACK_ENABLE") << ";\n"
              "    " << channel_handle << ".ChannelInit.WriteMode = "
           << (write_is_memory ? "DMA_CHANNEL_MODE_MEMORY" : "DMA_CHANNEL_MODE_PERIPHERY") << ";\n"
              "    " << channel_handle << ".ChannelInit.WriteInc = "
           << (write_is_memory ? "DMA_CHANNEL_INC_ENABLE" : "DMA_CHANNEL_INC_DISABLE") << ";\n"
              "    " << channel_handle << ".ChannelInit.WriteSize = " << *size_enum << ";\n"
              "    " << channel_handle << ".ChannelInit.WriteBurstSize = 0;\n";
    if (!write_is_memory)
    {
        init_c << "    " << channel_handle << ".ChannelInit.WriteRequest = " << *request_enum << ";\n";
    }
    init_c << "    " << channel_handle << ".ChannelInit.WriteAck = "
           << (write_is_memory ? "DMA_CHANNEL_ACK_DISABLE" : "DMA_CHANNEL_ACK_ENABLE") << ";\n";
    if (config.irq_enable)
    {
        init_c << "\n    HAL_DMA_LocalIRQEnable(&" << channel_handle << ", DMA_IRQ_ENABLE);\n";
    }
    init_c << "}\n";
    contribution.init_function_text = init_c.str();

    return ParseResult<PeripheralContribution>{true, "", contribution};
}
