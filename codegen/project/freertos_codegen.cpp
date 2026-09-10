#include "freertos_codegen.h"
#include "contribution_utils.h"
#include "utf8_path.h"
#include "win_file_io.h"
#include <cstdlib>
#include <filesystem>
#include <sstream>

namespace fs = std::filesystem;

namespace
{
    ParseResult<FreeRtosProjectPieces> makeError(const std::string& message)
    {
        return ParseResult<FreeRtosProjectPieces>{false, message, std::nullopt};
    }

    CodegenWriteResult makeWriteError(const std::string& message)
    {
        return CodegenWriteResult{false, message};
    }

    // Минимальное шелл-экранирование для одного аргумента-пути в git clone —
    // независимая копия codegen/project/cmake_project_codegen.cpp::quoteForShell
    // (тот файл её не экспортирует, а трогать HW-подтверждённый CMake-путь
    // ради общего заголовка из-за одной двухстрочной функции не стоит).
    std::string quoteForShellLocal(const std::string& path)
    {
        std::string quoted = "\"";
        for (char c : path)
        {
            if (c == '"' || c == '\\')
            {
                quoted += '\\';
            }
            quoted += c;
        }
        quoted += "\"";
        return quoted;
    }

    // --- FreeRTOSConfig.h ----------------------------------------------------
    // Дословный шаблон из docs/сделать/mik32-freertos/src/FreeRTOSConfig.h —
    // единственного подтверждённого рабочего набора значений под MIK32/SCR1
    // (Syntacore, автор порта). Курируемое подмножество (grill-решение
    // 2026-09-10) подставляется, остальное — фиксированные строки один в
    // один с оригиналом. Тестовые defines демо-приложения FreeRTOS
    // (bktBLOCK_TIME_TASK_STACK_SIZE и т.п., хвост оригинального файла) не
    // перенесены — они принадлежат тестовому набору самого FreeRTOS, не
    // нужны сгенерированному пользовательскому коду.
    std::string buildFreeRtosConfigH(const FreeRtosConfig& config)
    {
        std::ostringstream out;
        out << "/*\n"
               " * Сгенерировано MIK32-Configurator.\n"
               " * Курируемое подмножество FreeRTOSConfig.h — см.\n"
               " * docs/implementations/freertos.md. Остальные параметры фиксированы на\n"
               " * значениях из docs/сделать/mik32-freertos/src/FreeRTOSConfig.h (Syntacore,\n"
               " * единственный подтверждённый порт под MIK32/SCR1).\n"
               " */\n"
               "#ifndef FREERTOS_CONFIG_H\n"
               "#define FREERTOS_CONFIG_H\n"
               "\n"
               "#include \"csr.h\"\n"
               "#include \"scr1_csr_encoding.h\"\n"
               "#include \"scr1_timer.h\"\n"
               "#include \"mik32_memory_map.h\"\n"
               "\n"
               "#define configMTIME_BASE_ADDRESS                ( SCR1_TIMER_BASE_ADDRESS + 8 )\n"
               "#define configMTIMECMP_BASE_ADDRESS             ( SCR1_TIMER_BASE_ADDRESS + 16 )\n"
               "#define configUSE_PREEMPTION                    " << (config.os_config.use_preemption ? 1 : 0) << "\n"
               "#define configUSE_IDLE_HOOK                     0\n"
               "#define configUSE_TICK_HOOK                     0\n"
               // configCPU_CLOCK_HZ: HSI 32МГц — единственный источник
               // тактирования во всём проекте (ТЗ §3.1.8, SystemClock_Config).
               "#define configCPU_CLOCK_HZ                      ( 32000000 )\n"
               "#define configTICK_RATE_HZ                      ( ( TickType_t ) " << config.os_config.tick_rate_hz << " )\n"
               "#define configMAX_PRIORITIES                    ( " << config.os_config.max_priorities << " )\n"
               "#define configMINIMAL_STACK_SIZE                ( ( unsigned short ) " << config.os_config.minimal_stack_size_words << " )\n"
               "#define configISR_STACK_SIZE_WORDS              128\n"
               "#define configTOTAL_HEAP_SIZE                   ( ( size_t ) " << config.os_config.total_heap_size_bytes << " )\n"
               "#define configMAX_TASK_NAME_LEN                 ( 16 )\n"
               "#define configUSE_TRACE_FACILITY                0\n"
               "#define configUSE_16_BIT_TICKS                  0\n"
               "#define configIDLE_SHOULD_YIELD                 0\n"
               "#define configUSE_MUTEXES                       1\n"
               "#define configQUEUE_REGISTRY_SIZE               8\n"
               "#define configCHECK_FOR_STACK_OVERFLOW          " << config.os_config.stack_overflow_check << "\n"
               "#define configUSE_RECURSIVE_MUTEXES             1\n"
               "#define configUSE_MALLOC_FAILED_HOOK            1\n"
               "#define configUSE_APPLICATION_TASK_TAG          0\n"
               "#define configUSE_COUNTING_SEMAPHORES           1\n"
               "#define configGENERATE_RUN_TIME_STATS           0\n"
               "#define configUSE_PORT_OPTIMISED_TASK_SELECTION 1\n"
               "\n"
               "/* Co-routines сознательно не поддержаны (устаревшая, не рекомендуемая\n"
               " * самим FreeRTOS часть API) — см. docs/implementations/freertos.md. */\n"
               "#define configUSE_CO_ROUTINES                   0\n"
               "#define configMAX_CO_ROUTINE_PRIORITIES         ( 2 )\n"
               "\n"
               // configUSE_TIMERS выводится, а не фиксируется: xTimerCreate/
               // API программных таймеров недоступны без этого флага, а
               // включать его для проектов без единого freertos.timers было
               // бы неиспользуемым кодом (timers.c) в сборке.
               "#define configUSE_TIMERS                        " << (config.timers.empty() ? 0 : 1) << "\n"
               "#define configTIMER_TASK_PRIORITY               ( configMAX_PRIORITIES - 1 )\n"
               "#define configTIMER_QUEUE_LENGTH                4\n"
               "#define configTIMER_TASK_STACK_DEPTH            ( configMINIMAL_STACK_SIZE )\n"
               "\n"
               "#define INCLUDE_vTaskPrioritySet                1\n"
               "#define INCLUDE_uxTaskPriorityGet               1\n"
               "#define INCLUDE_vTaskDelete                     1\n"
               "#define INCLUDE_vTaskCleanUpResources           1\n"
               "#define INCLUDE_vTaskSuspend                    1\n"
               "#define INCLUDE_vTaskDelayUntil                 1\n"
               "#define INCLUDE_vTaskDelay                      1\n"
               "#define INCLUDE_eTaskGetState                   1\n"
               "#define INCLUDE_xTimerPendFunctionCall          0\n"
               "#define INCLUDE_xTaskGetCurrentTaskHandle       1\n"
               "\n"
               "/* Normal assert() semantics without relying on the provision of an\n"
               " * assert.h header file (docs/сделать/mik32-freertos/src/FreeRTOSConfig.h). */\n"
               "void vAssertCalled( void );\n"
               "#define configASSERT( x ) do { if( ( x ) == 0 ) vAssertCalled(); } while (0)\n"
               "\n"
               "#endif /* FREERTOS_CONFIG_H */\n";
        return out.str();
    }

