// Вендорено из docs/elron/elbear_arduino_bsp/libraries/SPI/src/SPI.h, без изменений
// (заголовки авторства апстрима сохранены ниже). Только master-режим, как и у Wire —
// см. SPI.cpp для расхождений с установленным framework-mik32v2-sdk.
/*
 * Copyright (c) 2010 by Cristian Maglie <c.maglie@arduino.cc>
 * Copyright (c) 2014 by Paul Stoffregen <paul@pjrc.com> (Transaction API)
 * Copyright (c) 2014 by Matthijs Kooijman <matthijs@stdin.nl> (SPISettings AVR)
 * Copyright (c) 2014 by Andrew J. Kroll <xxxajk@gmail.com> (atomicity fixes)
 * SPI Master library for arduino.
 *
 * This file is free software; you can redistribute it and/or modify
 * it under the terms of either the GNU General Public License version 2
 * or the GNU Lesser General Public License version 2.1, both as
 * published by the Free Software Foundation.
 */

#ifndef _SPI_H_INCLUDED
#define _SPI_H_INCLUDED

#include <Arduino.h>

// mik32_hal_spi.h не оборачивает свои объявления в extern "C" (как и
// mik32_hal_i2c.h) — без этой обёртки компилятор мэнглит имена HAL_SPI_*
// под C++, а сам HAL скомпилирован как чистый C, из-за чего линковщик не
// находит символы (найдено 2026-10-26 при реальной сборке `pio run` с
// вызовом SPI0.transfer() — такой же фикс уже применён в Wire.h для
// utility/twi.h).
extern "C"
{
#include "mik32_hal_spi.h"
}

#define SPI_HAS_TRANSACTION         1
#define SPI_HAS_NOTUSINGINTERRUPT   1
#define SPI_ATOMIC_VERSION          1

#define SPI_DEFAULT_SPEED   4000000

#define SPI_CLOCK_DIV2    0x00
#define SPI_CLOCK_DIV4    0x01
#define SPI_CLOCK_DIV8    0x02
#define SPI_CLOCK_DIV16   0x03
#define SPI_CLOCK_DIV32   0x04
#define SPI_CLOCK_DIV64   0x05
#define SPI_CLOCK_DIV128  0x06
#define SPI_CLOCK_DIV256  0x07

// mode[1] - polarity, mode[0] - phase
#define SPI_MODE0         0b00
#define SPI_MODE1         0b01
#define SPI_MODE2         0b10
#define SPI_MODE3         0b11

class SPISettings {
public:
    uint32_t speedMaximum;
    uint8_t newDataOrder;
    uint8_t newDataMode;
    SPISettings(uint32_t clock, uint8_t bitOrder, uint8_t mode)
        : speedMaximum(clock), newDataOrder(bitOrder), newDataMode(mode) {}
    SPISettings()
        : speedMaximum(4000000), newDataOrder(MSBFIRST), newDataMode(SPI_MODE0){}
};


class SPIClass
{
private:
  uint8_t _spiNum;
  SPI_HandleTypeDef _hspi = {0};
  uint32_t _speed     = 0;
  uint8_t _dataOrder  = MSBFIRST;
  uint8_t _dataMode   = -1;
  bool _isInited  = false;
  bool _newConfig = false;
  bool _spiInUse  = false;
  uint8_t _interruptMode = 0;
  uint8_t _interruptMask = 0;

  void updateSettings(uint32_t speedMaximum, uint8_t dataOrder, uint8_t dataMode);

public:
  inline SPIClass(uint8_t num)
  {
    _spiNum = (num < SPI_COMMON_QTY) ? num : 1;
  }
  void begin();

  void usingInterrupt(uint8_t interruptNumber);
  void notUsingInterrupt(uint8_t interruptNumber);

  void beginTransaction(SPISettings settings);

  uint8_t   transfer(uint8_t data);
  uint16_t  transfer16(uint16_t data);
  void      transfer(void *buf, size_t count);

  void endTransaction(void);

  void end();

  void setBitOrder(uint8_t bitOrder);
  void setDataMode(uint8_t dataMode);
  void setClockDivider(uint8_t clockDiv);
};

extern SPIClass SPI1;
#if SPI_COMMON_QTY > 1
extern SPIClass SPI0;
#endif
extern SPIClass& SPI;

#endif
