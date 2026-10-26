//
// Rust-ветка кодогенерации — тесты. В отличие от C-ветки (test_parsers.cpp),
// здесь ОБЯЗАТЕЛЕН как минимум один тест с реальным `cargo build`
// (RustProjectCodegen.WrittenProjectActuallyBuildsWithCargo) — решение из
// /grill-me сессии (docs/implementations/rust_codegen.md): золотые файлы и
// сравнение текста не ловят ошибки компиляции/линковки самого Rust-кода,
// только настоящая сборка тулчейном (та же логика, что нашла 3 линковочных
// бага в CMake-ветке).
//
#include <gtest/gtest.h>
#include "project_config.h"
#include "rust_feature_validator.h"
#include "rust_project_codegen.h"
#include "user_code_markers_rust.h"
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

namespace
{
    // Пишем во временную директорию ОС, а не в generated_projects/ — та
    // предназначена для реального вывода конфигуратора, а не для мусора от
    // прогонов ctest (находка из /grill-me: test_rust_* папки там
    // перезаписывались при каждом полном прогоне тестов).
    fs::path scratchDirFor(const std::string& name)
    {
        return fs::temp_directory_path() / ("mik32_test_rust_" + name);
    }

    std::string readFileToString(const std::string& path)
    {
        std::ifstream file(path, std::ios::binary);
        std::ostringstream buffer;
        buffer << file.rdbuf();
        return buffer.str();
    }
}

TEST(RustProjectCodegen, ParsesLanguageRustAndAutoSelectsCargoToolchain)
{
    ParseResult<ProjectConfig> config_result = parseProjectConfig("fixtures/project_rust_gpio_uart.json");
    ASSERT_TRUE(config_result.success) << config_result.error;
    EXPECT_EQ(config_result.config->language, OutputLanguage::Rust);
    EXPECT_EQ(config_result.config->toolchain, ToolchainKind::Cargo);
}

TEST(RustProjectCodegen, GeneratesMainRsWithRealBaudDriveStrengthAndDirection)
{
    ParseResult<ProjectConfig> config_result = parseProjectConfig("fixtures/project_rust_gpio_uart.json");
    ASSERT_TRUE(config_result.success) << config_result.error;

    EXPECT_EQ(findUnsupportedRustFeature(*config_result.config), "");

    ParseResult<GeneratedProjectFirmwareRust> firmware = generateProjectFirmwareRust(*config_result.config);
    ASSERT_TRUE(firmware.success) << firmware.error;

    const std::string& main_rs = firmware.config->main_rs;
    // GPIO: instance=2, pin=7, mode=output, drive_strength=8mA.
    EXPECT_NE(main_rs.find("gpio2.p8_2_7.into_output()"), std::string::npos) << main_rs;
    EXPECT_NE(main_rs.find("DriveStrength::Ma8"), std::string::npos) << main_rs;
    // UART: instance=0 -> фиксированные пины P16_0_6 (tx) / P16_0_5 (rx).
    EXPECT_NE(main_rs.find("gpio0.p16_0_6.into_serial_port()"), std::string::npos) << main_rs;
    EXPECT_NE(main_rs.find("gpio0.p16_0_5.into_serial_port()"), std::string::npos) << main_rs;
    EXPECT_NE(main_rs.find("baud_rate: 9600"), std::string::npos) << main_rs;
    EXPECT_NE(main_rs.find("data_bits: DataBits::Eight"), std::string::npos) << main_rs;
    EXPECT_NE(main_rs.find("stop_bits: StopBits::One"), std::string::npos) << main_rs;
    EXPECT_NE(main_rs.find("parity: Parity::None"), std::string::npos) << main_rs;
    // direction=duplex -> оба направления включены.
    EXPECT_NE(main_rs.find("transmitting: true"), std::string::npos) << main_rs;
    EXPECT_NE(main_rs.find("receiving: true"), std::string::npos) << main_rs;
    // Обязательные "use ... as _;" (см. rust_project_codegen.cpp — без них
    // либо не собирается, либо тихо портится без точки входа).
    EXPECT_NE(main_rs.find("use mik32_rt as _;"), std::string::npos) << main_rs;
    EXPECT_NE(main_rs.find("use riscv as _;"), std::string::npos) << main_rs;
}

// I2C — реализован 2026-10-26 (docs/implementations/rust/i2c.md), больше не
// отклоняется целиком. Проверяем сгенерированный main.rs: slave-режим,
// фильтры, general call, второй адрес+маска, HW-подтверждённый TIMINGR-пресет
// для 100 кГц (тот же, что и C-ветка — codegen/project/i2c_contribution.cpp,
// calcI2cTimingr: PRESC=1, SCLDEL=15, SDADEL=15, SCLH=75, SCLL=75).
TEST(RustProjectCodegen, GeneratesSlaveI2cWithFiltersDualAddressAndConfirmedTiming)
{
    ParseResult<ProjectConfig> config_result = parseProjectConfig("fixtures/project_rust_i2c.json");
    ASSERT_TRUE(config_result.success) << config_result.error;

    EXPECT_EQ(findUnsupportedRustFeature(*config_result.config), "");

    ParseResult<GeneratedProjectFirmwareRust> firmware = generateProjectFirmwareRust(*config_result.config);
    ASSERT_TRUE(firmware.success) << firmware.error;

    const std::string& main_rs = firmware.config->main_rs;
    // instance=1 -> фиксированные пины P16_1_12/P16_1_13 (GPIO_1).
    EXPECT_NE(main_rs.find("gpio1.p16_1_12.into_serial_port()"), std::string::npos) << main_rs;
    EXPECT_NE(main_rs.find("gpio1.p16_1_13.into_serial_port()"), std::string::npos) << main_rs;
    EXPECT_NE(main_rs.find("i2c1_pin_a.set_pull_up()"), std::string::npos) << main_rs;
    EXPECT_NE(main_rs.find("i2c1_pin_b.set_pull_up()"), std::string::npos) << main_rs;
    EXPECT_NE(main_rs.find("mode: i2c::Mode::Slave"), std::string::npos) << main_rs;
    EXPECT_NE(main_rs.find("presc: 1, scldel: 15, sdadel: 15, sclh: 75, scll: 75"), std::string::npos) << main_rs;
    EXPECT_NE(main_rs.find("digital_filter: 2"), std::string::npos) << main_rs;
    EXPECT_NE(main_rs.find("analog_filter: true"), std::string::npos) << main_rs;
    EXPECT_NE(main_rs.find("own_address: 41"), std::string::npos) << main_rs;
    EXPECT_NE(main_rs.find("address_mode: i2c::AddressMode::Seven"), std::string::npos) << main_rs;
    EXPECT_NE(main_rs.find("dual_address_enabled: true"), std::string::npos) << main_rs;
    EXPECT_NE(main_rs.find("own_address_2: 10"), std::string::npos) << main_rs;
    EXPECT_NE(main_rs.find("own_address_2_mask: i2c::OwnAddress2Mask::Mask111111X"), std::string::npos) << main_rs;
    EXPECT_NE(main_rs.find("general_call: true"), std::string::npos) << main_rs;
    EXPECT_NE(main_rs.find("use mik32v2_hal::i2c::{self, I2c};"), std::string::npos) << main_rs;
}

