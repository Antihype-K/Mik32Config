//! Timer16 HAL-обёртка — Rust-паритет с codegen/project/timer16_contribution.cpp
//! (C-ветка): mode=period/one_shot/external_event_count/encoder, 6 из 7
//! источников тактирования (TIMER16_SOURCE_EXTERNAL_INPUT1 исключён — см.
//! kClockSourceInfo в C-ветке), trigger_edge, фильтры, waveform_inverted,
//! режим энкодера. Trigger.Source НАМЕРЕННО не реализован — та же граница,
//! что и в C-ветке (schemas/timer16.json: not_covered_by_this_codegen.trigger
//! описывает это как сознательно отложенное, не забытое).
//!
//! mik32-hal-main НЕ содержит рабочего кода таймеров вовсе (lib.rs: "timer"/
//! "timer::delay" исключены как мёртвый код, скопированный из ARM Cortex-M
//! шаблона, см. docs/implementations/rust_codegen.md) — это новый модуль,
//! написанный по тому же паттерну, что i2c.rs (этот же каталог): Instance
//! trait + macro_rules! для трёх разных PAC-типов (Timer16_0/1/2 — РАЗНЫЕ
//! Rust-типы, структурно идентичные, как и у I2C, не единый Deref-bound).
//!
//! Строки ниже сверены построчно с mik32_hal_timer16.c (HW-подтверждённый
//! HAL, см. docs/implementations/timer16.md) — комментарии указывают
//! конкретную Si-функцию C-эталона.
//!
//! DMA/прерывания НЕ реализованы (отклоняется в rust_feature_validator.cpp) —
//! как и в C-ветке, этот кодоген вызывает только non-IT варианты запуска.

use mik32v2_pac::Pm;

use crate::gpio::{self, TimerSerialMode};

/// ШИМ-вывод конкретного instance — занят только при mode=period/one_shot
/// (knowledge_base/timer16.json: mode_waveform_enable).
pub trait PinTimerPwm<I> {}
/// Input1 — занят при mode=external_event_count ИЛИ mode=encoder
/// (mik32_hal_timer16.c, HAL_TIMER16_MspInit).
pub trait PinTimerInput1<I> {}
/// Input2 — занят только при mode=encoder.
pub trait PinTimerInput2<I> {}

// Датащит МИК32 v2.2.3, Таблица 193; те же номера, что knowledge_base/
// timer16.json: instance_fixed_pwm_pin/instance_fixed_input1_pin/
// instance_fixed_input2_pin, уже проверенные C-веткой и cross_module_validator.
impl PinTimerPwm<mik32v2_pac::Timer16_0> for gpio::P16_0_7<TimerSerialMode> {}
impl PinTimerInput1<mik32v2_pac::Timer16_0> for gpio::P16_0_5<TimerSerialMode> {}
impl PinTimerInput2<mik32v2_pac::Timer16_0> for gpio::P16_0_6<TimerSerialMode> {}

impl PinTimerPwm<mik32v2_pac::Timer16_1> for gpio::P16_0_10<TimerSerialMode> {}
impl PinTimerInput1<mik32v2_pac::Timer16_1> for gpio::P16_0_8<TimerSerialMode> {}
impl PinTimerInput2<mik32v2_pac::Timer16_1> for gpio::P16_0_9<TimerSerialMode> {}

impl PinTimerPwm<mik32v2_pac::Timer16_2> for gpio::P16_0_13<TimerSerialMode> {}
impl PinTimerInput1<mik32v2_pac::Timer16_2> for gpio::P16_0_11<TimerSerialMode> {}
impl PinTimerInput2<mik32v2_pac::Timer16_2> for gpio::P16_0_12<TimerSerialMode> {}

/// CFGR.CKSEL (внутренний путь) + PM.TIMER_CFG.MUX_TIMER16_x — HAL_Timer16_
/// SetSourceClock (mik32_hal_timer16.c:159-192): для 6 внутренних источников
/// пишется 3-битный MUX в PM, CKSEL=0. TIMER16_SOURCE_EXTERNAL_INPUT1 (7-й
/// вариант) здесь не представлен — используется отдельно через mode
/// (см. Timer16::new_counter ниже, которая сама ставит CKSEL=1).
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum ClockSource { SysClk, Hclk, Osc32m, Hsi32m, Osc32k, Lsi32k }

