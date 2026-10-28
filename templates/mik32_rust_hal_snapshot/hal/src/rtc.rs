//! RTC HAL-обёртка — Rust-паритет с codegen/project/rtc_contribution.cpp
//! (C-ветка): время+дата+будильник, частичное сравнение полей (compare_fields).
//! RTC — единственный глобальный блок (mik32_memory_map.h: RTC_BASE_ADDRESS),
//! без instance, как в C/Arduino-ветках.
//!
//! Регистры TIME/DATE/TALRM/DALRM используют BCD-кодировку (десятки/единицы
//! раздельными полями) — PAC даёт для этого именованные `.s()/.ts()/.m()/...`
//! методы (templates/mik32_rust_hal_snapshot/pac/src/rtc/rrtc_time.rs,
//! rrtc_date.rs), поэтому raw-битовый обход (как у Timer32 CONTROL.SOURSE)
//! здесь не нужен — PAC для RTC совпадает с реальным HAL (mik32_hal_rtc.c)
//! по разбору BCD, сверено построчно.
//!
//! IRQ/EPIC: по тому же критерию, что уже применён к I2C/SPI/Timer32
//! (EPIC не обёрнут в mik32v2-hal), подключение прерывания к trap_handler
//! остаётся на стороне пользователя — ровно как в C-ветке
//! (rtc_contribution.cpp: "EPIC-маскирование... остаются на стороне
//! пользователя", тот же паттерн, что у SPI/GPIO IRQ). Поэтому
//! `interrupt_enabled=true` переносится полностью (RTC_CTRL.INTE), без
//! отказа в rust_feature_validator.cpp — объём не превышает C-ветку.
//!
//! Найденный на реальной плате баг (C-ветка, HW-тест 2026-10-1x,
//! см. feedback_hal_init_epic_clock_gotcha и
//! feedback_level_interrupt_needs_peripheral_side_clear в памяти
//! ассистента): без __HAL_PCC_RTC_CLK_ENABLE() регистры RTC не тактированы
//! и не меняются; RTC_CTRL.ALRM — уровневый флаг, не снимается одним EPIC-
//! сбросом, нужен явный HAL_RTC_ClearAlrmFlag на стороне периферии. Здесь
//! это задокументировано в комментарии на структуре Alarm — Rust-ветка не
//! генерирует IRQ-обработчик автоматически (ни один уже портированный
//! модуль Rust-кодогена это не делает), поэтому поставить пользователя
//! перед тем же риском без возможности его обойти кодом нельзя — комментарий
//! единственный способ предупредить.

use mik32v2_pac::Pm;

/// RTC_TimeTypeDef — текущее время (устанавливается один раз, RTC считает сама).
#[derive(Debug, Clone, Copy)]
pub struct Time {
    pub dow: u8,
    pub hours: u8,
    pub minutes: u8,
    pub seconds: u8,
}

/// RTC_DateTypeDef.
#[derive(Debug, Clone, Copy)]
pub struct Date {
    pub century: u8,
    pub day: u8,
    pub month: u8,
    pub year: u8,
}

/// Подмножество полей, которые будильник обязан сравнить, чтобы сработать
/// (MaskAlarmTime/MaskAlarmDate — RTC_TALRM_C*_M/RTC_DALRM_C*_M).
#[derive(Debug, Clone, Copy, Default)]
pub struct AlarmCompareFields {
    pub dow: bool,
    pub hours: bool,
    pub minutes: bool,
    pub seconds: bool,
    pub century: bool,
    pub day: bool,
    pub month: bool,
    pub year: bool,
}

/// RTC_AlarmTypeDef.
#[derive(Debug, Clone, Copy)]
pub struct Alarm {
    pub time: Time,
    pub date: Date,
    pub compare: AlarmCompareFields,
    pub interrupt_enabled: bool,
}

#[derive(Debug, Clone, Copy)]
pub struct Config {
    pub time: Time,
    pub date: Date,
    pub alarm: Option<Alarm>,
}

pub struct Rtc {
    rtc: mik32v2_pac::Rtc,
}

