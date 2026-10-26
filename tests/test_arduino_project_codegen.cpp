//
// Arduino-ветка кодогенерации — тесты. Паттерн тот же, что
// test_rust_project_codegen.cpp: ParseResult для генерации main.cpp,
// отдельно — CodegenWriteResult для транзакционной записи проекта на диск
// во временный каталог ОС (scratchDirFor), с проверкой USER CODE merge.
//
#include <gtest/gtest.h>
#include "project_config.h"
#include "arduino_project_codegen.h"
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

namespace
{
    fs::path scratchDirFor(const std::string& name)
    {
        return fs::temp_directory_path() / ("mik32_test_arduino_" + name);
    }

    std::string readFileToString(const fs::path& path)
    {
        std::ifstream file(path, std::ios::binary);
        std::ostringstream buffer;
        buffer << file.rdbuf();
        return buffer.str();
    }

    GpioConfig makeGpioOutput(int instance, std::vector<uint8_t> pins)
    {
        GpioConfig block;
        block.type = "gpio";
        block.instance = instance;
        block.mode = "output";
        block.pull = "none";
        block.drive_strength = "2mA";
        block.pins = std::move(pins);
        return block;
    }

    UartConfig makeUart(int instance, int baud_rate)
    {
        UartConfig block;
        block.type = "uart";
        block.instance = instance;
        block.baud_rate = baud_rate;
        block.data_bits = 8;
        block.stop_bits = 1;
        block.parity = "none";
        block.flow_control = false;
        block.direction = "duplex";
        return block;
    }

    I2cConfig makeI2c(int instance, const std::string& mode, int own_address = 0)
    {
        I2cConfig block;
        block.type = "i2c";
        block.instance = instance;
        block.mode = mode;
        block.clock_speed = 100000;
        block.address_mode = "7bit";
        block.own_address = own_address;
        block.digital_filter = 0;
        block.analog_filter = true;
        block.general_call = false;
        block.no_stretch_mode = false;
        block.dual_address_enabled = false;
        block.own_address_2 = 0;
        block.own_address_2_mask = "none";
        return block;
    }
}

TEST(ArduinoProjectCodegen, RejectsUnsupportedPeripheralLikeTimer16)
{
    ProjectConfig config;
    config.mcu = "MIK32V2";
    config.version = "1.0";
    config.gpio_blocks.push_back(makeGpioOutput(0, {3}));

    Timer16Config timer_block;
    timer_block.type = "timer16";
    config.timer16_blocks.push_back(timer_block);

    ParseResult<GeneratedProjectFirmwareArduino> firmware = generateProjectFirmwareArduino(config);
    EXPECT_FALSE(firmware.success);
    EXPECT_NE(firmware.error.find("Timer16"), std::string::npos) << firmware.error;
}

TEST(ArduinoProjectCodegen, AcceptsAllEightGpio2PinsLikeCCodegenDoes)
{
    // parser/gpio/gpio_parser.cpp::maxPinForInstance допускает GPIO_2 пины
    // 0-7 (датащит МИК32 v2.2.3, Таблица 193) — Arduino-ветка не должна быть
    // уже этого диапазона (pins_arduino.h/variant.c расширены под все 8).
    ProjectConfig config;
    config.gpio_blocks.push_back(makeGpioOutput(2, {0, 1, 2, 3, 4, 5, 6, 7}));

    ParseResult<GeneratedProjectFirmwareArduino> firmware = generateProjectFirmwareArduino(config);
    ASSERT_TRUE(firmware.success) << firmware.error;
    EXPECT_NE(firmware.config->main_cpp.find("pinMode(P2_0, OUTPUT);"), std::string::npos);
    EXPECT_NE(firmware.config->main_cpp.find("pinMode(P2_7, OUTPUT);"), std::string::npos);
}

namespace
{
    SpiConfig makeSpiMaster(int instance)
    {
        SpiConfig block;
        block.type = "spi";
        block.instance = instance;
        block.mode = "master";
        block.clk_phase = "off";
        block.clk_polarity = "low";
        block.threshold_tx = 4;
        block.baud_rate_div = 2;
        block.manual_cs = true;
        block.decoder = false;
        block.chip_select = "cs0";
        return block;
    }
}

TEST(ArduinoProjectCodegen, RejectsSpiSlaveAsUnsupported)
{
    // SPIClass (cores/arduino/SPI.cpp) не реализует slave-режим.
    ProjectConfig config;
    SpiConfig spi_block = makeSpiMaster(1);
    spi_block.mode = "slave";
    config.spi_blocks.push_back(spi_block);

    ParseResult<GeneratedProjectFirmwareArduino> firmware = generateProjectFirmwareArduino(config);
    EXPECT_FALSE(firmware.success);
    EXPECT_NE(firmware.error.find("mode=master"), std::string::npos) << firmware.error;
}

