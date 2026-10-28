//! SPI HAL-обёртка — Rust-паритет с codegen/project/spi_contribution.cpp
//! (C-ветка), за вычетом поля `interrupts` (см. ниже). mik32-hal-main НЕ
//! содержит модуля SPI вовсе (docs/implementations/rust_codegen.md) — новый
//! модуль, написан по тому же паттерну, что i2c.rs (в этом же каталоге):
//! Instance настраивает только регистры, сами TXDATA/RXDATA-транзакции
//! пользователь делает вручную через .free() — C-ветка кодогена тоже не
//! генерирует вызовы HAL_SPI_Exchange, только HAL_SPI_Init.
//!
//! `interrupts` (schemas/spi.json) НЕ реализован — отклоняется в
//! rust_feature_validator.cpp тем же критерием, что уже применён к I2C
//! interrupt_mode и GPIO IRQ: HAL_SPI_IRQHandler (шаблон, который генерирует
//! C-ветка) рассчитан на подключение к EPIC/raw_trap_handler, а mik32v2-hal
//! не оборачивает контроллер EPIC ни для одной периферии.
//!
//! ВАЖНО (найдено при порте, не угадано заранее): mik32v2-pac генерирует
//! именованные варианты поля CONFIG.CS (spi_0/config.rs, enum `Cs`) с
//! числовыми значениями NotSelected=15/Cs1=1/Cs2=3/Cs3=7/Cs4=0 — они НЕ
//! совпадают с реальными HAL-макросами (mik32_hal_spi.h:22-26: SPI_CS_NONE=
//! 0b1111=15, SPI_CS_0=0b1110=14, SPI_CS_1=0b1101=13, SPI_CS_2=0b1011=11,
//! SPI_CS_3=0b0111=7 — совпадает только NotSelected и, по случайному
//! совпадению значения, Cs3/SPI_CS_3). Тот же класс бага, что уже пойман в
//! Timer32 (инвертированный PWM_INV) — писать через именованные
//! `.cs().cs1()` и т.п. означало бы выбрать не тот ведомый на реальной
//! плате. Поэтому CS и Decoder (ниже) пишутся raw-битами напрямую по
//! числам из mik32_hal_spi.h, а не через автогенерированные варианты PAC.
//!
//! Decoder (SPI_CONFIG_PERI_SEL, бит 9 — mik32_sdk_snapshot/shared/periphery/
//! spi.h:29-30) в PAC вообще не экспонирован как поле (ни в config.rs, ни
//! где-либо ещё в spi_0/spi_1) — пишется raw-битом через `.bits()` всего
//! регистра CONFIG, с явным указанием позиции бита в комментарии ниже.

use mik32v2_pac::Pm;

use crate::gpio::{self, Func2Mode};

/// SCK/MOSI/MISO/SS_IN — 4 пина, резервируются ВСЕГДА при использовании
/// instance, независимо от mode (knowledge_base/spi.json:
/// instance_fixed_pins, mik32_hal_spi.c, HAL_SPI_MspInit:16,57 — маска
/// GPIO_PIN_0|1|2 плюс отдельно GPIO_PIN_3). Роль каждого пина внутри
/// четвёрки не гарантирована (тот же случай неразличимости, что и SDA/SCL
/// в i2c.rs) — трейт единый, без раздельных Sck/Mosi/Miso.
pub trait PinSpi<I> {}

pub trait Pins<I> {}
impl<I, A, B, C, D> Pins<I> for (A, B, C, D)
where
    A: PinSpi<I>,
    B: PinSpi<I>,
    C: PinSpi<I>,
    D: PinSpi<I>,
{
}

