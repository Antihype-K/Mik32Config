//
// Загрузчик knowledge_base/gpio.json.
//
// Концепция: аппаратные факты (какая строка схемы соответствует какому
// HAL-enum'у, какой макрос включает тактирование какого инстанса) не
// зашиваются в код кодогенератора напрямую, а читаются из отдельного JSON
// (knowledge_base/gpio.json), заполненного вручную из даташита МИК32 v2.2.3 —
// см. архитектурный документ, §7 "База знаний МИК32", Правило В1. Это даёт
// возможность позже поправить аппаратный факт (например, число пинов на
// инстанс) без перекомпиляции логики кодогенератора.
//
#ifndef MIK32_CONFIGURATOR_GPIO_KNOWLEDGE_H
#define MIK32_CONFIGURATOR_GPIO_KNOWLEDGE_H

#include <string>
#include <map>
#include "parse_result.h"

struct GpioKnowledge
{
    int max_instances;

    // instance (0/1/2) -> число физически распаянных пинов на этом
    // instance. Не одинаково для всех instance — GPIO_2 имеет только 8,
    // а не 16 (см. _comment_pins_per_instance в knowledge_base/gpio.json).
    std::map<int, int> pins_per_instance;

    // instance (0/1/2) -> имя макроса включения тактирования, например
    // "__HAL_PCC_GPIO_0_CLK_ENABLE".
    std::map<int, std::string> instance_clock_enable_macro;

    // instance (0/1/2) -> имя указателя-хендла HAL, например "GPIO_0".
    std::map<int, std::string> instance_gpio_handle;

    // строка схемы ("output", "input", ...) -> имя HAL enum'а,
    // например "HAL_GPIO_MODE_GPIO_OUTPUT".
    std::map<std::string, std::string> mode_hal_enum;
    std::map<std::string, std::string> pull_hal_enum;
    std::map<std::string, std::string> drive_strength_hal_enum;

    // "low"/"high"/"falling"/"rising"/"change" -> имя HAL enum'а
    // HAL_GPIO_InterruptMode (ТЗ §3.1.2 п.5).
    std::map<std::string, std::string> trigger_hal_enum;

    // "<gpio_instance>_<pin>_<line>" -> имя enum-константы
    // HAL_GPIO_Line_Config, например "GPIO_MUX_PORT0_6_LINE_2". Не каждая
    // комбинация (pin, line) существует физически — отсутствие ключа
    // означает недопустимую комбинацию (см. _comment_irq_mux_enum в
    // knowledge_base/gpio.json).
    std::map<std::string, std::string> irq_mux_enum;
};

ParseResult<GpioKnowledge> loadGpioKnowledge(const std::string& file_path);

#endif //MIK32_CONFIGURATOR_GPIO_KNOWLEDGE_H
