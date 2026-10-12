# Единый справочник названий полей (ночная сессия 12→13.10.2026)

Источник истины для GUI-агентов (облачные routine 02:40/08:00 EAT) и для
backend-кода. **Этот файл специально лежит в корне репозитория, а не в
docs/** — docs/ в .gitignore (личные заметки, никогда не коммитятся), а этот
файл обязан быть виден облачным GUI-агентам через git. Удалить/перенести в
docs/ можно после ночной сессии, когда координация через git больше не нужна.

Все названия ниже сверены с установленным `framework-mik32v2-sdk`
(идентично свежему GitFlic-клону, кроме `extern "C"`-обёртки).

## Аудит существующих модулей (GPIO/I2C/SPI/Timer16/Timer32)

Проверено построчно: названия полей в `schemas/gpio.json`, `schemas/i2c.json`,
`schemas/spi.json`, `schemas/timer16.json`, `schemas/timer32.json` уже
являются snake_case-отражением реальных имён полей/enum'ов HAL
(`prescaler`, `arr`, `cmp`, `count_mode`, `top`, `ocr`, `pwm_invert`,
`capture_edge`, `noise_filter`, `clk_phase`, `clk_polarity`, `threshold_tx`,
`baud_rate_div`, `manual_cs`, `decoder`, `chip_select`, `clock_speed`,
`address_mode`, `own_address`, `target_address`, `drive_strength`, `pull`).
**Переименований не требуется** — соответствие уже есть. FreeRTOS-модуль не
аудируется против датащита МК: там источник терминологии — сам FreeRTOS API,
а не железо.

## USART — новые поля (эта сессия)

Все поля ниже — **необязательные** (не ломают существующие фикстуры с
`"flow_control": false` и т.п.). Источник — `mik32_hal_usart.h`,
`USART_HandleTypeDef`.

| JSON-поле | Тип | Значения | HAL-источник | Поведение по умолчанию (поле не задано) |
|---|---|---|---|---|
| `bit_direction` | enum | `msb_first`, `lsb_first` | `HAL_USART_BitDirection_enum` (MSB_First=1/LSB_First=0) | `lsb_first` (HAL default 0) |
| `mode` | enum | `asynchronous`, `synchronous` | `HAL_USART_Mode_enum` | `asynchronous` (0) |
| `xck_mode` | enum | `mode0`..`mode3` | `HAL_USART_XCK_Mode_enum`, только при `mode=synchronous` | `mode0` |
| `last_byte_clock` | bool | — | поле `last_byte_clock`, только при `mode=synchronous` | `false` (Disable) |
| `channel_mode` | enum | `duplex`, `half_duplex` | `HAL_USART_ChannelMode_enum` (Duplex_Mode=0/Semiduplex_Mode=1) | `duplex` |
| `tx_inversion` | bool | — | поле `tx_inversion` | `false` |
| `rx_inversion` | bool | — | поле `rx_inversion` | `false` |
| `swap` | bool | — | поле `swap` (Swap RXD & TXD) | `false` |
| `rts_mode` | enum | `always_enabled`, `modem` | `HAL_USART_RTS_mode_enum`, только при `flow_control=true` | `always_enabled` |
| `modem_signals` | object `{ri,dsr,dtr,ddis,dcd: bool}` | — | `HAL_USART_Modem_TypeDef` (без rts/cts — те уже покрыты `flow_control`) | все `false` |
| `parity_bit_inversion` | bool | — | поле `parity_bit_inversion`, только при `parity != "none"` | `false` |

Существующее поле `flow_control` (bool) **не меняется** — продолжает включать
`rts`+`cts` с режимом по умолчанию `AlwaysEnable_mode`; `rts_mode` уточняет
режим RTS только когда `flow_control=true`.

## Тактирование (PCC) — новый top-level объект `clock`

Сосед `peripherals` в project JSON. Источник — `mik32_hal_pcc.h`,
`PCC_InitTypeDef`. Статус: см. `OVERNIGHT_CLOCK_CONFIG_FIELDS.md` (создаётся
вместе с реализацией, может появиться позже этого файла).

## Таймеры — пересчёт частоты/скважности

Статус: в процессе, см. `OVERNIGHT_TIMER_FREQUENCY_DUTY.md` (создаётся
вместе с реализацией).
