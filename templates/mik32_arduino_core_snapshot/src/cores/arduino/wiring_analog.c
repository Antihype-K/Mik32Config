// HAL-заголовки объявляют регистровые структуры с полями OUTPUT/INPUT —
// должны идти ДО Arduino.h, иначе одноимённые макросы wiring_constants.h
// подставятся в имена полей структуры (найдено на реальной сборке 2026-10-26).
#include "mik32_hal_adc.h"
#include "mik32_hal_gpio.h"
#include "mik32_hal_timer32.h"
#include "Arduino.h"

#ifdef __cplusplus
extern "C" {
#endif


// -------------------------- Analog read -------------------------- //
#define ADC_SAMPLES_QTY           10  // samples quantity for averaging adc results
#define ADC_DEFAULT_RESOLUTION    10  // resolution for arduino compatibility

uint8_t currentResolution = ADC_DEFAULT_RESOLUTION; // resolution used for output results

// structure for ADC channel initialization. Only the channel number 
// changes, everything else is the same
static ADC_HandleTypeDef hadc = 
{
  .Instance    = ANALOG_REG,
  .Init.EXTClb = ADC_EXTCLB_ADCREF,
  .Init.EXTRef = ADC_EXTREF_OFF,
  .Init.Sel    = 0
};

void analogReadResolution(uint8_t resolution)
{
  // resolution limits
  if (resolution > 32) resolution = 32;
  if (resolution < 1)  resolution = 1;

  // save new resolution
  currentResolution = resolution;
}

// initialize the channel, run a single measurement, wait for the result
uint32_t analogRead(uint32_t PinNumber)
{
  uint32_t value = 0;
  uint32_t adcChannel = analogInputToChannelNumber(PinNumber);
  if (adcChannel != NC) 
  {
    additionalPinsInit(PinNumber);

    // init channel 
    hadc.Init.Sel = adcChannel;
    HAL_ADC_Init(&hadc);

    // start the dummy conversion in case another channel was polled before
    HAL_ADC_SINGLE_AND_SET_CH(hadc.Instance, adcChannel);
    HAL_ADC_WaitAndGetValue(&hadc);

    // accumulate results
    uint32_t acc = 0;
    for (uint8_t i = 0; i<ADC_SAMPLES_QTY; i++)
    {
      HAL_ADC_Single(&hadc);
      acc += HAL_ADC_WaitAndGetValue(&hadc);
    }
    // get value by averaging with MCU_ADC_RESOLUTION
    value = acc/ADC_SAMPLES_QTY;

    // map value to selected resolution
    if (currentResolution > MCU_ADC_RESOLUTION)
    {
      // extra least significant bits are padded with zeros
      value = (value << (currentResolution - MCU_ADC_RESOLUTION));
    }
    else
    {
      // extra least significant bits read from the ADC are discarded
      value = (value >> (MCU_ADC_RESOLUTION - currentResolution));
    }
    additionalPinsDeinit(PinNumber);
  }
  else
    ErrorMsgHandler("analogRead(): invalid analog pin number");
  
  return value;
}



// -------------------------- Analog write -------------------------- //
#define PWM_RESOLUTION_DEFAULT    8
#define WRITE_VAL_MAX_DEFAULT     ((1<<PWM_RESOLUTION_DEFAULT) - 1)
#define PWM_TOP_VAL_DEFAULT       32000   // corresponds 1000 Hz

static TIMER32_HandleTypeDef htimer32;
static TIMER32_CHANNEL_HandleTypeDef htimer32_channel;
static uint32_t WriteValMax = WRITE_VAL_MAX_DEFAULT;
static uint32_t pwmTopVal = PWM_TOP_VAL_DEFAULT;
static uint8_t pwmIsInited = 0;


/*
It is recommended to enable the timer in the following order:
- Set the required operating mode. In the INT_MASK register 0;
- Write 0xFFFFFFFF to the INT_CLEAR register;
- Set TIM_EN to 1;
- If you need interrupts from the timer, set the required interrupt mask in the INT_MASK register.
*/
void analogWrite(uint32_t PinNumber, uint32_t writeVal)
{
  if (writeVal >= WriteValMax)
  {
    // if we need max value, use digitalWrite to supply constant level   
    digitalWrite(PinNumber, HIGH);
  }
  else
  {
    // if we need less then max, use pwm
    int8_t pwmState = digitalPinPwmIsOn(PinNumber);

    if (pwmState > 0)      // pin has pwm and pwm is already on
    {
      // we can only change writeVal if it is differ from current value
      TIMER32_TypeDef* timer = pwmPinToTimer(PinNumber);
      uint32_t newOCR = (uint32_t) (((uint64_t)pwmTopVal * writeVal) / WriteValMax); 
      if (timer->CHANNELS[pwmPinToTimerChannel(PinNumber)].OCR != newOCR)
      {
        //  if new ocr differs from current, set new ocr
        timer->CHANNELS[pwmPinToTimerChannel(PinNumber)].OCR = newOCR;
      }
    }
    else if (pwmState == 0) // pin has pwm and pwm is off
    {
      // init pin as pwm
      uint32_t OCRval = (pwmTopVal * writeVal) / WriteValMax;

      // initialization of the required timer
      htimer32.Instance         = pwmPinToTimer(PinNumber);
      htimer32.Top              = pwmTopVal;
      htimer32.State            = TIMER32_STATE_DISABLE;
      htimer32.Clock.Source     = TIMER32_SOURCE_PRESCALER;
      htimer32.Clock.Prescaler  = 0; // Prescaler = 1
      htimer32.InterruptMask    = 0;
      htimer32.CountMode        = TIMER32_COUNTMODE_FORWARD;
      HAL_Timer32_Init(&htimer32);
      
      // gpio init as timer channel pin
      HAL_GPIO_PinConfig(digitalPinToPort(PinNumber), digitalPinToBitMask(PinNumber), 
                        HAL_GPIO_MODE_TIMER_SERIAL, HAL_GPIO_PULL_NONE, HAL_GPIO_DS_2MA);

      htimer32_channel.TimerInstance  = htimer32.Instance;
      htimer32_channel.ChannelIndex   = pwmPinToTimerChannel(PinNumber);
      htimer32_channel.PWM_Invert     = TIMER32_CHANNEL_NON_INVERTED_PWM;
      htimer32_channel.Mode           = TIMER32_CHANNEL_MODE_PWM;
      htimer32_channel.CaptureEdge    = TIMER32_CHANNEL_CAPTUREEDGE_RISING;
      htimer32_channel.OCR            = OCRval;
      htimer32_channel.Noise          = TIMER32_CHANNEL_FILTER_OFF;
      HAL_Timer32_Channel_Init(&htimer32_channel);

      // start timer with initialized channel
      HAL_Timer32_Channel_Enable(&htimer32_channel);
      HAL_Timer32_Value_Clear(&htimer32);
      HAL_Timer32_Start(&htimer32);
      pwmIsInited++; // increase inited channels qty
    }
    else  // pin doesn't have pwm
      ErrorMsgHandler("analogWrite(): invalid pwm pin number");
  }
}

// Set the resolution of analogWrite parameters
void analogWriteResolution(uint8_t resolution)
{
  if ((resolution > 0) && (resolution < 32)) 
    WriteValMax = (1 << resolution) - 1;
  else if (resolution == 32)
    WriteValMax = UINT32_MAX;
  else
    ErrorMsgHandler("analogWriteResolution(): invalid resolution");
}

// Set the frequency of analogWrite
void analogWriteFrequency(uint32_t freq)
{
  if ((freq >= 1) && (freq <= PWM_FREQUENCY_MAX))
    pwmTopVal = F_CPU/freq;
  else
    ErrorMsgHandler("analogWriteFrequency(): invalid frequency");
}

/*
It is recommended to turn off the timer in the following order:
- Write 0 to the INT_MASK register;
- Write 0 to the TIM_PRESCALE register;
- Write 0 to the INT_CLEAR register;
- Set TIM_EN to 0.
*/
// use only if digitalPinPwmIsOn(PinNumber) > 0
void analogWriteStop(uint32_t PinNumber)
{
  if ((pwmIsInited > 0))
  {
    // load the timer address and channel number corresponding to the specified pin
    htimer32.Instance               = pwmPinToTimer(PinNumber);
    htimer32_channel.ChannelIndex   = pwmPinToTimerChannel(PinNumber);
    htimer32.Instance->CHANNELS[htimer32_channel.ChannelIndex].OCR = 0;
    htimer32_channel.TimerInstance  = htimer32.Instance;

    // deinit channel
    HAL_Timer32_Channel_DeInit(&htimer32_channel);
    pwmIsInited--; // decrease inited channels qty

    // stop timer if no inited channels left
    if (pwmIsInited == 0)
    {
      // HAL_Timer32_Deinit нет в установленном framework-mik32v2-sdk (была в
      // старом API elbear_arduino_bsp) — HAL_Timer32_Stop уже останавливает
      // таймер, повторной деинициализации регистров не требуется (найдено
      // на реальной сборке 2026-10-26).
      HAL_Timer32_Stop(&htimer32);
    }

    // config pin as input when timer channel off
    HAL_GPIO_PinConfig(digitalPinToPort(PinNumber), digitalPinToBitMask(PinNumber), 
                      HAL_GPIO_MODE_GPIO_INPUT, HAL_GPIO_PULL_NONE, HAL_GPIO_DS_2MA);
  }
}

#ifdef __cplusplus
}
#endif
