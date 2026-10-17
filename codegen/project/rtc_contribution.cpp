#include "rtc_contribution.h"
#include "contribution_utils.h"
#include <sstream>

// Концепция: RTC — единственный глобальный блок (hrtc.Instance = RTC,
// mik32_memory_map.h), без instance в имени хендла/функций, в отличие от
// всех остальных контрибуций этого каталога.
ParseResult<PeripheralContribution> buildRtcContribution(
    const RtcConfig& config,
    const RtcKnowledge& knowledge)
{
    const std::string* dow_enum = findOrNull(knowledge.weekday_hal_enum, std::to_string(config.dow));
    const std::string* month_enum = findOrNull(knowledge.month_hal_enum, std::to_string(config.month));
    if (!dow_enum || !month_enum)
    {
        return ParseResult<PeripheralContribution>{
            false, "RtcConfig ссылается на dow/month, отсутствующий в knowledge_base/rtc.json", std::nullopt};
    }

    PeripheralContribution contribution;
    contribution.label = "RTC";
    contribution.init_order = kInitOrderRtc;
    contribution.init_function_name = "RTC_Init";
    contribution.init_c_includes = {"mik32_hal_rtc.h", "mik32_hal_pcc.h"};
    contribution.public_prototypes = {"void " + contribution.init_function_name + "(void);"};
    contribution.global_definitions = "RTC_HandleTypeDef hrtc;\n";

    std::ostringstream init_c;
    init_c << "/*\n"
              " * RTC: " << *dow_enum << " " << config.day << "." << config.month << "." << config.year
           << " (век " << config.century << ") " << config.hours << ":" << config.minutes << ":" << config.seconds << ".\n";
    if (config.has_alarm)
    {
        init_c << " * Будильник: сравнивает поля {";
        for (size_t i = 0; i < config.alarm_compare_fields.size(); ++i)
        {
            if (i > 0) init_c << ", ";
            init_c << config.alarm_compare_fields[i];
        }
        init_c << "} с заданным временем/датой будильника — остальные поля игнорируются сравнением.\n";
        if (config.alarm_interrupt_enabled)
        {
            // Errata MIK32V2 v1.0.1, §6.1 «Активный уровень срабатывания
            // будильника»: WU.SYS_LEVEL.LVL_RTC не несёт функциональной
            // нагрузки для срабатывания самого будильника внутри RTC — это
            // отдельный бит контроллера пробуждения (shared/periphery/wakeup.h,
            // Тир 3 этого аудита, нет HAL-обёртки), актуальный только для
            // ПРОБУЖДЕНИЯ ядра из спящего/стоп-режима по этому прерыванию,
            // не для срабатывания прерывания при активном ядре. Этот кодоген
            // не входит в сон/стоп-режимы (см. AUDIT_TZ_PERIPHERY_GAPS.md,
            // Тир 3, «Режимы энергопотребления») — упомянуто для полноты,
            // если пользователь планирует использовать будильник для выхода
            // из сна, обязательно установить LVL_RTC=1 вручную.
            init_c << " * Errata п. 6.1 (mik32v2errataV1_0_1_1 v1.0.1): если планируете использовать\n"
                      " * этот будильник для ПРОБУЖДЕНИЯ ядра из спящего/стоп-режима (а не просто\n"
                      " * прерывание при активном ядре), WU.SYS_LEVEL.LVL_RTC должен быть вручную\n"
                      " * установлен в 1 — иначе активный уровень срабатывания будильника не несёт\n"
                      " * функциональной нагрузки. Этот кодоген не настраивает режимы энергопотребления\n"
                      " * (отдельный, не реализованный пока Тир 3 модуль) — только сама RTC.\n"
                      " * EPIC-маскирование (HAL_EPIC_MaskLevelSet(HAL_EPIC_RTC_MASK)) и подключение\n"
                      " * RTC_IRQHandler_Template() (см. ниже) к своему raw_trap_handler остаются\n"
                      " * на стороне пользователя.\n";
        }
    }
    init_c << " */\n"
              "void " << contribution.init_function_name << "(void)\n"
              "{\n"
              // Найдено на реальной плате (HW-тест этой сессии): в отличие
              // от Timer16/Timer32, HAL_RTC_MspInit НИКОГДА не вызывается
              // автоматически ни одной функцией HAL_RTC_* — вендорный
              // пример (Hal_RTC/Hal_RTC_IRQ, RTC_Init()) вызывает этот
              // макрос вручную первой строкой. Без него RTC не тактируется
              // и регистры не меняются — счётчик визуально "стоит".
              "    __HAL_PCC_RTC_CLK_ENABLE();\n"
              "\n"
              "    hrtc.Instance = RTC;\n"
              "\n"
              "    /* Выключение RTC для записи даты и времени (докс. пример Hal_RTC/src/main.c) */\n"
              "    HAL_RTC_Disable(&hrtc);\n"
              "\n"
              "    RTC_TimeTypeDef sTime = {0};\n"
              "    sTime.Dow = " << *dow_enum << ";\n"
              "    sTime.Hours = " << config.hours << ";\n"
              "    sTime.Minutes = " << config.minutes << ";\n"
              "    sTime.Seconds = " << config.seconds << ";\n"
              "    HAL_RTC_SetTime(&hrtc, &sTime);\n"
              "\n"
              "    RTC_DateTypeDef sDate = {0};\n"
              "    sDate.Century = " << config.century << ";\n"
              "    sDate.Day = " << config.day << ";\n"
              "    sDate.Month = " << *month_enum << ";\n"
              "    sDate.Year = " << config.year << ";\n"
              "    HAL_RTC_SetDate(&hrtc, &sDate);\n";

    if (config.has_alarm)
    {
        const std::string* alarm_dow_enum = findOrNull(knowledge.weekday_hal_enum, std::to_string(config.alarm_dow));
        const std::string* alarm_month_enum = findOrNull(knowledge.month_hal_enum, std::to_string(config.alarm_month));
        if (!alarm_dow_enum || !alarm_month_enum)
        {
            return ParseResult<PeripheralContribution>{
                false, "RtcConfig ссылается на alarm_dow/alarm_month, отсутствующий в knowledge_base/rtc.json", std::nullopt};
        }

        std::ostringstream time_mask;
        std::ostringstream date_mask;
        bool first_time_field = true;
        bool first_date_field = true;
        for (const std::string& field : config.alarm_compare_fields)
        {
            if (const std::string* time_bit = findOrNull(knowledge.alarm_compare_time_mask, field))
            {
                if (!first_time_field) time_mask << " | ";
                time_mask << *time_bit;
                first_time_field = false;
            }
            else if (const std::string* date_bit = findOrNull(knowledge.alarm_compare_date_mask, field))
            {
                if (!first_date_field) date_mask << " | ";
                date_mask << *date_bit;
                first_date_field = false;
            }
        }
        if (first_time_field) time_mask << "0";
        if (first_date_field) date_mask << "0";

        init_c << "\n"
                  "    RTC_AlarmTypeDef sAlarm = {0};\n"
                  "    sAlarm.AlarmTime.Dow = " << *alarm_dow_enum << ";\n"
                  "    sAlarm.AlarmTime.Hours = " << config.alarm_hours << ";\n"
                  "    sAlarm.AlarmTime.Minutes = " << config.alarm_minutes << ";\n"
                  "    sAlarm.AlarmTime.Seconds = " << config.alarm_seconds << ";\n"
                  "    sAlarm.AlarmDate.Century = " << config.alarm_century << ";\n"
                  "    sAlarm.AlarmDate.Day = " << config.alarm_day << ";\n"
                  "    sAlarm.AlarmDate.Month = " << *alarm_month_enum << ";\n"
                  "    sAlarm.AlarmDate.Year = " << config.alarm_year << ";\n"
                  "    sAlarm.MaskAlarmTime = " << time_mask.str() << ";\n"
                  "    sAlarm.MaskAlarmDate = " << date_mask.str() << ";\n"
                  "    HAL_RTC_SetAlarm(&hrtc, &sAlarm);\n";

        if (config.alarm_interrupt_enabled)
        {
            init_c << "\n"
                      "    hrtc.Interrupts.Alarm = RTC_ALARM_IRQ_ENABLE;\n"
                      "    HAL_RTC_InterruptInit(&hrtc);\n";
        }
    }

    init_c << "\n"
              "    HAL_RTC_Enable(&hrtc);\n"
              "}\n";
    contribution.init_function_text = init_c.str();

    // Найдено на реальной плате (HW-тест этой сессии, AUDIT_TZ_PERIPHERY_GAPS.md):
    // RTC_CTRL.ALRM — уровневый флаг, который НЕ снимается сбросом одного
    // только EPIC (HAL_EPIC_Clear). Если обработчик прерывания не вызовет
    // HAL_RTC_ClearAlrmFlag, линия останется активной и ядро немедленно
    // уйдёт в обработчик снова сразу после return — бесконечный повторный
    // вход, прошивка виснет в момент срабатывания будильника, даже не успев
    // вернуться в main(). Шаблон ниже — готовый правильный обработчик,
    // чтобы пользователю не пришлось находить эту проблему самому.
    if (config.has_alarm && config.alarm_interrupt_enabled)
    {
        std::ostringstream irq_template;
        irq_template << "/*\n"
                        " * Шаблон обработчика прерывания RTC. Вызовите из своего raw_trap_handler\n"
                        " * внутри if (EPIC_CHECK_RTC()) { ... }. ВАЖНО: RTC_CTRL.ALRM — уровневый\n"
                        " * флаг, не снимается одним только HAL_EPIC_Clear — без вызова\n"
                        " * HAL_RTC_ClearAlrmFlag ниже прошивка зависнет в бесконечном повторном\n"
                        " * входе в прерывание сразу в момент срабатывания будильника (найдено на\n"
                        " * реальной плате при HW-валидации этого модуля).\n"
                        " */\n"
                        "void RTC_IRQHandler_Template(void)\n"
                        "{\n"
                        "    HAL_RTC_AlarmDisable(&hrtc);\n"
                        "    HAL_RTC_ClearAlrmFlag(&hrtc);\n"
                        "\n"
                        "    /* USER TODO: обработать срабатывание будильника. */\n"
                        "}\n";
        contribution.helper_functions_text = irq_template.str();
        contribution.irq_handler_function_name = "RTC_IRQHandler_Template";
    }

    return ParseResult<PeripheralContribution>{true, "", contribution};
}
