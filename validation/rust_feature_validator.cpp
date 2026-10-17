#include "rust_feature_validator.h"
#include <sstream>

std::string findUnsupportedRustFeature(const ProjectConfig& config)
{
    if (config.language != OutputLanguage::Rust)
    {
        return "";
    }

    // Типы периферии, у которых в mik32-hal-main вообще нет ни строчки
    // рабочего кода (см. rust_codegen.md) — не частичная поддержка, а
    // полное отсутствие, поэтому проверяется per-тип, а не per-поле.
    if (!config.timer16_blocks.empty())
    {
        return "Периферия \"timer16\" не поддерживается в Rust-ветке конфигуратора"
               " (в mik32-hal-main нет рабочего кода таймеров — timer.rs/timer/delay.rs"
               " не компилируются даже в апстриме, см. docs/implementations/rust_codegen.md)";
    }
    if (!config.timer32_blocks.empty())
    {
        return "Периферия \"timer32\" не поддерживается в Rust-ветке конфигуратора"
               " (в mik32-hal-main нет рабочего кода таймеров, см. docs/implementations/rust_codegen.md)";
    }
    if (!config.i2c_blocks.empty())
    {
        return "Периферия \"i2c\" не поддерживается в Rust-ветке конфигуратора"
               " (mik32-hal-main не содержит модуля I2C, см. docs/implementations/rust_codegen.md)";
    }
    if (!config.spi_blocks.empty())
    {
        return "Периферия \"spi\" не поддерживается в Rust-ветке конфигуратора"
               " (mik32-hal-main не содержит модуля SPI, см. docs/implementations/rust_codegen.md)";
    }
    if (config.rtc_block.has_value())
    {
        return "Периферия \"rtc\" не поддерживается в Rust-ветке конфигуратора"
               " (mik32-hal-main не содержит модуля RTC, AUDIT_TZ_PERIPHERY_GAPS.md — RTC добавлен только в C-ветке)";
    }
    if (config.crc_block.has_value())
    {
        return "Периферия \"crc\" не поддерживается в Rust-ветке конфигуратора"
               " (mik32-hal-main не содержит модуля CRC, AUDIT_TZ_PERIPHERY_GAPS.md — CRC добавлен только в C-ветке)";
    }
    if (config.eeprom_block.has_value())
    {
        return "Периферия \"eeprom\" не поддерживается в Rust-ветке конфигуратора"
               " (mik32-hal-main не содержит модуля EEPROM, AUDIT_TZ_PERIPHERY_GAPS.md — добавлен только в C-ветке)";
    }
    if (config.wdt_block.has_value())
    {
        return "Периферия \"wdt\" не поддерживается в Rust-ветке конфигуратора"
               " (mik32-hal-main не содержит модуля WDT, AUDIT_TZ_PERIPHERY_GAPS.md — добавлен только в C-ветке)";
    }
    if (!config.dac_blocks.empty())
    {
        return "Периферия \"dac\" не поддерживается в Rust-ветке конфигуратора"
               " (mik32-hal-main не содержит модуля DAC, AUDIT_TZ_PERIPHERY_GAPS.md — добавлен только в C-ветке)";
    }
    if (config.adc_block.has_value())
    {
        return "Периферия \"adc\" не поддерживается в Rust-ветке конфигуратора"
               " (mik32-hal-main не содержит модуля ADC, AUDIT_TZ_PERIPHERY_GAPS.md — добавлен только в C-ветке)";
    }
    if (config.crypto_block.has_value())
    {
        return "Периферия \"crypto\" не поддерживается в Rust-ветке конфигуратора"
               " (mik32-hal-main не содержит модуля CRYPTO, AUDIT_TZ_PERIPHERY_GAPS.md — добавлен только в C-ветке)";
    }
    if (config.otp_block.has_value())
    {
        return "Периферия \"otp\" не поддерживается в Rust-ветке конфигуратора"
               " (mik32-hal-main не содержит модуля OTP, AUDIT_TZ_PERIPHERY_GAPS.md — добавлен только в C-ветке)";
    }
    if (config.tsens_block.has_value())
    {
        return "Периферия \"tsens\" не поддерживается в Rust-ветке конфигуратора"
               " (mik32-hal-main не содержит модуля TSENS, AUDIT_TZ_PERIPHERY_GAPS.md — добавлен только в C-ветке)";
    }
    if (config.pvd_block.has_value())
    {
        return "Периферия \"pvd\" не поддерживается в Rust-ветке конфигуратора"
               " (mik32-hal-main не содержит модуля PVD, AUDIT_TZ_PERIPHERY_GAPS.md — добавлен только в C-ветке)";
    }
    if (config.bus_watchdog_block.has_value())
    {
        return "Периферия \"bus_watchdog\" не поддерживается в Rust-ветке конфигуратора"
               " (mik32-hal-main не содержит модуля bus watchdog, AUDIT_TZ_PERIPHERY_GAPS.md — добавлен только в C-ветке)";
    }
    if (!config.dma_blocks.empty())
    {
        return "Периферия \"dma\" не поддерживается в Rust-ветке конфигуратора"
               " (mik32-hal-main не содержит модуля DMA, docs/implementations/dma.md — добавлен только в C-ветке)";
    }
    if (config.freertos.enabled)
    {
        return "FreeRTOS не поддерживается в Rust-ветке конфигуратора"
               " (нет соответствующей интеграции — только PlatformIO/CMake-C-ветка умеет FreeRTOS)";
    }

    for (const GpioConfig& block : config.gpio_blocks)
    {
        if (block.mode == "timer_serial" || block.mode == "analog")
        {
            std::ostringstream error;
            error << "GPIO instance " << block.instance << ": mode=\"" << block.mode
                  << "\" не поддерживается в Rust-ветке (mik32v2-hal::gpio умеет только"
                     " output/input/serial — into_output/into_floating_input/"
                     "into_pull_up_input/into_pull_down_input/into_serial_port,"
                     " см. docs/implementations/rust_codegen.md)";
            return error.str();
        }
        if (!block.interrupts.empty())
        {
            std::ostringstream error;
            error << "GPIO instance " << block.instance << ": прерывания GPIO IRQ не поддерживаются"
                     " в Rust-ветке (mik32v2-hal::gpio не оборачивает контроллер GPIO_IRQ/LINE_MUX,"
                     " см. docs/implementations/rust_codegen.md)";
            return error.str();
        }
    }

    for (const UartConfig& block : config.uart_blocks)
    {
        if (block.flow_control)
        {
            std::ostringstream error;
            error << "UART instance " << block.instance << ": flow_control=true не поддерживается"
                     " в Rust-ветке (mik32v2-hal::serial не реализует RTS/CTS — потребовались бы"
                     " новые PinTx/PinRx-подобные trait-имплы, которых нет в апстриме,"
                     " см. docs/implementations/rust_codegen.md)";
            return error.str();
        }
        if (!block.dma.empty())
        {
            std::ostringstream error;
            error << "UART instance " << block.instance << ": DMA не поддерживается в Rust-ветке"
                     " (mik32v2-hal::serial работает только в режиме опроса — TXDATA/RXDATA напрямую,"
                     " без привязки к контроллеру DMA, см. docs/implementations/rust_codegen.md)";
            return error.str();
        }
    }

    return "";
}
