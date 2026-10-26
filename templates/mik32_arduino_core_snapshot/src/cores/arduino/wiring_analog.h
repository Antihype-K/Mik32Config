#ifndef _WIRING_ANALOG_
#define _WIRING_ANALOG_

#ifdef __cplusplus
extern "C" {
#endif

#include "stdint.h"

/*
 * \brief Reads the value from the specified analog pin.
 *
 * \param PinNumber
 *
 * \return Read value from selected pin, if no error.
 */
uint32_t analogRead(uint32_t PinNumber);

/*
 * \brief Set the resolution of adc results. Default is 10 bits (range from 0 to 1023).
 *
 * \param resolution 1...32
 */
void analogReadResolution(uint8_t resolution);

/*
 * \brief Writes an analog value (PWM wave) to a pin.
 *
 * \param PinNumber
 * \param default writeVal is 0...255
 */
void analogWrite(uint32_t PinNumber, uint32_t writeVal);

/*
 * \brief Set the resolution of analogWrite parameters. Default is 8 bits (range from 0 to 255).
 *
 * \param resolution 1...32
 */
void analogWriteResolution(uint8_t resolution);

/*
 * \brief Set the frequency of analogWrite. Applying after calling analogWrite(). 
 * Default is PWM_FREQUENCY (1000) in Hertz.
 *
 * \param freq 1...1000000 Hz
 */
void analogWriteFrequency(uint32_t freq);

/*
 * \brief Stops timer and deinit pwm channel on pin PinNumber
 *
 * \param PinNumber
 */
void analogWriteStop(uint32_t PinNumber);


#ifdef __cplusplus
}
#endif

#endif /* _WIRING_ANALOG_ */
