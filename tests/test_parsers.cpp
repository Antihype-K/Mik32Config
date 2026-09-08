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

#include "parse_result.h"

#include <algorithm>
#include <fstream>
#include <sstream>

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
