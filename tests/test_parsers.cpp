//
// Created by antihype on 04.08.2026.
//

#include <gtest/gtest.h>
#include <gpio/gpio_parser.h>
#include <gpio_knowledge.h>
#include <gpio_codegen.h>

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
