/*
  HardwareSerial.cpp - Hardware serial library for Wiring
*/

#include "HardwareSerial.h"
// uart_lib.h/mik32_hal_irq.h объявляют свои функции без extern "C" — в
// .cpp-файле без явной обёртки компилятор дал бы им C++-линковку, а
// определения собраны с C-линковкой: undefined reference к UART_Init()/
// HAL_EPIC_MaskLevelSet() на линковке (тот же класс бага, что и
// HAL_Init() в board.cpp, найдено на реальной сборке 2026-10-26).
extern "C" {
#include <uart_lib.h>
#include "mik32_hal_irq.h"
}
#include "wiring_LL.h"

// HardwareSerial class objects for use in Arduino IDE
HardwareSerial Serial0(0);
#if SERIAL_PORT_QTY > 1
HardwareSerial Serial1(1);
#endif
// default interface
HardwareSerial& Serial = DEFAULT_SERIAL;


void serialEvent() __attribute__((weak));
bool Serial0_available() __attribute__((weak));

void serialEventRun(void)
{
  if (Serial0_available && serialEvent && Serial0_available()) 
    serialEvent();
}


// Public Methods
void HardwareSerial::begin(unsigned long baud, uint8_t config)
{
  // find the frequency divider (F_CPU is in the preprocessor defines)
  uint32_t brr = (uint32_t)(F_CPU/baud); 
  if (brr < 16) // the driver says that the divisor must be at least 16
    brr = 16;

  // parse config
  // RXNEIE добавляется ПОСЛЕ UART_Init() (см. ниже), не здесь — найдено на
  // реальной сборке 2026-10-26: если RXNEIE взведён уже на входе в
  // UART_Init(), цикл ожидания REACK/TEACK (uart_lib.c) зависает навечно;
  // без RXNEIE в начальном CONTROL1 (ровно как в проверенном рабочем
  // C/HAL-тесте c_uart_sanity_check, где rxneie=Disable) квитирование
  // проходит нормально.
  uint32_t reg1config = UART_CONTROL1_RE_M | UART_CONTROL1_TE_M;
  uint32_t reg2config = 0;
  uint32_t reg3config = 0;

  // first look at parity, because the parity bit is included in the data length
  bool useParity = true;                    
  if ((config >> 4) == 0)       // no - everything is zero, don't set anything in reg1config
    useParity = false;
  else if ((config >> 4) == 2)  // even
    reg1config |= UART_CONTROL1_PCE_M;
  else if ((config >> 4) == 3)  // odd
    reg1config |= (UART_CONTROL1_PCE_M | UART_CONTROL1_PS_M);

  // data length
  if (((config & 0x07) == 4) && (useParity == false))       // 7 bit + no parity
    // 7 bits without parity - configure 7 bits
    reg1config |= UART_CONTROL1_M_7BIT_M;
  else if (((config & 0x07) == 6) && (useParity == true))   // 8 bit + parity
    // 8 bits with parity - configure 9 bits
    reg1config |= UART_CONTROL1_M_9BIT_M;
  // if 7 bits with parity or 8 bits without parity - do not change anything, 
  // leaving the data length selected as 8 bits

  // stop bit                       // stop bit = 1 - everything is zero, don't set anything
  if  ((config >> 3) & (0x01 == 1)) // stop bit = 2
    reg2config |= UART_CONTROL2_STOP_1_M;

  // turn on the receiver and transmitter, apply the parsed config
  if (uartNum == 0)
  {
    UART_Init(UART_0, brr, reg1config, reg2config, reg3config);
    UART_0->CONTROL1 |= UART_CONTROL1_RXNEIE_M;
    // Enable level-based interrupts for the EPIC_UART_0 line, we have only receive interrupt enabled
    HAL_EPIC_MaskLevelSet(HAL_EPIC_UART_0_MASK);
    isInited = true;
  }
  else if (uartNum == 1)
  {
    UART_Init(UART_1, brr, reg1config, reg2config, reg3config);
    UART_1->CONTROL1 |= UART_CONTROL1_RXNEIE_M;
    // Enable level-based interrupts for the EPIC_UART_1 line, we have only receive interrupt enabled
    HAL_EPIC_MaskLevelSet(HAL_EPIC_UART_1_MASK);
    isInited = true;
  }
}

