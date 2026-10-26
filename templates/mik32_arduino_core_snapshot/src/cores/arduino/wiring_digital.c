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

// mik32_hal_gpio.h -> shared/periphery/gpio.h объявляет регистровую структуру
// с полем OUTPUT — должен быть подключён ДО Arduino.h, иначе макрос
// OUTPUT=0x1 (wiring_constants.h) подставится в это имя поля и структура не
// скомпилируется (найдено на реальной сборке 2026-10-26).
#include "mik32_hal_gpio.h"
#include "Arduino.h"
#include "pins_arduino.h"
#include "wiring_LL.h"

#ifdef __cplusplus
extern "C" {
#endif

// initialization
void pinMode(uint32_t PinNumber, uint32_t PinMode)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};

  // if pin number is greater than pin list size - return
  if ((PinNumber>=pinCommonQty()))
  {
    ErrorMsgHandler("pinMode(): pin number exceeds the total number of pins");
    return;
  }

  if (digitalPinPwmIsOn(PinNumber) > 0 )
    // if the pin use PWM, disable PWM
    analogWriteStop(PinNumber);

  // determine the port and the pin number in the port
  GPIO_TypeDef *GPIO_addr = digitalPinToPort(PinNumber);
  GPIO_InitStruct.Pin     = digitalPinToBitMask(PinNumber);

  // set up direction and pull up/down
  switch (PinMode)
  {
    case INPUT: 
      GPIO_InitStruct.Mode = HAL_GPIO_MODE_GPIO_INPUT;
      GPIO_InitStruct.Pull = HAL_GPIO_PULL_NONE;
      break;
    case INPUT_PULLUP:
      GPIO_InitStruct.Mode = HAL_GPIO_MODE_GPIO_INPUT;
      GPIO_InitStruct.Pull = HAL_GPIO_PULL_UP;
      break;
    case INPUT_PULLDOWN:
      GPIO_InitStruct.Mode = HAL_GPIO_MODE_GPIO_INPUT;
      GPIO_InitStruct.Pull = HAL_GPIO_PULL_DOWN;
      break;
    case OUTPUT:
      GPIO_InitStruct.Mode = HAL_GPIO_MODE_GPIO_OUTPUT;
      GPIO_InitStruct.Pull = HAL_GPIO_PULL_NONE;
      break;
  }

  // init pin
  HAL_GPIO_Init(GPIO_addr, &GPIO_InitStruct);
  additionalPinsInit(PinNumber);
}

void fastPinMode(uint32_t PinNumber, uint32_t PinMode)
{
  if ((PinNumber>=pinCommonQty()))
  {
    ErrorMsgHandler("fastPinMode(): pin number exceeds the total number of pins");
    return;
  }
  
  GPIO_TypeDef* port = digitalPinToPort(PinNumber);
  HAL_PinsTypeDef pinMask = digitalPinToBitMask(PinNumber);
  
  // set direction
  if (PinMode == OUTPUT)
    GPIO_OUTPUT_MODE_PIN(port, pinMask);
  else
    GPIO_INPUT_MODE_PIN(port, pinMask);
  
  // set pullup
  if (PinMode == INPUT_PULLUP)
  {
    uint8_t pos = PIN_MASK_TO_PIN_NUMBER(pinMask);
    if      (port == GPIO_0)  PIN_SET_PAD_CONFIG(PORT_0_PUPD, pos, HAL_GPIO_PULL_UP);
    else if (port == GPIO_1)  PIN_SET_PAD_CONFIG(PORT_1_PUPD, pos, HAL_GPIO_PULL_UP);
    else                      PIN_SET_PAD_CONFIG(PORT_2_PUPD, pos, HAL_GPIO_PULL_UP);
  }
}

// write pin
__attribute__((noinline, section(".ram_text"))) void digitalWrite(uint32_t PinNumber, uint32_t Val)
{
  if ((PinNumber>=pinCommonQty()))
  {
    ErrorMsgHandler("digitalWrite(): pin number exceeds the total number of pins");
    return;
  }

  GPIO_TypeDef* port = digitalPinToPort(PinNumber);
  HAL_PinsTypeDef mask = digitalPinToBitMask(PinNumber);

  if (digitalPinPwmIsOn(PinNumber) > 0)
  {
    // if the pin use PWM, disable PWM
    analogWriteStop(PinNumber);
    // and configure pin as output
    HAL_GPIO_PinConfig(port, mask, HAL_GPIO_MODE_GPIO_OUTPUT, HAL_GPIO_PULL_NONE, HAL_GPIO_DS_2MA);
  }
    
  if (Val == HIGH)
    GPIO_SET_PIN(port, mask);
  else
    GPIO_CLEAR_PIN(port, mask);
}

// read pin
__attribute__((noinline, section(".ram_text"))) int digitalRead(uint32_t PinNumber)
{ 
  if ((PinNumber>=pinCommonQty()))
  {
    ErrorMsgHandler("digitalRead(): pin number exceeds the total number of pins");
    return -1;
  }

  if (digitalPinPwmIsOn(PinNumber) > 0)
    // if the pin use PWM, disable PWM
    analogWriteStop(PinNumber); // pin is configured as input here

  return GPIO_READ_PIN(digitalPinToPort(PinNumber), digitalPinToBitMask(PinNumber));
}

// toggle pin
__attribute__((noinline, section(".ram_text"))) void digitalToggle(uint32_t PinNumber)
{
  if ((PinNumber>=pinCommonQty()))
  {
    ErrorMsgHandler("digitalToggle(): pin number exceeds the total number of pins");
    return;
  }

  GPIO_TypeDef* port = digitalPinToPort(PinNumber);
  HAL_PinsTypeDef mask = digitalPinToBitMask(PinNumber);

  if (digitalPinPwmIsOn(PinNumber) > 0)
  {
    // if the pin use PWM, disable PWM
    analogWriteStop(PinNumber);
    // and configure pin as output
    HAL_GPIO_PinConfig(port, mask, HAL_GPIO_MODE_GPIO_OUTPUT, HAL_GPIO_PULL_NONE, HAL_GPIO_DS_2MA);
  }
  // GPIO_TOGGLE_PIN раскрывается в точке вызова и подставляет ->OUTPUT;
  // здесь OUTPUT всё ещё макрос Arduino (0x1) из wiring_constants.h, поэтому
  // прячем его только вокруг этого вызова (найдено на реальной сборке
  // 2026-10-26: push_macro в pins_arduino.h защищает лишь парсинг структуры,
  // не последующие обращения к полю OUTPUT из других файлов).
#pragma push_macro("OUTPUT")
#undef OUTPUT
  GPIO_TOGGLE_PIN(port, mask);
#pragma pop_macro("OUTPUT")
}

#ifdef __cplusplus
}
#endif
