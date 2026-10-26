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
#ifndef _WIRING_PULSE_
#define _WIRING_PULSE_

#include "stdint.h"
#define PULSE_TIMEOUT_DEFAULT   1000000L

/*
* \brief 
* Measures the length (in microseconds) of a pulse on the pin; state is HIGH
* or LOW, the type of pulse to measure.  Works on pulses from 20-30 microseconds
* to 3 minutes in length, but must be called at least a few dozen microseconds
* before the start of the pulse.
*/
unsigned long pulseIn(int pin, int state, unsigned long timeout = PULSE_TIMEOUT_DEFAULT);
unsigned long pulseInLong(int pin, int state, unsigned long timeout = PULSE_TIMEOUT_DEFAULT);

#endif /* _WIRING_PULSE_ */