// SPI_0 -> GPIO_0 пины 0(sck)/1(mosi)/2(miso)/3(ss_in) — mik32_hal_spi.c:16,45.
impl PinSpi<mik32v2_pac::Spi0> for gpio::P16_0_0<Func2Mode> {}
impl PinSpi<mik32v2_pac::Spi0> for gpio::P16_0_1<Func2Mode> {}
impl PinSpi<mik32v2_pac::Spi0> for gpio::P16_0_2<Func2Mode> {}
impl PinSpi<mik32v2_pac::Spi0> for gpio::P16_0_3<Func2Mode> {}
// SPI_0 chip_select — cs0: GPIO_0/4, cs1: GPIO_1/14, cs2: GPIO_1/15, cs3: GPIO_2/6
// (knowledge_base/spi.json:instance_chip_select_pin, mik32_hal_spi.c:21-39).
impl PinSpi<mik32v2_pac::Spi0> for gpio::P16_0_4<Func2Mode> {}
impl PinSpi<mik32v2_pac::Spi0> for gpio::P16_1_14<Func2Mode> {}
impl PinSpi<mik32v2_pac::Spi0> for gpio::P16_1_15<Func2Mode> {}
impl PinSpi<mik32v2_pac::Spi0> for gpio::P8_2_6<Func2Mode> {}

// SPI_1 -> GPIO_1 пины 0(sck)/1(mosi)/2(miso)/3(ss_in) — mik32_hal_spi.c:57,82.
impl PinSpi<mik32v2_pac::Spi1> for gpio::P16_1_0<Func2Mode> {}
impl PinSpi<mik32v2_pac::Spi1> for gpio::P16_1_1<Func2Mode> {}
impl PinSpi<mik32v2_pac::Spi1> for gpio::P16_1_2<Func2Mode> {}
impl PinSpi<mik32v2_pac::Spi1> for gpio::P16_1_3<Func2Mode> {}
// SPI_1 chip_select — все 4 на GPIO_1 пины 4/5/6/7 (mik32_hal_spi.c:62-77).
impl PinSpi<mik32v2_pac::Spi1> for gpio::P16_1_4<Func2Mode> {}
impl PinSpi<mik32v2_pac::Spi1> for gpio::P16_1_5<Func2Mode> {}
impl PinSpi<mik32v2_pac::Spi1> for gpio::P16_1_6<Func2Mode> {}
impl PinSpi<mik32v2_pac::Spi1> for gpio::P16_1_7<Func2Mode> {}

/// CONFIG.MSTREN — SPI_HandleTypeDef.Init.SPI_Mode.
#[derive(Debug, PartialEq, Eq, Clone, Copy)]
pub enum Mode { Master, Slave }

/// CONFIG.CPHA — SPI_PHASE_OFF/SPI_PHASE_ON.
#[derive(Debug, PartialEq, Eq, Clone, Copy)]
pub enum ClkPhase { Off, On }

/// CONFIG.CPOL — SPI_POLARITY_LOW/SPI_POLARITY_HIGH.
#[derive(Debug, PartialEq, Eq, Clone, Copy)]
pub enum ClkPolarity { Low, High }

/// CONFIG.BAUD_RATE_DIV — только для Mode::Master (mik32_hal_spi.c:232-241).
/// Значения здесь совпадают с PAC-enum `BaudRateDiv` (проверено, в отличие
/// от CS — см. комментарий в начале файла), но пишутся через match на
/// собственный enum, чтобы конфигуратор не зависел от имён PAC-вариантов.
#[derive(Debug, PartialEq, Eq, Clone, Copy)]
pub enum BaudRateDiv { Div4, Div8, Div16, Div32, Div64, Div128, Div256 }

/// CONFIG.CS — только для Mode::Master. raw-значения — см. комментарий в
/// начале файла про сломанный PAC-enum.
#[derive(Debug, PartialEq, Eq, Clone, Copy)]
pub enum ChipSelect { Cs0, Cs1, Cs2, Cs3 }

