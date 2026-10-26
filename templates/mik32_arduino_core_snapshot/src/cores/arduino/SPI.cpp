// Вендорено из docs/elron/elbear_arduino_bsp/libraries/SPI/src/SPI.cpp, с правками под
// установленный framework-mik32v2-sdk (тот же класс несовместимости API, что уже
// найден и исправлен у Wire/twi.c, см. docs/implementations/arduino_codegen.md).
#include "SPI.h"

SPIClass SPI1(1);
#if SPI_COMMON_QTY > 1
SPIClass SPI0(0);
#endif
SPIClass& SPI = DEFAULT_SPI;

static uint8_t reverse_bits(uint8_t byte);

void SPIClass::updateSettings(uint32_t speedMaximum, uint8_t dataOrder, uint8_t dataMode)
{
  if ((_speed != speedMaximum) || (_dataOrder != dataOrder) || (_dataMode != dataMode))
  {
    // mik32v2 делители: div = 2 << (0...7), значение в скобках - значение для регистра.
    uint8_t divRegVal = 0;
    while(divRegVal <= 7)
    {
      if ((F_CPU/(2 << divRegVal)) <= speedMaximum)
        break;
      divRegVal++;
    }

    _hspi.Init.CLKPhase    = dataMode & 0b00000001;
    _hspi.Init.CLKPolarity = (dataMode & 0b00000010)>>1;
    _hspi.Init.BaudRateDiv = divRegVal;

    _speed = speedMaximum;
    _dataOrder = dataOrder;
    _dataMode = dataMode;
    _newConfig = true;
  }
}

void SPIClass::begin()
{
  spi_onBegin(_spiNum);

  if (_spiNum == 0)
    _hspi.Instance         = SPI_0;
  else
    _hspi.Instance         = SPI_1;
  _hspi.Init.SPI_Mode    = HAL_SPI_MODE_MASTER;
  _hspi.Init.ThresholdTX = 4;
  _hspi.Init.Decoder     = SPI_DECODER_NONE;
  _hspi.Init.ManualCS    = SPI_MANUALCS_ON;
  _hspi.Init.ChipSelect  = SPI_CS_NONE;
  _hspi.Init.BaudRateDiv = SPI_CLOCK_DIV8;
  _hspi.Init.CLKPhase    = SPI_MODE0 & 0b00000001;
  _hspi.Init.CLKPolarity = (SPI_MODE0 & 0b00000010)>>1;

  _spiInUse = true;
}

void SPIClass::end()
{
  if (_spiInUse && _isInited)
  {
    HAL_SPI_Disable(&_hspi);

    if (_hspi.Instance == SPI_0)
    {
      HAL_GPIO_PinConfig(GPIO_0, (HAL_PinsTypeDef)(GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_2),
                        HAL_GPIO_MODE_GPIO_INPUT, HAL_GPIO_PULL_NONE, HAL_GPIO_DS_2MA);
    }
    else if (_hspi.Instance == SPI_1)
    {
      HAL_GPIO_PinConfig(GPIO_1, (HAL_PinsTypeDef)(GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_2),
                        HAL_GPIO_MODE_GPIO_INPUT, HAL_GPIO_PULL_NONE, HAL_GPIO_DS_2MA);
    }

    _spiInUse = false;
    _isInited = false;
    _interruptMode = 0;
    _interruptMask = 0;
  }
}

void SPIClass::usingInterrupt(uint8_t interruptNumber)
{
  if(interruptNumber < EXTERNAL_INTERRUPTS_QTY)
  {
    noInterrupts();
    _interruptMask |= (1 << interruptNumber);
    if (!_interruptMode)
      _interruptMode = 1;
    interrupts();
  }
}

void SPIClass::notUsingInterrupt(uint8_t interruptNumber)
{
  if(interruptNumber < EXTERNAL_INTERRUPTS_QTY)
  {
    noInterrupts();
    _interruptMask &= ~(1<<interruptNumber);
    if (!_interruptMask)
      _interruptMode = 0;
    interrupts();
  }
}

void SPIClass::beginTransaction(SPISettings settings)
{
  updateSettings(settings.speedMaximum, settings.newDataOrder, settings.newDataMode);

  if (_spiInUse && (_interruptMode > 0))
  {
    for (uint8_t i = 0; i < EXTERNAL_INTERRUPTS_QTY; i++)
    {
      if (_interruptMask & (1 << i))
        disableInterrupt(i);
    }
  }

  if(_spiInUse && ((!_isInited) || _newConfig))
  {
    if (HAL_SPI_Init(&_hspi) != HAL_OK)
      ErrorMsgHandler("SPI.beginTransaction(): initialization error");
    else
    {
      _isInited = true;
      _newConfig = false;
    }
  }
}

uint8_t SPIClass::transfer(uint8_t data)
{
  uint8_t rxByte = 0;

  if (_isInited)
  {
    if (_dataOrder == LSBFIRST)
      data = reverse_bits(data);

    HAL_StatusTypeDef SPI_Status = HAL_SPI_Exchange(&_hspi, &data, &rxByte, 1, SPI_TIMEOUT_DEFAULT*2);
    if (SPI_Status != HAL_OK)
      HAL_SPI_ClearError(&_hspi);

    if (_dataOrder == LSBFIRST)
      rxByte = reverse_bits(rxByte);
  }
  return rxByte;
}

