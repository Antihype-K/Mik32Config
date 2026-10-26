#include "Arduino.h"

/* Measures the length (in microseconds) of a pulse on the pin; state is HIGH
 * or LOW, the type of pulse to measure.  Works on pulses from 20-30 microseconds
 * to 3 minutes in length, but must be called at least a few dozen microseconds
 * before the start of the pulse.
 */
unsigned long pulseIn(int pin, int state, unsigned long timeout)
{
  // check the pin number
  if ((pin>=pinCommonQty()))
  {
    ErrorMsgHandler("pulseIn(): pin number exceeds the total number of pins");
    return -1;
  }
  if (digitalPinPwmIsOn(pin) > 0)
    // pwm off
    analogWriteStop(pin);

  // target state to hal terms
  GPIO_PinState targetState = (state == LOW) ? GPIO_PIN_LOW : GPIO_PIN_HIGH;
  // get port and pin number
  GPIO_TypeDef* halPort = digitalPinToPort(pin);
  HAL_PinsTypeDef halPin =  digitalPinToBitMask(pin);

  // save initial state
  GPIO_PinState iniState = HAL_GPIO_ReadPin(halPort, halPin);

  // wait for any previous pulse to end
  uint32_t startMicros = micros();
  while (HAL_GPIO_ReadPin(halPort, halPin) == iniState)
  {
    if (micros() - startMicros > timeout)
      return 0;
  }

  // wait for the desired polarity pulse start
  while (HAL_GPIO_ReadPin(halPort, halPin) != targetState) 
  {
    if (micros() - startMicros > timeout)
      return 0;
  }

  // wait for the pulse to stop
  uint32_t start = micros();
  while (HAL_GPIO_ReadPin(halPort, halPin) == targetState) 
  {
    if (micros() - startMicros > timeout)
      return 0;
  }
  return (micros() - start);
}

/* 
 * For backward compatibility
 */
unsigned long pulseInLong(int pin, int state, unsigned long timeout)
{
  return pulseIn(pin, state, timeout);
}
