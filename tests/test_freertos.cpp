//
// FreeRTOS: parser/freertos/freertos_parser.h + codegen/project/freertos_codegen.h.
// По образцу tests/test_parsers.cpp (loadProjectKnowledgeForTests и т.п.
// продублированы здесь намеренно — та копия статична в своей единице
// трансляции, а не экспортируется).
//
#include <gtest/gtest.h>
#include <freertos_parser.h>
#include <freertos_codegen.h>
#include <project_config.h>
#include <project_codegen.h>
#include <gpio_knowledge.h>
#include <uart_knowledge.h>
#include <timer16_knowledge.h>
#include <i2c_knowledge.h>
#include <timer32_knowledge.h>
#include <spi_knowledge.h>

#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

namespace
{
    ProjectKnowledge loadProjectKnowledgeForFreeRtosTests()
    {
        ProjectKnowledge knowledge;
        knowledge.gpio = *loadGpioKnowledge("../knowledge_base/gpio.json").config;
        knowledge.uart = *loadUartKnowledge("../knowledge_base/uart.json").config;
        knowledge.timer16 = *loadTimer16Knowledge("../knowledge_base/timer16.json").config;
        knowledge.i2c = *loadI2cKnowledge("../knowledge_base/i2c.json").config;
        knowledge.timer32 = *loadTimer32Knowledge("../knowledge_base/timer32.json").config;
        knowledge.spi = *loadSpiKnowledge("../knowledge_base/spi.json").config;
        return knowledge;
    }

    bool contains(const std::string& haystack, const std::string& needle)
    {
        return haystack.find(needle) != std::string::npos;
    }
}

// --- parser/freertos/freertos_parser.h --------------------------------------

TEST(FreeRtosParse, DisabledIsDefaultAndSkipsAllValidation)
{
    nlohmann::json node = nlohmann::json::object(); // enabled отсутствует вовсе
    ParseResult<FreeRtosConfig> result = parseFreeRtosBlock(node);

    ASSERT_TRUE(result.success) << result.error;
    EXPECT_FALSE(result.config->enabled);
}

TEST(FreeRtosParse, DisabledConfigIgnoresInvalidSiblingFields)
{
    // enabled=false — остальные поля (даже мусорные) не читаются вовсе, тот
    // же принцип, что у toolchain/hal_source_mode в project_config.cpp.
    nlohmann::json node = {{"enabled", false}, {"tasks", "не массив, а строка"}};
    ParseResult<FreeRtosConfig> result = parseFreeRtosBlock(node);

    ASSERT_TRUE(result.success) << result.error;
    EXPECT_FALSE(result.config->enabled);
}

TEST(FreeRtosParse, ParsesMinimalValidEnabledConfig)
{
    nlohmann::json node = {
        {"enabled", true},
        {"tasks", nlohmann::json::array({
            {{"name", "WorkerTask"}, {"priority", 1}, {"stack_size_words", 128}}
        })}
    };
    ParseResult<FreeRtosConfig> result = parseFreeRtosBlock(node);

    ASSERT_TRUE(result.success) << result.error;
    EXPECT_TRUE(result.config->enabled);
    ASSERT_EQ(result.config->tasks.size(), 1u);
    EXPECT_EQ(result.config->tasks[0].name, "WorkerTask");
    // Дефолты os_config — дословно из docs/сделать/mik32-freertos/src/FreeRTOSConfig.h.
    EXPECT_EQ(result.config->os_config.tick_rate_hz, 100);
    EXPECT_EQ(result.config->os_config.max_priorities, 7);
    EXPECT_EQ(result.config->os_config.heap_scheme, "heap_4");
}

TEST(FreeRtosParse, RejectsEnabledWithNoTasks)
{
    nlohmann::json node = {{"enabled", true}};
    ParseResult<FreeRtosConfig> result = parseFreeRtosBlock(node);

    EXPECT_FALSE(result.success);
}