    std::string queueHandleType() { return "QueueHandle_t"; }
    std::string semaphoreHandleType() { return "SemaphoreHandle_t"; }
    std::string timerHandleType() { return "TimerHandle_t"; }
}

ParseResult<FreeRtosProjectPieces> buildFreeRtosProjectPieces(
    const FreeRtosConfig& config,
    bool console_available,
    const std::vector<std::string>& gpio_irq_handler_function_names)
{
    if (!config.enabled)
    {
        return makeError("buildFreeRtosProjectPieces вызван для выключенного FreeRTOS");
    }

    FreeRtosProjectPieces pieces;
    pieces.freertos_config_h = buildFreeRtosConfigH(config);

    pieces.main_c_includes = {"mik32_hal.h", "FreeRTOS.h", "task.h"};
    if (!config.queues.empty())
    {
        pieces.main_c_includes.push_back("queue.h");
    }
    if (!config.semaphores.empty() || !config.mutexes.empty())
    {
        pieces.main_c_includes.push_back("semphr.h");
    }
    if (!config.timers.empty())
    {
        pieces.main_c_includes.push_back("timers.h");
    }
    if (!gpio_irq_handler_function_names.empty())
    {
        pieces.main_c_includes.push_back("mik32_hal_gpio.h");
        pieces.main_c_includes.push_back("mik32_hal_irq.h");
    }
    if (console_available)
    {
        pieces.main_c_includes.push_back("xprintf.h");
    }

    // --- globals_and_prototypes ---------------------------------------------
    std::ostringstream globals;
    for (const FreeRtosQueueConfig& queue : config.queues)
    {
        globals << queueHandleType() << " " << queue.name << ";\n";
    }
    for (const FreeRtosSemaphoreConfig& sem : config.semaphores)
    {
        globals << semaphoreHandleType() << " " << sem.name << ";\n";
    }
    for (const FreeRtosMutexConfig& mutex : config.mutexes)
    {
        globals << semaphoreHandleType() << " " << mutex.name << ";\n";
    }
    for (const FreeRtosTimerConfig& timer : config.timers)
    {
        globals << timerHandleType() << " " << timer.name << ";\n";
    }
    globals << "\n";
    for (const FreeRtosTaskConfig& task : config.tasks)
    {
        globals << "static void " << task.name << "_Task(void *argument);\n";
    }
    for (const FreeRtosTimerConfig& timer : config.timers)
    {
        globals << "static void " << timer.name << "_Callback(TimerHandle_t xTimer);\n";
    }
    pieces.globals_and_prototypes = globals.str();

    // --- object_and_task_creation --------------------------------------------
    std::ostringstream creation;
    for (const FreeRtosQueueConfig& queue : config.queues)
    {
        creation << "    " << queue.name << " = xQueueCreate(" << queue.length
                 << ", " << queue.item_size_bytes << ");\n";
    }
    for (const FreeRtosSemaphoreConfig& sem : config.semaphores)
    {
        if (sem.kind == "binary")
        {
            creation << "    " << sem.name << " = xSemaphoreCreateBinary();\n";
        }
        else // "counting"
        {
            creation << "    " << sem.name << " = xSemaphoreCreateCounting("
                      << sem.max_count << ", " << sem.initial_count << ");\n";
        }
    }
    for (const FreeRtosMutexConfig& mutex : config.mutexes)
    {
        if (mutex.kind == "normal")
        {
            creation << "    " << mutex.name << " = xSemaphoreCreateMutex();\n";
        }
        else // "recursive"
        {
            creation << "    " << mutex.name << " = xSemaphoreCreateRecursiveMutex();\n";
        }
    }
    for (const FreeRtosTimerConfig& timer : config.timers)
    {
        const std::string auto_reload = (timer.kind == "periodic") ? "pdTRUE" : "pdFALSE";
        creation << "    " << timer.name << " = xTimerCreate(\"" << timer.name
                  << "\", pdMS_TO_TICKS(" << timer.period_ms << "), " << auto_reload
                  << ", NULL, " << timer.name << "_Callback);\n";
    }
    if (!config.queues.empty() || !config.semaphores.empty() || !config.mutexes.empty() || !config.timers.empty())
    {
        creation << "\n";
    }
    for (const FreeRtosTaskConfig& task : config.tasks)
    {
        creation << "    xTaskCreate(" << task.name << "_Task, \"" << task.name << "\", "
                  << task.stack_size_words << ", NULL, " << task.priority << ", NULL);\n";
    }
    pieces.object_and_task_creation = creation.str();

    // --- task_and_timer_definitions -------------------------------------------
    std::ostringstream definitions;
    for (size_t i = 0; i < config.tasks.size(); ++i)
    {
        const FreeRtosTaskConfig& task = config.tasks[i];
        if (i > 0)
        {
            definitions << "\n";
        }
        definitions << "static void " << task.name << "_Task(void *argument)\n"
                       "{\n"
                       "    /* USER CODE BEGIN Task_" << task.name << "_Init */\n"
                       "    /* USER CODE END Task_" << task.name << "_Init */\n"
                       "\n"
                       "    for (;;)\n"
                       "    {\n"
                       "        /* USER CODE BEGIN Task_" << task.name << "_Loop */\n"
                       "        vTaskDelay(pdMS_TO_TICKS(1));\n"
                       "        /* USER CODE END Task_" << task.name << "_Loop */\n"
                       "    }\n"
                       "}\n";
    }
    for (const FreeRtosTimerConfig& timer : config.timers)
    {
        definitions << "\n"
                       "static void " << timer.name << "_Callback(TimerHandle_t xTimer)\n"
                       "{\n"
                       "    /* USER CODE BEGIN Timer_" << timer.name << "_Callback */\n"
                       "    /* USER CODE END Timer_" << timer.name << "_Callback */\n"
                       "}\n";
    }
    pieces.task_and_timer_definitions = definitions.str();

    // --- trap_handler_glue ---------------------------------------------------
    // Форма — по образцу docs/сделать/mik32-freertos/src/main.c (raw_trap_handler/
    // freertos_risc_v_application_interrupt_handler) и src/scr_sys.c
    // (vApplicationMallocFailedHook/vAssertCalled), единственного
    // подтверждённого рабочего порта под MIK32/SCR1. GPIO_<N>_IRQHandler_Template
    // уже сам сбрасывает GPIO_IRQ->CLEAR на своей линии (gpio_contribution.cpp) —
    // HAL_GPIO_ClearInterrupts() здесь дополнительно, как в оригинальном
    // примере (иной регистр состояния, не LINE_MUX-канал).
    std::ostringstream glue;
    glue << "extern void freertos_risc_v_trap_handler();\n"
            "RAM_ATTR void raw_trap_handler()\n"
            "{\n"
            "    freertos_risc_v_trap_handler();\n"
            "}\n"
            "\n"
            "RAM_ATTR void freertos_risc_v_application_interrupt_handler(void)\n"
            "{\n";
    if (!gpio_irq_handler_function_names.empty())
    {
        glue << "    if (EPIC_CHECK_GPIO_IRQ())\n"
                "    {\n";
        for (const std::string& handler_name : gpio_irq_handler_function_names)
        {
            glue << "        " << handler_name << "();\n";
        }
        glue << "        HAL_GPIO_ClearInterrupts();\n"
                "    }\n"
                "\n";
    }
    glue << "    HAL_EPIC_Clear(0xFFFFFFFF);\n"
            "}\n"
            "\n"
            "void freertos_risc_v_application_exception_handler(void)\n"
            "{\n"
            "    taskDISABLE_INTERRUPTS();\n";
    if (console_available)
    {
        glue << "    xprintf(\"\\nFreeRTOS: необработанное исключение (mcause через отладчик)\\n\");\n";
    }
    glue << "    while (1)\n"
            "    {\n"
            "    }\n"
            "}\n"
            "\n"
            "void vApplicationMallocFailedHook(void)\n"
            "{\n"
            "    taskDISABLE_INTERRUPTS();\n";
    if (console_available)
    {
        glue << "    xprintf(\"\\nFreeRTOS: pvPortMalloc не смог выделить память "
                "(увеличьте freertos.os_config.total_heap_size_bytes)\\n\");\n";
    }
    glue << "    vTaskEndScheduler();\n"
            "}\n"
            "\n"
            "void vAssertCalled(void)\n"
            "{\n"
            "    taskDISABLE_INTERRUPTS();\n";
    if (console_available)
    {
        glue << "    xprintf(\"\\nFreeRTOS: configASSERT сработал\\n\");\n";
    }
    glue << "    vTaskEndScheduler();\n"
            "}\n";
    pieces.trap_handler_glue = glue.str();

    return ParseResult<FreeRtosProjectPieces>{true, "", pieces};
}

