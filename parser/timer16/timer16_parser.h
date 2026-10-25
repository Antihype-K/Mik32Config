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

    // Тир 1 аудита (AUDIT_TZ_PERIPHERY_GAPS.md): все пять — необязательны,
    // дефолты сохраняют прежнее хардкод-поведение кодогена
    // (schemas/timer16.json). clock_source принимается, но игнорируется
    // кодогеном при mode=="external_event_count" (Clock.Source там всегда
    // TIMER16_SOURCE_EXTERNAL_INPUT1) — тот же паттерн, что уже был у "cmp"
    // до этой сессии.
    std::string clock_source = "internal_system"; // mik32_hal_timer16.h, HAL_Timer16_SourceTypeDef (минус EXTERNAL_INPUT1 — см. выше)
    std::string trigger_edge = "software";         // Trigger.ActiveEdge — HAL_Timer16_Trigger_ActiveEdgeTypeDef

    // НЕОБЯЗАТЕЛЬНОЕ поле (добавлено 2026-10-22, schemas/timer16.json
    // "trigger_source"). Пустая строка — поле не задано пользователем,
    // кодоген берёт instance_default_trigger_source из knowledge_base
    // (прежнее поведение). Непустое — один из 8 логических имён
    // ("gpio_a".."gpio_d","temperature","adc","rtc","alarm"), кодоген
    // смотрит trigger_source_hal_enum[instance][trigger_source].
    std::string trigger_source = "";
    std::string external_clock_filter = "none";    // Filter.ExternalClock — HAL_Timer16_FilterTypeDef
    std::string trigger_filter = "none";            // Filter.Trigger — HAL_Timer16_FilterTypeDef
    bool waveform_inverted = false;                 // Waveform.Polarity, только при Waveform.Enable=ENABLE (period/one_shot)

    // docs/open_tasks.md "Timer16 — прерывания не реализованы". ARRM/CMPM —
    // HAL_Timer16_*_IT включают их через SetInterruptMask (см.
    // codegen/project/timer16_contribution.cpp).
    bool overflow_interrupt = false; // TIMER16_FLAG_ARRM/TIMER16_IER_ARRMIE_M
    bool compare_interrupt = false;  // TIMER16_FLAG_CMPM/TIMER16_IER_CMPMIE_M, только при mode=period/one_shot
};

ParseResult<Timer16Config> parseTimer16Config(const std::string& file_name_path);

// Блочная точка входа — см. обоснование в parser/gpio/gpio_parser.h:
// валидирует ОДИН уже извлечённый элемент "peripherals", чтобы
// мультипериферийный пайплайн мог разобрать все блоки этого типа, а не
// только первый, не дублируя правила схемы.
ParseResult<Timer16Config> parseTimer16Block(const nlohmann::json& timer16);

#endif //MIK32_CONFIGURATOR_TIMER16_PARSER_H
