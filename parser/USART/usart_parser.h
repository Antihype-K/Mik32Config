//
// UartConfig / parseUsartConfig — по образцу parser/gpio/gpio_parser.h.
//
#ifndef MIK32_CONFIGURATOR_USART_PARSER_H
#define MIK32_CONFIGURATOR_USART_PARSER_H
#include <string>
#include <vector>
#include "parse_result.h"
#include "json.hpp"

struct UartDmaConfig
{
    int channel;               // 0-3 (ТЗ §3.1.3.3)
    std::string direction;     // "tx" | "rx"
    std::string buffer_type;   // "linear" | "circular"
    int buffer_size_bytes;
    std::string element_size;  // "byte" | "halfword" | "word"
    bool increment;
};

struct UartModemSignalsConfig
{
    bool ri;
    bool dsr;
    bool dtr;
    bool ddis;
    bool dcd;
};

struct UartConfig
{
    std::string type;
    int instance;
    int baud_rate;
    int data_bits;
    int stop_bits;
    std::string parity;
    bool flow_control;
    std::string direction;
    // Пусто, если DMA не запрошен (обычный случай) — ТЗ §3.1.3.3.
    std::vector<UartDmaConfig> dma;

    // Ночная сессия 12->13.10.2026 — см. OVERNIGHT_FIELD_REFERENCE.md.
    // Все поля ниже необязательные (не меняют required_fields), поведение
    // по умолчанию при отсутствии поля — реальный default HAL-перечисления.
    std::string bit_direction = "lsb_first";    // "msb_first"/"lsb_first" -> HAL_USART_BitDirection_enum
    std::string mode = "asynchronous";          // "asynchronous"/"synchronous" -> HAL_USART_Mode_enum
    std::string xck_mode = "mode0";              // "mode0".."mode3", только при mode="synchronous"
    bool last_byte_clock = false;                // только при mode="synchronous"
    std::string channel_mode = "duplex";         // "duplex"/"half_duplex" -> HAL_USART_ChannelMode_enum
    bool tx_inversion = false;
    bool rx_inversion = false;
    bool swap = false;                           // Swap RXD & TXD
    std::string rts_mode = "always_enabled";     // "always_enabled"/"modem", только при flow_control=true
    bool has_modem_signals = false;
    UartModemSignalsConfig modem_signals = {false, false, false, false, false};
    // ПРИМЕЧАНИЕ: отдельного поля parity_bit_inversion НЕТ — это не самостоятельная
    // фича. HAL-поле parity_bit_inversion физически кодирует бит PS (чётность/
    // нечётность, датащит Таблица 18, разряд 9) и уже полностью управляется
    // существующим полем "parity" (none/even/odd) через knowledge_base/uart.json —
    // добавление отдельного JSON-поля дало бы два разных способа задать одно и то
    // же, с риском противоречия (см. OVERNIGHT_FIELD_REFERENCE.md).
};

ParseResult<UartConfig> parseUsartConfig(const std::string& file_name_path);

// Блочная точка входа — см. обоснование в parser/gpio/gpio_parser.h:
// валидирует ОДИН уже извлечённый элемент "peripherals", чтобы
// мультипериферийный пайплайн мог разобрать все блоки этого типа, а не
// только первый, не дублируя правила схемы.
ParseResult<UartConfig> parseUartBlock(const nlohmann::json& uart);

#endif //MIK32_CONFIGURATOR_USART_PARSER_H
