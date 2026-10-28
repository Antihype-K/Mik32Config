//! Timer32 HAL-обёртка — Rust-паритет с codegen/project/timer32_contribution.cpp
//! (C-ветка): count_mode (forward/reverse/bidirectional), 6 clock_source,
//! prescaler/top как сырые числа регистра (НЕ индекс enum'а, в отличие от
//! Timer16 — mik32_hal_timer32.h, TIMER32_ClockConfigTypeDef.Prescaler:
//! "Частота делится на Prescaler + 1"), каналы 0-3 (pwm/capture/compare)
//! ТОЛЬКО для Timer32_1/Timer32_2 (Timer32_0 — только базовый счётчик, та же
//! граница, что в C-ветке), duty_percent считается в кодогене в ocr (как и в
//! C), overflow_interrupt/underflow_interrupt.
//!
//! Errata MIK32V2 v1.0.1, §9.1 — обязательный перенос (см. docs/open_tasks.md):
//! сброс флагов IC_INT_CHx/OC_INT_CHx/UDF_INT/OVF_INT происходит в конце
//! счёта таймера, во время которого была запись в INT_CLEAR — возможен
//! повторный вход в обработчик при низкой частоте счёта. `Instance::
//! clear_single_interrupt_flag`/`set_interrupt_mask`/`clear_interrupt_mask`
//! дают генератору ровно те три примитива, которыми в C-ветке собирается
//! обход (отключить маску сработавшего источника, сбросить ТОЛЬКО его флаг,
//! повторно включить из основного цикла — см. rust_project_codegen.cpp).
//!
//! **ВАЖНО, расхождение PAC-комментариев и реального HAL** (shared/periphery/
//! timer32.h, mik32_hal_timer32.h — выигрывает реальный HAL, см. корневой
//! CLAUDE.md "Datasheet/errata priority"): названные варианты PAC-enum'ов
//! для CONTROL.SOURSE (Tim1=0/Tim2=2/Tim3=3), CH{n}_CNTR.NOISE
//! (Enable=0/Disable=1) и CH{n}_CNTR.PWM_INV (Direct=1/Inverted=0) НЕ
//! соответствуют реальным битовым маскам (TIMER32_CONTROL_CLOCK_*_M:
//! PRESCALER=0/TIM1=1/TX_PIN=2/TIM2=3; TIMER32_CHANNEL_FILTER_OFF=0/ON=1;
//! TIMER32_CHANNEL_NON_INVERTED_PWM=0/INVERTED=1) — эти три поля пишутся
//! здесь СЫРЫМИ битами (`.bits()`), а не именованными методами PAC, чтобы не
//! унаследовать путаницу автогенератора SVD. COUNT_MODE, MODE и EDGE,
//! наоборот, у PAC совпадают с реальным HAL побитово — для них используются
//! именованные методы.

use mik32v2_pac::Pm;

/// HAL_TIMER32_SourceTypeDef (mik32_hal_timer32.h:27-34).
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum ClockSource {
    Prescaler,
    Tim1SysClk,
    Tim1Hclk,
    Tim2Osc32k,
    Tim2Lsi32k,
    TxPad,
}

/// HAL_TIMER32_CountModeTypeDef (mik32_hal_timer32.h:37-41).
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum CountMode {
    Forward,
    Reverse,
    Bidirectional,
}

/// HAL_TIMER32_CHANNEL_ModeTypeDef (mik32_hal_timer32.h:61-66).
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum ChannelMode {
    Compare,
    Capture,
    Pwm,
}

/// HAL_TIMER32_CHANNEL_CaptureEdgeTypeDef (mik32_hal_timer32.h:68-72).
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum CaptureEdge {
    Rising,
    Falling,
}

/// 1:1 с Timer32ChannelConfig (parser/timer32/timer32_parser.h:13-28) после
/// того, как кодоген уже посчитал duty_percent -> ocr.
#[derive(Debug, Clone, Copy)]
pub struct ChannelConfig {
    pub mode: ChannelMode,
    pub ocr: u32,
    pub pwm_invert: bool,
    pub capture_edge: CaptureEdge,
    pub noise_filter: bool,
}

pub struct Timer32<I> {
    timer: I,
}