// DMA для I2C отклоняется (ни PAC, ни mik32v2-hal не содержат драйвера
// контроллера DMA — тот же критерий, что и у UART DMA).
TEST(RustProjectCodegen, RejectsI2cDmaAsUnsupported)
{
    ParseResult<ProjectConfig> config_result = parseProjectConfig("fixtures/project_rust_i2c.json");
    ASSERT_TRUE(config_result.success) << config_result.error;
    ProjectConfig config = *config_result.config;

    I2cDmaConfig dma;
    dma.channel = 0;
    dma.direction = "rx";
    config.i2c_blocks[0].dma.push_back(dma);

    const std::string reason = findUnsupportedRustFeature(config);
    EXPECT_NE(reason, "");
    EXPECT_NE(reason.find("DMA"), std::string::npos) << reason;

    ParseResult<GeneratedProjectFirmwareRust> firmware = generateProjectFirmwareRust(config);
    EXPECT_FALSE(firmware.success);
}

// interrupt_mode (шаблон IRQHandler) отклоняется — mik32v2-hal не оборачивает
// контроллер EPIC (как и для GPIO IRQ).
TEST(RustProjectCodegen, RejectsI2cInterruptModeAsUnsupported)
{
    ParseResult<ProjectConfig> config_result = parseProjectConfig("fixtures/project_rust_i2c.json");
    ASSERT_TRUE(config_result.success) << config_result.error;
    ProjectConfig config = *config_result.config;
    config.i2c_blocks[0].interrupt_mode = true;

    const std::string reason = findUnsupportedRustFeature(config);
    EXPECT_NE(reason, "");
    EXPECT_NE(reason.find("interrupt_mode"), std::string::npos) << reason;
}

// Timer16 — реализован (docs/implementations/rust/timer16.md). mode=period,
// instance=1 -> фиксированный ШИМ-пин P16_0_10 (into_timer_serial(), не
// into_serial_port() — PADx_CFG=0b10, mik32_hal_gpio.h HAL_GPIO_MODE_TIMER_
// SERIAL). Проверяем clock_source/prescaler/trigger_edge/фильтры/
// waveform_inverted/arr/cmp попадают в сгенерированный main.rs.
TEST(RustProjectCodegen, GeneratesPeriodTimer16WithClockSourceFiltersAndInvertedWaveform)
{
    ParseResult<ProjectConfig> config_result = parseProjectConfig("fixtures/project_rust_timer16.json");
    ASSERT_TRUE(config_result.success) << config_result.error;

    EXPECT_EQ(findUnsupportedRustFeature(*config_result.config), "");

    ParseResult<GeneratedProjectFirmwareRust> firmware = generateProjectFirmwareRust(*config_result.config);
    ASSERT_TRUE(firmware.success) << firmware.error;

    const std::string& main_rs = firmware.config->main_rs;
    EXPECT_NE(main_rs.find("gpio0.p16_0_10.into_timer_serial()"), std::string::npos) << main_rs;
    EXPECT_NE(main_rs.find("Timer16::new_pwm("), std::string::npos) << main_rs;
    EXPECT_NE(main_rs.find("p.timer16_1"), std::string::npos) << main_rs;
    EXPECT_NE(main_rs.find("timer16::ClockSource::Hsi32m"), std::string::npos) << main_rs;
    EXPECT_NE(main_rs.find("timer16::Prescaler::P8"), std::string::npos) << main_rs;
    EXPECT_NE(main_rs.find("timer16::TriggerEdge::Rising"), std::string::npos) << main_rs;
    EXPECT_NE(main_rs.find("timer16::Filter::Clock4"), std::string::npos) << main_rs;
    EXPECT_NE(main_rs.find("timer16::Filter::Clock2"), std::string::npos) << main_rs;
    // waveform_inverted=true, mode=period (не one_shot) -> второй аргумент
    // true, third (one_shot) false.
    EXPECT_NE(main_rs.find("true,\n        false,\n        999,\n        250,"), std::string::npos) << main_rs;
    EXPECT_NE(main_rs.find("use mik32v2_hal::timer16::{self, Timer16};"), std::string::npos) << main_rs;
}