void HardwareSerial::end()
{
  if (isInited)
  {
    // deinit uart and disable uart interrupt
    if (uartNum == 0)
    {
      // wait for the data to be sent if necessary
      UART_WaitTransmission(UART_0); // переименована в установленном framework-mik32v2-sdk
      // shortcut: UART_Deinit нет в установленном framework-mik32v2-sdk
      // (была в старом uart_lib.h elbear_arduino_bsp), end() не реально
      // деинициализирует периферию — апгрейд, если понадобится повторная
      // Serial.begin() на другой скорости без ресета платы.
      HAL_EPIC_MaskLevelClear(HAL_EPIC_UART_0_MASK);
      isInited = false;
    }
    else if (uartNum == 1)
    {
      // wait for the data to be sent if necessary
      UART_WaitTransmission(UART_1); // переименована в установленном framework-mik32v2-sdk
      HAL_EPIC_MaskLevelClear(HAL_EPIC_UART_1_MASK);
      isInited = false;
    }

    // reset buffer indices
    _rx_buffer_head = _rx_buffer_tail = 0;    
  }
}

int HardwareSerial::available(void)
{
  // free space in buffer
  return ((unsigned int)(SERIAL_RX_BUFFER_SIZE + _rx_buffer_head - _rx_buffer_tail)) % SERIAL_RX_BUFFER_SIZE;
}

int HardwareSerial::availableForWrite(void)
{
  // don't use buffer
  return -1;
}

void HardwareSerial::rx_complete_irq(void)
{
  // which UART to use
  UART_TypeDef* uart = (uartNum == 1) ? UART_1 : UART_0;
  rx_buffer_index_t i;
  unsigned char c;
  // while there is something to receive, put the data into the buffer
  // and edit the buffer index
  while(!UART_IS_RX_FIFO_EMPTY(uart))
  {
    c = UART_READ_BYTE(uart);
    // find next index in buffer with upper limit
    i = (unsigned int)(_rx_buffer_head + 1)%SERIAL_RX_BUFFER_SIZE;
    if (i != _rx_buffer_tail)
    {
      // write if there is space in the buffer
      _rx_buffer[_rx_buffer_head] = c;
      _rx_buffer_head = i;
    }
  }
  // ORE/NF/FE не чистятся чтением RXDATA (в отличие от RXNE) — write-1-clear,
  // как в эталонном HAL_USART_IRQHandler (mik32_hal_usart.c:420/431). Без этого
  // ORE висит навсегда после первого overrun-а и, по документированному багу
  // (uart_lib.c:4-11), блокирует взведение TC — печать нескольких байт подряд
  // (println) зависала намертво на реальной плате 2026-10-26, одиночный write() —
  // не успевал словить overrun и проходил.
  uart->FLAGS |= (UART_FLAGS_ORE_M | UART_FLAGS_NF_M | UART_FLAGS_FE_M);
}

// wrapper for use in С-files
extern "C" void __attribute__((optimize("O3"))) serial_interrupt_handler(uint8_t uartNumInt) 
{
  if (uartNumInt == 0)
  {
    Serial0.rx_complete_irq();
  }
  else if ((uartNumInt == 1) && (SERIAL_PORT_QTY > 1))
  {
    Serial1.rx_complete_irq();
  }
}

int HardwareSerial::peek(void)
{
  if (_rx_buffer_head == _rx_buffer_tail) // nothing to read
    return -1;
  else
    return _rx_buffer[_rx_buffer_tail];   // return first element 
}

int HardwareSerial::read(void)
{
  // if there is nothing to read, return -1
  if (_rx_buffer_head == _rx_buffer_tail)
    return -1;
  else 
  {
    // return first element and edit the buffer index
    unsigned char c = _rx_buffer[_rx_buffer_tail];
    _rx_buffer_tail = (uint8_t)(_rx_buffer_tail + 1)%SERIAL_RX_BUFFER_SIZE;
    return (int)c;
  }
}

// write byte
size_t HardwareSerial::write(uint8_t c)
{
  if (isInited)
  {
    if (uartNum == 0)
    {
      UART_WriteByte(UART_0, c);
      UART_WaitTransmission(UART_0);
    }
    else if (uartNum == 1)
    {
      UART_WriteByte(UART_1, c);
      UART_WaitTransmission(UART_1);
    }
    return 1;
  }
  else
    return 0;
}

// write bytes buffer
size_t HardwareSerial::write(const uint8_t *buffer, size_t size)
{
  if (isInited)
  {
    size_t n = 0;
    while (size--) 
    {
      if (HardwareSerial::write(*buffer++))
        n++; 
      else 
        break;
    }
    return n;
  }
  else
    return 0;
}

void HardwareSerial::flush()
{
  // wait for the data transfer complete
  if (uartNum == 0)
    UART_WaitTransmission(UART_0); // переименована в установленном framework-mik32v2-sdk
  else if (uartNum == 1)
    UART_WaitTransmission(UART_1); // переименована в установленном framework-mik32v2-sdk
}
