//
// I2cConfig / parseI2cConfig — по образцу parser/gpio/gpio_parser.h,
// parser/USART/usart_parser.h, parser/timer16/timer16_parser.h.
// Схема — schemas/i2c.json (ТЗ по I2C не содержит отдельного раздела,
// см. docs/implementations/i2c.md).
//
#ifndef MIK32_CONFIGURATOR_I2C_PARSER_H
#define MIK32_CONFIGURATOR_I2C_PARSER_H
#include <string>
#include <vector>
#include "parse_result.h"
#include "json.hpp"

// DMA-канал для I2C, по образцу UartDmaConfig (parser/USART/usart_parser.h),
// но меньше полей: mik32_hal_i2c.c, HAL_I2C_Master/Slave_Transmit/Receive_DMA
// (строки 983-1113) всегда пересылают TXDR/RXDR байтами и сами вызывают
// HAL_DMA_Start с адресом и длиной, переданными ВЫЗЫВАЮЩИМ кодом в рантайме —
// buffer_type/buffer_size_bytes/element_size/increment у UART-DMA (которая
// сама объявляет статический буфер и Start-функцию) здесь не нужны, кодоген
// лишь один раз конфигурирует ChannelInit (см. i2c_contribution.cpp) и
// подключает hdmatx/hdmarx к хендлу I2C.
struct I2cDmaConfig
{
    int channel;            // 0-3, см. schemas/i2c.json
    std::string direction;  // "tx" | "rx"
};

struct I2cConfig
{
    std::string type;
    int instance;
    std::string mode;           // "master" / "slave"
    int clock_speed;

    // Только для mode == "slave" (иначе пустая/0 — не читаются кодогеном
    // для master, HAL_I2C_MasterInit эти поля не использует вообще).
    std::string address_mode;   // "7bit" / "10bit"
    int own_address;

    // Общие для master/slave — HAL_I2C_{Analog,Digital}FilterInit вызываются
    // из обеих веток HAL_I2C_Init (см. schemas/i2c.json).
    int digital_filter;      // 0 / 1 / 2 / 15 тактов
    bool analog_filter;      // true = фильтр включён (I2C_ANALOGFILTER_ENABLE=0 в HAL — инверсия скрыта в кодогене)

    // Только для mode == "slave" (HAL_I2C_SlaveInit).
    bool general_call;
    bool no_stretch_mode;
    bool dual_address_enabled;
    int own_address_2;             // валиден только если dual_address_enabled
    std::string own_address_2_mask; // "none".."1111111", валиден только если dual_address_enabled

    // Необязательное поле — не более 2 элементов (по одному на tx/rx),
    // пустой вектор = прежнее поведение (только блокирующие Transmit/Receive,
    // как и раньше). См. schemas/i2c.json, "dma".
    std::vector<I2cDmaConfig> dma;

    // Включает генерацию шаблона обработчика прерывания (HAL_I2C_IRQHandler)
    // для последующего вызова пользователем HAL_I2C_Master/Slave_*_IT —
    // сами _IT-функции (mik32_hal_i2c.c:1139+) не требуют от кодогена
    // никакой доп. настройки хендла, в отличие от DMA. См. schemas/i2c.json.
    bool interrupt_mode = false;
};

ParseResult<I2cConfig> parseI2cConfig(const std::string& file_name_path);

// Блочная точка входа — см. обоснование в parser/gpio/gpio_parser.h:
// валидирует ОДИН уже извлечённый элемент "peripherals", чтобы
// мультипериферийный пайплайн мог разобрать все блоки этого типа, а не
// только первый, не дублируя правила схемы.
ParseResult<I2cConfig> parseI2cBlock(const nlohmann::json& i2c);

#endif //MIK32_CONFIGURATOR_I2C_PARSER_H
