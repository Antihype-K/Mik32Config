//! I2C HAL-обёртка (мастер/слейв, фильтры, general call, no-stretch, второй
//! адрес+маска, авторесет) — Rust-паритет с codegen/project/i2c_contribution.cpp
//! (C-ветка). mik32-hal-main НЕ содержит модуля I2C вовсе (см.
//! docs/implementations/rust_codegen.md, docs/implementations/rust/i2c.md) —
//! это новый модуль, написанный по тому же паттерну, что serial.rs (в этом же
//! каталоге), но без Read/Write-трансиверов: C-ветка кодогена тоже не
//! генерирует сами Transmit/Receive-вызовы, только Init/Reset — пользователь
//! дергает блокирующие HAL-функции вручную из USER CODE. Здесь — то же самое:
//! I2c::new()/reset() настраивают регистры, сами транзакции (нужен доступ к
//! CR2/ISR/TXDR/RXDR) пользователь делает через .free() ниже.
//!
//! DMA НЕ реализован (явно отклоняется в rust_feature_validator.cpp) — ни
//! mik32v2-hal, ни PAC-снэпшот (templates/mik32_rust_hal_snapshot/pac/src/dma*)
//! не содержат драйвера контроллера DMA с обёрткой ChannelInit, которая есть
//! в C-ветке (mik32_hal_dma.c) — тот же критерий, что уже применён к UART DMA.
//!
//! ВАЖНО (PAC-особенность, НЕ как у USART): mik32v2-pac генерирует I2C_0 и
//! I2C_1 как ДВА РАЗНЫХ модуля (i2c_0::RegisterBlock / i2c_1::RegisterBlock,
//! структурно идентичные, но разные типы Rust — в отличие от USART, где оба
//! инстанса делят один usart_0::RegisterBlock). Поэтому общий Deref-bound
//! (как Instance в serial.rs) здесь не типизируется один раз на оба
//! инстанса — вместо этого Instance объявляет методы настройки регистров
//! напрямую, а macro_rules! дублирует ОДИНАКОВЫЙ код для каждого конкретного
//! типа (I2c0/I2c1) — тела идентичны, различаются только подставляемые типы.

use mik32v2_pac::Pm;

use crate::gpio::{self, Func2Mode};

/// SDA/SCL-пины конкретного блока I2C — неразличимая пара (см.
/// knowledge_base/i2c.json, _comment_instance_fixed_pins: HAL
/// (mik32_hal_i2c.c:3-26, HAL_I2C_MspInit) настраивает оба пина одной маской
/// HAL_GPIO_Init, порядок в маске не гарантирует физического соответствия
/// SDA/SCL — не выдумываем неподтверждённый факт, как и в C-ветке).
pub trait PinI2c<I> {}

pub trait Pins<I> {}
impl<I, A, B> Pins<I> for (A, B)
where
    A: PinI2c<I>,
    B: PinI2c<I>,
{
}

// Датащит МИК32 v2.2.3, Таблица 193 «Назначение выводов»; те же номера, что
// уже проверены в C-ветке (knowledge_base/i2c.json:instance_fixed_pins) и в
// Rust codegen (codegen/project/rust_project_codegen.cpp, i2cSdaPin/i2cSclPin):
// I2C_0 -> GPIO_0 пины 9,10; I2C_1 -> GPIO_1 пины 12,13.
impl PinI2c<mik32v2_pac::I2c0> for gpio::P16_0_9<Func2Mode> {}
impl PinI2c<mik32v2_pac::I2c0> for gpio::P16_0_10<Func2Mode> {}
impl PinI2c<mik32v2_pac::I2c1> for gpio::P16_1_12<Func2Mode> {}
impl PinI2c<mik32v2_pac::I2c1> for gpio::P16_1_13<Func2Mode> {}

/// CR1.PE — HAL_I2C_MODE_MASTER/HAL_I2C_MODE_SLAVE (mik32_hal_i2c.c:178-197,
/// cr1.rs не содержит отдельного "mode"-бита — режим задаётся тем, какие поля
/// кодоген заполняет после Init, см. new() ниже).
#[derive(Debug, PartialEq, Eq, Clone, Copy)]
pub enum Mode { Master, Slave }

/// OAR1.OA1MODE — oar1.rs, Oa1mode::_7bit/_10bit.
#[derive(Debug, PartialEq, Eq, Clone, Copy)]
pub enum AddressMode { Seven, Ten }

