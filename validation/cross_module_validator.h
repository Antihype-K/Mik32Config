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
#include "json.hpp"
#include "uart_knowledge.h"
#include "timer16_knowledge.h"
#include "i2c_knowledge.h"
#include "timer32_knowledge.h"
#include "spi_knowledge.h"

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
// timer16_knowledge добавлен по той же схеме, что и uart_knowledge: Timer16
// тоже не декларирует пин в JSON (см. schemas/timer16.json — ШИМ-вывод
// жёстко закреплён датащитом за instance), заявка выводится из
// knowledge_base/timer16.json (instance_fixed_pwm_pin), и только когда
// выбранный mode реально задействует ШИМ-вывод (mode_waveform_enable) —
// ТЗ §3.1.4 п.4.
// i2c_knowledge — четвёртый источник заявок: I2C всегда занимает ОБЕ линии
// (SDA+SCL) сразу при включении инстанса, независимо от mode (master/slave)
// или demo — в отличие от UART (зависит от direction/flow_control) и Timer16
// (зависит от mode_waveform_enable), у I2C нет режима, не использующего обе
// линии (см. knowledge_base/i2c.json, instance_fixed_pins).
// timer32_knowledge — пятый источник заявок: в отличие от Timer16 (один
// фиксированный ШИМ-вывод на instance), у Timer32_1/2 своя фиксированная
// пара (gpio_instance, pin) НА КАЖДЫЙ из 4 каналов (instance_channel_fixed_pin),
// и заявка возникает для каждого сконфигурированного канала независимо от
// его mode (pwm/capture/compare) — mik32_hal_timer32.c,
// HAL_TIMER32_Channel_MspInit мультиплексирует вывод на любой Channel_Init
// безусловно, до проверки режима. Timer32_0 никогда не заявляет пин (у него
// нет каналов — ТЗ §3.1.5).
// spi_knowledge — шестой источник заявок: как и I2C, SPI занимает несколько
// пинов сразу (SCK/MOSI/MISO/SS_IN — 4 штуки, instance_fixed_pins), но, в
// отличие от I2C, не при любом mode — плюс ОДИН дополнительный CS-пин, ТОЛЬКО
// когда mode=master (instance_chip_select_pin[instance][chip_select]) —
// см. knowledge_base/spi.json, _comment_instance_chip_select_pin: у slave
// поле Init.ChipSelect в реальном HAL остаётся не заполненным, и HAL_GPIO_Init
// в HAL_SPI_MspInit вызывается с пустой маской.
CrossModuleValidationResult validatePinConflicts(
    const std::string& config_path,
    const UartKnowledge& uart_knowledge,
    const Timer16Knowledge& timer16_knowledge,
    const I2cKnowledge& i2c_knowledge,
    const Timer32Knowledge& timer32_knowledge,
    const SpiKnowledge& spi_knowledge);

// Документная перегрузка — по той же причине, что и parseProjectDocument
// (codegen/project/project_config.h): GUI обязан показывать конфликты выводов
// в реальном времени (ТЗ §3.1.9 п.5, панель диагностики), то есть проверять
// конфигурацию, которой ещё нет на диске.
CrossModuleValidationResult validatePinConflictsInDocument(
    const nlohmann::json& root,
    const UartKnowledge& uart_knowledge,
    const Timer16Knowledge& timer16_knowledge,
    const I2cKnowledge& i2c_knowledge,
    const Timer32Knowledge& timer32_knowledge,
    const SpiKnowledge& spi_knowledge);

#endif //MIK32_CONFIGURATOR_CROSS_MODULE_VALIDATOR_H