// mode=external_event_count — использует Input1, не ШИМ-пин; Waveform.Enable
// всегда DISABLE (mode_waveform_enable в knowledge_base/timer16.json), поэтому
// Timer16::new_counter не принимает waveform_inverted/cmp вовсе.
TEST(RustProjectCodegen, GeneratesExternalEventCountTimer16UsingInput1Pin)
{
    ParseResult<ProjectConfig> config_result = parseProjectConfig("fixtures/project_rust_timer16.json");
    ASSERT_TRUE(config_result.success) << config_result.error;
    ProjectConfig config = *config_result.config;
    config.timer16_blocks[0].mode = "external_event_count";

    ParseResult<GeneratedProjectFirmwareRust> firmware = generateProjectFirmwareRust(config);
    ASSERT_TRUE(firmware.success) << firmware.error;

    const std::string& main_rs = firmware.config->main_rs;
    // instance=1 -> Input1 фиксирован на P16_0_8 (knowledge_base/timer16.json,
    // instance_fixed_input1_pin).
    EXPECT_NE(main_rs.find("gpio0.p16_0_8.into_timer_serial()"), std::string::npos) << main_rs;
    EXPECT_NE(main_rs.find("Timer16::new_counter("), std::string::npos) << main_rs;
    EXPECT_EQ(main_rs.find("p16_0_10"), std::string::npos) << main_rs; // ШИМ-пин не занят
}

// mode=encoder — занимает Input1+Input2, prescaler обязан быть 1 (парсер уже
// это проверяет — см. parser/timer16/timer16_parser.cpp), здесь только
// проверяем, что Rust-ветка генерирует new_encoder с обоими пинами.
TEST(RustProjectCodegen, GeneratesEncoderTimer16UsingBothInputPins)
{
    ParseResult<ProjectConfig> config_result = parseProjectConfig("fixtures/project_rust_timer16.json");
    ASSERT_TRUE(config_result.success) << config_result.error;
    ProjectConfig config = *config_result.config;
    config.timer16_blocks[0].mode = "encoder";
    config.timer16_blocks[0].prescaler = 1;

    ParseResult<GeneratedProjectFirmwareRust> firmware = generateProjectFirmwareRust(config);
    ASSERT_TRUE(firmware.success) << firmware.error;

    const std::string& main_rs = firmware.config->main_rs;
    EXPECT_NE(main_rs.find("gpio0.p16_0_8.into_timer_serial()"), std::string::npos) << main_rs;
    EXPECT_NE(main_rs.find("gpio0.p16_0_9.into_timer_serial()"), std::string::npos) << main_rs;
    EXPECT_NE(main_rs.find("Timer16::new_encoder("), std::string::npos) << main_rs;
}

// Trigger.Source не реализован даже в C-ветке (schemas/timer16.json:
// not_covered_by_this_codegen.trigger) — тот же пробел в Rust-ветке.
TEST(RustProjectCodegen, RejectsTimer16TriggerSourceAsUnsupported)
{
    ParseResult<ProjectConfig> config_result = parseProjectConfig("fixtures/project_rust_timer16.json");
    ASSERT_TRUE(config_result.success) << config_result.error;
    ProjectConfig config = *config_result.config;
    config.timer16_blocks[0].trigger_source = "gpio_a";

    const std::string reason = findUnsupportedRustFeature(config);
    EXPECT_NE(reason, "");
    EXPECT_NE(reason.find("trigger_source"), std::string::npos) << reason;
}

// overflow_interrupt/compare_interrupt — нет обёртки EPIC/trap_handler в
// mik32v2-hal, тот же пробел, что у UART/I2C.
TEST(RustProjectCodegen, RejectsTimer16InterruptsAsUnsupported)
{
    ParseResult<ProjectConfig> config_result = parseProjectConfig("fixtures/project_rust_timer16.json");
    ASSERT_TRUE(config_result.success) << config_result.error;
    ProjectConfig config = *config_result.config;
    config.timer16_blocks[0].compare_interrupt = true;

    const std::string reason = findUnsupportedRustFeature(config);
    EXPECT_NE(reason, "");
    EXPECT_NE(reason.find("interrupt"), std::string::npos) << reason;
}

// Реальная сборка Timer16-проекта cargo'м — по образцу
// WrittenI2cProjectActuallyBuildsWithCargo: timer16.rs — новый файл
// HAL-снэпшота, синтаксические ошибки в macro_rules! impl_instance! (3 разных
// PAC-типа Timer16_0/1/2) не поймать ничем, кроме настоящей компиляции.
TEST(RustProjectCodegen, WrittenTimer16ProjectActuallyBuildsWithCargo)
{
    ParseResult<ProjectConfig> config_result = parseProjectConfig("fixtures/project_rust_timer16.json");
    ASSERT_TRUE(config_result.success) << config_result.error;
    const ProjectConfig& config = *config_result.config;

    ParseResult<GeneratedProjectFirmwareRust> firmware = generateProjectFirmwareRust(config);
    ASSERT_TRUE(firmware.success) << firmware.error;

    const fs::path output_dir = scratchDirFor("cargo_build_timer16");
    std::error_code ec;
    fs::remove_all(output_dir, ec);

    nlohmann::json project_json;
    {
        std::ifstream fixture_file("fixtures/project_rust_timer16.json");
        project_json = nlohmann::json::parse(fixture_file);
    }

    CodegenWriteResult write_result = writeGeneratedRustProject(
        *firmware.config,
        "../templates/cargo_skeleton",
        "../templates/mik32_rust_hal_snapshot",
        output_dir.string(),
        config,
        project_json);
    ASSERT_TRUE(write_result.success) << write_result.error;

    const fs::path absolute_output_dir = fs::absolute(output_dir);
    const std::string command = "cd \"" + absolute_output_dir.string() + "\" && cargo build --release 2>&1";
    std::string cargo_output;
    FILE* pipe = popen(command.c_str(), "r");
    ASSERT_NE(pipe, nullptr) << "не удалось запустить cargo build";
    char buffer[4096];
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr)
    {
        cargo_output += buffer;
    }
    const int status = pclose(pipe);

    EXPECT_EQ(status, 0) << "cargo build завершился с ошибкой, вывод:\n" << cargo_output;
    const fs::path elf_path = output_dir / "target" / "riscv32imc-unknown-none-elf" / "release" / "mik32-firmware";
    EXPECT_TRUE(fs::exists(elf_path))
        << "cargo build 'успешен' (код 0), но ELF-файл не появился — вывод:\n" << cargo_output;
}