CodegenWriteResult writeFreeRtosKernelSource(
    const FreeRtosConfig& config,
    const std::string& kernel_snapshot_dir,
    const std::string& output_project_dir)
{
    const fs::path dest_root = utf8Path(output_project_dir) / "src" / "FreeRTOS-Kernel";
    std::error_code fs_error;

    fs::path source_root;
    fs::path temp_clone_dir; // непусто только для Network — удаляется в конце

    if (config.source_mode == FreeRtosSourceMode::Bundled)
    {
        source_root = utf8Path(kernel_snapshot_dir);
        if (!fs::exists(source_root, fs_error))
        {
            return makeWriteError("Снэпшот FreeRTOS-Kernel не найден: " + kernel_snapshot_dir);
        }
    }
    else if (config.source_mode == FreeRtosSourceMode::LocalPath)
    {
        if (config.source_local_path.empty())
        {
            return makeWriteError("freertos.source_mode=local_path, но source_local_path не задан");
        }
        source_root = utf8Path(config.source_local_path);
        if (!fs::exists(source_root, fs_error) || !fs::exists(source_root / "include", fs_error)
            || !fs::exists(source_root / "portable", fs_error))
        {
            return makeWriteError("Каталог freertos.source_local_path (" + config.source_local_path
                + ") не существует или не содержит include/+portable/");
        }
    }
    else // FreeRtosSourceMode::Network
    {
        // Официальный апстрим ядра (MIT) — github.com/FreeRTOS/FreeRTOS-Kernel.
        // Порт portable/GCC/RISC-V и chip_specific_extensions/SCR1 в нём же
        // (SCR1 — контрибуция Syntacore, автора docs/сделать/mik32-freertos),
        // как и в вендоренном снэпшоте Bundled-режима — это тот же самый код,
        // не отдельная MIK32-специфичная вилка.
        temp_clone_dir = utf8Path(output_project_dir) / "_freertos_kernel_clone_tmp";
        std::error_code cleanup_before;
        fs::remove_all(temp_clone_dir, cleanup_before);
        const std::string command = "git clone --depth 1 "
            "https://github.com/FreeRTOS/FreeRTOS-Kernel.git " + quoteForShellLocal(temp_clone_dir.u8string());
        const int clone_status = std::system(command.c_str());
        if (clone_status != 0)
        {
            return makeWriteError("git clone FreeRTOS-Kernel не удался (код " + std::to_string(clone_status)
                + ") — проверьте git и сетевой доступ, либо используйте "
                  "freertos.source_mode=bundled/local_path для offline-сборки");
        }
        source_root = temp_clone_dir;
        if (!fs::exists(source_root / "include", fs_error) || !fs::exists(source_root / "portable", fs_error))
        {
            fs::remove_all(temp_clone_dir, fs_error);
            return makeWriteError("Клонированный репозиторий FreeRTOS-Kernel не содержит ожидаемую структуру include/+portable/");
        }
    }

    // include/ и portable/ копируются отдельно, а не всё дерево целиком:
    // корень репозитория/снэпшота содержит ещё tasks.c/queue.c/... прямо на
    // верхнем уровне — это переносится copyDirectoryTreeSkipExisting(source_root, dest_root)
    // одним вызовом ниже, portable/MemMang после общего копирования
    // прорежается вручную (оставляем только выбранный heap_<N>.c).
    if (!copyDirectoryTreeSkipExisting(source_root.u8string(), dest_root.u8string(), fs_error))
    {
        if (!temp_clone_dir.empty())
        {
            std::error_code cleanup_error;
            fs::remove_all(temp_clone_dir, cleanup_error);
        }
        return makeWriteError("Не удалось скопировать FreeRTOS-Kernel из " + source_root.u8string()
            + ": " + fs_error.message());
    }

    if (!temp_clone_dir.empty())
    {
        std::error_code cleanup_error;
        fs::remove_all(temp_clone_dir, cleanup_error); // best-effort
    }

    // --- прореживание portable/MemMang: только выбранная схема кучи --------
    const fs::path mem_mang_dir = dest_root / "portable" / "MemMang";
    std::error_code mem_mang_error;
    if (fs::exists(mem_mang_dir, mem_mang_error))
    {
        const std::string keep_file_name = config.os_config.heap_scheme + ".c";
        for (const fs::directory_entry& entry : fs::directory_iterator(mem_mang_dir, mem_mang_error))
        {
            if (mem_mang_error)
            {
                break;
            }
            if (entry.is_regular_file() && entry.path().extension() == ".c"
                && entry.path().filename().u8string() != keep_file_name)
            {
                std::error_code remove_error;
                fs::remove(entry.path(), remove_error); // best-effort — лишний heap_N.c не критичен, но мешает сборке дублем pvPortMalloc
            }
        }
    }

    return CodegenWriteResult{true, ""};
}
