#ifndef _WIRING_LL_H_
#define _WIRING_LL_H_

#include "mik32_hal_irq.h" 
#include "mik32_hal_gpio.h"
#include "mik32_hal_scr1_timer.h"
#include "mik32_hal_timer16.h"
#include "pad_config.h"

// ----------------- COMMON ----------------- //
// convert pin mask from HAL_PinsTypeDef to pin number
#define PIN_MASK_TO_PIN_NUMBER(mask)    \
({                                      \
  uint8_t pos;                          \
  for (pos = 0; pos < 16; pos++)        \
    if ((mask >> pos) & 1)              \
      break;                            \
  pos;                                  \
})

// ----------------- SYSTICK ----------------- //
// get ticks from systick
#define SYSTICK_GET_TICKS()                       ((uint64_t)(SCR1_TIMER->MTIMEH)<<32 | SCR1_TIMER->MTIME)

// ----------------- UART ----------------- //
#define UART_READ_BYTE(pUart)                     ((uint16_t)pUart->RXDATA)
#define UART_IS_RX_FIFO_EMPTY(pUart)              ((pUart->FLAGS & UART_FLAGS_RXNE_M) == 0)

// ----------------- TIMER16 ----------------- //
#define TIM16_DISABLE(htim16)                     (htim16.Instance->CR &= ~TIMER16_CR_ENABLE_M)
#define TIM16_CLEAR_INT_MASK(htim16, intMask)     (htim16.Instance->ICR = intMask)
#define TIM16_GET_ARRM_INT_STATUS(htim16)         ((bool)((htim16.Instance->ISR & htim16.Instance->IER) & TIMER16_ISR_ARR_MATCH_M))
#define TIM16_DISABLE_INT_BY_MASK(htim16,intMask) (htim16.Instance->IER &= ~(intMask))

// ----------------- EPIC ----------------- //
#define EPIC_LEVEL_SET_BY_MASK(mask)              (EPIC->MASK_LEVEL_SET |= mask)
#define EPIC_LEVEL_CLEAR_BY_MASK(mask)            (EPIC->MASK_LEVEL_CLEAR |= mask)
#define EPIC_CLEAR_ALL()                          (EPIC->CLEAR = 0xFFFFFFFF)

// ----------------- IRQ ----------------- //
#define GLOBAL_IRQ_DISABLE()                      (clear_csr(mie, MIE_MEIE))
#define GLOBAL_IRQ_ENABLE()                       set_csr(mstatus, MSTATUS_MIE); \
                                                  set_csr(mie, MIE_MEIE)

// ----------------- GPIO ----------------- //
#define GPIO_INPUT_MODE_PIN(GPIO_x, pinMask)      ((GPIO_x)->DIRECTION_IN = (pinMask))
#define GPIO_OUTPUT_MODE_PIN(GPIO_x, pinMask)     ((GPIO_x)->DIRECTION_OUT = (pinMask))
#define GPIO_SET_PIN(GPIO_x, pinMask)             ((GPIO_x)->SET = (pinMask))
#define GPIO_CLEAR_PIN(GPIO_x, pinMask)           ((GPIO_x)->CLEAR = (pinMask))
#define GPIO_TOGGLE_PIN(GPIO_x, pinMask)          ((GPIO_x)->OUTPUT ^= pinMask)
#define GPIO_READ_PIN(GPIO_x, pinMask)            (((GPIO_x)->SET & (pinMask)) != (uint32_t)GPIO_PIN_LOW ? GPIO_PIN_HIGH : GPIO_PIN_LOW)
// get pin state by it's number
#define GPIO_GET_PIN_STATE(GPIO_x, pinNumber)     (((GPIO_x)->OUTPUT >> pinNumber) & 0b1)

// ----------------- GPIO IRQ ----------------- //
#define GPIO_IRQ_LINE_ENABLE(lineMask)            ( GPIO_IRQ->ENABLE_SET = (1 << (lineMask >> GPIO_IRQ_LINE_S)) )
#define GPIO_IRQ_LINE_DISABLE(lineMask)           ( GPIO_IRQ->ENABLE_CLEAR = (1 << (lineMask >> GPIO_IRQ_LINE_S)) )
#define GPIO_IRQ_LINE_STATE(lineMask)             ((GPIO_IRQ->INTERRUPT & (1 << (lineMask >> GPIO_IRQ_LINE_S))) != 0)
#define GPIO_IRQ_CLEAR_ALL()                      ( GPIO_IRQ->CLEAR = 0b11111111)

// ----------------- PIN CONFIG ----------------- //
// return config of pin with pinNumber(0...16) in portReg (config, pupd, ds for ports 0...2)
#define PIN_GET_PAD_CONFIG(portReg, pinNumber)          ((PAD_CONFIG->portReg  >> (pinNumber<<1)) & 0b11)
#define PIN_SET_PAD_CONFIG(portReg, pinNumber, value)   (PAD_CONFIG->portReg = (PAD_CONFIG->portReg & (~PAD_CONFIG_PIN_M(pinNumber))) \
                                                        | PAD_CONFIG_PIN(pinNumber, value))  

#endif /* _WIRING_LL_H_ */