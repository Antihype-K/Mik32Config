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

#ifndef _WIRING_DIGITAL_
#define _WIRING_DIGITAL_

#ifdef __cplusplus
extern "C" {
#endif

/**
 * \brief Configures the specified pin to behave either as an input or an output.
 *
 * \param PinNumber The number of the pin whose mode you wish to set
 * \param PinMode Either INPUT, INPUT_PULLUP or OUTPUT
 */
void pinMode(uint32_t PinNumber, uint32_t PinMode);

/**
 * \brief Configures the specified pin to behave either as an input or an output
 * using quick macros and without calling checks
 *
 * \param PinNumber The number of the pin whose mode you wish to set
 * \param PinMode Either INPUT, INPUT_PULLUP or OUTPUT
 */
void fastPinMode(uint32_t PinNumber, uint32_t PinMode);

/**
 * \brief Write a HIGH or a LOW value to a digital pin.
 *
 * If the pin has been configured as an OUTPUT with pinMode(), its voltage will be set to the
 * corresponding value: 3.3V for HIGH, 0V (ground) for LOW.
 *
 * \param PinNumber the pin number
 * \param PinMode HIGH or LOW
 */
void digitalWrite(uint32_t PinNumber, uint32_t Val);

/**
 * \brief Reads the value from a specified digital pin, either HIGH or LOW.
 *
 * \param PinNumber The number of the digital pin you want to read (int)
 *
 * \return HIGH or LOW
 */
int digitalRead(uint32_t PinNumber);

/**
 * \brief Toggle the value from a specified digital pin.
 *
 * \param PinNumber The number of the digital pin you want to toggle (int)
 */
void digitalToggle(uint32_t PinNumber);

#ifdef __cplusplus
}
#endif

#endif /* _WIRING_DIGITAL_ */
