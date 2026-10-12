//
// Timer32Config / parseTimer32Config — по образцу parser/timer16/timer16_parser.h.
// ТЗ §3.1.5.
//
#ifndef MIK32_CONFIGURATOR_TIMER32_PARSER_H
#define MIK32_CONFIGURATOR_TIMER32_PARSER_H
#include <string>
#include <vector>
#include <cstdint>
#include "parse_result.h"
#include "json.hpp"

struct Timer32ChannelConfig
{
    int index;              // 0-3
    std::string mode;       // "pwm" | "capture" | "compare"
    uint32_t ocr;            // используется для pwm/compare, игнорируется для capture
    bool pwm_invert;         // только mode=pwm
    std::string capture_edge; // только mode=capture: "rising" | "falling"
    bool noise_filter;        // только mode=capture

    // OVERNIGHT_FIELD_REFERENCE.md: альтернатива ocr для mode=pwm — вместо
    // прямого регистрового значения задаётся duty_percent, пересчёт в ocr
    // делает парсер, зная top родительского Timer32Config. Для mode=compare
    // duty_percent не применим (ocr там порог сравнения, не скважность).
    bool has_duty_percent = false;
    double requested_duty_percent = 0.0;
};

struct Timer32Config
{
    std::string type;
    int instance;
    uint32_t prescaler;
    std::string count_mode; // "forward" | "reverse" | "bidirectional"
    uint32_t top;
    // Пусто для instance=0 (ТЗ §3.1.5: Timer32_0 — только счётчик, без каналов).
    std::vector<Timer32ChannelConfig> channels;

    // OVERNIGHT_FIELD_REFERENCE.md: альтернатива prescaler+top — задать
    // желаемую частоту ШИМ напрямую, парсер сам вычисляет top (при
    // prescaler=0, максимальное разрешение — см. парсер).
    bool has_frequency = false;
    double requested_frequency_hz = 0.0;
};

ParseResult<Timer32Config> parseTimer32Config(const std::string& file_name_path);

// Блочная точка входа — см. обоснование в parser/gpio/gpio_parser.h.
ParseResult<Timer32Config> parseTimer32Block(const nlohmann::json& timer32);

#endif //MIK32_CONFIGURATOR_TIMER32_PARSER_H