TEST(ArduinoProjectCodegen, RejectsSpiDecoderAsUnsupported)
{
    ProjectConfig config;
    SpiConfig spi_block = makeSpiMaster(0);
    spi_block.decoder = true;
    config.spi_blocks.push_back(spi_block);

    ParseResult<GeneratedProjectFirmwareArduino> firmware = generateProjectFirmwareArduino(config);
    EXPECT_FALSE(firmware.success);
    EXPECT_NE(firmware.error.find("decoder"), std::string::npos) << firmware.error;
}

TEST(ArduinoProjectCodegen, RejectsSpiAutoCsAsUnsupported)
{
    ProjectConfig config;
    SpiConfig spi_block = makeSpiMaster(0);
    spi_block.manual_cs = false;
    config.spi_blocks.push_back(spi_block);

    ParseResult<GeneratedProjectFirmwareArduino> firmware = generateProjectFirmwareArduino(config);
    EXPECT_FALSE(firmware.success);
    EXPECT_NE(firmware.error.find("CS"), std::string::npos) << firmware.error;
}

TEST(ArduinoProjectCodegen, RejectsSpiInterruptsAsUnsupported)
{
    ProjectConfig config;
    SpiConfig spi_block = makeSpiMaster(1);
    spi_block.interrupts.push_back("rx_fifo_not_empty");
    config.spi_blocks.push_back(spi_block);

    ParseResult<GeneratedProjectFirmwareArduino> firmware = generateProjectFirmwareArduino(config);
    EXPECT_FALSE(firmware.success);
    EXPECT_NE(firmware.error.find("прерывания SPI"), std::string::npos) << firmware.error;
}

TEST(ArduinoProjectCodegen, GeneratesSpiBeginForMaster)
{
    ProjectConfig config;
    config.spi_blocks.push_back(makeSpiMaster(0));
    config.spi_blocks.push_back(makeSpiMaster(1));

    ParseResult<GeneratedProjectFirmwareArduino> firmware = generateProjectFirmwareArduino(config);
    ASSERT_TRUE(firmware.success) << firmware.error;

    const std::string& main_cpp = firmware.config->main_cpp;
    EXPECT_NE(main_cpp.find("SPI0.begin();"), std::string::npos) << main_cpp;
    EXPECT_NE(main_cpp.find("SPI1.begin();"), std::string::npos) << main_cpp;
    EXPECT_NE(main_cpp.find("#include <SPI.h>"), std::string::npos) << main_cpp;
}

TEST(ArduinoProjectCodegen, GeneratesBeginTransactionAfterBeginForSpi)
{
    // Баг 2026-10-26: SPIClass::transfer() (cores/arduino/SPI.cpp:135)
    // проверяет _isInited, которое begin() не выставляет — это делает
    // только beginTransaction(). Без вызова beginTransaction() в setup()
    // любой transfer() в пользовательском коде молча возвращает 0.
    ProjectConfig config;
    config.spi_blocks.push_back(makeSpiMaster(0));

    ParseResult<GeneratedProjectFirmwareArduino> firmware = generateProjectFirmwareArduino(config);
    ASSERT_TRUE(firmware.success) << firmware.error;

    const std::string& main_cpp = firmware.config->main_cpp;
    const size_t begin_pos = main_cpp.find("SPI0.begin();");
    const size_t begin_transaction_pos = main_cpp.find("SPI0.beginTransaction(SPISettings());");
    ASSERT_NE(begin_pos, std::string::npos) << main_cpp;
    ASSERT_NE(begin_transaction_pos, std::string::npos) << main_cpp;
    EXPECT_GT(begin_transaction_pos, begin_pos) << main_cpp;
}

TEST(ArduinoProjectCodegen, WarnsAboutSsInPinAsManualCsInGeneratedComment)
{
    // Предупреждение 2026-10-26 (docs/open_tasks.md п.2): P0_3/P1_3 — это
    // аппаратный SS_IN (MODF detect), не свободный GPIO; конфиг не несёт
    // поля с выбранным пользователем CS-пином (он выбирается в коде вне
    // JSON), поэтому проверяем только наличие предупреждающего комментария.
    ProjectConfig config;
    config.spi_blocks.push_back(makeSpiMaster(0));
    config.spi_blocks.push_back(makeSpiMaster(1));

    ParseResult<GeneratedProjectFirmwareArduino> firmware = generateProjectFirmwareArduino(config);
    ASSERT_TRUE(firmware.success) << firmware.error;

    const std::string& main_cpp = firmware.config->main_cpp;
    EXPECT_NE(main_cpp.find("P0_3"), std::string::npos) << main_cpp;
    EXPECT_NE(main_cpp.find("P1_3"), std::string::npos) << main_cpp;
    EXPECT_NE(main_cpp.find("SS_IN"), std::string::npos) << main_cpp;
}

