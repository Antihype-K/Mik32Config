//
// Кросс-модульная проверка конфликтов назначений выводов между разными
// типами периферии в одном конфиге — ТЗ §3.1.6.2:
// «конфликты назначений выводов, конфликты каналов DMA ... нарушения
// аппаратных ограничений». Эта сессия закрывает только конфликты пинов
// между GPIO и UART (DMA-каналы и GPIO IRQ — отдельные, ещё не
// реализованные подсистемы, см. docs/implementations/).
//
// Концепция: UART сам не декларирует пины в JSON (см. решение убрать
// tx_pin/rx_pin — parser/USART/usart_parser.h), поэтому его "заявка" на
// физический пин выводится из knowledge_base/uart.json (instance_fixed_pins)
// по instance/flow_control, а не из полей конфига. GPIO, наоборот,
// заявляет пины явно в config.pins. Оба вида заявок сводятся к общему
// ключу (gpio_instance, pin) и сравниваются.
//
#ifndef MIK32_CONFIGURATOR_CROSS_MODULE_VALIDATOR_H
#define MIK32_CONFIGURATOR_CROSS_MODULE_VALIDATOR_H

#include <string>
#include <vector>
#include "uart_knowledge.h"

struct PinConflict
{
    int gpio_instance;
    int pin;
    // ТЗ §3.1.6.4: сообщение должно называть номер вывода и наименования
    // ВСЕХ конфликтующих блоков — обычно два, но не ограничено двумя.
    std::vector<std::string> conflicting_owners;
};

struct CrossModuleValidationResult
{
    bool success;       // false — не удалось прочитать/разобрать config_path
    std::string error;  // причина, если success == false
    std::vector<PinConflict> conflicts;  // пусто — конфликтов нет (это Ошибка по §3.1.6.3)
};

// Читает JSON-конфиг напрямую (не через parse<Peripheral>Config — тем
// парсерам нужен ровно один блок своего типа, а здесь нужно перебрать ВСЕ
// блоки "gpio" и "uart" в одном файле). Предполагает, что каждый блок уже
// прошёл собственную schema-валидацию (parseGpioConfig/parseUsartConfig) —
// эта функция не переисполняет её и не заменяет собой возврат
// makeError-стиля из отдельных парсеров.
CrossModuleValidationResult validatePinConflicts(
    const std::string& config_path,
    const UartKnowledge& uart_knowledge);

#endif //MIK32_CONFIGURATOR_CROSS_MODULE_VALIDATOR_H
