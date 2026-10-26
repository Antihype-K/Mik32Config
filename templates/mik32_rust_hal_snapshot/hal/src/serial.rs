use core::{error::Error, fmt, marker::PhantomData, ops::Deref};
use mik32v2_pac::{crypto::config, usart_0::flags, Pm, Usart0, Usart1};
use embedded_hal_nb::serial::{ErrorKind, ErrorType, Read, Write};
use nb::block;
use rcc::Config as RccConfig;
use core::ptr;

use crate::{gpio::{self, Func2Mode}, rcc};

pub trait Pins<U> {}
pub trait PinTx<U> {}
pub trait PinRx<U> {}

impl<U, TX, RX> Pins<U> for (TX, RX)
where
    TX: PinTx<U>,
    RX: PinRx<U>,
{
}


impl PinTx<Usart0> for gpio::P16_0_6<Func2Mode> {}
impl PinRx<Usart0> for gpio::P16_0_5<Func2Mode> {}

impl PinTx<Usart1> for gpio::P16_1_9<Func2Mode> {}
impl PinRx<Usart1> for gpio::P16_1_8<Func2Mode> {}



/// Serial abstraction
pub struct Serial<U, PINS> {
    usart: U,
    pins: PINS,
}

impl<U, PINS> Serial<U, PINS>
where
    PINS: Pins<U>,
    U: Instance,
{
    pub fn new(usart: U, pins: PINS, config: Config) -> Self {
        let pm = unsafe { &(*Pm::ptr()) };
        U::enable_clock(&pm);

        // ТЗ §3.1.8 / knowledge_base/uart.json ("hsi_hz"): HSI 32МГц — единственный
        // источник тактирования конфигуратора; тот же делитель, что уже
        // подтверждён на железе в C-ветке (codegen/project/uart_contribution.cpp:76,
        // `divider = knowledge.hsi_hz / config.baud_rate`). Заменяет исходный
        // mik32-hal-main/src/serial.rs:48 (`w.brr().bits(0xd05)` — авторский
        // TODO "Calculate correct baudrate divisor on the fly" так и остался
        // нереализованным в апстриме, см. docs/implementations/rust_codegen.md).
        const HSI_HZ: u32 = 32_000_000;
        let divider = (HSI_HZ / config.baud_rate) as u16;
        usart.divider().modify(|_, w| unsafe { w.brr().bits(divider) });

        // Длина кадра (M1:M0) и контроль чётности (PCE/PS) можно менять только
        // при UE=0 (mik32v2-pac/src/usart_0/control1.rs, докомментарии полей
        // `m`/`ps`) — UE включается ниже отдельным вызовом, уже после этого.
        // Битовая раскладка — mik32_hal_usart.c:88-92 (framework-mik32v2-sdk) +
        // knowledge_base/uart.json (data_bits_encoding/parity_encoding,
        // Датащит МИК32 v2.2.3, Таблица 18 «Регистр управления 1 CONTROL1»).
        usart.control1().modify(|_, w| {
            let w = match config.data_bits {
                DataBits::Seven => w.m()._7bits(),
                DataBits::Eight => w.m()._8bits(),
                DataBits::Nine => w.m()._9bits(),
            };
            let w = match config.parity {
                Parity::None => w.pce().disable(),
                Parity::Even => w.pce().enable().ps().parity(),
                Parity::Odd => w.pce().enable().ps().odd(),
            };
            let w = if config.transmitting { w.te().enable() } else { w.te().disable() };
            if config.receiving { w.re().enable() } else { w.re().disable() }
        });

        // Количество стоп-бит — тоже требует UE=0 (control2.rs, поле `stop_1`).
        // mik32_hal_usart.c:108 + knowledge_base/uart.json (stop_bits_encoding).
        usart.control2().modify(|_, w| match config.stop_bits {
            StopBits::One => w.stop_1()._1bit(),
            StopBits::Two => w.stop_1()._2bits(),
        });

        // Расширение паритета с C-веткой (OVERNIGHT_FIELD_REFERENCE.md,
        // codegen/project/uart_contribution.cpp:75-91): очередность бит,
        // инверсии TX/RX, swap TX<->RX — все четыре бита физически в
        // CONTROL2 (control2.rs: msbfirst bit19, txinv bit17, rxinv bit16,
        // swap bit15) и требуют UE=0, как и прочие поля этого регистра.
        usart.control2().modify(|_, w| {
            let w = if config.bit_direction == BitDirection::Msb { w.msbfirst().msb() } else { w.msbfirst().lsb() };
            let w = if config.tx_inversion { w.txinv().inverted() } else { w.txinv().direct() };
            let w = if config.rx_inversion { w.rxinv().inverted() } else { w.rxinv().direct() };
            if config.swap { w.swap().swap() } else { w.swap().normal() }
        });

        // Синхронный режим (CLKEN) + полярность/фаза тактового сигнала CK
        // (CPOL/CPHA) + "последний такт" (LBCL) — ТОЧНО та же битовая
        // раскладка xck_mode->CPOL/CPHA, что уже подтверждена на железе в
        // C-ветке: templates/mik32_sdk_snapshot/hal/peripherals/Source/mik32_hal_usart.c:109-111
        // (`if (xck_mode & 0b10) CPOL; if (xck_mode & 0b01) CPHA;`).
        // Запрет mode=Synchronous на instance=1 (Errata §10.1) проверяется
        // раньше, в rust_feature_validator.cpp — не здесь.
        usart.control2().modify(|_, w| {
            let w = if config.mode == Mode::Synchronous { w.clken().synchronous() } else { w.clken().asynchronous() };
            let xck = config.xck_mode as u8;
            let w = if xck & 0b10 != 0 { w.cpol()._1() } else { w.cpol()._0() };
            let w = if xck & 0b01 != 0 { w.cpha()._1() } else { w.cpha()._0() };
            if config.last_byte_clock { w.lbcl()._1() } else { w.lbcl()._0() }
        });

        // Полудуплекс (HDSEL) и аппаратное управление RTS/CTS (RTSE/CTSE) —
        // CONTROL3 (control3.rs: hdsel bit3, rtse bit8, ctse bit9), тоже
        // требует UE=0. flow_control=true включает оба бита сразу (как и
        // C-ветка — Modem.rts/Modem.cts выставляются одним флагом
        // flow_control, uart_contribution.cpp:67,145-146): CTS выключает
        // передачу при неактивном входе, RTS не даёт собеседнику слать,
        // когда приёмник не готов.
        usart.control3().modify(|_, w| {
            let w = if config.channel_mode == ChannelMode::HalfDuplex { w.hdsel().half_duplex() } else { w.hdsel().duplex() };
            let w = if config.flow_control { w.rtse()._1() } else { w.rtse()._0() };
            if config.flow_control { w.ctse().control() } else { w.ctse().ignored() }
        });

        // Enable USART (сбрасывает флаги, разрешает работу с уже
        // сконфигурированными выше CONTROL1/CONTROL2/CONTROL3).
        usart.control1().modify(|_, w| w.ue().enable());

        // TEACK взводится только после реального включения передатчика —
        // ждать его при transmitting=false означало бы зависнуть здесь
        // навсегда (в исходном mik32-hal-main TE включался безусловно,
        // поэтому апстрим этого случая не видел).
        if config.transmitting {
            while usart.flags().read().teack().bit_is_clear() {};
        }

        Serial { usart, pins }
    }

    pub fn split(self) -> (Tx<U>, Rx<U>) {
        (
            Tx {
                _usart: PhantomData,
            },
            Rx {
                _usart: PhantomData,
            },
        )
    }

    pub fn release(self) -> (U, PINS) {
        (self.usart, self.pins)
    }
}

