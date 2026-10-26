#define ARDUINO_MAIN

#include "Arduino.h"

// Weak empty variant initialization function.
// May be redefined by variant files.
void initVariant() __attribute__((weak));
void initVariant() { }

extern "C" void __libc_init_array(void);

extern "C" int main(void) __attribute__((weak));
int main()
{
    // Установленный framework-mik32v2-sdk/shared/runtime/crt0.S прыгает в main()
    // сразу после SystemInit(), не вызывая глобальные C++-конструкторы
    // (.init_array) — найдено на реальной сборке 2026-10-26: Serial0.write(c)
    // работал (девиртуализированный вызов на конкретном объекте), а
    // Serial0.println()/print() зависали намертво — внутри Print::println()
    // вызов this->write(...) идёт через vtable (this статически Print*,
    // write переопределён в HardwareSerial), а vtable-указатель Serial0
    // оставался нулевым из .bss, так как конструктор HardwareSerial никогда
    // не выполнялся. Трогать crt0.S установленного SDK нельзя (правило
    // проекта — не чинить системный SDK), поэтому конструкторы запускаются
    // здесь, в вендоренном main.cpp, перед первым обращением к любому
    // глобальному C++-объекту.
    __libc_init_array();

    initVariant(); // for freeRTOS
    setup();
    post_init();

    while (1)
    {
        loop();
    }
}