// AUDIT_TZ_PERIPHERY_GAPS.md, Тир 2 — RTC добавлен только в C-ветке.
TEST(RustProjectCodegen, RejectsRtcBlockAsUnsupportedInRustBranch)
{
    ParseResult<ProjectConfig> config_result = parseProjectConfig("fixtures/project_rust_gpio_uart.json");
    ASSERT_TRUE(config_result.success) << config_result.error;
    ProjectConfig config = *config_result.config;

    RtcConfig rtc_block;
    rtc_block.type = "rtc";
    rtc_block.dow = 1;
    rtc_block.hours = 0;
    rtc_block.minutes = 0;
    rtc_block.seconds = 0;
    rtc_block.century = 20;
    rtc_block.day = 1;
    rtc_block.month = 1;
    rtc_block.year = 22;
    config.rtc_block = rtc_block;

    const std::string reason = findUnsupportedRustFeature(config);
    EXPECT_NE(reason, "");
    EXPECT_NE(reason.find("rtc"), std::string::npos) << reason;

    ParseResult<GeneratedProjectFirmwareRust> firmware = generateProjectFirmwareRust(config);
    EXPECT_FALSE(firmware.success);
}

// AUDIT_TZ_PERIPHERY_GAPS.md, Тир 2 — CRC добавлен только в C-ветке.
TEST(RustProjectCodegen, RejectsCrcBlockAsUnsupportedInRustBranch)
{
    ParseResult<ProjectConfig> config_result = parseProjectConfig("fixtures/project_rust_gpio_uart.json");
    ASSERT_TRUE(config_result.success) << config_result.error;
    ProjectConfig config = *config_result.config;

    CrcConfig crc_block;
    crc_block.type = "crc";
    crc_block.poly = 0x04C11DB7;
    crc_block.init = 0xFFFFFFFF;
    crc_block.input_reverse = true;
    crc_block.output_reverse = true;
    crc_block.output_inversion = true;
    config.crc_block = crc_block;

    const std::string reason = findUnsupportedRustFeature(config);
    EXPECT_NE(reason, "");
    EXPECT_NE(reason.find("crc"), std::string::npos) << reason;

    ParseResult<GeneratedProjectFirmwareRust> firmware = generateProjectFirmwareRust(config);
    EXPECT_FALSE(firmware.success);
}

// AUDIT_TZ_PERIPHERY_GAPS.md, Тир 2 — EEPROM добавлен только в C-ветке.
TEST(RustProjectCodegen, RejectsEepromBlockAsUnsupportedInRustBranch)
{
    ParseResult<ProjectConfig> config_result = parseProjectConfig("fixtures/project_rust_gpio_uart.json");
    ASSERT_TRUE(config_result.success) << config_result.error;
    ProjectConfig config = *config_result.config;

    EepromConfig eeprom_block;
    eeprom_block.type = "eeprom";
    eeprom_block.mode = "two_stage";
    config.eeprom_block = eeprom_block;

    const std::string reason = findUnsupportedRustFeature(config);
    EXPECT_NE(reason, "");
    EXPECT_NE(reason.find("eeprom"), std::string::npos) << reason;

    ParseResult<GeneratedProjectFirmwareRust> firmware = generateProjectFirmwareRust(config);
    EXPECT_FALSE(firmware.success);
}

// AUDIT_TZ_PERIPHERY_GAPS.md, Тир 2 — WDT добавлен только в C-ветке.
TEST(RustProjectCodegen, RejectsWdtBlockAsUnsupportedInRustBranch)
{
    ParseResult<ProjectConfig> config_result = parseProjectConfig("fixtures/project_rust_gpio_uart.json");
    ASSERT_TRUE(config_result.success) << config_result.error;
    ProjectConfig config = *config_result.config;

    WdtConfig wdt_block;
    wdt_block.type = "wdt";
    wdt_block.clock_source = "osc32m";
    wdt_block.reload_ms = 500;
    config.wdt_block = wdt_block;

    const std::string reason = findUnsupportedRustFeature(config);
    EXPECT_NE(reason, "");
    EXPECT_NE(reason.find("wdt"), std::string::npos) << reason;

    ParseResult<GeneratedProjectFirmwareRust> firmware = generateProjectFirmwareRust(config);
    EXPECT_FALSE(firmware.success);
}

// AUDIT_TZ_PERIPHERY_GAPS.md, Тир 2 — DAC добавлен только в C-ветке.
TEST(RustProjectCodegen, RejectsDacBlockAsUnsupportedInRustBranch)
{
    ParseResult<ProjectConfig> config_result = parseProjectConfig("fixtures/project_rust_gpio_uart.json");
    ASSERT_TRUE(config_result.success) << config_result.error;
    ProjectConfig config = *config_result.config;

    DacConfig dac_block;
    dac_block.type = "dac";
    dac_block.instance = 0;
    dac_block.div = 31;
    dac_block.ext_ref = false;
    dac_block.ext_clb = false;
    config.dac_blocks.push_back(dac_block);

    const std::string reason = findUnsupportedRustFeature(config);
    EXPECT_NE(reason, "");
    EXPECT_NE(reason.find("dac"), std::string::npos) << reason;

    ParseResult<GeneratedProjectFirmwareRust> firmware = generateProjectFirmwareRust(config);
    EXPECT_FALSE(firmware.success);
}

// AUDIT_TZ_PERIPHERY_GAPS.md, Тир 2 — ADC/CRYPTO/OTP/TSENS добавлены только в C-ветке.
TEST(RustProjectCodegen, RejectsAdcBlockAsUnsupportedInRustBranch)
{
    ParseResult<ProjectConfig> config_result = parseProjectConfig("fixtures/project_rust_gpio_uart.json");
    ASSERT_TRUE(config_result.success) << config_result.error;
    ProjectConfig config = *config_result.config;
    AdcConfig block; block.type = "adc"; block.channel = 0; block.ext_ref = false; block.ext_clb = false;
    config.adc_block = block;

    const std::string reason = findUnsupportedRustFeature(config);
    EXPECT_NE(reason, "");
    EXPECT_NE(reason.find("adc"), std::string::npos) << reason;
    EXPECT_FALSE(generateProjectFirmwareRust(config).success);
}