TEST(ArduinoProjectCodegen, RejectsRtcAsUnsupported)
{
    ProjectConfig config;
    RtcConfig rtc_block;
    rtc_block.type = "rtc";
    config.rtc_block = rtc_block;

    ParseResult<GeneratedProjectFirmwareArduino> firmware = generateProjectFirmwareArduino(config);
    EXPECT_FALSE(firmware.success);
    EXPECT_NE(firmware.error.find("RTC"), std::string::npos) << firmware.error;
}

TEST(ArduinoProjectCodegen, GeneratesMainCppWithGpioPinMode)
{
    ProjectConfig config;
    config.gpio_blocks.push_back(makeGpioOutput(1, {5, 6}));

    ParseResult<GeneratedProjectFirmwareArduino> firmware = generateProjectFirmwareArduino(config);
    ASSERT_TRUE(firmware.success) << firmware.error;

    const std::string& main_cpp = firmware.config->main_cpp;
    EXPECT_NE(main_cpp.find("pinMode(P1_5, OUTPUT);"), std::string::npos) << main_cpp;
    EXPECT_NE(main_cpp.find("pinMode(P1_6, OUTPUT);"), std::string::npos) << main_cpp;
    EXPECT_NE(main_cpp.find("void setup()"), std::string::npos);
    EXPECT_NE(main_cpp.find("void loop()"), std::string::npos);
}

TEST(ArduinoProjectCodegen, GeneratesSerialBeginForUart)
{
    ProjectConfig config;
    config.uart_blocks.push_back(makeUart(0, 9600));

    ParseResult<GeneratedProjectFirmwareArduino> firmware = generateProjectFirmwareArduino(config);
    ASSERT_TRUE(firmware.success) << firmware.error;

    const std::string& main_cpp = firmware.config->main_cpp;
    EXPECT_NE(main_cpp.find("Serial0.begin(9600, SERIAL_8N1);"), std::string::npos) << main_cpp;
}

TEST(ArduinoProjectCodegen, GeneratesWireBeginForI2cMaster)
{
    ProjectConfig config;
    config.i2c_blocks.push_back(makeI2c(0, "master"));
    config.i2c_blocks.push_back(makeI2c(1, "master"));

    ParseResult<GeneratedProjectFirmwareArduino> firmware = generateProjectFirmwareArduino(config);
    ASSERT_TRUE(firmware.success) << firmware.error;

    const std::string& main_cpp = firmware.config->main_cpp;
    EXPECT_NE(main_cpp.find("Wire0.begin();"), std::string::npos) << main_cpp;
    EXPECT_NE(main_cpp.find("Wire1.begin();"), std::string::npos) << main_cpp;
    EXPECT_NE(main_cpp.find("#include <Wire.h>"), std::string::npos) << main_cpp;
}

TEST(ArduinoProjectCodegen, RejectsI2cSlaveModeAsUnsupported)
{
    // utility/twi.c (templates/mik32_arduino_core_snapshot) только master —
    // slave-приём апстрима асинхронный и написан под несуществующие в
    // установленном HAL функции (см. комментарий в twi.c).
    ProjectConfig config;
    config.i2c_blocks.push_back(makeI2c(1, "slave", 0x29));

    ParseResult<GeneratedProjectFirmwareArduino> firmware = generateProjectFirmwareArduino(config);
    EXPECT_FALSE(firmware.success);
    EXPECT_NE(firmware.error.find("mode=master"), std::string::npos) << firmware.error;
}

