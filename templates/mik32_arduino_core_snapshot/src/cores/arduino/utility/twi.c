#include <stdlib.h>
#include <inttypes.h>
#include "Arduino.h"
#include "twi.h"
#include "mik32_hal_irq.h"

// Апстримный twi.c (docs/elron/elbear_arduino_bsp/libraries/Wire/src/utility/twi.c)
// написан под более старую версию HAL: вызывал HAL_I2C_Deinit/HAL_I2C_Get_Interrupts_Status/
// HAL_I2C_Clear_Reload/HAL_I2C_Get_CR1_Content/HAL_I2C_Reset_TXDR_Content/HAL_I2C_Write_TXDR/
// HAL_I2C_Reset_Interrupt_Flag/HAL_I2C_Get_RXDR — ни одной из этих функций нет в установленном
// framework-mik32v2-sdk (проверено grep'ом по заголовку, найдено при вендоринге Wire-библиотеки
// 2026-10-26, та же категория несовместимости, что уже 9 раз ловилась на cores/arduino, см.
// docs/implementations/arduino_codegen.md). Переписано против реально установленного API —
// того же, что уже проверен и HW-подтверждён в codegen/project/i2c_contribution.cpp.
//
// Arduino v1 (arduino_project_codegen.cpp, findUnsupportedArduinoFeature) принимает только
// mode=master — slave целиком отклоняется на этапе валидации конфига, НЕ здесь: slave-приём в
// апстриме асинхронный (прерывание ADDR/RXNE/STOP), а чинить этот путь — отдельная задача
// такого же объёма, что уже потребовалась для NACK-бага в C-ветке I2C (codegen/CLAUDE.md,
// "I2C DMA/IRQ"). Угадывать правильную последовательность сброса флагов под новый HAL здесь
// означает рисковать тем же классом бага, что уже дважды ловился на RTC/Bus Watchdog
// (см. корневой CLAUDE.md, «Не угадывать полярность бита регистра») — поэтому twi_slaveWrite
// переведён на готовую блокирующую HAL_I2C_Slave_Transmit() (единственный путь, который реально
// есть в установленном HAL и не требует ручной работы с регистрами), а twi_interruptHandler
// оставлен пустым: master-режим (twi_init, Mode=MASTER) никогда не включает
// HAL_EPIC_MaskLevelSet() для I2C, поэтому EPIC_CHECK_I2C_0()/1() в trap_handler.c физически не
// сработает — этот код недостижим при текущем ограничении на master-only.

#define TIMEOUT_TICKS   1000000
#define TWI_FREQ_DEF    WIRE_FREQ_100K  // default is 100 kHz

// ---------------------------------------------------------------- //

/*
 * Function twi_init
 * Desc     readys twi pins and sets twi bitrate
 * Input    handler - pointer to wire common handler,
 *          slaveAddress - address of Arduino if working in slave mode
 * Output   none
 */
uint8_t twi_init(WireHandler_TypeDef* handler, uint8_t slaveAddress)
{
  // Common settings
  uint32_t EPICmask;

  if (handler->i2c_num == 0)
  {
    handler->i2c_param.Instance = I2C_0;
    EPICmask = HAL_EPIC_I2C_0_MASK;
  }
  else if (handler->i2c_num == 1)
  {
    handler->i2c_param.Instance = I2C_1;
    EPICmask = HAL_EPIC_I2C_1_MASK;
  }
  else
    return I2C_ERROR;

  handler->i2c_param.Init.DigitalFilter = I2C_DIGITALFILTER_2CLOCKCYCLES;
  handler->i2c_param.Init.AnalogFilter  = I2C_ANALOGFILTER_DISABLE;
  if (slaveAddress == 0)  // if there is no address - master mode
  {
    handler->i2c_param.Init.Mode    = HAL_I2C_MODE_MASTER;
    handler->i2c_param.Init.AutoEnd = I2C_AUTOEND_ENABLE;
  }
  else
  {
    // 7-bit address without any additional bits
    handler->i2c_param.Init.Mode          = HAL_I2C_MODE_SLAVE;
    handler->i2c_param.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE; // stretch is used
    handler->i2c_param.Init.OwnAddress1   = slaveAddress;
  }

  // set the frequency
  if (handler->i2c_param.Init.Mode == HAL_I2C_MODE_MASTER)
    twi_setFrequency(handler, TWI_FREQ_DEF, true);
  else
    twi_setFrequency(handler, 0, true);

  if (HAL_I2C_Init(&(handler->i2c_param)) != HAL_OK)
    return I2C_ERROR;

  // enable interrupts for slave mode
  if (handler->i2c_param.Init.Mode == HAL_I2C_MODE_SLAVE)
  {
    // enable interrupts for addressing, byte reception and transfer completion
    HAL_I2C_InterruptDisable(&(handler->i2c_param), I2C_INTMASK);
    HAL_I2C_InterruptEnable(&(handler->i2c_param), I2C_CR1_ADDRIE_M | I2C_CR1_RXIE_M | I2C_CR1_STOPIE_M);
    // enable interrupt from i2c
    HAL_EPIC_MaskLevelSet(EPICmask);
  }
  handler->isInited = true;
  return I2C_OK;
}

