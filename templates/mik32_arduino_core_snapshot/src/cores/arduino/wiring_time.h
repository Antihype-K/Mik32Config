/*
  Copyright (c) 2011 Arduino.  All right reserved.
  Copyright (c) 2013 by Paul Stoffregen <paul@pjrc.com> (delayMicroseconds)

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

#ifndef _WIRING_TIME_H_
#define _WIRING_TIME_H_

#ifdef __cplusplus
extern "C" {
#endif

#define clockCyclesPerMicrosecond()   (F_CPU / 1000000L)
#define clockCyclesToMicroseconds(a)  ((a) / clockCyclesPerMicrosecond())
#define microsecondsToClockCycles(a)  ((a) * clockCyclesPerMicrosecond())

/**
 * \brief Initialize timer for creating delays
 *
 */
void SysTick_Init(void);

/**
 * \brief Returns the number of ticks since the Arduino board began running the current program.

 * \return Number of ticks since the program started
 */
uint64_t SysTick_GetTicks(void);

/**
 * \brief Returns the number of milliseconds since the Arduino board began running the current program.
 *
 * This number will overflow (go back to zero), after approximately 50 days.
 *
 * \return Number of milliseconds since the program started (uint32_t)
 */
uint32_t millis(void);

/**
 * \brief Returns the number of microseconds since the Arduino board began running the current program.
 *
 * This number will overflow (go back to zero), after approximately 70 minutes. 
 *
 * \note There are 1,000 microseconds in a millisecond and 1,000,000 microseconds in a second.
 */
uint32_t micros(void); 

/**
 * \brief Pauses the program for the amount of time (in milliseconds) specified as parameter.
 * (There are 1000 milliseconds in a second.)
 *
 * \param ms the number of milliseconds to pause (uint32_t)
 */
void delay(uint32_t ms);

/**
 * \brief Pauses the program for the amount of time (in microseconds) specified as parameter.
 *
 * \param us the number of microseconds to pause (uint32_t)
 */

void delayMicroseconds(uint16_t us);


#ifdef __cplusplus
}
#endif

#endif /* _WIRING_TIME_H_ */
