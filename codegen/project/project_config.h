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
#include "freertos_parser.h"

// Выбор способа упаковки сгенерированной прошивки — второй, независимый от
// PlatformIO путь (codegen/project/cmake_project_codegen.h). PlatformIO
// остаётся дефолтом: поле toolchain отсутствует почти во всех существующих
// фикстур tests/fixtures/*.json, и они обязаны продолжать парситься как
// раньше (см. ProjectConfigParse.* в tests/test_parsers.cpp).
//
// Cargo — третье значение, появившееся вместе с Rust-веткой (codegen/project/
// rust_project_codegen.h). В отличие от PlatformIO/CMake, Cargo не выбирается
// полем "toolchain" напрямую — он следует автоматически из "language":"rust"
// (см. OutputLanguage ниже и парсинг в project_config.cpp): единственный
// разумный сборщик для Rust — Cargo, отдельного выбора тут нет и не нужно.
enum class ToolchainKind { PlatformIO, CMake, Cargo };

// Выходной язык генерируемой прошивки. C — единственный язык, на котором
// проект живёт с самого начала (все golden-фикстуры, HW-валидация); Rust —
// второй язык, добавленный поверх mik32-hal-main/mik32-rt/mik32v2-pac
// (см. docs/implementations/rust_codegen.md). Поле независимо от toolchain
// исторически (PlatformIO/CMake — оба варианта упаковки C-прошивки), но
// однозначно определяет его: language=Rust => toolchain=Cargo всегда.
enum class OutputLanguage { C, Rust };

// Откуда брать исходники HAL SDK при сборке CMake-варианта. Bundled и
// LocalPath — offline-режимы (снэпшот в templates/mik32_sdk_snapshot/ или
// каталог пользователя с той же структурой hal/+shared/), Network —
// единственный режим, реально требующий сети (git clone), см.
// docs/implementations/cmake_toolchain.md.
//
// То же самое трёхзначное деление переиспользуется для источника
// hal/rt/pac-крейтов в Rust-ветке (rust_hal_source_mode ниже) — те же три
// сценария (вендоренный снэпшот / git-зависимости как у апстрима / каталог
// пользователя), см. docs/implementations/rust_codegen.md.
enum class HalSourceMode { Bundled, Network, LocalPath };

// Ночная сессия 12->13.10.2026 (OVERNIGHT_FIELD_REFERENCE.md) — лайт-версия
// конфигурации тактирования (backend; GUI-форма — отдельная сессия).
// Поле уровня проекта, НЕ элемент peripherals[] (PCC — не периферия,
// назначаемая instance'ом, а единственный блок тактирования всего МК).
// Источник — mik32_hal_pcc.h, PCC_InitTypeDef. Дефолты здесь воспроизводят
// значения, которые раньше были жёстко зашиты в kSystemClockConfigDefinition
// (project_codegen.cpp) — старые конфиги без поля "clock" генерируют
// побайтово тот же SystemClock_Config(), что и раньше.
struct ClockConfig
{
    // "hsi32m"/"osc32m" -> PCC_OSCILLATORTYPE_HSI32M/OSC32M. osc32m — прежний
    // хардкод (внешний кварц 32МГц), используется и как OscillatorEnable
    // (всегда ALL — оба генератора включены для FreqMon), и как
    // FreqMon.OscillatorSystem (реальный источник тактирования ядра).
    std::string oscillator_system = "osc32m";
    // PCC_InitTypeDef: "делитель... число от Min=0 до Max=255".
    int ahb_divider = 0;
    int apbm_divider = 0;
    int apbp_divider = 0;
};

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

    // --- Поля упаковки проекта (codegen/project/cmake_project_codegen.h) ---
    // Все пять — необязательные поля верхнего уровня JSON-документа, со
    // значениями по умолчанию ровно такими, как здесь. Дефолт PlatformIO,
    // а не CMake, потому что PlatformIO-путь уже подтверждён на реальной
    // плате (golden-фикстуры, см. project_codegen.cpp), а CMake — новый,
    // ещё не проверенный на железе вариант (CLAUDE.md, hardware validation
    // gate).
    ToolchainKind toolchain = ToolchainKind::PlatformIO;
    HalSourceMode hal_source_mode = HalSourceMode::Bundled;
    std::string hal_source_local_path;   // используется только если hal_source_mode == LocalPath
    std::string compiler_path;           // пусто = автоопределение на этапе CMake configure
    bool per_type_files = false;         // используется только если toolchain == CMake

    // --- Rust-ветка (codegen/project/rust_project_codegen.h) -------------
    // language=Rust переключает toolchain на Cargo автоматически при
    // парсинге (project_config.cpp) — дальше по пайплайну читается уже
    // только toolchain, не language. rust_hal_source_mode/
    // rust_hal_source_local_path используются только если language == Rust,
    // по тому же контракту, что hal_source_mode/hal_source_local_path для
    // CMake-ветки.
    OutputLanguage language = OutputLanguage::C;
    HalSourceMode rust_hal_source_mode = HalSourceMode::Bundled;
    std::string rust_hal_source_local_path;

    // Поле уровня проекта, НЕ элемент peripherals[] — см. обоснование в
    // parser/freertos/freertos_parser.h. По умолчанию FreeRtosConfig{} —
    // enabled=false, main() генерируется в прежнем виде (while(1)).
    FreeRtosConfig freertos;

    // Необязательное поле верхнего уровня "clock" — см. ClockConfig выше.
    ClockConfig clock;
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
