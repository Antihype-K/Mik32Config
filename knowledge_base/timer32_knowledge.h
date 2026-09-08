//
// Загрузчик knowledge_base/timer32.json — по образцу timer16_knowledge.h.
//
#ifndef MIK32_CONFIGURATOR_TIMER32_KNOWLEDGE_H
#define MIK32_CONFIGURATOR_TIMER32_KNOWLEDGE_H

#include <string>
#include <map>
#include "parse_result.h"

struct Timer32Knowledge
{
    int max_instances;

    // instance (0/1/2) -> имя указателя-хендла HAL, например "TIMER32_1".
    std::map<int, std::string> instance_handle;

    // instance -> есть ли у него каналы ШИМ/захват/сравнение (false только
    // для Timer32_0 — ТЗ §3.1.5: «базовый счётчик без режимов ШИМ, захвата
    // и сравнения»).
    std::map<int, bool> instance_has_channels;

    std::map<std::string, std::string> count_mode_hal_enum;
    std::map<std::string, std::string> channel_mode_hal_enum;
    std::map<std::string, std::string> capture_edge_hal_enum;

    // ТЗ §3.1.8: HSI 32МГц, используется для расчёта частоты счёта в
    // комментарии над Timer32_Init (freq = sys_clk_hz / (prescaler + 1)).
    long sys_clk_hz;

    struct FixedPin
    {
        int gpio_instance;
        int pin;
    };
    // instance (1/2) -> channel_index (0-3) -> жёстко закреплённый датащитом
    // вывод. Используется и кодогеном (авто-резервирование — ТЗ §3.1.5 п.4),
    // и validation/cross_module_validator.cpp (поиск конфликтов).
    std::map<int, std::map<int, FixedPin>> instance_channel_fixed_pin;
};

ParseResult<Timer32Knowledge> loadTimer32Knowledge(const std::string& file_path);

#endif //MIK32_CONFIGURATOR_TIMER32_KNOWLEDGE_H