/// CFGR.PRESC — mik32_hal_timer16.c, HAL_Timer16_SetPrescaler. 8 значений,
/// степени двойки 1..128 — те же, что prescaler_hal_enum в knowledge_base/
/// timer16.json.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Prescaler { P1, P2, P4, P8, P16, P32, P64, P128 }

/// CFGR.TRIGEN — Trigger.ActiveEdge (HAL_Timer16_SetTriggerEdge). Трактуется
/// как в C-ветке: Trigger.Source не задаётся этим кодогеном, но
/// ActiveEdge/TRIGEN всё равно настраивается явно.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum TriggerEdge { Software, Rising, Falling, Both }

/// CFGR.CKFLT/TRGFLT — HAL_Timer16_FilterTypeDef, общий тип для Filter.
/// ExternalClock и Filter.Trigger (как в C-ветке, filterConstant).
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Filter { None, Clock2, Clock4, Clock8 }

/// Общие поля настройки, собираемые ДО запуска конкретного режима — 1:1 с
/// частью Timer16Config (parser/timer16/timer16_parser.h), которая не зависит
/// от mode.
#[derive(Debug, Clone, Copy)]
pub struct Config {
    pub prescaler: Prescaler,
    pub trigger_edge: TriggerEdge,
    pub external_clock_filter: Filter,
    pub trigger_filter: Filter,
}

/// Peripheral wrapper. `MODE` типизирует, что и как пользователь запустил —
/// отдельного трансивера, как у I2C, не нужно: таймер после запуска работает
/// аппаратно без участия CPU (тот же вывод, что уже сделан в C-ветке,
/// timer16_contribution.h: "Флага emit_demo_loop нет намеренно").
pub struct Timer16<I> {
    timer: I,
}

impl<I: Instance> Timer16<I> {
    /// mode=period/one_shot. HAL_Timer16_Init + HAL_Timer16_StartPWM/
    /// StartOneShot (mik32_hal_timer16.c:495-644).
    pub fn new_pwm<PIN>(
        timer: I,
        _pwm_pin: PIN,
        clock_source: ClockSource,
        config: Config,
        waveform_inverted: bool,
        one_shot: bool,
        arr: u16,
        cmp: u16,
    ) -> Self
    where
        PIN: PinTimerPwm<I>,
    {
        let pm = unsafe { &(*Pm::ptr()) };
        I::enable_clock(pm);
        timer.disable();

        // HAL_Timer16_ClockInit -> HAL_Timer16_SetSourceClock (внутренний
        // источник): PM.TIMER_CFG.MUX_TIMER16_x = SourceClock, CFGR.CKSEL=0.
        I::set_internal_clock_mux(pm, clock_source);
        timer.configure_cfgr(false, false, config, waveform_inverted);

        timer.set_arr(arr);
        timer.set_cmp(cmp);

        // HAL_Timer16_StartPWM/StartOneShot: Disable -> CFGR.WAVE=0 (PWM/
        // one-shot формат, не "set-once") -> Enable -> запуск.
        timer.enable();
        if one_shot {
            timer.start_single();
        } else {
            timer.start_continuous();
        }

        Timer16 { timer }
    }

    /// mode=external_event_count. HAL_Timer16_Counter_Start
    /// (mik32_hal_timer16.c:589-599) — считает внешние события на Input1,
    /// волновая форма не генерируется (Waveform.Enable=DISABLE, не
    /// настраивается этой функцией вовсе — mode_waveform_enable=false).
    pub fn new_counter<PIN>(timer: I, _input1_pin: PIN, config: Config, arr: u16) -> Self
    where
        PIN: PinTimerInput1<I>,
    {
        let pm = unsafe { &(*Pm::ptr()) };
        I::enable_clock(pm);
        timer.disable();

        // HAL_Timer16_SetSourceClock(TIMER16_SOURCE_EXTERNAL_INPUT1):
        // CFGR.CKSEL=1, без записи PM.TIMER_CFG.
        timer.configure_cfgr(true, true, config, false);

        timer.set_arr(arr);

        timer.enable();
        timer.start_continuous();

        Timer16 { timer }
    }

