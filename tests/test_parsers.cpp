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
#include <timer16_parser.h>
#include <timer16_knowledge.h>
#include <timer16_contribution.h>
#include <i2c_parser.h>
#include <i2c_knowledge.h>
#include <timer32_parser.h>
#include <timer32_knowledge.h>
#include <spi_parser.h>
#include <spi_knowledge.h>
#include <rtc_parser.h>
#include <rtc_knowledge.h>
#include <rtc_contribution.h>
#include <crc_parser.h>
#include <crc_contribution.h>
#include <spifi_parser.h>
#include <spifi_contribution.h>
#include <eeprom_parser.h>
#include <eeprom_contribution.h>
#include <wdt_parser.h>
#include <wdt_contribution.h>
#include <dac_parser.h>
#include <dac_knowledge.h>
#include <dac_contribution.h>
#include <adc_parser.h>
#include <adc_contribution.h>
#include <crypto_parser.h>
#include <crypto_contribution.h>
#include <otp_parser.h>
#include <otp_contribution.h>
#include <tsens_parser.h>
#include <tsens_contribution.h>
#include <pvd_parser.h>
#include <pvd_contribution.h>
#include <bus_watchdog_parser.h>
#include <bus_watchdog_contribution.h>
#include <dma_parser.h>
#include <dma_contribution.h>
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
        knowledge.rtc = *loadRtcKnowledge("../knowledge_base/rtc.json").config;
        knowledge.dac = *loadDacKnowledge("../knowledge_base/dac.json").config;
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

// ============================================================================
// Timer16 — Тир 1 аудита (AUDIT_TZ_PERIPHERY_GAPS.md): clock_source (6 из 7
// вариантов HAL, минус EXTERNAL_INPUT1 — тот уже доступен через
// mode="external_event_count"), trigger_edge, external_clock_filter/
// trigger_filter, waveform_inverted.
// ============================================================================

TEST(Timer16Parse, DefaultsNewFieldsToPreviousHardcodedBehavior)
{
    ParseResult<Timer16Config> result = parseTimer16Config("fixtures/timer16_valid.json");

    ASSERT_TRUE(result.success) << result.error;
    EXPECT_EQ(result.config->clock_source, "internal_system");
    EXPECT_EQ(result.config->trigger_edge, "software");
    EXPECT_EQ(result.config->external_clock_filter, "none");
    EXPECT_EQ(result.config->trigger_filter, "none");
    EXPECT_FALSE(result.config->waveform_inverted);
}

TEST(Timer16Parse, ParsesClockSourceAhb)
{
    ParseResult<Timer16Config> result = parseTimer16Config("fixtures/timer16_clock_source_ahb.json");

    ASSERT_TRUE(result.success) << result.error;
    EXPECT_EQ(result.config->clock_source, "internal_ahb");
}

TEST(Timer16Parse, ParsesTriggerEdgeAndFilters)
{
    ParseResult<Timer16Config> result = parseTimer16Config("fixtures/timer16_trigger_and_filters.json");

    ASSERT_TRUE(result.success) << result.error;
    EXPECT_EQ(result.config->trigger_edge, "rising");
    EXPECT_EQ(result.config->external_clock_filter, "4clock");
    EXPECT_EQ(result.config->trigger_filter, "8clock");
}

TEST(Timer16Parse, ParsesWaveformInverted)
{
    ParseResult<Timer16Config> result = parseTimer16Config("fixtures/timer16_waveform_inverted.json");

    ASSERT_TRUE(result.success) << result.error;
    EXPECT_TRUE(result.config->waveform_inverted);
}

// docs/open_tasks.md "Timer16 — прерывания не реализованы".
TEST(Timer16Parse, ParsesOverflowAndCompareInterrupt)
{
    ParseResult<Timer16Config> result = parseTimer16Config("fixtures/timer16_overflow_and_compare_interrupt.json");

    ASSERT_TRUE(result.success) << result.error;
    EXPECT_TRUE(result.config->overflow_interrupt);
    EXPECT_TRUE(result.config->compare_interrupt);
}

TEST(Timer16Parse, RejectsCompareInterruptOutsidePeriodOrOneShot)
{
    ParseResult<Timer16Config> result = parseTimer16Config("fixtures/timer16_compare_interrupt_wrong_mode.json");

    EXPECT_FALSE(result.success);
}

// clock_source принимается и для external_event_count — парсер его не
// отклоняет (значение проверяется позже кодогеном, который его игнорирует,
// см. ProjectCodegen.Timer16IgnoresClockSourceForExternalEventCount).
TEST(Timer16Parse, AcceptsClockSourceWithExternalEventCountMode)
{
    ParseResult<Timer16Config> result =
        parseTimer16Config("fixtures/timer16_external_event_count_with_clock_source.json");

    ASSERT_TRUE(result.success) << result.error;
    EXPECT_EQ(result.config->mode, "external_event_count");
    EXPECT_EQ(result.config->clock_source, "internal_ahb");
}

TEST(Timer16Parse, RejectsUnknownClockSource)
{
    ParseResult<Timer16Config> result = parseTimer16Config("fixtures/timer16_invalid_clock_source.json");

    EXPECT_FALSE(result.success);
}

TEST(Timer16Parse, RejectsUnknownTriggerEdge)
{
    ParseResult<Timer16Config> result = parseTimer16Config("fixtures/timer16_invalid_trigger_edge.json");

    EXPECT_FALSE(result.success);
}

// trigger_source — добавлено 2026-10-22, выбор источника Trigger.Source
// (schemas/timer16.json, knowledge_base/timer16.json trigger_source_hal_enum).
TEST(Timer16Parse, DefaultsTriggerSourceToEmptyMeaningInstanceDefault)
{
    ParseResult<Timer16Config> result = parseTimer16Config("fixtures/timer16_valid.json");

    ASSERT_TRUE(result.success) << result.error;
    EXPECT_EQ(result.config->trigger_source, "");
}

TEST(Timer16Parse, ParsesExplicitTriggerSource)
{
    ParseResult<Timer16Config> result = parseTimer16Config("fixtures/timer16_trigger_source_explicit.json");

    ASSERT_TRUE(result.success) << result.error;
    EXPECT_EQ(result.config->trigger_source, "alarm");
}

TEST(Timer16Parse, RejectsUnknownTriggerSource)
{
    ParseResult<Timer16Config> result = parseTimer16Config("fixtures/timer16_invalid_trigger_source.json");

    EXPECT_FALSE(result.success);
}

TEST(Timer16Parse, RejectsUnknownFilterValue)
{
    ParseResult<Timer16Config> result = parseTimer16Config("fixtures/timer16_invalid_filter.json");

    EXPECT_FALSE(result.success);
}

// Clock.Source при external_event_count всегда EXTERNAL_INPUT1 — смысл
// режима в подсчёте внешних событий на этом входе, config.clock_source
// (здесь internal_ahb) принят парсером выше, но кодоген его не читает.
TEST(ProjectCodegen, Timer16IgnoresClockSourceForExternalEventCount)
{
    ParseResult<Timer16Config> config_result =
        parseTimer16Config("fixtures/timer16_external_event_count_with_clock_source.json");
    ASSERT_TRUE(config_result.success) << config_result.error;

    ParseResult<Timer16Knowledge> knowledge_result = loadTimer16Knowledge("../knowledge_base/timer16.json");
    ASSERT_TRUE(knowledge_result.success) << knowledge_result.error;

    ParseResult<PeripheralContribution> contribution_result =
        buildTimer16Contribution(*config_result.config, *knowledge_result.config);
    ASSERT_TRUE(contribution_result.success) << contribution_result.error;

    EXPECT_TRUE(contains(contribution_result.config->init_function_text,
        ".Clock.Source = TIMER16_SOURCE_EXTERNAL_INPUT1;"));
    EXPECT_FALSE(contains(contribution_result.config->init_function_text, "TIMER16_SOURCE_INTERNAL_AHB"));
}

// docs/open_tasks.md "Timer16 — прерывания не реализованы": overflow_interrupt/
// compare_interrupt переключают Start на _IT-вариант, пишут явную маску IER
// (ARRM|CMPM без ARROK/CMPOK) и генерируют IRQ-шаблон со сбросом обоих флагов.
TEST(ProjectCodegen, Timer16OverflowAndCompareInterruptUseStartItAndMask)
{
    ParseResult<Timer16Config> config_result =
        parseTimer16Config("fixtures/timer16_overflow_and_compare_interrupt.json");
    ASSERT_TRUE(config_result.success) << config_result.error;

    ParseResult<Timer16Knowledge> knowledge_result = loadTimer16Knowledge("../knowledge_base/timer16.json");
    ASSERT_TRUE(knowledge_result.success) << knowledge_result.error;

    ParseResult<PeripheralContribution> contribution_result =
        buildTimer16Contribution(*config_result.config, *knowledge_result.config);
    ASSERT_TRUE(contribution_result.success) << contribution_result.error;

    EXPECT_TRUE(contains(contribution_result.config->init_function_text, "HAL_Timer16_StartPWM_IT("));
    EXPECT_TRUE(contains(contribution_result.config->init_function_text,
        "HAL_Timer16_SetInterruptMask(&htimer16_1, TIMER16_IER_ARRMIE_M | TIMER16_IER_CMPMIE_M);"));
    EXPECT_TRUE(contains(contribution_result.config->helper_functions_text, "TIMER16_1_IRQHandler_Template"));
    EXPECT_TRUE(contains(contribution_result.config->helper_functions_text, "TIMER16_FLAG_ARRM"));
    EXPECT_TRUE(contains(contribution_result.config->helper_functions_text, "TIMER16_FLAG_CMPM"));
}

TEST(ProjectCodegen, Timer16WithoutInterruptsUsesPlainStartAndNoHelper)
{
    ParseResult<Timer16Config> config_result = parseTimer16Config("fixtures/timer16_valid.json");
    ASSERT_TRUE(config_result.success) << config_result.error;

    ParseResult<Timer16Knowledge> knowledge_result = loadTimer16Knowledge("../knowledge_base/timer16.json");
    ASSERT_TRUE(knowledge_result.success) << knowledge_result.error;

    ParseResult<PeripheralContribution> contribution_result =
        buildTimer16Contribution(*config_result.config, *knowledge_result.config);
    ASSERT_TRUE(contribution_result.success) << contribution_result.error;

    EXPECT_FALSE(contains(contribution_result.config->init_function_text, "_IT("));
    EXPECT_TRUE(contribution_result.config->helper_functions_text.empty());
}

// Waveform.Polarity игнорируется (но принят парсером) при
// external_event_count — Waveform.Enable там DISABLE (mode_waveform_enable).
TEST(ProjectCodegen, Timer16PeriodModeAppliesWaveformInverted)
{
    ParseResult<Timer16Config> config_result = parseTimer16Config("fixtures/timer16_waveform_inverted.json");
    ASSERT_TRUE(config_result.success) << config_result.error;

    ParseResult<Timer16Knowledge> knowledge_result = loadTimer16Knowledge("../knowledge_base/timer16.json");
    ASSERT_TRUE(knowledge_result.success) << knowledge_result.error;

    ParseResult<PeripheralContribution> contribution_result =
        buildTimer16Contribution(*config_result.config, *knowledge_result.config);
    ASSERT_TRUE(contribution_result.success) << contribution_result.error;

    EXPECT_TRUE(contains(contribution_result.config->init_function_text,
        ".Waveform.Polarity = TIMER16_WAVEFORM_POLARITY_INVERTED;"));
}

// trigger_source=alarm на instance=1 -> trigger_source_hal_enum["1"]["alarm"]
// = TIMER16_TRIGGER_AlARM (общий для всех instance код 0b111) — не дефолт
// instance_default_trigger_source (который был бы TIMER16_TRIGGER_TIM1_GPIO1_9).
TEST(ProjectCodegen, Timer16AppliesExplicitTriggerSource)
{
    ParseResult<Timer16Config> config_result = parseTimer16Config("fixtures/timer16_trigger_source_explicit.json");
    ASSERT_TRUE(config_result.success) << config_result.error;

    ParseResult<Timer16Knowledge> knowledge_result = loadTimer16Knowledge("../knowledge_base/timer16.json");
    ASSERT_TRUE(knowledge_result.success) << knowledge_result.error;

    ParseResult<PeripheralContribution> contribution_result =
        buildTimer16Contribution(*config_result.config, *knowledge_result.config);
    ASSERT_TRUE(contribution_result.success) << contribution_result.error;

    EXPECT_TRUE(contains(contribution_result.config->init_function_text,
        ".Trigger.Source = TIMER16_TRIGGER_AlARM;"));
    EXPECT_FALSE(contains(contribution_result.config->init_function_text, "TIMER16_TRIGGER_TIM1_GPIO1_9"));
}

// Без явного trigger_source — поведение без изменений (дефолт instance).
TEST(ProjectCodegen, Timer16WithoutTriggerSourceKeepsInstanceDefault)
{
    ParseResult<Timer16Config> config_result = parseTimer16Config("fixtures/timer16_waveform_inverted.json");
    ASSERT_TRUE(config_result.success) << config_result.error;

    ParseResult<Timer16Knowledge> knowledge_result = loadTimer16Knowledge("../knowledge_base/timer16.json");
    ASSERT_TRUE(knowledge_result.success) << knowledge_result.error;

    ParseResult<PeripheralContribution> contribution_result =
        buildTimer16Contribution(*config_result.config, *knowledge_result.config);
    ASSERT_TRUE(contribution_result.success) << contribution_result.error;

    EXPECT_TRUE(contains(contribution_result.config->init_function_text,
        ".Trigger.Source = TIMER16_TRIGGER_TIM1_GPIO1_9;"));
}

// AUDIT_TZ_PERIPHERY_GAPS.md, Timer16 (2026-10-14) — режим энкодера.
TEST(Timer16Parse, ParsesValidEncoderMode)
{
    ParseResult<Timer16Config> result = parseTimer16Config("fixtures/timer16_encoder_valid.json");

    ASSERT_TRUE(result.success) << result.error;
    EXPECT_EQ(result.config->mode, "encoder");
    EXPECT_EQ(result.config->prescaler, 1);
    EXPECT_EQ(result.config->arr, 999u);
}

// Датащит, раздел «Режим энкодера»: PRESC[2:0] обязан быть 0b000 (prescaler=1).
TEST(Timer16Parse, RejectsEncoderModeWithPrescalerNotOne)
{
    ParseResult<Timer16Config> result = parseTimer16Config("fixtures/timer16_encoder_prescaler_not_one.json");

    EXPECT_FALSE(result.success);
}

TEST(Timer16Parse, RejectsEncoderModeWithFrequencyHz)
{
    ParseResult<Timer16Config> result = parseTimer16Config("fixtures/timer16_encoder_frequency_hz_rejected.json");

    EXPECT_FALSE(result.success);
}

TEST(ProjectCodegen, Timer16EncoderModeSetsEncoderModeFieldAndCallsEncoderStart)
{
    ParseResult<Timer16Config> config_result = parseTimer16Config("fixtures/timer16_encoder_valid.json");
    ASSERT_TRUE(config_result.success) << config_result.error;

    ParseResult<Timer16Knowledge> knowledge_result = loadTimer16Knowledge("../knowledge_base/timer16.json");
    ASSERT_TRUE(knowledge_result.success) << knowledge_result.error;

    ParseResult<PeripheralContribution> contribution_result =
        buildTimer16Contribution(*config_result.config, *knowledge_result.config);
    ASSERT_TRUE(contribution_result.success) << contribution_result.error;

    const std::string& init_c = contribution_result.config->init_function_text;
    EXPECT_TRUE(contains(init_c, ".EncoderMode = TIMER16_ENCODER_ENABLE;"));
    EXPECT_TRUE(contains(init_c, "HAL_Timer16_Encoder_Start(&htimer16_1, 999);"));
    EXPECT_TRUE(contains(init_c, "Input1=GPIO_0/PIN_8, Input2=GPIO_0/PIN_9"));
}