TEST(ArduinoProjectCodegen, GeneratesAnalogWriteForTimer32PwmChannel)
{
    ProjectConfig config;
    Timer32Config timer_block;
    timer_block.type = "timer32";
    timer_block.instance = 1;
    timer_block.clock_source = "prescaler";
    Timer32ChannelConfig channel;
    channel.index = 2;
    channel.mode = "pwm";
    channel.pwm_invert = false;
    channel.has_duty_percent = true;
    channel.requested_duty_percent = 50.0;
    timer_block.channels.push_back(channel);
    timer_block.has_frequency = true;
    timer_block.requested_frequency_hz = 2000;
    config.timer32_blocks.push_back(timer_block);

    ParseResult<GeneratedProjectFirmwareArduino> firmware = generateProjectFirmwareArduino(config);
    ASSERT_TRUE(firmware.success) << firmware.error;

    const std::string& main_cpp = firmware.config->main_cpp;
    EXPECT_NE(main_cpp.find("analogWriteFrequency(2000);"), std::string::npos) << main_cpp;
    EXPECT_NE(main_cpp.find("analogWrite(P0_2, 128);"), std::string::npos) << main_cpp;
}

TEST(ArduinoProjectCodegen, RejectsTimer32PwmWithRawOcrInsteadOfDutyPercent)
{
    ProjectConfig config;
    Timer32Config timer_block;
    timer_block.type = "timer32";
    timer_block.instance = 2;
    timer_block.clock_source = "prescaler";
    Timer32ChannelConfig channel;
    channel.index = 0;
    channel.mode = "pwm";
    channel.ocr = 1000;
    channel.has_duty_percent = false;
    timer_block.channels.push_back(channel);
    config.timer32_blocks.push_back(timer_block);

    ParseResult<GeneratedProjectFirmwareArduino> firmware = generateProjectFirmwareArduino(config);
    EXPECT_FALSE(firmware.success);
    EXPECT_NE(firmware.error.find("duty_percent"), std::string::npos) << firmware.error;
}

TEST(ArduinoProjectCodegen, RejectsTimer32CaptureModeAsUnsupported)
{
    ProjectConfig config;
    Timer32Config timer_block;
    timer_block.type = "timer32";
    timer_block.instance = 1;
    timer_block.clock_source = "prescaler";
    Timer32ChannelConfig channel;
    channel.index = 0;
    channel.mode = "capture";
    channel.capture_edge = "rising";
    timer_block.channels.push_back(channel);
    config.timer32_blocks.push_back(timer_block);

    ParseResult<GeneratedProjectFirmwareArduino> firmware = generateProjectFirmwareArduino(config);
    EXPECT_FALSE(firmware.success);
    EXPECT_NE(firmware.error.find("mode=pwm"), std::string::npos) << firmware.error;
}

namespace
{
    Timer16Config makeTimer16Pwm(int instance, int prescaler, uint16_t arr, uint16_t cmp)
    {
        Timer16Config block;
        block.type = "timer16";
        block.instance = instance;
        block.mode = "period";
        block.prescaler = prescaler;
        block.arr = arr;
        block.cmp = cmp;
        block.clock_source = "internal_system";
        block.trigger_edge = "software";
        block.trigger_source = "";
        block.external_clock_filter = "none";
        block.trigger_filter = "none";
        block.waveform_inverted = false;
        block.overflow_interrupt = false;
        block.compare_interrupt = false;
        return block;
    }
}

TEST(ArduinoProjectCodegen, GeneratesHybridHalCallForTimer16Pwm)
{
    // Та же пара ARR/CMP, что HW-подтверждённый вендорный пример в
    // docs/implementations/timer16.md (tests/fixtures/timer16_valid.json) —
    // Timer16_1, PRESCALER_1, скважность ~50% на GPIO_0/PIN_10.
    ProjectConfig config;
    config.timer16_blocks.push_back(makeTimer16Pwm(1, 1, 0xFFFF, 0x7FFF));

    ParseResult<GeneratedProjectFirmwareArduino> firmware = generateProjectFirmwareArduino(config);
    ASSERT_TRUE(firmware.success) << firmware.error;

    const std::string& main_cpp = firmware.config->main_cpp;
    EXPECT_NE(main_cpp.find("Timer16_HandleTypeDef htimer16_1;"), std::string::npos) << main_cpp;
    EXPECT_NE(main_cpp.find("htimer16_1.Instance = TIMER16_1;"), std::string::npos) << main_cpp;
    EXPECT_NE(main_cpp.find("htimer16_1.Clock.Prescaler = TIMER16_PRESCALER_1;"), std::string::npos) << main_cpp;
    EXPECT_NE(main_cpp.find("HAL_Timer16_Init(&htimer16_1);"), std::string::npos) << main_cpp;
    EXPECT_NE(main_cpp.find("HAL_Timer16_StartPWM(&htimer16_1, 65535, 32767);"), std::string::npos) << main_cpp;
    EXPECT_NE(main_cpp.find("extern \"C\" {\n#include <mik32_hal_timer16.h>\n}"), std::string::npos) << main_cpp;
}

