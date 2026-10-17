#include "rust_project_codegen.h"
#include "rust_feature_validator.h"
#include "user_code_markers_rust.h"
#include "contribution_utils.h"
#include "win_file_io.h"
#include "utf8_path.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <set>
#include <sstream>
#include <system_error>

namespace fs = std::filesystem;

namespace
{
    ParseResult<GeneratedProjectFirmwareRust> makeError(const std::string& message)
    {
        return ParseResult<GeneratedProjectFirmwareRust>{false, message, std::nullopt};
    }

    // instance -> имя поля Peripherals (mik32v2-pac/src/lib.rs:1832-1836).
    const char* gpioPeripheralField(int instance)
    {
        switch (instance)
        {
            case 0: return "gpio16_0";
            case 1: return "gpio16_1";
            default: return "gpio8_2"; // instance==2 — schemas/gpio.json не пускает другие значения
        }
    }

    // instance -> префикс имени поля Parts (mik32-hal-main/src/gpio.rs,
    // макрос gpio!, строки 231-278: p16_0_N/p16_1_N/p8_2_N).
    const char* gpioPinFieldPrefix(int instance)
    {
        switch (instance)
        {
            case 0: return "p16_0_";
            case 1: return "p16_1_";
            default: return "p8_2_";
        }
    }

    std::string gpioSplitVarName(int instance)
    {
        return "gpio" + std::to_string(instance);
    }

    std::string gpioPinFieldName(int instance, int pin)
    {
        return std::string(gpioPinFieldPrefix(instance)) + std::to_string(pin);
    }

    // mode="output"/"input"+pull -> метод into_* (hal/src/gpio/convert.rs).
    // mode="serial" не проходит через эту функцию — UART заявляет свои пины
    // отдельно (buildUartPinLines ниже), не через config.gpio_blocks.
    // mode="timer_serial"/"analog" отсеяны findUnsupportedRustFeature раньше.
    std::string gpioConvertMethod(const std::string& mode, const std::string& pull)
    {
        if (mode == "output")
        {
            return "into_output";
        }
        if (mode == "serial")
        {
            return "into_serial_port";
        }
        // mode == "input"
        if (pull == "pull_up") return "into_pull_up_input";
        if (pull == "pull_down") return "into_pull_down_input";
        return "into_floating_input"; // pull == "none"
    }

    // drive_strength (schemas/gpio.json enum: "2mA"/"4mA"/"8mA") ->
    // DriveStrength (hal/src/gpio/convert.rs, добавлено конфигуратором).
    std::string driveStrengthVariant(const std::string& drive_strength)
    {
        if (drive_strength == "4mA") return "DriveStrength::Ma4";
        if (drive_strength == "8mA") return "DriveStrength::Ma8";
        return "DriveStrength::Ma2"; // "2mA"
    }

    struct FixedPin { int gpio_instance; int pin; };
    // Датащит МИК32 v2.2.3, Таблица 193 «Назначение выводов», с.320 — те же
    // две пары, что уже зашиты в PinTx/PinRx trait-имплах
    // hal/src/serial.rs:23-27 (mik32-hal-main) и knowledge_base/uart.json:
    // instance_fixed_pins. Не настраиваемо иначе даже теоретически: HAL
    // просто не скомпилируется для другой пары пинов (Pins<U> реализован
    // только для этих конкретных типов).
    FixedPin uartTxPin(int instance) { return instance == 0 ? FixedPin{0, 6} : FixedPin{1, 9}; }
    FixedPin uartRxPin(int instance) { return instance == 0 ? FixedPin{0, 5} : FixedPin{1, 8}; }

    const char* usartPeripheralField(int instance) { return instance == 0 ? "usart_0" : "usart_1"; }

    std::string dataBitsVariant(int data_bits)
    {
        if (data_bits == 7) return "DataBits::Seven";
        if (data_bits == 9) return "DataBits::Nine";
        return "DataBits::Eight";
    }
    std::string stopBitsVariant(int stop_bits)
    {
        return stop_bits == 2 ? "StopBits::Two" : "StopBits::One";
    }
    std::string parityVariant(const std::string& parity)
    {
        if (parity == "even") return "Parity::Even";
        if (parity == "odd") return "Parity::Odd";
        return "Parity::None";
    }
}