TEST(RustProjectCodegen, RejectsCryptoBlockAsUnsupportedInRustBranch)
{
    ParseResult<ProjectConfig> config_result = parseProjectConfig("fixtures/project_rust_gpio_uart.json");
    ASSERT_TRUE(config_result.success) << config_result.error;
    ProjectConfig config = *config_result.config;
    CryptoConfig block; block.type = "crypto"; block.algorithm = "aes"; block.cipher_mode = "ecb";
    config.crypto_block = block;

    const std::string reason = findUnsupportedRustFeature(config);
    EXPECT_NE(reason, "");
    EXPECT_NE(reason.find("crypto"), std::string::npos) << reason;
    EXPECT_FALSE(generateProjectFirmwareRust(config).success);
}

TEST(RustProjectCodegen, RejectsOtpBlockAsUnsupportedInRustBranch)
{
    ParseResult<ProjectConfig> config_result = parseProjectConfig("fixtures/project_rust_gpio_uart.json");
    ASSERT_TRUE(config_result.success) << config_result.error;
    ProjectConfig config = *config_result.config;
    OtpConfig block; block.type = "otp"; block.read_mode = "two_stages";
    config.otp_block = block;

    const std::string reason = findUnsupportedRustFeature(config);
    EXPECT_NE(reason, "");
    EXPECT_NE(reason.find("otp"), std::string::npos) << reason;
    EXPECT_FALSE(generateProjectFirmwareRust(config).success);
}

TEST(RustProjectCodegen, RejectsTsensBlockAsUnsupportedInRustBranch)
{
    ParseResult<ProjectConfig> config_result = parseProjectConfig("fixtures/project_rust_gpio_uart.json");
    ASSERT_TRUE(config_result.success) << config_result.error;
    ProjectConfig config = *config_result.config;
    TsensConfig block; block.type = "tsens"; block.clock_source = "sys_clk"; block.frequency_hz = 40000;
    config.tsens_block = block;

    const std::string reason = findUnsupportedRustFeature(config);
    EXPECT_NE(reason, "");
    EXPECT_NE(reason.find("tsens"), std::string::npos) << reason;
    EXPECT_FALSE(generateProjectFirmwareRust(config).success);
}

TEST(RustProjectCodegen, RejectsPvdBlockAsUnsupportedInRustBranch)
{
    ParseResult<ProjectConfig> config_result = parseProjectConfig("fixtures/project_rust_gpio_uart.json");
    ASSERT_TRUE(config_result.success) << config_result.error;
    ProjectConfig config = *config_result.config;
    PvdConfig block; block.type = "pvd"; block.under_threshold = 3; block.over_threshold = 12; block.digital_filter_value = 100;
    config.pvd_block = block;

    const std::string reason = findUnsupportedRustFeature(config);
    EXPECT_NE(reason, "");
    EXPECT_NE(reason.find("pvd"), std::string::npos) << reason;
    EXPECT_FALSE(generateProjectFirmwareRust(config).success);
}

TEST(RustProjectCodegen, RejectsBusWatchdogBlockAsUnsupportedInRustBranch)
{
    ParseResult<ProjectConfig> config_result = parseProjectConfig("fixtures/project_rust_gpio_uart.json");
    ASSERT_TRUE(config_result.success) << config_result.error;
    ProjectConfig config = *config_result.config;
    BusWatchdogConfig block; block.type = "bus_watchdog";
    config.bus_watchdog_block = block;

    const std::string reason = findUnsupportedRustFeature(config);
    EXPECT_NE(reason, "");
    EXPECT_NE(reason.find("bus_watchdog"), std::string::npos) << reason;
    EXPECT_FALSE(generateProjectFirmwareRust(config).success);
}

TEST(RustProjectCodegen, RejectsDmaBlockAsUnsupportedInRustBranch)
{
    ParseResult<ProjectConfig> config_result = parseProjectConfig("fixtures/project_rust_gpio_uart.json");
    ASSERT_TRUE(config_result.success) << config_result.error;
    ProjectConfig config = *config_result.config;
    DmaConfig block; block.type = "dma"; block.instance = 0; block.request = "spi_0";
    block.direction = "peripheral_to_memory"; block.buffer_size_bytes = 16; block.element_size = "byte";
    config.dma_blocks.push_back(block);

    const std::string reason = findUnsupportedRustFeature(config);
    EXPECT_NE(reason, "");
    EXPECT_NE(reason.find("dma"), std::string::npos) << reason;
    EXPECT_FALSE(generateProjectFirmwareRust(config).success);
}

TEST(RustProjectCodegen, RejectsGpioTimerSerialModeAsUnsupported)
{
    ProjectConfig config;
    config.language = OutputLanguage::Rust;
    GpioConfig block;
    block.type = "gpio";
    block.instance = 0;
    block.mode = "timer_serial";
    block.pull = "none";
    block.drive_strength = "2mA";
    block.pins = {3};
    config.gpio_blocks.push_back(block);

    const std::string reason = findUnsupportedRustFeature(config);
    EXPECT_NE(reason, "");
    EXPECT_NE(reason.find("timer_serial"), std::string::npos) << reason;
}

// С 2026-10-26 flow_control (аппаратный RTS/CTS) реализован через
// CONTROL3.RTSE/CTSE (templates/mik32_rust_hal_snapshot/pac/src/usart_0/control3.rs) —
// больше не отклоняется, а генерируется в main.rs.
TEST(RustProjectCodegen, GeneratesFlowControlTrueIntoMainRs)
{
    ParseResult<ProjectConfig> config_result = parseProjectConfig("fixtures/project_rust_gpio_uart.json");
    ASSERT_TRUE(config_result.success) << config_result.error;
    ProjectConfig config = *config_result.config;
    config.uart_blocks[0].flow_control = true;

    EXPECT_EQ(findUnsupportedRustFeature(config), "");
    ParseResult<GeneratedProjectFirmwareRust> firmware = generateProjectFirmwareRust(config);
    ASSERT_TRUE(firmware.success) << firmware.error;
    EXPECT_NE(firmware.config->main_rs.find("flow_control: true"), std::string::npos) << firmware.config->main_rs;
}

