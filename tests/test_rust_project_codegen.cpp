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
    // generated_projects/ гитигнорится (CLAUDE.md) — тот же принцип, что
    // scratchDirFor в test_parsers.cpp (CMake-ветка), независимая копия по
    // той же причине (см. rust_project_codegen.cpp — новая логика в новых
    // файлах, не трогая существующие тесты).
    fs::path scratchDirFor(const std::string& name)
    {
        return fs::path("../generated_projects") / ("test_rust_" + name);
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

TEST(RustProjectCodegen, RejectsI2cBlockAsUnsupportedInRustBranch)
{
    ParseResult<ProjectConfig> config_result = parseProjectConfig("fixtures/project_rust_gpio_uart.json");
    ASSERT_TRUE(config_result.success) << config_result.error;
    ProjectConfig config = *config_result.config;

    I2cConfig i2c_block;
    i2c_block.type = "i2c";
    i2c_block.instance = 0;
    config.i2c_blocks.push_back(i2c_block);

    const std::string reason = findUnsupportedRustFeature(config);
    EXPECT_NE(reason, "");
    EXPECT_NE(reason.find("i2c"), std::string::npos) << reason;

    ParseResult<GeneratedProjectFirmwareRust> firmware = generateProjectFirmwareRust(config);
    EXPECT_FALSE(firmware.success);
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

TEST(RustProjectCodegen, RejectsUartFlowControlAsUnsupported)
{
    ParseResult<ProjectConfig> config_result = parseProjectConfig("fixtures/project_rust_gpio_uart.json");
    ASSERT_TRUE(config_result.success) << config_result.error;
    ProjectConfig config = *config_result.config;
    config.uart_blocks[0].flow_control = true;

    const std::string reason = findUnsupportedRustFeature(config);
    EXPECT_NE(reason, "");
    EXPECT_NE(reason.find("flow_control"), std::string::npos) << reason;
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
    EXPECT_TRUE(fs::exists(output_dir / "target" / "riscv32imc-unknown-none-elf" / "release" / "mik32-firmware"))
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
