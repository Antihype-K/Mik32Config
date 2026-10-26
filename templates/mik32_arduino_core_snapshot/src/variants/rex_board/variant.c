/**
 *******************************************************************************
 * Copyright (c) 2021 Nanjing Qinheng Microelectronics Co., Ltd.
 * All rights reserved.
 *
 * This software component is licensed by WCH under BSD 3-Clause license,
 * the "License"; You may not use this file except in compliance with the
 * License. You may obtain a copy of the License at:
 *                        opensource.org/licenses/BSD-3-Clause
 *
 *******************************************************************************
 */

#include "pins_arduino.h"
#include "mik32_hal_adc.h"
#include "wiring_analog.h"
#include "wiring_LL.h"

/**
 * @brief Determines the address of the port by the board pin number to which this pin belongs on the MCU
 * @return The address of the port corresponding to the board pin number. Can return 0 if the pin not exists
 */

 __attribute__((noinline, section(".ram_text"))) GPIO_TypeDef *digitalPinToPort(uint32_t digitalPinNumber)
{
  if (digitalPinNumber >= pinCommonQty())
  {
    ErrorMsgHandler("digitalPinToPort(): pin number exceeds the total number of pins");
    return NULL;
  }

  if (digitalPinNumber <= P0_15)
    return GPIO_0;
  else if ((digitalPinNumber >= P1_0) && (digitalPinNumber <= P1_15))
    return GPIO_1;
  else if ((digitalPinNumber >= P2_0) && (digitalPinNumber < PINS_COMMON_QTY))
    return GPIO_2;
  else
    return NULL;
}

// determines the pin address inside the port by the board pin number
__attribute__((noinline, section(".ram_text"))) HAL_PinsTypeDef digitalPinToBitMask(uint32_t digitalPinNumber)
{
  if (digitalPinNumber >= PINS_COMMON_QTY)
  {
    ErrorMsgHandler("digitalPinToBitMask(): pin number exceeds the total number of pins");
    return NOT_A_PIN;
  }

  // P2_0..P2_7 -> бит 0..7 физического пина GPIO_2 (digitalPinNumber - P2_0),
  // а не & 0xF от самого значения enum — после добавления P2_0..P2_5 перед
  // P2_6/P2_7 (были раньше единственными значениями P2_x) старое "+6 & 0xF"
  // совпадало с физическим номером только для P2_6/P2_7 случайно.
  if (digitalPinNumber >= P2_0)
    return (HAL_PinsTypeDef)(1 << ((digitalPinNumber - P2_0) & 0xF));
  else
    return (HAL_PinsTypeDef)(1 << (digitalPinNumber & 0xF));
}

// the function returns a reference to the OUTPUT address of the GPIO register
volatile uint32_t *portOutputRegister(GPIO_TypeDef *GPIO_x)
{
  // ->OUTPUT_ — опечатка в elbear_arduino_bsp (поле на самом деле OUTPUT,
  // без подчёркивания); OUTPUT прячется локально, т.к. wiring_constants.h
  // (через pins_arduino.h) определяет его как макрос 0x1 — то же, что и
  // digitalToggle() в wiring_digital.c (найдено на реальной сборке
  // 2026-10-26).
#pragma push_macro("OUTPUT")
#undef OUTPUT
  return &GPIO_x->OUTPUT;
#pragma pop_macro("OUTPUT")
}

// the function returns a reference to the STATE address of the GPIO register
volatile uint32_t *portInputRegister(GPIO_TypeDef *GPIO_x)
{
  return &GPIO_x->STATE;
}

// ---------------------- ADC ---------------------- //
// determines the ADC channel number by the board pin number
uint32_t analogInputToChannelNumber(uint32_t PinNumber)
{
  uint32_t adcChannel = 0;

  switch (PinNumber)
  {
  case PIN_A0:
    adcChannel = ADC_CHANNEL0;
    break;
  case PIN_A1:
    adcChannel = ADC_CHANNEL1;
    break;
  case PIN_A2:
    adcChannel = ADC_CHANNEL2;
    break;
  case PIN_A3:
    adcChannel = ADC_CHANNEL3;
    break;
  case PIN_A4:
    adcChannel = ADC_CHANNEL4;
    break;
  case PIN_A5:
    adcChannel = ADC_CHANNEL5;
    break;
  case PIN_A6:
    adcChannel = ADC_CHANNEL6;
    break;
  case PIN_A7:
    adcChannel = ADC_CHANNEL7;
    break;
  default:
    adcChannel = NC;
  }
  return adcChannel;
}

// ---------------------- PWM ---------------------- //
// return 1 if digitalPin configured as pwm and enabled, 
// 0 if pwm on pin is disabled, 
// -1 if pin doesn't support pwm
__attribute__((noinline, section(".ram_text"))) int8_t digitalPinPwmIsOn(uint8_t digitalPin)
{
  if (digitalPinHasPWM(digitalPin))
  {
    uint8_t config = 0;
    if ((digitalPin & 16) == 0)
      config = PIN_GET_PAD_CONFIG(PORT_0_CFG, (digitalPin & 0x3));
    else
      config = PIN_GET_PAD_CONFIG(PORT_1_CFG, (digitalPin & 0x3));

    if (config == 2)
      return 1; // pwm is on
    else
      return 0; // pwm is off
  }
  return -1;    // pin doesn't support pwm
}

