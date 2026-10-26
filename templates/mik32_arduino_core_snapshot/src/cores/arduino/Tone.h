#ifndef _TONE_H_
#define _TONE_H_

#include "stdint.h"

#define TONE_MIN_FREQUENCY_HZ   4
#define TONE_MAX_FREQUENCY_HZ   40000

/*
* \brief 
* Start tone on specified pin with frequency in range 
* TONE_MIN_FREQUENCY_HZ...TONE_MAX_FREQUENCY_HZ.
* If duration = 0, tone won't stop until noTone() has called
*/
void tone(uint8_t pin, unsigned int frequency, unsigned long duration = 0);

/*
* \brief 
* Stop tone on specified pin
*/
void noTone(uint8_t pin);

#endif /* _TONE_H_ */