impl Rtc {
    /// HAL_RTC_MspInit (clock) + Disable + SetTime/SetDate(+Alarm) + Enable —
    /// точная копия порядка операций rtc_contribution.cpp (C-ветка): RTC
    /// нужно выключить (CTRL.EN=0) перед записью TIME/DATE, иначе запись
    /// игнорируется (mik32_hal_rtc.c, комментарий у HAL_RTC_Disable).
    pub fn new(rtc: mik32v2_pac::Rtc, pm: &Pm, config: Config) -> Self {
        pm.clk_apb_m_set().write(|w| w.rtc().set_bit());

        rtc.rrtc_ctrl().modify(|_, w| w.en().disabled());
        Self::wait_flag(&rtc);

        // Как и для TALRM/DALRM ниже (см. комментарий там): пишем BCD raw-
        // битами через те же сдвиги, вместо именованных .th()/.h()/.../().set()
        // — у автогенератора эти field writer'ы помечены Unsafe (нет
        // декларированного диапазона в svd), `.set()` на них не реализован,
        // нужен `.bits()`. Проще и единообразнее со способом записи TALRM/DALRM
        // переиспользовать уже выведенные pack_time_bits/pack_date_bits.
        let time_value = pack_time_bits(config.time);
        rtc.rrtc_time().write(|w| unsafe { w.bits(time_value) });
        Self::wait_flag(&rtc);

        let date_value = pack_date_bits(config.date);
        rtc.rrtc_date().write(|w| unsafe { w.bits(date_value) });
        Self::wait_flag(&rtc);

        if let Some(alarm) = config.alarm {
            // PAC-ограничение (отличие от TIME/DATE): автогенератор не
            // экспонировал value-поля (H/M/S/DOW, D/M/Y/C) для TALRM/DALRM —
            // только Cx-биты сравнения (rrtc_talrm.rs/rrtc_dalrm.rs). Реальный
            // HAL (mik32_hal_rtc.c:HAL_RTC_Alarm_SetTime/SetDate) пишет
            // значение по ТЕМ ЖЕ сдвигам, что TIME/DATE (RTC_TIME_*_S,
            // RTC_DATE_*_S переиспользуются для TALRM/DALRM, подтверждено
            // shared/periphery/rtc.h) — поэтому значение пишем raw-битами
            // (как уже сделано для Timer32 CONTROL.SOURSE/PWM_INV в
            // rust/timer32.md), компарные биты — именованным API сразу после.
            let talrm_value = pack_time_bits(alarm.time);
            rtc.rrtc_talrm().write(|w| unsafe { w.bits(talrm_value) });
            Self::wait_flag(&rtc);
            rtc.rrtc_talrm().modify(|_, w| {
                if alarm.compare.seconds { w.cs().enable(); }
                if alarm.compare.minutes { w.cm().enable(); }
                if alarm.compare.hours { w.ch().enable(); }
                if alarm.compare.dow { w.cdow().enable(); }
                w
            });
            Self::wait_flag(&rtc);

            let dalrm_value = pack_date_bits(alarm.date);
            rtc.rrtc_dalrm().write(|w| unsafe { w.bits(dalrm_value) });
            Self::wait_flag(&rtc);
            rtc.rrtc_dalrm().modify(|_, w| {
                if alarm.compare.day { w.cd().enable(); }
                if alarm.compare.month { w.cm().enable(); }
                if alarm.compare.year { w.cy().enable(); }
                if alarm.compare.century { w.cc().enable(); }
                w
            });
            Self::wait_flag(&rtc);

            if alarm.interrupt_enabled {
                rtc.rrtc_ctrl().modify(|_, w| w.inte().enable());
                Self::wait_flag(&rtc);
            }
        }

        rtc.rrtc_ctrl().modify(|_, w| w.en().enable());
        Self::wait_flag(&rtc);

        Self { rtc }
    }

    /// Сброс уровневого флага ALRM на стороне периферии (RTC_CTRL.ALRM) —
    /// HAL_EPIC_Clear() ОДИН не снимает эту линию (см. комментарий модуля
    /// выше); вызывать из своего обработчика IRQ, не только чистить EPIC.
    pub fn clear_alarm_flag(&self) {
        self.rtc.rrtc_ctrl().modify(|_, w| w.alrm().inactive());
        Self::wait_flag(&self.rtc);
    }

    pub fn alarm_flag_active(&self) -> bool {
        self.rtc.rrtc_ctrl().read().alrm().is_active()
    }

    pub fn free(self) -> mik32v2_pac::Rtc {
        self.rtc
    }

    /// HAL_RTC_WaitFlag: синхронизация между тактовыми доменами, запрещена
    /// новая запись пока CTRL.FLAG=1 (mik32_hal_rtc.c, retry_limit=10000).
    fn wait_flag(rtc: &mik32v2_pac::Rtc) {
        for _ in 0..10_000 {
            if rtc.rrtc_ctrl().read().flag().is_ready() {
                return;
            }
        }
        while !rtc.rrtc_ctrl().read().flag().is_ready() {}
    }
}

/// Сдвиги битов BCD-полей TIME, переиспользуемые TALRM (shared/periphery/rtc.h:
/// RTC_TIME_DOW_S=24, TH_S=22, H_S=18, TM_S=15, M_S=11, TS_S=8, S_S=4).
fn pack_time_bits(time: Time) -> u32 {
    ((time.dow as u32) << 24)
        | (((time.hours / 10) as u32) << 22)
        | (((time.hours % 10) as u32) << 18)
        | (((time.minutes / 10) as u32) << 15)
        | (((time.minutes % 10) as u32) << 11)
        | (((time.seconds / 10) as u32) << 8)
        | (((time.seconds % 10) as u32) << 4)
}

/// Сдвиги битов BCD-полей DATE, переиспользуемые DALRM (shared/periphery/rtc.h:
/// RTC_DATE_TC_S=23, C_S=19, TY_S=15, Y_S=11, TM_S=10, M_S=6, TD_S=4, D_S=0).
fn pack_date_bits(date: Date) -> u32 {
    (((date.century / 10) as u32) << 23)
        | (((date.century % 10) as u32) << 19)
        | (((date.year / 10) as u32) << 15)
        | (((date.year % 10) as u32) << 11)
        | (((date.month / 10) as u32) << 10)
        | (((date.month % 10) as u32) << 6)
        | (((date.day / 10) as u32) << 4)
        | ((date.day % 10) as u32)
}

