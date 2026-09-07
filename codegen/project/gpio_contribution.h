//
// Вклад GPIO в общий сгенерированный проект (см. концепцию в
// codegen/project/peripheral_contribution.h).
//
// Отличие от удалённого codegen/gpio/: там на входе был ОДИН GpioConfig и на
// выходе три готовых файла; здесь на входе ВСЕ блоки type="gpio",
// относящиеся к одному instance, а на выходе — кусочки, которые сшивает
// codegen/project/project_codegen.cpp.
//
// Почему на входе вектор, а не один блок. Несколько блоков gpio с одним и тем
// же instance — штатная и частая конфигурация: у порта GPIO_0 пины 0-1 могут
// быть выходами с подтяжкой 8мА, а пин 5 — входом с подтяжкой вверх. Схема
// schemas/gpio.json задаёт mode/pull/drive_strength на весь блок сразу, то
// есть описать это одним блоком физически невозможно. При этом HAL_GPIO_Init
// (docs/examples/mik32-hal/peripherals/Source/mik32_hal_gpio.c:47-72) делает
// read-modify-write по каждому пину отдельно, поэтому несколько вызовов
// подряд по одному порту корректны и не затирают друг друга. Все такие блоки
// сливаются в ОДНУ функцию GPIO_<instance>_Init с несколькими
// HAL_GPIO_Init — так у каждого физического порта ровно одна точка
// инициализации в сгенерированном коде.
//
#ifndef MIK32_CONFIGURATOR_GPIO_CONTRIBUTION_H
#define MIK32_CONFIGURATOR_GPIO_CONTRIBUTION_H

#include <vector>
#include "gpio_parser.h"
#include "gpio_knowledge.h"
#include "parse_result.h"
#include "peripheral_contribution.h"

// blocks — непустой список блоков с ОДИНАКОВЫМ instance (группировку делает
// вызывающий, codegen/project/project_codegen.cpp).
// emit_demo_loop — добавлять ли в тело while(1) переключение пинов
// (HAL_GPIO_TogglePin) для блоков в режиме "output". См. обоснование флага в
// codegen/project/project_codegen.h.
ParseResult<PeripheralContribution> buildGpioContribution(
    const std::vector<GpioConfig>& blocks,
    const GpioKnowledge& knowledge,
    bool emit_demo_loop);

#endif //MIK32_CONFIGURATOR_GPIO_CONTRIBUTION_H
