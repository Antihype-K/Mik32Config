//
// Проверка «эту конфигурацию вообще можно сгенерировать в Rust» —
// отдельно от validation/cross_module_validator.h (тот про конфликты пинов
// МЕЖДУ блоками, а это про то, поддерживает ли сам Rust HAL
// (templates/mik32_rust_hal_snapshot/hal) запрошенную фичу вообще, вне
// зависимости от других блоков).
//
// Не добавлено в cross_module_validator.cpp: тот модуль весь построен
// вокруг PinConflict/(gpio_instance,pin)-пересечений и принимает knowledge-
// структуры по отдельности, а не ProjectConfig целиком — втискивать сюда
// проверку "тип блока в принципе не реализован на этом языке" потребовало
// бы либо менять его сигнатуры (риск для уже протестированного пин-конфликт
// пути), либо заводить седьмой независимый knowledge-параметр без
// какой-либо связи с пинами. Отдельный файл — по тому же принципу, что и
// остальной Rust-код этой сессии (см. docs/implementations/rust_codegen.md):
// новая логика в новом файле, минимальная правка существующих.
//
// v1 Rust HAL (mik32v2-hal, вендоренный и доработанный снэпшот
// mik32-hal-main) поддерживает: GPIO mode ∈ {output, input, serial} без
// прерываний, drive_strength (все три), RCC.
// UART (с 2026-10-26, см. docs/implementations/rust/usart.md) — полный
// паритет с C-веткой КРОМЕ DMA и "модемного" подрежима: MSB/LSB,
// sync/async (+xck_mode/last_byte_clock), half-duplex, TX/RX-инверсии,
// swap, аппаратный RTS/CTS (flow_control), запрет mode=synchronous на
// instance=1 (Errata §10.1, как и в C-ветке).
// I2C (с 2026-10-26, см. docs/implementations/rust/i2c.md) — паритет с
// C-веткой КРОМЕ DMA и interrupt_mode: master/slave, цифровой/аналоговый
// фильтр, general call, no-stretch, второй адрес+маска, авторесет.
// НЕ поддерживает: GPIO IRQ, GPIO mode ∈ {timer_serial, analog}, Timer16,
// Timer32, SPI, UART DMA, UART modem_signals/rts_mode=modem, I2C
// DMA/interrupt_mode, FreeRTOS. Регистр
// MODEM в PAC не даёт записать DCD/DSR/RI/DDIS), FreeRTOS — ни один из них
// не имеет рабочего кода/бита в mik32-hal-main/PAC (см. rust_codegen.md,
// разделы «Находки» и «Итоговый v1 объём периферии»).
//
#ifndef MIK32_CONFIGURATOR_RUST_FEATURE_VALIDATOR_H
#define MIK32_CONFIGURATOR_RUST_FEATURE_VALIDATOR_H

#include <string>
#include "project_config.h"

// Пустая строка — конфиг целиком укладывается в то, что умеет Rust-ветка.
// Непустая — точная причина отказа (какой блок/instance/поле), пригодная
// для показа пользователю как есть, без дополнительной обёртки. Функция
// проверяет только "поддерживается ли эта фича в принципе на Rust" — она
// НЕ проверяет конфликты пинов (это cross_module_validator) и НЕ
// перепроверяет schema-валидацию отдельных блоков (та уже пройдена к
// моменту вызова, см. parseProjectConfig).
//
// Вызывать только когда config.language == OutputLanguage::Rust — для
// language == C возвращает пустую строку безусловно (незачем звать, но
// вызов не вреден).
std::string findUnsupportedRustFeature(const ProjectConfig& config);

#endif //MIK32_CONFIGURATOR_RUST_FEATURE_VALIDATOR_H
