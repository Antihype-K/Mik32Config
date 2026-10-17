//
// Загрузчик knowledge_base/timer16.json — по образцу gpio_knowledge.h/uart_knowledge.h.
//
#ifndef MIK32_CONFIGURATOR_TIMER16_KNOWLEDGE_H
#define MIK32_CONFIGURATOR_TIMER16_KNOWLEDGE_H

#include <string>
#include <map>
#include "parse_result.h"

struct Timer16Knowledge
{
    int max_instances;

    // instance (0/1/2) -> имя указателя-хендла HAL, например "TIMER16_1".
    std::map<int, std::string> instance_handle;

    // реальный делитель ("1".."128") -> имя HAL enum'а, например "TIMER16_PRESCALER_16".
    std::map<std::string, std::string> prescaler_hal_enum;

    // ТЗ §3.1.8: HSI 32МГц, используется как sys_clk при Clock.Source=
    // INTERNAL_SYSTEM — константа для расчёта частоты ШИМ в комментарии
    // (см. codegen/timer16/timer16_codegen.cpp).
    long sys_clk_hz;

    struct FixedPin
    {
        int gpio_instance;
        int pin;
    };
    // instance (0/1/2) -> жёстко закреплённый датащитом ШИМ-вывод.
    // Используется и кодогеном (для авто-резервирования пина — ТЗ §3.1.4
    // п.4), и validation/cross_module_validator.cpp (поиск конфликтов).
    std::map<int, FixedPin> instance_fixed_pwm_pin;

    // AUDIT_TZ_PERIPHERY_GAPS.md, Timer16 (2026-10-14). instance -> Input1
    // (занят при mode=external_event_count ИЛИ mode=encoder) / Input2
    // (занят только при mode=encoder) — см. knowledge_base/timer16.json.
    std::map<int, FixedPin> instance_fixed_input1_pin;
    std::map<int, FixedPin> instance_fixed_input2_pin;

    // instance (0/1/2) -> имя дефолтного (функционально неактивного —
    // Trigger.ActiveEdge=SOFTWARE) HAL enum'а источника триггера.
    std::map<int, std::string> instance_default_trigger_source;

    // "period"/"one_shot"/"external_event_count" -> занимает ли этот режим
    // фиксированный ШИМ-вывод (true) или нет (false, только счёт).
    std::map<std::string, bool> mode_waveform_enable;
};

ParseResult<Timer16Knowledge> loadTimer16Knowledge(const std::string& file_path);

#endif //MIK32_CONFIGURATOR_TIMER16_KNOWLEDGE_H
