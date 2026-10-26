#include "board.h"
// mik32_hal.h (подключается транзитивно через mik32_hal_pcc.h) объявляет
// свои функции (HAL_Init и т.п.) без extern "C" — внутри .cpp-файла без
// явной обёртки компилятор дал бы им C++-линковку (мангленные имена), а
// определения в mik32_hal.c собраны с C-линковкой: undefined reference на
// линковке (найдено на реальной сборке 2026-10-26). Оборачиваем все
// HAL-инклюды в board.cpp в extern "C", чтобы линковка совпала.
extern "C" {
#include "mik32_hal_pcc.h"
#include "mik32_hal_irq.h"
// HAL-заголовки — до Arduino.h (см. комментарий в wiring_digital.c, та же
// причина: конфликт макросов OUTPUT/INPUT с полями регистровых структур).
// mcu32_memory_map.h в elbear_arduino_bsp — опечатка/другое имя; в
// установленном framework-mik32v2-sdk файл называется mik32_memory_map.h
// (найдено на реальной сборке 2026-10-26).
#include "mik32_memory_map.h"
#include "mik32_hal_spifi_w25.h"
}
#include "Arduino.h"

// --------------------- flash cache limit --------------------- //

// define board flash size
#if defined(ARDUINO_START_MIK32_V1)
    #define FLASH_SIZE  0x00400000  // 4 Mb
#elif defined(ARDUINO_ACE_UNO_16MB)
    #define FLASH_SIZE  0x01000000  // 16 Mb
#elif defined(ARDUINO_ACE_UNO_32MB)
    #define FLASH_SIZE  0x02000000  // 32 Mb
#else
    #define FLASH_SIZE  0x00800000  // 8 Mb
#endif
// define margin from flash end
#define FLASH_END_OFFSET    0x1000

// values from ld script
// __RODATA__ не существует в установленном framework-mik32v2-sdk
// (shared/ldscripts/sections.lds) — .text и .rodata слиты в одну секцию
// .text, конец которой размечен символом __TEXT_END__ (найдено на реальной
// сборке 2026-10-26).
extern uint8_t __TEXT_END__[];

// RAM-function for updating flash cache limit by app size to improve performance
__attribute__((section(".ram_text"))) void updateCacheLimit(void)
{
    uint32_t new_limit = (uintptr_t)__TEXT_END__ - SPIFI_BASE_ADDRESS;
    // limit cache size by flash size with margin
    if (new_limit > (FLASH_SIZE - FLASH_END_OFFSET))
        new_limit = FLASH_SIZE - FLASH_END_OFFSET;

    uint32_t MCMDbackup = SPIFI_CONFIG->MCMD;           // save current value from MCMD
    SPIFI_CONFIG->STAT |= SPIFI_CONFIG_STAT_RESET_M;    // reset MCMD mode for writing new CLIMIT
    SPIFI_CONFIG->CLIMIT = new_limit;                   // set new CLIMIT
    SPIFI_CONFIG->MCMD = MCMDbackup;                    // restore MCMD value
}

// --------------------- init --------------------- // 
// called from crt0.S before constructors initialization
extern "C" void SystemInit(void)
{
    // set irq vector to ram region
    write_csr(mtvec, 0x02000000);

    HAL_Init();

    // HAL_Init() сам по себе не настраивает тактовое дерево (PCC) — только
    // вызывает HAL_MspInit(), который по умолчанию пустой (weak). В C-ветке
    // кодогена это делает отдельная сгенерированная SystemClock_Config()
    // (codegen/project/pcc_contribution.cpp), вызываемая из main() ДО любой
    // периферии — в Arduino-ветке эквивалента не было вообще. Без этого
    // busy-wait UART на REACK/TEACK (uart_lib.c/mik32_hal_usart.c —
    // ожидание подтверждения готовности приёмника/передатчика) зависает
    // навечно: делители AHB/APB_P/APB_M и выбор системного осциллятора
    // остаются в состоянии сброса, а не заданы явно. GPIO при
    // этом работал, потому что включение его тактирования — это всего
    // лишь один бит в регистре, не зависящий от PCC_Config (найдено на
    // реальной сборке 2026-10-26: GPIO+UART Arduino-демо зависала до
    // loop(), даже светодиод не мигал, хотя чистый GPIO-блинк без UART
    // работал). Используются те же дефолты, что генерирует C-ветка для
    // Clock/PCC-блока (см. HAL_PCC_Config комментарии в mik32_hal_pcc.c):
    // оба осциллятора 32МГц и оба 32КГц включены, делители = 0 (/1),
    // системный источник — внешний кварц OSC32M (см. feedback_hsi32m_
    // calibration_not_accurate_by_default в памяти ассистента — HSI32M
    // без ручной калибровки по месту даёт заметно неточную частоту).
    PCC_InitTypeDef PCC_OscInit = {0};
    PCC_OscInit.OscillatorEnable = PCC_OSCILLATORTYPE_HSI32M | PCC_OSCILLATORTYPE_OSC32M | PCC_OSCILLATORTYPE_LSI32K | PCC_OSCILLATORTYPE_OSC32K;
    PCC_OscInit.FreqMon.OscillatorSystem = PCC_OSCILLATORTYPE_OSC32M;
    PCC_OscInit.FreqMon.ForceOscSys = PCC_FORCE_OSC_SYS_UNFIXED;
    PCC_OscInit.FreqMon.Force32KClk = PCC_FREQ_MONITOR_SOURCE_OSC32K;
    PCC_OscInit.AHBDivider = 0;
    PCC_OscInit.APBMDivider = 0;
    PCC_OscInit.APBPDivider = 0;
    PCC_OscInit.HSI32MCalibrationValue = 128;
    PCC_OscInit.LSI32KCalibrationValue = 8;
    PCC_OscInit.RTCClockSelection = PCC_RTC_CLOCK_SOURCE_AUTO;
    PCC_OscInit.RTCClockCPUSelection = PCC_CPU_RTC_CLOCK_SOURCE_OSC32K;
    HAL_PCC_Config(&PCC_OscInit);

    // gpio clock
    __HAL_PCC_GPIO_0_CLK_ENABLE();
    __HAL_PCC_GPIO_1_CLK_ENABLE();
    __HAL_PCC_GPIO_2_CLK_ENABLE();
    __HAL_PCC_GPIO_IRQ_CLK_ENABLE();
   
    // for delays
    SysTick_Init();   
    // update flash cache limit by app size
    updateCacheLimit();
}

// called after setup()
void post_init(void)
{
    if(isInterruptsEnabled()) // if user has called noInterrupts() in setup(), this value is false
        interrupts(); // enable global interrupts
}

// --------------------- other --------------------- // 
volatile bool use_error_messages = true;
// print error message to Serial
extern "C" void ErrorMsgHandler(const char * msg)
{
    // function works if Serial is used in sketch and user doesn't turn it off
    #ifdef HardwareSerial_h         
        if(use_error_messages&&Serial)
            Serial.println(msg);
    #endif
}