impl ChipSelect {
    // mik32_hal_spi.h:22-26 — SPI_CS_0..3.
    const fn raw_bits(self) -> u8 {
        match self {
            ChipSelect::Cs0 => 0b1110,
            ChipSelect::Cs1 => 0b1101,
            ChipSelect::Cs2 => 0b1011,
            ChipSelect::Cs3 => 0b0111,
        }
    }
}

/// SPI configuration — 1:1 с SpiConfig (parser/spi/spi_parser.h) за вычетом
/// interrupts (отклонено в rust_feature_validator.cpp).
pub struct Config {
    pub mode: Mode,
    pub clk_phase: ClkPhase,
    pub clk_polarity: ClkPolarity,
    pub threshold_tx: u8, // 0..=8, mik32_hal_spi.c:270-275.
    // Читаются только при mode == Master (mik32_hal_spi.c:232-241, как и в
    // C-ветке — для slave эти поля не задаются вовсе).
    pub baud_rate_div: BaudRateDiv,
    pub decoder: bool,
    pub manual_cs: bool,
    pub chip_select: ChipSelect,
}

/// SPI peripheral wrapper. `CS` — пин ручного/автоматического выбора
/// ведомого, передаётся отдельно от `PINS` (SCK/MOSI/MISO/SS_IN), потому
/// что физически он может лежать на другом GPIO-блоке (SPI_0/cs3 -> GPIO_2,
/// knowledge_base/spi.json:instance_chip_select_pin) и нужен только при
/// Mode::Master.
pub struct Spi<I, PINS, CS = ()> {
    spi: I,
    pins: PINS,
    cs: CS,
}

// free() — один impl-блок без ограничения на CS (иначе компилятор не может
// доказать непересекаемость двух inherent-impl с одинаковым именем метода
// для разных связываний CS, даже если один из них специализирован под "()":
// E0592 "duplicate definitions with name `free`" — найдено реальной
// компиляцией cargo build, не угадано заранее).
impl<I, PINS, CS> Spi<I, PINS, CS>
where
    PINS: Pins<I>,
    I: Instance,
{
    pub fn free(self) -> (I, PINS, CS) {
        (self.spi, self.pins, self.cs)
    }
}

impl<I, PINS> Spi<I, PINS, ()>
where
    PINS: Pins<I>,
    I: Instance,
{
    /// Mode::Slave — без CS-пина (HAL_SPI_Init форсирует ManualCS=OFF и не
    /// трогает Init.ChipSelect для slave, mik32_hal_spi.c:239).
    pub fn new_slave(spi: I, pins: PINS, config: Config) -> Self {
        let pm = unsafe { &(*Pm::ptr()) };
        I::enable_clock(pm);
        spi.configure_common(&config);
        Spi { spi, pins, cs: () }
    }
}

impl<I, PINS, CS> Spi<I, PINS, CS>
where
    PINS: Pins<I>,
    CS: PinSpi<I>,
    I: Instance,
{
    /// Mode::Master — с CS-пином (всегда резервируется HAL_SPI_MspInit
    /// независимо от manual_cs, см. комментарий instance_chip_select_pin).
    pub fn new_master(spi: I, pins: PINS, cs: CS, config: Config) -> Self {
        let pm = unsafe { &(*Pm::ptr()) };
        I::enable_clock(pm);
        spi.configure_common(&config);
        spi.configure_master(&config);
        Spi { spi, pins, cs }
    }
}

/// Implemented by all SPI instances. Методы, а не единый Deref-bound — тот
/// же паттерн, что в i2c.rs (SPI_0/SPI_1 — два разных RegisterBlock-типа в
/// PAC-снэпшоте).
pub trait Instance {
    fn enable_clock(pm: &mik32v2_pac::pm::RegisterBlock);
    fn configure_common(&self, config: &Config);
    fn configure_master(&self, config: &Config);
}

