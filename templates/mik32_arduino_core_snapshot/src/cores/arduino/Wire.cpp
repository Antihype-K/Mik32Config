// Вендорено без изменений из docs/elron/elbear_arduino_bsp/libraries/Wire/src/Wire.cpp —
// весь HAL-специфичный код живёт в utility/twi.c (переписан под установленный
// framework-mik32v2-sdk, см. комментарии там), этот файл только раскладывает
// Arduino Wire-API по буферам и вызывает twi_*().

extern "C"
{
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
}
#include "Wire.h"

// --------------------------- Public Methods --------------------------- //
void TwoWire::begin(void)
{
  rxBufferIndex = 0;
  rxBufferLength = 0;
  txBufferIndex = 0;
  txBufferLength = 0;
  twi_init(&wireHandler, slaveAddress);
}

void TwoWire::begin(uint8_t address)
{
  slaveAddress = address;
  begin();
}

void TwoWire::begin(int address)
{
  begin((uint8_t)address);
}

void TwoWire::end(void)
{
  flush(); // wait for transmission complete
  twi_deinit(&wireHandler);
  slaveAddress = 0;
}

void TwoWire::setClock(uint32_t clock)
{
  if (twi_setFrequency(&wireHandler, clock, false) != I2C_OK)
    ErrorMsgHandler("Wire.setClock(): invalid frequency or Wire was not begin");
}

// ----------------------------- request ----------------------------- //
uint8_t TwoWire::requestFrom(uint8_t address, uint8_t quantity, uint32_t iaddress, uint8_t isize, uint8_t sendStop)
{
  if (isize > 0)
  {
    // send internal register address if needed
    beginTransmission(address); // save slave address

    // the maximum size of internal address is 3 bytes
    if (isize > 3)    isize = 3;

    // write internal register address - most significant byte first
    while (isize-- > 0)
      write((uint8_t)(iaddress >> (isize*8))); // write data to txBuf

    endTransmission(false); // transmit data without stop generation
  }
  // clamp to buffer length
  if(quantity > BUFFER_LENGTH)
    quantity = BUFFER_LENGTH;

  // perform blocking read into buffer
  uint8_t read = twi_masterReadFrom(&(wireHandler.i2c_param), address, rxBuffer, quantity, sendStop);

  // set rx buffer iterator vars
  rxBufferIndex = 0;
  rxBufferLength = read;

  return read;
}

uint8_t TwoWire::requestFrom(uint8_t address, uint8_t quantity, uint8_t sendStop)
{
	return requestFrom((uint8_t)address, (uint8_t)quantity, (uint32_t)0, (uint8_t)0, (uint8_t)sendStop);
}

uint8_t TwoWire::requestFrom(uint8_t address, uint8_t quantity)
{
  return requestFrom((uint8_t)address, (uint8_t)quantity, (uint8_t)true);
}

uint8_t TwoWire::requestFrom(int address, int quantity)
{
  return requestFrom((uint8_t)address, (uint8_t)quantity, (uint8_t)true);
}

uint8_t TwoWire::requestFrom(int address, int quantity, int sendStop)
{
  return requestFrom((uint8_t)address, (uint8_t)quantity, (uint8_t)sendStop);
}

// ----------------------------- Transmission ----------------------------- //
void TwoWire::beginTransmission(uint8_t address)
{
  // indicate that we are transmitting
  transmitting = 1;
  // set address of targeted slave whithout shift
  txAddress = address;
  // reset tx buffer iterator vars
  txBufferIndex = 0;
  txBufferLength = 0;
}

void TwoWire::beginTransmission(int address)
{
  beginTransmission((uint8_t)address);
}

//  Calling endTransmission(false) allows a sketch to
//  perform a repeated start.
//  WARNING: Nothing in the library keeps track of whether
//  the bus tenure has been properly ended with a STOP. It
//  is very possible to leave the bus in a hung state if
//  no call to endTransmission(true) is made. Some I2C
//  devices will behave oddly if they do not see a STOP.
uint8_t TwoWire::endTransmission(uint8_t sendStop)
{
  uint8_t ret = I2C_ERROR;
  if (transmitting)
  {
    // transmit buffer (blocking)
    ret = twi_masterWriteTo(&(wireHandler.i2c_param), txAddress, txBuffer, txBufferLength, sendStop);
    // reset tx buffer iterator vars
    txBufferIndex = 0;
    txBufferLength = 0;
    // indicate that we are done transmitting
    transmitting = 0;
  }
  return ret;
}

