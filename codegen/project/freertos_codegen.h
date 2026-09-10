//
// FreeRTOS: генерация FreeRTOSConfig.h + FreeRTOS-специфичной формы main.c +
// вендоринг исходников ядра (Bundled/Network/LocalPath).
//
// Место в пайплайне (см. project_codegen.cpp): контрибуции периферии
// (PeripheralContribution) строятся как обычно — FreeRTOS не меняет ИХ,
// только ФОРМУ main(), в которую они вставляются (grill-решение
// 2026-09-10: init-вызовы остаются перед vTaskStartScheduler(), while(1) не
// генерируется вовсе). Поэтому этот модуль не выпускает PeripheralContribution
// (FreeRTOS — не периферия, см. parser/freertos/freertos_parser.h), а
// отдельный набор кусочков, которые project_codegen.cpp вставляет в main.c
// вместо classic-варианта, когда config.freertos.enabled.
//
#ifndef MIK32_CONFIGURATOR_FREERTOS_CODEGEN_H
#define MIK32_CONFIGURATOR_FREERTOS_CODEGEN_H

#include <string>
#include <vector>
#include "freertos_parser.h"
#include "peripheral_contribution.h"
#include "parse_result.h"
#include "codegen_result.h"

struct FreeRtosProjectPieces
{
    // Полное содержимое src/FreeRTOSConfig.h.
    std::string freertos_config_h;

    // Заголовки, которые нужно добавить в main_c_includes сборщика (помимо
    // тех, что уже просят контрибуции периферии): "FreeRTOS.h", "task.h" и,
    // по необходимости, "queue.h"/"semphr.h"/"timers.h".
    std::vector<std::string> main_c_includes;

    // Глобальные хендлы объектов ОС (QueueHandle_t/SemaphoreHandle_t/
    // TimerHandle_t по имени пользователя) + прототипы функций задач/
    // колбэков таймеров — единым блоком, вставляется в main.c после externs
    // контрибуций периферии, перед SystemClock_Config.
    std::string globals_and_prototypes;

    // xTaskCreate/xQueueCreate/xSemaphoreCreate*/xTimerCreate — вставляется в
    // main() после init-вызовов периферии и секции USER CODE BEGIN Init,
    // перед vTaskStartScheduler().
    std::string object_and_task_creation;

    // Определения функций задач + колбэков программных таймеров —
    // вставляется в main.c после SystemClock_Config.
    std::string task_and_timer_definitions;

    // raw_trap_handler/freertos_risc_v_application_interrupt_handler/
    // _exception_handler + vApplicationMallocFailedHook/vAssertCalled —
    // вставляется в main.c последним блоком.
    std::string trap_handler_glue;
};

// console_available — тот же признак, что уже вычисляет project_codegen.cpp
// (findConsoleUartIndex(...) >= 0): если есть UART с включённым TX,
// vApplicationMallocFailedHook/exception-обработчик печатают диагностику
// через xprintf (как в docs/сделать/mik32-freertos/src/scr_sys.c), иначе —
// только останавливают выполнение, не ссылаясь на xprintf.h вовсе.
//
// gpio_irq_handler_function_names — имена уже сгенерированных
// GPIO_<N>_IRQHandler_Template (PeripheralContribution::irq_handler_function_name
// у всех непустых GPIO-контрибуций), в порядке их появления в contributions.
ParseResult<FreeRtosProjectPieces> buildFreeRtosProjectPieces(
    const FreeRtosConfig& config,
    bool console_available,
    const std::vector<std::string>& gpio_irq_handler_function_names);

// Копирует исходники FreeRTOS-Kernel + RISC-V/SCR1 порт в
// <output_project_dir>/src/FreeRTOS-Kernel/ по режиму config.source_mode
// (тот же трёхрежимный контракт, что и HalSourceMode в cmake_project_codegen.cpp
// — Bundled из templates/freertos_kernel_snapshot/, Network через git clone
// официального github.com/FreeRTOS/FreeRTOS-Kernel, LocalPath — каталог
// пользователя с той же структурой include/+portable/).
//
// portable/MemMang копируется ВЫБОРОЧНО: только heap_<N>.c, соответствующий
// config.os_config.heap_scheme — остальные четыре реализации (все объявляют
// один и тот же pvPortMalloc/vPortFree) не копируются вовсе, чтобы не
// потребовался build_src_filter в platformio.ini (см. docs/сделать/
// mik32-freertos/platformio.ini — там та же проблема решена фильтром;
// здесь решается на этапе копирования, раз конфигуратор сам решает, что
// копировать).
CodegenWriteResult writeFreeRtosKernelSource(
    const FreeRtosConfig& config,
    const std::string& kernel_snapshot_dir,
    const std::string& output_project_dir);

#endif //MIK32_CONFIGURATOR_FREERTOS_CODEGEN_H