    /// mode=encoder. HAL_Timer16_Encoder_Start (mik32_hal_timer16.c:677-688):
    /// CKSEL обязан быть внутренним (0), PRESC обязан быть 1 (проверено
    /// парсером, как и в C-ветке) — AUDIT_TZ_PERIPHERY_GAPS.md, раздел
    /// «Режим энкодера».
    pub fn new_encoder<P1, P2>(
        timer: I,
        _input1_pin: P1,
        _input2_pin: P2,
        config: Config,
        arr: u16,
    ) -> Self
    where
        P1: PinTimerInput1<I>,
        P2: PinTimerInput2<I>,
    {
        let pm = unsafe { &(*Pm::ptr()) };
        I::enable_clock(pm);
        timer.disable();

        // CKSEL=0 (внутренний), SysClk — HAL_Timer16_Init по умолчанию не
        // трогает Clock.Source при encoder в C-ветке (clock_source
        // игнорируется этим кодогеном для encoder точно так же, как для
        // external_event_count), PRESC=1 (P1) обязателен для энкодера.
        timer.configure_cfgr(false, false, config, false);
        timer.set_encoder_mode(true);

        timer.enable();
        // HAL_Timer16_Encoder_Start: ICR = ARROKCF|DOWNCF|UPCF перед записью ARR.
        timer.clear_encoder_flags();
        timer.set_arr(arr);
        timer.start_continuous();

        Timer16 { timer }
    }

    pub fn free(self) -> I {
        self.timer
    }
}

/// Implemented by all Timer16 instances. Методы, а не единый Deref-bound —
/// Timer16_0/1/2 — разные Rust-типы, как I2c0/I2c1 (см. комментарий в начале
/// файла).
pub trait Instance {
    fn enable_clock(pm: &mik32v2_pac::pm::RegisterBlock);
    fn set_internal_clock_mux(pm: &mik32v2_pac::pm::RegisterBlock, source: ClockSource);
    fn disable(&self);
    fn enable(&self);
    fn configure_cfgr(&self, external_clock: bool, count_external: bool, config: Config, waveform_inverted: bool);
    fn set_encoder_mode(&self, enable: bool);
    fn clear_encoder_flags(&self);
    fn set_arr(&self, value: u16);
    fn set_cmp(&self, value: u16);
    fn start_single(&self);
    fn start_continuous(&self);
}

