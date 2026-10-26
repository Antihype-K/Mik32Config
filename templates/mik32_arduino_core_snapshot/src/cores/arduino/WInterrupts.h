#ifndef _WIRING_INTERRUPTS_
#define _WIRING_INTERRUPTS_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include "stdbool.h"

// enable/disable interrupts
void interrupts(void);
void noInterrupts(void);
bool isInterruptsEnabled(void);

// attach/detach interrupt to pin
void attachInterrupt(uint8_t interruptNum, void (*userFunc)(void), int mode);
void detachInterrupt(uint8_t interruptNum);

// enable/disable single external interrupt by it's number
void disableInterrupt(uint8_t interruptNum);
void enableInterrupt(uint8_t interruptNum);

#ifdef __cplusplus
}
#endif

#endif /* _WIRING_INTERRUPTS_ */