impl<I: Instance> Timer32<I> {
    /// HAL_Timer32_Init (ClockInit->Source_Set, Prescaler_Set, Top_Set,
    /// CountMode_Set, mik32_hal_timer32.c:67-99) — НЕ запускает счёт (как и
    /// HAL_Timer32_Init в C): для instance без каналов вызывающий код должен
    /// вызвать `start()` явно, для instance с каналами — сначала
    /// `configure_channel()` на все каналы, затем `start()` (тот же порядок,
    /// что timer32_contribution.cpp: Channel_Init для всех каналов, потом
    /// единственный HAL_Timer32_Start внутри последнего *_Start_IT).
    pub fn new(
        timer: I,
        clock_source: ClockSource,
        count_mode: CountMode,
        prescaler: u32,
        top: u32,
    ) -> Self {
        let pm = unsafe { &(*Pm::ptr()) };
        I::enable_clock(pm);

        timer.set_clock_source(pm, clock_source);
        timer.set_count_mode(count_mode);
        timer.set_prescale(prescaler);
        timer.set_top(top);

        Timer32 { timer }
    }

    /// HAL_Timer32_Start/_Base_Start_IT (mik32_hal_timer32.c:364-371,
    /// 376-382). AUDIT_TZ_PERIPHERY_GAPS.md, Timer32: overflow_interrupt/
    /// underflow_interrupt — общие флаги, независимые от каналов.
    pub fn start(&self, overflow_interrupt: bool, underflow_interrupt: bool) {
        let mut mask = 0u32;
        if overflow_interrupt {
            mask |= 1 << 0; // TIMER32_INT_OVERFLOW_M
        }
        if underflow_interrupt {
            mask |= 1 << 1; // TIMER32_INT_UNDERFLOW_M
        }
        if mask != 0 {
            self.timer.set_interrupt_mask(mask);
        }
        self.timer.start();
    }

    pub fn free(self) -> I {
        self.timer
    }
}

impl<I: InstanceWithChannels> Timer32<I> {
    /// HAL_Timer32_Channel_Init + HAL_Timer32_Channel_Enable +
    /// HAL_Timer32_{PWM,Compare,Capture}_Start_IT без финального
    /// HAL_Timer32_Start (вынесен в `Timer32::start()` — счёт должен
    /// запускаться один раз после настройки ВСЕХ каналов, повторный вызов
    /// в C безвреден за счёт идемпотентности `ENABLE |= TIM_EN_M`, но здесь
    /// сделан явным одним вызовом). Включает тот же бит маски прерывания,
    /// что соответствующий *_Start_IT в C-ветке (pwm делит
    /// TIMER32_INT_OVERFLOW_M с overflow_interrupt — см. комментарий
    /// timer32_contribution.cpp:67-72; повторное включение безвредно,
    /// HAL_Timer32_InterruptMask_Set всегда `|=`).
    pub fn configure_channel(&self, index: u8, config: ChannelConfig) {
        assert!(index < 4, "Timer32 channel index must be 0..=3");
        self.timer.configure_channel(index, &config);

        let channel_mask: u32 = match config.mode {
            ChannelMode::Pwm => 1 << 0, // TIMER32_INT_OVERFLOW_M (HAL_Timer32_PWM_Start_IT)
            ChannelMode::Compare => (1u32 << index) << 6, // TIMER32_INT_OC_M(index)
            ChannelMode::Capture => (1u32 << index) << 2, // TIMER32_INT_IC_M(index)
        };
        self.timer.set_interrupt_mask(channel_mask);
    }
}

/// Реализуется всеми тремя инстансами (как Timer16_0/1/2 — разные Rust-типы,
/// не единый Deref-bound).
pub trait Instance {
    fn enable_clock(pm: &mik32v2_pac::pm::RegisterBlock);
    fn set_clock_source(&self, pm: &mik32v2_pac::pm::RegisterBlock, source: ClockSource);
    fn set_count_mode(&self, mode: CountMode);
    fn set_prescale(&self, value: u32);
    fn set_top(&self, value: u32);
    fn start(&self);
    /// Errata §9.1: HAL_Timer32_InterruptMask_Set — `|=`, безопасно звать
    /// несколько раз подряд.
    fn set_interrupt_mask(&self, mask: u32);
    /// Errata §9.1: отключить маску ТОЛЬКО сработавшего источника
    /// (HAL_Timer32_InterruptMask_Clear).
    fn clear_interrupt_mask(&self, mask: u32);
    /// Errata §9.1: HAL_Timer32_InterruptFlags_Get — читать статус ДО сброса.
    fn interrupt_flags(&self) -> u32;
    /// Errata §9.1: HAL_Timer32_InterruptFlags_ClearMask — сбросить ТОЛЬКО
    /// флаг сработавшего источника, не все разом.
    fn clear_interrupt_flags(&self, mask: u32);
}