// MSB/LSB first (OVERNIGHT_FIELD_REFERENCE.md / uart_contribution.cpp:75).
TEST(RustProjectCodegen, GeneratesMsbFirstBitDirectionIntoMainRs)
{
    ParseResult<ProjectConfig> config_result = parseProjectConfig("fixtures/project_rust_gpio_uart.json");
    ASSERT_TRUE(config_result.success) << config_result.error;
    ProjectConfig config = *config_result.config;
    config.uart_blocks[0].bit_direction = "msb_first";

    ParseResult<GeneratedProjectFirmwareRust> firmware = generateProjectFirmwareRust(config);
    ASSERT_TRUE(firmware.success) << firmware.error;
    EXPECT_NE(firmware.config->main_rs.find("bit_direction: BitDirection::Msb"), std::string::npos) << firmware.config->main_rs;
}

// Half-duplex (CONTROL3.HDSEL) — channel_mode="half_duplex".
TEST(RustProjectCodegen, GeneratesHalfDuplexChannelModeIntoMainRs)
{
    ParseResult<ProjectConfig> config_result = parseProjectConfig("fixtures/project_rust_gpio_uart.json");
    ASSERT_TRUE(config_result.success) << config_result.error;
    ProjectConfig config = *config_result.config;
    config.uart_blocks[0].channel_mode = "half_duplex";

    ParseResult<GeneratedProjectFirmwareRust> firmware = generateProjectFirmwareRust(config);
    ASSERT_TRUE(firmware.success) << firmware.error;
    EXPECT_NE(firmware.config->main_rs.find("channel_mode: ChannelMode::HalfDuplex"), std::string::npos) << firmware.config->main_rs;
}

// TX/RX-инверсии и swap (CONTROL2.TXINV/RXINV/SWAP).
TEST(RustProjectCodegen, GeneratesTxRxInversionAndSwapIntoMainRs)
{
    ParseResult<ProjectConfig> config_result = parseProjectConfig("fixtures/project_rust_gpio_uart.json");
    ASSERT_TRUE(config_result.success) << config_result.error;
    ProjectConfig config = *config_result.config;
    config.uart_blocks[0].tx_inversion = true;
    config.uart_blocks[0].rx_inversion = true;
    config.uart_blocks[0].swap = true;

    ParseResult<GeneratedProjectFirmwareRust> firmware = generateProjectFirmwareRust(config);
    ASSERT_TRUE(firmware.success) << firmware.error;
    EXPECT_NE(firmware.config->main_rs.find("tx_inversion: true"), std::string::npos) << firmware.config->main_rs;
    EXPECT_NE(firmware.config->main_rs.find("rx_inversion: true"), std::string::npos) << firmware.config->main_rs;
    EXPECT_NE(firmware.config->main_rs.find("swap: true"), std::string::npos) << firmware.config->main_rs;
}

// Синхронный режим на instance=0 — разрешён, xck_mode/last_byte_clock
// попадают в сгенерированный Config.
TEST(RustProjectCodegen, GeneratesSynchronousModeWithXckModeOnInstance0)
{
    ParseResult<ProjectConfig> config_result = parseProjectConfig("fixtures/project_rust_gpio_uart.json");
    ASSERT_TRUE(config_result.success) << config_result.error;
    ProjectConfig config = *config_result.config;
    config.uart_blocks[0].mode = "synchronous";
    config.uart_blocks[0].xck_mode = "mode2";
    config.uart_blocks[0].last_byte_clock = true;

    EXPECT_EQ(findUnsupportedRustFeature(config), "");
    ParseResult<GeneratedProjectFirmwareRust> firmware = generateProjectFirmwareRust(config);
    ASSERT_TRUE(firmware.success) << firmware.error;
    EXPECT_NE(firmware.config->main_rs.find("mode: Mode::Synchronous"), std::string::npos) << firmware.config->main_rs;
    EXPECT_NE(firmware.config->main_rs.find("xck_mode: XckMode::Mode2"), std::string::npos) << firmware.config->main_rs;
    EXPECT_NE(firmware.config->main_rs.find("last_byte_clock: true"), std::string::npos) << firmware.config->main_rs;
}

// Errata MIK32V2 v1.0.1 §10.1 — тот же запрет, что в C-ветке
// (parser/USART/usart_parser.cpp): mode=synchronous запрещён на instance=1.
TEST(RustProjectCodegen, RejectsSynchronousModeOnInstance1DueToErrata)
{
    ParseResult<ProjectConfig> config_result = parseProjectConfig("fixtures/project_rust_gpio_uart.json");
    ASSERT_TRUE(config_result.success) << config_result.error;
    ProjectConfig config = *config_result.config;
    config.uart_blocks[0].instance = 1;
    config.uart_blocks[0].mode = "synchronous";

    const std::string reason = findUnsupportedRustFeature(config);
    EXPECT_NE(reason, "");
    EXPECT_NE(reason.find("synchronous"), std::string::npos) << reason;
    EXPECT_FALSE(generateProjectFirmwareRust(config).success);
}

// Регистр MODEM в PAC-снэпшоте не даёт записать DCD/DSR/RI/DDIS — весь
// "модем"-подрежим отклоняется, а не угадывается наполовину.
TEST(RustProjectCodegen, RejectsUartModemSignalsAsUnsupported)
{
    ParseResult<ProjectConfig> config_result = parseProjectConfig("fixtures/project_rust_gpio_uart.json");
    ASSERT_TRUE(config_result.success) << config_result.error;
    ProjectConfig config = *config_result.config;
    config.uart_blocks[0].flow_control = true;
    config.uart_blocks[0].has_modem_signals = true;
    config.uart_blocks[0].modem_signals.dtr = true;

    const std::string reason = findUnsupportedRustFeature(config);
    EXPECT_NE(reason, "");
    EXPECT_NE(reason.find("modem_signals"), std::string::npos) << reason;
}