// period/one_shot/external_event_count не должны неожиданно получить
// EncoderMode=ENABLE — явная регрессия-защита для веток, не относящихся к encoder.
TEST(ProjectCodegen, Timer16NonEncoderModeKeepsEncoderModeDisabled)
{
    ParseResult<Timer16Config> config_result = parseTimer16Config("fixtures/timer16_waveform_inverted.json");
    ASSERT_TRUE(config_result.success) << config_result.error;

    ParseResult<Timer16Knowledge> knowledge_result = loadTimer16Knowledge("../knowledge_base/timer16.json");
    ASSERT_TRUE(knowledge_result.success) << knowledge_result.error;

    ParseResult<PeripheralContribution> contribution_result =
        buildTimer16Contribution(*config_result.config, *knowledge_result.config);
    ASSERT_TRUE(contribution_result.success) << contribution_result.error;

    EXPECT_TRUE(contains(contribution_result.config->init_function_text,
        ".EncoderMode = TIMER16_ENCODER_DISABLE;"));
}

// Input2 (GPIO_0/PIN_9) занят Timer16_1 в режиме encoder — явный gpio-блок на
// том же пине должен конфликтовать (та же физическая коллизия, что и у
// ШИМ-вывода/SPI/Timer32, см. CrossModuleValidator.DetectsTimer32ChannelPinConflict).
TEST(CrossModuleValidator, DetectsTimer16EncoderInput2GpioConflict)
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
        "fixtures/cross_timer16_encoder_input2_gpio_conflict.json", *uart_knowledge_result.config,
        *timer16_knowledge_result.config, *i2c_knowledge_result.config, *timer32_knowledge_result.config,
        *spi_knowledge_result.config);

    ASSERT_TRUE(result.success) << result.error;
    ASSERT_EQ(result.conflicts.size(), 1u);
    EXPECT_EQ(result.conflicts[0].gpio_instance, 0);
    EXPECT_EQ(result.conflicts[0].pin, 9);
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

// Найдено на docs/examples/mik32-examples/HAL_ADC: первая перегенерация
// вендорского проекта (ни одного маркера USER CODE в исходнике) раньше
// стирала файл целиком. Теперь авторазбор должен сам разложить старый код
// по секциям Includes/Loop — без ручного переноса пользователем.
TEST(UserCodeMarkers, UnmarkedExistingFileIsAutoSplitNotErased)
{
    const std::string existing =
        "#include \"uart_lib.h\"\n"
        "ADC_HandleTypeDef hadc;\n"
        "int main() {\n"
        "    SystemClock_Config();\n"
        "    ADC_Init();\n"
        "    uint16_t v = 0;\n"
        "    /* это /* не настоящий конец */ комментарий с ловушкой */\n"
        "    while (1) { v = HAL_ADC_WaitAndGetValue(&hadc); xprintf(\"hi %u\\n\", v); }\n"
        "}\n"
        "void HAL_ADC_MspInit(ADC_HandleTypeDef *hadc) { /* заменено GPIO-блоками */ }\n";

    const std::string freshly_generated =
        "#include \"mik32_hal_pcc.h\"\n"
        "/* USER CODE BEGIN Includes */\n"
        "/* USER CODE END Includes */\n"
        "/* USER CODE BEGIN PV */\n"
        "/* USER CODE END PV */\n"
        "void SystemClock_Config(void);\n"
        "int main() {\n"
        "    SystemClock_Config();\n"
        "    GPIO_1_Init();\n"
        "    ADC_Init();\n"
        "    /* USER CODE BEGIN Init */\n"
        "    /* USER CODE END Init */\n"
        "    while (1) {\n"
        "        /* USER CODE BEGIN 3 */\n"
        "        /* USER CODE END 3 */\n"
        "    }\n"
        "}\n"
        "/* USER CODE BEGIN 4 */\n"
        "/* USER CODE END 4 */\n";

    const std::string merged = mergeUserCodeSections(freshly_generated, existing);

    // Цикл while — пользовательская логика — сохранён целиком в Loop.
    EXPECT_NE(merged.find("xprintf(\"hi %u\\n\", v)"), std::string::npos);
    // Чужой #include — в Includes.
    EXPECT_NE(merged.find("#include \"uart_lib.h\""), std::string::npos);
    // "ADC_Init();" внутри main() — уже регенерированный вызов, повторно
    // звать его не нужно, значит в Init его быть не должно.
    {
        const size_t init_begin = merged.find("USER CODE BEGIN Init");
        const size_t init_end = merged.find("USER CODE END Init");
        const std::string init_body = merged.substr(init_begin, init_end - init_begin);
        EXPECT_EQ(init_body.find("ADC_Init();"), std::string::npos);
    }
    // "uint16_t v = 0;" — не регенерированный вызов, должен попасть в Init.
    EXPECT_NE(merged.find("uint16_t v = 0;"), std::string::npos);
    // HAL_ADC_MspInit — замещён GPIO-блоками, не должен дублироваться.
    EXPECT_EQ(merged.find("HAL_ADC_MspInit"), std::string::npos);
    // Глобальная "ADC_HandleTypeDef hadc;" превращена в extern (определение
    // уже в mik32_init.c) — иначе "multiple definition" на линковке.
    EXPECT_NE(merged.find("extern ADC_HandleTypeDef hadc;"), std::string::npos);

    // Повторный прогон — уже обычные маркеры, идемпотентно.
    const std::string merged_again = mergeUserCodeSections(freshly_generated, merged);
    EXPECT_EQ(merged_again, merged);
}

// Если авторазбор не смог найти int main() хотя бы в одном из текстов —
// не угадываем структуру: заворачиваем файл целиком в "#if 0", но ничего не
// стираем.
TEST(UserCodeMarkers, UnparsableExistingFileFallsBackToIfZeroWrap)
{
    const std::string existing = "// совсем не main.c, просто кусок кода\nint x = 1;\n";

    const std::string freshly_generated =
        "/* USER CODE BEGIN 4 */\n"
        "/* USER CODE END 4 */\n";

    const std::string merged = mergeUserCodeSections(freshly_generated, existing);

    EXPECT_NE(merged.find("#if 0"), std::string::npos);
    EXPECT_NE(merged.find("int x = 1;"), std::string::npos);
}

// Баг найден 2026-10-19: BEGIN <N> с повреждённым/стёртым парным END <N> не
// матчился regex'ом и секция тихо считалась отсутствующей — пользовательский
// код внутри терялся без ошибки. Теперь — явный отказ (исключение), а не
// молчаливая потеря.
TEST(UserCodeMarkers, OrphanedBeginWithoutMatchingEndThrowsInsteadOfSilentlyDroppingContent)
{
    const std::string existing =
        "/* USER CODE BEGIN Includes */\n"
        "#include \"my_sensor.h\"\n"
        "/* USER CODE END Includes */\n"
        "/* USER CODE BEGIN Loop */\n"
        "my_sensor_poll();\n";
        // END Loop отсутствует/стёрт — ровно сценарий из CLAUDE.md.

    const std::string freshly_generated =
        "/* USER CODE BEGIN Includes */\n"
        "/* USER CODE END Includes */\n"
        "/* USER CODE BEGIN Loop */\n"
        "/* USER CODE END Loop */\n";

    EXPECT_THROW(mergeUserCodeSections(freshly_generated, existing), std::runtime_error);
}

// Риск, отмеченный самостоятельно после ed785ce: при двух хендлах одного
// типа старое сопоставление "вендор[i] <-> генератор[i]" по индексу
// объявления было бы неверным, если вендор объявил их в обратном порядке
// относительно генератора. Проверяем, что переименование идёт по номеру
// инстанса в хвосте имени (hi2c1 -> hi2c_1), а не по позиции в файле.
TEST(UserCodeMarkers, HandleRenameMatchesByInstanceNumberNotDeclarationOrder)
{
    const std::string init_c =
        "I2C_HandleTypeDef hi2c_0;\n"
        "I2C_HandleTypeDef hi2c_1;\n";

    // Вендор объявил хендлы в ОБРАТНОМ порядке относительно генератора.
    const std::string existing =
        "I2C_HandleTypeDef hi2c1;\n"
        "I2C_HandleTypeDef hi2c0;\n"
        "int main() {\n"
        "    uint8_t v1 = HAL_I2C_IsBusy(&hi2c1);\n"
        "    uint8_t v0 = HAL_I2C_IsBusy(&hi2c0);\n"
        "    while (1) {}\n"
        "}\n";

    const std::string freshly_generated =
        "/* USER CODE BEGIN PV */\n"
        "/* USER CODE END PV */\n"
        "int main() {\n"
        "    /* USER CODE BEGIN Init */\n"
        "    /* USER CODE END Init */\n"
        "    while (1) {\n"
        "        /* USER CODE BEGIN 3 */\n"
        "        /* USER CODE END 3 */\n"
        "    }\n"
        "}\n";

    const std::string merged = mergeUserCodeSections(freshly_generated, existing, init_c);

    // hi2c1 (инстанс 1) должен стать hi2c_1, а не hi2c_0 (позиционное
    // сопоставление по индексу объявления дало бы обратную, неверную пару).
    EXPECT_NE(merged.find("HAL_I2C_IsBusy(&hi2c_1)"), std::string::npos);
    EXPECT_NE(merged.find("HAL_I2C_IsBusy(&hi2c_0)"), std::string::npos);
}

