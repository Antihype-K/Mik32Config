//
// Timer16Config / parseTimer16Config — по образцу parser/gpio/gpio_parser.h
// и parser/USART/usart_parser.h. ТЗ §3.1.4.
//
#ifndef MIK32_CONFIGURATOR_TIMER16_PARSER_H
#define MIK32_CONFIGURATOR_TIMER16_PARSER_H
#include <string>
#include <cstdint>
#include "parse_result.h"
#include "json.hpp"

struct Timer16Config
{
    std::string type;
    int instance;
    std::string mode;
    int prescaler;
    uint16_t arr;
    uint16_t cmp;

    // OVERNIGHT_FIELD_REFERENCE.md — пересчёт частоты/скважности. Если
    // JSON-блок задал "frequency_hz"+"duty_percent" вместо prescaler/arr/cmp,
    // парсер сам подбирает prescaler и вычисляет arr/cmp (оставляя их выше
    // заполненными результатом); здесь хранится ЗАПРОШЕННОЕ пользователем
    // значение — для комментария в сгенерированном коде (запрошено vs
    // фактически получилось после округления, как у UART baud_rate).
    bool has_frequency_duty = false;
    double requested_frequency_hz = 0.0;
    double requested_duty_percent = 0.0;
};

ParseResult<Timer16Config> parseTimer16Config(const std::string& file_name_path);

// Блочная точка входа — см. обоснование в parser/gpio/gpio_parser.h:
// валидирует ОДИН уже извлечённый элемент "peripherals", чтобы
// мультипериферийный пайплайн мог разобрать все блоки этого типа, а не
// только первый, не дублируя правила схемы.
ParseResult<Timer16Config> parseTimer16Block(const nlohmann::json& timer16);

#endif //MIK32_CONFIGURATOR_TIMER16_PARSER_H
