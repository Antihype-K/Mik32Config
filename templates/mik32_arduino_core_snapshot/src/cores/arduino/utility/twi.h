// Вендорено без изменений из docs/elron/elbear_arduino_bsp/libraries/Wire/src/utility/twi.h —
// только сигнатуры, вся несовместимость с установленным HAL была в реализации (twi.c).
#ifndef __TWI_H__
#define __TWI_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "mik32_hal_i2c.h"

#ifndef TWI_BUFFER_LENGTH
#define TWI_BUFFER_LENGTH   32
#endif

typedef struct
{
  I2C_HandleTypeDef i2c_param;
  uint8_t           i2c_num;
  bool              isInited;
  uint8_t           rxBuffer[TWI_BUFFER_LENGTH];
  volatile uint8_t  rxBufferIndex;
  void              (*onSlaveTransmit)(void*);
  void              (*onSlaveReceive) (void*, uint8_t*, int);
  void              *instance;
}WireHandler_TypeDef;

// I2C state
typedef enum
{
  I2C_OK            = 0,
  I2C_DATA_TOO_LONG = 1,
  I2C_NACK_ADDR     = 2,
  I2C_NACK_DATA     = 3,
  I2C_ERROR         = 4,
  I2C_TIMEOUT       = 5,
  I2C_BUSY          = 6
} i2c_status_e;

uint8_t twi_init  (WireHandler_TypeDef* handler, uint8_t slaveAddress);
void    twi_deinit(WireHandler_TypeDef* handler);

uint8_t twi_setFrequency(WireHandler_TypeDef* handler, uint32_t frequency, bool onInit);

uint8_t twi_masterReadFrom  (I2C_HandleTypeDef* hi2c, uint8_t address, uint8_t* data, uint8_t length, uint8_t sendStop);
uint8_t twi_masterWriteTo   (I2C_HandleTypeDef* hi2c, uint8_t address, uint8_t* data, uint8_t length, uint8_t sendStop);
i2c_status_e twi_slaveWrite (I2C_HandleTypeDef* hi2c, uint8_t *txData, uint8_t bytesNum);

void twi_interruptHandler(WireHandler_TypeDef* handler);

#ifdef __cplusplus
}
#endif

#endif /* __TWI_H__ */