TEST(ArduinoProjectCodegen, RejectsTimer16OneShotModeAsUnsupported)
{
    ProjectConfig config;
    Timer16Config block = makeTimer16Pwm(1, 1, 0xFFFF, 0x7FFF);
    block.mode = "one_shot";
    config.timer16_blocks.push_back(block);

    ParseResult<GeneratedProjectFirmwareArduino> firmware = generateProjectFirmwareArduino(config);
    EXPECT_FALSE(firmware.success);
    EXPECT_NE(firmware.error.find("mode=period"), std::string::npos) << firmware.error;
}

TEST(ArduinoProjectCodegen, RejectsTimer16NonDefaultClockSourceAsUnsupported)
{
    ProjectConfig config;
    Timer16Config block = makeTimer16Pwm(1, 1, 0xFFFF, 0x7FFF);
    block.clock_source = "internal_hsi32m";
    config.timer16_blocks.push_back(block);

    ParseResult<GeneratedProjectFirmwareArduino> firmware = generateProjectFirmwareArduino(config);
    EXPECT_FALSE(firmware.success);
    EXPECT_NE(firmware.error.find("clock_source"), std::string::npos) << firmware.error;
}

TEST(ArduinoProjectCodegen, RejectsTimer16HardwareTriggerAsUnsupported)
{
    ProjectConfig config;
    Timer16Config block = makeTimer16Pwm(1, 1, 0xFFFF, 0x7FFF);
    block.trigger_edge = "rising";
    config.timer16_blocks.push_back(block);

    ParseResult<GeneratedProjectFirmwareArduino> firmware = generateProjectFirmwareArduino(config);
    EXPECT_FALSE(firmware.success);
    EXPECT_NE(firmware.error.find("триггер"), std::string::npos) << firmware.error;
}

TEST(ArduinoProjectCodegen, RejectsTimer16InterruptsAsUnsupported)
{
    ProjectConfig config;
    Timer16Config block = makeTimer16Pwm(1, 1, 0xFFFF, 0x7FFF);
    block.compare_interrupt = true;
    config.timer16_blocks.push_back(block);

    ParseResult<GeneratedProjectFirmwareArduino> firmware = generateProjectFirmwareArduino(config);
    EXPECT_FALSE(firmware.success);
    EXPECT_NE(firmware.error.find("прерывания Timer16"), std::string::npos) << firmware.error;
}

TEST(ArduinoProjectCodegen, WritesProjectFilesToOutputDirectory)
{
    ProjectConfig config;
    config.gpio_blocks.push_back(makeGpioOutput(0, {3}));

    ParseResult<GeneratedProjectFirmwareArduino> firmware = generateProjectFirmwareArduino(config);
    ASSERT_TRUE(firmware.success) << firmware.error;

    const fs::path output_dir = scratchDirFor("write_basic");
    std::error_code ec;
    fs::remove_all(output_dir, ec);

    nlohmann::json project_json;
    project_json["mcu"] = "MIK32V2";
    project_json["version"] = "1.0";
    project_json["peripherals"] = nlohmann::json::array();

    CodegenWriteResult write_result = writeGeneratedArduinoProject(
        *firmware.config,
        "../templates/pio_skeleton_arduino",
        "../templates/mik32_arduino_core_snapshot",
        output_dir.string(),
        project_json);
    ASSERT_TRUE(write_result.success) << write_result.error;

    EXPECT_TRUE(fs::exists(output_dir / "src" / "main.cpp"));
    EXPECT_TRUE(fs::exists(output_dir / "platformio.ini"));
    EXPECT_TRUE(fs::exists(output_dir / "lib" / "mik32_arduino_core"));
    EXPECT_TRUE(fs::exists(output_dir / "project.mik32"));

    const std::string main_cpp = readFileToString(output_dir / "src" / "main.cpp");
    EXPECT_NE(main_cpp.find("pinMode(P0_3, OUTPUT);"), std::string::npos) << main_cpp;
}