TEST(FreeRtosParse, RejectsNonIdentifierName)
{
    nlohmann::json node = {
        {"enabled", true},
        {"tasks", nlohmann::json::array({
            {{"name", "1BadName"}, {"priority", 1}, {"stack_size_words", 128}}
        })}
    };
    ParseResult<FreeRtosConfig> result = parseFreeRtosBlock(node);

    EXPECT_FALSE(result.success);
}

TEST(FreeRtosParse, RejectsTaskPriorityOutOfRange)
{
    nlohmann::json node = {
        {"enabled", true},
        {"os_config", {{"max_priorities", 4}}},
        {"tasks", nlohmann::json::array({
            {{"name", "WorkerTask"}, {"priority", 4}, {"stack_size_words", 128}}
        })}
    };
    ParseResult<FreeRtosConfig> result = parseFreeRtosBlock(node);

    EXPECT_FALSE(result.success);
}

TEST(FreeRtosParse, RejectsDuplicateNameAcrossDifferentObjectTypes)
{
    // Задача и очередь с одинаковым именем — общий C-неймспейс всех
    // объектов ОС (grill-решение 2026-09-10), см. parser/freertos/freertos_parser.h.
    nlohmann::json node = {
        {"enabled", true},
        {"tasks", nlohmann::json::array({
            {{"name", "Shared"}, {"priority", 1}, {"stack_size_words", 128}}
        })},
        {"queues", nlohmann::json::array({
            {{"name", "Shared"}, {"length", 4}, {"item_size_bytes", 4}}
        })}
    };
    ParseResult<FreeRtosConfig> result = parseFreeRtosBlock(node);

    EXPECT_FALSE(result.success);
}

TEST(FreeRtosParse, CountingSemaphoreRequiresInitialCountWithinMaxCount)
{
    nlohmann::json node = {
        {"enabled", true},
        {"tasks", nlohmann::json::array({
            {{"name", "WorkerTask"}, {"priority", 1}, {"stack_size_words", 128}}
        })},
        {"semaphores", nlohmann::json::array({
            {{"name", "Pool"}, {"kind", "counting"}, {"max_count", 3}, {"initial_count", 5}}
        })}
    };
    ParseResult<FreeRtosConfig> result = parseFreeRtosBlock(node);

    EXPECT_FALSE(result.success);
}

TEST(FreeRtosParse, AcceptsFullObjectSet)
{
    nlohmann::json node = {
        {"enabled", true},
        {"tasks", nlohmann::json::array({
            {{"name", "WorkerTask"}, {"priority", 1}, {"stack_size_words", 128}}
        })},
        {"queues", nlohmann::json::array({
            {{"name", "PollQueue"}, {"length", 4}, {"item_size_bytes", 4}}
        })},
        {"semaphores", nlohmann::json::array({
            {{"name", "BinSem"}, {"kind", "binary"}},
            {{"name", "CountSem"}, {"kind", "counting"}, {"max_count", 3}, {"initial_count", 0}}
        })},
        {"mutexes", nlohmann::json::array({
            {{"name", "NormalMutex"}, {"kind", "normal"}},
            {{"name", "RecMutex"}, {"kind", "recursive"}}
        })},
        {"timers", nlohmann::json::array({
            {{"name", "HeartbeatTimer"}, {"kind", "periodic"}, {"period_ms", 1000}}
        })}
    };
    ParseResult<FreeRtosConfig> result = parseFreeRtosBlock(node);

    ASSERT_TRUE(result.success) << result.error;
    EXPECT_EQ(result.config->queues.size(), 1u);
    EXPECT_EQ(result.config->semaphores.size(), 2u);
    EXPECT_EQ(result.config->mutexes.size(), 2u);
    EXPECT_EQ(result.config->timers.size(), 1u);
}

// --- codegen/project/project_config.h — freertos как поле уровня проекта ---