/*
 * Function twi_deinit
 * Desc     disables twi pins
 * Input    handler - pointer to wire common handler
 * Output   none
 */
void twi_deinit(WireHandler_TypeDef* handler)
{
  // HAL_I2C_Deinit() нет в установленном framework-mik32v2-sdk (была в
  // старом API апстрима) — HAL_I2C_Disable() выключает интерфейс (EN=0),
  // этого достаточно для Wire::end() (повторный Wire::begin() заново
  // вызывает HAL_I2C_Init()).
  HAL_I2C_Disable(&(handler->i2c_param));

  // for slave mode disable interrupts from i2c
  if (handler->i2c_param.Init.Mode == HAL_I2C_MODE_SLAVE)
  {
    if (handler->i2c_num == 0)
      HAL_EPIC_MaskLevelClear(HAL_EPIC_I2C_0_MASK);
    else
      HAL_EPIC_MaskLevelClear(HAL_EPIC_I2C_1_MASK);
  }

  handler->isInited = false;
}

/*
 * Function twi_setClock
 * Desc     sets twi bit rate
 * Input    handler - pointer to wire common handler
 *          Clock Frequency - 100000 / 400000 / 1000000,
 *          onInit = true only if function is called from twi_init()
 * Output   I2C_OK - frequency changed, I2C_ERROR - frequency didn't change
 */
uint8_t twi_setFrequency(WireHandler_TypeDef* handler, uint32_t frequency, bool onInit)
{
  uint8_t ret = I2C_OK;

  // change the frequency only during or after bus initialization
  if (handler->isInited || onInit)
  {
    // You can change the frequency only when the interface is turned off
    HAL_I2C_Disable(&(handler->i2c_param));

    if (frequency == WIRE_FREQ_100K)      // 100 kHz
    {
      handler->i2c_param.Clock.PRESC  = 2;
      handler->i2c_param.Clock.SCLDEL = 8;
      handler->i2c_param.Clock.SDADEL = 2;
      handler->i2c_param.Clock.SCLH   = 49;
      handler->i2c_param.Clock.SCLL   = 49;
    }
    else if (frequency == WIRE_FREQ_400K) // 400 kHz
    {
      handler->i2c_param.Clock.PRESC  = 0;
      handler->i2c_param.Clock.SCLDEL = 3;
      handler->i2c_param.Clock.SDADEL = 2;
      handler->i2c_param.Clock.SCLH   = 30;
      handler->i2c_param.Clock.SCLL   = 30;
    }
    else if (frequency == WIRE_FREQ_1000K)// 1000 kHz
    {
      handler->i2c_param.Clock.PRESC  = 0;
      handler->i2c_param.Clock.SCLDEL = 1;
      handler->i2c_param.Clock.SDADEL = 2;
      handler->i2c_param.Clock.SCLH   = 6;
      handler->i2c_param.Clock.SCLL   = 6;
    }
    else if (frequency == 0)              // slave mode
    {
      handler->i2c_param.Clock.PRESC  = 0;
      handler->i2c_param.Clock.SCLDEL = 0;
      handler->i2c_param.Clock.SDADEL = 2;
      handler->i2c_param.Clock.SCLH   = 0;
      handler->i2c_param.Clock.SCLL   = 0;
    }
    else
      // frequency does not change
      ret = I2C_ERROR;

    // write the timings to the register if everything is OK
    if (ret == I2C_OK)
      HAL_I2C_SetClockSpeed(&(handler->i2c_param));

    //turn the interface back only if initialization has already passed
    if (handler->isInited)
      HAL_I2C_Enable(&(handler->i2c_param));
  }
  else
    ret = I2C_ERROR;

  return ret;
}

/*
 * Function twi_masterReadFrom
 * Desc     attempts to become twi bus master and read a
 *          series of bytes from a device on the bus
 * Input    hi2c: pointer to hardware i2c handler
 *          address: 7bit i2c device address
 *          data: pointer to byte array
 *          length: number of bytes to read into array
 *          sendStop: Boolean indicating whether to send a stop at the end
 * Output   number of bytes read
 */
uint8_t twi_masterReadFrom(I2C_HandleTypeDef* hi2c, uint8_t address, uint8_t* data, uint8_t length, uint8_t sendStop)
{
  uint8_t ret = 0;

  // if there are errors left from previous transactions, you need to restart
  // the interface for correct operation
  if (hi2c->ErrorCode != I2C_ERROR_NONE)
    HAL_I2C_Reset(hi2c);

  if (sendStop)   hi2c->Init.AutoEnd = I2C_AUTOEND_ENABLE;
  else            hi2c->Init.AutoEnd = I2C_AUTOEND_DISABLE;

  // put the data directly into the external buffer
  if (HAL_I2C_Master_Receive(hi2c, address, data, length, TIMEOUT_TICKS) == HAL_OK)
    ret = length;

  return ret;
}