/// OAR2.OA2MSK — oar2.rs, Oa2msk (8 значений), то же кодирование и порядок,
/// что ownAddress2MaskConstant в i2c_contribution.cpp (schemas/i2c.json enum
/// "none".."1111111").
#[derive(Debug, PartialEq, Eq, Clone, Copy)]
pub enum OwnAddress2Mask {
    None,
    Mask111111X,
    Mask11111XX,
    Mask1111XXX,
    Mask111XXXX,
    Mask11XXXXX,
    Mask1XXXXXX,
    Mask1111111,
}

/// PRESC/SCLDEL/SDADEL/SCLH/SCLL — считаются конфигуратором НА ЭТАПЕ
/// КОДОГЕНЕРАЦИИ (knowledge_base/i2c.json: call_shape.
/// timingr_computed_at_codegen_time_not_runtime), той же формулой, что
/// calcI2cTimingr в i2c_contribution.cpp — реальный HAL_I2C_calcFreqCoef
/// физически отсутствует в установленном SDK (см. комментарий в
/// i2c_contribution.cpp). Здесь — только литералы, без расчёта в рантайме.
#[derive(Debug, Clone, Copy)]
pub struct Timing {
    pub presc: u8,
    pub scldel: u8,
    pub sdadel: u8,
    pub sclh: u8,
    pub scll: u8,
}

/// I2C configuration — расширено конфигуратором относительно нуля (в
/// mik32-hal-main такого модуля не было вовсе). Поля — 1:1 с I2cConfig
/// (parser/i2c/i2c_parser.h) за вычетом dma/interrupt_mode (отклонены в
/// rust_feature_validator.cpp).
pub struct Config {
    pub mode: Mode,
    pub timing: Timing,
    // Общие поля — читаются обеими ветками HAL_I2C_Init (mik32_hal_i2c.c:
    // "Configure ANFOFF and DNF[3:0] in I2C_CR1" выполняется до switch по Mode).
    pub digital_filter: u8, // 0 (выключен) / 1 / 2 / 15 тактов I2CCLK — cr1.rs Dnf
    pub analog_filter: bool,
    // Только для mode == Slave (как и в C HAL_I2C_SlaveInit).
    pub own_address: u16,
    pub address_mode: AddressMode,
    pub dual_address_enabled: bool,
    pub own_address_2: u8,
    pub own_address_2_mask: OwnAddress2Mask,
    pub general_call: bool,
    pub no_stretch_mode: bool,
}

/// I2C peripheral wrapper.
pub struct I2c<I, PINS> {
    i2c: I,
    pins: PINS,
}

