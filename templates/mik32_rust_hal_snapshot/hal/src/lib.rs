//! mik32v2-hal — вендоренный и частично доработанный снэпшот
//! `docs/сделать/mik32-hal-main` (апстрим: без публичного репозитория,
//! копия предоставлена пользователем).
//!
//! В исходном апстриме этот пакет собирался только как `main.rs`-бинарник
//! (никакого `lib.rs`, модули объявлены `mod` — не `pub mod` — внутри
//! `main.rs`) — то есть не мог быть подключён как обычная Cargo-зависимость.
//! Этот `lib.rs` добавлен конфигуратором, чтобы у сгенерированных Rust-
//! проектов было настоящее имя крейта для `path`-зависимости, а не
//! копипаста исходников через `#[path = ...]`.
//!
//! Что вошло в снэпшот и почему — `docs/implementations/rust_codegen.md`:
//! только `gpio`, `serial`, `rcc`, `peripheral`, `time` (рабочий код).
//! `timer`/`timer::delay` (апстримный) и `usart.rs` исключены — не
//! компилируются в апстриме (мёртвый код, скопированный из ARM Cortex-M
//! шаблона). `timer16` — НЕ апстримный модуль, написан конфигуратором с нуля
//! по образцу `i2c.rs`, см. docs/implementations/rust/timer16.md.
#![no_std]

pub mod crc;
pub mod gpio;
pub mod i2c;
pub mod peripheral;
pub mod rcc;
pub mod rtc;
pub mod serial;
pub mod spi;
pub mod time;
pub mod timer16;
pub mod timer32;
pub mod tsens;

pub use mik32v2_pac as pac;