TEST(ProjectConfigParse, AllowsFreeRtosOnlyProjectWithNoPeripherals)
{
    ParseResult<ProjectConfig> result = parseProjectConfig("fixtures/freertos_only_no_peripherals.json");

    ASSERT_TRUE(result.success) << result.error;
    EXPECT_TRUE(result.config->freertos.enabled);
    EXPECT_TRUE(result.config->gpio_blocks.empty());
}

TEST(ProjectConfigParse, KeepsFreeRtosDisabledWhenFieldAbsent)
{
    // Существующие фикстуры без поля "freertos" (подавляющее большинство)
    // обязаны продолжать парситься с freertos.enabled=false по умолчанию.
    ParseResult<ProjectConfig> result = parseProjectConfig("fixtures/gpio_valid.json");

    ASSERT_TRUE(result.success) << result.error;
    EXPECT_FALSE(result.config->freertos.enabled);
}

// --- codegen/project/project_codegen.h — main.c в FreeRTOS-режиме ----------

TEST(ProjectCodegen, FreeRtosGeneratesSchedulerMainWithoutClassicWhileLoop)
{
    ParseResult<ProjectConfig> config_result = parseProjectConfig("fixtures/freertos_only_no_peripherals.json");
    ASSERT_TRUE(config_result.success) << config_result.error;

    ParseResult<GeneratedProjectFirmware> firmware_result =
        generateProjectFirmware(*config_result.config, loadProjectKnowledgeForFreeRtosTests());
    ASSERT_TRUE(firmware_result.success) << firmware_result.error;

    const std::string& main_c = firmware_result.config->main_c;
    EXPECT_TRUE(contains(main_c, "vTaskStartScheduler();"));
    EXPECT_TRUE(contains(main_c, "xTaskCreate(WorkerTask_Task,"));
    // Классический demo-цикл (USER CODE BEGIN Loop) — только у не-FreeRTOS
    // ветки, см. project_codegen.cpp.
    EXPECT_FALSE(contains(main_c, "USER CODE BEGIN Loop"));
    EXPECT_FALSE(firmware_result.config->freertos_config_h.empty());
}

TEST(ProjectCodegen, FreeRtosAutoWiresGpioIrqHandlerIntoInterruptHandler)
{
    ParseResult<ProjectConfig> config_result = parseProjectConfig("fixtures/freertos_button_semaphore.json");
    ASSERT_TRUE(config_result.success) << config_result.error;

    ParseResult<GeneratedProjectFirmware> firmware_result =
        generateProjectFirmware(*config_result.config, loadProjectKnowledgeForFreeRtosTests());
    ASSERT_TRUE(firmware_result.success) << firmware_result.error;

    const std::string& main_c = firmware_result.config->main_c;
    EXPECT_TRUE(contains(main_c, "raw_trap_handler()"));
    EXPECT_TRUE(contains(main_c, "freertos_risc_v_application_interrupt_handler(void)"));
    EXPECT_TRUE(contains(main_c, "GPIO_0_IRQHandler_Template();"));
    EXPECT_TRUE(contains(main_c, "ButtonSemaphore = xSemaphoreCreateBinary();"));
}

TEST(ProjectCodegen, FreeRtosConfigHReflectsCuratedOsConfig)
{
    ParseResult<ProjectConfig> config_result = parseProjectConfig("fixtures/freertos_button_semaphore.json");
    ASSERT_TRUE(config_result.success) << config_result.error;

    ParseResult<GeneratedProjectFirmware> firmware_result =
        generateProjectFirmware(*config_result.config, loadProjectKnowledgeForFreeRtosTests());
    ASSERT_TRUE(firmware_result.success) << firmware_result.error;

    const std::string& config_h = firmware_result.config->freertos_config_h;
    EXPECT_TRUE(contains(config_h, "configTICK_RATE_HZ                      ( ( TickType_t ) 100 )"));
    EXPECT_TRUE(contains(config_h, "configMAX_PRIORITIES                    ( 7 )"));
    // Ни одного freertos.timers в фикстуре — configUSE_TIMERS должен
    // выводиться в 0, а не фиксироваться на 1 (см. freertos_codegen.cpp).
    EXPECT_TRUE(contains(config_h, "configUSE_TIMERS                        0"));
}