ParseResult<GeneratedProjectFirmwareRust> generateProjectFirmwareRust(
    const ProjectConfig& config)
{
    const std::string unsupported = findUnsupportedRustFeature(config);
    if (!unsupported.empty())
    {
        return makeError(unsupported);
    }

    // --- какие GPIO-инстансы нужно split()'ить -----------------------------
    // Объединение явных gpio_blocks и инстансов, неявно нужных UART (fixed
    // pins) — оба потребителя берут поля ИЗ ОДНОГО split(): Parts — обычная
    // struct, разные поля можно "вынести" по отдельности сколько угодно раз
    // разным потребителям, но сам split() можно вызвать только один раз на
    // instance (peripheral.split(self) потребляет исходный PAC-хендл по
    // значению) — см. rust_project_codegen.h.
    std::set<int> gpio_instances;
    for (const GpioConfig& block : config.gpio_blocks)
    {
        gpio_instances.insert(block.instance);
    }
    for (const UartConfig& block : config.uart_blocks)
    {
        gpio_instances.insert(uartTxPin(block.instance).gpio_instance);
        gpio_instances.insert(uartRxPin(block.instance).gpio_instance);
    }

    const bool has_gpio = !gpio_instances.empty();
    const bool has_uart = !config.uart_blocks.empty();
    const bool has_drive_strength = !config.gpio_blocks.empty(); // обязательное поле каждого блока gpio

    // --- код для тела main() -----------------------------------------------
    std::ostringstream splits;
    for (int instance : gpio_instances)
    {
        splits << "    let " << gpioSplitVarName(instance) << " = p." << gpioPeripheralField(instance)
               << ".split();\n";
    }

    std::ostringstream gpio_setup;
    for (const GpioConfig& block : config.gpio_blocks)
    {
        const std::string method = gpioConvertMethod(block.mode, block.pull);
        for (uint8_t pin : block.pins)
        {
            const std::string var_name = "gpio" + std::to_string(block.instance) + "_p" + std::to_string(pin);
            gpio_setup << "    let mut " << var_name << " = "
                       << gpioSplitVarName(block.instance) << "."
                       << gpioPinFieldName(block.instance, pin) << "." << method << "();\n";
            gpio_setup << "    " << var_name << ".set_drive_strength("
                       << driveStrengthVariant(block.drive_strength) << ");\n";
        }
    }

    std::ostringstream uart_setup;
    for (const UartConfig& block : config.uart_blocks)
    {
        const FixedPin tx_pin = uartTxPin(block.instance);
        const FixedPin rx_pin = uartRxPin(block.instance);
        const std::string tx_var = "uart" + std::to_string(block.instance) + "_tx_pin";
        const std::string rx_var = "uart" + std::to_string(block.instance) + "_rx_pin";
        const std::string serial_var = "serial" + std::to_string(block.instance);
        const std::string tx_half = "uart" + std::to_string(block.instance) + "_tx";
        const std::string rx_half = "uart" + std::to_string(block.instance) + "_rx";

        // ТЗ §3.1.3: direction управляет тем, какие направления включены —
        // тот же смысл, что transmitting/receiving в C-ветке
        // (codegen/project/uart_contribution.cpp:69-70), физические пины
        // заявляются безусловно (см. FixedPin выше — HAL сам настраивает
        // TX/RX в serial-режим независимо от направления).
        const std::string transmitting = (block.direction == "tx" || block.direction == "duplex") ? "true" : "false";
        const std::string receiving = (block.direction == "rx" || block.direction == "duplex") ? "true" : "false";

        uart_setup << "    let " << tx_var << " = " << gpioSplitVarName(tx_pin.gpio_instance) << "."
                   << gpioPinFieldName(tx_pin.gpio_instance, tx_pin.pin) << ".into_serial_port();\n";
        uart_setup << "    let " << rx_var << " = " << gpioSplitVarName(rx_pin.gpio_instance) << "."
                   << gpioPinFieldName(rx_pin.gpio_instance, rx_pin.pin) << ".into_serial_port();\n";
        uart_setup << "    let " << serial_var << " = Serial::new(\n"
                   << "        p." << usartPeripheralField(block.instance) << ",\n"
                   << "        (" << tx_var << ", " << rx_var << "),\n"
                   << "        UartConfig {\n"
                   << "            baud_rate: " << block.baud_rate << ",\n"
                   << "            data_bits: " << dataBitsVariant(block.data_bits) << ",\n"
                   << "            stop_bits: " << stopBitsVariant(block.stop_bits) << ",\n"
                   << "            parity: " << parityVariant(block.parity) << ",\n"
                   << "            transmitting: " << transmitting << ",\n"
                   << "            receiving: " << receiving << ",\n"
                   << "        },\n"
                   << "    );\n";
        // Ни tx_half, ни rx_half не мутируются — пустой while(1) ничего в них
        // не пишет и не читает, без "let _ = &..." компилятор дал бы
        // unused_variables warning.
        uart_setup << "    let (" << tx_half << ", " << rx_half << ") = "
                   << serial_var << ".split();\n";
        uart_setup << "    let _ = &" << tx_half << ";\n";
        uart_setup << "    let _ = &" << rx_half << ";\n";
    }

    std::ostringstream main_rs;
    main_rs << "//! Сгенерировано MIK32-Configurator (Rust)";
    if (!config.mcu.empty() || !config.version.empty())
    {
        main_rs << " — mcu=" << config.mcu << ", version=" << config.version;
    }
    main_rs << ".\n"
               "#![no_std]\n"
               "#![no_main]\n"
               "\n"
               "use core::panic::PanicInfo;\n"
               "use mik32_rt::entry;\n"
               // Обязательно (не просто стиль) — без явной ссылки на крейт
               // Rust не линкует его: mik32_rt::entry берётся из
               // mik32_rt_macros (proc-macro, не оставляет рантайм-символов
               // сама по себе), поэтому без "use mik32_rt as _;" линковщик
               // не подтягивает asm.rs (_start/trap_entry) из mik32-rt —
               // сборка формально проходит, но без точки входа вообще, без
               // единой ошибки (см. docs/implementations/rust_codegen.md).
               "use mik32_rt as _;\n"
               // Аналогично обязательно: mik32v2-pac по умолчанию требует
               // реализацию critical-section (default = ["critical-section"]),
               // которую регистрирует riscv (фича critical-section-single-hart,
               // Cargo.toml сгенерированного проекта) — но только если сам
               // крейт riscv на что-то сослались, иначе он не линкуется и
               // сборка падает с "undefined symbol: _critical_section_1_0_acquire".
               "use riscv as _;\n"
               "use mik32v2_pac::Peripherals;\n"
               "use mik32v2_hal::rcc;\n";
    if (has_gpio)
    {
        main_rs << "use mik32v2_hal::gpio::GpioExt;\n";
    }
    if (has_drive_strength)
    {
        main_rs << "use mik32v2_hal::gpio::DriveStrength;\n";
    }
    if (has_uart)
    {
        main_rs << "use mik32v2_hal::serial::{Serial, Config as UartConfig, DataBits, StopBits, Parity};\n";
    }
    main_rs << "\n"
               "// USER CODE BEGIN Includes\n"
               "// USER CODE END Includes\n"
               "\n"
               "#[entry]\n"
               "fn main() -> ! {\n"
               "    rcc::Config::init(rcc::Config::default());\n"
               "\n"
               "    let p = Peripherals::take().unwrap();\n"
               "\n"
            << splits.str();
    if (!splits.str().empty())
    {
        main_rs << "\n";
    }
    main_rs << gpio_setup.str();
    if (!gpio_setup.str().empty())
    {
        main_rs << "\n";
    }
    main_rs << uart_setup.str();
    if (!uart_setup.str().empty())
    {
        main_rs << "\n";
    }
    main_rs << "    // USER CODE BEGIN Init\n"
               "    // USER CODE END Init\n"
               "\n"
               "    loop {\n"
               "        // USER CODE BEGIN Loop\n"
               "        // USER CODE END Loop\n"
               "    }\n"
               "}\n"
               "\n"
               "#[unsafe(export_name = \"trap_handler\")]\n"
               "fn trap() {\n"
               "    loop {}\n"
               "}\n"
               "\n"
               "#[panic_handler]\n"
               "fn panic(_info: &PanicInfo) -> ! {\n"
               "    loop {}\n"
               "}\n";

    GeneratedProjectFirmwareRust result;
    result.main_rs = main_rs.str();
    return ParseResult<GeneratedProjectFirmwareRust>{true, "", result};
}

