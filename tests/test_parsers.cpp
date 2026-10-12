//
// Created by antihype on 04.08.2026.
//

#include <gtest/gtest.h>
#include <gpio/gpio_parser.h>
#include <gpio_knowledge.h>
#include <usart_parser.h>
#include <uart_knowledge.h>
#include <user_code_markers.h>
#include <cross_module_validator.h>
#include <timer16_knowledge.h>
#include <i2c_knowledge.h>
#include <timer32_parser.h>
#include <timer32_knowledge.h>
#include <spi_parser.h>
#include <spi_knowledge.h>
#include <project_config.h>
#include <project_codegen.h>
#include <cmake_project_codegen.h>

#include "parse_result.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

TEST(GpioParse, ParsesValidConfig)
{
    // Given
    std::string path = "fixtures/gpio_valid.json";

    // When

    ParseResult<GpioConfig> result = parseGpioConfig(path);

    // Then

    EXPECT_TRUE(result.success);


}

TEST(GpioParse, RejectsDuplicatePins)
{
    ParseResult<GpioConfig> result = parseGpioConfig("fixtures/gpio_duplicate_pins.json");

    EXPECT_FALSE(result.success);
}

TEST(GpioParse, RejectsEmptyPins)
{
    ParseResult<GpioConfig> result = parseGpioConfig("fixtures/gpio_empty_pins.json");

    EXPECT_FALSE(result.success);
}

TEST(GpioParse, RejectsInvalidMode)
{
    ParseResult<GpioConfig> result = parseGpioConfig("fixtures/gpio_invalid_mode.json");

    EXPECT_FALSE(result.success);
}

TEST(GpioParse, RejectsOutOfRangePin)
{
    ParseResult<GpioConfig> result = parseGpioConfig("fixtures/gpio_out_of_range_pin.json");

    EXPECT_FALSE(result.success);
}

TEST(GpioParse, RejectsWrongInstance)
{
    ParseResult<GpioConfig> result = parseGpioConfig("fixtures/gpio_wrong_instance.json");

    EXPECT_FALSE(result.success);
}

// GPIO_2 физически имеет только 8 пинов (0-7), в отличие от GPIO_0/GPIO_1
// (по 16) — датащит, Таблица 193 «Назначение выводов», с.320. Пин 8 valid
// для instance 0/1, но не для instance 2.
TEST(GpioParse, RejectsPinOutOfRangeForInstanceTwo)
{
    ParseResult<GpioConfig> result = parseGpioConfig("fixtures/gpio_out_of_range_pin_for_instance2.json");

    EXPECT_FALSE(result.success);
}

namespace
{
    std::string readFileToString(const std::string& path)
    {
        std::ifstream file(path);
        std::ostringstream buffer;
        buffer << file.rdbuf();
        return buffer.str();
    }