TEST(ArduinoProjectCodegen, PreservesUserCodeInMainCppAcrossRegeneration)
{
    ProjectConfig config;
    config.gpio_blocks.push_back(makeGpioOutput(0, {3}));

    ParseResult<GeneratedProjectFirmwareArduino> firmware = generateProjectFirmwareArduino(config);
    ASSERT_TRUE(firmware.success) << firmware.error;

    const fs::path output_dir = scratchDirFor("user_code_preserved");
    std::error_code ec;
    fs::remove_all(output_dir, ec);

    nlohmann::json project_json;
    project_json["mcu"] = "MIK32V2";

    ASSERT_TRUE(writeGeneratedArduinoProject(*firmware.config, "../templates/pio_skeleton_arduino",
        "../templates/mik32_arduino_core_snapshot", output_dir.string(), project_json).success);

    const fs::path main_cpp_path = output_dir / "src" / "main.cpp";
    std::string edited = readFileToString(main_cpp_path);
    const std::string marker = "// USER CODE BEGIN Loop";
    const size_t marker_pos = edited.find(marker);
    ASSERT_NE(marker_pos, std::string::npos);
    edited.insert(marker_pos + marker.size(), "\n  // ручная правка пользователя\n");
    {
        std::ofstream out(main_cpp_path, std::ios::trunc);
        out << edited;
    }

    // Повторная генерация с тем же конфигом — правка должна выжить.
    ASSERT_TRUE(writeGeneratedArduinoProject(*firmware.config, "../templates/pio_skeleton_arduino",
        "../templates/mik32_arduino_core_snapshot", output_dir.string(), project_json).success);

    const std::string regenerated = readFileToString(main_cpp_path);
    EXPECT_NE(regenerated.find("// ручная правка пользователя"), std::string::npos) << regenerated;
}

// --- Clock/PCC (CLAUDE.md, Tier 1) ------------------------------------
// board.cpp::SystemInit() вендоренного Arduino-ядра (не main.cpp) —
// см. комментарий у GeneratedProjectFirmwareArduino::system_init_pcc_block.

TEST(ArduinoProjectCodegen, DefaultClockProducesSameBlockAsVendoredBoardCppHardcode)
{
    // ClockConfig{} (поле "clock" не задано в JSON) обязано отрендерить
    // ровно тот же PCC_OscInit, что был зашит в board.cpp до этой фичи
    // (см. CLAUDE.md "Баг 2026-10-26" в комментарии board.cpp) — иначе
    // поведение проектов без "clock" в конфиге изменилось бы незаметно.
    ProjectConfig config;
    config.gpio_blocks.push_back(makeGpioOutput(0, {3}));

    ParseResult<GeneratedProjectFirmwareArduino> firmware = generateProjectFirmwareArduino(config);
    ASSERT_TRUE(firmware.success) << firmware.error;

    const std::string& block = firmware.config->system_init_pcc_block;
    EXPECT_NE(block.find("PCC_OscInit.OscillatorEnable = PCC_OSCILLATORTYPE_HSI32M | PCC_OSCILLATORTYPE_OSC32M | "
                         "PCC_OSCILLATORTYPE_LSI32K | PCC_OSCILLATORTYPE_OSC32K;"), std::string::npos) << block;
    EXPECT_NE(block.find("PCC_OscInit.FreqMon.OscillatorSystem = PCC_OSCILLATORTYPE_OSC32M;"), std::string::npos) << block;
    EXPECT_NE(block.find("PCC_OscInit.FreqMon.ForceOscSys = PCC_FORCE_OSC_SYS_UNFIXED;"), std::string::npos) << block;
    EXPECT_NE(block.find("PCC_OscInit.FreqMon.Force32KClk = PCC_FREQ_MONITOR_SOURCE_OSC32K;"), std::string::npos) << block;
    EXPECT_NE(block.find("PCC_OscInit.AHBDivider = 0;"), std::string::npos) << block;
    EXPECT_NE(block.find("PCC_OscInit.APBMDivider = 0;"), std::string::npos) << block;
    EXPECT_NE(block.find("PCC_OscInit.APBPDivider = 0;"), std::string::npos) << block;
    EXPECT_NE(block.find("PCC_OscInit.HSI32MCalibrationValue = 128;"), std::string::npos) << block;
    EXPECT_NE(block.find("PCC_OscInit.LSI32KCalibrationValue = 8;"), std::string::npos) << block;
    EXPECT_NE(block.find("PCC_OscInit.RTCClockSelection = PCC_RTC_CLOCK_SOURCE_AUTO;"), std::string::npos) << block;
    EXPECT_NE(block.find("PCC_OscInit.RTCClockCPUSelection = PCC_CPU_RTC_CLOCK_SOURCE_OSC32K;"), std::string::npos) << block;
    EXPECT_NE(block.find("HAL_PCC_Config(&PCC_OscInit);"), std::string::npos) << block;
}

