//
// Разбор ВСЕГО файла конфигурации сразу, а не одного блока одного типа.
//
// Концепция. Каждый parse<Peripheral>Config(path) по построению находит в
// массиве "peripherals" ПЕРВЫЙ блок своего типа и возвращает его — это
// осознанное ограничение однопериферийного пайплайна. Мультипериферийной
// генерации нужен другой контракт: пройти массив один раз и разложить ВСЕ
// блоки по типам, сохранив порядок их появления в файле. Правила валидации
// при этом не дублируются — для каждого блока вызывается блочная точка входа
// соответствующего парсера (parseGpioBlock / parseUartBlock /
// parseTimer16Block / parseI2cBlock), то есть ровно та же самая проверка
// схемы, что и в однопериферийном пути.
//
// Соответствие архитектуре: «МИК32 конфигуратор - Архитектура.md», раздел
// «Общий пайплайн» — формат верхнего уровня {mcu, version, peripherals:[...]},
// диспетчеризация по полю "type" каждого элемента.
//
#ifndef MIK32_CONFIGURATOR_PROJECT_CONFIG_H
#define MIK32_CONFIGURATOR_PROJECT_CONFIG_H

#include <string>
#include <vector>
#include "parse_result.h"
#include "json.hpp"
#include "gpio_parser.h"
#include "usart_parser.h"
#include "timer16_parser.h"
#include "i2c_parser.h"
#include "timer32_parser.h"
#include "spi_parser.h"

struct ProjectConfig
{
    std::string mcu;
    std::string version;

    // Порядок внутри каждого вектора — порядок блоков в JSON. Он НЕ
    // определяет порядок инициализации в сгенерированном main() (за это
    // отвечает init_order в PeripheralContribution), но определяет,
    // например, какой UART считается «первым» для привязки xputc.
    std::vector<GpioConfig> gpio_blocks;
    std::vector<UartConfig> uart_blocks;
    std::vector<Timer16Config> timer16_blocks;
    std::vector<I2cConfig> i2c_blocks;
    std::vector<Timer32Config> timer32_blocks;
    std::vector<SpiConfig> spi_blocks;
};

// Ошибка возвращается, если:
//  - файл не открывается / не парсится как JSON / нет массива "peripherals";
//  - любой отдельный блок не прошёл свою schema-валидацию (текст ошибки
//    парсера дополняется номером блока — при N блоках сообщение вида
//    «Поле "instance" вне диапазона» без указания блока бесполезно);
//  - встретился тип периферии, для которого ещё нет парсера/кодогена
//    (тихо игнорировать нельзя: пользователь получил бы прошивку без
//    запрошенной периферии и без единого сообщения);
//  - два блока uart/timer16/i2c/spi ссылаются на один и тот же instance
//    (один физический блок периферии нельзя настроить дважды двумя разными
//    наборами параметров — это нарушение аппаратного ограничения,
//    ТЗ §3.1.6.2). Для gpio это НЕ ошибка: несколько блоков на один
//    instance — штатный способ задать разным пинам одного порта разные
//    mode/pull/drive_strength, они сливаются в одну init-функцию
//    (см. codegen/project/gpio_contribution.h).
//  - в конфигурации нет ни одного поддерживаемого блока (генерировать
//    нечего).
ParseResult<ProjectConfig> parseProjectConfig(const std::string& file_name_path);

// Документная точка входа — тот же приём, что и parse<X>Block в парсерах.
// Нужна GUI: редактор держит конфигурацию в памяти как nlohmann::json и
// обязан перевалидировать её на каждое изменение поля, не записывая
// промежуточные состояния на диск (ТЗ §3.2 п.1 — файл проекта не должен
// оказываться в несогласованном состоянии). parseProjectConfig выше — тонкая
// обёртка: открыть файл, разобрать JSON, вызвать эту функцию.
ParseResult<ProjectConfig> parseProjectDocument(const nlohmann::json& root);

#endif //MIK32_CONFIGURATOR_PROJECT_CONFIG_H