/// Количество бит данных в кадре — Датащит Табл.18, поле CONTROL1.M1:M0;
/// то же кодирование, что knowledge_base/uart.json:data_bits_encoding
/// (C-ветка конфигуратора, уже подтверждено на железе).
#[derive(Debug, PartialEq, Eq, Clone, Copy)]
pub enum DataBits { Seven, Eight, Nine }

/// CONTROL2.STOP_1 — Датащит Табл.18 / knowledge_base/uart.json:stop_bits_encoding.
#[derive(Debug, PartialEq, Eq, Clone, Copy)]
pub enum StopBits { One, Two }

/// CONTROL1.PCE/PS — Датащит Табл.18 / knowledge_base/uart.json:parity_encoding.
#[derive(Debug, PartialEq, Eq, Clone, Copy)]
pub enum Parity { None, Even, Odd }

/// CONTROL2.MSBFIRST — очередность передачи/приёма бит. Датащит + control2.rs
/// bit19; соответствует bit_direction в OVERNIGHT_FIELD_REFERENCE.md /
/// uart_contribution.cpp:75 (msb_first/lsb_first -> MSB_First/LSB_First).
#[derive(Debug, PartialEq, Eq, Clone, Copy)]
pub enum BitDirection { Msb, Lsb }

/// CONTROL2.CLKEN — синхронный/асинхронный режим. control2.rs bit11.
/// Errata MIK32V2 v1.0.1 §10.1 запрещает Synchronous на instance=1 — этот
/// запрет проверяется в rust_feature_validator.cpp (как и в C-ветке —
/// parser/USART/usart_parser.cpp), не в самом HAL.
#[derive(Debug, PartialEq, Eq, Clone, Copy)]
pub enum Mode { Asynchronous, Synchronous }

/// CONTROL2.CPOL/CPHA — комбинация полярности/фазы тактового сигнала CK,
/// используется только при Mode::Synchronous. Биты полностью совпадают с
/// C-веткой: templates/mik32_sdk_snapshot/hal/peripherals/Source/mik32_hal_usart.c:109-111
/// (xck_mode — 2-битное значение, bit1->CPOL, bit0->CPHA).
#[derive(Debug, PartialEq, Eq, Clone, Copy)]
pub enum XckMode { Mode0 = 0b00, Mode1 = 0b01, Mode2 = 0b10, Mode3 = 0b11 }

