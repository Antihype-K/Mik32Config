//
// FreeRtosConfig / parseFreeRtosBlock — конфигурация FreeRTOS, единая на весь
// проект (ProjectConfig::freertos), а не блок в массиве "peripherals".
//
// Почему отдельный парсер, а не поля прямо в project_config.cpp, как у
// toolchain/hal_source_mode: те — по одному скаляру каждый, а freertos —
// вложенный объект с пятью массивами объектов (tasks/queues/semaphores/
// mutexes/timers), с тем же объёмом валидации, что и у периферийных
// парсеров (parser/timer16, parser/USART и т.п.) — поэтому оформлен по их
// образцу, а не как ещё десяток веток if в project_config.cpp.
//
// FreeRTOS — единственный модуль проекта, для которого сознательно не
// заведён элемент peripherals[].type=="freertos": grill-сессия 2026-09-10
// зафиксировала это явным решением пользователя — FreeRTOS меняет форму
// main() целиком (vTaskStartScheduler вместо while(1)), а не добавляет ещё
// один init-вызов, поэтому концептуально это свойство ВСЕГО проекта, как
// toolchain, а не периферия.
//
#ifndef MIK32_CONFIGURATOR_FREERTOS_PARSER_H
#define MIK32_CONFIGURATOR_FREERTOS_PARSER_H

#include <string>
#include <vector>
#include "parse_result.h"
#include "json.hpp"

// Откуда брать исходники FreeRTOS-Kernel + RISC-V/SCR1 порт под MIK32 —
// тот же трёхрежимный выбор, что и HalSourceMode (codegen/project/
// project_config.h) для HAL в CMake-тулчейне, по прямому решению
// пользователя (grill-сессия 2026-09-10): симметрия ожидаема пользователем
// заранее, а не добавлена по инициативе исполнителя.
enum class FreeRtosSourceMode { Bundled, Network, LocalPath };

struct FreeRtosOsConfig
{
    // Курируемое подмножество FreeRTOSConfig.h (grill-решение: не полная
    // parity с CubeMX) — только то, что реально меняет поведение планировщика
    // от проекта к проекту. Дефолты — дословно из
    // docs/сделать/mik32-freertos/src/FreeRTOSConfig.h (единственный
    // подтверждённый рабочий набор значений под MIK32/SCR1).
    int tick_rate_hz = 100;
    int max_priorities = 7;
    int minimal_stack_size_words = 256;
    int total_heap_size_bytes = 10 * 1024;
    bool use_preemption = true;
    // 0 = выключено, 1 = метод 1 (быстрый, штамп указателя стека), 2 = метод 2
    // (штамп + заполнение шаблоном) — configCHECK_FOR_STACK_OVERFLOW принимает
    // ровно эти три значения (FreeRTOS.h, документация проекта FreeRTOS).
    int stack_overflow_check = 0;
    // heap_1..heap_5 (FreeRTOS/Source/portable/MemMang/*.c) — heap_4 выбран
    // дефолтом здесь (НЕ heap_1, как в примере docs/сделать/mik32-freertos):
    // heap_1 не умеет освобождать память вовсе (vPortFree — no-op), что для
    // пользовательских задач общего назначения (не факт, что ни одна никогда
    // не удалит очередь/задачу) more suprising, чем чуть больший код heap_4.
    // Расхождение с примером осознанное — см. docs/implementations/freertos.md.
    std::string heap_scheme = "heap_4";
};

struct FreeRtosTaskConfig
{
    std::string name;
    int priority = 1;
    int stack_size_words = 128;
};

struct FreeRtosQueueConfig
{
    std::string name;
    int length = 1;
    int item_size_bytes = 4;
};

struct FreeRtosSemaphoreConfig
{
    std::string name;
    std::string kind; // "binary" | "counting"
    int initial_count = 0; // используется только при kind == "counting"
    int max_count = 1;     // используется только при kind == "counting"
};

struct FreeRtosMutexConfig
{
    std::string name;
    std::string kind; // "normal" | "recursive"
};

struct FreeRtosTimerConfig
{
    std::string name;
    std::string kind; // "one_shot" | "periodic"
    int period_ms = 1000;
};

struct FreeRtosConfig
{
    bool enabled = false;
    FreeRtosSourceMode source_mode = FreeRtosSourceMode::Bundled;
    std::string source_local_path; // используется только при source_mode == LocalPath
    FreeRtosOsConfig os_config;
    std::vector<FreeRtosTaskConfig> tasks;
    std::vector<FreeRtosQueueConfig> queues;
    std::vector<FreeRtosSemaphoreConfig> semaphores;
    std::vector<FreeRtosMutexConfig> mutexes;
    std::vector<FreeRtosTimerConfig> timers;
};

// Разбирает и валидирует ровно узел root["freertos"] (объект, не элемент
// массива peripherals — см. обоснование выше). Если узла нет вовсе,
// project_config.cpp просто не вызывает эту функцию и оставляет
// FreeRtosConfig{} по умолчанию (enabled=false) — отсутствие поля не ошибка,
// как и у toolchain/hal_source_mode.
ParseResult<FreeRtosConfig> parseFreeRtosBlock(const nlohmann::json& freertos_node);

#endif //MIK32_CONFIGURATOR_FREERTOS_PARSER_H
