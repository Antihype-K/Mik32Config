use super::*;

impl<const P: u8, const N: u8, MODE> Pin<P, N, MODE> {
    /// Configures the pin to operate as an push pull output pin
    /// Initial state will be low.
    pub fn into_output(mut self) -> Pin<P, N, Output> {
        self._set_low();

        unsafe {
            (*Gpio::<P>::ptr()).direction_out().write(|w| w.bits(1 << N));
        }
        Pin::new()
    }

    /// Configures the pin to operate as a floating input pin
    pub fn into_floating_input(mut self) -> Pin<P, N, Input<Floating>> {
        unsafe {
            (*Gpio::<P>::ptr()).direction_in().write(|w| w.bits(1 << N));
            let mask = 0b11 << 2 * N;
            let value = 0b00 << 2 * N;
            match P {
                0 => (*mik32v2_pac::PadConfig::ptr()).pad0_pupd()
                .modify(|r, w| w.bits((r.bits() & !mask) | value)),
                1 => (*mik32v2_pac::PadConfig::ptr()).pad1_pupd()
                .modify(|r, w| w.bits((r.bits() & !mask) | value)),
                2 => (*mik32v2_pac::PadConfig::ptr()).pad2_pupd()
                .modify(|r, w| w.bits((r.bits() & !mask) | value)),
                _ => panic!("Invalid GPIO port number: {}", P)
            }
        };

        Pin::new()
    }

    /// Configures the pin to operate as a pulled down input pin
    pub fn into_pull_down_input(mut self) -> Pin<P, N, Input<Floating>> {
        unsafe {
            (*Gpio::<P>::ptr()).direction_in().write(|w| w.bits(1 << N));
            let mask = 0b11 << 2 * N;
            let value = 0b10 << 2 * N;
            match P {
                0 => (*mik32v2_pac::PadConfig::ptr()).pad0_pupd()
                .modify(|r, w| w.bits((r.bits() & !mask) | value)),
                1 => (*mik32v2_pac::PadConfig::ptr()).pad1_pupd()
                .modify(|r, w| w.bits((r.bits() & !mask) | value)),
                2 => (*mik32v2_pac::PadConfig::ptr()).pad2_pupd()
                .modify(|r, w| w.bits((r.bits() & !mask) | value)),
                _ => panic!("Invalid GPIO port number: {}", P)
            }
        };

        Pin::new()
    }

    /// Configures the pin to operate as a pulled up input pin
    pub fn into_pull_up_input(mut self) -> Pin<P, N, Input<Floating>> {
        unsafe {
            (*Gpio::<P>::ptr()).direction_in().write(|w| w.bits(1 << N));
            let mask = 0b11 << 2 * N;
            let value = 0b01 << 2 * N;
            match P {
                0 => (*mik32v2_pac::PadConfig::ptr()).pad0_pupd()
                .modify(|r, w| w.bits((r.bits() & !mask) | value)),
                1 => (*mik32v2_pac::PadConfig::ptr()).pad1_pupd()
                .modify(|r, w| w.bits((r.bits() & !mask) | value)),
                2 => (*mik32v2_pac::PadConfig::ptr()).pad2_pupd()
                .modify(|r, w| w.bits((r.bits() & !mask) | value)),
                _ => panic!("Invalid GPIO port number: {}", P)
            }
        };

        Pin::new()
    }

    /// Configures the output to work in the Func2 Mode
    pub fn into_serial_port(mut self) -> Pin<P, N, Func2Mode> {
        unsafe {
            (*Gpio::<P>::ptr()).direction_in().write(|w| w.bits(1 << N));
            let mask = 0b11 << 2 * N;
            let value = 0b01 << 2 * N;
            match P {
                0 => (*mik32v2_pac::PadConfig::ptr()).pad0_cfg()
                .modify(|r, w| w.bits((r.bits() & !mask) | value)),
                1 => (*mik32v2_pac::PadConfig::ptr()).pad1_cfg()
                .modify(|r, w| w.bits((r.bits() & !mask) | value)),
                2 => (*mik32v2_pac::PadConfig::ptr()).pad2_cfg()
                .modify(|r, w| w.bits((r.bits() & !mask) | value)),
                _ => panic!("Invalid GPIO port number: {}", P)
            }
        };

        Pin::new()
    }

