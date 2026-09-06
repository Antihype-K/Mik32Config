//
// Created by antihype on 04.08.2026.
//

#include <gtest/gtest.h>
#include <gpio/gpio_parser.h>
#include <gpio_knowledge.h>
#include <gpio_codegen.h>
#include <usart_parser.h>
#include <uart_knowledge.h>
#include <uart_codegen.h>
#include <user_code_markers.h>
#include <cross_module_validator.h>

#include "parse_result.h"

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
}

// Golden-file тест кодогенератора: сравнивает mik32_init.c, собранный
// generateGpioFirmware для минимальной фикстуры (один пин на output), с
// заранее зафиксированным эталоном. Та же фикстура используется как сценарий
// для обязательной аппаратной проверки на 1T Rex Board (см. CLAUDE.md,
// "Hardware validation gate") — GPIO_0 pin 3 совпадает с пином светодиода в
// docs/examples/mik32-examples/HAL_Blink.
TEST(GpioCodegen, MatchesGoldenFileForSinglePinOutput)
{
    ParseResult<GpioConfig> parse_result = parseGpioConfig("fixtures/gpio_codegen_output.json");
    ASSERT_TRUE(parse_result.success);

    ParseResult<GpioKnowledge> knowledge_result = loadGpioKnowledge("../knowledge_base/gpio.json");
    ASSERT_TRUE(knowledge_result.success) << knowledge_result.error;

    ParseResult<GeneratedGpioFirmware> firmware_result =
        generateGpioFirmware(*parse_result.config, *knowledge_result.config);
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

// Golden-file тест кодогенератора UART: instance 1 совпадает с TXD/RXD,
// промаркированными на 1T Rex Board (см. docs/implementations/uart.md).
TEST(UartCodegen, MatchesGoldenFileForAsyncNoFlowControl)
{
    ParseResult<UartConfig> parse_result = parseUsartConfig("fixtures/uart_codegen_output.json");
    ASSERT_TRUE(parse_result.success);

    ParseResult<UartKnowledge> knowledge_result = loadUartKnowledge("../knowledge_base/uart.json");
    ASSERT_TRUE(knowledge_result.success) << knowledge_result.error;

    ParseResult<GeneratedUartFirmware> firmware_result =
        generateUartFirmware(*parse_result.config, *knowledge_result.config);
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

    CrossModuleValidationResult result = validatePinConflicts(
        "fixtures/cross_gpio_uart_conflict.json", *knowledge_result.config);

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

    CrossModuleValidationResult result = validatePinConflicts(
        "fixtures/cross_gpio_uart_no_conflict.json", *knowledge_result.config);

    ASSERT_TRUE(result.success) << result.error;
    EXPECT_TRUE(result.conflicts.empty());
}