namespace
{
    // Независимая копия uniqueTempDirName (cmake_project_codegen.cpp) — та
    // же причина, что и copyDirectoryTreeSkipExisting в contribution_utils:
    // маленькая утилита, растиражированная по каждому writeGenerated*Project
    // независимо, а не вынесенная в общий заголовок ради одной функции на
    // три вызывающих места.
    std::string uniqueTempDirName(const std::string& output_project_dir)
    {
        static std::atomic<unsigned> counter{0};
        const auto now = std::chrono::steady_clock::now().time_since_epoch();
        const long long ticks = std::chrono::duration_cast<std::chrono::microseconds>(now).count();
        std::ostringstream out;
        out << output_project_dir << ".tmp-generating-" << ticks << "-" << counter.fetch_add(1);
        return out.str();
    }

    // Вставляет lines сразу после строки, содержащей marker — тот же приём,
    // что insertAfterMarkerLine (cmake_project_codegen.cpp), но с отступом
    // в четыре пробела (стиль Cargo.toml/TOML, а не восемь как в CMakeLists.txt).
    bool insertAfterMarkerLine(std::string& text, const std::string& marker, const std::vector<std::string>& lines)
    {
        const size_t marker_pos = text.find(marker);
        if (marker_pos == std::string::npos)
        {
            return false;
        }
        size_t line_end = text.find('\n', marker_pos);
        const size_t insert_pos = (line_end == std::string::npos) ? text.size() : line_end + 1;

        std::ostringstream block;
        for (const std::string& line : lines)
        {
            block << line << "\n";
        }
        text.insert(insert_pos, block.str());
        return true;
    }