impl<I, PINS> I2c<I, PINS>
where
    PINS: Pins<I>,
    I: Instance,
{
    pub fn new(i2c: I, pins: PINS, config: Config) -> Self {
        let pm = unsafe { &(*Pm::ptr()) };
        I::enable_clock(&pm);

        // "Clear PE in I2C_CR1" (mik32_hal_i2c.c, HAL_I2C_Init, HAL_I2C_Disable) —
        // большинство полей ниже (DNF/ANFOFF/TIMINGR/NOSTRETCH) по докомментариям
        // регистров (cr1.rs/timingr.rs) меняются только при PE=0.
        i2c.set_pe(false);

        // "Configure ANFOFF and DNF[3:0] in I2C_CR1" (mik32_hal_i2c.c,
        // HAL_I2C_AnalogFilterInit/HAL_I2C_DigitalFilterInit).
        i2c.set_filters(config.analog_filter, config.digital_filter);

        // "Configure PRESC/SDADEL/SCLDEL/SCLH/SCLL in I2C_TIMINGR"
        // (mik32_hal_i2c.c, HAL_I2C_SetClockSpeed) — literal-значения,
        // посчитанные конфигуратором заранее (см. Timing выше).
        i2c.set_timing(config.timing);

        // "Configure NOSTRETCH in I2C_CR1" — HAL всегда форсирует
        // NOSTRETCH_DISABLE для master (mik32_hal_i2c.c: `if (Mode ==
        // MASTER) HAL_I2C_NoStretchMode(hi2c, I2C_NOSTRETCH_DISABLE)`), для
        // slave берёт пользовательское значение.
        let stretch_disable = match config.mode {
            Mode::Master => false,
            Mode::Slave => config.no_stretch_mode,
        };
        i2c.set_nostretch_disable(stretch_disable);

        // "Set PE in I2C_CR1" — до SlaveInit, именно в этом порядке в
        // оригинале (mik32_hal_i2c.c, HAL_I2C_Init: Enable() вызывается ДО
        // switch(Mode) { SlaveInit/MasterInit }).
        i2c.set_pe(true);

        if config.mode == Mode::Slave {
            // HAL_I2C_OwnAddress1 (mik32_hal_i2c.c:99-121).
            i2c.set_oar1(config.address_mode, config.own_address);

            // HAL_I2C_OwnAddress2 (mik32_hal_i2c.c:124-137) — только если
            // dual_address_enabled, иначе OAR2 остаётся в сброшенном
            // состоянии (как и C: OwnAddress2=0, Mask=DISABLE при
            // dual_address_enabled=false, см. i2c_contribution.cpp:228-232).
            if config.dual_address_enabled {
                i2c.set_oar2(config.own_address_2, config.own_address_2_mask);
            }

            // HAL_I2C_GeneralCall (mik32_hal_i2c.c:139-144): CR1.GCEN.
            i2c.set_general_call(config.general_call);

            // HAL_I2C_SBCMode (mik32_hal_i2c.c:146-151) — хардкод DISABLE,
            // как и в C-ветке (i2c_contribution.cpp:235, "SBCMode =
            // I2C_SBC_DISABLE" — не запрошено ни ТЗ, ни схемой, см.
            // knowledge_base/i2c.json: not_covered_by_this_codegen.slave_full_config).
            i2c.set_sbc_disable();
        }

        I2c { i2c, pins }
    }

    /// Авторесет — HAL_I2C_Reset (mik32_hal_i2c.c:32-38): снять и снова
    /// установить PE. Параллель с I2C_<N>_Reset() в C-ветке
    /// (i2c_contribution.cpp:313-316); hi2c->ErrorCode здесь нет (этот Rust
    /// HAL не отслеживает отдельное поле ошибки) — сброс самого интерфейса
    /// идентичен.
    pub fn reset(&mut self) {
        self.i2c.set_pe(false);
        self.i2c.set_pe(true);
    }

    /// Освобождает периферию/пины — доступ к сырым регистрам CR2/ISR/TXDR/
    /// RXDR для ручных транзакций остаётся пользовательскому коду (USER
    /// CODE), как и в C-ветке, где кодоген тоже не генерирует сами
    /// Transmit/Receive-вызовы.
    pub fn free(self) -> (I, PINS) {
        (self.i2c, self.pins)
    }
}

/// Implemented by all I2C instances. Методы, а не единый Deref-bound — см.
/// комментарий в начале файла (I2c0/I2c1 деляют на два разных
/// RegisterBlock-типа в PAC-снэпшоте, не на один общий, как usart_0 у UART).
pub trait Instance {
    fn enable_clock(pm: &mik32v2_pac::pm::RegisterBlock);
    fn set_pe(&self, enable: bool);
    fn set_filters(&self, analog_filter_enabled: bool, digital_filter_cycles: u8);
    fn set_timing(&self, timing: Timing);
    fn set_nostretch_disable(&self, disable: bool);
    fn set_oar1(&self, mode: AddressMode, own_address: u16);
    fn set_oar2(&self, own_address_2: u8, mask: OwnAddress2Mask);
    fn set_general_call(&self, enable: bool);
    fn set_sbc_disable(&self);
}

