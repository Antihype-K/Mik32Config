#include <inttypes.h>
#include "mik32_hal_irq.h" 

#include "pins_arduino.h"
#include "wiring_digital.h"
#include "WInterrupts.h"
#include "wiring_LL.h" 

// interrupts are enabled by default after setup()
static bool intIsEnabled = true;

typedef void (*voidFuncPtr)(void);

// empty irq handler
static void nothing(void) 
{
  ;
}

// enable global interrupts
void interrupts(void)
{
  GLOBAL_IRQ_ENABLE();
  intIsEnabled = true;
}

// disable global interrupts
void noInterrupts(void)
{
  GLOBAL_IRQ_DISABLE();
  intIsEnabled = false;
}

bool isInterruptsEnabled(void)
{
  return intIsEnabled;
}
// we can provide no more than 8 interrupts on gpio at the same time
static volatile voidFuncPtr intFunc[EXTERNAL_INTERRUPTS_QTY] = 
{
#if EXTERNAL_INTERRUPTS_QTY > 7
  nothing,
#endif
#if EXTERNAL_INTERRUPTS_QTY > 6
  nothing,
#endif
#if EXTERNAL_INTERRUPTS_QTY > 5
  nothing,
#endif
#if EXTERNAL_INTERRUPTS_QTY > 4
  nothing,
#endif
#if EXTERNAL_INTERRUPTS_QTY > 3
  nothing,
#endif
#if EXTERNAL_INTERRUPTS_QTY > 2
  nothing,
#endif
  nothing,
  nothing,
};

void attachInterrupt(uint8_t interruptNum, void (*userFunc)(void), int mode) 
{
  // if the interrupt number does not exceed the total number
  if(interruptNum < EXTERNAL_INTERRUPTS_QTY) 
  {
    intFunc[interruptNum] = userFunc; // save pointer to irq handler
    
    // init pin as input without pullups
    pinMode(interruptToDigitalPin(interruptNum), INPUT);
    
    // interrupt line initialization
    HAL_GPIO_InterruptMode halMode;
    if      (mode == LOW)     halMode = GPIO_INT_MODE_LOW;
    else if (mode == HIGH)    halMode = GPIO_INT_MODE_HIGH;
    else if (mode == CHANGE)  halMode = GPIO_INT_MODE_CHANGE;
    else if (mode == FALLING) halMode = GPIO_INT_MODE_FALLING;
    else if (mode == RISING)  halMode = GPIO_INT_MODE_RISING;
    else  return;
    HAL_GPIO_InitInterruptLine(interruptToGpioIntMux(interruptNum), halMode);
    
    // turn on the mask in EPIC
    if ((halMode == GPIO_INT_MODE_LOW) || (halMode == GPIO_INT_MODE_HIGH))
      // by level
      HAL_EPIC_MaskLevelSet(HAL_EPIC_GPIO_IRQ_MASK);
    else
      // by edge
      HAL_EPIC_MaskEdgeSet(HAL_EPIC_GPIO_IRQ_MASK);
  }
  else
    ErrorMsgHandler("attachInterrupt(): incorrect interrupt number\n\r");
}

void detachInterrupt(uint8_t interruptNum) 
{
  if(interruptNum < EXTERNAL_INTERRUPTS_QTY) 
  {
    // disable the interrupt in line
    HAL_GPIO_DeInitInterruptLine(interruptToGpioIntLine(interruptNum));
    intFunc[interruptNum] = nothing;

    // we don't change anything in the EPIC controller, in case there are still configured lines left
  }
}

// disable single interrupt
void disableInterrupt(uint8_t interruptNum)
{
  if(interruptNum < EXTERNAL_INTERRUPTS_QTY) 
    // disable gpio interrupt line
    GPIO_IRQ_LINE_DISABLE(interruptToGpioIntLine(interruptNum));
}

// enable single interrupt
void enableInterrupt(uint8_t interruptNum)
{
  if(interruptNum < EXTERNAL_INTERRUPTS_QTY) 
    // enable gpio interrupt line
    GPIO_IRQ_LINE_ENABLE(interruptToGpioIntLine(interruptNum));
}

// common gpio interrupt handler 
void __attribute__((noinline, section(".ram_text"), optimize("O3"))) gpio_interrupt_handler(void)
{
  // go through all the interrupts and call the handler for the triggered line
  for (uint8_t i = 0; i < EXTERNAL_INTERRUPTS_QTY; i++)
  {
    if (GPIO_IRQ_LINE_STATE(interruptToGpioIntLine(i)))
      intFunc[i]();
  }
  GPIO_IRQ_CLEAR_ALL();
}