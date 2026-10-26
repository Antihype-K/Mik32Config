
#include "uart_lib.h"

// TC может не взвестись (например, если RX-овэрран блокирует состояние UART,
// когда приём включён, а обработчик прерывания ещё не запущен — найдено на
// реальной сборке Arduino-демо 2026-10-26: передача зависала намертво именно
// с включённым RE/RXNEIE и пропадала при RE=0). HAL_Micros() (mik32_hal.c) —
// weak-заглушка, всегда возвращает 0, ничто её не переопределяет ни в одной
// ветке проекта — таймаут на её основе (как в xputc()/HAL_USART_Transmit())
// никогда не срабатывает. Используем счётчик итераций вместо реального
// времени — не зависит от рабочего таймера.
#define UART_LIB_TC_TIMEOUT_ITERATIONS 1000000UL

__attribute__((weak)) void HAL_UART_MspInit(UART_TypeDef* uart)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0}; 

    if (uart == UART_0)
    {
        __HAL_PCC_UART_0_CLK_ENABLE();
        GPIO_InitStruct.Pin = GPIO_PIN_5 | GPIO_PIN_6;
        GPIO_InitStruct.Mode = HAL_GPIO_MODE_SERIAL;
        GPIO_InitStruct.Pull = HAL_GPIO_PULL_NONE;
        HAL_GPIO_Init(GPIO_0, &GPIO_InitStruct);
    }

    if (uart == UART_1)
    {
        __HAL_PCC_UART_1_CLK_ENABLE();
        GPIO_InitStruct.Pin = GPIO_PIN_8 | GPIO_PIN_9;
        GPIO_InitStruct.Mode = HAL_GPIO_MODE_SERIAL;
        GPIO_InitStruct.Pull = HAL_GPIO_PULL_NONE;
        HAL_GPIO_Init(GPIO_1, &GPIO_InitStruct);
    }
}

bool UART_Init(UART_TypeDef* uart, 
    uint32_t divider, uint32_t control1,
    uint32_t control2, uint32_t control3)
{
    HAL_UART_MspInit(uart);
    
    bool ready;
    uint32_t flags;
    
    if (divider < 16)
    {
        return false;
    }
      
    uart->CONTROL1 = 0;
    uart->DIVIDER = divider;
    uart->FLAGS = 0xFFFFFFFF;
    
    uart->CONTROL2 = control2;
    uart->CONTROL3 = control3;
    // UE включается ОТДЕЛЬНОЙ записью, а RE/TE/прочие биты — следующей
    // (не одной записью control1|UE), ровно как в проверенном рабочем
    // HAL_USART_Init() (mik32_hal_usart.c: __HAL_USART_Enable() отдельно,
    // потом CONTROL1 |= TE_M/RE_M) — найдено на реальной сборке
    // Arduino-демо 2026-10-26: совмещённая запись UE+RE+TE в один регистр
    // не давала взвестись REACK/TEACK.
    uart->CONTROL1 = UART_CONTROL1_UE_M;
    uart->CONTROL1 |= control1;

    do
    {
        flags = uart->FLAGS;
        ready = true;
        if(control1 & UART_CONTROL1_RE_M)
        {
            ready = ready && ((flags & UART_FLAGS_REACK_M) != 0);
        }
        if(control1 & UART_CONTROL1_TE_M)
        {
            ready = ready && ((flags & UART_FLAGS_TEACK_M) != 0);
        }
    }
    while (!ready);
    
    return true;
}


bool UART_IsTransmissionFinished(UART_TypeDef* uart)
{
    if ((uart->FLAGS & UART_FLAGS_TC_M) == 0)
    {
        return false;
    }
    else
    {
        return true;
    }
}

void UART_WaitTransmission(UART_TypeDef* uart)
{
    volatile uint32_t guard = UART_LIB_TC_TIMEOUT_ITERATIONS;
    while (!UART_IsTransmissionFinished(uart) && guard--) ;
}

bool UART_IsTxBufferFreed(UART_TypeDef* uart)
{
    return ((uart->FLAGS & UART_FLAGS_TXE_M) != 0);
}

void UART_WriteByte(UART_TypeDef* uart, uint16_t byte)
{   
    uart->TXDATA = byte;
}

void UART_Write(UART_TypeDef* uart, uint16_t* byte_array, uint32_t count)
{
    while (count-- > 0)
    {
        UART_WriteByte(uart, *byte_array++);
        UART_WaitTransmission(uart);
    }
}



bool UART_IsRxFifoEmpty(UART_TypeDef* uart)
{
    return ((uart->FLAGS & UART_FLAGS_RXNE_M) == 0);    
}

bool UART_IsRxFifoFull(UART_TypeDef* uart)
{
    return (!UART_IsRxFifoEmpty(uart));    
}

void UART_WaitReceiving(UART_TypeDef* uart)
{   
    while (UART_IsRxFifoEmpty(uart)) ;
}

uint16_t UART_ReadByte(UART_TypeDef* uart)
{    
    return (uint16_t)uart->RXDATA;
}

void UART_Read(UART_TypeDef* uart, uint8_t* byte_array, uint32_t count)
{
    while (count-- > 0)
    {
        UART_WaitReceiving(uart);
        *byte_array++ = UART_ReadByte(uart);
    }    
}


void UART_ClearRxFifo(UART_TypeDef* uart)
{    
    while (!UART_IsRxFifoEmpty(uart)) 
    {
        (void)uart->RXDATA;
    }
}

void __attribute__((weak)) xputc(char c)
{
	UART_WriteByte(UART_0, c);
	UART_WaitTransmission(UART_0);
}
