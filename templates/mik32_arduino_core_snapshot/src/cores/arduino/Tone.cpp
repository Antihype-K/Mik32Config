// HAL-заголовки — до Arduino.h (см. комментарий в wiring_digital.c).
#include "mik32_hal_timer16.h"
#include "mik32_hal_irq.h"
#include "wiring_LL.h"
#include "Arduino.h"
#include "pins_arduino.h"

#define PRESCALERS_QTY    7
typedef struct
{
  unsigned int frequency;
  uint8_t prescaler;
  uint32_t period_ticks;
}FrequencyParams_t;
FrequencyParams_t frequencyParams = {0, 0, 0};

// timer handler
Timer16_HandleTypeDef htimer16_1;

// timer_toggle_count:
//  > 0 - duration specified
//  = 0 - stopped
//  < 0 - infinitely (until noTone() called)
volatile long timer_toggle_count;

// tone pin params
volatile int8_t timer_pin = -1;
volatile GPIO_TypeDef* timer_pin_port = 0;
volatile HAL_PinsTypeDef timer_pin_mask = (HAL_PinsTypeDef)0;

bool timerIsOn = false;

static void Timer16_Init(uint8_t prescaler)
{
  htimer16_1.Instance             = TIMER16_1;
  htimer16_1.Clock.Source         = TIMER16_SOURCE_INTERNAL_SYSTEM;
  // prescaler depends on required frequency
  htimer16_1.Clock.Prescaler      = prescaler; 
  htimer16_1.CountMode            = TIMER16_COUNTMODE_INTERNAL;
  htimer16_1.ActiveEdge           = TIMER16_ACTIVEEDGE_RISING;
  htimer16_1.Preload              = TIMER16_PRELOAD_AFTERWRITE;
  htimer16_1.Trigger.Source       = 0; 
  // timer triggers by software
  htimer16_1.Trigger.ActiveEdge   = TIMER16_TRIGGER_ACTIVEEDGE_SOFTWARE;
  htimer16_1.Trigger.TimeOut      = TIMER16_TIMEOUT_DISABLE; 
  htimer16_1.Filter.ExternalClock = TIMER16_FILTER_NONE;
  htimer16_1.Filter.Trigger       = TIMER16_FILTER_NONE;
  htimer16_1.Waveform.Enable      = TIMER16_WAVEFORM_GENERATION_DISABLE;
  htimer16_1.Waveform.Polarity    = TIMER16_WAVEFORM_POLARITY_NONINVERTED;
  htimer16_1.EncoderMode          = TIMER16_ENCODER_DISABLE;
  HAL_Timer16_Init(&htimer16_1);
}

// calculate prescaler and period in timer ticks for required frequancy
static void calcFrequencyParams(FrequencyParams_t* params, unsigned int newFrequency)
{
  // limit the frequency to an acceptable range
  if (newFrequency < TONE_MIN_FREQUENCY_HZ)  newFrequency = TONE_MIN_FREQUENCY_HZ;
  if (newFrequency > TONE_MAX_FREQUENCY_HZ)  newFrequency = TONE_MAX_FREQUENCY_HZ;
    
  // go through the prescalers possible values ​to find the appropriate one.
  // prescaler is set to a value from 0 to 7
  uint32_t tempPeriod;
  for (uint8_t prescaler = 0; prescaler < PRESCALERS_QTY; prescaler++)
  {
    // timer frequency is 2 times greater than the required signal frequency
    tempPeriod = F_CPU / ((1 << prescaler) * 2 * newFrequency);
    if (tempPeriod <= UINT16_MAX) 
    {
      // if period is suitable save the current prescaler and period
      params->frequency = newFrequency;
      params->prescaler = prescaler;
      params->period_ticks = tempPeriod;
      break;
    }
  }
}

// start tone with frequency (in hertz) and duration (in milliseconds)
void tone(uint8_t pin, unsigned int frequency, unsigned long duration)
{
  if ((pin>=pinCommonQty()))
  {
    ErrorMsgHandler("tone(): pin number exceeds the total number of pins");
    return;
  }

  if (!timerIsOn) // if tone is not generated at the moment
  {
    // calculate the parameters necessary to ensure a given frequency if the frequency has changed
    if (frequencyParams.frequency != frequency)
      calcFrequencyParams(&frequencyParams, frequency);
    
    // Calculate the toggle count
    if (duration > 0)
      timer_toggle_count = (duration * (F_CPU/(frequencyParams.period_ticks*(1<<frequencyParams.prescaler))))/1000 - 1;
    else
      timer_toggle_count = -1;

    // save info about the selected tone pin
    timer_pin = pin;
    timer_pin_port = digitalPinToPort(pin);
    timer_pin_mask = digitalPinToBitMask(pin);
    
    // initialize the timer and pin, enable timer interrupt and start the count
    Timer16_Init(frequencyParams.prescaler);
    HAL_EPIC_MaskLevelSet(HAL_EPIC_TIMER16_1_MASK);

    pinMode(pin, OUTPUT);
    GPIO_CLEAR_PIN((GPIO_TypeDef *)timer_pin_port, timer_pin_mask);

    HAL_Timer16_Counter_Start_IT(&htimer16_1, frequencyParams.period_ticks);
    timerIsOn = true;
  }
  // if tone is generated, but the same pin that works now is specified 
  else if (timerIsOn && (pin == timer_pin))
  {
    // change frequency if necessary
    if (frequency != frequencyParams.frequency)
    {
      calcFrequencyParams(&frequencyParams, frequency);
      htimer16_1.Clock.Prescaler = frequencyParams.prescaler;                   // update value in common structure
      HAL_Timer16_SetPrescaler(&htimer16_1, frequencyParams.prescaler);         // the timer is turned off inside the function
      HAL_Timer16_Counter_Start_IT(&htimer16_1, frequencyParams.period_ticks);  // start with a new period
    }
  }
}

// stop tone
void noTone(uint8_t pin)
{
  if ((pin>=pinCommonQty()))
  {
    ErrorMsgHandler("noTone(): pin number exceeds the total number of pins");
    return;
  }

  if (timerIsOn)
  {
    // pin to 0
    GPIO_CLEAR_PIN((GPIO_TypeDef *)timer_pin_port, timer_pin_mask);
    TIM16_DISABLE(htimer16_1);
    EPIC_LEVEL_CLEAR_BY_MASK(HAL_EPIC_TIMER16_1_MASK);
    pinMode(pin, INPUT);                              // deinit pin
    timer_pin = -1;                                   // reset to default
    timerIsOn = false;
  }
}

// irq handler
extern "C" void __attribute__((noinline, section(".ram_text"), optimize("O3"))) tone_interrupt_handler(void)
{
  if (TIM16_GET_ARRM_INT_STATUS(htimer16_1))
  {
    // timer period has passed, change the pin state
    if (timer_toggle_count != 0)
    {
      GPIO_TOGGLE_PIN((GPIO_TypeDef*)timer_pin_port, timer_pin_mask);
      if (timer_toggle_count > 0)
        timer_toggle_count--;
    }
    else
    {
      // turn off if the specified duration has passed
      GPIO_CLEAR_PIN((GPIO_TypeDef *)timer_pin_port, timer_pin_mask);           
      TIM16_DISABLE(htimer16_1);
      EPIC_LEVEL_CLEAR_BY_MASK(HAL_EPIC_TIMER16_1_MASK);
      timerIsOn = false;
    }
  }  
  // reset timer interrupt flags
  TIM16_CLEAR_INT_MASK(htimer16_1, 0xFFFFFFFF);
}