TEST(ArduinoProjectCodegen, CustomClockBlockOverridesOscillatorAndDividers)
{
    // Нестандартная частота через HSI32M (внутренний RC) + делители AHB/APB —
    // ровно те поля, что ТЗ/аудит требуют для Clock/PCC (CLAUDE.md, Tier 1).
    ProjectConfig config;
    config.gpio_blocks.push_back(makeGpioOutput(0, {3}));
    config.clock.oscillator_system = "hsi32m";
    config.clock.ahb_divider = 1;
    config.clock.apbm_divider = 2;
    config.clock.apbp_divider = 3;
    config.clock.hsi32m_calibration = 200;
    config.clock.lsi32k_calibration = 4;
    config.clock.rtc_clock_source = "lsi32k";
    config.clock.rtc_clock_cpu_source = "lsi32k";
    config.clock.oscillator_enable = {"hsi32m", "lsi32k"};
    config.clock.force_osc_sys = true;
    config.clock.force_32k_clk = "lsi32k";

    ParseResult<GeneratedProjectFirmwareArduino> firmware = generateProjectFirmwareArduino(config);
    ASSERT_TRUE(firmware.success) << firmware.error;

    const std::string& block = firmware.config->system_init_pcc_block;
    EXPECT_NE(block.find("PCC_OscInit.OscillatorEnable = PCC_OSCILLATORTYPE_HSI32M | PCC_OSCILLATORTYPE_LSI32K;"),
        std::string::npos) << block;
    EXPECT_NE(block.find("PCC_OscInit.FreqMon.OscillatorSystem = PCC_OSCILLATORTYPE_HSI32M;"), std::string::npos) << block;
    EXPECT_NE(block.find("PCC_OscInit.FreqMon.ForceOscSys = PCC_FORCE_OSC_SYS_FIXED;"), std::string::npos) << block;
    EXPECT_NE(block.find("PCC_OscInit.FreqMon.Force32KClk = PCC_FREQ_MONITOR_SOURCE_LSI32K;"), std::string::npos) << block;
    EXPECT_NE(block.find("PCC_OscInit.AHBDivider = 1;"), std::string::npos) << block;
    EXPECT_NE(block.find("PCC_OscInit.APBMDivider = 2;"), std::string::npos) << block;
    EXPECT_NE(block.find("PCC_OscInit.APBPDivider = 3;"), std::string::npos) << block;
    EXPECT_NE(block.find("PCC_OscInit.HSI32MCalibrationValue = 200;"), std::string::npos) << block;
    EXPECT_NE(block.find("PCC_OscInit.LSI32KCalibrationValue = 4;"), std::string::npos) << block;
    EXPECT_NE(block.find("PCC_OscInit.RTCClockSelection = PCC_RTC_CLOCK_SOURCE_LSI32K;"), std::string::npos) << block;
    EXPECT_NE(block.find("PCC_OscInit.RTCClockCPUSelection = PCC_CPU_RTC_CLOCK_SOURCE_LSI32K;"), std::string::npos) << block;
}

TEST(ArduinoProjectCodegen, WritesPatchedPccBlockIntoBoardCppOnDisk)
{
    // HW-поведенческий гейт (CLAUDE.md): собрать прошивку с нестандартной
    // частотой через PCC и сверить baud Serial0 на UART-логе — см.
    // docs/open_tasks.md. Этот тест проверяет только host-side артефакт
    // (файл на диске), не поведение на кристалле.
    ProjectConfig config;
    config.gpio_blocks.push_back(makeGpioOutput(0, {3}));
    config.clock.oscillator_system = "hsi32m";
    config.clock.ahb_divider = 1;
    config.clock.oscillator_enable = {"hsi32m", "osc32m", "lsi32k", "osc32k"};

    ParseResult<GeneratedProjectFirmwareArduino> firmware = generateProjectFirmwareArduino(config);
    ASSERT_TRUE(firmware.success) << firmware.error;

    const fs::path output_dir = scratchDirFor("clock_pcc_board_cpp");
    std::error_code ec;
    fs::remove_all(output_dir, ec);

    nlohmann::json project_json;
    project_json["mcu"] = "MIK32V2";
    project_json["version"] = "1.0";
    project_json["peripherals"] = nlohmann::json::array();

    CodegenWriteResult write_result = writeGeneratedArduinoProject(
        *firmware.config,
        "../templates/pio_skeleton_arduino",
        "../templates/mik32_arduino_core_snapshot",
        output_dir.string(),
        project_json);
    ASSERT_TRUE(write_result.success) << write_result.error;

    const fs::path board_cpp_path =
        output_dir / "lib" / "mik32_arduino_core" / "src" / "cores" / "arduino" / "board.cpp";
    ASSERT_TRUE(fs::exists(board_cpp_path));
    const std::string board_cpp = readFileToString(board_cpp_path);

    // Патченное значение попало в файл на диске...
    EXPECT_NE(board_cpp.find("PCC_OscInit.FreqMon.OscillatorSystem = PCC_OSCILLATORTYPE_HSI32M;"), std::string::npos) << board_cpp;
    EXPECT_NE(board_cpp.find("PCC_OscInit.AHBDivider = 1;"), std::string::npos) << board_cpp;
    // ...а остальной SystemInit() (write_csr/HAL_Init/GPIO-тактирование/
    // SysTick/updateCacheLimit) не затронут патчем.
    EXPECT_NE(board_cpp.find("write_csr(mtvec, 0x02000000);"), std::string::npos) << board_cpp;
    EXPECT_NE(board_cpp.find("__HAL_PCC_GPIO_0_CLK_ENABLE();"), std::string::npos) << board_cpp;
    EXPECT_NE(board_cpp.find("SysTick_Init();"), std::string::npos) << board_cpp;
    EXPECT_NE(board_cpp.find("updateCacheLimit();"), std::string::npos) << board_cpp;
}