/// Timer32_1/Timer32_2 — ТЗ §3.1.5 п.2: у Timer32_0 каналов нет вовсе
/// (PAC: только templates/.../timer32_0.rs, без ch1_cntr и т.п.).
pub trait InstanceWithChannels: Instance {
    fn configure_channel(&self, index: u8, config: &ChannelConfig);
}

macro_rules! impl_instance_base {
    ($(
        $TimerX:ident: $enable_clock:expr, $mux_tim1:ident, $mux_tim2:ident,
    )+) => {
        $(
            impl Instance for mik32v2_pac::$TimerX {
                fn enable_clock(pm: &mik32v2_pac::pm::RegisterBlock) {
                    $enable_clock(pm);
                }

                // HAL_Timer32_Source_Set (mik32_hal_timer32.c:113-173):
                // PM.TIMER_CFG.MUX_TIM32_x_TIM1/TIM2 (только для
                // tim1_sys_clk/tim1_hclk/tim2_osc32k/tim2_lsi32k — эти четыре
                // мультиплексируются, prescaler/tx_pad — нет), затем
                // CONTROL.CLOCK (биты 2:3) — сырыми битами, см. комментарий
                // в начале файла про расхождение PAC-enum с реальным HAL.
                fn set_clock_source(&self, pm: &mik32v2_pac::pm::RegisterBlock, source: ClockSource) {
                    let control_clock_bits: u32 = match source {
                        ClockSource::Prescaler => 0b00,
                        ClockSource::Tim1SysClk | ClockSource::Tim1Hclk => 0b01,
                        ClockSource::TxPad => 0b10,
                        ClockSource::Tim2Osc32k | ClockSource::Tim2Lsi32k => 0b11,
                    };

                    match source {
                        ClockSource::Tim1SysClk => {
                            pm.timer_cfg().modify(|_, w| w.$mux_tim1().sys_clk());
                        }
                        ClockSource::Tim1Hclk => {
                            pm.timer_cfg().modify(|_, w| w.$mux_tim1().hclk());
                        }
                        ClockSource::Tim2Osc32k => {
                            pm.timer_cfg().modify(|_, w| w.$mux_tim2().osc32k());
                        }
                        ClockSource::Tim2Lsi32k => {
                            pm.timer_cfg().modify(|_, w| w.$mux_tim2().lsi32k());
                        }
                        ClockSource::Prescaler | ClockSource::TxPad => {}
                    }

                    self.control().modify(|r, w| unsafe {
                        w.bits((r.bits() & !(0b11 << 2)) | (control_clock_bits << 2))
                    });
                }

                // HAL_Timer32_CountMode_Set (mik32_hal_timer32.c:187-205) —
                // CONTROL биты 0:1, именованные методы PAC здесь совпадают
                // с реальным HAL (TIMER32_CONTROL_MODE_UP/DOWN/BIDIR_M).
                fn set_count_mode(&self, mode: CountMode) {
                    self.control().modify(|_, w| match mode {
                        CountMode::Forward => w.count_mode().direct(),
                        CountMode::Reverse => w.count_mode().reverse(),
                        CountMode::Bidirectional => w.count_mode().bidirectional(),
                    });
                }

                // HAL_Timer32_Prescaler_Set/Top_Set — прямая запись числа,
                // не индекс enum'а (mik32_hal_timer32.h:93-94, см. комментарий
                // в начале файла).
                fn set_prescale(&self, value: u32) {
                    self.prescale().write(|w| unsafe { w.tim_prescale().bits(value) });
                }
                fn set_top(&self, value: u32) {
                    self.top().write(|w| unsafe { w.tim_top().bits(value) });
                }

                // HAL_Timer32_Start — ENABLE.TIM_EN (mik32_hal_timer32.c
                // через HAL_Timer32_Base_Start, не включает INT_MASK).
                fn start(&self) {
                    self.enable().modify(|_, w| w.tim_en().enable());
                }

                // HAL_Timer32_InterruptMask_Set/Clear — `|=`/`&= ~`, не
                // прямая запись (mik32_hal_timer32.c:175-185).
                fn set_interrupt_mask(&self, mask: u32) {
                    self.int_mask().modify(|r, w| unsafe { w.bits(r.bits() | mask) });
                }
                fn clear_interrupt_mask(&self, mask: u32) {
                    self.int_mask().modify(|r, w| unsafe { w.bits(r.bits() & !mask) });
                }

                // HAL_Timer32_InterruptFlags_Get/ClearMask — INT_FLAG
                // read-only статус, INT_CLEAR — запись маски сбрасываемых
                // флагов (не именованные биты в PAC — write-only регистр).
                fn interrupt_flags(&self) -> u32 {
                    self.int_flag().read().bits()
                }
                fn clear_interrupt_flags(&self, mask: u32) {
                    self.int_clear().write(|w| unsafe { w.bits(mask) });
                }
            }
        )+
    }
}