    // Все шесть баз знаний сразу — их требует сборщик проекта независимо
    // от того, какая периферия есть в конкретной фикстуре.
    ProjectKnowledge loadProjectKnowledgeForTests()
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

// Golden-file тест кодогенерации GPIO: сравнивает mik32_init.c, собранный
// сборщиком проекта для минимальной фикстуры (один пин на output), с заранее
// зафиксированным эталоном. Та же фикстура используется как сценарий для
// обязательной аппаратной проверки на 1T Rex Board (см. CLAUDE.md,
// "Hardware validation gate") — GPIO_2 pin 6 совпадает с пином светодиода.
//
// Тест переведён на codegen/project/ вместе с удалением однопериферийных
// генераторов: сборщик проекта — теперь единственный путь генерации, и
// эталон должен фиксировать именно его вывод.
TEST(ProjectCodegen, MatchesGoldenFileForSinglePinGpioOutput)
{
    ParseResult<ProjectConfig> config_result = parseProjectConfig("fixtures/gpio_codegen_output.json");
    ASSERT_TRUE(config_result.success) << config_result.error;

    ParseResult<GeneratedProjectFirmware> firmware_result =
        generateProjectFirmware(*config_result.config, loadProjectKnowledgeForTests());
    ASSERT_TRUE(firmware_result.success) << firmware_result.error;

    const std::string expected = readFileToString("fixtures/expected_gpio_init.c");

    EXPECT_EQ(firmware_result.config->mik32_init_c, expected);
}


TEST(UartParse, ParsesValidConfig)
{
    ParseResult<UartConfig> result = parseUsartConfig("fixtures/uart_valid.json");

    EXPECT_TRUE(result.success);
}

TEST(UartParse, RejectsInvalidDataBits)
{
    ParseResult<UartConfig> result = parseUsartConfig("fixtures/uart_invalid_data_bits.json");

    EXPECT_FALSE(result.success);
}

TEST(UartParse, RejectsInvalidParity)
{
    ParseResult<UartConfig> result = parseUsartConfig("fixtures/uart_invalid_parity.json");

    EXPECT_FALSE(result.success);
}

TEST(UartParse, RejectsWrongInstance)
{
    ParseResult<UartConfig> result = parseUsartConfig("fixtures/uart_wrong_instance.json");

    EXPECT_FALSE(result.success);
}

// OVERNIGHT_FIELD_REFERENCE.md — новые необязательные поля этой сессии.
TEST(UartParse, ParsesNewOptionalFieldsTogether)
{
    ParseResult<UartConfig> result = parseUsartConfig("fixtures/uart_new_fields_valid.json");

    ASSERT_TRUE(result.success) << result.error;
    EXPECT_EQ(result.config->bit_direction, "msb_first");
    EXPECT_EQ(result.config->channel_mode, "half_duplex");
    EXPECT_TRUE(result.config->tx_inversion);
    EXPECT_TRUE(result.config->rx_inversion);
    EXPECT_TRUE(result.config->swap);
    EXPECT_EQ(result.config->rts_mode, "modem");
    ASSERT_TRUE(result.config->has_modem_signals);
    EXPECT_TRUE(result.config->modem_signals.ri);
    EXPECT_TRUE(result.config->modem_signals.dsr);
    EXPECT_TRUE(result.config->modem_signals.dtr);
    EXPECT_TRUE(result.config->modem_signals.ddis);
    EXPECT_TRUE(result.config->modem_signals.dcd);
}

TEST(UartParse, AcceptsSynchronousModeOnInstance0)
{
    ParseResult<UartConfig> result = parseUsartConfig("fixtures/uart_sync_mode_instance0_valid.json");

    ASSERT_TRUE(result.success) << result.error;
    EXPECT_EQ(result.config->mode, "synchronous");
    EXPECT_EQ(result.config->xck_mode, "mode2");
    EXPECT_TRUE(result.config->last_byte_clock);
}

// Errata MIK32V2 v1.0.1 §10.1 — паразитный сигнал CK на PORT2_6 без обхода.
TEST(UartParse, RejectsSynchronousModeOnInstance1DueToErrata)
{
    ParseResult<UartConfig> result = parseUsartConfig("fixtures/uart_sync_mode_instance1_rejected.json");

    EXPECT_FALSE(result.success);
    EXPECT_NE(result.error.find("Errata"), std::string::npos);
}

TEST(UartParse, RejectsXckModeWithoutSynchronousMode)
{
    ParseResult<UartConfig> result = parseUsartConfig("fixtures/uart_xck_mode_without_sync.json");

    EXPECT_FALSE(result.success);
}

TEST(UartParse, RejectsRtsModeWithoutFlowControl)
{
    ParseResult<UartConfig> result = parseUsartConfig("fixtures/uart_rts_mode_without_flow_control.json");

    EXPECT_FALSE(result.success);
}

// OVERNIGHT_FIELD_REFERENCE.md — Timer16: пересчёт частоты/скважности.
TEST(Timer16Parse, ComputesPrescalerArrCmpFromFrequencyAndDuty)
{
    ParseResult<Timer16Config> result = parseTimer16Config("fixtures/timer16_frequency_duty_valid.json");

    ASSERT_TRUE(result.success) << result.error;
    EXPECT_TRUE(result.config->has_frequency_duty);
    // 32МГц / prescaler=1 / 1000Гц -> период 32000 тактов -> ARR=31999.
    EXPECT_EQ(result.config->prescaler, 1);
    EXPECT_EQ(result.config->arr, 31999);
    EXPECT_EQ(result.config->cmp, 16000);
}

TEST(Timer16Parse, RejectsFrequencyWithoutDutyPercent)
{
    ParseResult<Timer16Config> result = parseTimer16Config("fixtures/timer16_frequency_without_duty.json");

    EXPECT_FALSE(result.success);
}

TEST(Timer16Parse, RejectsFrequencyTogetherWithRawRegisters)
{
    ParseResult<Timer16Config> result = parseTimer16Config("fixtures/timer16_frequency_and_raw_registers_conflict.json");

    EXPECT_FALSE(result.success);
}

TEST(Timer16Parse, RejectsFrequencyUnreachableByAnyPrescaler)
{
    ParseResult<Timer16Config> result = parseTimer16Config("fixtures/timer16_frequency_too_low_unreachable.json");

    EXPECT_FALSE(result.success);
}

TEST(UartParse, RejectsInvalidBaudRate)
{
    ParseResult<UartConfig> result = parseUsartConfig("fixtures/uart_invalid_baud_rate.json");

    EXPECT_FALSE(result.success);
}

// Golden-file тест кодогенерации UART: instance 1 совпадает с TXD/RXD,
// промаркированными на 1T Rex Board (см. docs/implementations/uart.md).
// Переведён на codegen/project/ по той же причине, что и GPIO-эталон выше.
TEST(ProjectCodegen, MatchesGoldenFileForAsyncUartNoFlowControl)
{
    ParseResult<ProjectConfig> config_result = parseProjectConfig("fixtures/uart_codegen_output.json");
    ASSERT_TRUE(config_result.success) << config_result.error;

    ParseResult<GeneratedProjectFirmware> firmware_result =
        generateProjectFirmware(*config_result.config, loadProjectKnowledgeForTests());
    ASSERT_TRUE(firmware_result.success) << firmware_result.error;

    const std::string expected = readFileToString("fixtures/expected_uart_init.c");

    EXPECT_EQ(firmware_result.config->mik32_init_c, expected);
}


// §3.1.7.3 ТЗ: содержимое между /* USER CODE BEGIN <ID> */ и
// /* USER CODE END <ID> */ должно сохраняться при повторной генерации.
TEST(UserCodeMarkers, PreservesExistingSectionAndUsesDefaultForNewOne)
{
    const std::string existing =
        "#include \"foo.h\"\n"
        "/* USER CODE BEGIN Includes */\n"
        "#include \"my_sensor.h\"\n"
        "/* USER CODE END Includes */\n"
        "int main() {\n"
        "    /* USER CODE BEGIN Loop */\n"
        "    my_sensor_poll();\n"
        "    /* USER CODE END Loop */\n"
        "}\n";

    const std::string freshly_generated =
        "#include \"foo.h\"\n"
        "#include \"bar.h\"\n"
        "/* USER CODE BEGIN Includes */\n"
        "/* USER CODE END Includes */\n"
        "int main() {\n"
        "    /* USER CODE BEGIN Loop */\n"
        "    /* USER CODE END Loop */\n"
        "}\n";

    const std::string merged = mergeUserCodeSections(freshly_generated, existing);

    // Новый #include "bar.h" из генератора остаётся (это не пользовательский код).
    EXPECT_NE(merged.find("#include \"bar.h\""), std::string::npos);
    // Пользовательский include и вызов сохранились, несмотря на то что в
    // свежесгенерированном тексте эти секции были пустыми.
    EXPECT_NE(merged.find("#include \"my_sensor.h\""), std::string::npos);
    EXPECT_NE(merged.find("my_sensor_poll();"), std::string::npos);
}

TEST(UserCodeMarkers, NoExistingFileMeansNoMerge)
{
    const std::string freshly_generated =
        "/* USER CODE BEGIN Loop */\n"
        "/* USER CODE END Loop */\n";

    // Пустая существующая строка = "файла ещё не было" — merge не должен падать
    // и должен просто вернуть свежесгенерированный текст как есть.
    const std::string merged = mergeUserCodeSections(freshly_generated, "");

    EXPECT_EQ(merged, freshly_generated);
}


// ТЗ §3.1.6.2: конфликт назначений выводов между GPIO и UART. UART0 TX
// фиксирован на GPIO_0 pin 6 (knowledge_base/uart.json,
// instance_fixed_pins) — конфликтует с явным gpio-блоком, тоже
// заявляющим GPIO_0 pin 6.
TEST(CrossModuleValidator, DetectsGpioUartPinConflict)
{
    ParseResult<UartKnowledge> knowledge_result = loadUartKnowledge("../knowledge_base/uart.json");
    ASSERT_TRUE(knowledge_result.success) << knowledge_result.error;
    ParseResult<Timer16Knowledge> timer16_knowledge_result = loadTimer16Knowledge("../knowledge_base/timer16.json");
    ASSERT_TRUE(timer16_knowledge_result.success) << timer16_knowledge_result.error;
    ParseResult<I2cKnowledge> i2c_knowledge_result = loadI2cKnowledge("../knowledge_base/i2c.json");
    ASSERT_TRUE(i2c_knowledge_result.success) << i2c_knowledge_result.error;
    ParseResult<Timer32Knowledge> timer32_knowledge_result = loadTimer32Knowledge("../knowledge_base/timer32.json");
    ASSERT_TRUE(timer32_knowledge_result.success) << timer32_knowledge_result.error;
    ParseResult<SpiKnowledge> spi_knowledge_result = loadSpiKnowledge("../knowledge_base/spi.json");
    ASSERT_TRUE(spi_knowledge_result.success) << spi_knowledge_result.error;

    CrossModuleValidationResult result = validatePinConflicts(
        "fixtures/cross_gpio_uart_conflict.json", *knowledge_result.config, *timer16_knowledge_result.config,
        *i2c_knowledge_result.config, *timer32_knowledge_result.config, *spi_knowledge_result.config);

    ASSERT_TRUE(result.success) << result.error;
    ASSERT_EQ(result.conflicts.size(), 1u);
    EXPECT_EQ(result.conflicts[0].gpio_instance, 0);
    EXPECT_EQ(result.conflicts[0].pin, 6);
    EXPECT_EQ(result.conflicts[0].conflicting_owners.size(), 2u);
}

TEST(CrossModuleValidator, NoConflictWhenPinsDontOverlap)
{
    ParseResult<UartKnowledge> knowledge_result = loadUartKnowledge("../knowledge_base/uart.json");
    ASSERT_TRUE(knowledge_result.success) << knowledge_result.error;
    ParseResult<Timer16Knowledge> timer16_knowledge_result = loadTimer16Knowledge("../knowledge_base/timer16.json");
    ASSERT_TRUE(timer16_knowledge_result.success) << timer16_knowledge_result.error;
    ParseResult<I2cKnowledge> i2c_knowledge_result = loadI2cKnowledge("../knowledge_base/i2c.json");
    ASSERT_TRUE(i2c_knowledge_result.success) << i2c_knowledge_result.error;
    ParseResult<Timer32Knowledge> timer32_knowledge_result = loadTimer32Knowledge("../knowledge_base/timer32.json");
    ASSERT_TRUE(timer32_knowledge_result.success) << timer32_knowledge_result.error;
    ParseResult<SpiKnowledge> spi_knowledge_result = loadSpiKnowledge("../knowledge_base/spi.json");
    ASSERT_TRUE(spi_knowledge_result.success) << spi_knowledge_result.error;

    CrossModuleValidationResult result = validatePinConflicts(
        "fixtures/cross_gpio_uart_no_conflict.json", *knowledge_result.config, *timer16_knowledge_result.config,
        *i2c_knowledge_result.config, *timer32_knowledge_result.config, *spi_knowledge_result.config);

    ASSERT_TRUE(result.success) << result.error;
    EXPECT_TRUE(result.conflicts.empty());
}


// ============================================================================
// Межмодульная (общая) кодогенерация — codegen/project/.
// Фикстура project_all_peripherals.json содержит все четыре реализованные
// периферии сразу, причём GPIO_2 описан ДВУМЯ блоками (выход + вход): это
// главный сценарий, который старая схема «один генератор = три файла»
// выразить не могла вообще.
// Пины подобраны бесконфликтными: UART_0 занимает GPIO_0/5,6, I2C_0 —
// GPIO_0/9,10, Timer16_0 — GPIO_0/7, а gpio-блоки сидят на GPIO_2.
// ============================================================================

TEST(ProjectConfigParse, ParsesAllPeripheralTypesAndKeepsBothGpioBlocks)
{
    ParseResult<ProjectConfig> result = parseProjectConfig("fixtures/project_all_peripherals.json");
    ASSERT_TRUE(result.success) << result.error;

    EXPECT_EQ(result.config->mcu, "MIK32");
    // Оба gpio-блока сохранены как отдельные записи — слияние в одну
    // init-функцию происходит позже, в кодогенерации, а не при разборе.
    ASSERT_EQ(result.config->gpio_blocks.size(), 2u);
    EXPECT_EQ(result.config->gpio_blocks[0].mode, "output");
    EXPECT_EQ(result.config->gpio_blocks[1].mode, "input");
    ASSERT_EQ(result.config->uart_blocks.size(), 1u);
    ASSERT_EQ(result.config->i2c_blocks.size(), 1u);
    ASSERT_EQ(result.config->timer16_blocks.size(), 1u);
}

TEST(ProjectConfigParse, RejectsUnsupportedPeripheralType)
{
    ParseResult<ProjectConfig> result = parseProjectConfig("fixtures/project_unsupported_type.json");

    EXPECT_FALSE(result.success);
    EXPECT_TRUE(contains(result.error, "не поддерживается")) << result.error;
}

// Два блока uart на один instance — нарушение аппаратного ограничения
// (ТЗ §3.1.6.2): один физический UART нельзя настроить дважды разными
// параметрами. Для gpio такое же повторение, наоборот, разрешено.
TEST(ProjectConfigParse, RejectsDuplicateUartInstance)
{
    ParseResult<ProjectConfig> result = parseProjectConfig("fixtures/project_duplicate_uart.json");

    EXPECT_FALSE(result.success);
    EXPECT_TRUE(contains(result.error, "уже настроен")) << result.error;
}

// Ошибка отдельного блока должна называть его номер в массиве peripherals —
// при N блоках сообщение «Поле "instance" вне диапазона» без номера
// бесполезно.
TEST(ProjectConfigParse, ErrorMentionsBlockNumber)
{
    ParseResult<ProjectConfig> result = parseProjectConfig("fixtures/project_bad_second_block.json");

    EXPECT_FALSE(result.success);
    EXPECT_TRUE(contains(result.error, "Блок #2")) << result.error;
}

// Ключевая проверка всего модуля: ОДИН mik32_init.h с прототипами всех
// четырёх периферий и ОДИН main.c, вызывающий их все. Раньше каждый
// генератор выпускал свой mik32_init.h, и второй затирал первый.
TEST(ProjectCodegen, MergesAllPeripheralsIntoSingleInitHeaderAndMain)
{
    ParseResult<ProjectConfig> config_result = parseProjectConfig("fixtures/project_all_peripherals.json");
    ASSERT_TRUE(config_result.success) << config_result.error;

    ParseResult<GeneratedProjectFirmware> firmware_result =
        generateProjectFirmware(*config_result.config, loadProjectKnowledgeForTests());
    ASSERT_TRUE(firmware_result.success) << firmware_result.error;

    const std::string& init_h = firmware_result.config->mik32_init_h;
    EXPECT_TRUE(contains(init_h, "void GPIO_2_Init(void);"));
    EXPECT_TRUE(contains(init_h, "void UART_0_Init(void);"));
    EXPECT_TRUE(contains(init_h, "void I2C_0_Init(void);"));
    EXPECT_TRUE(contains(init_h, "void I2C_0_Reset(void);"));
    EXPECT_TRUE(contains(init_h, "void TIMER16_0_Init(void);"));

    // mik32_init.h сам подключает HAL-заголовки использованной периферии —
    // иначе код пользователя в main.c (USER CODE) не видит HAL-API, и
    // HAL_GPIO_TogglePin(GPIO_2, GPIO_PIN_6) не компилируется.
    EXPECT_TRUE(contains(init_h, "#include \"mik32_hal.h\""));
    EXPECT_TRUE(contains(init_h, "#include \"mik32_hal_gpio.h\""));
    EXPECT_TRUE(contains(init_h, "#include \"mik32_hal_usart.h\""));
    EXPECT_TRUE(contains(init_h, "#include \"mik32_hal_i2c.h\""));
    EXPECT_TRUE(contains(init_h, "#include \"mik32_hal_timer16.h\""));

    // Порядок вызовов задан kInitOrder* (contribution_utils.h):
    // GPIO -> UART -> I2C -> Timer16.
    const std::string& main_c = firmware_result.config->main_c;
    const size_t gpio_at = main_c.find("    GPIO_2_Init();");
    const size_t uart_at = main_c.find("    UART_0_Init();");
    const size_t i2c_at = main_c.find("    I2C_0_Init();");
    const size_t timer_at = main_c.find("    TIMER16_0_Init();");
    ASSERT_NE(timer_at, std::string::npos);
    EXPECT_LT(gpio_at, uart_at);
    EXPECT_LT(uart_at, i2c_at);
    EXPECT_LT(i2c_at, timer_at);

    // Хендлы именуются по instance — иначе два блока одного типа дали бы
    // переопределение символа на линковке.
    const std::string& init_c = firmware_result.config->mik32_init_c;
    EXPECT_TRUE(contains(init_c, "USART_HandleTypeDef husart_0;"));
    EXPECT_TRUE(contains(init_c, "I2C_HandleTypeDef hi2c_0;"));
    EXPECT_TRUE(contains(init_c, "Timer16_HandleTypeDef htimer16_0;"));

    // Заголовки HAL не дублируются, даже если их запросили несколько
    // модулей (mik32_hal_pcc.h нужен всем четырём).
    size_t pcc_count = 0;
    for (size_t at = init_c.find("#include \"mik32_hal_pcc.h\"");
         at != std::string::npos;
         at = init_c.find("#include \"mik32_hal_pcc.h\"", at + 1))
    {
        ++pcc_count;
    }
    EXPECT_EQ(pcc_count, 1u);
}

// Несколько gpio-блоков с одним instance сливаются в одну функцию с
// несколькими HAL_GPIO_Init — обоснование в codegen/project/gpio_contribution.h
// (HAL_GPIO_Init делает read-modify-write по каждому пину отдельно).
TEST(ProjectCodegen, MergesGpioBlocksOfSameInstanceIntoOneFunction)
{
    ParseResult<ProjectConfig> config_result = parseProjectConfig("fixtures/project_all_peripherals.json");
    ASSERT_TRUE(config_result.success) << config_result.error;

    ParseResult<GeneratedProjectFirmware> firmware_result =
        generateProjectFirmware(*config_result.config, loadProjectKnowledgeForTests());
    ASSERT_TRUE(firmware_result.success) << firmware_result.error;

    const std::string& init_c = firmware_result.config->mik32_init_c;

    size_t init_function_count = 0;
    for (size_t at = init_c.find("void GPIO_2_Init(void)");
         at != std::string::npos;
         at = init_c.find("void GPIO_2_Init(void)", at + 1))
    {
        ++init_function_count;
    }
    EXPECT_EQ(init_function_count, 1u);

    size_t hal_call_count = 0;
    for (size_t at = init_c.find("HAL_GPIO_Init(GPIO_2");
         at != std::string::npos;
         at = init_c.find("HAL_GPIO_Init(GPIO_2", at + 1))
    {
        ++hal_call_count;
    }
    EXPECT_EQ(hal_call_count, 2u);

    // Тактирование порта включается один раз на функцию, а не на блок.
    size_t clock_count = 0;
    for (size_t at = init_c.find("__HAL_PCC_GPIO_2_CLK_ENABLE()");
         at != std::string::npos;
         at = init_c.find("__HAL_PCC_GPIO_2_CLK_ENABLE()", at + 1))
    {
        ++clock_count;
    }
    EXPECT_EQ(clock_count, 1u);
}

// Каждая demo-логика получает собственную область видимости — иначе
// одноимённые локальные переменные двух модулей столкнулись бы.
TEST(ProjectCodegen, WrapsEachDemoLoopBodyInItsOwnScope)
{
    ParseResult<ProjectConfig> config_result = parseProjectConfig("fixtures/project_all_peripherals.json");
    ASSERT_TRUE(config_result.success) << config_result.error;

    ParseResult<GeneratedProjectFirmware> firmware_result =
        generateProjectFirmware(*config_result.config, loadProjectKnowledgeForTests());
    ASSERT_TRUE(firmware_result.success) << firmware_result.error;

    const std::string& main_c = firmware_result.config->main_c;
    EXPECT_TRUE(contains(main_c, "        /* GPIO_2 */\n        {\n"));
    EXPECT_TRUE(contains(main_c, "        /* UART_0 */\n        {\n"));
    EXPECT_TRUE(contains(main_c, "        /* I2C_0 */\n        {\n"));
    // Задержка одна на весь цикл, а не по одной на модуль.
    size_t delay_count = 0;
    for (size_t at = main_c.find("        HAL_DelayMs(500);");
         at != std::string::npos;
         at = main_c.find("        HAL_DelayMs(500);", at + 1))
    {
        ++delay_count;
    }
    EXPECT_EQ(delay_count, 1u);
}

// emit_demo_loops=false — «чистая» генерация: только инициализация, пустое
// тело while(1) под маркерами ТЗ §3.1.7.3.
TEST(ProjectCodegen, EmitsEmptyLoopWhenDemoDisabled)
{
    ParseResult<ProjectConfig> config_result = parseProjectConfig("fixtures/project_all_peripherals.json");
    ASSERT_TRUE(config_result.success) << config_result.error;

    ParseResult<GeneratedProjectFirmware> firmware_result =
        generateProjectFirmware(*config_result.config, loadProjectKnowledgeForTests(), false);
    ASSERT_TRUE(firmware_result.success) << firmware_result.error;

    const std::string& main_c = firmware_result.config->main_c;
    EXPECT_TRUE(contains(main_c, "        /* USER CODE BEGIN Loop */\n        /* USER CODE END Loop */\n"));
    EXPECT_FALSE(contains(main_c, "HAL_GPIO_TogglePin"));
    EXPECT_FALSE(contains(main_c, "xprintf"));
    // Инициализация при этом остаётся полной.
    EXPECT_TRUE(contains(main_c, "    I2C_0_Init();"));
}

// Сканер I2C без UART печатать результат не может — кодоген обязан
// отказаться, а не выпустить немой сканер.
TEST(ProjectCodegen, RejectsI2cScannerWithoutConsoleUart)
{
    ParseResult<ProjectConfig> config_result = parseProjectConfig("fixtures/project_i2c_scanner_no_uart.json");
    ASSERT_TRUE(config_result.success) << config_result.error;

    ParseResult<GeneratedProjectFirmware> firmware_result =
        generateProjectFirmware(*config_result.config, loadProjectKnowledgeForTests());

    EXPECT_FALSE(firmware_result.success);
    EXPECT_TRUE(contains(firmware_result.error, "scanner")) << firmware_result.error;
}

// --- Timer32 (ТЗ §3.1.5) ----------------------------------------------------

TEST(Timer32Parse, ParsesValidBaseCounterConfig)
{
    ParseResult<Timer32Config> result = parseTimer32Config("fixtures/timer32_base_counter.json");

    ASSERT_TRUE(result.success) << result.error;
    EXPECT_EQ(result.config->instance, 0);
    EXPECT_TRUE(result.config->channels.empty());
}

TEST(Timer32Parse, ParsesValidChannelConfig)
{
    ParseResult<Timer32Config> result = parseTimer32Config("fixtures/timer32_channel_pwm.json");

    ASSERT_TRUE(result.success) << result.error;
    ASSERT_EQ(result.config->channels.size(), 1u);
    EXPECT_EQ(result.config->channels[0].mode, "pwm");
    EXPECT_EQ(result.config->channels[0].ocr, 250u);
}

// ТЗ §3.1.5: «Timer32_0 – базовый счётчик без режимов ШИМ, захвата и
// сравнения» — подтверждено HAL (все Channel_*-функции возвращают HAL_ERROR
// для TIMER32_0).
TEST(Timer32Parse, RejectsChannelsOnInstanceZero)
{
    ParseResult<Timer32Config> result = parseTimer32Config("fixtures/timer32_channels_on_instance_zero.json");

    EXPECT_FALSE(result.success);
}

TEST(Timer32Parse, RejectsMissingOcrForPwm)
{
    ParseResult<Timer32Config> result = parseTimer32Config("fixtures/timer32_pwm_missing_ocr.json");

    EXPECT_FALSE(result.success);
}

TEST(Timer32Parse, RejectsMissingCaptureEdgeForCapture)
{
    ParseResult<Timer32Config> result = parseTimer32Config("fixtures/timer32_capture_missing_edge.json");

    EXPECT_FALSE(result.success);
}

TEST(Timer32Parse, RejectsDuplicateChannelIndex)
{
    ParseResult<Timer32Config> result = parseTimer32Config("fixtures/timer32_duplicate_channel_index.json");

    EXPECT_FALSE(result.success);
}

TEST(Timer32Parse, RejectsChannelIndexOutOfRange)
{
    ParseResult<Timer32Config> result = parseTimer32Config("fixtures/timer32_channel_index_out_of_range.json");

    EXPECT_FALSE(result.success);
}

// Канал 0 модуля Timer32_1 закреплён за GPIO_0/PIN_0 (ТЗ §3.1.5 п.4) — тот же
// вывод, явно занятый GPIO-блоком, должен считаться конфликтом.
TEST(CrossModuleValidator, DetectsTimer32ChannelPinConflict)
{
    ParseResult<UartKnowledge> uart_knowledge_result = loadUartKnowledge("../knowledge_base/uart.json");
    ASSERT_TRUE(uart_knowledge_result.success) << uart_knowledge_result.error;
    ParseResult<Timer16Knowledge> timer16_knowledge_result = loadTimer16Knowledge("../knowledge_base/timer16.json");
    ASSERT_TRUE(timer16_knowledge_result.success) << timer16_knowledge_result.error;
    ParseResult<I2cKnowledge> i2c_knowledge_result = loadI2cKnowledge("../knowledge_base/i2c.json");
    ASSERT_TRUE(i2c_knowledge_result.success) << i2c_knowledge_result.error;
    ParseResult<Timer32Knowledge> timer32_knowledge_result = loadTimer32Knowledge("../knowledge_base/timer32.json");
    ASSERT_TRUE(timer32_knowledge_result.success) << timer32_knowledge_result.error;
    ParseResult<SpiKnowledge> spi_knowledge_result = loadSpiKnowledge("../knowledge_base/spi.json");
    ASSERT_TRUE(spi_knowledge_result.success) << spi_knowledge_result.error;

    CrossModuleValidationResult result = validatePinConflicts(
        "fixtures/cross_timer32_gpio_conflict.json", *uart_knowledge_result.config, *timer16_knowledge_result.config,
        *i2c_knowledge_result.config, *timer32_knowledge_result.config, *spi_knowledge_result.config);

    ASSERT_TRUE(result.success) << result.error;
    ASSERT_EQ(result.conflicts.size(), 1u);
    EXPECT_EQ(result.conflicts[0].gpio_instance, 0);
    EXPECT_EQ(result.conflicts[0].pin, 0);
    EXPECT_EQ(result.conflicts[0].conflicting_owners.size(), 2u);
}

// Base-counter путь (без каналов) не использует _IT-варианты HAL-вызовов —
// Errata п.9.1 актуальна только когда включается прерывание.
TEST(ProjectCodegen, Timer32BaseCounterUsesPlainStartWithoutErrataWarning)
{
    ParseResult<ProjectConfig> config_result = parseProjectConfig("fixtures/timer32_base_counter.json");
    ASSERT_TRUE(config_result.success) << config_result.error;

    ParseResult<GeneratedProjectFirmware> firmware_result =
        generateProjectFirmware(*config_result.config, loadProjectKnowledgeForTests());
    ASSERT_TRUE(firmware_result.success) << firmware_result.error;

    const std::string& init_c = firmware_result.config->mik32_init_c;
    EXPECT_TRUE(contains(init_c, "HAL_Timer32_Start(&htimer32_0);"));
    EXPECT_FALSE(contains(init_c, "Errata"));
    EXPECT_FALSE(contains(init_c, "_IRQHandler_Template"));

    EXPECT_TRUE(contains(firmware_result.config->mik32_init_h, "void TIMER32_0_Init(void);"));
}

// Канал с mode=pwm запускается через _IT-вариант (единственный способ у
// реального HAL реально включить канал — HAL_Timer32_Channel_Enable вызван
// только внутри *_Start_IT), поэтому это единственный путь, где ТЗ §3.1.5
// п.3 требует и предупреждение об Errata п.9.1, и шаблон обработчика.
TEST(ProjectCodegen, Timer32ChannelConfigEmitsErrataWarningAndIrqTemplate)
{
    ParseResult<ProjectConfig> config_result = parseProjectConfig("fixtures/timer32_channel_pwm.json");
    ASSERT_TRUE(config_result.success) << config_result.error;

    ParseResult<GeneratedProjectFirmware> firmware_result =
        generateProjectFirmware(*config_result.config, loadProjectKnowledgeForTests());
    ASSERT_TRUE(firmware_result.success) << firmware_result.error;

    const std::string& init_c = firmware_result.config->mik32_init_c;
    EXPECT_TRUE(contains(init_c, "Errata п. 9.1"));
    EXPECT_TRUE(contains(init_c, "HAL_Timer32_PWM_Start_IT(&htimer32_1, &htimer32_1_ch0);"));
    EXPECT_TRUE(contains(init_c, "void TIMER32_1_IRQHandler_Template(void)"));
    // ТЗ §3.1.5 п.3: сброс флага — первое действие тела функции-обработчика.
    const size_t handler_at = init_c.find("void TIMER32_1_IRQHandler_Template(void)");
    ASSERT_NE(handler_at, std::string::npos);
    const size_t body_open = init_c.find('{', handler_at);
    const size_t clear_at = init_c.find("HAL_Timer32_InterruptFlags_Clear(&htimer32_1);", body_open);
    ASSERT_NE(clear_at, std::string::npos);
    // Никакого другого исполняемого выражения (не считая пустых строк) между
    // открывающей скобкой и вызовом очистки флага быть не должно.
    const std::string between = init_c.substr(body_open + 1, clear_at - (body_open + 1));
    EXPECT_TRUE(std::all_of(between.begin(), between.end(),
        [](char c) { return c == ' ' || c == '\n' || c == '\t' || c == '\r'; }));
}

// --- GPIO IRQ / LINE_MUX (ТЗ §3.1.2 п.5-7) ---------------------------------

TEST(GpioParse, ParsesValidInterrupt)
{
    ParseResult<GpioConfig> result = parseGpioConfig("fixtures/gpio_irq_valid.json");

    ASSERT_TRUE(result.success) << result.error;
    ASSERT_EQ(result.config->interrupts.size(), 1u);
    EXPECT_EQ(result.config->interrupts[0].pin, 6);
    EXPECT_EQ(result.config->interrupts[0].line, 2);
    EXPECT_EQ(result.config->interrupts[0].trigger, "rising");
}

// Прерывание конфигурируется на вывод, который физически подключён к
// мультиплексору линии — pin должен входить в pins[] этого же блока.
TEST(GpioParse, RejectsInterruptPinNotInBlockPins)
{
    ParseResult<GpioConfig> result = parseGpioConfig("fixtures/gpio_irq_pin_not_in_pins.json");

    EXPECT_FALSE(result.success);
}

TEST(GpioParse, RejectsInterruptLineOutOfRange)
{
    ParseResult<GpioConfig> result = parseGpioConfig("fixtures/gpio_irq_line_out_of_range.json");

    EXPECT_FALSE(result.success);
}

// Одна линия — один физический источник; регистр LINE_MUX не может слушать
// два разных вывода на одном канале одновременно.
TEST(GpioParse, RejectsDuplicateLineWithinSameBlock)
{
    ParseResult<GpioConfig> result = parseGpioConfig("fixtures/gpio_irq_duplicate_line_same_block.json");

    EXPECT_FALSE(result.success);
}

// ТЗ §3.1.2 п.5 / §3.1.6 п.2: у контроллера GPIO IRQ 8 каналов НА ВЕСЬ
// конфиг — тот же класс "глобального пула", что и 4 канала DMA.
TEST(ProjectConfigParse, RejectsGlobalGpioIrqLinePoolExhaustion)
{
    ParseResult<ProjectConfig> result = parseProjectConfig("fixtures/gpio_irq_global_line_pool_exhausted.json");

    EXPECT_FALSE(result.success);
    EXPECT_TRUE(contains(result.error, "line 3")) << result.error;
}

TEST(ProjectCodegen, GpioInterruptEmitsErrataWarningAndIrqTemplate)
{
    ParseResult<ProjectConfig> config_result = parseProjectConfig("fixtures/gpio_irq_valid.json");
    ASSERT_TRUE(config_result.success) << config_result.error;

    ParseResult<GeneratedProjectFirmware> firmware_result =
        generateProjectFirmware(*config_result.config, loadProjectKnowledgeForTests());
    ASSERT_TRUE(firmware_result.success) << firmware_result.error;

    const std::string& init_c = firmware_result.config->mik32_init_c;
    EXPECT_TRUE(contains(init_c, "Errata п. 3.1"));
    EXPECT_TRUE(contains(init_c, "HAL_GPIO_InitInterruptLine(GPIO_MUX_PORT2_6_LINE_2, GPIO_INT_MODE_RISING);"));
    EXPECT_TRUE(contains(init_c, "void GPIO_2_IRQHandler_Template(void)"));
    // ТЗ §3.1.2 п.7: сброс через (1 << N), не через макросы LINE_MUX/GPIO_MUX_*.
    EXPECT_TRUE(contains(init_c, "GPIO_IRQ->CLEAR = (1 << 2);"));
    EXPECT_FALSE(contains(init_c, "HAL_GPIO_ClearInterrupts"));
}

// (pin, line), которых нет в реальной таблице LINE_MUX датащита, должны
// быть отловлены на этапе кодогенерации (структурно валидны для парсера —
// это аппаратный факт, а не формат JSON).
TEST(ProjectCodegen, RejectsGpioInterruptWithNoHardwareMuxCombination)
{
    ParseResult<ProjectConfig> config_result = parseProjectConfig("fixtures/gpio_irq_invalid_hw_combo.json");
    ASSERT_TRUE(config_result.success) << config_result.error;

    ParseResult<GeneratedProjectFirmware> firmware_result =
        generateProjectFirmware(*config_result.config, loadProjectKnowledgeForTests());

    EXPECT_FALSE(firmware_result.success);
    EXPECT_TRUE(contains(firmware_result.error, "LINE_MUX")) << firmware_result.error;
}

// --- DMA (ТЗ §3.1.3.3 + §3.1.6 глобальный пул каналов) ---------------------

TEST(UartParse, ParsesValidDma)
{
    ParseResult<UartConfig> result = parseUsartConfig("fixtures/uart_dma_valid.json");

    ASSERT_TRUE(result.success) << result.error;
    ASSERT_EQ(result.config->dma.size(), 2u);
}

// ТЗ §3.1.3.3: DMA-передача в направлении TX не имеет смысла, если у этого
// же блока uart передатчик выключен (direction=rx).
TEST(UartParse, RejectsDmaDirectionMismatch)
{
    ParseResult<UartConfig> result = parseUsartConfig("fixtures/uart_dma_direction_mismatch.json");

    EXPECT_FALSE(result.success);
}

TEST(UartParse, RejectsDmaChannelOutOfRange)
{
    ParseResult<UartConfig> result = parseUsartConfig("fixtures/uart_dma_channel_out_of_range.json");

    EXPECT_FALSE(result.success);
}

// Не более одного элемента на направление — второй "tx" в одном блоке
// физически не может ссылаться на тот же USART TXDATA дважды.
TEST(UartParse, RejectsDuplicateDmaDirection)
{
    ParseResult<UartConfig> result = parseUsartConfig("fixtures/uart_dma_duplicate_direction.json");

    EXPECT_FALSE(result.success);
}

// ТЗ §3.1.6 п.2: «конфликты каналов DMA (4 канала на все периферийные
// запросы)» — буквальный, явно названный в ТЗ пример глобального пула.
TEST(ProjectConfigParse, RejectsGlobalDmaChannelPoolExhaustion)
{
    ParseResult<ProjectConfig> result = parseProjectConfig("fixtures/uart_dma_channel_conflict_global.json");

    EXPECT_FALSE(result.success);
    EXPECT_TRUE(contains(result.error, "DMA channel 2")) << result.error;
}

TEST(ProjectCodegen, UartDmaEmitsSharedInitAndPerDirectionHelpers)
{
    ParseResult<ProjectConfig> config_result = parseProjectConfig("fixtures/uart_dma_valid.json");
    ASSERT_TRUE(config_result.success) << config_result.error;

    ParseResult<GeneratedProjectFirmware> firmware_result =
        generateProjectFirmware(*config_result.config, loadProjectKnowledgeForTests());
    ASSERT_TRUE(firmware_result.success) << firmware_result.error;

    const std::string& init_c = firmware_result.config->mik32_init_c;
    // Общий hdma/DMA_Init — один на весь проект, не по одному на блок uart.
    EXPECT_TRUE(contains(init_c, "DMA_InitTypeDef hdma;"));
    EXPECT_TRUE(contains(init_c, "void DMA_Init(void)"));
    EXPECT_TRUE(contains(init_c, "hdma.Instance = DMA_CONFIG;"));

    EXPECT_TRUE(contains(init_c, "hdma_husart_0_tx.ChannelInit.Channel = DMA_CHANNEL_0;"));
    EXPECT_TRUE(contains(init_c, "hdma_husart_0_tx.ChannelInit.ReadMode = DMA_CHANNEL_MODE_MEMORY;"));
    EXPECT_TRUE(contains(init_c, "hdma_husart_0_tx.ChannelInit.WriteMode = DMA_CHANNEL_MODE_PERIPHERY;"));
    EXPECT_TRUE(contains(init_c, "hdma_husart_0_rx.ChannelInit.Channel = DMA_CHANNEL_1;"));
    EXPECT_TRUE(contains(init_c, "hdma_husart_0_rx.ChannelInit.ReadMode = DMA_CHANNEL_MODE_PERIPHERY;"));
    EXPECT_TRUE(contains(init_c, "hdma_husart_0_rx.ChannelInit.WriteMode = DMA_CHANNEL_MODE_MEMORY;"));

    EXPECT_TRUE(contains(init_c, "void UART_0_DMA_TX_Start(void)"));
    EXPECT_TRUE(contains(init_c, "HAL_DMA_Start(&hdma_husart_0_tx, (void*)husart_0_tx_dma_buffer, (void*)&UART_0->TXDATA, 32);"));
    EXPECT_TRUE(contains(init_c, "void UART_0_DMA_RX_Start(void)"));
    EXPECT_TRUE(contains(init_c, "HAL_DMA_Start(&hdma_husart_0_rx, (void*)&UART_0->RXDATA, (void*)husart_0_rx_dma_buffer, 16);"));

    // circular (rx) получает TODO-комментарий про повторный запуск из ISR;
    // linear (tx) — нет.
    EXPECT_TRUE(contains(init_c, "not_covered_by_this_codegen.dma_circular_buffer"));

    const std::string& init_h = firmware_result.config->mik32_init_h;
    EXPECT_TRUE(contains(init_h, "void DMA_Init(void);"));
    EXPECT_TRUE(contains(init_h, "void UART_0_DMA_TX_Start(void);"));
    EXPECT_TRUE(contains(init_h, "void UART_0_DMA_RX_Start(void);"));

    const std::string& main_c = firmware_result.config->main_c;
    const size_t dma_init_at = main_c.find("DMA_Init();");
    const size_t uart_init_at = main_c.find("UART_0_Init();");
    ASSERT_NE(dma_init_at, std::string::npos);
    ASSERT_NE(uart_init_at, std::string::npos);
    EXPECT_LT(dma_init_at, uart_init_at);

    // Буферы объявлены в mik32_init.c — без extern в main.c прикладной код
    // не мог бы ни заполнить TX-буфер, ни прочитать RX-буфер после приёма.
    EXPECT_TRUE(contains(main_c, "extern uint8_t husart_0_tx_dma_buffer[32];"));
    EXPECT_TRUE(contains(main_c, "extern uint8_t husart_0_rx_dma_buffer[16];"));
}

// Без единого блока uart, запросившего dma, DMA_Init/hdma не должны
// появляться вообще — иначе #include "mik32_hal_dma.h" был бы мёртвым
// кодом в самом частом (не-DMA) случае.
TEST(ProjectCodegen, OmitsDmaBoilerplateWhenNoUartRequestsIt)
{
    ParseResult<ProjectConfig> config_result = parseProjectConfig("fixtures/uart_valid.json");
    ASSERT_TRUE(config_result.success) << config_result.error;

    ParseResult<GeneratedProjectFirmware> firmware_result =
        generateProjectFirmware(*config_result.config, loadProjectKnowledgeForTests());
    ASSERT_TRUE(firmware_result.success) << firmware_result.error;

    EXPECT_FALSE(contains(firmware_result.config->mik32_init_c, "DMA_Init"));
    EXPECT_FALSE(contains(firmware_result.config->mik32_init_c, "mik32_hal_dma.h"));
}

// ============================================================================
// SPI — ТЗ по SPI молчит (см. docs/implementations/spi.md), источники:
// реальный HAL (mik32_hal_spi.h/.c) и datasheet (docs/MIK32_REFERENCE.md §4).
// ============================================================================

TEST(SpiParse, ParsesValidMasterConfig)
{
    ParseResult<SpiConfig> result = parseSpiConfig("fixtures/spi_master_valid.json");

    ASSERT_TRUE(result.success) << result.error;
    EXPECT_EQ(result.config->mode, "master");
    EXPECT_EQ(result.config->baud_rate_div, 64);
    EXPECT_EQ(result.config->chip_select, "cs0");
}

TEST(SpiParse, ParsesValidSlaveConfig)
{
    ParseResult<SpiConfig> result = parseSpiConfig("fixtures/spi_slave_valid.json");

    ASSERT_TRUE(result.success) << result.error;
    EXPECT_EQ(result.config->mode, "slave");
    // Поля master-only не запрошены — остаются на нулевых значениях
    // parseSpiBlock, а не на мусоре из JSON.
    EXPECT_EQ(result.config->chip_select, "");
}

TEST(SpiParse, RejectsMasterMissingChipSelect)
{
    ParseResult<SpiConfig> result = parseSpiConfig("fixtures/spi_master_missing_chip_select.json");

    EXPECT_FALSE(result.success);
}

TEST(SpiParse, RejectsThresholdOutOfRange)
{
    ParseResult<SpiConfig> result = parseSpiConfig("fixtures/spi_invalid_threshold.json");

    EXPECT_FALSE(result.success);
}

TEST(SpiParse, RejectsWrongInstance)
{
    ParseResult<SpiConfig> result = parseSpiConfig("fixtures/spi_wrong_instance.json");

    EXPECT_FALSE(result.success);
}

// SPI_0 master резервирует SCK/MOSI/MISO/SS_IN на GPIO_0/PIN_0-3 (независимо
// от mode) — явный gpio-блок на GPIO_0/PIN_1 (MOSI) должен конфликтовать.
TEST(CrossModuleValidator, DetectsSpiGpioPinConflict)
{
    ParseResult<UartKnowledge> uart_knowledge_result = loadUartKnowledge("../knowledge_base/uart.json");
    ASSERT_TRUE(uart_knowledge_result.success) << uart_knowledge_result.error;
    ParseResult<Timer16Knowledge> timer16_knowledge_result = loadTimer16Knowledge("../knowledge_base/timer16.json");
    ASSERT_TRUE(timer16_knowledge_result.success) << timer16_knowledge_result.error;
    ParseResult<I2cKnowledge> i2c_knowledge_result = loadI2cKnowledge("../knowledge_base/i2c.json");
    ASSERT_TRUE(i2c_knowledge_result.success) << i2c_knowledge_result.error;
    ParseResult<Timer32Knowledge> timer32_knowledge_result = loadTimer32Knowledge("../knowledge_base/timer32.json");
    ASSERT_TRUE(timer32_knowledge_result.success) << timer32_knowledge_result.error;
    ParseResult<SpiKnowledge> spi_knowledge_result = loadSpiKnowledge("../knowledge_base/spi.json");
    ASSERT_TRUE(spi_knowledge_result.success) << spi_knowledge_result.error;

    CrossModuleValidationResult result = validatePinConflicts(
        "fixtures/cross_spi_gpio_conflict.json", *uart_knowledge_result.config, *timer16_knowledge_result.config,
        *i2c_knowledge_result.config, *timer32_knowledge_result.config, *spi_knowledge_result.config);

    ASSERT_TRUE(result.success) << result.error;
    ASSERT_EQ(result.conflicts.size(), 1u);
    EXPECT_EQ(result.conflicts[0].gpio_instance, 0);
    EXPECT_EQ(result.conflicts[0].pin, 1);
}

TEST(ProjectCodegen, SpiMasterInitUsesHalFieldsForMode)
{
    ParseResult<ProjectConfig> config_result = parseProjectConfig("fixtures/spi_master_valid.json");
    ASSERT_TRUE(config_result.success) << config_result.error;

    ParseResult<GeneratedProjectFirmware> firmware_result =
        generateProjectFirmware(*config_result.config, loadProjectKnowledgeForTests());
    ASSERT_TRUE(firmware_result.success) << firmware_result.error;

    const std::string& init_c = firmware_result.config->mik32_init_c;
    EXPECT_TRUE(contains(init_c, "hspi0.Instance = SPI_0;"));
    EXPECT_TRUE(contains(init_c, "hspi0.Init.SPI_Mode = HAL_SPI_MODE_MASTER;"));
    EXPECT_TRUE(contains(init_c, "hspi0.Init.CLKPhase = SPI_PHASE_ON;"));
    EXPECT_TRUE(contains(init_c, "hspi0.Init.CLKPolarity = SPI_POLARITY_HIGH;"));
    EXPECT_TRUE(contains(init_c, "hspi0.Init.BaudRateDiv = SPI_BAUDRATE_DIV64;"));
    EXPECT_TRUE(contains(init_c, "hspi0.Init.ManualCS = SPI_MANUALCS_OFF;"));
    EXPECT_TRUE(contains(init_c, "hspi0.Init.ChipSelect = SPI_CS_0;"));
    EXPECT_TRUE(contains(init_c, "HAL_SPI_Init(&hspi0)"));
}

// Поля master-only (BaudRateDiv/Decoder/ManualCS/ChipSelect) не должны
// появляться в init-функции slave — это ровно то, что делают (точнее, чего
// НЕ делают) оба вендорных примера HAL_SPI_Master/Slave SPI0_Init.
TEST(ProjectCodegen, SpiSlaveInitOmitsMasterOnlyFields)
{
    ParseResult<ProjectConfig> config_result = parseProjectConfig("fixtures/spi_slave_valid.json");
    ASSERT_TRUE(config_result.success) << config_result.error;

    ParseResult<GeneratedProjectFirmware> firmware_result =
        generateProjectFirmware(*config_result.config, loadProjectKnowledgeForTests());
    ASSERT_TRUE(firmware_result.success) << firmware_result.error;

    const std::string& init_c = firmware_result.config->mik32_init_c;
    EXPECT_TRUE(contains(init_c, "hspi0.Init.SPI_Mode = HAL_SPI_MODE_SLAVE;"));
    EXPECT_FALSE(contains(init_c, "Init.BaudRateDiv"));
    EXPECT_FALSE(contains(init_c, "Init.Decoder"));
    EXPECT_FALSE(contains(init_c, "Init.ManualCS"));
    EXPECT_FALSE(contains(init_c, "Init.ChipSelect"));
}

// Errata MIK32V2 v1.0.1 §7.1: SPI_0 + master + авто-CS (manual_cs=false) +
// chip_select=cs3 — комментарий обязателен, кодоген НЕ блокируется (ошибок
// быть не должно — ситуации из Errata являются Предупреждением).
TEST(ProjectCodegen, SpiEmitsErrataCommentForAutoCsThreeOnInstanceZero)
{
    ParseResult<ProjectConfig> config_result = parseProjectConfig("fixtures/spi0_master_auto_cs3_errata.json");
    ASSERT_TRUE(config_result.success) << config_result.error;

    ParseResult<GeneratedProjectFirmware> firmware_result =
        generateProjectFirmware(*config_result.config, loadProjectKnowledgeForTests());
    ASSERT_TRUE(firmware_result.success) << firmware_result.error;

    EXPECT_TRUE(contains(firmware_result.config->mik32_init_c, "Errata п. 7.1"));
}

// ===========================================================================
// Часть 1/2: CMake-тулчейн (codegen/project/cmake_project_codegen.h) —
// второй, независимый от PlatformIO способ упаковки проекта.
// ===========================================================================
namespace
{
    // generated_projects/ гитигнорится на уровне репозитория (CLAUDE.md,
    // "Generated firmware projects stay private") — тестовые каталоги живут
    // там же, по тому же принципу: build-мусор, а не источник.
    fs::path scratchDirFor(const std::string& name)
    {
        return fs::path("../generated_projects") / ("test_cmake_" + name);
    }