//  This provides backwards compatibility with the original
//  definition, and expected behaviour, of endTransmission
uint8_t TwoWire::endTransmission(void)
{
  return endTransmission((uint8_t)true);
}

// ----------------------------- Write ----------------------------- //
// must be called in:
// slave tx event callback
// or after beginTransmission(address)
size_t TwoWire::write(uint8_t data)
{
  size_t ret = 1;
  if (transmitting)
  {
    // in master transmitter mode
    // don't bother if buffer is full
    if(txBufferLength >= BUFFER_LENGTH)
    {
      setWriteError();
      return 0;
    }
    // put byte in tx buffer
    txBuffer[txBufferIndex] = data;
    ++txBufferIndex;
    // update amount in buffer
    txBufferLength = txBufferIndex;
  }
  else
  {
    // in slave send mode - reply to master
    if (twi_slaveWrite(&(wireHandler.i2c_param), &data, 1) != I2C_OK)
      ret = 0;
  }
  return ret;
}

/**
  * @brief  This function must be called in slave Tx event callback or after
  *         beginTransmission() and before endTransmission().
  * @param  pdata: pointer to the buffer data
  * @param  quantity: number of bytes to write
  * @retval number of bytes ready to write.
  */
size_t TwoWire::write(const uint8_t *data, size_t quantity)
{
  size_t ret = quantity;

  if (transmitting)
  {
    // in master transmitter mode
    for(size_t i = 0; i < quantity; ++i)
      write(data[i]);
  }
  else
  {
    // in slave send mode - reply to master
    if (twi_slaveWrite(&(wireHandler.i2c_param), (uint8_t *)data, quantity) != I2C_OK)
      ret = 0;
  }
  return ret;
}

// ----------------------------- Stream func ----------------------------- //
// must be called in:
// slave rx event callback
// or after requestFrom(address, numBytes)
int TwoWire::available(void)
{
  return rxBufferLength - rxBufferIndex;
}

// must be called in:
// slave rx event callback
// or after requestFrom(address, numBytes)
int TwoWire::read(void)
{
  int value = -1;

  // get each successive byte on each call
  if (rxBufferIndex < rxBufferLength)
  {
    value = rxBuffer[rxBufferIndex];
    ++rxBufferIndex;
  }
  return value;
}

// must be called in:
// slave rx event callback
// or after requestFrom(address, numBytes)
int TwoWire::peek(void)
{
  int value = -1;

  if (rxBufferIndex < rxBufferLength)
    value = rxBuffer[rxBufferIndex];

  return value;
}

void TwoWire::flush(void)
{
  // we use blocking transactions, so we won't get here until the reception/transmission is complete
  rxBufferIndex = 0;
  rxBufferLength = 0;
  txBufferIndex = 0;
  txBufferLength = 0;
  txAddress = 0;
}

// ----------------------------- Events ----------------------------- //
// behind the scenes function that is called when data is received
void TwoWire::onReceiveService(uint8_t* inBytes, int numBytes)
{
  // don't bother if user hasn't registered a callback
  if(!user_onReceive)
    return;

  // copy twi rx buffer into local read buffer
  // this enables new reads to happen in parallel
  memcpy(rxBuffer, inBytes, numBytes);

  // set rx iterator vars
  rxBufferIndex = 0;
  rxBufferLength = numBytes;
  // alert user program
  user_onReceive(numBytes);
}

// behind the scenes function that is called when data is requested
void TwoWire::onRequestService(void)
{
  // don't bother if user hasn't registered a callback
  if(user_onRequest)
    user_onRequest(); // alert user program
}

// sets function called on slave write
void TwoWire::onReceive( void (*function)(int) )
{
  user_onReceive = function;
}

// sets function called on slave read
void TwoWire::onRequest( void (*function)(void) )
{
  user_onRequest = function;
}

// С function for trap handler
extern "C" void __attribute__((optimize("O3"))) wire_interrupt_handler(uint8_t num)
{
  if (num == 1)
    twi_interruptHandler(Wire1.getHandler());
#if I2C_COMMON_QTY>1
  else if (num == 0)
    twi_interruptHandler(Wire0.getHandler());
#endif
}

// ----------------------------- Preinstantiate Objects ----------------------------- //
TwoWire Wire1 = TwoWire(1); // I2C1 is always available
#if I2C_COMMON_QTY > 1
TwoWire Wire0 = TwoWire(0); // I2C0 is available on some boards
#endif
// default interface
TwoWire& Wire = DEFAULT_WIRE;
