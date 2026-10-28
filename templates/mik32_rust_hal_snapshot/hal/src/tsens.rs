//! TSENS (датчик температуры) HAL-обёртка — Rust-паритет с
//! codegen/project/tsens_contribution.cpp (C-ветка): TSENS — единственный
//! глобальный блок (ANALOG_REG, mik32_hal_tsens.h), как CRC/RTC.
//!
//! Объём идентичен C-ветке: только настройка тактирования/порогов/
//! прерываний. HAL_TSENS_SingleStart/ContinuousStart (когда и как часто
//! измерять) остаются за пользователем — тот же принцип "только
//! конфигурация", что у WDT/ADC/DAC (см. schemas/tsens.json,
//! "_comment_no_auto_start"). EPIC-маскирование и подключение к
//! trap_handler для interrupts остаются на стороне пользователя, как и у
//! RTC/SPI в этой же Rust-ветке (docs/implementations/rust/rtc.md) —
//! `interrupts` здесь не отклоняется.
//!
//! `div`/`low_threshold_raw`/`high_threshold_raw` — НЕ вычисляются в
//! рантайме (делитель частоты термосенсора ищется подбором в
//! HAL_TSENS_ClockDivider/HAL_TSENS_Clock, mik32_hal_tsens.c, — алгоритм
//! переносим на этапе кодогена, не в HAL, по тому же принципу, что TIMINGR
//! у I2C), а приходят уже готовыми raw-значениями, посчитанными кодогеном
//! (`rust_project_codegen.cpp`) той же формулой, что в установленном HAL.

use mik32v2_pac::Pm;
pub use mik32v2_pac::tsens::tsens_cfg::ClkMux;

#[derive(Debug, Clone, Copy)]
pub struct Config {
    pub clk_mux: ClkMux,
    /// Регистровое значение делителя (TSENS_CFG.DIV, 0..=1023) — посчитано
    /// кодогеном по алгоритму HAL_TSENS_Clock, не угадано.
    pub div: u16,
    /// Регистровое значение порога (TSENS_TRESHOLD.TRESHOLD_LOW, 225..=603) —
    /// посчитано кодогеном формулой TSENS_CELSIUS_TO_VALUE (analog_reg.h).
    pub low_threshold_raw: Option<u16>,
    pub high_threshold_raw: Option<u16>,
    pub enable_low_irq: bool,
    pub enable_high_irq: bool,
    pub enable_eoc_irq: bool,
}

pub struct Tsens {
    tsens: mik32v2_pac::Tsens,
}

impl Tsens {
    /// HAL_TSENS_MspInit (clock) + HAL_TSENS_Init: включение/тактирование/
    /// снятие сброса сенсора (NPD/NPD_CLK/NRST), выбор CLK_MUX и DIV —
    /// порядок как в mik32_hal_tsens.c. Пороги/прерывания — опционально,
    /// как SetLowThreshold/SetHiThreshold/__HAL_TSENS_IRQ_ENABLE_*.
    pub fn new(tsens: mik32v2_pac::Tsens, pm: &Pm, config: Config) -> Self {
        pm.clk_apb_p_set().modify(|_, w| w.analog_regs().set_bit());

        tsens.tsens_cfg().write(|w| {
            w.npd().enable()
                .npd_clk().enable()
                .nrst().set_bit()
                .clk_mux().variant(config.clk_mux)
        });
        tsens.tsens_cfg().modify(|_, w| unsafe { w.div().bits(config.div) });

        if let Some(low) = config.low_threshold_raw {
            tsens.tsens_treshold().modify(|_, w| unsafe { w.treshold_low().bits(low) });
        }
        if let Some(high) = config.high_threshold_raw {
            tsens.tsens_treshold().modify(|_, w| unsafe { w.treshold_hi().bits(high) });
        }

        if config.enable_low_irq {
            tsens.tsens_irq().modify(|_, w| w.low_mask().set_bit());
        }
        if config.enable_high_irq {
            tsens.tsens_irq().modify(|_, w| w.hi_mask().set_bit());
        }
        if config.enable_eoc_irq {
            tsens.tsens_irq().modify(|_, w| w.eoc_mask().set_bit());
        }

        Self { tsens }
    }

    /// HAL_TSENS_GetTemperature — сырое значение TSENS_VALUE, перевод в
    /// градусы Цельсия (формула TSENS_VALUE_TO_CELSIUS, *100) оставлен
    /// пользователю, как и сам момент измерения (Single/ContinuousStart).
    pub fn read_value_raw(&self) -> u16 {
        self.tsens.tsens_value().read().value().bits()
    }
}