    // Каталог-заглушка с той же структурой, что и
    // ~/.platformio/packages/framework-mik32v2-sdk (hal/<модуль>/Include|Source,
    // shared/<модуль>) — минимальный, но реальный на диске набор файлов,
    // достаточный для проверки обхода ФС в writeGeneratedCmakeProject.
    fs::path makeLocalHalDir(const std::string& name, bool with_hal_subdir)
    {
        const fs::path dir = scratchDirFor(name);
        std::error_code ec;
        fs::remove_all(dir, ec);
        fs::create_directories(dir, ec);
        if (with_hal_subdir)
        {
            fs::create_directories(dir / "hal" / "core" / "Include", ec);
            fs::create_directories(dir / "hal" / "core" / "Source", ec);
            fs::create_directories(dir / "shared" / "periphery", ec);
            std::ofstream(dir / "hal" / "core" / "Source" / "stub.c") << "/* stub */\n";
            std::ofstream(dir / "hal" / "core" / "Include" / "stub.h") << "#pragma once\n";
            std::ofstream(dir / "shared" / "periphery" / "stub.h") << "#pragma once\n";
        }
        return dir;
    }

    nlohmann::json readJsonFixture(const std::string& path)
    {
        return nlohmann::json::parse(readFileToString(path));
    }
}

TEST(ProjectConfigParse, KeepsToolchainDefaultsWhenFieldsAbsent)
{
    // fixtures/project_all_peripherals.json не содержит ни одного из пяти
    // новых полей — старые фикстуры обязаны продолжать парситься с дефолтами
    // struct ProjectConfig, без единой правки самой фикстуры.
    ParseResult<ProjectConfig> result = parseProjectConfig("fixtures/project_all_peripherals.json");
    ASSERT_TRUE(result.success) << result.error;

    EXPECT_EQ(result.config->toolchain, ToolchainKind::PlatformIO);
    EXPECT_EQ(result.config->hal_source_mode, HalSourceMode::Bundled);
    EXPECT_FALSE(result.config->per_type_files);
    EXPECT_TRUE(result.config->hal_source_local_path.empty());
    EXPECT_TRUE(result.config->compiler_path.empty());
}

TEST(ProjectConfigParse, ParsesCmakeToolchainFields)
{
    ParseResult<ProjectConfig> result = parseProjectConfig("fixtures/project_cmake_bundled.json");
    ASSERT_TRUE(result.success) << result.error;

    EXPECT_EQ(result.config->toolchain, ToolchainKind::CMake);
    EXPECT_EQ(result.config->hal_source_mode, HalSourceMode::Bundled);
    EXPECT_TRUE(result.config->per_type_files);
}

// OVERNIGHT_FIELD_REFERENCE.md — лайт-версия конфигурации тактирования.
TEST(ProjectConfigParse, KeepsClockDefaultsWhenFieldAbsent)
{
    ParseResult<ProjectConfig> result = parseProjectConfig("fixtures/project_all_peripherals.json");
    ASSERT_TRUE(result.success) << result.error;

    EXPECT_EQ(result.config->clock.oscillator_system, "osc32m");
    EXPECT_EQ(result.config->clock.ahb_divider, 0);
    EXPECT_EQ(result.config->clock.apbm_divider, 0);
    EXPECT_EQ(result.config->clock.apbp_divider, 0);
}

TEST(ProjectConfigParse, ParsesCustomClockFields)
{
    ParseResult<ProjectConfig> result = parseProjectConfig("fixtures/project_clock_custom.json");
    ASSERT_TRUE(result.success) << result.error;

    EXPECT_EQ(result.config->clock.oscillator_system, "hsi32m");
    EXPECT_EQ(result.config->clock.ahb_divider, 1);
    EXPECT_EQ(result.config->clock.apbm_divider, 3);
    EXPECT_EQ(result.config->clock.apbp_divider, 7);
}

TEST(ProjectConfigParse, RejectsInvalidOscillatorSystem)
{
    ParseResult<ProjectConfig> result = parseProjectConfig("fixtures/project_clock_invalid_oscillator.json");

    EXPECT_FALSE(result.success);
}

TEST(ProjectConfigParse, RejectsClockDividerOutOfRange)
{
    ParseResult<ProjectConfig> result = parseProjectConfig("fixtures/project_clock_divider_out_of_range.json");

    EXPECT_FALSE(result.success);
}

TEST(ProjectCodegen, GeneratedSystemClockConfigReflectsCustomClockFields)
{
    ParseResult<ProjectConfig> config_result = parseProjectConfig("fixtures/project_clock_custom.json");
    ASSERT_TRUE(config_result.success) << config_result.error;

    ParseResult<GeneratedProjectFirmware> firmware_result =
        generateProjectFirmware(*config_result.config, loadProjectKnowledgeForTests());
    ASSERT_TRUE(firmware_result.success) << firmware_result.error;

    const std::string& main_c = firmware_result.config->main_c;
    EXPECT_NE(main_c.find("PCC_OscInit.FreqMon.OscillatorSystem = PCC_OSCILLATORTYPE_HSI32M;"), std::string::npos);
    EXPECT_NE(main_c.find("PCC_OscInit.AHBDivider = 1;"), std::string::npos);
    EXPECT_NE(main_c.find("PCC_OscInit.APBMDivider = 3;"), std::string::npos);
    EXPECT_NE(main_c.find("PCC_OscInit.APBPDivider = 7;"), std::string::npos);
}

TEST(ProjectConfigParse, RejectsInvalidToolchainValue)
{
    ParseResult<ProjectConfig> result = parseProjectConfig("fixtures/project_invalid_toolchain.json");

    EXPECT_FALSE(result.success);
}

TEST(CmakeProjectCodegen, SplitsInitFilesPerPeripheralTypeWhenRequested)
{
    ParseResult<ProjectConfig> config_result = parseProjectConfig("fixtures/project_cmake_bundled.json");
    ASSERT_TRUE(config_result.success) << config_result.error;
    ASSERT_TRUE(config_result.config->per_type_files);

    ParseResult<GeneratedProjectFirmwareCmake> firmware_result =
        generateProjectFirmwareCmake(*config_result.config, loadProjectKnowledgeForTests());
    ASSERT_TRUE(firmware_result.success) << firmware_result.error;

    std::vector<std::string> names;
    for (const GeneratedFile& file : firmware_result.config->init_files)
    {
        names.push_back(file.relative_path);
    }

    EXPECT_NE(std::find(names.begin(), names.end(), "mik32_gpio_init.c"), names.end());
    EXPECT_NE(std::find(names.begin(), names.end(), "mik32_gpio_init.h"), names.end());
    EXPECT_NE(std::find(names.begin(), names.end(), "mik32_usart_init.c"), names.end());
    EXPECT_NE(std::find(names.begin(), names.end(), "mik32_usart_init.h"), names.end());
    // Фикстура не запрашивает DMA — общего "остатка" быть не должно вовсе.
    EXPECT_EQ(std::find(names.begin(), names.end(), "mik32_init.c"), names.end())
        << "per_type_files=true без DMA не должен порождать общий mik32_init.c";

    // Тот же конфиг с per_type_files=false — ровно та же пара файлов, что и
    // в PlatformIO-пути (project_codegen.h).
    ParseResult<ProjectConfig> combined_config_result = parseProjectConfig("fixtures/project_cmake_bundled.json");
    ASSERT_TRUE(combined_config_result.success) << combined_config_result.error;
    ProjectConfig combined_config = *combined_config_result.config;
    combined_config.per_type_files = false;

    ParseResult<GeneratedProjectFirmwareCmake> combined_firmware =
        generateProjectFirmwareCmake(combined_config, loadProjectKnowledgeForTests());
    ASSERT_TRUE(combined_firmware.success) << combined_firmware.error;
    ASSERT_EQ(combined_firmware.config->init_files.size(), 2u);
    EXPECT_EQ(combined_firmware.config->init_files[0].relative_path, "mik32_init.h");
    EXPECT_EQ(combined_firmware.config->init_files[1].relative_path, "mik32_init.c");
}

TEST(CmakeProjectCodegen, RejectsNonexistentLocalHalPathWithoutCreatingOutputDir)
{
    ParseResult<ProjectConfig> config_result = parseProjectConfig("fixtures/project_cmake_bundled.json");
    ASSERT_TRUE(config_result.success) << config_result.error;
    ProjectConfig config = *config_result.config;
    config.hal_source_mode = HalSourceMode::LocalPath;
    config.hal_source_local_path = "fixtures/this_path_does_not_exist_at_all";

    ParseResult<GeneratedProjectFirmwareCmake> firmware_result =
        generateProjectFirmwareCmake(config, loadProjectKnowledgeForTests());
    ASSERT_TRUE(firmware_result.success) << firmware_result.error;

    const fs::path output_dir = scratchDirFor("no_output_created");
    std::error_code ec;
    fs::remove_all(output_dir, ec);
    ASSERT_FALSE(fs::exists(output_dir));

    const nlohmann::json project_json = readJsonFixture("fixtures/project_cmake_bundled.json");

    CodegenWriteResult write_result = writeGeneratedCmakeProject(
        *firmware_result.config,
        "../templates/cmake_skeleton",
        "../templates/mik32_sdk_snapshot",
        output_dir.string(),
        config,
        project_json);

    EXPECT_FALSE(write_result.success);
    EXPECT_FALSE(fs::exists(output_dir));
}

// Транзакционность (ТЗ §3.2.2): ошибка обнаруживается ПОСЛЕ того, как
// скелет уже скопирован во временный каталог (hal_source_local_path
// существует, но без hal/ внутри — эта проверка идёт вторым шагом), поэтому
// тест реально проверяет откат, а не "ничего не успело начаться".
TEST(CmakeProjectCodegen, TransactionRollsBackOnMidwayFailureWithoutTouchingExistingOutput)
{
    ParseResult<ProjectConfig> config_result = parseProjectConfig("fixtures/project_cmake_bundled.json");
    ASSERT_TRUE(config_result.success) << config_result.error;
    ProjectConfig config = *config_result.config;
    config.hal_source_mode = HalSourceMode::LocalPath;

    const fs::path bad_local = makeLocalHalDir("bad_local_no_hal", /*with_hal_subdir=*/false);
    config.hal_source_local_path = bad_local.string();

    ParseResult<GeneratedProjectFirmwareCmake> firmware_result =
        generateProjectFirmwareCmake(config, loadProjectKnowledgeForTests());
    ASSERT_TRUE(firmware_result.success) << firmware_result.error;

    const fs::path output_dir = scratchDirFor("rollback_output");
    std::error_code ec;
    fs::remove_all(output_dir, ec);
    fs::create_directories(output_dir / "src", ec);
    {
        std::ofstream marker(output_dir / "PREEXISTING.txt");
        marker << "user content, must survive\n";
    }
    {
        std::ofstream existing_main(output_dir / "src" / "main.c");
        existing_main << "/* old main.c, must survive */\n";
    }

    const nlohmann::json project_json = readJsonFixture("fixtures/project_cmake_bundled.json");

    CodegenWriteResult write_result = writeGeneratedCmakeProject(
        *firmware_result.config,
        "../templates/cmake_skeleton",
        "../templates/mik32_sdk_snapshot",
        output_dir.string(),
        config,
        project_json);

    EXPECT_FALSE(write_result.success);

    // Ничего нового не появилось...
    EXPECT_FALSE(fs::exists(output_dir / "mik32-sdk"));
    EXPECT_FALSE(fs::exists(output_dir / "CMakeLists.txt"));
    EXPECT_FALSE(fs::exists(output_dir / "project.mik32"));
    // ...а то, что было, осталось побайтово тем же самым.
    ASSERT_TRUE(fs::exists(output_dir / "PREEXISTING.txt"));
    ASSERT_TRUE(fs::exists(output_dir / "src" / "main.c"));
    EXPECT_EQ(readFileToString((output_dir / "PREEXISTING.txt").string()), "user content, must survive\n");
    EXPECT_EQ(readFileToString((output_dir / "src" / "main.c").string()), "/* old main.c, must survive */\n");

    // Осколка временного каталога рядом не осталось.
    bool leftover_tmp_found = false;
    for (const auto& entry : fs::directory_iterator(output_dir.parent_path()))
    {
        if (entry.path().filename().string().rfind(output_dir.filename().string() + ".tmp-generating-", 0) == 0)
        {
            leftover_tmp_found = true;
        }
    }
    EXPECT_FALSE(leftover_tmp_found);

    fs::remove_all(output_dir, ec);
    fs::remove_all(bad_local, ec);
}

// Не входит в обязательный список из 6 сценариев спецификации, но проверяет
// то, ради чего вся транзакция затевалась: успешная запись — маркеры
// CMakeLists.txt реально заполняются реальным (не выдуманным) списком
// файлов, найденных обходом только что скопированного mik32-sdk/.
TEST(CmakeProjectCodegen, WritesProjectSuccessfullyAndFillsCMakeMarkersFromRealFileTree)
{
    ParseResult<ProjectConfig> config_result = parseProjectConfig("fixtures/project_cmake_bundled.json");
    ASSERT_TRUE(config_result.success) << config_result.error;
    ProjectConfig config = *config_result.config;
    config.hal_source_mode = HalSourceMode::LocalPath;

    const fs::path local_hal = makeLocalHalDir("good_local_hal", /*with_hal_subdir=*/true);
    config.hal_source_local_path = local_hal.string();
    config.compiler_path = "/opt/riscv/bin/riscv32-unknown-elf-gcc";

    ParseResult<GeneratedProjectFirmwareCmake> firmware_result =
        generateProjectFirmwareCmake(config, loadProjectKnowledgeForTests());
    ASSERT_TRUE(firmware_result.success) << firmware_result.error;

    const fs::path output_dir = scratchDirFor("write_success");
    std::error_code ec;
    fs::remove_all(output_dir, ec);

    const nlohmann::json project_json = readJsonFixture("fixtures/project_cmake_bundled.json");

    CodegenWriteResult write_result = writeGeneratedCmakeProject(
        *firmware_result.config,
        "../templates/cmake_skeleton",
        "../templates/mik32_sdk_snapshot",
        output_dir.string(),
        config,
        project_json);
    ASSERT_TRUE(write_result.success) << write_result.error;

    EXPECT_TRUE(fs::exists(output_dir / "CMakeLists.txt"));
    EXPECT_TRUE(fs::exists(output_dir / "cmake" / "mik32-riscv-toolchain.cmake"));
    EXPECT_TRUE(fs::exists(output_dir / "mik32-sdk" / "hal" / "core" / "Source" / "stub.c"));
    EXPECT_TRUE(fs::exists(output_dir / "mik32-sdk" / "shared" / "periphery" / "stub.h"));
    EXPECT_TRUE(fs::exists(output_dir / "src" / "mik32_gpio_init.c"));
    EXPECT_TRUE(fs::exists(output_dir / "src" / "main.c"));
    EXPECT_TRUE(fs::exists(output_dir / "project.mik32"));

    const std::string cmakelists = readFileToString((output_dir / "CMakeLists.txt").string());
    EXPECT_TRUE(contains(cmakelists, "src/mik32_gpio_init.c"));
    EXPECT_TRUE(contains(cmakelists, "src/main.c"));
    EXPECT_TRUE(contains(cmakelists, "mik32-sdk/hal/core/Source/stub.c"));
    EXPECT_TRUE(contains(cmakelists, "mik32-sdk/hal/core/Include"));
    EXPECT_TRUE(contains(cmakelists, "mik32-sdk/shared/periphery"));
    EXPECT_TRUE(contains(cmakelists, "set(MIK32_COMPILER_PATH \"/opt/riscv/bin/riscv32-unknown-elf-gcc\""));

    fs::remove_all(output_dir, ec);
    fs::remove_all(local_hal, ec);
}
// === НАЧАЛО: временные тесты транзакционности (координатор: перенести этот блок) ===
//
// Проверяют транзакционную запись writeGeneratedProject (codegen/project/
// project_codegen.cpp): mik32_init.h/.c/main.c пишутся во временный
// каталог-брат src_dir и переносятся в src_dir только при полном успехе;
// при любой ошибке src_dir остаётся нетронутым. См.
// docs/implementations/project_codegen.md, раздел "Транзакционная запись
// writeGeneratedProject".
#include <filesystem>

namespace
{
    namespace test_fs = std::filesystem;