// generated_projects/HAL_USART_IRQ_refix: вендорский "#define BUFFER_LENGTH
// 50" перед main() попадал в Includes (или в PV в составе переменной) вместо
// PD (Private Define) — по смыслу CubeMX-шаблона макросы лежат в PD, не
// среди #include.
TEST(UserCodeMarkers, VendorDefineBeforeMainGoesToPdNotIncludes)
{
    const std::string init_c = "";

    const std::string existing =
        "/*\n"
        " * Демонстрация.\n"
        " */\n"
        "\n"
        "#define BUFFER_LENGTH 50\n"
        "\n"
        "static char buf[BUFFER_LENGTH];\n"
        "int main() {\n"
        "    while (1) {}\n"
        "}\n";

    const std::string freshly_generated =
        "/* USER CODE BEGIN Includes */\n"
        "/* USER CODE END Includes */\n"
        "/* USER CODE BEGIN PD */\n"
        "/* USER CODE END PD */\n"
        "/* USER CODE BEGIN PV */\n"
        "/* USER CODE END PV */\n"
        "int main() {\n"
        "    /* USER CODE BEGIN Init */\n"
        "    /* USER CODE END Init */\n"
        "    while (1) {\n"
        "        /* USER CODE BEGIN 3 */\n"
        "        /* USER CODE END 3 */\n"
        "    }\n"
        "}\n";

    const std::string merged = mergeUserCodeSections(freshly_generated, existing, init_c);

    const size_t pd_begin = merged.find("USER CODE BEGIN PD");
    const size_t pd_end = merged.find("USER CODE END PD");
    const size_t includes_begin = merged.find("USER CODE BEGIN Includes");
    const size_t includes_end = merged.find("USER CODE END Includes");
    ASSERT_NE(pd_begin, std::string::npos);
    ASSERT_NE(pd_end, std::string::npos);

    EXPECT_NE(merged.substr(pd_begin, pd_end - pd_begin).find("#define BUFFER_LENGTH 50"), std::string::npos);
    EXPECT_EQ(merged.substr(includes_begin, includes_end - includes_begin).find("#define"), std::string::npos);
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

// Каждый вызов "<Label>_Init();" в main.c обёрнут СВОЕЙ парой USER CODE
// маркеров ("<label>_Call") — иначе правка, вписанная рядом с вызовом одной
// периферии, при перегенерации либо терялась бы (общий блок "USER CODE
// BEGIN 2" не привязан к конкретному вызову), либо относилась бы сразу ко
// всем вызовам.
TEST(ProjectCodegen, EachPeripheralInitCallHasItsOwnUserCodeMarkers)
{
    ParseResult<ProjectConfig> config_result = parseProjectConfig("fixtures/project_all_peripherals.json");
    ASSERT_TRUE(config_result.success) << config_result.error;

    ParseResult<GeneratedProjectFirmware> firmware_result =
        generateProjectFirmware(*config_result.config, loadProjectKnowledgeForTests());
    ASSERT_TRUE(firmware_result.success) << firmware_result.error;

    const std::string& main_c = firmware_result.config->main_c;
    EXPECT_TRUE(contains(main_c, "/* USER CODE BEGIN GPIO_2_Call */"));
    EXPECT_TRUE(contains(main_c, "/* USER CODE END GPIO_2_Call */"));
    EXPECT_TRUE(contains(main_c, "/* USER CODE BEGIN I2C_0_Call */"));
    EXPECT_TRUE(contains(main_c, "/* USER CODE END I2C_0_Call */"));

    // Ручной код, вписанный пользователем ВНУТРИ маркера на конкретную
    // периферию (I2C_0), должен пережить перегенерацию, а GPIO_2 остаться
    // нетронутым — проверяем, что это не общий блок на все вызовы сразу.
    const std::string hand_edited = mergeUserCodeSections(main_c,
        "/* USER CODE BEGIN I2C_0_Call */\n"
        "    I2C_0_Init();\n"
        "    // ручная диагностика после I2C\n"
        "/* USER CODE END I2C_0_Call */\n");
    EXPECT_TRUE(contains(hand_edited, "// ручная диагностика после I2C"));

    const std::string regenerated = mergeUserCodeSections(main_c, hand_edited);
    EXPECT_TRUE(contains(regenerated, "// ручная диагностика после I2C"));
    EXPECT_TRUE(contains(regenerated, "/* USER CODE BEGIN GPIO_2_Call */"));
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

// Тело while(1) всегда пустое под маркерами ТЗ §3.1.7.3 — конфигуратор
// настраивает периферию, а не пишет прикладную логику.
TEST(ProjectCodegen, EmitsEmptyLoopUnderUserCodeMarkers)
{
    ParseResult<ProjectConfig> config_result = parseProjectConfig("fixtures/project_all_peripherals.json");
    ASSERT_TRUE(config_result.success) << config_result.error;

    ParseResult<GeneratedProjectFirmware> firmware_result =
        generateProjectFirmware(*config_result.config, loadProjectKnowledgeForTests());
    ASSERT_TRUE(firmware_result.success) << firmware_result.error;

    const std::string& main_c = firmware_result.config->main_c;
    EXPECT_TRUE(contains(main_c, "        /* USER CODE BEGIN 3 */\n        /* USER CODE END 3 */\n"));
    EXPECT_FALSE(contains(main_c, "HAL_GPIO_TogglePin"));
    EXPECT_FALSE(contains(main_c, "xprintf"));
    // Инициализация при этом остаётся полной.
    EXPECT_TRUE(contains(main_c, "    I2C_0_Init();"));
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

// mik32_hal_timer32.h: HAL_TIMER32_CHANNEL_NoiseTypeDef Noise — поле общее для
// всех режимов канала, не только capture (реальный вендорный код
// controlFirmware/main.cpp задаёт его и на PWM-каналах). Раньше парсер читал
// noise_filter только внутри mode=="capture" — PWM-значение тихо терялось.
TEST(Timer32Parse, ParsesNoiseFilterOnPwmChannel)
{
    ParseResult<Timer32Config> result = parseTimer32Config("fixtures/timer32_channel_pwm.json");

    ASSERT_TRUE(result.success) << result.error;
    ASSERT_EQ(result.config->channels.size(), 1u);
    EXPECT_TRUE(result.config->channels[0].noise_filter);
}

// ТЗ §3.1.5: «Timer32_0 – базовый счётчик без режимов ШИМ, захвата и
// сравнения» — подтверждено HAL (все Channel_*-функции возвращают HAL_ERROR
// для TIMER32_0).
TEST(Timer32Parse, RejectsChannelsOnInstanceZero)
{
    ParseResult<Timer32Config> result = parseTimer32Config("fixtures/timer32_channels_on_instance_zero.json");

    EXPECT_FALSE(result.success);
}

// OVERNIGHT_FIELD_REFERENCE.md — Timer32: пересчёт частоты/скважности.
TEST(Timer32Parse, ComputesPrescalerTopFromFrequencyAndOcrFromDutyPercent)
{
    ParseResult<Timer32Config> result = parseTimer32Config("fixtures/timer32_frequency_duty_valid.json");

    ASSERT_TRUE(result.success) << result.error;
    EXPECT_TRUE(result.config->has_frequency);
    EXPECT_EQ(result.config->prescaler, 0u);
    EXPECT_EQ(result.config->top, 31999u);
    ASSERT_EQ(result.config->channels.size(), 1u);
    EXPECT_TRUE(result.config->channels[0].has_duty_percent);
    EXPECT_EQ(result.config->channels[0].ocr, 8000u);
}

TEST(Timer32Parse, RejectsFrequencyTogetherWithRawRegisters)
{
    ParseResult<Timer32Config> result = parseTimer32Config("fixtures/timer32_frequency_and_raw_registers_conflict.json");

    EXPECT_FALSE(result.success);
}

TEST(Timer32Parse, RejectsOcrTogetherWithDutyPercent)
{
    ParseResult<Timer32Config> result = parseTimer32Config("fixtures/timer32_ocr_and_duty_percent_conflict.json");

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

// AUDIT_TZ_PERIPHERY_GAPS.md, Timer32 — clock_source/overflow_interrupt/
// underflow_interrupt, необязательные поля, общие для любого instance.
TEST(Timer32Parse, DefaultsClockSourceToPrescalerWhenFieldAbsent)
{
    ParseResult<Timer32Config> result = parseTimer32Config("fixtures/timer32_base_counter.json");

    ASSERT_TRUE(result.success) << result.error;
    EXPECT_EQ(result.config->clock_source, "prescaler");
    EXPECT_FALSE(result.config->overflow_interrupt);
    EXPECT_FALSE(result.config->underflow_interrupt);
}

TEST(Timer32Parse, ParsesClockSourceTim1Hclk)
{
    ParseResult<Timer32Config> result = parseTimer32Config("fixtures/timer32_clock_source_tim1_hclk.json");

    ASSERT_TRUE(result.success) << result.error;
    EXPECT_EQ(result.config->clock_source, "tim1_hclk");
}

TEST(Timer32Parse, ParsesClockSourceTim2Lsi32k)
{
    ParseResult<Timer32Config> result = parseTimer32Config("fixtures/timer32_clock_source_tim2_lsi32k.json");

    ASSERT_TRUE(result.success) << result.error;
    EXPECT_EQ(result.config->clock_source, "tim2_lsi32k");
}

TEST(Timer32Parse, RejectsUnknownClockSource)
{
    ParseResult<Timer32Config> result = parseTimer32Config("fixtures/timer32_invalid_clock_source.json");

    EXPECT_FALSE(result.success);
}

TEST(Timer32Parse, ParsesOverflowInterruptOnBaseCounter)
{
    ParseResult<Timer32Config> result = parseTimer32Config("fixtures/timer32_base_counter_with_overflow_interrupt.json");

    ASSERT_TRUE(result.success) << result.error;
    EXPECT_TRUE(result.config->overflow_interrupt);
    EXPECT_FALSE(result.config->underflow_interrupt);
}

TEST(Timer32Parse, ParsesBothOverflowAndUnderflowInterrupts)
{
    ParseResult<Timer32Config> result = parseTimer32Config("fixtures/timer32_base_counter_with_both_interrupts.json");

    ASSERT_TRUE(result.success) << result.error;
    EXPECT_TRUE(result.config->overflow_interrupt);
    EXPECT_TRUE(result.config->underflow_interrupt);
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

// GPIO-блок с mode=analog явно отказывается от вывода в пользу периферии
// (vendor-пример controlFirmware: GPIO_PIN_3 -> HAL_GPIO_MODE_ANALOG, затем
// тот же пин используется Timer32 channel 3 PWM) — это не конфликт.
TEST(CrossModuleValidator, AnalogModeGpioDoesNotConflictWithTimer32Channel)
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
        "fixtures/cross_timer32_gpio_analog_no_conflict.json", *uart_knowledge_result.config,
        *timer16_knowledge_result.config, *i2c_knowledge_result.config, *timer32_knowledge_result.config,
        *spi_knowledge_result.config);

    ASSERT_TRUE(result.success) << result.error;
    EXPECT_EQ(result.conflicts.size(), 0u);
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
    // Noise — поле TIMER32_CHANNEL_HandleTypeDef общее для всех режимов
    // (mik32_hal_timer32.h:138), раньше кодоген писал .Noise только для
    // mode=capture — PWM-канал терял значение при каждой перегенерации.
    EXPECT_TRUE(contains(init_c, "htimer32_1_ch0.Noise = TIMER32_CHANNEL_FILTER_ON;"));
    EXPECT_TRUE(contains(init_c, "void TIMER32_1_IRQHandler_Template(void)"));
    // Errata §9.1, полный обход: читаем статус ДО очистки, отключаем маску
    // ТОЛЬКО сработавшего источника и сбрасываем ТОЛЬКО его флаг (не общий
    // HAL_Timer32_InterruptFlags_Clear — тот сбросил бы вообще все флаги разом).
    const size_t handler_at = init_c.find("void TIMER32_1_IRQHandler_Template(void)");
    ASSERT_NE(handler_at, std::string::npos);
    const size_t body_open = init_c.find('{', handler_at);
    const size_t flags_decl_at = init_c.find(
        "const uint32_t flags = HAL_Timer32_InterruptFlags_Get(&htimer32_1);", body_open);
    ASSERT_NE(flags_decl_at, std::string::npos);
    // Между открывающей скобкой и чтением статуса — только пустые строки:
    // чтение статуса первое, буквально по примеру errata (читает ДО очистки).
    const std::string between = init_c.substr(body_open + 1, flags_decl_at - (body_open + 1));
    EXPECT_TRUE(std::all_of(between.begin(), between.end(),
        [](char c) { return c == ' ' || c == '\n' || c == '\t' || c == '\r'; }));
    EXPECT_TRUE(contains(init_c, "HAL_Timer32_InterruptMask_Clear(&htimer32_1, TIMER32_INT_OVERFLOW_M);"));
    EXPECT_TRUE(contains(init_c, "HAL_Timer32_InterruptFlags_ClearMask(&htimer32_1, TIMER32_INT_OVERFLOW_M);"));
    EXPECT_TRUE(contains(init_c, "void TIMER32_1_ReenableAfterErrata91_Template(void)"));
    EXPECT_TRUE(contains(init_c, "HAL_Timer32_InterruptMask_Set(&htimer32_1, TIMER32_INT_OVERFLOW_M);"));
}

// AUDIT_TZ_PERIPHERY_GAPS.md, Timer32 — clock_source: без поля поведение не
// меняется (старый хардкод TIMER32_SOURCE_PRESCALER).
TEST(ProjectCodegen, Timer32WithoutClockSourceFieldStaysOnPrescaler)
{
    ParseResult<ProjectConfig> config_result = parseProjectConfig("fixtures/timer32_base_counter.json");
    ASSERT_TRUE(config_result.success) << config_result.error;

    ParseResult<GeneratedProjectFirmware> firmware_result =
        generateProjectFirmware(*config_result.config, loadProjectKnowledgeForTests());
    ASSERT_TRUE(firmware_result.success) << firmware_result.error;

    EXPECT_TRUE(contains(firmware_result.config->mik32_init_c, "htimer32_0.Clock.Source = TIMER32_SOURCE_PRESCALER;"));
}

TEST(ProjectCodegen, Timer32ClockSourceTim1HclkIsKnownFrequency)
{
    ParseResult<ProjectConfig> config_result = parseProjectConfig("fixtures/timer32_clock_source_tim1_hclk.json");
    ASSERT_TRUE(config_result.success) << config_result.error;

    ParseResult<GeneratedProjectFirmware> firmware_result =
        generateProjectFirmware(*config_result.config, loadProjectKnowledgeForTests());
    ASSERT_TRUE(firmware_result.success) << firmware_result.error;

    const std::string& init_c = firmware_result.config->mik32_init_c;
    EXPECT_TRUE(contains(init_c, "htimer32_0.Clock.Source = TIMER32_SOURCE_TIM1_HCLK;"));
    EXPECT_TRUE(contains(init_c, "Расчётная частота счёта"));
}

// clock_source=tim2_lsi32k — внешний/некалиброванный источник, частота не
// должна выдумываться (тот же принцип, что у Timer16 osc32k/lsi32k).
TEST(ProjectCodegen, Timer32ClockSourceTim2Lsi32kDoesNotComputeFrequency)
{
    ParseResult<ProjectConfig> config_result = parseProjectConfig("fixtures/timer32_clock_source_tim2_lsi32k.json");
    ASSERT_TRUE(config_result.success) << config_result.error;

    ParseResult<GeneratedProjectFirmware> firmware_result =
        generateProjectFirmware(*config_result.config, loadProjectKnowledgeForTests());
    ASSERT_TRUE(firmware_result.success) << firmware_result.error;

    const std::string& init_c = firmware_result.config->mik32_init_c;
    EXPECT_TRUE(contains(init_c, "htimer32_0.Clock.Source = TIMER32_SOURCE_TIM2_LSI32K;"));
    EXPECT_TRUE(contains(init_c, "частота счёта не вычислена"));
    EXPECT_FALSE(contains(init_c, "Расчётная частота счёта"));
}

// AUDIT_TZ_PERIPHERY_GAPS.md, Timer32 — overflow_interrupt на базовом
// счётчике (раньше такого способа не было вообще, только HAL_Timer32_Start).
TEST(ProjectCodegen, Timer32BaseCounterWithOverflowInterruptUsesStartIT)
{
    ParseResult<ProjectConfig> config_result = parseProjectConfig("fixtures/timer32_base_counter_with_overflow_interrupt.json");
    ASSERT_TRUE(config_result.success) << config_result.error;

    ParseResult<GeneratedProjectFirmware> firmware_result =
        generateProjectFirmware(*config_result.config, loadProjectKnowledgeForTests());
    ASSERT_TRUE(firmware_result.success) << firmware_result.error;

    const std::string& init_c = firmware_result.config->mik32_init_c;
    EXPECT_TRUE(contains(init_c, "HAL_Timer32_Start_IT(&htimer32_0, TIMER32_INT_OVERFLOW_M);"));
    EXPECT_TRUE(contains(init_c, "void TIMER32_0_IRQHandler_Template(void)"));
    EXPECT_TRUE(contains(init_c, "void TIMER32_0_ReenableAfterErrata91_Template(void)"));
    EXPECT_FALSE(contains(init_c, "HAL_Timer32_Start(&htimer32_0);"));
}

TEST(ProjectCodegen, Timer32BaseCounterWithBothInterruptsCombinesMask)
{
    ParseResult<ProjectConfig> config_result = parseProjectConfig("fixtures/timer32_base_counter_with_both_interrupts.json");
    ASSERT_TRUE(config_result.success) << config_result.error;

    ParseResult<GeneratedProjectFirmware> firmware_result =
        generateProjectFirmware(*config_result.config, loadProjectKnowledgeForTests());
    ASSERT_TRUE(firmware_result.success) << firmware_result.error;

    EXPECT_TRUE(contains(firmware_result.config->mik32_init_c,
        "HAL_Timer32_Start_IT(&htimer32_1, TIMER32_INT_OVERFLOW_M | TIMER32_INT_UNDERFLOW_M);"));
}

// Канал уже включает OVERFLOW сам (PWM_Start_IT) — underflow_interrupt
// добавляется дополнительным вызовом HAL_Timer32_InterruptMask_Set.
TEST(ProjectCodegen, Timer32ChannelWithExtraUnderflowInterruptAddsSeparateMaskCall)
{
    ParseResult<ProjectConfig> config_result = parseProjectConfig("fixtures/timer32_channel_pwm_with_underflow_interrupt.json");
    ASSERT_TRUE(config_result.success) << config_result.error;

    ParseResult<GeneratedProjectFirmware> firmware_result =
        generateProjectFirmware(*config_result.config, loadProjectKnowledgeForTests());
    ASSERT_TRUE(firmware_result.success) << firmware_result.error;

    const std::string& init_c = firmware_result.config->mik32_init_c;
    EXPECT_TRUE(contains(init_c, "HAL_Timer32_PWM_Start_IT(&htimer32_1, &htimer32_1_ch0);"));
    EXPECT_TRUE(contains(init_c, "HAL_Timer32_InterruptMask_Set(&htimer32_1, TIMER32_INT_UNDERFLOW_M);"));
}

// Errata §9.1, обход с ДВУМЯ независимыми источниками (ШИМ-канал делит
// TIMER32_INT_OVERFLOW_M + отдельный underflow_interrupt) — именно тот
// случай, где важно не продублировать overflow дважды в списке источников
// и не дать одному сработавшему источнику затереть флаг другого.
TEST(ProjectCodegen, Timer32Errata91WorkaroundHandlesTwoIndependentSources)
{
    ParseResult<ProjectConfig> config_result = parseProjectConfig("fixtures/timer32_channel_pwm_with_underflow_interrupt.json");
    ASSERT_TRUE(config_result.success) << config_result.error;

    ParseResult<GeneratedProjectFirmware> firmware_result =
        generateProjectFirmware(*config_result.config, loadProjectKnowledgeForTests());
    ASSERT_TRUE(firmware_result.success) << firmware_result.error;

    const std::string& init_c = firmware_result.config->mik32_init_c;
    ASSERT_TRUE(contains(init_c, "void TIMER32_1_IRQHandler_Template(void)"));
    ASSERT_TRUE(contains(init_c, "void TIMER32_1_ReenableAfterErrata91_Template(void)"));

    // Ровно один источник OVERFLOW (не продублирован ШИМ-каналом и
    // underflow_interrupt отдельно) — считаем вхождения mask_expr в условии "if".
    const size_t overflow_if_count = [&]()
    {
        size_t count = 0, pos = 0;
        while ((pos = init_c.find("if (flags & TIMER32_INT_OVERFLOW_M)", pos)) != std::string::npos)
        {
            ++count;
            pos += 1;
        }
        return count;
    }();
    EXPECT_EQ(overflow_if_count, 1u);

    EXPECT_TRUE(contains(init_c, "HAL_Timer32_InterruptMask_Clear(&htimer32_1, TIMER32_INT_OVERFLOW_M);"));
    EXPECT_TRUE(contains(init_c, "HAL_Timer32_InterruptFlags_ClearMask(&htimer32_1, TIMER32_INT_OVERFLOW_M);"));
    EXPECT_TRUE(contains(init_c, "HAL_Timer32_InterruptMask_Clear(&htimer32_1, TIMER32_INT_UNDERFLOW_M);"));
    EXPECT_TRUE(contains(init_c, "HAL_Timer32_InterruptFlags_ClearMask(&htimer32_1, TIMER32_INT_UNDERFLOW_M);"));

    // Повторное включение в шаблоне для основного цикла — оба источника,
    // каждый под своей отдельной проверкой "флаг реально нулевой".
    const size_t reenable_at = init_c.find("void TIMER32_1_ReenableAfterErrata91_Template(void)");
    const std::string reenable_body = init_c.substr(reenable_at);
    EXPECT_TRUE(contains(reenable_body, "if ((flags & TIMER32_INT_OVERFLOW_M) == 0)"));
    EXPECT_TRUE(contains(reenable_body, "if ((flags & TIMER32_INT_UNDERFLOW_M) == 0)"));
}

// ============================================================================
// RTC — AUDIT_TZ_PERIPHERY_GAPS.md, Тир 2. Единственный глобальный блок
// (нет instance). mik32_hal_rtc.h/.c + оба вендорных примера Hal_RTC/Hal_RTC_IRQ.
// ============================================================================

TEST(RtcParse, ParsesValidConfigWithoutAlarm)
{
    ParseResult<RtcConfig> result = parseRtcConfig("fixtures/rtc_valid_no_alarm.json");

    ASSERT_TRUE(result.success) << result.error;
    EXPECT_EQ(result.config->dow, 2);
    EXPECT_EQ(result.config->hours, 20);
    EXPECT_EQ(result.config->century, 20);
    EXPECT_EQ(result.config->day, 19);
    EXPECT_EQ(result.config->month, 7);
    EXPECT_FALSE(result.config->has_alarm);
}

TEST(RtcParse, ParsesAlarmWithDefaultFullCompareFields)
{
    ParseResult<RtcConfig> result = parseRtcConfig("fixtures/rtc_valid_with_alarm_full_compare.json");

    ASSERT_TRUE(result.success) << result.error;
    ASSERT_TRUE(result.config->has_alarm);
    EXPECT_EQ(result.config->alarm_seconds, 5);
    ASSERT_EQ(result.config->alarm_compare_fields.size(), 8u);
    EXPECT_TRUE(result.config->alarm_interrupt_enabled);
}

TEST(RtcParse, ParsesAlarmWithPartialCompareFields)
{
    ParseResult<RtcConfig> result = parseRtcConfig("fixtures/rtc_valid_with_alarm_partial_compare.json");

    ASSERT_TRUE(result.success) << result.error;
    ASSERT_TRUE(result.config->has_alarm);
    ASSERT_EQ(result.config->alarm_compare_fields.size(), 3u);
    EXPECT_FALSE(result.config->alarm_interrupt_enabled);
}

// Датащит/HAL RTC_DateTypeDef: день месяца валидируется относительно
// month/year с учётом високосного года — 2023 не високосный, 29 февраля нет.
TEST(RtcParse, RejectsFeb29OnNonLeapYear)
{
    ParseResult<RtcConfig> result = parseRtcConfig("fixtures/rtc_invalid_feb29_non_leap.json");

    EXPECT_FALSE(result.success);
}

TEST(RtcParse, AcceptsFeb29OnLeapYear)
{
    ParseResult<RtcConfig> result = parseRtcConfig("fixtures/rtc_valid_feb29_leap.json");

    ASSERT_TRUE(result.success) << result.error;
    EXPECT_EQ(result.config->day, 29);
}

TEST(RtcParse, RejectsDowOutOfRange)
{
    ParseResult<RtcConfig> result = parseRtcConfig("fixtures/rtc_invalid_dow_out_of_range.json");

    EXPECT_FALSE(result.success);
}

TEST(RtcParse, RejectsHoursOutOfRange)
{
    ParseResult<RtcConfig> result = parseRtcConfig("fixtures/rtc_invalid_hours_out_of_range.json");

    EXPECT_FALSE(result.success);
}

// RTC — единственный глобальный блок (нет instance) — второй блок type="rtc"
// в одном проекте не имеет физического смысла.
TEST(ProjectConfigParse, RejectsSecondRtcBlock)
{
    ParseResult<ProjectConfig> result = parseProjectConfig("fixtures/rtc_two_blocks_rejected.json");

    EXPECT_FALSE(result.success);
}

TEST(ProjectCodegen, RtcWithoutAlarmSetsTimeAndDateOnly)
{
    ParseResult<ProjectConfig> config_result = parseProjectConfig("fixtures/rtc_valid_no_alarm.json");
    ASSERT_TRUE(config_result.success) << config_result.error;

    ParseResult<GeneratedProjectFirmware> firmware_result =
        generateProjectFirmware(*config_result.config, loadProjectKnowledgeForTests());
    ASSERT_TRUE(firmware_result.success) << firmware_result.error;

    const std::string& init_c = firmware_result.config->mik32_init_c;
    EXPECT_TRUE(contains(init_c, "hrtc.Instance = RTC;"));
    EXPECT_TRUE(contains(init_c, "sTime.Dow = RTC_WEEKDAY_TUESDAY;"));
    EXPECT_TRUE(contains(init_c, "sDate.Month = RTC_MONTH_JULY;"));
    EXPECT_TRUE(contains(init_c, "HAL_RTC_Enable(&hrtc);"));
    EXPECT_FALSE(contains(init_c, "RTC_AlarmTypeDef"));
    EXPECT_FALSE(contains(init_c, "HAL_RTC_InterruptInit"));
}

TEST(ProjectCodegen, RtcWithFullCompareAlarmSetsAllMaskBitsAndInterrupt)
{
    ParseResult<ProjectConfig> config_result = parseProjectConfig("fixtures/rtc_valid_with_alarm_full_compare.json");
    ASSERT_TRUE(config_result.success) << config_result.error;

    ParseResult<GeneratedProjectFirmware> firmware_result =
        generateProjectFirmware(*config_result.config, loadProjectKnowledgeForTests());
    ASSERT_TRUE(firmware_result.success) << firmware_result.error;

    const std::string& init_c = firmware_result.config->mik32_init_c;
    EXPECT_TRUE(contains(init_c, "sAlarm.MaskAlarmTime = RTC_TALRM_CDOW_M | RTC_TALRM_CH_M | RTC_TALRM_CM_M | RTC_TALRM_CS_M;"));
    EXPECT_TRUE(contains(init_c, "sAlarm.MaskAlarmDate = RTC_DALRM_CC_M | RTC_DALRM_CD_M | RTC_DALRM_CM_M | RTC_DALRM_CY_M;"));
    EXPECT_TRUE(contains(init_c, "hrtc.Interrupts.Alarm = RTC_ALARM_IRQ_ENABLE;"));
    EXPECT_TRUE(contains(init_c, "HAL_RTC_InterruptInit(&hrtc);"));
    EXPECT_TRUE(contains(init_c, "Errata п. 6.1"));
    // Найдено на реальной плате: обработчик обязан звать HAL_RTC_ClearAlrmFlag,
    // иначе уровневое прерывание вешает прошивку в момент срабатывания.
    EXPECT_TRUE(contains(init_c, "void RTC_IRQHandler_Template(void)"));
    EXPECT_TRUE(contains(init_c, "HAL_RTC_ClearAlrmFlag(&hrtc);"));
}

TEST(ProjectCodegen, RtcWithPartialCompareAlarmOnlyMasksSelectedFields)
{
    ParseResult<ProjectConfig> config_result = parseProjectConfig("fixtures/rtc_valid_with_alarm_partial_compare.json");
    ASSERT_TRUE(config_result.success) << config_result.error;

    ParseResult<GeneratedProjectFirmware> firmware_result =
        generateProjectFirmware(*config_result.config, loadProjectKnowledgeForTests());
    ASSERT_TRUE(firmware_result.success) << firmware_result.error;

    const std::string& init_c = firmware_result.config->mik32_init_c;
    EXPECT_TRUE(contains(init_c, "sAlarm.MaskAlarmTime = RTC_TALRM_CH_M | RTC_TALRM_CM_M | RTC_TALRM_CS_M;"));
    EXPECT_TRUE(contains(init_c, "sAlarm.MaskAlarmDate = 0;"));
    EXPECT_FALSE(contains(init_c, "HAL_RTC_InterruptInit"));
    EXPECT_FALSE(contains(init_c, "RTC_IRQHandler_Template"));
}

// ============================================================================
// CRC — AUDIT_TZ_PERIPHERY_GAPS.md, Тир 2. Единственный глобальный блок
// (нет instance). mik32_hal_crc32.h/.c + вендорный пример HAL_CRC32.
// ============================================================================

TEST(CrcParse, ParsesValidRawFields)
{
    ParseResult<CrcConfig> result = parseCrcConfig("fixtures/crc_valid_raw_fields.json");

    ASSERT_TRUE(result.success) << result.error;
    EXPECT_EQ(result.config->poly, 305419896u);
    EXPECT_EQ(result.config->init, 4294967295u);
    EXPECT_TRUE(result.config->input_reverse);
    EXPECT_FALSE(result.config->output_reverse);
    EXPECT_TRUE(result.config->output_inversion);
    EXPECT_TRUE(result.config->requested_preset.empty());
}

// Поля preset и mik32_hal_crc32.h (строки 240-283): CRC-32Q — poly=0x814141AB,
// init=0, input/output_reverse=false, output_inversion=off.
TEST(CrcParse, ParsesPresetCrc32Q)
{
    ParseResult<CrcConfig> result = parseCrcConfig("fixtures/crc_valid_preset_crc32q.json");

    ASSERT_TRUE(result.success) << result.error;
    EXPECT_EQ(result.config->poly, 0x814141ABu);
    EXPECT_EQ(result.config->init, 0u);
    EXPECT_FALSE(result.config->input_reverse);
    EXPECT_FALSE(result.config->output_reverse);
    EXPECT_FALSE(result.config->output_inversion);
    EXPECT_EQ(result.config->requested_preset, "crc32q");
}

TEST(CrcParse, ParsesPresetCrc32)
{
    ParseResult<CrcConfig> result = parseCrcConfig("fixtures/crc_valid_preset_crc32.json");

    ASSERT_TRUE(result.success) << result.error;
    EXPECT_EQ(result.config->poly, 0x04C11DB7u);
    EXPECT_EQ(result.config->init, 0xFFFFFFFFu);
    EXPECT_TRUE(result.config->input_reverse);
    EXPECT_TRUE(result.config->output_reverse);
    EXPECT_TRUE(result.config->output_inversion);
}

TEST(CrcParse, RejectsPresetTogetherWithRawFields)
{
    ParseResult<CrcConfig> result = parseCrcConfig("fixtures/crc_invalid_preset_and_raw_fields.json");

    EXPECT_FALSE(result.success);
}

TEST(CrcParse, RejectsMissingEverything)
{
    ParseResult<CrcConfig> result = parseCrcConfig("fixtures/crc_invalid_missing_everything.json");

    EXPECT_FALSE(result.success);
}

TEST(ProjectConfigParse, RejectsSecondCrcBlock)
{
    ParseResult<ProjectConfig> result = parseProjectConfig("fixtures/crc_two_blocks_rejected.json");

    EXPECT_FALSE(result.success);
}

TEST(ProjectCodegen, CrcRawFieldsGenerateExpectedInitCode)
{
    ParseResult<ProjectConfig> config_result = parseProjectConfig("fixtures/crc_valid_raw_fields.json");
    ASSERT_TRUE(config_result.success) << config_result.error;

    ParseResult<GeneratedProjectFirmware> firmware_result =
        generateProjectFirmware(*config_result.config, loadProjectKnowledgeForTests());
    ASSERT_TRUE(firmware_result.success) << firmware_result.error;

    const std::string& init_c = firmware_result.config->mik32_init_c;
    EXPECT_TRUE(contains(init_c, "hcrc.Instance = CRC;"));
    EXPECT_TRUE(contains(init_c, "hcrc.Poly = 0x12345678;"));
    EXPECT_TRUE(contains(init_c, "hcrc.Init = 0xffffffff;"));
    EXPECT_TRUE(contains(init_c, "hcrc.InputReverse = CRC_REFIN_TRUE;"));
    EXPECT_TRUE(contains(init_c, "hcrc.OutputReverse = CRC_REFOUT_FALSE;"));
    EXPECT_TRUE(contains(init_c, "hcrc.OutputInversion = CRC_OUTPUTINVERSION_ON;"));
    EXPECT_TRUE(contains(init_c, "HAL_CRC_Init(&hcrc);"));
}

TEST(ProjectCodegen, CrcPresetGeneratesCommentNamingThePreset)
{
    ParseResult<ProjectConfig> config_result = parseProjectConfig("fixtures/crc_valid_preset_crc32q.json");
    ASSERT_TRUE(config_result.success) << config_result.error;

    ParseResult<GeneratedProjectFirmware> firmware_result =
        generateProjectFirmware(*config_result.config, loadProjectKnowledgeForTests());
    ASSERT_TRUE(firmware_result.success) << firmware_result.error;

    const std::string& init_c = firmware_result.config->mik32_init_c;
    EXPECT_TRUE(contains(init_c, "hcrc.Poly = 0x814141ab;"));
    EXPECT_TRUE(contains(init_c, "preset=\"crc32q\""));
}

// ============================================================================
// SPIFI — контроллер внешней памяти. Единственный глобальный блок (нет
// instance). Покрывает только Memory Mode (HAL_SPIFI_MemoryMode_Init,
// mik32_hal_spifi.h/.c).
// ============================================================================

TEST(SpifiParse, ParsesValid)
{
    ParseResult<SpifiConfig> result = parseSpifiConfig("fixtures/spifi_valid.json");

    ASSERT_TRUE(result.success) << result.error;
    EXPECT_TRUE(result.config->cache_enabled);
    EXPECT_EQ(result.config->cache_limit, 2415919103u);
    EXPECT_EQ(result.config->cmd_frame_form, "opcode_3addr");
    EXPECT_EQ(result.config->cmd_field_form, "all_serial");
    EXPECT_EQ(result.config->cmd_op_code, 3);
    EXPECT_EQ(result.config->cmd_interim_length, 0);
}

TEST(SpifiParse, RejectsMissingEverything)
{
    ParseResult<SpifiConfig> result = parseSpifiConfig("fixtures/spifi_invalid_missing_everything.json");

    EXPECT_FALSE(result.success);
}

TEST(SpifiParse, RejectsUnknownFrameForm)
{
    ParseResult<SpifiConfig> result = parseSpifiConfig("fixtures/spifi_invalid_bad_frame_form.json");

    EXPECT_FALSE(result.success);
}

TEST(ProjectConfigParse, RejectsSecondSpifiBlock)
{
    ParseResult<ProjectConfig> result = parseProjectConfig("fixtures/spifi_two_blocks_rejected.json");

    EXPECT_FALSE(result.success);
}

TEST(ProjectCodegen, SpifiGeneratesExpectedInitCode)
{
    ParseResult<ProjectConfig> config_result = parseProjectConfig("fixtures/spifi_valid.json");
    ASSERT_TRUE(config_result.success) << config_result.error;

    ParseResult<GeneratedProjectFirmware> firmware_result =
        generateProjectFirmware(*config_result.config, loadProjectKnowledgeForTests());
    ASSERT_TRUE(firmware_result.success) << firmware_result.error;

    const std::string& init_c = firmware_result.config->mik32_init_c;
    EXPECT_TRUE(contains(init_c, "hspifi.Instance = SPIFI_CONFIG;"));
    EXPECT_TRUE(contains(init_c, "hspifi.CacheEnable = SPIFI_CACHE_ENABLE;"));
    EXPECT_TRUE(contains(init_c, "hspifi.CacheLimit = 0x8fffffff;"));
    EXPECT_TRUE(contains(init_c, "hspifi.Command.InterimLength = 0;"));
    EXPECT_TRUE(contains(init_c, "hspifi.Command.FieldForm = SPIFI_FIELDFORM_ALL_SERIAL;"));
    EXPECT_TRUE(contains(init_c, "hspifi.Command.FrameForm = SPIFI_FRAMEFORM_OPCODE_3ADDR;"));
    EXPECT_TRUE(contains(init_c, "hspifi.Command.OpCode = 0x3;"));
    EXPECT_TRUE(contains(init_c, "HAL_SPIFI_MemoryMode_Init(&hspifi);"));
}

// ============================================================================
// EEPROM — AUDIT_TZ_PERIPHERY_GAPS.md, Тир 2. Единственный глобальный блок
// (нет instance). mik32_hal_eeprom.h/.c + вендорный пример HAL_EEPROM.
// ============================================================================

TEST(EepromParse, ParsesValidTwoStageWithDefaults)
{
    ParseResult<EepromConfig> result = parseEepromConfig("fixtures/eeprom_valid_two_stage.json");

    ASSERT_TRUE(result.success) << result.error;
    EXPECT_EQ(result.config->mode, "two_stage");
    EXPECT_TRUE(result.config->ecc_enabled);
    EXPECT_FALSE(result.config->single_error_interrupt_enabled);
}

TEST(EepromParse, ParsesThreeStageWithCustomFields)
{
    ParseResult<EepromConfig> result = parseEepromConfig("fixtures/eeprom_valid_three_stage_custom.json");

    ASSERT_TRUE(result.success) << result.error;
    EXPECT_EQ(result.config->mode, "three_stage");
    EXPECT_FALSE(result.config->ecc_enabled);
    EXPECT_TRUE(result.config->single_error_interrupt_enabled);
}

TEST(EepromParse, RejectsUnknownMode)
{
    ParseResult<EepromConfig> result = parseEepromConfig("fixtures/eeprom_invalid_mode.json");

    EXPECT_FALSE(result.success);
}

TEST(EepromParse, RejectsMissingMode)
{
    ParseResult<EepromConfig> result = parseEepromConfig("fixtures/eeprom_missing_mode.json");

    EXPECT_FALSE(result.success);
}

TEST(ProjectConfigParse, RejectsSecondEepromBlock)
{
    ParseResult<ProjectConfig> result = parseProjectConfig("fixtures/eeprom_two_blocks_rejected.json");

    EXPECT_FALSE(result.success);
}

TEST(ProjectCodegen, EepromTwoStageDefaultsGenerateExpectedInitCode)
{
    ParseResult<ProjectConfig> config_result = parseProjectConfig("fixtures/eeprom_valid_two_stage.json");
    ASSERT_TRUE(config_result.success) << config_result.error;

    ParseResult<GeneratedProjectFirmware> firmware_result =
        generateProjectFirmware(*config_result.config, loadProjectKnowledgeForTests());
    ASSERT_TRUE(firmware_result.success) << firmware_result.error;

    const std::string& init_c = firmware_result.config->mik32_init_c;
    EXPECT_TRUE(contains(init_c, "heeprom.Instance = EEPROM_REGS;"));
    EXPECT_TRUE(contains(init_c, "heeprom.Mode = HAL_EEPROM_MODE_TWO_STAGE;"));
    EXPECT_TRUE(contains(init_c, "heeprom.ErrorCorrection = HAL_EEPROM_ECC_ENABLE;"));
    EXPECT_TRUE(contains(init_c, "heeprom.EnableInterrupt = HAL_EEPROM_SERR_DISABLE;"));
    EXPECT_TRUE(contains(init_c, "HAL_EEPROM_Init(&heeprom);"));
    EXPECT_TRUE(contains(init_c, "HAL_EEPROM_CalculateTimings(&heeprom, 32000000);"));
}

TEST(ProjectCodegen, EepromThreeStageCustomFieldsFlipAllThreeConstants)
{
    ParseResult<ProjectConfig> config_result = parseProjectConfig("fixtures/eeprom_valid_three_stage_custom.json");
    ASSERT_TRUE(config_result.success) << config_result.error;

    ParseResult<GeneratedProjectFirmware> firmware_result =
        generateProjectFirmware(*config_result.config, loadProjectKnowledgeForTests());
    ASSERT_TRUE(firmware_result.success) << firmware_result.error;

    const std::string& init_c = firmware_result.config->mik32_init_c;
    EXPECT_TRUE(contains(init_c, "heeprom.Mode = HAL_EEPROM_MODE_THREE_STAGE;"));
    EXPECT_TRUE(contains(init_c, "heeprom.ErrorCorrection = HAL_EEPROM_ECC_DISABLE;"));
    EXPECT_TRUE(contains(init_c, "heeprom.EnableInterrupt = HAL_EEPROM_SERR_ENABLE;"));
}

// ============================================================================
// WDT — AUDIT_TZ_PERIPHERY_GAPS.md, Тир 2. Единственный глобальный блок
// (нет instance). mik32_hal_wdt.h/.c + вендорный пример HAL_WDT.
// ============================================================================

TEST(WdtParse, ParsesValid32MClockSource)
{
    ParseResult<WdtConfig> result = parseWdtConfig("fixtures/wdt_valid_32m.json");

    ASSERT_TRUE(result.success) << result.error;
    EXPECT_EQ(result.config->clock_source, "osc32m");
    EXPECT_EQ(result.config->reload_ms, 500u);
}

TEST(WdtParse, ParsesValid32KClockSource)
{
    ParseResult<WdtConfig> result = parseWdtConfig("fixtures/wdt_valid_32k.json");

    ASSERT_TRUE(result.success) << result.error;
    EXPECT_EQ(result.config->clock_source, "osc32k");
    EXPECT_EQ(result.config->reload_ms, 100000u);
}

// mik32_hal_wdt.h: WDT_MAX_VALUE_CLOCK32M=524мс — 600 превышает для 32МГц-источников.
TEST(WdtParse, RejectsReloadMsTooHighFor32MClockSource)
{
    ParseResult<WdtConfig> result = parseWdtConfig("fixtures/wdt_invalid_reload_ms_too_high_for_32m.json");

    EXPECT_FALSE(result.success);
}

// WDT_MAX_VALUE_CLOCK32K=511875мс — 600000 превышает даже для 32кГц-источников.
TEST(WdtParse, RejectsReloadMsTooHighFor32KClockSource)
{
    ParseResult<WdtConfig> result = parseWdtConfig("fixtures/wdt_invalid_reload_ms_too_high_for_32k.json");

    EXPECT_FALSE(result.success);
}

TEST(WdtParse, RejectsUnknownClockSource)
{
    ParseResult<WdtConfig> result = parseWdtConfig("fixtures/wdt_invalid_clock_source.json");

    EXPECT_FALSE(result.success);
}

TEST(ProjectConfigParse, RejectsSecondWdtBlock)
{
    ParseResult<ProjectConfig> result = parseProjectConfig("fixtures/wdt_two_blocks_rejected.json");

    EXPECT_FALSE(result.success);
}

TEST(ProjectCodegen, WdtGeneratesInitOnlyNeverStartOrRefresh)
{
    ParseResult<ProjectConfig> config_result = parseProjectConfig("fixtures/wdt_valid_32m.json");
    ASSERT_TRUE(config_result.success) << config_result.error;

    ParseResult<GeneratedProjectFirmware> firmware_result =
        generateProjectFirmware(*config_result.config, loadProjectKnowledgeForTests());
    ASSERT_TRUE(firmware_result.success) << firmware_result.error;

    const std::string& init_c = firmware_result.config->mik32_init_c;
    EXPECT_TRUE(contains(init_c, "hwdt.Instance = WDT;"));
    EXPECT_TRUE(contains(init_c, "hwdt.Init.Clock = HAL_WDT_OSC32M;"));
    EXPECT_TRUE(contains(init_c, "hwdt.Init.ReloadMs = 500;"));
    EXPECT_TRUE(contains(init_c, "HAL_WDT_Init(&hwdt, WDT_TIMEOUT_DEFAULT)"));
    // Безопасность: кодоген никогда не запускает и не обновляет WDT сам
    // (упоминания без "(" допустимы — это объяснительный комментарий).
    EXPECT_FALSE(contains(init_c, "HAL_WDT_Start("));
    EXPECT_FALSE(contains(init_c, "HAL_WDT_Refresh("));
}

TEST(ProjectCodegen, WdtLsi32kClockSourceMapsToCorrectMacro)
{
    ParseResult<ProjectConfig> config_result = parseProjectConfig("fixtures/wdt_valid_32k.json");
    ASSERT_TRUE(config_result.success) << config_result.error;

    ParseResult<GeneratedProjectFirmware> firmware_result =
        generateProjectFirmware(*config_result.config, loadProjectKnowledgeForTests());
    ASSERT_TRUE(firmware_result.success) << firmware_result.error;

    EXPECT_TRUE(contains(firmware_result.config->mik32_init_c, "hwdt.Init.Clock = HAL_WDT_OSC32K;"));
}

// ============================================================================
// DAC — AUDIT_TZ_PERIPHERY_GAPS.md, Тир 2. Два физических канала (instance
// 0/1). mik32_hal_dac.h/.c + вендорный пример HAL_DAC.
// ============================================================================

TEST(DacParse, ParsesValidInstance0)
{
    ParseResult<DacConfig> result = parseDacConfig("fixtures/dac_valid_instance0.json");

    ASSERT_TRUE(result.success) << result.error;
    EXPECT_EQ(result.config->instance, 0);
    EXPECT_EQ(result.config->div, 31);
    EXPECT_FALSE(result.config->ext_ref);
    EXPECT_TRUE(result.config->ext_clb);
}

TEST(DacParse, RejectsInstanceOutOfRange)
{
    ParseResult<DacConfig> result = parseDacConfig("fixtures/dac_invalid_instance.json");

    EXPECT_FALSE(result.success);
}

TEST(ProjectConfigParse, RejectsDuplicateDacInstance)
{
    ParseResult<ProjectConfig> result = parseProjectConfig("fixtures/dac_duplicate_instance_rejected.json");

    EXPECT_FALSE(result.success);
}

TEST(ProjectCodegen, DacInstance0GeneratesExpectedInitCode)
{
    ParseResult<ProjectConfig> config_result = parseProjectConfig("fixtures/dac_valid_instance0.json");
    ASSERT_TRUE(config_result.success) << config_result.error;

    ParseResult<GeneratedProjectFirmware> firmware_result =
        generateProjectFirmware(*config_result.config, loadProjectKnowledgeForTests());
    ASSERT_TRUE(firmware_result.success) << firmware_result.error;

    const std::string& init_c = firmware_result.config->mik32_init_c;
    EXPECT_TRUE(contains(init_c, "hdac0.Instance = ANALOG_REG;"));
    EXPECT_TRUE(contains(init_c, "hdac0.Instance_dac = HAL_DAC0;"));
    EXPECT_TRUE(contains(init_c, "hdac0.Init.DIV = 31;"));
    EXPECT_TRUE(contains(init_c, "hdac0.Init.EXTRef = DAC_EXTREF_OFF;"));
    EXPECT_TRUE(contains(init_c, "hdac0.Init.EXTClb = DAC_EXTCLB_DACREF;"));
    EXPECT_TRUE(contains(init_c, "HAL_DAC_Init(&hdac0);"));
}

TEST(ProjectCodegen, DacInstance1WithExtRefMentionsDacRefPin)
{
    ParseResult<ProjectConfig> config_result = parseProjectConfig("fixtures/dac_valid_instance1_extref.json");
    ASSERT_TRUE(config_result.success) << config_result.error;

    ParseResult<GeneratedProjectFirmware> firmware_result =
        generateProjectFirmware(*config_result.config, loadProjectKnowledgeForTests());
    ASSERT_TRUE(firmware_result.success) << firmware_result.error;

    const std::string& init_c = firmware_result.config->mik32_init_c;
    EXPECT_TRUE(contains(init_c, "hdac1.Instance_dac = HAL_DAC1;"));
    EXPECT_TRUE(contains(init_c, "DAC_REF"));
    EXPECT_TRUE(contains(init_c, "GPIO_1/PIN_11"));
}

// ============================================================================
// ADC/CRYPTO/OTP/TSENS — AUDIT_TZ_PERIPHERY_GAPS.md, Тир 2. Все четыре —
// единственные глобальные блоки, без knowledge_base.
// ============================================================================

TEST(AdcParse, ParsesValidChannel)
{
    ParseResult<AdcConfig> result = parseAdcConfig("fixtures/adc_valid.json");
    ASSERT_TRUE(result.success) << result.error;
    EXPECT_EQ(result.config->channel, 3);
    EXPECT_FALSE(result.config->ext_ref);
    EXPECT_TRUE(result.config->ext_clb);
}

TEST(AdcParse, RejectsChannelOutOfRange)
{
    EXPECT_FALSE(parseAdcConfig("fixtures/adc_invalid_channel.json").success);
}

TEST(ProjectConfigParse, RejectsSecondAdcBlock)
{
    EXPECT_FALSE(parseProjectConfig("fixtures/adc_two_blocks_rejected.json").success);
}

TEST(ProjectCodegen, AdcGeneratesExpectedInitCode)
{
    ParseResult<ProjectConfig> config_result = parseProjectConfig("fixtures/adc_valid.json");
    ASSERT_TRUE(config_result.success) << config_result.error;
    ParseResult<GeneratedProjectFirmware> firmware_result =
        generateProjectFirmware(*config_result.config, loadProjectKnowledgeForTests());
    ASSERT_TRUE(firmware_result.success) << firmware_result.error;

    const std::string& init_c = firmware_result.config->mik32_init_c;
    EXPECT_TRUE(contains(init_c, "hadc.Instance = ANALOG_REG;"));
    EXPECT_TRUE(contains(init_c, "hadc.Init.Sel = ADC_CHANNEL3;"));
    EXPECT_TRUE(contains(init_c, "hadc.Init.EXTRef = ADC_EXTREF_OFF;"));
    EXPECT_TRUE(contains(init_c, "hadc.Init.EXTClb = ADC_EXTCLB_ADCREF;"));
    EXPECT_TRUE(contains(init_c, "HAL_ADC_Init(&hadc);"));
}

TEST(CryptoParse, ParsesValidAesCtrWithDefaults)
{
    ParseResult<CryptoConfig> result = parseCryptoConfig("fixtures/crypto_valid_aes_ctr.json");
    ASSERT_TRUE(result.success) << result.error;
    EXPECT_EQ(result.config->algorithm, "aes");
    EXPECT_EQ(result.config->cipher_mode, "ctr");
    EXPECT_EQ(result.config->swap_mode, "none");
    EXPECT_EQ(result.config->order_mode, "lsw");
}

TEST(CryptoParse, ParsesCustomSwapAndOrderMode)
{
    ParseResult<CryptoConfig> result = parseCryptoConfig("fixtures/crypto_valid_kuznechik_cbc_custom.json");
    ASSERT_TRUE(result.success) << result.error;
    EXPECT_EQ(result.config->swap_mode, "byte");
    EXPECT_EQ(result.config->order_mode, "msw");
}

TEST(CryptoParse, RejectsUnknownAlgorithm)
{
    EXPECT_FALSE(parseCryptoConfig("fixtures/crypto_invalid_algorithm.json").success);
}

TEST(ProjectConfigParse, RejectsSecondCryptoBlock)
{
    EXPECT_FALSE(parseProjectConfig("fixtures/crypto_two_blocks_rejected.json").success);
}

TEST(ProjectCodegen, CryptoGeneratesExpectedInitCodeWithoutKeyOrIv)
{
    ParseResult<ProjectConfig> config_result = parseProjectConfig("fixtures/crypto_valid_kuznechik_cbc_custom.json");
    ASSERT_TRUE(config_result.success) << config_result.error;
    ParseResult<GeneratedProjectFirmware> firmware_result =
        generateProjectFirmware(*config_result.config, loadProjectKnowledgeForTests());
    ASSERT_TRUE(firmware_result.success) << firmware_result.error;

    const std::string& init_c = firmware_result.config->mik32_init_c;
    EXPECT_TRUE(contains(init_c, "hcrypto.Instance = CRYPTO;"));
    EXPECT_TRUE(contains(init_c, "hcrypto.Algorithm = CRYPTO_ALG_KUZNECHIK;"));
    EXPECT_TRUE(contains(init_c, "hcrypto.CipherMode = CRYPTO_CIPHER_MODE_CBC;"));
    EXPECT_TRUE(contains(init_c, "hcrypto.SwapMode = CRYPTO_SWAP_MODE_BYTE;"));
    EXPECT_TRUE(contains(init_c, "hcrypto.OrderMode = CRYPTO_ORDER_MODE_MSW;"));
    EXPECT_TRUE(contains(init_c, "HAL_Crypto_Init(&hcrypto);"));
    // Безопасность: ключ/IV никогда не генерируются.
    EXPECT_FALSE(contains(init_c, "HAL_Crypto_SetKey("));
    EXPECT_FALSE(contains(init_c, "HAL_Crypto_SetIV("));
}

TEST(OtpParse, ParsesValidTwoStages)
{
    ParseResult<OtpConfig> result = parseOtpConfig("fixtures/otp_valid_two_stages.json");
    ASSERT_TRUE(result.success) << result.error;
    EXPECT_EQ(result.config->read_mode, "two_stages");
}

TEST(OtpParse, RejectsUnknownReadMode)
{
    EXPECT_FALSE(parseOtpConfig("fixtures/otp_invalid_read_mode.json").success);
}

TEST(ProjectConfigParse, RejectsSecondOtpBlock)
{
    EXPECT_FALSE(parseProjectConfig("fixtures/otp_two_blocks_rejected.json").success);
}

TEST(ProjectCodegen, OtpTwoStagesGeneratesExpectedInitCode)
{
    ParseResult<ProjectConfig> config_result = parseProjectConfig("fixtures/otp_valid_two_stages.json");
    ASSERT_TRUE(config_result.success) << config_result.error;
    ParseResult<GeneratedProjectFirmware> firmware_result =
        generateProjectFirmware(*config_result.config, loadProjectKnowledgeForTests());
    ASSERT_TRUE(firmware_result.success) << firmware_result.error;

    const std::string& init_c = firmware_result.config->mik32_init_c;
    EXPECT_TRUE(contains(init_c, "hotp.Instance = OTP;"));
    EXPECT_TRUE(contains(init_c, "hotp.ReadMode = OPT_READ_2STAGES;"));
    EXPECT_TRUE(contains(init_c, "HAL_OTP_Init(&hotp);"));
    // Безопасность: запись никогда не генерируется (OTP необратима). "(" —
    // чтобы не ловить упоминание "HAL_OTP_Write*" в объяснительном комментарии.
    EXPECT_FALSE(contains(init_c, "HAL_OTP_Write("));
}

TEST(ProjectCodegen, OtpThreeStagesMapsToCorrectMacro)
{
    ParseResult<ProjectConfig> config_result = parseProjectConfig("fixtures/otp_valid_three_stages.json");
    ASSERT_TRUE(config_result.success) << config_result.error;
    ParseResult<GeneratedProjectFirmware> firmware_result =
        generateProjectFirmware(*config_result.config, loadProjectKnowledgeForTests());
    ASSERT_TRUE(firmware_result.success) << firmware_result.error;

    EXPECT_TRUE(contains(firmware_result.config->mik32_init_c, "hotp.ReadMode = OPT_READ_3STAGES;"));
}

TEST(TsensParse, ParsesMinimalValid)
{
    ParseResult<TsensConfig> result = parseTsensConfig("fixtures/tsens_valid_minimal.json");
    ASSERT_TRUE(result.success) << result.error;
    EXPECT_EQ(result.config->clock_source, "sys_clk");
    EXPECT_EQ(result.config->frequency_hz, 40000u);
    EXPECT_FALSE(result.config->has_low_threshold);
    EXPECT_TRUE(result.config->interrupts.empty());
}

TEST(TsensParse, ParsesThresholdsAndInterrupts)
{
    ParseResult<TsensConfig> result = parseTsensConfig("fixtures/tsens_valid_with_thresholds_and_interrupts.json");
    ASSERT_TRUE(result.success) << result.error;
    EXPECT_TRUE(result.config->has_low_threshold);
    EXPECT_EQ(result.config->low_threshold_celsius, -10);
    EXPECT_TRUE(result.config->has_high_threshold);
    EXPECT_EQ(result.config->high_threshold_celsius, 85);
    ASSERT_EQ(result.config->interrupts.size(), 3u);
}

TEST(TsensParse, RejectsFrequencyTooHigh)
{
    EXPECT_FALSE(parseTsensConfig("fixtures/tsens_invalid_frequency_too_high.json").success);
}

TEST(TsensParse, RejectsThresholdOutOfRange)
{
    EXPECT_FALSE(parseTsensConfig("fixtures/tsens_invalid_threshold_out_of_range.json").success);
}

TEST(ProjectConfigParse, RejectsSecondTsensBlock)
{
    EXPECT_FALSE(parseProjectConfig("fixtures/tsens_two_blocks_rejected.json").success);
}

TEST(ProjectCodegen, TsensMinimalGeneratesInitWithoutThresholdsOrInterrupts)
{
    ParseResult<ProjectConfig> config_result = parseProjectConfig("fixtures/tsens_valid_minimal.json");
    ASSERT_TRUE(config_result.success) << config_result.error;
    ParseResult<GeneratedProjectFirmware> firmware_result =
        generateProjectFirmware(*config_result.config, loadProjectKnowledgeForTests());
    ASSERT_TRUE(firmware_result.success) << firmware_result.error;

    const std::string& init_c = firmware_result.config->mik32_init_c;
    EXPECT_TRUE(contains(init_c, "htsens.Instance = ANALOG_REG;"));
    EXPECT_TRUE(contains(init_c, "htsens.Clock = HAL_TSENS_SYS_CLK;"));
    EXPECT_TRUE(contains(init_c, "htsens.Frequency = 40000;"));
    EXPECT_TRUE(contains(init_c, "HAL_TSENS_Init(&htsens)"));
    EXPECT_FALSE(contains(init_c, "SetLowThreshold"));
    EXPECT_FALSE(contains(init_c, "SetHiThreshold"));
    EXPECT_FALSE(contains(init_c, "IRQ_ENABLE"));
}

TEST(ProjectCodegen, TsensWithThresholdsAndInterruptsGeneratesAllThree)
{
    ParseResult<ProjectConfig> config_result = parseProjectConfig("fixtures/tsens_valid_with_thresholds_and_interrupts.json");
    ASSERT_TRUE(config_result.success) << config_result.error;
    ParseResult<GeneratedProjectFirmware> firmware_result =
        generateProjectFirmware(*config_result.config, loadProjectKnowledgeForTests());
    ASSERT_TRUE(firmware_result.success) << firmware_result.error;

    const std::string& init_c = firmware_result.config->mik32_init_c;
    EXPECT_TRUE(contains(init_c, "htsens.Clock = HAL_TSENS_HCLK;"));
    EXPECT_TRUE(contains(init_c, "HAL_TSENS_SetLowThreshold(&htsens, -10);"));
    EXPECT_TRUE(contains(init_c, "HAL_TSENS_SetHiThreshold(&htsens, 85);"));
    EXPECT_TRUE(contains(init_c, "__HAL_TSENS_IRQ_ENABLE_LOWIRQ(&htsens);"));
    EXPECT_TRUE(contains(init_c, "__HAL_TSENS_IRQ_ENABLE_HIIRQ(&htsens);"));
    EXPECT_TRUE(contains(init_c, "__HAL_TSENS_IRQ_ENABLE_EOC(&htsens);"));
}

// --- PVD (монитор напряжения, Тир 3) ---------------------------------------

TEST(PvdParse, ParsesMinimalValid)
{
    ParseResult<PvdConfig> result = parsePvdConfig("fixtures/pvd_valid_minimal.json");
    ASSERT_TRUE(result.success) << result.error;
    EXPECT_EQ(result.config->under_threshold, 3);
    EXPECT_EQ(result.config->over_threshold, 12);
    EXPECT_EQ(result.config->digital_filter_value, 100);
    EXPECT_FALSE(result.config->enable_reference_calibration);
}

TEST(PvdParse, ParsesCalibrationFlag)
{
    ParseResult<PvdConfig> result = parsePvdConfig("fixtures/pvd_valid_with_calibration.json");
    ASSERT_TRUE(result.success) << result.error;
    EXPECT_TRUE(result.config->enable_reference_calibration);
}

TEST(PvdParse, RejectsThresholdOutOfRange)
{
    EXPECT_FALSE(parsePvdConfig("fixtures/pvd_invalid_threshold_out_of_range.json").success);
}

TEST(ProjectConfigParse, RejectsSecondPvdBlock)
{
    EXPECT_FALSE(parseProjectConfig("fixtures/pvd_two_blocks_rejected.json").success);
}

TEST(ProjectCodegen, PvdGeneratesClockEnableAndRegisterWrites)
{
    ParseResult<ProjectConfig> config_result = parseProjectConfig("fixtures/pvd_valid_minimal.json");
    ASSERT_TRUE(config_result.success) << config_result.error;
    ParseResult<GeneratedProjectFirmware> firmware_result =
        generateProjectFirmware(*config_result.config, loadProjectKnowledgeForTests());
    ASSERT_TRUE(firmware_result.success) << firmware_result.error;

    const std::string& init_c = firmware_result.config->mik32_init_c;
    EXPECT_TRUE(contains(init_c, "__HAL_PCC_PVD_CONTROL_CLK_ENABLE();"));
    EXPECT_TRUE(contains(init_c, "PVD_CONTROL->DPF_VALUE = 100;"));
    EXPECT_TRUE(contains(init_c, "PVD_CONTROL->CONFIG = pvd_config;"));
    // Биты сброса-по-порогу (NRESETU/NRESETO) кодоген никогда не трогает —
    // см. schemas/pvd.json, _comment_no_reset_fields. Само слово встречается
    // в пояснительном комментарии (ожидаемо), но не в реальной записи бита.
    EXPECT_FALSE(contains(init_c, "PVD_CONFIG_NRESETU_M"));
    EXPECT_FALSE(contains(init_c, "PVD_CONFIG_NRESETO_M"));
}

TEST(ProjectCodegen, PvdWithCalibrationSetsEnVrefClbBit)
{
    ParseResult<ProjectConfig> config_result = parseProjectConfig("fixtures/pvd_valid_with_calibration.json");
    ASSERT_TRUE(config_result.success) << config_result.error;
    ParseResult<GeneratedProjectFirmware> firmware_result =
        generateProjectFirmware(*config_result.config, loadProjectKnowledgeForTests());
    ASSERT_TRUE(firmware_result.success) << firmware_result.error;

    EXPECT_TRUE(contains(firmware_result.config->mik32_init_c, "pvd_config |= PVD_CONFIG_EN_VREFCLB_M;"));
}

// --- Bus Watchdog (сторожевой таймер шины, Тир 3) --------------------------

TEST(BusWatchdogParse, ParsesMinimalValidAllDisabled)
{
    ParseResult<BusWatchdogConfig> result = parseBusWatchdogConfig("fixtures/bus_watchdog_valid_minimal.json");
    ASSERT_TRUE(result.success) << result.error;
    EXPECT_FALSE(result.config->eeprom_enabled);
    EXPECT_FALSE(result.config->spifi_enabled);
    EXPECT_FALSE(result.config->dom3_enabled);
    EXPECT_EQ(result.config->timeout, 0u);
}

TEST(BusWatchdogParse, ParsesAllLinesEnabledWithTimeout)
{
    ParseResult<BusWatchdogConfig> result = parseBusWatchdogConfig("fixtures/bus_watchdog_valid_all_enabled.json");
    ASSERT_TRUE(result.success) << result.error;
    EXPECT_TRUE(result.config->eeprom_enabled);
    EXPECT_TRUE(result.config->spifi_enabled);
    EXPECT_TRUE(result.config->dom3_enabled);
    EXPECT_EQ(result.config->timeout, 15u);
}

TEST(BusWatchdogParse, RejectsEnabledLineWithoutTimeout)
{
    EXPECT_FALSE(parseBusWatchdogConfig("fixtures/bus_watchdog_invalid_enabled_without_timeout.json").success);
}

TEST(BusWatchdogParse, RejectsTimeoutOutOfRange)
{
    // WDT_BUS->TIMEOUT реально реализован только 4 битами (0-15), несмотря на
    // 32-битное объявление в wdt_bus.h — подтверждено HW-тестом 2026-10-14
    // (запись 0xFFFFFFFF считалась обратно как 0x0000000F; timeout=1000000
    // обрезался до эффективного 0 и вешал плату через dom3-линию). См.
    // schemas/bus_watchdog.json.
    EXPECT_FALSE(parseBusWatchdogConfig("fixtures/bus_watchdog_invalid_timeout_out_of_range.json").success);
}

TEST(ProjectConfigParse, RejectsSecondBusWatchdogBlock)
{
    EXPECT_FALSE(parseProjectConfig("fixtures/bus_watchdog_two_blocks_rejected.json").success);
}

TEST(ProjectCodegen, BusWatchdogMinimalGeneratesClockEnableAndZeroEnable)
{
    ParseResult<ProjectConfig> config_result = parseProjectConfig("fixtures/bus_watchdog_valid_minimal.json");
    ASSERT_TRUE(config_result.success) << config_result.error;
    ParseResult<GeneratedProjectFirmware> firmware_result =
        generateProjectFirmware(*config_result.config, loadProjectKnowledgeForTests());
    ASSERT_TRUE(firmware_result.success) << firmware_result.error;

    const std::string& init_c = firmware_result.config->mik32_init_c;
    EXPECT_TRUE(contains(init_c, "__HAL_PCC_WDT_BUS_CLK_ENABLE();"));
    EXPECT_TRUE(contains(init_c, "WDT_BUS->TIMEOUT = 0u;"));
    EXPECT_TRUE(contains(init_c, "WDT_BUS->ENABLE = bus_watchdog_enable;"));
    EXPECT_FALSE(contains(init_c, "WDT_BUS_EEPROM_S"));
    EXPECT_FALSE(contains(init_c, "WDT_BUS_SPIFI_S"));
    EXPECT_FALSE(contains(init_c, "WDT_BUS_DOM3_S"));
}

TEST(ProjectCodegen, BusWatchdogAllEnabledGeneratesAllThreeBits)
{
    ParseResult<ProjectConfig> config_result = parseProjectConfig("fixtures/bus_watchdog_valid_all_enabled.json");
    ASSERT_TRUE(config_result.success) << config_result.error;
    ParseResult<GeneratedProjectFirmware> firmware_result =
        generateProjectFirmware(*config_result.config, loadProjectKnowledgeForTests());
    ASSERT_TRUE(firmware_result.success) << firmware_result.error;

    const std::string& init_c = firmware_result.config->mik32_init_c;
    EXPECT_TRUE(contains(init_c, "WDT_BUS->TIMEOUT = 15u;"));
    EXPECT_TRUE(contains(init_c, "bus_watchdog_enable |= (1 << WDT_BUS_EEPROM_S);"));
    EXPECT_TRUE(contains(init_c, "bus_watchdog_enable |= (1 << WDT_BUS_SPIFI_S);"));
    EXPECT_TRUE(contains(init_c, "bus_watchdog_enable |= (1 << WDT_BUS_DOM3_S);"));
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

// --- standalone DMA (docs/implementations/dma.md) --------------------------

TEST(DmaParse, ParsesValidPeripheralToMemory)
{
    ParseResult<DmaConfig> result = parseDmaConfig("fixtures/dma_valid_peripheral_to_memory.json");
    ASSERT_TRUE(result.success) << result.error;
    EXPECT_EQ(result.config->instance, 2);
    EXPECT_EQ(result.config->request, "i2c_0");
    EXPECT_EQ(result.config->direction, "peripheral_to_memory");
    EXPECT_EQ(result.config->buffer_size_bytes, 16);
    EXPECT_EQ(result.config->priority, "very_high");
}

TEST(DmaParse, ParsesValidMemoryToMemoryWithoutRequest)
{
    ParseResult<DmaConfig> result = parseDmaConfig("fixtures/dma_valid_memory_to_memory.json");
    ASSERT_TRUE(result.success) << result.error;
    EXPECT_TRUE(result.config->request.empty());
    EXPECT_EQ(result.config->priority, "high");
}

TEST(DmaParse, RejectsRequestFieldOnMemoryToMemory)
{
    EXPECT_FALSE(parseDmaConfig("fixtures/dma_invalid_memory_to_memory_with_request.json").success);
}

TEST(DmaParse, RejectsMissingRequestWhenRequired)
{
    EXPECT_FALSE(parseDmaConfig("fixtures/dma_invalid_missing_request.json").success);
}

TEST(DmaParse, RejectsInstanceOutOfRange)
{
    EXPECT_FALSE(parseDmaConfig("fixtures/dma_invalid_instance_out_of_range.json").success);
}

TEST(ProjectConfigParse, RejectsTwoDmaBlocksOnSameChannel)
{
    EXPECT_FALSE(parseProjectConfig("fixtures/dma_two_blocks_same_channel_rejected.json").success);
}

TEST(ProjectConfigParse, RejectsStandaloneDmaChannelConflictingWithUartEmbeddedDma)
{
    ParseResult<ProjectConfig> result = parseProjectConfig("fixtures/dma_cross_uart_embedded_channel_conflict.json");
    EXPECT_FALSE(result.success);
    EXPECT_TRUE(contains(result.error, "DMA channel 0")) << result.error;
}

TEST(ProjectCodegen, StandaloneDmaGeneratesChannelInitAndSharedDmaInit)
{
    ParseResult<ProjectConfig> config_result = parseProjectConfig("fixtures/dma_valid_peripheral_to_memory.json");
    ASSERT_TRUE(config_result.success) << config_result.error;
    ParseResult<GeneratedProjectFirmware> firmware_result =
        generateProjectFirmware(*config_result.config, loadProjectKnowledgeForTests());
    ASSERT_TRUE(firmware_result.success) << firmware_result.error;

    const std::string& init_c = firmware_result.config->mik32_init_c;
    EXPECT_TRUE(contains(init_c, "DMA_InitTypeDef hdma;"));
    EXPECT_TRUE(contains(init_c, "void DMA_Init(void)"));
    EXPECT_TRUE(contains(init_c, "DMA_ChannelHandleTypeDef hdma_channel2;"));
    EXPECT_TRUE(contains(init_c, "uint8_t dma_channel2_buffer[16];"));
    EXPECT_TRUE(contains(init_c, "hdma_channel2.ChannelInit.Channel = DMA_CHANNEL_2;"));
    EXPECT_TRUE(contains(init_c, "hdma_channel2.ChannelInit.ReadMode = DMA_CHANNEL_MODE_PERIPHERY;"));
    EXPECT_TRUE(contains(init_c, "hdma_channel2.ChannelInit.ReadRequest = DMA_CHANNEL_I2C_0_REQUEST;"));
    EXPECT_TRUE(contains(init_c, "hdma_channel2.ChannelInit.WriteMode = DMA_CHANNEL_MODE_MEMORY;"));

    const std::string& main_c = firmware_result.config->main_c;
    EXPECT_TRUE(contains(main_c, "extern uint8_t dma_channel2_buffer[16];"));
}

TEST(ProjectCodegen, StandaloneDmaMemoryToMemoryOmitsRequestFields)
{
    ParseResult<ProjectConfig> config_result = parseProjectConfig("fixtures/dma_valid_memory_to_memory.json");
    ASSERT_TRUE(config_result.success) << config_result.error;
    ParseResult<GeneratedProjectFirmware> firmware_result =
        generateProjectFirmware(*config_result.config, loadProjectKnowledgeForTests());
    ASSERT_TRUE(firmware_result.success) << firmware_result.error;

    const std::string& init_c = firmware_result.config->mik32_init_c;
    EXPECT_TRUE(contains(init_c, "hdma_channel3.ChannelInit.ReadMode = DMA_CHANNEL_MODE_MEMORY;"));
    EXPECT_TRUE(contains(init_c, "hdma_channel3.ChannelInit.WriteMode = DMA_CHANNEL_MODE_MEMORY;"));
    EXPECT_TRUE(contains(init_c, "hdma_channel3.ChannelInit.Priority = DMA_CHANNEL_PRIORITY_HIGH;"));
    EXPECT_FALSE(contains(init_c, "hdma_channel3.ChannelInit.ReadRequest"));
    EXPECT_FALSE(contains(init_c, "hdma_channel3.ChannelInit.WriteRequest"));
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
// I2C — ТЗ по I2C молчит (см. docs/implementations/i2c.md), источники:
// черновик архитектуры §5.4 и реальный HAL (mik32_hal_i2c.h/.c).
// Тир 1 аудита (AUDIT_TZ_PERIPHERY_GAPS.md): digital_filter/analog_filter
// (общие master+slave), general_call/no_stretch_mode/dual_address_enabled/
// own_address_2/own_address_2_mask (только slave).
// ============================================================================

TEST(I2cParse, ParsesValidMasterConfigWithFilterDefaults)
{
    ParseResult<I2cConfig> result = parseI2cConfig("fixtures/i2c_master_valid.json");

    ASSERT_TRUE(result.success) << result.error;
    EXPECT_EQ(result.config->mode, "master");
    // Поля не запрошены — дефолт сохраняет прежнее хардкод-поведение кодогена
    // (schemas/i2c.json).
    EXPECT_EQ(result.config->digital_filter, 0);
    EXPECT_FALSE(result.config->analog_filter);
}

TEST(I2cParse, ParsesMasterDigitalAndAnalogFilter)
{
    ParseResult<I2cConfig> result = parseI2cConfig("fixtures/i2c_master_with_filters.json");

    ASSERT_TRUE(result.success) << result.error;
    EXPECT_EQ(result.config->digital_filter, 15);
    EXPECT_TRUE(result.config->analog_filter);
}

TEST(I2cParse, ParsesSlaveGeneralCallAndNoStretchMode)
{
    ParseResult<I2cConfig> result = parseI2cConfig("fixtures/i2c_slave_general_call_no_stretch.json");

    ASSERT_TRUE(result.success) << result.error;
    EXPECT_TRUE(result.config->general_call);
    EXPECT_TRUE(result.config->no_stretch_mode);
}

TEST(I2cParse, SlaveWithoutOptionalFieldsKeepsThemAtHardcodedDefaults)
{
    ParseResult<I2cConfig> result = parseI2cConfig("fixtures/i2c_slave_receive.json");

    ASSERT_TRUE(result.success) << result.error;
    EXPECT_FALSE(result.config->general_call);
    EXPECT_FALSE(result.config->no_stretch_mode);
    EXPECT_FALSE(result.config->dual_address_enabled);
}

TEST(I2cParse, ParsesSlaveDualAddressWithExplicitMask)
{
    ParseResult<I2cConfig> result = parseI2cConfig("fixtures/i2c_slave_dual_address.json");

    ASSERT_TRUE(result.success) << result.error;
    EXPECT_TRUE(result.config->dual_address_enabled);
    EXPECT_EQ(result.config->own_address_2, 55);
    EXPECT_EQ(result.config->own_address_2_mask, "11111xx");
}

// own_address_2_mask необязателен даже при dual_address_enabled=true —
// дефолт "none" (I2C_OWNADDRESS2_MASK_DISABLE, точное совпадение).
TEST(I2cParse, DualAddressWithoutMaskDefaultsToNone)
{
    ParseResult<I2cConfig> result = parseI2cConfig("fixtures/i2c_slave_dual_address_default_mask.json");

    ASSERT_TRUE(result.success) << result.error;
    EXPECT_EQ(result.config->own_address_2_mask, "none");
}

TEST(I2cParse, RejectsDigitalFilterOutsideAllowedSet)
{
    ParseResult<I2cConfig> result = parseI2cConfig("fixtures/i2c_invalid_digital_filter.json");

    EXPECT_FALSE(result.success);
}

TEST(I2cParse, RejectsUnknownOwnAddress2Mask)
{
    ParseResult<I2cConfig> result = parseI2cConfig("fixtures/i2c_invalid_own_address_2_mask.json");

    EXPECT_FALSE(result.success);
}

// mik32_hal_i2c.h: DualAddressMode требует 7-битный основной адрес —
// dual_address_enabled=true с address_mode=10bit должен быть отклонён.
TEST(I2cParse, RejectsDualAddressWithTenBitAddressMode)
{
    ParseResult<I2cConfig> result = parseI2cConfig("fixtures/i2c_dual_address_requires_7bit.json");

    EXPECT_FALSE(result.success);
}

TEST(I2cParse, RejectsDualAddressEnabledWithoutOwnAddress2)
{
    ParseResult<I2cConfig> result = parseI2cConfig("fixtures/i2c_dual_address_missing_own_address_2.json");

    EXPECT_FALSE(result.success);
}

TEST(I2cParse, RejectsOwnAddress2OutOfRange)
{
    ParseResult<I2cConfig> result = parseI2cConfig("fixtures/i2c_own_address_2_out_of_range.json");

    EXPECT_FALSE(result.success);
}

// --- DMA/IT (2026-10-21, см. docs/implementations/i2c.md) -----------------
// HAL_I2C_Master/Slave_*_DMA существуют в реально установленном SDK
// (mik32_hal_i2c.c:983-1113), требуют только hi2c.hdmatx/hdmarx.

TEST(I2cParse, ParsesValidDma)
{
    ParseResult<I2cConfig> result = parseI2cConfig("fixtures/i2c_master_dma_valid.json");

    ASSERT_TRUE(result.success) << result.error;
    ASSERT_EQ(result.config->dma.size(), 2u);
    EXPECT_EQ(result.config->dma[0].channel, 0);
    EXPECT_EQ(result.config->dma[0].direction, "tx");
    EXPECT_EQ(result.config->dma[1].channel, 1);
    EXPECT_EQ(result.config->dma[1].direction, "rx");
}

TEST(I2cParse, RejectsDmaChannelOutOfRange)
{
    ParseResult<I2cConfig> result = parseI2cConfig("fixtures/i2c_dma_channel_out_of_range.json");

    EXPECT_FALSE(result.success);
}

TEST(I2cParse, RejectsDuplicateDmaDirection)
{
    ParseResult<I2cConfig> result = parseI2cConfig("fixtures/i2c_dma_duplicate_direction.json");

    EXPECT_FALSE(result.success);
}

TEST(I2cParse, ParsesInterruptMode)
{
    ParseResult<I2cConfig> result = parseI2cConfig("fixtures/i2c_master_interrupt_mode.json");

    ASSERT_TRUE(result.success) << result.error;
    EXPECT_TRUE(result.config->interrupt_mode);
}

TEST(I2cParse, InterruptModeDefaultsToFalse)
{
    ParseResult<I2cConfig> result = parseI2cConfig("fixtures/i2c_master_valid.json");

    ASSERT_TRUE(result.success) << result.error;
    EXPECT_FALSE(result.config->interrupt_mode);
}

// ТЗ §3.1.6 п.2: канал DMA — глобальный пул на ВЕСЬ конфиг, общий между
// UART и I2C (и любой другой периферией, использующей ПДП).
TEST(ProjectConfigParse, RejectsGlobalDmaChannelPoolExhaustionAcrossUartAndI2c)
{
    ParseResult<ProjectConfig> result = parseProjectConfig("fixtures/i2c_uart_dma_channel_conflict_global.json");

    EXPECT_FALSE(result.success);
    EXPECT_TRUE(contains(result.error, "DMA channel 2")) << result.error;
}

TEST(ProjectCodegen, I2cDmaWiresHdmatxHdmarxAndChannelInit)
{
    ParseResult<ProjectConfig> config_result = parseProjectConfig("fixtures/i2c_master_dma_valid.json");
    ASSERT_TRUE(config_result.success) << config_result.error;

    ParseResult<GeneratedProjectFirmware> firmware_result =
        generateProjectFirmware(*config_result.config, loadProjectKnowledgeForTests());
    ASSERT_TRUE(firmware_result.success) << firmware_result.error;

    const std::string& init_c = firmware_result.config->mik32_init_c;
    EXPECT_TRUE(contains(init_c, "DMA_InitTypeDef hdma;"));
    EXPECT_TRUE(contains(init_c, "void DMA_Init(void)"));
    EXPECT_TRUE(contains(init_c, "DMA_ChannelHandleTypeDef hdma_hi2c_0_tx;"));
    EXPECT_TRUE(contains(init_c, "DMA_ChannelHandleTypeDef hdma_hi2c_0_rx;"));

    EXPECT_TRUE(contains(init_c, "hi2c_0.hdmatx = &hdma_hi2c_0_tx;"));
    EXPECT_TRUE(contains(init_c, "hi2c_0.hdmarx = &hdma_hi2c_0_rx;"));

    // Порядок: hdmatx/hdmarx заполняются ДО HAL_I2C_Init (vendor I2C0_Init).
    const size_t hdmatx_at = init_c.find("hi2c_0.hdmatx =");
    const size_t init_call_at = init_c.find("HAL_I2C_Init(&hi2c_0);");
    ASSERT_NE(hdmatx_at, std::string::npos);
    ASSERT_NE(init_call_at, std::string::npos);
    EXPECT_LT(hdmatx_at, init_call_at);

    // ChannelInit — буквально по вендорным DMA_CH0_Init/DMA_CH1_Init.
    EXPECT_TRUE(contains(init_c, "hdma_hi2c_0_tx.ChannelInit.Channel = DMA_CHANNEL_0;"));
    EXPECT_TRUE(contains(init_c, "hdma_hi2c_0_tx.ChannelInit.ReadMode = DMA_CHANNEL_MODE_MEMORY;"));
    EXPECT_TRUE(contains(init_c, "hdma_hi2c_0_tx.ChannelInit.WriteMode = DMA_CHANNEL_MODE_PERIPHERY;"));
    EXPECT_TRUE(contains(init_c, "hdma_hi2c_0_tx.ChannelInit.ReadRequest = DMA_CHANNEL_I2C_0_REQUEST;"));
    EXPECT_TRUE(contains(init_c, "hdma_hi2c_0_tx.ChannelInit.ReadAck = DMA_CHANNEL_ACK_DISABLE;"));
    EXPECT_TRUE(contains(init_c, "hdma_hi2c_0_rx.ChannelInit.Channel = DMA_CHANNEL_1;"));
    EXPECT_TRUE(contains(init_c, "hdma_hi2c_0_rx.ChannelInit.ReadMode = DMA_CHANNEL_MODE_PERIPHERY;"));
    EXPECT_TRUE(contains(init_c, "hdma_hi2c_0_rx.ChannelInit.WriteMode = DMA_CHANNEL_MODE_MEMORY;"));
    EXPECT_TRUE(contains(init_c, "hdma_hi2c_0_rx.ChannelInit.WriteRequest = DMA_CHANNEL_I2C_0_REQUEST;"));
}

TEST(ProjectCodegen, I2cInterruptModeEmitsIrqHandlerTemplate)
{
    ParseResult<ProjectConfig> config_result = parseProjectConfig("fixtures/i2c_master_interrupt_mode.json");
    ASSERT_TRUE(config_result.success) << config_result.error;

    ParseResult<GeneratedProjectFirmware> firmware_result =
        generateProjectFirmware(*config_result.config, loadProjectKnowledgeForTests());
    ASSERT_TRUE(firmware_result.success) << firmware_result.error;

    const std::string& init_c = firmware_result.config->mik32_init_c;
    EXPECT_TRUE(contains(init_c, "void I2C_1_IRQHandler_Template(void)"));
    EXPECT_TRUE(contains(init_c, "HAL_I2C_IRQHandler(&hi2c_1);"));
    EXPECT_TRUE(contains(init_c, "EPIC_CHECK_I2C_1()"));
}

// Без dma[] у I2C — поведение не должно меняться (регрессия на старые
// фикстуры/поведение): никаких hdma_hi2c_*/IRQHandler в выводе.
TEST(ProjectCodegen, I2cWithoutDmaOrInterruptModeKeepsOldOutput)
{
    ParseResult<ProjectConfig> config_result = parseProjectConfig("fixtures/i2c_master_valid.json");
    ASSERT_TRUE(config_result.success) << config_result.error;

    ParseResult<GeneratedProjectFirmware> firmware_result =
        generateProjectFirmware(*config_result.config, loadProjectKnowledgeForTests());
    ASSERT_TRUE(firmware_result.success) << firmware_result.error;

    const std::string& init_c = firmware_result.config->mik32_init_c;
    EXPECT_FALSE(contains(init_c, "hdma_hi2c_0"));
    EXPECT_FALSE(contains(init_c, "IRQHandler_Template"));
    EXPECT_FALSE(contains(init_c, "DMA_InitTypeDef hdma;"));
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

TEST(SpiParse, ParsesMasterWithAllFourInterrupts)
{
    ParseResult<SpiConfig> result = parseSpiConfig("fixtures/spi_master_with_interrupts.json");

    ASSERT_TRUE(result.success) << result.error;
    ASSERT_EQ(result.config->interrupts.size(), 4u);
    EXPECT_EQ(result.config->interrupts[0], "rx_fifo_not_empty");
    EXPECT_EQ(result.config->interrupts[1], "tx_fifo_not_full");
    EXPECT_EQ(result.config->interrupts[2], "rx_overflow");
    EXPECT_EQ(result.config->interrupts[3], "mode_fail");
}

TEST(SpiParse, ParsesSlaveWithSingleInterrupt)
{
    ParseResult<SpiConfig> result = parseSpiConfig("fixtures/spi_slave_with_single_interrupt.json");

    ASSERT_TRUE(result.success) << result.error;
    ASSERT_EQ(result.config->interrupts.size(), 1u);
    EXPECT_EQ(result.config->interrupts[0], "rx_fifo_not_empty");
}

TEST(SpiParse, DefaultsToEmptyInterruptsWhenFieldAbsent)
{
    ParseResult<SpiConfig> result = parseSpiConfig("fixtures/spi_master_valid.json");

    ASSERT_TRUE(result.success) << result.error;
    EXPECT_TRUE(result.config->interrupts.empty());
}

TEST(SpiParse, RejectsUnknownInterruptName)
{
    ParseResult<SpiConfig> result = parseSpiConfig("fixtures/spi_invalid_interrupt_name.json");

    EXPECT_FALSE(result.success);
}

TEST(SpiParse, RejectsInterruptsNotArray)
{
    ParseResult<SpiConfig> result = parseSpiConfig("fixtures/spi_interrupts_not_array.json");

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

// Прерывания SPI — единственный закрываемый пробел по SPI из
// AUDIT_TZ_PERIPHERY_GAPS.md. Без поля "interrupts" поведение не меняется:
// ни HAL_SPI_InterruptEnable, ни шаблон IRQ-обработчика не появляются.
TEST(ProjectCodegen, SpiWithoutInterruptsStaysPollingOnly)
{
    ParseResult<ProjectConfig> config_result = parseProjectConfig("fixtures/spi_master_valid.json");
    ASSERT_TRUE(config_result.success) << config_result.error;

    ParseResult<GeneratedProjectFirmware> firmware_result =
        generateProjectFirmware(*config_result.config, loadProjectKnowledgeForTests());
    ASSERT_TRUE(firmware_result.success) << firmware_result.error;

    const std::string& init_c = firmware_result.config->mik32_init_c;
    EXPECT_FALSE(contains(init_c, "HAL_SPI_InterruptEnable"));
    EXPECT_FALSE(contains(init_c, "SPI_0_IRQHandler_Template"));
}

TEST(ProjectCodegen, SpiMasterWithInterruptsGeneratesEnableCallAndHandlerTemplate)
{
    ParseResult<ProjectConfig> config_result = parseProjectConfig("fixtures/spi_master_with_interrupts.json");
    ASSERT_TRUE(config_result.success) << config_result.error;

    ParseResult<GeneratedProjectFirmware> firmware_result =
        generateProjectFirmware(*config_result.config, loadProjectKnowledgeForTests());
    ASSERT_TRUE(firmware_result.success) << firmware_result.error;

    const std::string& init_c = firmware_result.config->mik32_init_c;
    EXPECT_TRUE(contains(init_c, "HAL_SPI_InterruptEnable(&hspi0, SPI_INT_STATUS_RX_FIFO_NOT_EMPTY_M |"));
    EXPECT_TRUE(contains(init_c, "SPI_INT_STATUS_TX_FIFO_NOT_FULL_M"));
    EXPECT_TRUE(contains(init_c, "SPI_INT_STATUS_RX_OVERFLOW_M"));
    EXPECT_TRUE(contains(init_c, "SPI_INT_STATUS_MODE_FAIL_M"));
    EXPECT_TRUE(contains(init_c, "void SPI_0_IRQHandler_Template(void)"));
    EXPECT_TRUE(contains(init_c, "HAL_SPI_IRQHandler(&hspi0);"));
}

TEST(ProjectCodegen, SpiSlaveWithSingleInterruptGeneratesOnlyThatMask)
{
    ParseResult<ProjectConfig> config_result = parseProjectConfig("fixtures/spi_slave_with_single_interrupt.json");
    ASSERT_TRUE(config_result.success) << config_result.error;

    ParseResult<GeneratedProjectFirmware> firmware_result =
        generateProjectFirmware(*config_result.config, loadProjectKnowledgeForTests());
    ASSERT_TRUE(firmware_result.success) << firmware_result.error;

    const std::string& init_c = firmware_result.config->mik32_init_c;
    EXPECT_TRUE(contains(init_c, "HAL_SPI_InterruptEnable(&hspi1, SPI_INT_STATUS_RX_FIFO_NOT_EMPTY_M);"));
    EXPECT_FALSE(contains(init_c, "SPI_INT_STATUS_TX_FIFO_NOT_FULL_M"));
    EXPECT_TRUE(contains(init_c, "void SPI_1_IRQHandler_Template(void)"));
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
            fs::create_directories(dir / "shared" / "runtime", ec);
            std::ofstream(dir / "hal" / "core" / "Source" / "stub.c") << "/* stub */\n";
            std::ofstream(dir / "hal" / "core" / "Include" / "stub.h") << "#pragma once\n";
            std::ofstream(dir / "shared" / "periphery" / "stub.h") << "#pragma once\n";
            // crt0.S обязателен для cmake_project_codegen.cpp (стартап-код,
            // компилируется отдельным правилом CRT0_RULE) — без него
            // генерация теперь ошибается заранее, а не линкуется без него.
            std::ofstream(dir / "shared" / "runtime" / "crt0.S") << "/* stub crt0 */\n";
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

// AUDIT_TZ_PERIPHERY_GAPS.md, Clock/PCC — добавлено 2026-10-14: calibration/
// RTC/oscillator_enable/ForceOscSys/Force32KClk.
TEST(ProjectConfigParse, KeepsNewClockDefaultsWhenFieldsAbsent)
{
    ParseResult<ProjectConfig> result = parseProjectConfig("fixtures/project_all_peripherals.json");
    ASSERT_TRUE(result.success) << result.error;

    EXPECT_EQ(result.config->clock.hsi32m_calibration, 128);
    EXPECT_EQ(result.config->clock.lsi32k_calibration, 8);
    EXPECT_EQ(result.config->clock.rtc_clock_source, "auto");
    EXPECT_EQ(result.config->clock.rtc_clock_cpu_source, "osc32k");
    ASSERT_EQ(result.config->clock.oscillator_enable.size(), 4u);
    EXPECT_FALSE(result.config->clock.force_osc_sys);
    EXPECT_EQ(result.config->clock.force_32k_clk, "osc32k");
}

TEST(ProjectConfigParse, ParsesFullCustomClockFields)
{
    ParseResult<ProjectConfig> result = parseProjectConfig("fixtures/project_clock_full_custom.json");
    ASSERT_TRUE(result.success) << result.error;

    EXPECT_EQ(result.config->clock.hsi32m_calibration, 200);
    EXPECT_EQ(result.config->clock.lsi32k_calibration, 12);
    EXPECT_EQ(result.config->clock.rtc_clock_source, "lsi32k");
    EXPECT_EQ(result.config->clock.rtc_clock_cpu_source, "lsi32k");
    ASSERT_EQ(result.config->clock.oscillator_enable.size(), 2u);
    EXPECT_EQ(result.config->clock.oscillator_enable[0], "hsi32m");
    EXPECT_EQ(result.config->clock.oscillator_enable[1], "lsi32k");
    EXPECT_TRUE(result.config->clock.force_osc_sys);
    EXPECT_EQ(result.config->clock.force_32k_clk, "lsi32k");
}

// Защита от "выключил источник, от которого сам тактируешься" — см.
// project_config.cpp, комментарий над этой проверкой.
TEST(ProjectConfigParse, RejectsOscillatorEnableWithoutOscillatorSystem)
{
    ParseResult<ProjectConfig> result = parseProjectConfig("fixtures/project_clock_oscillator_enable_without_system.json");

    EXPECT_FALSE(result.success);
}

TEST(ProjectConfigParse, RejectsHsi32mCalibrationOutOfRange)
{
    ParseResult<ProjectConfig> result = parseProjectConfig("fixtures/project_clock_invalid_hsi32m_calibration.json");

    EXPECT_FALSE(result.success);
}

TEST(ProjectConfigParse, RejectsLsi32kCalibrationOutOfRange)
{
    ParseResult<ProjectConfig> result = parseProjectConfig("fixtures/project_clock_invalid_lsi32k_calibration.json");

    EXPECT_FALSE(result.success);
}

TEST(ProjectCodegen, GeneratedSystemClockConfigReflectsFullCustomClockFields)
{
    ParseResult<ProjectConfig> config_result = parseProjectConfig("fixtures/project_clock_full_custom.json");
    ASSERT_TRUE(config_result.success) << config_result.error;

    ParseResult<GeneratedProjectFirmware> firmware_result =
        generateProjectFirmware(*config_result.config, loadProjectKnowledgeForTests());
    ASSERT_TRUE(firmware_result.success) << firmware_result.error;

    const std::string& main_c = firmware_result.config->main_c;
    EXPECT_TRUE(contains(main_c, "PCC_OscInit.OscillatorEnable = PCC_OSCILLATORTYPE_HSI32M | PCC_OSCILLATORTYPE_LSI32K;"));
    EXPECT_TRUE(contains(main_c, "PCC_OscInit.FreqMon.ForceOscSys = PCC_FORCE_OSC_SYS_FIXED;"));
    EXPECT_TRUE(contains(main_c, "PCC_OscInit.FreqMon.Force32KClk = PCC_FREQ_MONITOR_SOURCE_LSI32K;"));
    EXPECT_TRUE(contains(main_c, "PCC_OscInit.HSI32MCalibrationValue = 200;"));
    EXPECT_TRUE(contains(main_c, "PCC_OscInit.LSI32KCalibrationValue = 12;"));
    EXPECT_TRUE(contains(main_c, "PCC_OscInit.RTCClockSelection = PCC_RTC_CLOCK_SOURCE_LSI32K;"));
    EXPECT_TRUE(contains(main_c, "PCC_OscInit.RTCClockCPUSelection = PCC_CPU_RTC_CLOCK_SOURCE_LSI32K;"));
}

TEST(ProjectCodegen, GeneratedSystemClockConfigKeepsOldDefaultsWhenFieldsAbsent)
{
    ParseResult<ProjectConfig> config_result = parseProjectConfig("fixtures/project_all_peripherals.json");
    ASSERT_TRUE(config_result.success) << config_result.error;

    ParseResult<GeneratedProjectFirmware> firmware_result =
        generateProjectFirmware(*config_result.config, loadProjectKnowledgeForTests());
    ASSERT_TRUE(firmware_result.success) << firmware_result.error;

    const std::string& main_c = firmware_result.config->main_c;
    EXPECT_TRUE(contains(main_c, "PCC_OscInit.OscillatorEnable = PCC_OSCILLATORTYPE_HSI32M | PCC_OSCILLATORTYPE_OSC32M "
        "| PCC_OSCILLATORTYPE_LSI32K | PCC_OSCILLATORTYPE_OSC32K;"));
    EXPECT_TRUE(contains(main_c, "PCC_OscInit.FreqMon.ForceOscSys = PCC_FORCE_OSC_SYS_UNFIXED;"));
    EXPECT_TRUE(contains(main_c, "PCC_OscInit.FreqMon.Force32KClk = PCC_FREQ_MONITOR_SOURCE_OSC32K;"));
    EXPECT_TRUE(contains(main_c, "PCC_OscInit.HSI32MCalibrationValue = 128;"));
    EXPECT_TRUE(contains(main_c, "PCC_OscInit.LSI32KCalibrationValue = 8;"));
    EXPECT_TRUE(contains(main_c, "PCC_OscInit.RTCClockSelection = PCC_RTC_CLOCK_SOURCE_AUTO;"));
    EXPECT_TRUE(contains(main_c, "PCC_OscInit.RTCClockCPUSelection = PCC_CPU_RTC_CLOCK_SOURCE_OSC32K;"));
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

    // Регрессия (найдено реальной прошивкой на плате 2026-10-20): crt0.S
    // должен компилироваться отдельной командой с объектом, названным
    // буквально "crt0.o" — sections.lds матчит стартап-код по этому имени,
    // а обычный target_sources() назвал бы объект "crt0.S.o"/"crt0.S.obj",
    // из-за чего entry point уезжал в середину .text вместо 0x80000000 и
    // МК не стартовал (сборка и линковка при этом проходили без ошибок).
    EXPECT_TRUE(contains(cmakelists, "${CMAKE_BINARY_DIR}/crt0.o"));
    EXPECT_TRUE(contains(cmakelists, "add_custom_command("));
    EXPECT_FALSE(contains(cmakelists, "        mik32-sdk/shared/runtime/crt0.S\n"));

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

// OVERNIGHT_FIELD_REFERENCE.md — CHANGELOG_GENERATED.md.
TEST(ProjectCodegenWriteTransactional, ChangelogSaysFirstGenerationOnFreshDir)
{
    ParseResult<ProjectConfig> config_result = parseProjectConfig("fixtures/gpio_codegen_output.json");
    ASSERT_TRUE(config_result.success) << config_result.error;
    ParseResult<GeneratedProjectFirmware> firmware_result =
        generateProjectFirmware(*config_result.config, loadProjectKnowledgeForTests());
    ASSERT_TRUE(firmware_result.success) << firmware_result.error;

    const test_fs::path output_dir = makeTempTestOutputDir("changelog_first");
    std::error_code cleanup_error;
    test_fs::remove_all(output_dir, cleanup_error);

    ASSERT_TRUE(writeGeneratedProject(*firmware_result.config, "../templates/pio_skeleton", output_dir.u8string()).success);
    EXPECT_TRUE(contains(readBinaryFileToString(output_dir / "CHANGELOG_GENERATED.md"), "Первая генерация"));

    test_fs::remove_all(output_dir, cleanup_error);
}

TEST(ProjectCodegenWriteTransactional, ChangelogSaysNothingChangedOnIdenticalRegeneration)
{
    ParseResult<ProjectConfig> config_result = parseProjectConfig("fixtures/gpio_codegen_output.json");
    ASSERT_TRUE(config_result.success) << config_result.error;
    ParseResult<GeneratedProjectFirmware> firmware_result =
        generateProjectFirmware(*config_result.config, loadProjectKnowledgeForTests());
    ASSERT_TRUE(firmware_result.success) << firmware_result.error;

    const test_fs::path output_dir = makeTempTestOutputDir("changelog_unchanged");
    std::error_code cleanup_error;
    test_fs::remove_all(output_dir, cleanup_error);

    ASSERT_TRUE(writeGeneratedProject(*firmware_result.config, "../templates/pio_skeleton", output_dir.u8string()).success);
    ASSERT_TRUE(writeGeneratedProject(*firmware_result.config, "../templates/pio_skeleton", output_dir.u8string()).success);
    EXPECT_TRUE(contains(readBinaryFileToString(output_dir / "CHANGELOG_GENERATED.md"), "ничего не изменилось"));

    test_fs::remove_all(output_dir, cleanup_error);
}

// Найдено на реальном вендорном проекте (docs/examples/mik32/controlFirmware,
// entry point — main.cpp, не main.c): до фикса writeGeneratedProject искала
// существующий main.c только по фиксированному имени и, не найдя его, писала
// рядом новый main.c с ПУСТЫМИ USER CODE секциями, а старый main.cpp
// оставался нетронутым — PlatformIO увидел бы два int main() и упал на
// линковке, а прикладной код терялся бы без единого предупреждения.
TEST(ProjectCodegenWriteTransactional, MigratesLegacyMainCppAndRemovesItAfterSuccess)
{
    ParseResult<ProjectConfig> config_result = parseProjectConfig("fixtures/gpio_codegen_output.json");
    ASSERT_TRUE(config_result.success) << config_result.error;
    ParseResult<GeneratedProjectFirmware> firmware_result =
        generateProjectFirmware(*config_result.config, loadProjectKnowledgeForTests());
    ASSERT_TRUE(firmware_result.success) << firmware_result.error;

    const test_fs::path output_dir = makeTempTestOutputDir("legacy_cpp_entry");
    std::error_code cleanup_error;
    test_fs::remove_all(output_dir, cleanup_error);
    const test_fs::path src_dir = output_dir / "src";
    test_fs::create_directories(src_dir);

    std::ofstream legacy_main(src_dir / "main.cpp", std::ios::binary);
    legacy_main << "int unique_marker_variable = 424242;\n\nint main()\n{\n    while (1) {}\n}\n";
    legacy_main.close();

    ASSERT_TRUE(writeGeneratedProject(*firmware_result.config, "../templates/pio_skeleton", output_dir.u8string()).success);

    EXPECT_FALSE(test_fs::exists(src_dir / "main.cpp"));
    EXPECT_TRUE(test_fs::exists(src_dir / "main.c"));
    EXPECT_TRUE(contains(readBinaryFileToString(src_dir / "main.c"), "unique_marker_variable"));

    test_fs::remove_all(output_dir, cleanup_error);
}

TEST(ProjectCodegenWriteTransactional, ChangelogShowsDiffWhenConfigChangesBetweenRegenerations)
{
    ParseResult<ProjectConfig> first_config = parseProjectConfig("fixtures/gpio_codegen_output.json");
    ASSERT_TRUE(first_config.success) << first_config.error;
    ParseResult<GeneratedProjectFirmware> first_firmware =
        generateProjectFirmware(*first_config.config, loadProjectKnowledgeForTests());
    ASSERT_TRUE(first_firmware.success) << first_firmware.error;

    // gpio_duplicate_pins.json — другой instance/pins того же типа periph,
    // достаточно другой текст mik32_init.c, чтобы дифф был непустым.
    ParseResult<ProjectConfig> second_config = parseProjectConfig("fixtures/timer16_valid.json");
    ASSERT_TRUE(second_config.success) << second_config.error;
    ParseResult<GeneratedProjectFirmware> second_firmware =
        generateProjectFirmware(*second_config.config, loadProjectKnowledgeForTests());
    ASSERT_TRUE(second_firmware.success) << second_firmware.error;

    const test_fs::path output_dir = makeTempTestOutputDir("changelog_diff");
    std::error_code cleanup_error;
    test_fs::remove_all(output_dir, cleanup_error);

    ASSERT_TRUE(writeGeneratedProject(*first_firmware.config, "../templates/pio_skeleton", output_dir.u8string()).success);
    ASSERT_TRUE(writeGeneratedProject(*second_firmware.config, "../templates/pio_skeleton", output_dir.u8string()).success);

    const std::string changelog = readBinaryFileToString(output_dir / "CHANGELOG_GENERATED.md");
    EXPECT_TRUE(contains(changelog, "mik32_init.c"));
    EXPECT_TRUE(contains(changelog, "-"));
    EXPECT_TRUE(contains(changelog, "+"));

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
    ASSERT_NE(main_c_on_disk.find("/* USER CODE BEGIN 3 */"), std::string::npos);

    // Пользователь вручную вставляет строку внутрь USER CODE-секции "3".
    const std::string marker_begin = "/* USER CODE BEGIN 3 */";
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