/// CONTROL3.HDSEL — дуплекс/полудуплекс. control3.rs bit3.
#[derive(Debug, PartialEq, Eq, Clone, Copy)]
pub enum ChannelMode { Duplex, HalfDuplex }

/// USART configuration — расширено конфигуратором при генерации Rust-кода
/// относительно исходного mik32-hal-main, где `Config` был пустой struct {}
/// и `Serial::new` жёстко писал BRR=0xd05 и TE/RE=1 безусловно, игнорируя
/// любые пользовательские настройки (см. docs/implementations/rust_codegen.md).
pub struct Config {
    pub baud_rate: u32,
    pub data_bits: DataBits,
    pub stop_bits: StopBits,
    pub parity: Parity,
    pub transmitting: bool,
    pub receiving: bool,
    // Поля ниже — расширение паритета с C-веткой (uart_contribution.cpp),
    // см. комментарии у соответствующих регистровых записей выше.
    pub bit_direction: BitDirection,
    pub mode: Mode,
    pub xck_mode: XckMode,
    pub last_byte_clock: bool,
    pub channel_mode: ChannelMode,
    pub tx_inversion: bool,
    pub rx_inversion: bool,
    pub swap: bool,
    pub flow_control: bool,
}

/// Serial receiver
pub struct Rx<U> {
    _usart: PhantomData<U>,
}

impl<U> ErrorType for Rx<U>
where
    U: Instance,
{
    type Error = ErrorKind;
}

impl<U> Read<u8> for Rx<U>
where
    U: Instance,
{
    fn read(&mut self) -> nb::Result<u8, Self::Error> {
        // NOTE(unsafe) atomic read with no side effects
        let flags = unsafe { (*U::ptr()).flags() };

        if flags.read().pe().bit_is_set() {
            flags.write(|w| w.pe().clear_bit_by_one());
            return Err(nb::Error::Other(ErrorKind::Parity));
        }
        if flags.read().fe().bit_is_set() {
            flags.write(|w| w.fe().clear_bit_by_one());
            return Err(nb::Error::Other(ErrorKind::FrameFormat));
        }
        if flags.read().nf().bit_is_set() {
            flags.write(|w| w.nf().clear_bit_by_one());
            return Err(nb::Error::Other(ErrorKind::Noise));
        }
        if flags.read().ore().bit_is_set() {
            flags.write(|w| w.ore().clear_bit_by_one());
            return Err(nb::Error::Other(ErrorKind::Overrun));
        }

        if flags.read().rxne().bit_is_set() {
            // NOTE(unsafe): Atomic read with no side effects
            return Ok(unsafe {
                // Casting to `u8` should be fine, as we've configured the USART
                // to use 8 data bits.
                (*U::ptr()).rxdata().read().rdr().bits() as u8
            });
        }

        Err(nb::Error::WouldBlock)
    }
}

/// Serial transmitter
pub struct Tx<U> {
    _usart: PhantomData<U>,
}

impl<U> ErrorType for Tx<U>
where
    U: Instance,
{
    type Error = ErrorKind;
}

impl<U> Write<u8> for Tx<U>
where
    U: Instance,
{
    fn flush(&mut self) -> nb::Result<(), Self::Error> {
        // NOTE(unsafe) atomic read with no side effects
        let flags = unsafe { (*U::ptr()).flags().read() };

        if flags.tc().bit_is_set() {
            Ok(())
        } else {
            Err(nb::Error::WouldBlock)
        }
    }

    fn write(&mut self, byte: u8) -> nb::Result<(), Self::Error> {
        unsafe {
            (*U::ptr()).txdata().write(|w| w.tdr().bits(byte as u16));
            while (*U::ptr()).flags().read().tc().bit_is_clear() {};
        }
        Ok(())
    }
}

/// Implemented by all USART instances
pub trait Instance: Deref<Target = mik32v2_pac::usart_0::RegisterBlock> {
    fn ptr() -> *const mik32v2_pac::usart_0::RegisterBlock;
    fn enable_clock(pm: &mik32v2_pac::pm::RegisterBlock);
}

macro_rules! impl_instance {
    ($(
        $USARTX:ident: ($usartX:ident, $usartXen:ident),
    )+) => {
        $(
            impl Instance for $USARTX {
                fn ptr() -> *const mik32v2_pac::usart_0::RegisterBlock {
                    $USARTX::ptr()
                }

                fn enable_clock(pm: &mik32v2_pac::pm::RegisterBlock) {
                    pm.clk_apb_p_set().modify(|_, w| w.$usartXen().set_bit());
                }
            }
        )+
    }
}

impl_instance! {
    Usart0: (usart0, uart_0),
    Usart1: (usart1, uart_1),
}

impl<U> fmt::Write for Tx<U>
where
    Tx<U>: embedded_hal_nb::serial::Write<u8>,
{
    fn write_str(&mut self, s: &str) -> fmt::Result {
        let _ = s.as_bytes().iter().map(|c| block!(self.write(*c))).last();
        Ok(())
    }
}