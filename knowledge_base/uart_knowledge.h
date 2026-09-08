//
// Загрузчик knowledge_base/uart.json — по образцу gpio_knowledge.h.
//
#ifndef MIK32_CONFIGURATOR_UART_KNOWLEDGE_H
#define MIK32_CONFIGURATOR_UART_KNOWLEDGE_H

#include <string>
#include <map>
#include "parse_result.h"

struct UartKnowledge
{
    int max_instances;

    // instance (0/1) -> имя макроса-хендла, например "UART_0".
    std::map<int, std::string> instance_handle;

    // "7"/"8" -> имя HAL enum'а, например "Frame_8bit".
    std::map<std::string, std::string> data_bits_hal_enum;

    // "1"/"2" -> "StopBit_1"/"StopBit_2".
    std::map<std::string, std::string> stop_bits_hal_enum;

    struct ParitySetting
    {
        std::string parity_bit;            // "Enable"/"Disable"
        std::string parity_bit_inversion;  // "Enable"/"Disable"
    };
    // "none"/"even"/"odd" -> пара полей parity_bit/parity_bit_inversion.
    std::map<std::string, ParitySetting> parity_hal_setting;

    // ТЗ §3.1.8: HSI 32МГц — единственный источник тактирования, константа
    // для расчёта погрешности Baud Rate (см. codegen/uart/uart_codegen.cpp).
    long hsi_hz;

    struct FixedPin
    {
        int gpio_instance;
        int pin;
    };
    struct InstanceFixedPins
    {
        FixedPin tx;
        FixedPin rx;
        FixedPin rts;
        FixedPin cts;
    };
    // instance (0/1) -> жёстко закреплённые датащитом TX/RX/RTS/CTS пины.
    // Не используется в uart_codegen (HAL сам знает свои пины) — читается
    // validation/cross_module_validator.cpp для поиска конфликтов пинов
    // между GPIO-блоком и UART-инстансом (ТЗ §3.1.6.2).
    std::map<int, InstanceFixedPins> instance_fixed_pins;

    // instance (0/1) -> имя HAL_DMA_ChannelRequestTypeDef, например
    // "DMA_CHANNEL_USART_0_REQUEST" (ТЗ §3.1.3.3).
    std::map<int, std::string> dma_request_enum;
    // "0".."3" -> "DMA_CHANNEL_0".."DMA_CHANNEL_3".
    std::map<std::string, std::string> dma_channel_hal_enum;
    // "byte"/"halfword"/"word" -> HAL_DMA_ChannelSizeTypeDef.
    std::map<std::string, std::string> dma_element_size_hal_enum;
};

ParseResult<UartKnowledge> loadUartKnowledge(const std::string& file_path);

#endif //MIK32_CONFIGURATOR_UART_KNOWLEDGE_H