    // Уникальный каталог под системным temp — тесты этого блока не должны
    // трогать generated_projects/ и не должны видеть друг друга.
    test_fs::path makeTempTestOutputDir(const std::string& test_name)
    {
        const auto now_ns = static_cast<unsigned long long>(
            std::chrono::steady_clock::now().time_since_epoch().count());
        return test_fs::temp_directory_path() /
            ("mik32_cfg_transactional_test_" + test_name + "_" + std::to_string(now_ns));
    }

    std::string readBinaryFileToString(const test_fs::path& path)
    {
        std::ifstream file(path, std::ios::binary);
        std::ostringstream buffer;
        buffer << file.rdbuf();
        return buffer.str();
    }
}

// Случай 1: успешная генерация в пустой каталог — файлы на диске побайтово
// совпадают с тем, что вернул generateProjectFirmware (то же самое, что
// писала прежняя, нетранзакционная версия writeGeneratedProject, для случая
// без существующего main.c для слияния).
TEST(ProjectCodegenWriteTransactional, SuccessfulWriteMatchesGeneratedFirmwareByteForByte)
{
    ParseResult<ProjectConfig> config_result = parseProjectConfig("fixtures/gpio_codegen_output.json");
    ASSERT_TRUE(config_result.success) << config_result.error;

    ParseResult<GeneratedProjectFirmware> firmware_result =
        generateProjectFirmware(*config_result.config, loadProjectKnowledgeForTests());
    ASSERT_TRUE(firmware_result.success) << firmware_result.error;

    const test_fs::path output_dir = makeTempTestOutputDir("success");
    std::error_code cleanup_error;
    test_fs::remove_all(output_dir, cleanup_error);

    const CodegenWriteResult write_result =
        writeGeneratedProject(*firmware_result.config, "../templates/pio_skeleton", output_dir.u8string());
    ASSERT_TRUE(write_result.success) << write_result.error;

    const test_fs::path src_dir = output_dir / "src";
    EXPECT_EQ(readBinaryFileToString(src_dir / "mik32_init.h"), firmware_result.config->mik32_init_h);
    EXPECT_EQ(readBinaryFileToString(src_dir / "mik32_init.c"), firmware_result.config->mik32_init_c);
    EXPECT_EQ(readBinaryFileToString(src_dir / "main.c"), firmware_result.config->main_c);
    // Скелет тоже должен был скопироваться (вне транзакции, но частью того
    // же вызова) — platformio.ini как маркер того, что копирование прошло.
    EXPECT_TRUE(test_fs::exists(output_dir / "platformio.ini"));

    test_fs::remove_all(output_dir, cleanup_error);
}

// Порт прошивки всегда /dev/ttyUSB0 — и в свежем проекте, и в проекте,
// где platformio.ini остался от прошлой генерации с другим портом.
TEST(ProjectCodegenWriteTransactional, UploadPortIsAlwaysTtyUsb0)
{
    ParseResult<ProjectConfig> config_result = parseProjectConfig("fixtures/gpio_codegen_output.json");
    ASSERT_TRUE(config_result.success) << config_result.error;
    ParseResult<GeneratedProjectFirmware> firmware_result =
        generateProjectFirmware(*config_result.config, loadProjectKnowledgeForTests());
    ASSERT_TRUE(firmware_result.success) << firmware_result.error;

    const test_fs::path output_dir = makeTempTestOutputDir("upload_port");
    std::error_code cleanup_error;
    test_fs::remove_all(output_dir, cleanup_error);

    ASSERT_TRUE(writeGeneratedProject(*firmware_result.config, "../templates/pio_skeleton", output_dir.u8string()).success);
    EXPECT_TRUE(contains(readBinaryFileToString(output_dir / "platformio.ini"), "--com=/dev/ttyUSB0"));

    {
        std::ofstream stale(output_dir / "platformio.ini", std::ios::binary);
        stale << "upload_command = python3 uploader.py $SOURCE --com=/dev/ttyUSB1\n";
    }
    ASSERT_TRUE(writeGeneratedProject(*firmware_result.config, "../templates/pio_skeleton", output_dir.u8string()).success);
    const std::string ini = readBinaryFileToString(output_dir / "platformio.ini");
    EXPECT_TRUE(contains(ini, "--com=/dev/ttyUSB0"));
    EXPECT_FALSE(contains(ini, "ttyUSB1"));

    test_fs::remove_all(output_dir, cleanup_error);
}

// Случай 2: сбой на середине — output_project_dir/src существует как обычный
// файл, а не каталог, поэтому перенос из временного каталога в src_dir
// гарантированно проваливается на шаге create_directories(src_dir). Ожидание:
// writeGeneratedProject возвращает success=false, "src" остаётся ровно тем
// же обычным файлом (не превращается в каталог, не приобретает потерянных
// mik32_init.h/.c/main.c), и никакого временного каталога-брата не остаётся.
TEST(ProjectCodegenWriteTransactional, MidTransactionFailureLeavesOutputDirUntouched)
{
    ParseResult<ProjectConfig> config_result = parseProjectConfig("fixtures/gpio_codegen_output.json");
    ASSERT_TRUE(config_result.success) << config_result.error;

    ParseResult<GeneratedProjectFirmware> firmware_result =
        generateProjectFirmware(*config_result.config, loadProjectKnowledgeForTests());
    ASSERT_TRUE(firmware_result.success) << firmware_result.error;

    const test_fs::path output_dir = makeTempTestOutputDir("midfailure");
    std::error_code cleanup_error;
    test_fs::remove_all(output_dir, cleanup_error);
    test_fs::create_directories(output_dir);

    // "src" — обычный файл, не каталог: любая попытка создать/писать внутрь
    // src_dir гарантированно проваливается.
    const test_fs::path bogus_src_path = output_dir / "src";
    {
        std::ofstream bogus_src_file(bogus_src_path, std::ios::binary);
        bogus_src_file << "не каталог";
    }
    ASSERT_TRUE(test_fs::exists(bogus_src_path));
    ASSERT_FALSE(test_fs::is_directory(bogus_src_path));

    const CodegenWriteResult write_result =
        writeGeneratedProject(*firmware_result.config, "../templates/pio_skeleton", output_dir.u8string());

    EXPECT_FALSE(write_result.success);
    // "src" остаётся ровно тем же обычным файлом.
    ASSERT_TRUE(test_fs::exists(bogus_src_path));
    EXPECT_FALSE(test_fs::is_directory(bogus_src_path));
    EXPECT_EQ(readBinaryFileToString(bogus_src_path), "не каталог");

    // Никаких временных каталогов-братьев (src_tmp_*) не осталось валяться.
    bool leftover_temp_dir_found = false;
    for (const auto& entry : test_fs::directory_iterator(output_dir))
    {
        if (entry.path().filename().u8string().rfind("src_tmp_", 0) == 0)
        {
            leftover_temp_dir_found = true;
        }
    }
    EXPECT_FALSE(leftover_temp_dir_found);

    test_fs::remove_all(output_dir, cleanup_error);
}

// Случай 3: повторная генерация поверх существующего main.c с ручной правкой
// внутри USER CODE-секции — правка обязана пережить транзакционную обёртку
// точно так же, как переживала прежнюю прямую запись.
TEST(ProjectCodegenWriteTransactional, PreservesUserCodeAcrossRegeneration)
{
    ParseResult<ProjectConfig> config_result = parseProjectConfig("fixtures/project_all_peripherals.json");
    ASSERT_TRUE(config_result.success) << config_result.error;

    ParseResult<GeneratedProjectFirmware> firmware_result =
        generateProjectFirmware(*config_result.config, loadProjectKnowledgeForTests());
    ASSERT_TRUE(firmware_result.success) << firmware_result.error;

    const test_fs::path output_dir = makeTempTestOutputDir("usercode");
    std::error_code cleanup_error;
    test_fs::remove_all(output_dir, cleanup_error);

    const CodegenWriteResult first_write =
        writeGeneratedProject(*firmware_result.config, "../templates/pio_skeleton", output_dir.u8string());
    ASSERT_TRUE(first_write.success) << first_write.error;

    const test_fs::path main_c_path = output_dir / "src" / "main.c";
    std::string main_c_on_disk = readBinaryFileToString(main_c_path);
    ASSERT_NE(main_c_on_disk.find("/* USER CODE BEGIN Loop */"), std::string::npos);

    // Пользователь вручную вставляет строку внутрь USER CODE-секции Loop.
    const std::string marker_begin = "/* USER CODE BEGIN Loop */";
    const size_t insert_at = main_c_on_disk.find(marker_begin) + marker_begin.size();
    const std::string user_marker_line = "\n        MY_HAND_WRITTEN_MARKER_CALL();";
    main_c_on_disk.insert(insert_at, user_marker_line);
    {
        std::ofstream edited_file(main_c_path, std::ios::binary | std::ios::trunc);
        edited_file << main_c_on_disk;
    }

    // Регенерация той же прошивки поверх уже существующего проекта.
    const CodegenWriteResult second_write =
        writeGeneratedProject(*firmware_result.config, "../templates/pio_skeleton", output_dir.u8string());
    ASSERT_TRUE(second_write.success) << second_write.error;

    const std::string main_c_after_regeneration = readBinaryFileToString(main_c_path);
    EXPECT_NE(main_c_after_regeneration.find("MY_HAND_WRITTEN_MARKER_CALL();"), std::string::npos);
    // mik32_init.h/.c маркеров не имеют и должны совпасть со свежей генерацией.
    EXPECT_EQ(readBinaryFileToString(output_dir / "src" / "mik32_init.c"), firmware_result.config->mik32_init_c);

    test_fs::remove_all(output_dir, cleanup_error);
}
// === КОНЕЦ: временные тесты транзакционности ===
