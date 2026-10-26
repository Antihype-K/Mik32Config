#include "mik32_hal_irq.h"
#include "wiring_LL.h"

// isr functions
extern void serial_interrupt_handler(uint8_t uartNumInt);
extern void gpio_interrupt_handler(void);
extern void tone_interrupt_handler(void);
void __attribute__((weak)) wire_interrupt_handler(uint8_t num)
{
    // dummy function for case when wire library is not in use
}
void __attribute__((weak)) servo_interrupt_handler(void)
{
    // dummy function for case when servo library is not in use
}
void __attribute__((weak)) IRremote_interrupt_handler(void)
{
    // dummy function for case when IRremote library is not in use
}

bool __attribute__((section(".ram_text"), weak)) ISR(void)
{ 
    /* 
    A dummy function for the case when additional interrupts are not used in the project.
    In the project, you need to create a function of the form:
    extern "C" __attribute__((section(".ram_text"))) bool ISR(void)
    {
        if (EPIC_CHECK_TIMER16_1())
        {
            // timer16 is taken as an example
            if (TIM16_GET_ARRM_INT_STATUS(htimer16_1_))
            {
                // necessary actions
            }
            // reset timer interrupt flags
            TIM16_CLEAR_INT_MASK(htimer16_1_, 0xFFFFFFFF);
        }
        return false;
    }

    If you don't need to call standard trap_handler() code, you can return true from this function
    and trap_handler() code will be missed. But you must carefully clear interrupt flags by yourself 
    in your custom ISR() function.

    libraries required to use this example:
    #include "mik32_hal_timer16.h"
    #include "wiring_LL.h" 
    */
    return false;
}

// ---------------------------------------------- //
void __attribute__((noinline, section(".ram_text"), optimize("O3"))) trap_handler (void)  
{
    // custom interrupt
    if(ISR())
    {
        // reset all interrupts and miss trap_handler() code
        EPIC_CLEAR_ALL();
        return;
    }
        
    // gpio interrupt
    if (EPIC_CHECK_GPIO_IRQ())
        gpio_interrupt_handler();

    // IRremote timer interrupt
    if (EPIC_CHECK_TIMER16_0())
        IRremote_interrupt_handler();

    // tone timer interrupt
    if (EPIC_CHECK_TIMER16_1())
        tone_interrupt_handler();

    // servo timer interrupt
    if (EPIC_CHECK_TIMER16_2())
        servo_interrupt_handler();

    // uart0 interrupt
    if (EPIC_CHECK_UART_0())
        serial_interrupt_handler(0);

    // uart1 interrupt
    if (EPIC_CHECK_UART_1())
        serial_interrupt_handler(1);

    // i2c0 interrupt
    if (EPIC_CHECK_I2C_0())
        wire_interrupt_handler(0);

    // i2c1 interrupt
    if (EPIC_CHECK_I2C_1())
        wire_interrupt_handler(1);

    // reset all interrupts
    EPIC_CLEAR_ALL();
}