macro_rules! impl_instance {
    ($(
        $TimerX:ident: $mux_method:ident, $ten:ident,
    )+) => {
        $(
            impl Instance for mik32v2_pac::$TimerX {
                fn enable_clock(pm: &mik32v2_pac::pm::RegisterBlock) {
                    pm.clk_apb_p_set().modify(|_, w| w.$ten().set_bit());
                }

                // HAL_Timer16_SetSourceClock, внутренний путь
                // (mik32_hal_timer16.c:174-191): PM.TIMER_CFG.MUX_TIMER16_x.
                fn set_internal_clock_mux(pm: &mik32v2_pac::pm::RegisterBlock, source: ClockSource) {
                    pm.timer_cfg().modify(|_, w| {
                        let w = w.$mux_method();
                        match source {
                            ClockSource::SysClk => w.sys_clk(),
                            ClockSource::Hclk => w.hclk(),
                            ClockSource::Osc32m => w.osc32m(),
                            ClockSource::Hsi32m => w.hsi32m(),
                            ClockSource::Osc32k => w.osc32k(),
                            ClockSource::Lsi32k => w.lsi32k(),
                        }
                    });
                }

                // HAL_Timer16_Disable/Enable — CR.ENABLE.
                fn disable(&self) {
                    self.cr().modify(|_, w| w.enable().clear_bit());
                }
                fn enable(&self) {
                    self.cr().modify(|_, w| w.enable().set_bit());
                }

                // HAL_Timer16_Init: ClockInit (CKSEL/COUNTMODE) + SetPrescaler
                // + SetFilterExternalClock/SetFilterTrigger + SetPreload
                // (всегда AFTERWRITE — PRELOAD не в scope, как в C-ветке,
                // handle_name.Preload = TIMER16_PRELOAD_AFTERWRITE
                // захардкожено) + SelectTrigger(SOFTWARE)/SetTriggerEdge +
                // WaveformPolarity. Один modify() вместо отдельных вызовов —
                // CFGR можно писать одним проходом, как и SetSourceClock+
                // SetPrescaler+... делают последовательные RMW одного и
                // того же регистра в C.
                fn configure_cfgr(&self, external_clock: bool, count_external: bool, config: Config, waveform_inverted: bool) {
                    self.cfgr().modify(|_, w| {
                        let w = if external_clock { w.cksel().external() } else { w.cksel().internal() };
                        let w = if count_external { w.count_mode().external() } else { w.count_mode().internal() };
                        let w = match config.prescaler {
                            Prescaler::P1 => w.presc()._1(),
                            Prescaler::P2 => w.presc()._2(),
                            Prescaler::P4 => w.presc()._4(),
                            Prescaler::P8 => w.presc()._8(),
                            Prescaler::P16 => w.presc()._16(),
                            Prescaler::P32 => w.presc()._32(),
                            Prescaler::P64 => w.presc()._64(),
                            Prescaler::P128 => w.presc()._128(),
                        };
                        let w = match config.external_clock_filter {
                            Filter::None => w.ckflt().disable(),
                            Filter::Clock2 => w.ckflt()._2clock(),
                            Filter::Clock4 => w.ckflt()._4clock(),
                            Filter::Clock8 => w.ckflt()._8clock(),
                        };
                        let w = match config.trigger_filter {
                            Filter::None => w.trgflt().disable(),
                            Filter::Clock2 => w.trgflt()._2clock(),
                            Filter::Clock4 => w.trgflt()._4clock(),
                            Filter::Clock8 => w.trgflt()._8clock(),
                        };
                        // Preload=AFTERWRITE — та же хардкод-граница, что
                        // timer16_contribution.cpp:264.
                        let w = w.preload().after_write();
                        // Trigger.Source не реализован (см. комментарий в
                        // начале файла) — TRIGEN всё равно настраивается по
                        // trigger_edge, как в C-ветке.
                        let w = match config.trigger_edge {
                            TriggerEdge::Software => w.trigen().software(),
                            TriggerEdge::Rising => w.trigen().rising(),
                            TriggerEdge::Falling => w.trigen().folling(),
                            TriggerEdge::Both => w.trigen().both_edge(),
                        };
                        if waveform_inverted { w.wavwpol().inverted() } else { w.wavwpol().noninverted() }
                    });
                }

                // HAL_Timer16_SetEncoderMode — CFGR.ENC.
                fn set_encoder_mode(&self, enable: bool) {
                    self.cfgr().modify(|_, w| if enable { w.enc().enable() } else { w.enc().disable() });
                }

                // HAL_Timer16_Encoder_Start: ICR = ARROKCF|DOWNCF|UPCF.
                fn clear_encoder_flags(&self) {
                    self.icr().write(|w| w.arrrocf().bit(true).downcf().bit(true).upcf().bit(true));
                }

                // HAL_Timer16_SetARR: пишем ARR, затем (поскольку ARROKIE не
                // включается этим кодогеном — прерывания вне scope) busy-wait
                // ISR.ARROK, затем сброс флага через ICR.ARRMCF — тот же
                // приём, что HAL_Timer16_WaitARROK + __HAL_TIMER16_CLEAR_FLAG.
                fn set_arr(&self, value: u16) {
                    self.arr().write(|w| unsafe { w.arr().bits(value) });
                    while !self.isr().read().arrok().bit_is_set() {}
                    self.icr().write(|w| w.arrrocf().bit(true));
                }

                // HAL_Timer16_SetCMP — тот же приём с CMPOK/CMPMCF.
                fn set_cmp(&self, value: u16) {
                    self.cmp().write(|w| unsafe { w.cmp().bits(value) });
                    while !self.isr().read().cmpok().bit_is_set() {}
                    self.icr().write(|w| w.cmpmcf().bit(true));
                }

                // __HAL_TIMER16_START_SINGLE/CONTINUOUS — CR.SNGSTRT/CNTSTRT.
                fn start_single(&self) {
                    self.cr().modify(|_, w| w.sngstrt().set_bit());
                }
                fn start_continuous(&self) {
                    self.cr().modify(|_, w| w.cntstrt().set_bit());
                }
            }
        )+
    }
}

impl_instance! {
    Timer16_0: mux_tim16_0, timer16_0,
    Timer16_1: mux_tim16_1, timer16_1,
    Timer16_2: mux_tim16_2, timer16_2,
}
