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
    // timer16 — реализован (templates/mik32_rust_hal_snapshot/hal/src/timer16.rs,
    // новый модуль, mik32-hal-main его не содержал вовсе — апстримный
    // timer.rs/timer/delay.rs мёртвый код). Прерывания/trigger_source
    // отклоняются per-блок ниже (тот же критерий, что и у I2C DMA/interrupt_mode).
    for (const Timer16Config& block : config.timer16_blocks)
    {
        if (block.overflow_interrupt || block.compare_interrupt)
        {
            std::ostringstream error;
            error << "Timer16 instance " << block.instance << ": overflow_interrupt/compare_interrupt"
                     " не поддерживаются в Rust-ветке (нет обёртки EPIC/trap_handler в mik32v2-hal,"
                     " тот же пробел, что уже зафиксирован для UART/I2C, см. docs/implementations/rust/timer16.md)";
            return error.str();
        }
        if (!block.trigger_source.empty())
        {
            std::ostringstream error;
            error << "Timer16 instance " << block.instance << ": trigger_source не поддерживается в"
                     " Rust-ветке (Trigger.Source не реализован даже в C-ветке — timer16_contribution.cpp,"
                     " knowledge_base/timer16.json: not_covered_by_this_codegen.trigger)";
            return error.str();
        }
    }
    // timer32 — реализован (templates/mik32_rust_hal_snapshot/hal/src/timer32.rs,
    // новый модуль). Полный паритет с C-веткой (timer32_contribution.cpp) —
    // ни одно поле схемы не требует отказа, см. docs/implementations/rust/timer32.md.
    // i2c — реализован 2026-10-26 (templates/mik32_rust_hal_snapshot/hal/src/i2c.rs,
    // новый модуль, mik32-hal-main его не содержал вовсе). DMA для I2C
    // отклоняется ниже per-блок (тот же критерий, что и у UART DMA — ни
    // PAC, ни HAL-снэпшот не содержат драйвера контроллера DMA).
    // spi — реализован (templates/mik32_rust_hal_snapshot/hal/src/spi.rs,
    // новый модуль). interrupts отклоняется per-блок ниже (тот же критерий,
    // что у I2C interrupt_mode/GPIO IRQ — mik32v2-hal не оборачивает EPIC).
    // rtc — реализован (templates/mik32_rust_hal_snapshot/hal/src/rtc.rs,
    // новый модуль). Полный паритет с C-веткой (rtc_contribution.cpp),
    // включая будильник+interrupt_enabled — EPIC-подключение остаётся на
    // стороне пользователя даже в C-ветке, см. docs/implementations/rust/rtc.md.
    // crc — реализован (templates/mik32_rust_hal_snapshot/hal/src/crc.rs,
    // новый модуль). Полный паритет с C-веткой (crc_contribution.cpp) —
    // только конфигурация алгоритма, write_data/read_crc на собственных
    // данных пользователя кодоген не угадывает (как и в C), см.
    // docs/implementations/rust/crc.md.
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
        // flow_control (аппаратный RTS/CTS) теперь реализован через
        // CONTROL3.RTSE/CTSE (mik32v2-hal::serial, см. rust_project_codegen.cpp
        // и templates/mik32_rust_hal_snapshot/hal/src/serial.rs) — больше не
        // отклоняется.
        if (!block.dma.empty())
        {
            std::ostringstream error;
            error << "UART instance " << block.instance << ": DMA не поддерживается в Rust-ветке"
                     " (mik32v2-hal::serial работает только в режиме опроса — TXDATA/RXDATA напрямую,"
                     " без привязки к контроллеру DMA, см. docs/implementations/rust_codegen.md)";
            return error.str();
        }
        // Errata MIK32V2 v1.0.1 §10.1 — тот же запрет, что уже проверен в
        // C-ветке (parser/USART/usart_parser.cpp): синхронный режим на
        // instance=1 даёт паразитный сигнал CK на PORT2_6 без обхода.
        if (block.mode == "synchronous" && block.instance == 1)
        {
            std::ostringstream error;
            error << "UART instance 1: mode=\"synchronous\" запрещён в Rust-ветке так же, как и в "
                     "C-ветке (Errata MIK32V2 v1.0.1 §10.1 — паразитный сигнал CK на PORT2_6, "
                     "см. docs/implementations/uart.md)";
            return error.str();
        }
        // ДИАГНОСТИКА PAC: регистр MODEM (usart_0/modem.rs) физически
        // реализует запись только для бита DTR (dtr(), bit12) — DCD/DSR/RI
        // в этом регистре read-only (входные статусные биты, без записи),
        // а бита DDIS (из C-структуры HAL_USART_HandleTypeDef.Modem.ddis)
        // в этом регистре нет совсем. Угадывать несуществующий бит
        // запрещено (корневой CLAUDE.md), поэтому полный "модем"-режим
        // отклоняется целиком, а не наполовину.
        if (block.has_modem_signals)
        {
            std::ostringstream error;
            error << "UART instance " << block.instance << ": modem_signals не поддерживается в"
                     " Rust-ветке (CONTROL.MODEM в PAC-снэпшоте позволяет только записать DTR;"
                     " DCD/DSR/RI в этом регистре read-only, бита DDIS нет вовсе —"
                     " templates/mik32_rust_hal_snapshot/pac/src/usart_0/modem.rs)";
            return error.str();
        }
        if (block.rts_mode == "modem")
        {
            std::ostringstream error;
            error << "UART instance " << block.instance << ": rts_mode=\"modem\" не поддерживается"
                     " в Rust-ветке (CONTROL3.RTSE в PAC-снэпшоте — простой enable/disable"
                     " аппаратного RTS, отдельного бита \"модемного\" режима RTS там нет —"
                     " templates/mik32_rust_hal_snapshot/pac/src/usart_0/control3.rs)";
            return error.str();
        }
    }

    for (const I2cConfig& block : config.i2c_blocks)
    {
        // Ни mik32v2-hal, ни PAC-снэпшот (templates/mik32_rust_hal_snapshot/
        // pac/src/dma*) не содержат драйвера контроллера DMA с обёрткой
        // ChannelInit, которая есть в C-ветке (mik32_hal_dma.c) — тот же
        // критерий, что уже применён к UART DMA выше.
        if (!block.dma.empty())
        {
            std::ostringstream error;
            error << "I2C instance " << block.instance << ": DMA не поддерживается в Rust-ветке"
                     " (mik32v2-hal не содержит драйвера контроллера DMA, см."
                     " templates/mik32_rust_hal_snapshot/hal/src/i2c.rs)";
            return error.str();
        }
        // interrupt_mode (шаблон IRQHandler) в C-ветке — отдельная
        // инфраструктура EPIC (HAL_EPIC_*, mik32_hal_irq.h), которую
        // mik32v2-hal не оборачивает вовсе (ни для GPIO, ни для UART, ни
        // здесь) — то же ограничение, что уже зафиксировано для GPIO IRQ.
        if (block.interrupt_mode)
        {
            std::ostringstream error;
            error << "I2C instance " << block.instance << ": interrupt_mode не поддерживается в"
                     " Rust-ветке (mik32v2-hal не оборачивает контроллер EPIC, см."
                     " templates/mik32_rust_hal_snapshot/hal/src/i2c.rs)";
            return error.str();
        }
    }

    for (const SpiConfig& block : config.spi_blocks)
    {
        // HAL_SPI_IRQHandler (шаблон, генерируемый C-веткой, spi_contribution.cpp)
        // рассчитан на подключение к EPIC/raw_trap_handler — mik32v2-hal не
        // оборачивает контроллер EPIC ни для одной периферии (тот же
        // критерий, что у I2C interrupt_mode и GPIO IRQ выше).
        if (!block.interrupts.empty())
        {
            std::ostringstream error;
            error << "SPI instance " << block.instance << ": interrupts не поддерживаются в"
                     " Rust-ветке (mik32v2-hal не оборачивает контроллер EPIC, см."
                     " templates/mik32_rust_hal_snapshot/hal/src/spi.rs)";
            return error.str();
        }
    }

    return "";
}