macro_rules! impl_instance {
    ($(
        $I2CX:ident: $i2cXen:ident,
    )+) => {
        $(
            impl Instance for mik32v2_pac::$I2CX {
                fn enable_clock(pm: &mik32v2_pac::pm::RegisterBlock) {
                    pm.clk_apb_p_set().modify(|_, w| w.$i2cXen().set_bit());
                }

                fn set_pe(&self, enable: bool) {
                    self.cr1().modify(|_, w| if enable { w.pe().enable() } else { w.pe().disable() });
                }

                // ANFOFF — инвертированная логика: Anfoff::Disable(=0)
                // означает, что аналоговый фильтр ВКЛЮЧЁН (бит "ANF OFF"
                // снят) — см. cr1.rs, Anfoff-enum, и тот же инвертированный
                // комментарий в i2c_contribution.cpp ("ENABLE=0, DISABLE=1").
                // DNF — только 0/1/2/15 реально достижимы из схемы
                // конфигуратора (schemas/i2c.json), но пишем точное число.
                fn set_filters(&self, analog_filter_enabled: bool, digital_filter_cycles: u8) {
                    self.cr1().modify(|_, w| {
                        let w = if analog_filter_enabled { w.anfoff().disable() } else { w.anfoff().enable() };
                        match digital_filter_cycles {
                            1 => w.dnf()._1(),
                            2 => w.dnf()._2(),
                            15 => w.dnf()._15(),
                            _ => w.dnf().disable(),
                        }
                    });
                }

                fn set_timing(&self, timing: Timing) {
                    self.timingr().write(|w| unsafe {
                        w.presc().bits(timing.presc)
                            .scldel().bits(timing.scldel)
                            .sdadel().bits(timing.sdadel)
                            .sclh().bits(timing.sclh)
                            .scll().bits(timing.scll)
                    });
                }

                fn set_nostretch_disable(&self, disable: bool) {
                    self.cr1().modify(|_, w| {
                        if disable { w.nostretch().stretching_disable() } else { w.nostretch().stretching_enabled() }
                    });
                }

                // HAL_I2C_OwnAddress1 (mik32_hal_i2c.c:99-121): OAR1=0, затем
                // OA1MODE+OA1 одним проходом — адрес не сдвигается вручную
                // влево на 1, как в C (там это компенсирует то, что
                // I2C_OAR1_OA1_S начинается с бита 0 в регистровой карте):
                // PAC-поле oa1_7bit уже занимает биты [1:7] само (см.
                // oar1.rs:125-127, "Bits 1:7"), поэтому пишем 7-битный адрес
                // без сдвига.
                // OAR1.OA1EN — oar1.rs: Oa1en::Nack/Ack (0/1), не
                // Disable/Enable — "включить адрес" выражено как "отвечать
                // ACK на совпадение", а не отдельным enable-битом.
                fn set_oar1(&self, mode: AddressMode, own_address: u16) {
                    self.oar1().write(|w| unsafe {
                        match mode {
                            AddressMode::Seven => w.oa1mode()._7bit().oa1_7bit().bits(own_address as u8 & 0x7F),
                            AddressMode::Ten => w.oa1mode()._10bit().oa1_10bit().bits(own_address & 0x3FF),
                        }.oa1en().ack()
                    });
                }

                // HAL_I2C_OwnAddress2 (mik32_hal_i2c.c:124-137): OAR2=0,
                // маска+адрес+OA2EN одним проходом.
                fn set_oar2(&self, own_address_2: u8, mask: OwnAddress2Mask) {
                    self.oar2().write(|w| unsafe {
                        let w = w.oa2().bits(own_address_2 & 0x7F);
                        let w = match mask {
                            OwnAddress2Mask::None => w.oa2msk().no_mask(),
                            OwnAddress2Mask::Mask111111X => w.oa2msk()._1_1_masked(),
                            OwnAddress2Mask::Mask11111XX => w.oa2msk()._2_1_masked(),
                            OwnAddress2Mask::Mask1111XXX => w.oa2msk()._3_1_masked(),
                            OwnAddress2Mask::Mask111XXXX => w.oa2msk()._4_1_masked(),
                            OwnAddress2Mask::Mask11XXXXX => w.oa2msk()._5_1_masked(),
                            OwnAddress2Mask::Mask1XXXXXX => w.oa2msk()._6_1_masked(),
                            OwnAddress2Mask::Mask1111111 => w.oa2msk()._7_1_masked(),
                        };
                        // OAR2.OA2EN — та же Ack/Nack-семантика, что OA1EN.
                        w.oa2en().ack()
                    });
                }

                // HAL_I2C_GeneralCall (mik32_hal_i2c.c:139-144): CR1.GCEN.
                fn set_general_call(&self, enable: bool) {
                    self.cr1().modify(|_, w| if enable { w.gcen().enable() } else { w.gcen().disabled() });
                }

                // HAL_I2C_SBCMode (mik32_hal_i2c.c:146-151) — хардкод DISABLE.
                fn set_sbc_disable(&self) {
                    self.cr1().modify(|_, w| w.sbc().disable());
                }
            }
        )+
    }
}

impl_instance! {
    I2c0: i2c_0,
    I2c1: i2c_1,
}
