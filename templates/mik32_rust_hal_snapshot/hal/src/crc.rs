//! CRC HAL-обёртка — Rust-паритет с codegen/project/crc_contribution.cpp
//! (C-ветка): CRC — единственный глобальный блок (mik32_memory_map.h,
//! CRC_BASE_ADDRESS), без instance, как RTC.
//!
//! Объём идентичен C-ветке: `new()` настраивает только алгоритм
//! (Poly/InputReverse/OutputReverse/OutputInversion — точная копия порядка
//! вызовов `HAL_CRC_Init`, сверено с mik32_hal_crc32.c). `HAL_CRC_WriteData`/
//! `WriteData32`/`ReadCRC` на собственных данных пользователя этот кодоген
//! не может угадать наперёд — оставлены методами `write_data`/`write_data32`/
//! `read_crc`, вызываются пользователем точно как HAL-аналоги в C (включая
//! автоматическую запись `Init` перед каждым вызовом `write_data*` — так
//! делает `HAL_CRC_WriteData`/`WriteData32`, не `HAL_CRC_Init`).
//!
//! `input_reverse`/`output_reverse` — не прямые enum-варианты CTRL.TOT/TOTR
//! (4 состояния: None/Bits/BitsBytes/Bytes), а документированная HAL-пара
//! (CRC_REFIN_FALSE=CRC_REVERSE_BYTES=3, CRC_REFIN_TRUE=CRC_REVERSE_BITS_BYTES=2;
//! CRC_REFOUT_FALSE=CRC_REVERSE_OFF=0, CRC_REFOUT_TRUE=CRC_REVERSE_BITS_BYTES=2),
//! сверено построчно с mik32_hal_crc32.h строки 36-59 — не угадано.

use mik32v2_pac::Pm;
use mik32v2_pac::crc::ctrl::{Fxor, Tot, Totr};

#[derive(Debug, Clone, Copy)]
pub struct Config {
    pub poly: u32,
    pub init: u32,
    pub input_reverse: bool,
    pub output_reverse: bool,
    pub output_inversion: bool,
}

pub struct Crc {
    crc: mik32v2_pac::Crc,
    init: u32,
}

impl Crc {
    /// HAL_CRC32_MspInit (clock) + SetInputReverse/SetOutputReverse/
    /// SetOutputInversion/SetPoly — порядок и состав точно как
    /// HAL_CRC_Init (mik32_hal_crc32.c). Init не пишется здесь — он
    /// применяется только перед вычислением (write_data*), как в C.
    pub fn new(crc: mik32v2_pac::Crc, pm: &Pm, config: Config) -> Self {
        pm.clk_ahb_set().modify(|_, w| w.crc32().set_bit());

        let tot = if config.input_reverse { Tot::BitsBytes } else { Tot::Bytes };
        let totr = if config.output_reverse { Totr::BitsBytes } else { Totr::None };
        let fxor = if config.output_inversion { Fxor::InversionEnable } else { Fxor::InversionDisable };
        crc.ctrl().modify(|_, w| w.tot().variant(tot).totr().variant(totr).fxor().variant(fxor));
        crc.poly().write(|w| unsafe { w.bits(config.poly) });

        Self { crc, init: config.init }
    }

    fn set_init(&self) {
        self.crc.ctrl().modify(|_, w| w.was().init_data());
        self.crc.data().write(|w| unsafe { w.bits(self.init) });
        self.crc.ctrl().modify(|_, w| w.was().data());
    }

    fn wait_busy(&self) {
        while self.crc.ctrl().read().busy().is_busy() {}
    }

    /// HAL_CRC_WriteData — записывает Init, затем байты message (без вызова
    /// read_crc, как в C: ReadCRC делается отдельным вызовом пользователем).
    pub fn write_data(&self, message: &[u8]) {
        self.set_init();
        for &byte in message {
            self.crc.data().write(|w| unsafe { w.bits(byte as u32) });
        }
    }

    /// HAL_CRC_WriteData32.
    pub fn write_data32(&self, message: &[u32]) {
        self.set_init();
        for &word in message {
            self.crc.data().write(|w| unsafe { w.bits(word) });
        }
    }

    /// HAL_CRC_ReadCRC — ждёт BUSY=0, возвращает DATA.
    pub fn read_crc(&self) -> u32 {
        self.wait_busy();
        self.crc.data().read().bits()
    }
}
