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
};

ParseResult<Timer32Config> parseTimer32Config(const std::string& file_name_path);

// Блочная точка входа — см. обоснование в parser/gpio/gpio_parser.h.
ParseResult<Timer32Config> parseTimer32Block(const nlohmann::json& timer32);

#endif //MIK32_CONFIGURATOR_TIMER32_PARSER_H