// --- F_CPU в platformio.ini (баг найден 2026-10-26: HardwareSerial.cpp/
// Tone.cpp считают тайминги от F_CPU, захардкоженного в шаблоне platformio.ini
// независимо от реального Clock-блока) -----------------------------------

TEST(ArduinoProjectCodegen, DefaultClockDoesNotPatchPlatformioIni)
{
    // Без Clock-блока (ClockConfig{} == 32 МГц, как было всегда) —
    // platformio.ini не должен отличаться от шаблона.
    ProjectConfig config;
    config.gpio_blocks.push_back(makeGpioOutput(0, {3}));

    ParseResult<GeneratedProjectFirmwareArduino> firmware = generateProjectFirmwareArduino(config);
    ASSERT_TRUE(firmware.success) << firmware.error;
    EXPECT_EQ(firmware.config->ahb_frequency_hz, 32000000LL);

    const fs::path output_dir = scratchDirFor("fcpu_default_untouched");
    std::error_code ec;
    fs::remove_all(output_dir, ec);

    nlohmann::json project_json;
    project_json["mcu"] = "MIK32V2";
    project_json["version"] = "1.0";
    project_json["peripherals"] = nlohmann::json::array();

    ASSERT_TRUE(writeGeneratedArduinoProject(*firmware.config, "../templates/pio_skeleton_arduino",
        "../templates/mik32_arduino_core_snapshot", output_dir.string(), project_json).success);

    const std::string pio_ini = readFileToString(output_dir / "platformio.ini");
    EXPECT_NE(pio_ini.find("-D F_CPU=32000000UL"), std::string::npos) << pio_ini;
}

TEST(ArduinoProjectCodegen, NonDefaultClockPatchesFCpuInPlatformioIni)
{
    // AHBDivider=1 при номинале 32 МГц -> реальная частота ядра 16 МГц
    // (project_config.h::computeAhbFrequencyHz, формула из mik32_hal_pcc.c).
    // platformio.ini должен получить это значение, иначе HardwareSerial.cpp
    // считает BRR от неверного F_CPU — framing errors на реальном UART
    // (подтверждено на 1T Rex Board, см. docs/implementations/arduino_codegen.md).
    ProjectConfig config;
    config.gpio_blocks.push_back(makeGpioOutput(0, {3}));
    config.clock.ahb_divider = 1;

    ParseResult<GeneratedProjectFirmwareArduino> firmware = generateProjectFirmwareArduino(config);
    ASSERT_TRUE(firmware.success) << firmware.error;
    EXPECT_EQ(firmware.config->ahb_frequency_hz, 16000000LL);

    const fs::path output_dir = scratchDirFor("fcpu_patched");
    std::error_code ec;
    fs::remove_all(output_dir, ec);

    nlohmann::json project_json;
    project_json["mcu"] = "MIK32V2";
    project_json["version"] = "1.0";
    project_json["peripherals"] = nlohmann::json::array();

    ASSERT_TRUE(writeGeneratedArduinoProject(*firmware.config, "../templates/pio_skeleton_arduino",
        "../templates/mik32_arduino_core_snapshot", output_dir.string(), project_json).success);

    const std::string pio_ini = readFileToString(output_dir / "platformio.ini");
    EXPECT_NE(pio_ini.find("-D F_CPU=16000000UL"), std::string::npos) << pio_ini;
    EXPECT_EQ(pio_ini.find("-D F_CPU=32000000UL"), std::string::npos) << pio_ini;
}
