/*
  HardwareSerial.h - Hardware serial library for Wiring
  Copyright (c) 2006 Nicholas Zambetti.  All right reserved.

  This library is free software; you can redistribute it and/or
  modify it under the terms of the GNU Lesser General Public
  License as published by the Free Software Foundation; either
  version 2.1 of the License, or (at your option) any later version.

  This library is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public
  License along with this library; if not, write to the Free Software
  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA

  Modified 28 September 2010 by Mark Sproul
  Modified 14 August 2012 by Alarus
  Modified 3 December 2013 by Matthijs Kooijman
*/

#ifndef HardwareSerial_h
#define HardwareSerial_h

#include <inttypes.h>
#include "stdint.h"
#include "Stream.h"
#include "pins_arduino.h"

// Define constants and variables for buffering incoming serial data.  We're
// using a ring buffer (I think), in which head is the index of the location
// to which to write the next incoming character and tail is the index of the
// location from which to read.
// NOTE: a "power of 2" buffer size is recommended to dramatically
//       optimize all the modulo operations for ring buffers.
// WARNING: When buffer sizes are increased to > 256, the buffer index
// variables are automatically increased in size, but the extra
// atomicity guards needed for that are not implemented. This will
// often work, but occasionally a race condition can occur that makes
// Serial behave erratically. See https://github.com/arduino/Arduino/issues/2405

#if !defined(SERIAL_RX_BUFFER_SIZE)
#define SERIAL_RX_BUFFER_SIZE 64
#endif

#if  (SERIAL_RX_BUFFER_SIZE>256)
typedef uint16_t rx_buffer_index_t;
#else
typedef uint8_t rx_buffer_index_t;
#endif

// Define config for Serial.begin(baud, config);
// parity:      7...4 bits = 0 (no), 2 (even), 3 (odd)
// stop bit:    3 bit = 0 (1 bit), 1 (2 bits)
// data width:  2...0 bits =  4 (7 bit), 6 (8 bit)
// ------- No parity (N) -------
// 1stop bit
#define SERIAL_7N1 0x04   // 00000100
#define SERIAL_8N1 0x06   // 00000110
// 2stop bits
#define SERIAL_7N2 0x0C   // 00001100
#define SERIAL_8N2 0x0E   // 00001110
// ------- Even parity -------
#define SERIAL_7E1 0x24   // 00100100
#define SERIAL_8E1 0x26   // 00100110
#define SERIAL_7E2 0x2C   // 00101100
#define SERIAL_8E2 0x2E   // 00101110
// ------- Odd parity -------
#define SERIAL_7O1 0x34   // 00110100
#define SERIAL_8O1 0x36   // 00110110
#define SERIAL_7O2 0x3C   // 00111100
#define SERIAL_8O2 0x3E   // 00111110

class HardwareSerial : public Stream
{
  protected:
    volatile rx_buffer_index_t _rx_buffer_head;
    volatile rx_buffer_index_t _rx_buffer_tail;
    unsigned char _rx_buffer[SERIAL_RX_BUFFER_SIZE];
  
  private:
    bool isInited = false;
    uint8_t uartNum;
    
  public:
    inline HardwareSerial(uint8_t num) 
    {
      // Set the UART to be used
      uartNum = (num < SERIAL_PORT_QTY) ? num : 0; // UART0 by default
    }
    
    void begin(unsigned long baud) { begin(baud, SERIAL_8N1); }
    void begin(unsigned long, uint8_t);
    void end();
    virtual int available(void);
    virtual int availableForWrite(void);
    virtual int peek(void);
    virtual int read(void);
    virtual void flush(void);
    size_t write(uint8_t) override;
    inline size_t write(unsigned long n)  { return write((uint8_t)n); }
    inline size_t write(long n)           { return write((uint8_t)n); }
    inline size_t write(unsigned int n)   { return write((uint8_t)n); }
    inline size_t write(int n)            { return write((uint8_t)n); }
    size_t write(const uint8_t *buffer, size_t size) override;
    using Print::write; // pull in write(str)
    operator bool() { return isInited; }

    inline void rx_complete_irq(void) __attribute__((always_inline, optimize("O3")));
};

extern HardwareSerial Serial0;
#if SERIAL_PORT_QTY > 1
extern HardwareSerial Serial1;
#endif
// default interface
extern HardwareSerial& Serial;

extern void serialEventRun(void) __attribute__((weak));

#endif
