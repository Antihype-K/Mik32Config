/*
  pins_arduino.h - Pin definition functions for Arduino
  Part of Arduino - http://www.arduino.cc/

  Copyright (c) 2007 David A. Mellis

  This library is free software; you can redistribute it and/or
  modify it under the terms of the GNU Lesser General Public
  License as published by the Free Software Foundation; either
  version 2.1 of the License, or (at your option) any later version.

  This library is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General
  Public License along with this library; if not, write to the
  Free Software Foundation, Inc., 59 Temple Place, Suite 330,
  Boston, MA  02111-1307  USA
*/

// Вариант для платы "1T Rex Board" (AmurWiFi) — несущий модуль тот же
// ELSOMIK SE (см. CPU-блок в docs/Rex Board.pdf), поэтому нумерация пинов
// идентична вариантам elbear_arduino_bsp для ELSOMIK — переиспользована
// как есть, без переименования. GPIO_2 имеет все 8 физических пинов
// (0-7, датащит МИК32 v2.2.3, Таблица 193 «Назначение выводов», с.320 —
// как и в C-кодогене, parser/gpio/gpio_parser.cpp::maxPinForInstance),
// а не только P2.06/P2.07 — на схеме 1T Rex Board (docs/Rex Board.pdf)
// P2.00-P2.05 разведены на бортовую SPIFI-флешку (DD2), но это
// особенность конкретной платы, а не ограничение кристалла: конфигуратор
// не навязывает этот вывод как занятый, если пользователь явно не включил
// SPIFI-блок (который Arduino-ветка и так целиком отклоняет, см.
// findUnsupportedArduinoFeature) — как и C-кодоген, молча доверяет выбору
// пользователя. На разъёмы платы выведены не все 34 (+6=40) пинов —
// XS1-XS4 дают лишь подмножество, аналоговые A0-A3/XS5-XS7 в v1 не
// подключены — но это вопрос физического доступа, не компиляции.

#ifndef Pins_Arduino_h
#define Pins_Arduino_h

#include "wiring_constants.h"

// К этому моменту wiring_constants.h уже определил OUTPUT/INPUT/HIGH/LOW —
// mik32_hal_gpio.h/mik32_hal_timer32.h объявляют регистровые структуры с
// полями тех же имён, поэтому макросы временно прячутся на время этих двух
// инклудов (push_macro/pop_macro — поддерживается gcc/clang, см. реальную
// ошибку сборки 2026-10-26: "expected unqualified-id before numeric
// constant" в поле OUTPUT структуры GPIO_TypeDef).
#pragma push_macro("OUTPUT")
#pragma push_macro("INPUT")
#pragma push_macro("HIGH")
#pragma push_macro("LOW")
#undef OUTPUT
#undef INPUT
#undef HIGH
#undef LOW