    // Перенос дерева tmp_root поверх уже существующего to_root, файл за
    // файлом — независимая копия mergeMoveInto (cmake_project_codegen.cpp),
    // то же обоснование (см. .h там: "не полностью атомарно... но риск
    // минимален на практике").
    bool mergeMoveInto(const fs::path& tmp_root, const fs::path& to_root,
                        const std::vector<std::string>& protected_relative_paths,
                        std::error_code& fs_error)
    {
        fs::recursive_directory_iterator it(tmp_root, fs_error);
        if (fs_error)
        {
            return false;
        }
        for (const fs::recursive_directory_iterator end; it != end; it.increment(fs_error))
        {
            if (fs_error)
            {
                return false;
            }
            const fs::path relative = fs::relative(it->path(), tmp_root, fs_error);
            if (fs_error)
            {
                return false;
            }
            const std::string relative_generic = relative.generic_string();
            const fs::path destination = to_root / relative;
            const bool is_protected = std::find(protected_relative_paths.begin(), protected_relative_paths.end(),
                                                 relative_generic) != protected_relative_paths.end();

            if (it->is_directory(fs_error) && !fs_error)
            {
                fs::create_directories(destination, fs_error);
            }
            else if (!fs_error)
            {
                if (is_protected)
                {
                    std::error_code exists_error;
                    if (fs::exists(destination, exists_error))
                    {
                        continue;
                    }
                }
                fs::create_directories(destination.parent_path(), fs_error);
                if (fs_error)
                {
                    return false;
                }
                std::error_code remove_error;
                fs::remove(destination, remove_error);
                fs::rename(it->path(), destination, fs_error);
                if (fs_error)
                {
                    fs_error.clear();
                    fs::copy_file(it->path(), destination, fs::copy_options::overwrite_existing, fs_error);
                }
            }
            if (fs_error)
            {
                return false;
            }
        }
        return true;
    }

    // Три строки Cargo.toml [dependencies] под маркер
    // "# MIK32_CONFIGURATOR: DEPENDENCIES" — по HalSourceMode
    // (rust_hal_source_mode), см. templates/mik32_rust_hal_snapshot/VERSION.md,
    // раздел «Назначение»: mik32v2-hal копируется из снэпшота ВСЕГДА (нет
    // публичного репозитория), pac/rt зависят от режима.
    std::vector<std::string> buildDependencyLines(const ProjectConfig& config)
    {
        std::vector<std::string> lines;
        switch (config.rust_hal_source_mode)
        {
            case HalSourceMode::Network:
                // Версия/URL — ровно то, что использует сам апстрим
                // (mik32v2-pac-main/Cargo.toml: repository на crates.io;
                // mik32-hal-main/Cargo.toml: mik32-rt через этот же git URL).
                lines.push_back("mik32v2-pac = \"0.0.1\"");
                lines.push_back("mik32-rt = { git = \"https://github.com/mik32-rs/mik32-rt.git\" }");
                lines.push_back("mik32v2-hal = { path = \"vendor/hal\" }");
                break;
            case HalSourceMode::LocalPath:
                lines.push_back("mik32v2-pac = { path = \"" + config.rust_hal_source_local_path + "/pac\" }");
                lines.push_back("mik32-rt = { path = \"" + config.rust_hal_source_local_path + "/rt\" }");
                lines.push_back("mik32v2-hal = { path = \"" + config.rust_hal_source_local_path + "/hal\" }");
                break;
            case HalSourceMode::Bundled:
            default:
                lines.push_back("mik32v2-pac = { path = \"vendor/pac\" }");
                lines.push_back("mik32-rt = { path = \"vendor/rt\" }");
                lines.push_back("mik32v2-hal = { path = \"vendor/hal\" }");
                break;
        }
        return lines;
    }
}