// rts_mode="modem" — нет отдельного бита в CONTROL3 под "модемный" RTS.
TEST(RustProjectCodegen, RejectsUartRtsModeModemAsUnsupported)
{
    ParseResult<ProjectConfig> config_result = parseProjectConfig("fixtures/project_rust_gpio_uart.json");
    ASSERT_TRUE(config_result.success) << config_result.error;
    ProjectConfig config = *config_result.config;
    config.uart_blocks[0].flow_control = true;
    config.uart_blocks[0].rts_mode = "modem";

    const std::string reason = findUnsupportedRustFeature(config);
    EXPECT_NE(reason, "");
    EXPECT_NE(reason.find("rts_mode"), std::string::npos) << reason;
}

// DMA остаётся отклонённым (без изменений) — регрессия на уже
// существующее поведение после правок флага flow_control выше.
TEST(RustProjectCodegen, StillRejectsUartDmaAsUnsupported)
{
    ParseResult<ProjectConfig> config_result = parseProjectConfig("fixtures/project_rust_gpio_uart.json");
    ASSERT_TRUE(config_result.success) << config_result.error;
    ProjectConfig config = *config_result.config;
    UartDmaConfig dma;
    dma.channel = 0;
    dma.direction = "tx";
    dma.buffer_type = "linear";
    dma.buffer_size_bytes = 16;
    dma.element_size = "byte";
    dma.increment = true;
    config.uart_blocks[0].dma.push_back(dma);

    const std::string reason = findUnsupportedRustFeature(config);
    EXPECT_NE(reason, "");
    EXPECT_NE(reason.find("DMA"), std::string::npos) << reason;
}

// Единственный обязательный тест "реальной сборки" для этой сессии
// (см. комментарий в шапке файла). Собирает сгенерированный Cargo-проект
// НАСТОЯЩИМ cargo (Bundled-снэпшот, offline, riscv32imc-unknown-none-elf) и
// требует нулевой код возврата — тот же принцип, что CMake-ветка нашла в
// нём 3 линковочных бага, которых не увидел ни один синтетический тест.
TEST(RustProjectCodegen, WrittenProjectActuallyBuildsWithCargo)
{
    ParseResult<ProjectConfig> config_result = parseProjectConfig("fixtures/project_rust_gpio_uart.json");
    ASSERT_TRUE(config_result.success) << config_result.error;
    const ProjectConfig& config = *config_result.config;

    ParseResult<GeneratedProjectFirmwareRust> firmware = generateProjectFirmwareRust(config);
    ASSERT_TRUE(firmware.success) << firmware.error;

    const fs::path output_dir = scratchDirFor("cargo_build_gpio_uart");
    std::error_code ec;
    fs::remove_all(output_dir, ec);

    nlohmann::json project_json;
    {
        std::ifstream fixture_file("fixtures/project_rust_gpio_uart.json");
        project_json = nlohmann::json::parse(fixture_file);
    }

    CodegenWriteResult write_result = writeGeneratedRustProject(
        *firmware.config,
        "../templates/cargo_skeleton",
        "../templates/mik32_rust_hal_snapshot",
        output_dir.string(),
        config,
        project_json);
    ASSERT_TRUE(write_result.success) << write_result.error;

    ASSERT_TRUE(fs::exists(output_dir / "Cargo.toml"));
    ASSERT_TRUE(fs::exists(output_dir / "src" / "main.rs"));
    ASSERT_TRUE(fs::exists(output_dir / "vendor" / "hal"));
    ASSERT_TRUE(fs::exists(output_dir / "vendor" / "pac"));
    ASSERT_TRUE(fs::exists(output_dir / "vendor" / "rt"));

    // Регрессия (найдено реальной прошивкой на плате 2026-10-20): без фичи
    // "memory-spifi" vendor/rt/build.rs подставляет memory_default.x
    // (FLASH_TEXT=0x01000000, тестовая заглушка апстрима) вместо
    // memory_spifi.x (FLASH_TEXT=0x80000000 — реальная область загрузки
    // МИК32) — cargo build проходил чисто, но МК не стартовал после
    // прошивки. См. buildDependencyLines в rust_project_codegen.cpp.
    {
        std::ifstream cargo_toml_file((output_dir / "Cargo.toml").string());
        std::string cargo_toml((std::istreambuf_iterator<char>(cargo_toml_file)),
                                std::istreambuf_iterator<char>());
        EXPECT_NE(cargo_toml.find("mik32-rt"), std::string::npos);
        EXPECT_NE(cargo_toml.find("memory-spifi"), std::string::npos)
            << "mik32-rt без features=[\"memory-spifi\"] -> МК не стартует на реальной плате, см. комментарий выше";
    }

    const fs::path absolute_output_dir = fs::absolute(output_dir);
    const std::string command = "cd \"" + absolute_output_dir.string() + "\" && cargo build --release 2>&1";
    // popen, а не std::system: нужен реальный вывод cargo для диагностики
    // ASSERT_EQ ниже (код возврата 0/не-0 сам по себе не объясняет, что
    // сломалось, если сломается).
    std::string cargo_output;
    FILE* pipe = popen(command.c_str(), "r");
    ASSERT_NE(pipe, nullptr) << "не удалось запустить cargo build";
    char buffer[4096];
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr)
    {
        cargo_output += buffer;
    }
    const int status = pclose(pipe);

    EXPECT_EQ(status, 0) << "cargo build завершился с ошибкой, вывод:\n" << cargo_output;
    const fs::path elf_path = output_dir / "target" / "riscv32imc-unknown-none-elf" / "release" / "mik32-firmware";
    EXPECT_TRUE(fs::exists(elf_path))
        << "cargo build 'успешен' (код 0), но ELF-файл не появился — вывод:\n" << cargo_output;

    // Прямая проверка entry point поверх фичи memory-spifi выше: даже если
    // кто-то переименует фичу и Cargo.toml-строка перестанет совпадать, этот
    // ассерт всё равно поймает настоящую причину отказа на плате —
    // неправильный адрес точки входа.
    if (fs::exists(elf_path))
    {
        const std::string readelf_command =
            "riscv-none-elf-readelf -h \"" + elf_path.string() + "\" 2>&1";
        FILE* readelf_pipe = popen(readelf_command.c_str(), "r");
        ASSERT_NE(readelf_pipe, nullptr) << "не удалось запустить riscv-none-elf-readelf";
        std::string readelf_output;
        char readelf_buffer[1024];
        while (fgets(readelf_buffer, sizeof(readelf_buffer), readelf_pipe) != nullptr)
        {
            readelf_output += readelf_buffer;
        }
        pclose(readelf_pipe);
        EXPECT_NE(readelf_output.find("0x80000000"), std::string::npos)
            << "Entry point должен быть 0x80000000 (реальная область загрузки МИК32), "
               "иначе МК не стартует на плате при чистой линковке. readelf -h:\n"
            << readelf_output;
    }
}

