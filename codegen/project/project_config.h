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

#include <optional>
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
#include "rtc_parser.h"
#include "crc_parser.h"
#include "eeprom_parser.h"
#include "wdt_parser.h"
#include "dac_parser.h"
#include "adc_parser.h"
#include "crypto_parser.h"
#include "otp_parser.h"
#include "tsens_parser.h"
#include "pvd_parser.h"
#include "bus_watchdog_parser.h"
#include "dma_parser.h"
#include "spifi_parser.h"

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
// (см. docs/implementations/rust_codegen.md). Arduino — третий, v1-объём
// GPIO+UART+I2C+setup/loop поверх вендоренного docs/elron/elbear_arduino_bsp
// (см. docs/implementations/arduino_codegen.md), решено в /grill-me сессии
// 2026-10-26. Поле независимо от toolchain исторически (PlatformIO/CMake —
// оба варианта упаковки C-прошивки), но однозначно определяет его:
// language=Rust => toolchain=Cargo всегда, language=Arduino =>
// toolchain=PlatformIO всегда (CMake для Arduino-ветки не реализован).
enum class OutputLanguage { C, Rust, Arduino };

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

    // AUDIT_TZ_PERIPHERY_GAPS.md, Clock/PCC — добавлено 2026-10-14.
    // Необязательные поля, дефолты воспроизводят старый хардкод
    // project_codegen.cpp побайтово.
    int hsi32m_calibration = 128;  // PCC_InitTypeDef.HSI32MCalibrationValue, 0-255
    int lsi32k_calibration = 8;    // PCC_InitTypeDef.LSI32KCalibrationValue, 0-15
    std::string rtc_clock_source = "auto";         // "auto"/"lsi32k"/"osc32k" -> PCC_RTCClockSourceTypeDef
    std::string rtc_clock_cpu_source = "osc32k";    // "osc32k"/"lsi32k" -> PCC_CPURTCClockSourceTypeDef
    // Маска включённых осцилляторов (PCC_InitTypeDef.OscillatorEnable).
    // Дефолт — все четыре (старое поведение, PCC_OSCILLATORTYPE_ALL).
    // ВАЖНО: oscillator_system обязан входить в эту маску — HAL_PCC_Config
    // сначала настраивает систему на oscillator_system, а ПОТОМ выключает
    // всё, что не попало в маску (mik32_hal_pcc.c:345-370) — убрать из маски
    // источник, от которого реально тактируется ядро, значит заглушить
    // кристалл. Парсер проверяет это условие и отказывает, а не кодоген.
    std::vector<std::string> oscillator_enable = {"hsi32m", "osc32m", "lsi32k", "osc32k"};
    bool force_osc_sys = false;    // PCC_ForceOscSysTypeDef: false=UNFIXED (старое поведение), true=FIXED
    std::string force_32k_clk = "osc32k"; // "auto"/"lsi32k"/"osc32k" -> PCC_FreqMonitorSourceTypeDef, дефолт = старый хардкод
};

// Реальная частота AHB (ядра) из Clock-блока — единственное место формулы,
// чтобы C/Rust/Arduino-ветки кодогена не считали её по отдельности (риск
// рассинхронизации, как уже было с мапперами, см. feedback в памяти
// ассистента). Формула — mik32_hal_pcc.c: HAL_PCC_DividerAHB(), комментарий
// "Частота шины AHB определяется по формуле: F_sys_clk / (DividerAHB + 1)".
// F_sys_clk — номинал осциллятора (32 МГц для HSI32M и для внешнего кварца
// OSC32M, установленного на плате, см. ClockConfig::oscillator_system выше).
inline long long computeAhbFrequencyHz(const ClockConfig& clock)
{
    constexpr long long kNominalOscillatorHz = 32000000LL;  // HSI32M и OSC32M оба номинально 32 МГц
    return kNominalOscillatorHz / (clock.ahb_divider + 1);
}

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

    // AUDIT_TZ_PERIPHERY_GAPS.md, Тир 2. RTC — единственный глобальный блок
    // (нет instance, schemas/rtc.json) — optional, не vector: допустим
    // максимум один блок type="rtc" на проект (проверено в project_config.cpp).
    std::optional<RtcConfig> rtc_block;

    // AUDIT_TZ_PERIPHERY_GAPS.md, Тир 2. CRC — тоже единственный глобальный
    // блок (нет instance, schemas/crc.json).
    std::optional<CrcConfig> crc_block;

    // AUDIT_TZ_PERIPHERY_GAPS.md, Тир 2. EEPROM — тоже единственный
    // глобальный блок (нет instance, schemas/eeprom.json).
    std::optional<EepromConfig> eeprom_block;

    // AUDIT_TZ_PERIPHERY_GAPS.md, Тир 2. WDT — тоже единственный глобальный
    // блок (нет instance, schemas/wdt.json).
    std::optional<WdtConfig> wdt_block;

    // AUDIT_TZ_PERIPHERY_GAPS.md, Тир 2. DAC — два физических канала (0/1),
    // как Timer32/SPI — вектор, не optional.
    std::vector<DacConfig> dac_blocks;

    // ADC/CRYPTO/OTP/TSENS — все единственные глобальные блоки, нет instance.
    std::optional<AdcConfig> adc_block;
    std::optional<CryptoConfig> crypto_block;
    std::optional<OtpConfig> otp_block;
    std::optional<TsensConfig> tsens_block;

    // AUDIT_TZ_PERIPHERY_GAPS.md, Тир 3 (регистровая работа, нет HAL-обёртки
    // конфигурации — только макросы включения тактирования). PVD/Bus
    // Watchdog — тоже единственные глобальные блоки, нет instance.
    std::optional<PvdConfig> pvd_block;
    std::optional<BusWatchdogConfig> bus_watchdog_block;

    // standalone DMA-каналы (docs/implementations/dma.md) — instance это
    // номер канала (0-3), не отдельный физический блок на канал, но то же
    // соглашение, что у Timer32/SPI/DAC. Делят глобальный пул из 4 каналов
    // с встроенным полем "dma" у UART (project_config.cpp, used_dma_channels).
    std::vector<DmaConfig> dma_blocks;

    // SPIFI — тоже единственный глобальный блок (нет instance,
    // schemas/spifi.json), покрывает только Memory Mode.
    std::optional<SpifiConfig> spifi_block;

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
