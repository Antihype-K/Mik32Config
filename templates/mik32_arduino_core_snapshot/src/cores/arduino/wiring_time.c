/*
  Copyright (c) 2011 Arduino.  All right reserved.

  This library is free software; you can redistribute it and/or
  modify it under the terms of the GNU Lesser General Public
  License as published by the Free Software Foundation; either
  version 2.1 of the License, or (at your option) any later version.

  This library is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
  See the GNU Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public
  License along with this library; if not, write to the Free Software
  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
*/

// HAL-заголовок — до Arduino.h (см. комментарий в wiring_digital.c).
#include "mik32_hal_scr1_timer.h"
#include "Arduino.h"

#ifdef __cplusplus
extern "C" {
#endif

// Старый elbear-API (SCR1_TIMER_HandleTypeDef/HAL_SCR1_Timer_Init(&handle)) в
// установленном framework-mik32v2-sdk заменён на готовые обёртки
// HAL_Time_SCR1TIM_* (hal/core/Source/mik32_hal_scr1_timer.c) — используем их
// напрямую вместо воссоздания логики делителя (найдено на реальной сборке
// 2026-10-26: handle-структура и HAL_SCR1_Timer_Init(clockSource, divider) не
// совпадали по сигнатуре с вендорным кодом elbear_arduino_bsp).
void SysTick_Init(void)
{
    HAL_Time_SCR1TIM_Init();
}

// number of microseconds since start of the program
uint32_t micros(void)
{
  return HAL_Time_SCR1TIM_Micros();
}

volatile uint32_t millis(void)
{
  return HAL_Time_SCR1TIM_Millis();
}

void delay(uint32_t ms)
{
  HAL_Time_SCR1TIM_DelayMs(ms);
}

void delayMicroseconds(uint16_t us)
{
  HAL_Time_SCR1TIM_DelayUs(us);
}

#ifdef __cplusplus
}
#endif