macro_rules! impl_instance {
    ($(
        $SPIX:ident: $spiXfield:ident,
    )+) => {
        $(
            impl Instance for mik32v2_pac::$SPIX {
                // PM.CLK_APB_P_SET — поле названо так же, как сам инстанс
                // (spi_0/spi_1), не "...en" (проверено pm/clk_apb_p_set.rs,
                // в отличие от i2c.rs/timer-модулей, где поле — i2c_0 и т.п.
                // тоже без суффикса "en"; имя здесь оставлено параметром
                // макроса для единообразия с остальными HAL-модулями).
                fn enable_clock(pm: &mik32v2_pac::pm::RegisterBlock) {
                    pm.clk_apb_p_set().modify(|_, w| w.$spiXfield().set_bit());
                }

                // Общие для master/slave поля CONFIG (mik32_hal_spi.c:
                // 244-249, 270-275) + ENABLE (HAL_SPI_Enable, вызываемый
                // из HAL_SPI_Init — mik32_hal_spi.c, строка в конце
                // HAL_SPI_Init перед return).
                fn configure_common(&self, config: &Config) {
                    self.config().modify(|_, w| {
                        let w = match config.mode {
                            Mode::Master => w.mode_sel().master(),
                            Mode::Slave => w.mode_sel().slave(),
                        };
                        let w = match config.clk_polarity {
                            ClkPolarity::Low => w.clk_pol()._0(),
                            ClkPolarity::High => w.clk_pol()._1(),
                        };
                        match config.clk_phase {
                            ClkPhase::Off => w.clk_ph()._0(),
                            ClkPhase::On => w.clk_ph()._1(),
                        }
                    });
                    // Threshold_of_TX_FIFO — поле шире u8 в PAC (FI::Ux = u32,
                    // tx_thr.rs), хотя реальный диапазон 0..=8 (mik32_hal_spi.c:
                    // 270-275) — приведение типа, не изменение значения.
                    self.tx_thr().write(|w| unsafe { w.threshold_of_tx_fifo().bits(config.threshold_tx as u32) });
                    // HAL_SPI_Enable (mik32_hal_spi.c): SPI_EN=1 после Init.
                    self.enable().modify(|_, w| w.spi_en().enable());
                }

                // Только Mode::Master (mik32_hal_spi.c:232-241): BaudRateDiv/
                // Decoder/ManualCS/ChipSelect.
                fn configure_master(&self, config: &Config) {
                    self.config().modify(|_, w| {
                        let w = match config.baud_rate_div {
                            BaudRateDiv::Div4 => w.baud_rate_div().div4(),
                            BaudRateDiv::Div8 => w.baud_rate_div().div8(),
                            BaudRateDiv::Div16 => w.baud_rate_div().div16(),
                            BaudRateDiv::Div32 => w.baud_rate_div().div32(),
                            BaudRateDiv::Div64 => w.baud_rate_div().div64(),
                            BaudRateDiv::Div128 => w.baud_rate_div().div128(),
                            BaudRateDiv::Div256 => w.baud_rate_div().div256(),
                        };
                        let w = if config.manual_cs { w.manual_cs().manual() } else { w.manual_cs().automatic() };
                        // CS — raw-биты, см. комментарий в начале файла
                        // (PAC-enum Cs расходится с mik32_hal_spi.h).
                        unsafe { w.cs().bits(config.chip_select.raw_bits()) }
                    });
                    // Decoder (PERI_SEL, бит 9 регистра CONFIG — mik32_sdk_
                    // snapshot/shared/periphery/spi.h:29-30) не экспонирован
                    // как named-поле PAC, пишется raw-битом всего регистра.
                    const PERI_SEL_BIT: u32 = 1 << 9;
                    self.config().modify(|r, w| unsafe {
                        let raw = if config.decoder { r.bits() | PERI_SEL_BIT } else { r.bits() & !PERI_SEL_BIT };
                        w.bits(raw)
                    });
                }
            }
        )+
    }
}

impl_instance! {
    Spi0: spi_0,
    Spi1: spi_1,
}