    /// Configures the output to work in Func3 ("timer/serial") mode — PADx_CFG
    /// value 0b10, confirmed by mik32_hal_gpio.h (установленный HAL,
    /// HAL_GPIO_MODE_TIMER_SERIAL = 0b10), та же PADx_CFG-таблица, что уже
    /// использует into_serial_port (0b01) выше. Добавлено для Timer16 Rust-
    /// паритета (templates/mik32_rust_hal_snapshot/hal/src/timer16.rs).
    pub fn into_timer_serial(mut self) -> Pin<P, N, TimerSerialMode> {
        unsafe {
            (*Gpio::<P>::ptr()).direction_in().write(|w| w.bits(1 << N));
            let mask = 0b11 << 2 * N;
            let value = 0b10 << 2 * N;
            match P {
                0 => (*mik32v2_pac::PadConfig::ptr()).pad0_cfg()
                .modify(|r, w| w.bits((r.bits() & !mask) | value)),
                1 => (*mik32v2_pac::PadConfig::ptr()).pad1_cfg()
                .modify(|r, w| w.bits((r.bits() & !mask) | value)),
                2 => (*mik32v2_pac::PadConfig::ptr()).pad2_cfg()
                .modify(|r, w| w.bits((r.bits() & !mask) | value)),
                _ => panic!("Invalid GPIO port number: {}", P)
            }
        };

        Pin::new()
    }
}

/// Нагрузочная способность вывода (регистр PADx_DS) — Датащит МИК32 v2.2.3,
/// Таблица 99 «Поля регистров PADx_DS управления нагрузочной способностью
/// выводов», с.183-184: 0b00=2мА, 0b01=4мА, 0b10=8мА. То же кодирование, что
/// уже используется в C-ветке конфигуратора (knowledge_base/gpio.json:46-51,
/// HAL_GPIO_DS_2MA/4MA/8MA).
///
/// Не входит в исходный mik32-hal-main — там `gpio/convert.rs` пишет только
/// PADx_PUPD/PADx_CFG, регистр PADx_DS существует в PAC
/// (mik32v2-pac-main/src/pad_config/pad0_ds.rs и т.д.), но не был обёрнут ни
/// одним методом. Добавлено конфигуратором при вендоринге снэпшота — см.
/// docs/implementations/rust_codegen.md.
#[derive(Debug, PartialEq, Eq, Clone, Copy)]
pub enum DriveStrength {
    Ma2,
    Ma4,
    Ma8,
}

impl<const P: u8, const N: u8, MODE> Pin<P, N, MODE> {
    /// Включает подтяжку к питанию (PADx_PUPD=0b01), не трогая
    /// направление/мультиплексор вывода (PADx_CFG) — нужно отдельно от
    /// into_serial_port() для открытого стока I2C: HAL_I2C_MspInit
    /// (mik32_hal_i2c.c:3-26) настраивает SDA/SCL с Pull=HAL_GPIO_PULL_UP
    /// одним вызовом HAL_GPIO_Init, а into_serial_port() (выше в этом файле)
    /// пишет только PADx_CFG, подтяжку не трогает — см.
    /// templates/mik32_rust_hal_snapshot/hal/src/i2c.rs,
    /// knowledge_base/i2c.json:_comment_instance_fixed_pins. Не входит в
    /// исходный mik32-hal-main (там не было I2C вовсе).
    pub fn set_pull_up(&mut self) {
        unsafe {
            let mask = 0b11 << (2 * N);
            let value = 0b01 << (2 * N);
            match P {
                0 => (*mik32v2_pac::PadConfig::ptr()).pad0_pupd()
                    .modify(|r, w| w.bits((r.bits() & !mask) | value)),
                1 => (*mik32v2_pac::PadConfig::ptr()).pad1_pupd()
                    .modify(|r, w| w.bits((r.bits() & !mask) | value)),
                2 => (*mik32v2_pac::PadConfig::ptr()).pad2_pupd()
                    .modify(|r, w| w.bits((r.bits() & !mask) | value)),
                _ => panic!("Invalid GPIO port number: {}", P),
            }
        };
    }
}

impl<const P: u8, const N: u8, MODE> Pin<P, N, MODE> {
    /// Настраивает нагрузочную способность вывода. Не переключает
    /// direction/mux (в отличие от into_output/into_serial_port и т.п.) —
    /// применяется дополнительно, после выбора режима, как и в C HAL
    /// (mik32_hal_gpio.c: один вызов HAL_GPIO_Init одновременно пишет
    /// DIRECTION и все три регистра PAD_CONFIG, включая DS).
    pub fn set_drive_strength(&mut self, ds: DriveStrength) {
        let bits: u32 = match ds {
            DriveStrength::Ma2 => 0b00,
            DriveStrength::Ma4 => 0b01,
            DriveStrength::Ma8 => 0b10,
        };
        unsafe {
            let mask = 0b11 << (2 * N);
            let value = bits << (2 * N);
            match P {
                0 => (*mik32v2_pac::PadConfig::ptr()).pad0_ds()
                    .modify(|r, w| w.bits((r.bits() & !mask) | value)),
                1 => (*mik32v2_pac::PadConfig::ptr()).pad1_ds()
                    .modify(|r, w| w.bits((r.bits() & !mask) | value)),
                2 => (*mik32v2_pac::PadConfig::ptr()).pad2_ds()
                    .modify(|r, w| w.bits((r.bits() & !mask) | value)),
                _ => panic!("Invalid GPIO port number: {}", P),
            }
        };
    }
}