/*
 * Function twi_masterWriteTo
 * Desc     attempts to become twi bus master and write a
 *          series of bytes to a device on the bus
 * Input    hi2c: pointer to hardware i2c handler
 *          address: 7bit i2c device address
 *          data: pointer to byte array
 *          length: number of bytes in array
 *          sendStop: boolean indicating whether or not to send a stop at the end
 * Output   0 .. success
 *          1 .. length to long for buffer
 *          2 .. address send, NACK received
 *          3 .. data send, NACK received
 *          4 .. other twi error (lost bus arbitration, bus error, ..)
 *          5 .. timeout
 */
uint8_t twi_masterWriteTo(I2C_HandleTypeDef* hi2c, uint8_t address, uint8_t* data, uint8_t length, uint8_t sendStop)
{
  uint8_t ret = I2C_OK;

  // if there are errors left from previous transactions, you need to restart
  // the interface for correct operation
  if (hi2c->ErrorCode != I2C_ERROR_NONE)
    HAL_I2C_Reset(hi2c);

  if (sendStop)   hi2c->Init.AutoEnd = I2C_AUTOEND_ENABLE;
  else            hi2c->Init.AutoEnd = I2C_AUTOEND_DISABLE;

  // take data from an external buffer
  HAL_I2C_Master_Transmit(hi2c, address, data, length, TIMEOUT_TICKS);

  // HAL_I2C_Master_WaitTXIS() (вызывается внутри HAL_I2C_Master_Transmit, см.
  // mik32_hal_i2c.c) уже сам пишет hi2c->ErrorCode = I2C_ERROR_NACK при обнаружении
  // NACKF — отдельная проверка через HAL_I2C_Get_Interrupts_Status() (апстримный код)
  // не нужна и к тому же такой функции нет в установленном HAL.
  if      (hi2c->ErrorCode == I2C_ERROR_TIMEOUT) ret = I2C_TIMEOUT;    // timeout
  else if (hi2c->ErrorCode == I2C_ERROR_NACK)    ret = I2C_NACK_DATA;  // didn't receive ACK
  else if (hi2c->ErrorCode != I2C_ERROR_NONE)    ret = I2C_ERROR;      // any other error

  return ret;
}

/*
 * Function twi_slaveWrite
 * Desc     attempts to become twi bus slave and write a
 *          series of bytes to a master after it asks
 * Input    hi2c: pointer to hardware i2c handler
 *          txData: pointer to byte array
 *          bytesNum: number of bytes in array
 * Output   0 .. success
 *          5 .. timeout
 *
 * Апстримная реализация писала TXDR/флаги вручную (HAL_I2C_Get_CR1_Content/
 * HAL_I2C_Reset_TXDR_Content/HAL_I2C_Write_TXDR/HAL_I2C_Slave_WaitTXIS/
 * HAL_I2C_Reset_Interrupt_Flag) — половины этих функций нет в установленном HAL.
 * HAL_I2C_Slave_Transmit() — готовая блокирующая замена с тем же эффектом (есть в
 * установленном mik32_hal_i2c.h). Slave-режим всё равно отклоняется кодогеном
 * (findUnsupportedArduinoFeature) в Arduino v1 — эта функция компилируется, но
 * не используется на практике.
 */
i2c_status_e twi_slaveWrite(I2C_HandleTypeDef* hi2c, uint8_t *txData, uint8_t bytesNum)
{
  if (hi2c->ErrorCode != I2C_ERROR_NONE)
    HAL_I2C_Reset(hi2c);

  if (HAL_I2C_Slave_Transmit(hi2c, txData, bytesNum, TIMEOUT_TICKS) != HAL_OK)
    return I2C_TIMEOUT;

  return I2C_OK;
}

/*
 * Function twi_interruptHandler
 * Desc     handles an interrupts from twi (slave mode only)
 *
 * Пустой stub: Arduino v1 принимает только mode=master (findUnsupportedArduinoFeature
 * в arduino_project_codegen.cpp), а master-режим twi_init() никогда не вызывает
 * HAL_EPIC_MaskLevelSet() для I2C — значит EPIC_CHECK_I2C_0()/1() в trap_handler.c
 * физически не взведётся, и эта функция не вызывается. Апстримная реализация читала
 * ISR/TXDR/RXDR напрямую через функции, которых нет в установленном HAL
 * (HAL_I2C_Get_CR1_Content/HAL_I2C_Get_Interrupts_Status/HAL_I2C_Reset_Interrupt_Flag/
 * HAL_I2C_Get_RXDR) — переписывать slave-приём под реальные регистры МИК32V2
 * вручную означало бы угадывать порядок сброса флагов ADDR/RXNE/STOP без HW-проверки
 * (тот же риск, что уже дважды ловился на RTC/Bus Watchdog, см. корневой CLAUDE.md) —
 * сознательно отложено до отдельной задачи с HW-валидацией slave-режима.
 */
void twi_interruptHandler(WireHandler_TypeDef* handler)
{
  (void)handler;
}
