# MIK32-Configurator

Кроссплатформенный GUI-конфигуратор и генератор init-кода для МК МИК32 (российский RISC-V).

Загружает один JSON-конфиг (`{mcu, version, peripherals: [...]}`), валидирует его по схемам
(`schemas/`) и генерирует C- или Rust-проект инициализации периферии (GPIO, UART, I2C, SPI,
таймеры, RTC, CRC, ADC/DAC, DMA, FreeRTOS и др.) на основе базы знаний (`knowledge_base/`).
GUI — на Qt.

## Сборка

```bash
cmake -S . -B build
cmake --build build
```

Бинарник `MIK32-Configurator` появится в `build/`.

### Тесты

```bash
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build
cd build && ctest --output-on-failure
```

Подробности по архитектуре, карте файлов и статусу модулей — в `CLAUDE.md` (в репозитории не
отслеживается, личные рабочие заметки).