CodegenWriteResult writeGeneratedRustProject(
    const GeneratedProjectFirmwareRust& firmware,
    const std::string& cargo_skeleton_dir,
    const std::string& rust_hal_snapshot_dir,
    const std::string& output_project_dir,
    const ProjectConfig& config,
    const nlohmann::json& project_json_copy)
{
    const fs::path tmp_dir = utf8Path(uniqueTempDirName(output_project_dir));

    std::error_code cleanup_leftover_error;
    fs::remove_all(tmp_dir, cleanup_leftover_error);

    auto fail = [&](const std::string& message) -> CodegenWriteResult
    {
        std::error_code ignore;
        fs::remove_all(tmp_dir, ignore);
        return CodegenWriteResult{false, message};
    };

    std::error_code fs_error;
    fs::create_directories(tmp_dir, fs_error);
    if (fs_error)
    {
        return fail("Не удалось создать временный каталог " + tmp_dir.u8string() + ": " + fs_error.message());
    }

    // --- 1. Скелет Cargo-проекта --------------------------------------------
    if (!copyDirectoryTreeSkipExisting(cargo_skeleton_dir, tmp_dir.u8string(), fs_error))
    {
        return fail("Не удалось скопировать шаблон " + cargo_skeleton_dir + ": " + fs_error.message());
    }

    // --- 2. mik32v2-hal — копируется из снэпшота ВСЕГДА, независимо от
    // rust_hal_source_mode (см. buildDependencyLines) -----------------------
    if (config.rust_hal_source_mode == HalSourceMode::Bundled
        || config.rust_hal_source_mode == HalSourceMode::Network)
    {
        const fs::path hal_src = utf8Path(rust_hal_snapshot_dir) / "hal";
        std::error_code exists_error;
        if (!fs::exists(hal_src, exists_error))
        {
            return fail("Снэпшот Rust HAL не найден (нет hal/): " + rust_hal_snapshot_dir);
        }
        if (!copyDirectoryTreeSkipExisting(hal_src.u8string(), (tmp_dir / "vendor" / "hal").u8string(), fs_error))
        {
            return fail("Не удалось скопировать mik32v2-hal из " + rust_hal_snapshot_dir + ": " + fs_error.message());
        }
    }

    if (config.rust_hal_source_mode == HalSourceMode::Bundled)
    {
        const fs::path pac_src = utf8Path(rust_hal_snapshot_dir) / "pac";
        const fs::path rt_src = utf8Path(rust_hal_snapshot_dir) / "rt";
        std::error_code exists_error;
        if (!fs::exists(pac_src, exists_error) || !fs::exists(rt_src, exists_error))
        {
            return fail("Снэпшот Rust HAL не содержит pac/ и rt/: " + rust_hal_snapshot_dir);
        }
        if (!copyDirectoryTreeSkipExisting(pac_src.u8string(), (tmp_dir / "vendor" / "pac").u8string(), fs_error)
            || !copyDirectoryTreeSkipExisting(rt_src.u8string(), (tmp_dir / "vendor" / "rt").u8string(), fs_error))
        {
            return fail("Не удалось скопировать pac/rt из " + rust_hal_snapshot_dir + ": " + fs_error.message());
        }
    }
    else if (config.rust_hal_source_mode == HalSourceMode::LocalPath)
    {
        if (config.rust_hal_source_local_path.empty())
        {
            return fail("rust_hal_source_mode=local_path, но rust_hal_source_local_path не задан");
        }
        const fs::path local_root = utf8Path(config.rust_hal_source_local_path);
        std::error_code exists_error;
        if (!fs::exists(local_root / "hal", exists_error) || !fs::exists(local_root / "pac", exists_error)
            || !fs::exists(local_root / "rt", exists_error))
        {
            return fail("Каталог rust_hal_source_local_path (" + config.rust_hal_source_local_path
                + ") не содержит подкаталоги hal/, pac/, rt/");
        }
        // При local_path сам HAL/PAC/RT не копируется вообще — Cargo.toml
        // ссылается напрямую на config.rust_hal_source_local_path (см.
        // buildDependencyLines), путь остаётся вне output_project_dir.
    }
    // HalSourceMode::Network для pac/rt ничего не копирует — Cargo.toml
    // ссылается на них напрямую (crates.io/git), сеть трогает сам cargo
    // при первой сборке, не этот код (тот же принцип, что git clone в
    // cmake_project_codegen.cpp, только сеть здесь дергает не конфигуратор,
    // а Cargo).

    // --- 3. Подстановка зависимостей в Cargo.toml ---------------------------
    const fs::path cargo_toml_path = tmp_dir / "Cargo.toml";
    const FileReadOutcome cargo_toml_read = readUtf8PathFile(cargo_toml_path.u8string());
    if (!cargo_toml_read.success)
    {
        return fail("Не удалось прочитать " + cargo_toml_path.u8string() + " из шаблона: " + cargo_toml_read.diagnostic);
    }
    std::string cargo_toml_content = cargo_toml_read.content;
    if (!insertAfterMarkerLine(cargo_toml_content, "# MIK32_CONFIGURATOR: DEPENDENCIES", buildDependencyLines(config)))
    {
        return fail("Маркер \"# MIK32_CONFIGURATOR: DEPENDENCIES\" не найден в шаблоне " + cargo_toml_path.u8string());
    }
    const FileWriteOutcome cargo_toml_write = writeUtf8PathFile(cargo_toml_path.u8string(), cargo_toml_content);
    if (!cargo_toml_write.success)
    {
        return fail("Не удалось записать " + cargo_toml_path.u8string() + ": " + cargo_toml_write.diagnostic);
    }

    // --- 4. src/main.rs (USER CODE слияние с уже существующим на диске) ---
    const fs::path tmp_src = tmp_dir / "src";
    fs::create_directories(tmp_src, fs_error);
    if (fs_error)
    {
        return fail("Не удалось создать " + tmp_src.u8string() + ": " + fs_error.message());
    }

    std::string main_rs_to_write = firmware.main_rs;
    const fs::path existing_main_rs_path = utf8Path(output_project_dir) / "src" / "main.rs";
    if (fs::exists(existing_main_rs_path))
    {
        const FileReadOutcome existing = readUtf8PathFile(existing_main_rs_path.u8string());
        if (existing.success)
        {
            main_rs_to_write = mergeUserCodeSectionsRust(firmware.main_rs, existing.content);
        }
    }
    const FileWriteOutcome main_rs_write = writeUtf8PathFile((tmp_src / "main.rs").u8string(), main_rs_to_write);
    if (!main_rs_write.success)
    {
        return fail("Не удалось записать src/main.rs: " + main_rs_write.diagnostic);
    }

    // --- 5. project.mik32 ---------------------------------------------------
    const FileWriteOutcome project_write =
        writeUtf8PathFile((tmp_dir / "project.mik32").u8string(), project_json_copy.dump(4) + "\n");
    if (!project_write.success)
    {
        return fail("Не удалось записать project.mik32: " + project_write.diagnostic);
    }

    // --- 6. Перенос из временного каталога в output_project_dir ------------
    // Cargo.toml и .cargo/config.toml защищены — пользователь мог
    // отредактировать их руками после первой генерации (тот же принцип,
    // что CMakeLists.txt в cmake_project_codegen.cpp).
    static const std::vector<std::string> kProtectedRelativePaths = {
        "Cargo.toml", ".cargo/config.toml"};

    std::error_code output_exists_error;
    const bool output_exists = fs::exists(utf8Path(output_project_dir), output_exists_error);

    if (!output_exists)
    {
        std::error_code rename_error;
        fs::rename(tmp_dir, utf8Path(output_project_dir), rename_error);
        if (rename_error)
        {
            std::error_code copy_error;
            if (!copyDirectoryTreeSkipExisting(tmp_dir.u8string(), output_project_dir, copy_error))
            {
                return fail("Не удалось перенести временный каталог в " + output_project_dir + ": " + copy_error.message());
            }
            std::error_code cleanup_error;
            fs::remove_all(tmp_dir, cleanup_error);
        }
    }
    else
    {
        std::error_code merge_error;
        if (!mergeMoveInto(tmp_dir, utf8Path(output_project_dir), kProtectedRelativePaths, merge_error))
        {
            return CodegenWriteResult{false, "Ошибка при переносе файлов в " + output_project_dir
                + " (часть файлов уже могла быть перенесена): " + merge_error.message()};
        }
        std::error_code cleanup_error;
        fs::remove_all(tmp_dir, cleanup_error);
    }

    return CodegenWriteResult{true, ""};
}