// Реальная сборка I2C-проекта cargo'м — по образцу
// WrittenProjectActuallyBuildsWithCargo выше (GPIO+UART); отдельный тест
// нужен, т.к. i2c.rs — совсем новый файл HAL-снэпшота, синтаксические ошибки
// в нём (particularly в macro_rules! impl_instance!) не поймать ничем, кроме
// настоящей компиляции обоих инстансов (I2c0/I2c1 — разные типы PAC, см.
// комментарий в i2c.rs).
TEST(RustProjectCodegen, WrittenI2cProjectActuallyBuildsWithCargo)
{
    ParseResult<ProjectConfig> config_result = parseProjectConfig("fixtures/project_rust_i2c.json");
    ASSERT_TRUE(config_result.success) << config_result.error;
    const ProjectConfig& config = *config_result.config;

    ParseResult<GeneratedProjectFirmwareRust> firmware = generateProjectFirmwareRust(config);
    ASSERT_TRUE(firmware.success) << firmware.error;

    const fs::path output_dir = scratchDirFor("cargo_build_i2c");
    std::error_code ec;
    fs::remove_all(output_dir, ec);

    nlohmann::json project_json;
    {
        std::ifstream fixture_file("fixtures/project_rust_i2c.json");
        project_json = nlohmann::json::parse(fixture_file);
    }

    CodegenWriteResult write_result = writeGeneratedRustProject(
        *firmware.config,
        "../templates/cargo_skeleton",
        "../templates/mik32_rust_hal_snapshot",
        output_dir.string(),
        config,
        project_json);
    ASSERT_TRUE(write_result.success) << write_result.error;

    const fs::path absolute_output_dir = fs::absolute(output_dir);
    const std::string command = "cd \"" + absolute_output_dir.string() + "\" && cargo build --release 2>&1";
    std::string cargo_output;
    FILE* pipe = popen(command.c_str(), "r");
    ASSERT_NE(pipe, nullptr) << "не удалось запустить cargo build";
    char buffer[4096];
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr)
    {
        cargo_output += buffer;
    }
    const int status = pclose(pipe);

    EXPECT_EQ(status, 0) << "cargo build завершился с ошибкой, вывод:\n" << cargo_output;
    const fs::path elf_path = output_dir / "target" / "riscv32imc-unknown-none-elf" / "release" / "mik32-firmware";
    EXPECT_TRUE(fs::exists(elf_path))
        << "cargo build 'успешен' (код 0), но ELF-файл не появился — вывод:\n" << cargo_output;
}

// ТЗ §3.1.7.3: пользовательская правка внутри USER CODE BEGIN/END Loop
// обязана пережить повторную генерацию — та же гарантия, что у main.c
// (C-ветка), только с комментариями "//" вместо "/* */".
TEST(RustProjectCodegen, PreservesUserCodeInMainRsAcrossRegeneration)
{
    ParseResult<ProjectConfig> config_result = parseProjectConfig("fixtures/project_rust_gpio_uart.json");
    ASSERT_TRUE(config_result.success) << config_result.error;
    const ProjectConfig& config = *config_result.config;

    ParseResult<GeneratedProjectFirmwareRust> firmware = generateProjectFirmwareRust(config);
    ASSERT_TRUE(firmware.success) << firmware.error;

    const fs::path output_dir = scratchDirFor("user_code_preserved");
    std::error_code ec;
    fs::remove_all(output_dir, ec);

    nlohmann::json project_json;
    {
        std::ifstream fixture_file("fixtures/project_rust_gpio_uart.json");
        project_json = nlohmann::json::parse(fixture_file);
    }

    ASSERT_TRUE(writeGeneratedRustProject(*firmware.config, "../templates/cargo_skeleton",
        "../templates/mik32_rust_hal_snapshot", output_dir.string(), config, project_json).success);

    // Пользователь дописывает что-то внутри USER CODE Loop.
    const fs::path main_rs_path = output_dir / "src" / "main.rs";
    std::string edited = readFileToString(main_rs_path.string());
    const std::string marker = "// USER CODE BEGIN Loop";
    const size_t marker_pos = edited.find(marker);
    ASSERT_NE(marker_pos, std::string::npos);
    edited.insert(marker_pos + marker.size(), "\n        // ручная правка пользователя\n");
    {
        std::ofstream out(main_rs_path, std::ios::trunc);
        out << edited;
    }

    // Повторная генерация — то же самое содержимое (config не менялся).
    ASSERT_TRUE(writeGeneratedRustProject(*firmware.config, "../templates/cargo_skeleton",
        "../templates/mik32_rust_hal_snapshot", output_dir.string(), config, project_json).success);

    const std::string regenerated = readFileToString(main_rs_path.string());
    EXPECT_NE(regenerated.find("// ручная правка пользователя"), std::string::npos) << regenerated;
}