TEST(ProjectCodegen, NonFreeRtosProjectMainUnaffectedByFreeRtosBranch)
{
    // Регрессия: обычный (не-FreeRTOS) проект обязан остаться ПОБАЙТОВО тем
    // же, что и до этой сессии — см. golden-тесты выше в tests/test_parsers.cpp
    // (MatchesGoldenFileForSinglePinGpioOutput и т.п., не дублируются здесь).
    ParseResult<ProjectConfig> config_result = parseProjectConfig("fixtures/gpio_valid.json");
    ASSERT_TRUE(config_result.success) << config_result.error;
    ASSERT_FALSE(config_result.config->freertos.enabled);

    ParseResult<GeneratedProjectFirmware> firmware_result =
        generateProjectFirmware(*config_result.config, loadProjectKnowledgeForFreeRtosTests());
    ASSERT_TRUE(firmware_result.success) << firmware_result.error;

    EXPECT_TRUE(firmware_result.config->freertos_config_h.empty());
    EXPECT_TRUE(contains(firmware_result.config->main_c, "while (1)"));
    EXPECT_FALSE(contains(firmware_result.config->main_c, "vTaskStartScheduler"));
}

// --- writeGeneratedProject: FreeRTOSConfig.h + Kernel-снэпшот на диске ------

TEST(ProjectCodegenWriteTransactional, FreeRtosWritesConfigHAndPrunedKernelSnapshot)
{
    ParseResult<ProjectConfig> config_result = parseProjectConfig("fixtures/freertos_button_semaphore.json");
    ASSERT_TRUE(config_result.success) << config_result.error;

    ParseResult<GeneratedProjectFirmware> firmware_result =
        generateProjectFirmware(*config_result.config, loadProjectKnowledgeForFreeRtosTests());
    ASSERT_TRUE(firmware_result.success) << firmware_result.error;

    const fs::path output_dir = fs::path("../generated_projects") / "test_freertos_write_transactional_tmp";
    std::error_code cleanup_error;
    fs::remove_all(output_dir, cleanup_error);

    CodegenWriteResult write_result = writeGeneratedProject(
        *firmware_result.config, "../templates/pio_skeleton_freertos", output_dir.u8string(),
        config_result.config->freertos, "../templates/freertos_kernel_snapshot");
    ASSERT_TRUE(write_result.success) << write_result.error;

    EXPECT_TRUE(fs::exists(output_dir / "src" / "FreeRTOSConfig.h"));
    EXPECT_TRUE(fs::exists(output_dir / "src" / "FreeRTOS-Kernel" / "tasks.c"));
    EXPECT_TRUE(fs::exists(output_dir / "src" / "FreeRTOS-Kernel" / "portable" / "MemMang" / "heap_4.c"));
    // Только выбранная схема кучи — остальные четыре не должны попасть на
    // диск (freertos_codegen.cpp::writeFreeRtosKernelSource).
    EXPECT_FALSE(fs::exists(output_dir / "src" / "FreeRTOS-Kernel" / "portable" / "MemMang" / "heap_1.c"));
    EXPECT_FALSE(fs::exists(output_dir / "src" / "FreeRTOS-Kernel" / "portable" / "MemMang" / "heap_2.c"));
    EXPECT_TRUE(fs::exists(output_dir / "src" / "FreeRTOS-Kernel" / "portable" / "GCC" / "RISC-V"
        / "chip_specific_extensions" / "SCR1" / "freertos_risc_v_chip_specific_extensions.h"));

    fs::remove_all(output_dir, cleanup_error);
}