uint16_t SPIClass::transfer16(uint16_t data)
{
  uint8_t buf[2];
  uint16_t rxVal = 0;
  if (_isInited)
  {
    if (_dataOrder == LSBFIRST)
    {
      buf[0] = reverse_bits(data&0xFF);
      buf[1] = reverse_bits((data>>8)&0xFF);
    }
    else
    {
      buf[0] = (data>>8)&0xFF;
      buf[1] = data&0xFF;
    }

    HAL_StatusTypeDef SPI_Status = HAL_SPI_Exchange(&_hspi, buf, buf, 2, SPI_TIMEOUT_DEFAULT*2);
    if (SPI_Status != HAL_OK)
      HAL_SPI_ClearError(&_hspi);

    if (_dataOrder == LSBFIRST)
      rxVal = ( ((uint16_t)reverse_bits(buf[1]) ) << 8) | reverse_bits(buf[0]);
    else
      rxVal = ( ((uint16_t)buf[0]) << 8) | buf[1];
  }
  return rxVal;
}

void SPIClass::transfer(void *buf, size_t count)
{
  if (count == 0)
    return;

  if (_isInited)
  {
    uint8_t *p = (uint8_t *)buf;

    if (_dataOrder == LSBFIRST)
    {
      for (uint32_t i = 0; i < count; i++)
        *(p+i) = reverse_bits(*(p+i));
    }

    HAL_StatusTypeDef SPI_Status = HAL_SPI_Exchange(&_hspi, (uint8_t*)buf, (uint8_t*)buf, count, SPI_TIMEOUT_DEFAULT*2);
    if (SPI_Status != HAL_OK)
      HAL_SPI_ClearError(&_hspi);

    if (_dataOrder == LSBFIRST)
    {
      p = (uint8_t *)buf;
      for (uint32_t i = 0; i < count; i++)
        *(p+i) = reverse_bits(*(p+i));
    }
  }
}

void SPIClass::endTransaction(void)
{
  if (_spiInUse && (_interruptMode > 0))
  {
    for (uint8_t i = 0; i < EXTERNAL_INTERRUPTS_QTY; i++)
    {
      if (_interruptMask & (1 << i))
        enableInterrupt(i);
    }
  }
}

void SPIClass::setBitOrder(uint8_t bitOrder)
{
  if (_spiInUse)
    _dataOrder = bitOrder;
  else
    ErrorMsgHandler("SPI.setBitOrder():SPI.begin() need to be called first");
}

// Апстрим вызывал HAL_SPI_Set_Clock_Mode()/HAL_SPI_Set_Clock_Divider() — этих функций
// нет в установленном framework-mik32v2-sdk (grep по mik32_hal_spi.c/.h — пусто,
// проверено перед правкой). HAL_SPI_Init() переписывает регистр CONFIG целиком одной
// транзакцией, включая CLKPhase/CLKPolarity/BaudRateDiv (mik32_hal_spi.c, HAL_SPI_Init) —
// то же самое действие, что пытались сделать удалённые функции, безопасно вызывать
// повторно после begin() (сам HAL_SPI_Init сначала делает HAL_SPI_Disable()).
void SPIClass::setDataMode(uint8_t dataMode)
{
  if (_spiInUse)
  {
    _hspi.Init.CLKPhase    = (dataMode&0b00000001);
    _hspi.Init.CLKPolarity = (dataMode&0b00000010)>>1;
    if (_isInited)
      HAL_SPI_Init(&_hspi);
    _dataMode = dataMode;
  }
  else
    ErrorMsgHandler("SPI.setDataMode():SPI.begin() need to be called first");
}

void SPIClass::setClockDivider(uint8_t clockDiv)
{
  if (_spiInUse)
  {
    if ((clockDiv == SPI_CLOCK_DIV2)   || (clockDiv == SPI_CLOCK_DIV4)   ||
        (clockDiv == SPI_CLOCK_DIV8)   || (clockDiv == SPI_CLOCK_DIV16)  ||
        (clockDiv == SPI_CLOCK_DIV32)  || (clockDiv == SPI_CLOCK_DIV64)  ||
        (clockDiv == SPI_CLOCK_DIV128) || (clockDiv == SPI_CLOCK_DIV256))
    {
      _hspi.Init.BaudRateDiv = clockDiv;
      _speed = F_CPU >> (clockDiv+1);
      if (_isInited)
        HAL_SPI_Init(&_hspi);
    }
    else
      ErrorMsgHandler("SPI.setClockDivider(): Invalid clock devider");
  }
  else
    ErrorMsgHandler("SPI.setClockDivider():SPI.begin() need to be called first");
}

static uint8_t reverse_bits(uint8_t byte)
{
  byte = (byte & 0xF0) >> 4 | (byte & 0x0F) << 4;
  byte = (byte & 0xCC) >> 2 | (byte & 0x33) << 2;
  byte = (byte & 0xAA) >> 1 | (byte & 0x55) << 1;
  return byte;
}