// function is used only if digitalPinHasPWM() is true
TIMER32_TypeDef *pwmPinToTimer(uint32_t digPinNumber)
{
  if (digPinNumber <= P0_15)
    return TIMER32_1;
  else if ((digPinNumber >= P1_0) && (digPinNumber <= P1_15))
    return TIMER32_2;
  else
    return NULL;
}

// function is used only if digitalPinHasPWM() is true
HAL_TIMER32_CHANNEL_IndexTypeDef pwmPinToTimerChannel(uint32_t digPinNumber)
{
  switch (digPinNumber & 0x3)
  {
  case 0:
    return TIMER32_CHANNEL_0;
  case 1:
    return TIMER32_CHANNEL_1;
  case 2:
    return TIMER32_CHANNEL_2;
  case 3:
    return TIMER32_CHANNEL_3;
  default:
    return 255;
  }
}

// ---------------------- interrupts ---------------------- //
// interrupt table is stored in ram to improve performance
// index = interrupt number. In each row {digitalPinNumber, IntLineValue, IntMuxValue}
uint8_t interruptInfo[EXTERNAL_INTERRUPTS_QTY][3] =
{
  {P0_8,  GPIO_LINE_0, GPIO_MUX_LINE_0_PORT0_8},  // INT0
  {P1_4,  GPIO_LINE_4, GPIO_MUX_LINE_4_PORT1_4},  // INT1
  {P1_5,  GPIO_LINE_5, GPIO_MUX_LINE_5_PORT1_5},  // INT2
  {P1_6,  GPIO_LINE_6, GPIO_MUX_LINE_6_PORT1_6},  // INT3
  {P1_9,  GPIO_LINE_1, GPIO_MUX_LINE_1_PORT1_9},  // INT4
  {P1_10, GPIO_LINE_2, GPIO_MUX_LINE_2_PORT1_10}, // INT5
  {P1_15, GPIO_LINE_3, GPIO_MUX_LINE_3_PORT1_15}, // INT6
  {P2_7,  GPIO_LINE_7, GPIO_MUX_LINE_7_PORT2_7},  // INT7
};

int8_t digitalPinToGpioIntMux(uint8_t digPinNumber)
{
  for (uint8_t i = 0; i < EXTERNAL_INTERRUPTS_QTY; i++)
  {
    if (interruptInfo[i][0] == digPinNumber)
      return interruptInfo[i][2];
  }
  return NOT_AN_INTERRUPT;
}
int8_t digitalPinToGpioIntLine(uint8_t digPinNumber)
{
  for (uint8_t i = 0; i < EXTERNAL_INTERRUPTS_QTY; i++)
  {
    if (interruptInfo[i][0] == digPinNumber)
      return interruptInfo[i][1];
  }
  return NOT_AN_INTERRUPT;
}

int8_t gpioIntLineToInterrupt(uint32_t gpioIntLine)
{
  for (uint8_t i = 0; i < EXTERNAL_INTERRUPTS_QTY; i++)
  {
    if (interruptInfo[i][1] == gpioIntLine)
      return i;
  }
  return NOT_AN_INTERRUPT;
}

int8_t digitalPinToInterrupt(uint32_t digPinNumber)
{
  for (uint8_t i = 0; i < EXTERNAL_INTERRUPTS_QTY; i++)
  {
    if (interruptInfo[i][0] == digPinNumber)
      return i;
  }
  return NOT_AN_INTERRUPT;
}

// ---------------------- SPI ---------------------- //
void spi_onBegin(uint8_t spiNum)
{
  // On mik32 spi0 needs pin 0.3, spi1 needs pin 1.3 for correct work.
  // This pins also can be used as pwm. If pwm is working when spi begins, we should stop the pwm channel

  // get pin NSS_IN config
  uint8_t config;
  if (spiNum == 1)
    config = PIN_GET_PAD_CONFIG(PORT_1_CFG, PIN_MASK_TO_PIN_NUMBER(GPIO_PIN_3));
  else
    config = PIN_GET_PAD_CONFIG(PORT_0_CFG, PIN_MASK_TO_PIN_NUMBER(GPIO_PIN_3));

  if(config == 2)  // timer for pwm
  {
    if (spiNum == 1)
    {
      analogWriteStop(P1_3);
      ErrorMsgHandler("analogWrite(): P1_3 cannot be used as PWM pin while SPI is running");
    }
    else
    {
      analogWriteStop(P0_3);
      ErrorMsgHandler("analogWrite(): P0_3 cannot be used as PWM pin while SPI1 is running");
    }
  }
}