// Timer32_0 тактируется с шины APB_M (__HAL_PCC_TIMER32_0_CLK_ENABLE,
// mik32_hal_pcc.h:118), а Timer32_1/2 — с APB_P (:166,169) — разные
// регистры PM, в отличие от Timer16, у которого все три инстанса на одной
// шине.
impl_instance_base! {
    Timer32_0: (|pm: &mik32v2_pac::pm::RegisterBlock| pm.clk_apb_m_set().modify(|_, w| w.timer32_0().set_bit())), mux_tim32_0_tim1, mux_tim32_0_tim2,
    Timer32_1: (|pm: &mik32v2_pac::pm::RegisterBlock| pm.clk_apb_p_set().modify(|_, w| w.timer32_1().set_bit())), mux_tim32_1_tim1, mux_tim32_1_tim2,
    Timer32_2: (|pm: &mik32v2_pac::pm::RegisterBlock| pm.clk_apb_p_set().modify(|_, w| w.timer32_2().set_bit())), mux_tim32_2_tim1, mux_tim32_2_tim2,
}

macro_rules! impl_instance_with_channels {
    ($(
        $TimerX:ident,
    )+) => {
        $(
            impl InstanceWithChannels for mik32v2_pac::$TimerX {
                // HAL_Timer32_Channel_Init (mik32_hal_timer32.c:193-231):
                // CNTRL.MODE/NOISE/CAPTURE_EDGE/PWM_INV в одном modify(), затем
                // OCR (compare/pwm). NOISE и PWM_INV пишутся сырыми битами —
                // см. расхождение PAC/HAL в комментарии начала файла; MODE и
                // EDGE — именованными методами (совпадают с реальным HAL).
                fn configure_channel(&self, index: u8, config: &ChannelConfig) {
                    let noise_bit: u32 = if config.noise_filter { 1 } else { 0 }; // TIMER32_CHANNEL_FILTER_ON=1/OFF=0
                    let pwm_inv_bit: u32 = if config.pwm_invert { 1 } else { 0 };  // TIMER32_CHANNEL_INVERTED_PWM=1/NON_INVERTED=0

                    macro_rules! configure_one_channel {
                        ($cntr:ident, $ocr:ident, $icr:ident) => {{
                            self.$cntr().modify(|r, w| {
                                // Bit 7 (EN) выставляется здесь же —
                                // HAL_Timer32_Channel_Enable (mik32_hal_timer32.c,
                                // CNTRL |= TIMER32_CH_CNTRL_EN_M), в C-ветке
                                // вызывается отдельно внутри каждого *_Start_IT
                                // сразу после Channel_Init.
                                let bits = (r.bits() & !1u32 & !(1u32 << 8)) | noise_bit | (pwm_inv_bit << 8) | (1u32 << 7);
                                let w = unsafe { w.bits(bits) };
                                let w = match config.mode {
                                    ChannelMode::Compare => w.mode().compare(),
                                    ChannelMode::Capture => w.mode().capture(),
                                    ChannelMode::Pwm => w.mode().pwm(),
                                };
                                match config.capture_edge {
                                    CaptureEdge::Rising => w.edge().front(),
                                    CaptureEdge::Falling => w.edge().back(),
                                }
                            });
                            match config.mode {
                                ChannelMode::Pwm | ChannelMode::Compare => {
                                    self.$ocr().write(|w| unsafe { w.ocr().bits(config.ocr) });
                                }
                                ChannelMode::Capture => {
                                    let _ = self.$icr().read();
                                }
                            }
                        }};
                    }

                    match index {
                        0 => configure_one_channel!(ch1_cntr, ch1_ocr, ch1_icr),
                        1 => configure_one_channel!(ch2_cntr, ch2_ocr, ch2_icr),
                        2 => configure_one_channel!(ch3_cntr, ch3_ocr, ch3_icr),
                        3 => configure_one_channel!(ch4_cntr, ch4_ocr, ch4_icr),
                        _ => unreachable!("Timer32 channel index must be 0..=3"),
                    }
                }
            }
        )+
    }
}

impl_instance_with_channels! {
    Timer32_1,
    Timer32_2,
}