#ifdef __cplusplus
extern "C" {
#endif

#include "mik32_hal_gpio.h"
#include "mik32_hal_timer32.h"

#pragma pop_macro("OUTPUT")
#pragma pop_macro("INPUT")
#pragma pop_macro("HIGH")
#pragma pop_macro("LOW")

// digital pins
typedef enum
{
	P0_0 = 0,         // 0
	P0_1,             // 1
	P0_2,             // 2
	P0_3,             // 3
	P0_4,             // 4
	P0_5,             // 5
	P0_6,             // 6
  P0_7,             // 7
  P0_8,             // 8
  P0_9,             // 9
  P0_10,            // 10
  P0_11,            // 11  // TDI
  P0_12,            // 12  // TCK
  P0_13,            // 13  // TMS
  P0_14,            // 14  // TRST
  P0_15,            // 15  // TDO
  P1_0,             // 16
  P1_1,             // 17
  P1_2,             // 18
  P1_3,             // 19
  P1_4,             // 20
  P1_5,             // 21
  P1_6,             // 22
  P1_7,             // 23
  P1_8,             // 24
  P1_9,             // 25
  P1_10,            // 26
  P1_11,            // 27
  P1_12,            // 28
  P1_13,            // 29
  P1_14,            // 30
  P1_15,            // 31
  P2_0,             // 32
  P2_1,             // 33
  P2_2,             // 34
  P2_3,             // 35
  P2_4,             // 36
  P2_5,             // 37
  P2_6,             // 38
  P2_7,             // 39
  PINS_COMMON_QTY,  // 40
} DigitalPinsTypeDef;
// total number of pins available for initialization
#define pinCommonQty()    (PINS_COMMON_QTY)

// digitalPinToBitMask() при выходе за диапазон возвращает значение-маркер
// ошибки; в elbear_arduino_bsp использовался как есть, но макрос не был
// объявлен ни в одном файле — найдено на реальной сборке 2026-10-26
// ("NOT_A_PIN undeclared" в variant.c). 0 — заведомо невалидная битовая
// маска GPIO (ни один реальный пин не маппится в неё).
#define NOT_A_PIN   ((HAL_PinsTypeDef)0)

// analog pins
#define PIN_A0        (P1_5)
#define PIN_A1        (P1_7)
#define PIN_A2        (P0_2)
#define PIN_A3        (P0_4)
#define PIN_A4        (P0_7)
#define PIN_A5        (P0_9)
#define PIN_A6        (P0_11)
#define PIN_A7        (P0_13)

static const uint8_t A0 = PIN_A0;
static const uint8_t A1 = PIN_A1;
static const uint8_t A2 = PIN_A2;
static const uint8_t A3 = PIN_A3;
static const uint8_t A4 = PIN_A4;
static const uint8_t A5 = PIN_A5;
static const uint8_t A6 = PIN_A6;
static const uint8_t A7 = PIN_A7;


// determines the address of the port by the board pin number to which this pin belongs on the MCU
GPIO_TypeDef* digitalPinToPort(uint32_t digPinNumber);     
// determines the pin address inside the port by the board pin number
HAL_PinsTypeDef digitalPinToBitMask(uint32_t digitalPinNumber);
// the function returns a reference to the OUTPUT address of the GPIO register
volatile uint32_t* portOutputRegister(GPIO_TypeDef* GPIO_x);
// the function returns a reference to the STATE address of the GPIO register
volatile uint32_t* portInputRegister(GPIO_TypeDef* GPIO_x);
// functions are needed for compatibility with other boards
static inline void additionalPinsInit(uint32_t PinNumber) {}
static inline void additionalPinsDeinit(uint32_t PinNumber){}

// UART
// available uarts quantity
#define SERIAL_PORT_QTY 2
#define DEFAULT_SERIAL  Serial0

// ADC
#define MCU_ADC_RESOLUTION    12 // bits
// determines the ADC channel number by the board pin number
uint32_t analogInputToChannelNumber(uint32_t PinNumber);

// PWM
#define PWM_FREQUENCY_MAX         1000000 // Hz
static inline __attribute__((always_inline)) bool digitalPinHasPWM(uint8_t p)
{
  return (p <= P1_15) && ((p & 0xF) < 4);
}
// return true if digitalPin configured as pwm
int8_t digitalPinPwmIsOn(uint8_t digitalPin);
// determines which timer the pin belongs to
TIMER32_TypeDef* pwmPinToTimer(uint32_t digPinNumber);
// determines which timer channel the pin belongs to
HAL_TIMER32_CHANNEL_IndexTypeDef pwmPinToTimerChannel(uint32_t digPinNumber);

// SPI
#define SPI_COMMON_QTY  2
#define DEFAULT_SPI     SPI1

#define PIN_SPI_SS      P1_3
#define PIN_SPI_MOSI    P1_1
#define PIN_SPI_MISO    P1_0
#define PIN_SPI_SCK     P1_2
static const uint8_t SS   = PIN_SPI_SS;
static const uint8_t MOSI = PIN_SPI_MOSI;
static const uint8_t MISO = PIN_SPI_MISO;
static const uint8_t SCK  = PIN_SPI_SCK;

#define PIN_SPI0_SS      P0_3
#define PIN_SPI0_MOSI    P0_1
#define PIN_SPI0_MISO    P0_0
#define PIN_SPI0_SCK     P0_2
static const uint8_t SS1   = PIN_SPI0_SS;
static const uint8_t MOSI1 = PIN_SPI0_MOSI;
static const uint8_t MISO1 = PIN_SPI0_MISO;
static const uint8_t SCK1  = PIN_SPI0_SCK;

// check if pwm is used on spi pins
void spi_onBegin(uint8_t spiNum);
static inline void spi_onEnd(uint8_t spiNum){}

// I2C
#define I2C_COMMON_QTY    2
#define DEFAULT_WIRE      Wire0 // use I2C0 by default
// Wire1 (I2C1)
#define PIN_WIRE_SDA      P1_12
#define PIN_WIRE_SCL      P1_13
static const uint8_t      SDA = PIN_WIRE_SDA;
static const uint8_t      SCL = PIN_WIRE_SCL;
// Wire0 (I2C0)
#define PIN_WIRE_SDA1     P0_9
#define PIN_WIRE_SCL1     P0_10
static const uint8_t      SDA1 = PIN_WIRE_SDA1;
static const uint8_t      SCL1 = PIN_WIRE_SCL1;
// available frequencies
#define WIRE_FREQ_100K    100000
#define WIRE_FREQ_400K    400000
#define WIRE_FREQ_1000K   1000000

// interrupts
#define EXTERNAL_INTERRUPTS_QTY   8
extern uint8_t interruptInfo[EXTERNAL_INTERRUPTS_QTY][3];
// determines the board pin number by interrupt number
#define interruptToDigitalPin(interruptNum)   (interruptInfo[interruptNum][0])
// determines gpio interrupt line by interrupt number
#define interruptToGpioIntLine(interruptNum)  ((uint8_t)interruptInfo[interruptNum][1])
// determines gpio interrupt mux by interrupt number
#define interruptToGpioIntMux(interruptNum)   ((uint8_t)interruptInfo[interruptNum][2])
// determines interrupt number by the board pin number 
int8_t digitalPinToInterrupt(uint32_t digPinNumber);
// determines interrupt number by the gpio interrupt line
int8_t gpioIntLineToInterrupt(uint32_t gpioIntLine);
// determines gpio interrupt mux by the board pin number
int8_t digitalPinToGpioIntMux(uint8_t digPinNumber);
// determines gpio interrupt line by the board pin number
int8_t digitalPinToGpioIntLine(uint8_t digPinNumber);


#ifdef __cplusplus
}
#endif

#endif
