// Вендорено без изменений из docs/elron/elbear_arduino_bsp/libraries/Wire/src/Wire.h
// (LGPL-заголовок сохранён в апстримном файле — см. docs/implementations/arduino_codegen.md,
// «Почему вендоринг Arduino-core, а не ссылка на внешний репозиторий»). Класс TwoWire сам
// не трогает регистры — вся HAL-специфика внизу, в utility/twi.c, который апстрим написал
// под более старую версию HAL и пришлось переписать под установленный framework-mik32v2-sdk
// (см. комментарии в utility/twi.c).

#ifndef TwoWire_h
#define TwoWire_h

#include "Stream.h"
#include "Arduino.h"
extern "C"
{
#include "utility/twi.h"
}

#define BUFFER_LENGTH 32

// WIRE_HAS_END means Wire has end()
#define WIRE_HAS_END      1

class TwoWire : public Stream
{
  private:
    WireHandler_TypeDef wireHandler;

    uint8_t rxBuffer[BUFFER_LENGTH];
    uint8_t rxBufferIndex = 0;
    uint8_t rxBufferLength = 0;

    uint8_t txAddress = 0;  // 7 bits without shift
    uint8_t txBuffer[BUFFER_LENGTH];
    uint8_t txBufferIndex = 0;
    uint8_t txBufferLength = 0;

    uint8_t transmitting = 0;
    uint8_t slaveAddress = 0;
    void (*user_onRequest)(void);
    void (*user_onReceive)(int numBytes);
    void onRequestService(void);
    void onReceiveService(uint8_t* inBytes, int numBytes);
    static inline void staticOnRequestService(void* instance)
    {
        // cast pointer to TwoWire type and call method
        static_cast<TwoWire*>(instance)->onRequestService();
    }
    static inline void staticOnReceiveService(void* instance, uint8_t* inBytes, int numBytes)
    {
        static_cast<TwoWire*>(instance)->onReceiveService(inBytes, numBytes);
    }
  public:
    inline TwoWire(uint8_t num)
    {
      wireHandler.i2c_num = (num < I2C_COMMON_QTY) ? num : 1; // I2C1 by default
      wireHandler.instance = this;
      // save pointers to static functions to wire handler
      wireHandler.onSlaveTransmit = &TwoWire::staticOnRequestService;
      wireHandler.onSlaveReceive = &TwoWire::staticOnReceiveService;
    }
    WireHandler_TypeDef* getHandler()
    {
      return &wireHandler;
    }
    void begin();
    void begin(uint8_t);
    void begin(int);
    void end();
    void setClock(uint32_t frequency);

    void beginTransmission(uint8_t address);
    void beginTransmission(int address);
    uint8_t endTransmission(void);
    uint8_t endTransmission(uint8_t sendStop);

    uint8_t requestFrom(uint8_t address, uint8_t quantity);
    uint8_t requestFrom(uint8_t address, uint8_t quantity, uint8_t sendStop);
    uint8_t requestFrom(uint8_t address, uint8_t quantity, uint32_t iaddress, uint8_t isize, uint8_t sendStop);
    uint8_t requestFrom(int address, int quantity);
    uint8_t requestFrom(int address, int quantity, int sendStop);

    virtual int available(void);
    virtual int read(void);
    virtual int peek(void);
    virtual void flush(void);

    virtual size_t write(uint8_t data);
    virtual size_t write(const uint8_t * data, size_t quantity);
    inline size_t write(unsigned long n)  { return write((uint8_t)n); }
    inline size_t write(long n)           { return write((uint8_t)n); }
    inline size_t write(unsigned int n)   { return write((uint8_t)n); }
    inline size_t write(int n)            { return write((uint8_t)n); }
    using Print::write;

    void onReceive( void (*)(int) );
    void onRequest( void (*)(void) );
};

extern TwoWire Wire1;
#if I2C_COMMON_QTY>1
extern TwoWire Wire0;
#endif
// default interface
extern TwoWire& Wire;

#endif
