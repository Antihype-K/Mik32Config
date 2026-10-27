#include "arduino_project_codegen.h"
#include "user_code_markers_rust.h"
#include "contribution_utils.h"
#include "win_file_io.h"
#include "utf8_path.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <sstream>
#include <system_error>
#include <vector>

namespace fs = std::filesystem;

namespace
{
    ParseResult<GeneratedProjectFirmwareArduino> makeError(const std::string& message)
    {
        return ParseResult<GeneratedProjectFirmwareArduino>{false, message, std::nullopt};
    }

    // Та же таблица, что tsens_contribution.cpp::clockSourceMacro (C-ветка).
    std::string tsensClockSourceMacro(const std::string& value)
    {
        if (value == "sys_clk") return "HAL_TSENS_SYS_CLK";
        if (value == "hclk") return "HAL_TSENS_HCLK";
        if (value == "osc32m") return "HAL_TSENS_OSC32M";
        if (value == "hsi32m") return "HAL_TSENS_HSI32M";
        if (value == "osc32k") return "HAL_TSENS_OSC32K";
        return "HAL_TSENS_LSI32K";
    }

    // v1 scope (docs/open_tasks.md, /grill-me 2026-10-26): GPIO+UART+I2C
    // only, никакой другой периферии в Arduino-ветке ещё нет. 9-bit UART
    // не поддержан Arduino-core (HardwareSerial.h: SERIAL_* макросы — только
    // 7/8 бит).
    // Timer16 (добавлено 2026-10-26, следующая сессия после v1): вендоренное
    // Arduino-ядро содержит tone()/noTone() (cores/arduino/Tone.cpp) —
    // готовую абстракцию поверх Timer16_1, НО library.json явно исключает
    // этот файл из сборки ("-<cores/arduino/Tone.cpp>"), а
    // cores/arduino/trap_handler.c при этом безусловно (не weak) вызывает
    // extern tone_interrupt_handler() внутри EPIC_CHECK_TIMER16_1 — включать
    // Tone.cpp означало бы менять поведение ВСЕХ Arduino-проектов сразу
    // (не только тех, что используют Timer16 в JSON-конфиге) без HW-
    // проверки линковки/IRQ-диспетчеризации в этой сессии. Решение: не
    // трогать core/Tone.cpp. Вместо Arduino-стиля класса — гибридный прямой
    // вызов HAL (HAL_Timer16_Init/HAL_Timer16_StartPWM) прямо в setup(),
    // как и предлагает задача, с явным комментарием "гибридный вызов".
    // Принимается только самый частый случай (mode=period, ШИМ без
    // прерываний/триггеров/нестандартного clock_source) — см.
    // docs/implementations/arduino_codegen.md.
    std::string findUnsupportedArduinoFeature(const ProjectConfig& config)
    {
        for (const Timer16Config& block : config.timer16_blocks)
        {
            if (block.mode != "period")
            {
                return "Arduino-кодоген v1 поддерживает Timer16 только в mode=period (ШИМ) — one_shot/external_event_count/encoder не выражены Arduino-API и не реализованы как гибридный HAL-вызов в этой сессии";
            }
            if (block.clock_source != "internal_system")
            {
                return "Arduino-кодоген v1 поддерживает Timer16 только с clock_source=internal_system (частота ШИМ в комментарии посчитана только для этого источника)";
            }
            if (block.trigger_edge != "software" || !block.trigger_source.empty())
            {
                return "Arduino-кодоген v1 не поддерживает аппаратный триггер Timer16 (trigger_edge/trigger_source) — только программный запуск";
            }
            if (block.external_clock_filter != "none" || block.trigger_filter != "none")
            {
                return "Arduino-кодоген v1 не поддерживает фильтры Timer16 (external_clock_filter/trigger_filter)";
            }
            if (block.waveform_inverted)
            {
                return "Arduino-кодоген v1 не поддерживает waveform_inverted у Timer16 (гибридный вызов всегда неинвертированный)";
            }
            if (block.overflow_interrupt || block.compare_interrupt)
            {
                // cores/arduino/trap_handler.c резервирует EPIC TIMER16_0/1/2
                // под IRremote/tone/servo (фиксированные слоты по instance) —
                // включение своих прерываний Timer16 рискует столкнуться с
                // этой диспетчеризацией без HW-проверки, поэтому отказ.
                return "Arduino-кодоген v1 не поддерживает прерывания Timer16 (overflow_interrupt/compare_interrupt) — EPIC-слоты TIMER16_0/1/2 зарезервированы вендоренным trap_handler.c под IRremote/tone/servo";
            }
        }
        if (config.rtc_block) return "Arduino-кодоген v1 не поддерживает RTC";
        if (config.eeprom_block) return "Arduino-кодоген v1 не поддерживает EEPROM";
        if (config.wdt_block) return "Arduino-кодоген v1 не поддерживает WDT";
        if (!config.dac_blocks.empty()) return "Arduino-кодоген v1 не поддерживает DAC";
        if (config.adc_block) return "Arduino-кодоген v1 не поддерживает ADC";
        if (config.crypto_block) return "Arduino-кодоген v1 не поддерживает CRYPTO";
        if (config.otp_block) return "Arduino-кодоген v1 не поддерживает OTP";
        if (config.pvd_block) return "Arduino-кодоген v1 не поддерживает PVD";
        if (config.bus_watchdog_block) return "Arduino-кодоген v1 не поддерживает Bus Watchdog";
        if (!config.dma_blocks.empty()) return "Arduino-кодоген v1 не поддерживает DMA";
        if (config.spifi_block) return "Arduino-кодоген v1 не поддерживает SPIFI";
        for (const UartConfig& block : config.uart_blocks)
        {
            if (block.instance != 0 && block.instance != 1)
            {
                return "Arduino-кодоген v1 поддерживает только UART0/UART1";
            }
            if (block.data_bits != 7 && block.data_bits != 8)
            {
                return "Arduino-кодоген v1 поддерживает только 7/8-битный UART (HardwareSerial.h SERIAL_* макросы)";
            }
        }
        for (const I2cConfig& block : config.i2c_blocks)
        {
            if (block.instance != 0 && block.instance != 1)
            {
                return "Arduino-кодоген v1 поддерживает только I2C0/I2C1";
            }
            // utility/twi.c (templates/mik32_arduino_core_snapshot) принимает
            // только master: slave-приём в апстримном twi.c асинхронный
            // (прерывания ADDR/RXNE/STOP), а функции, которыми апстрим читал
            // эти флаги, не существуют в установленном framework-mik32v2-sdk —
            // переписывать вручную по регистрам без HW-проверки означало бы
            // угадывать порядок сброса флагов (см. комментарий в twi.c).
            if (block.mode != "master")
            {
                return "Arduino-кодоген v1 поддерживает только I2C mode=master (slave-приём в Wire-библиотеке асинхронный, не переписан под установленный HAL без HW-проверки)";
            }
        }
        for (const SpiConfig& block : config.spi_blocks)
        {
            if (block.instance != 0 && block.instance != 1)
            {
                return "Arduino-кодоген v1 поддерживает только SPI0/SPI1";
            }
            // cores/arduino/SPI.cpp (SPIClass::begin()) жёстко задаёт режим
            // ведущего, ManualCS=ON, ChipSelect=NONE, Decoder=NONE, без
            // поддержки прерываний (HAL_SPI_Exchange — поллинг) — ровно та же
            // причина, по которой Wire.begin() не принимает clock_speed:
            // Arduino-библиотека конфигурирует CS/decoder/IRQ не через
            // begin(), а вообще не умеет (CS вручную через digitalWrite,
            // прерывания не реализованы).
            if (block.mode != "master")
            {
                return "Arduino-кодоген v1 поддерживает только SPI mode=master (SPIClass в вендоренной библиотеке не реализует slave)";
            }
            if (block.decoder)
            {
                return "Arduino-кодоген v1 не поддерживает SPI decoder (SPIClass.begin() всегда SPI_DECODER_NONE)";
            }
            if (!block.manual_cs)
            {
                return "Arduino-кодоген v1 не поддерживает автоматический CS (SPIClass.begin() всегда SPI_MANUALCS_ON — CS выбирается digitalWrite() в пользовательском коде)";
            }
            if (!block.interrupts.empty())
            {
                return "Arduino-кодоген v1 не поддерживает прерывания SPI (SPIClass использует только поллинговый HAL_SPI_Exchange)";
            }
        }
        // PWM через analogWrite(): variant.c сам определяет таймер/канал по
        // номеру пина (pwmPinToTimer/pwmPinToTimerChannel,
        // templates/mik32_arduino_core_snapshot/src/variants/rex_board/variant.c) —
        // P0_0..3 -> TIMER32_1 CH0..3, P1_0..3 -> TIMER32_2 CH0..3, в точности
        // та же карта, что knowledge_base/timer32.json::instance_channel_fixed_pin
        // использует в C-ветке. Остальной объём Timer32 (capture/compare,
        // инвертированный ШИМ, свой источник тактирования, EPIC-прерывания)
        // analogWrite() не выражает — явный отказ, не угадывание.
        bool has_pwm_frequency_request = false;
        double requested_pwm_frequency_hz = 0.0;
        for (const Timer32Config& block : config.timer32_blocks)
        {
            if (block.instance != 1 && block.instance != 2)
            {
                return "Arduino-кодоген v1 поддерживает ШИМ только на Timer32_1/Timer32_2 (analogWrite)";
            }
            if (block.clock_source != "prescaler")
            {
                return "Arduino-кодоген v1 поддерживает Timer32 только с clock_source=prescaler (analogWrite не настраивает источник тактирования)";
            }
            if (block.overflow_interrupt || block.underflow_interrupt)
            {
                return "Arduino-кодоген v1 не поддерживает прерывания Timer32 (analogWrite не настраивает EPIC)";
            }
            for (const Timer32ChannelConfig& channel : block.channels)
            {
                if (channel.mode != "pwm")
                {
                    return "Arduino-кодоген v1 поддерживает только mode=pwm у каналов Timer32 (capture/compare не представимы через analogWrite)";
                }
                if (channel.pwm_invert)
                {
                    return "Arduino-кодоген v1 не поддерживает pwm_invert (analogWrite всегда неинвертированный)";
                }
                if (!channel.has_duty_percent)
                {
                    return "Arduino-кодоген v1 поддерживает только duty_percent у каналов ШИМ (сырой ocr зависит от top, который analogWrite вычисляет сам)";
                }
            }
            if (block.has_frequency)
            {
                // htimer32/pwmTopVal в wiring_analog.c — единственная пара
                // static-переменных на весь проект (не по таймеру), поэтому
                // частота ШИМ общая для всех Timer32-блоков разом.
                if (has_pwm_frequency_request && requested_pwm_frequency_hz != block.requested_frequency_hz)
                {
                    return "Arduino-кодоген v1: analogWriteFrequency() общая на весь проект, нельзя задать разные частоты для разных Timer32-блоков";
                }
                has_pwm_frequency_request = true;
                requested_pwm_frequency_hz = block.requested_frequency_hz;
            }
        }
        for (const GpioConfig& block : config.gpio_blocks)
        {
            if (block.instance != 0 && block.instance != 1 && block.instance != 2)
            {
                return "Arduino-кодоген v1 поддерживает только GPIO0/GPIO1/GPIO2";
            }
            if (block.mode != "output" && block.mode != "input")
            {
                return "Arduino-кодоген v1 поддерживает только GPIO mode=output/input (не serial/analog/timer_serial)";
            }
        }
        return "";
    }

    // Pin -> имя константы в variants/rex_board/pins_arduino.h (P0_0..P0_15,
    // P1_0..P1_15, P2_6, P2_7 — см. templates/mik32_arduino_core_snapshot/).
    std::string pinConstant(int instance, int pin)
    {
        return "P" + std::to_string(instance) + "_" + std::to_string(pin);
    }

    std::string pinModeConstant(const std::string& mode, const std::string& pull)
    {
        if (mode == "output") return "OUTPUT";
        if (pull == "pull_up") return "INPUT_PULLUP";
        if (pull == "pull_down") return "INPUT_PULLDOWN";
        return "INPUT";
    }

    // instance -> имя глобального объекта HardwareSerial
    // (cores/arduino/HardwareSerial.h: extern HardwareSerial Serial0/Serial1).
    std::string serialObjectName(int instance) { return instance == 0 ? "Serial0" : "Serial1"; }

    // instance -> имя глобального объекта TwoWire. pins_arduino.h
    // (variants/elsomik, переиспользовано для rex_board): Wire0 = I2C0
    // (SDA1/SCL1 = P0.09/P0.10), Wire1 = I2C1 (SDA/SCL = P1.12/P1.13).
    std::string wireObjectName(int instance) { return instance == 0 ? "Wire0" : "Wire1"; }

    // instance -> имя глобального объекта SPIClass
    // (variants/rex_board/pins_arduino.h: SPI0 = SPI_0, SPI1 = SPI_1 — та же
    // нумерация, что spi_onBegin(uint8_t spiNum) в variant.c использует).
    std::string spiObjectName(int instance) { return instance == 0 ? "SPI0" : "SPI1"; }

    // instance -> HAL Instance-указатель (mik32_hal_timer16.h) — тот же
    // список, что knowledge_base/timer16.json::instance_handle, скопирован
    // сюда напрямую: Arduino-ветка не использует слой knowledge_base/*
    // C-ветки (см. комментарий в начале файла про объём v1).
    const char* timer16InstanceHandle(int instance)
    {
        if (instance == 0) return "TIMER16_0";
        if (instance == 1) return "TIMER16_1";
        return "TIMER16_2";
    }

    // prescaler (1/2/4/.../128) -> HAL_Timer16_PrescalerTypeDef enum
    // (mik32_hal_timer16.h) — та же таблица, что
    // knowledge_base/timer16.json::prescaler_hal_enum. findUnsupportedArduinoFeature
    // уже гарантирует, что parser/timer16/timer16_parser.cpp принял только
    // одно из этих 8 значений (enum_int в schemas/timer16.json).
    std::string timer16PrescalerEnum(int prescaler)
    {
        switch (prescaler)
        {
            case 1: return "TIMER16_PRESCALER_1";
            case 2: return "TIMER16_PRESCALER_2";
            case 4: return "TIMER16_PRESCALER_4";
            case 8: return "TIMER16_PRESCALER_8";
            case 16: return "TIMER16_PRESCALER_16";
            case 32: return "TIMER16_PRESCALER_32";
            case 64: return "TIMER16_PRESCALER_64";
            default: return "TIMER16_PRESCALER_128";
        }
    }

    // instance -> ШИМ-вывод, жёстко закреплённый датащитом/HAL
    // (knowledge_base/timer16.json::instance_fixed_pwm_pin,
    // docs/implementations/timer16.md). HAL_TIMER16_MspInit (внутри
    // HAL_Timer16_Init) сам мультиплексирует этот пин в
    // HAL_GPIO_MODE_TIMER_SERIAL — pinMode() в сгенерированном setup() не
    // нужен и не вызывается, как и в C-ветке.
    std::string timer16FixedPwmPinComment(int instance)
    {
        if (instance == 0) return "GPIO_0/PIN_7";
        if (instance == 1) return "GPIO_0/PIN_10";
        return "GPIO_0/PIN_13";
    }

    // data_bits/stop_bits/parity -> SERIAL_<7|8><N|E|O><1|2> (HardwareSerial.h).
    std::string serialConfigMacro(const UartConfig& block)
    {
        std::string out = "SERIAL_";
        out += (block.data_bits == 7) ? "7" : "8";
        if (block.parity == "even") out += "E";
        else if (block.parity == "odd") out += "O";
        else out += "N";
        out += (block.stop_bits == 2) ? "2" : "1";
        return out;
    }

    // Clock/PCC — те же HAL-макросы, что project_codegen.cpp::renderSystemClockConfig
    // (oscillatorEnableMacro/rtcClockSourceMacro/freqMonitorSourceMacro), но
    // своя маленькая копия: та функция сидит в анонимном namespace чужого
    // .cpp, не экспортирована через заголовок, а объём — десяток строк на
    // три switch'а, не стоит заводить общий хедер ради них.
    std::string arduinoOscillatorEnableMacro(const std::string& name)
    {
        if (name == "hsi32m") return "PCC_OSCILLATORTYPE_HSI32M";
        if (name == "osc32m") return "PCC_OSCILLATORTYPE_OSC32M";
        if (name == "lsi32k") return "PCC_OSCILLATORTYPE_LSI32K";
        return "PCC_OSCILLATORTYPE_OSC32K";
    }

    std::string arduinoRtcClockSourceMacro(const std::string& value)
    {
        if (value == "lsi32k") return "PCC_RTC_CLOCK_SOURCE_LSI32K";
        if (value == "osc32k") return "PCC_RTC_CLOCK_SOURCE_OSC32K";
        return "PCC_RTC_CLOCK_SOURCE_AUTO";
    }

    std::string arduinoFreqMonitorSourceMacro(const std::string& value)
    {
        if (value == "lsi32k") return "PCC_FREQ_MONITOR_SOURCE_LSI32K";
        if (value == "osc32k") return "PCC_FREQ_MONITOR_SOURCE_OSC32K";
        return "PCC_FREQ_MONITOR_SOURCE_AUTO";
    }

    // Рендерит замену блока PCC_OscInit внутри SystemInit() вендоренного
    // board.cpp (templates/mik32_arduino_core_snapshot/src/cores/arduino/
    // board.cpp, строки 85-97 на момент написания) — от "PCC_InitTypeDef
    // PCC_OscInit = {0};" включительно до "HAL_PCC_Config(&PCC_OscInit);"
    // включительно. Отступ (4 пробела) и набор полей — побайтовая копия
    // той же структуры, что собирает C-ветка (project_codegen.cpp::
    // renderSystemClockConfig) — единственное отличие: там отдельная
    // функция SystemClock_Config(), здесь — inline внутри уже существующей
    // SystemInit(), потому что Arduino-ядро не предоставляет пользователю
    // отдельного публичного clock API (CLAUDE.md задача: "замена вызова
    // SystemInit" отклонена — полностью переписывать SystemInit() рискует
    // потерять write_csr(mtvec,...)/HAL_Init()/GPIO-тактирование/SysTick/
    // updateCacheLimit, которые сюда не относятся; патчится только сама
    // PCC_OscInit-часть).
    std::string renderArduinoSystemInitPccBlock(const ClockConfig& clock)
    {
        const std::string oscillator_system_literal =
            (clock.oscillator_system == "hsi32m") ? "PCC_OSCILLATORTYPE_HSI32M" : "PCC_OSCILLATORTYPE_OSC32M";

        std::ostringstream oscillator_enable_mask;
        for (size_t i = 0; i < clock.oscillator_enable.size(); ++i)
        {
            if (i > 0) oscillator_enable_mask << " | ";
            oscillator_enable_mask << arduinoOscillatorEnableMacro(clock.oscillator_enable[i]);
        }

        std::ostringstream out;
        out << "    PCC_InitTypeDef PCC_OscInit = {0};\n"
               "    PCC_OscInit.OscillatorEnable = " << oscillator_enable_mask.str() << ";\n"
               "    PCC_OscInit.FreqMon.OscillatorSystem = " << oscillator_system_literal << ";\n"
               "    PCC_OscInit.FreqMon.ForceOscSys = "
            << (clock.force_osc_sys ? "PCC_FORCE_OSC_SYS_FIXED" : "PCC_FORCE_OSC_SYS_UNFIXED") << ";\n"
               "    PCC_OscInit.FreqMon.Force32KClk = " << arduinoFreqMonitorSourceMacro(clock.force_32k_clk) << ";\n"
               "    PCC_OscInit.AHBDivider = " << clock.ahb_divider << ";\n"
               "    PCC_OscInit.APBMDivider = " << clock.apbm_divider << ";\n"
               "    PCC_OscInit.APBPDivider = " << clock.apbp_divider << ";\n"
               "    PCC_OscInit.HSI32MCalibrationValue = " << clock.hsi32m_calibration << ";\n"
               "    PCC_OscInit.LSI32KCalibrationValue = " << clock.lsi32k_calibration << ";\n"
               "    PCC_OscInit.RTCClockSelection = " << arduinoRtcClockSourceMacro(clock.rtc_clock_source) << ";\n"
               "    PCC_OscInit.RTCClockCPUSelection = "
            << (clock.rtc_clock_cpu_source == "lsi32k" ? "PCC_CPU_RTC_CLOCK_SOURCE_LSI32K" : "PCC_CPU_RTC_CLOCK_SOURCE_OSC32K") << ";\n"
               "    HAL_PCC_Config(&PCC_OscInit);\n";
        return out.str();
    }
}

ParseResult<GeneratedProjectFirmwareArduino> generateProjectFirmwareArduino(
    const ProjectConfig& config)
{
    const std::string unsupported = findUnsupportedArduinoFeature(config);
    if (!unsupported.empty())
    {
        return makeError(unsupported);
    }

    std::ostringstream setup;
    for (const GpioConfig& block : config.gpio_blocks)
    {
        const std::string pin_mode = pinModeConstant(block.mode, block.pull);
        for (uint8_t pin : block.pins)
        {
            setup << "  pinMode(" << pinConstant(block.instance, pin) << ", " << pin_mode << ");\n";
        }
    }
    for (const UartConfig& block : config.uart_blocks)
    {
        setup << "  " << serialObjectName(block.instance) << ".begin(" << block.baud_rate
              << ", " << serialConfigMacro(block) << ");\n";
    }
    for (const I2cConfig& block : config.i2c_blocks)
    {
        // findUnsupportedArduinoFeature уже отклонил mode!=master — здесь
        // всегда master, begin(address) (slave) не генерируется.
        // shortcut: TwoWire.begin() (Wire.h) не принимает частоту шины —
        // clock_speed из конфига здесь не применяется; если понадобится
        // конкретная частота, нужно расширять сам Wire-класс ядра, не этот
        // кодоген.
        setup << "  " << wireObjectName(block.instance) << ".begin();\n";
    }
    for (const SpiConfig& block : config.spi_blocks)
    {
        // findUnsupportedArduinoFeature уже отклонил decoder/auto-CS/IRQ —
        // здесь всегда master, поллинг, CS вручную.
        // Баг (найден 2026-10-26, см. docs/open_tasks.md): SPIClass::begin()
        // (cores/arduino/SPI.cpp) только заполняет _hspi.Init и не вызывает
        // HAL_SPI_Init — _isInited выставляется в true только внутри
        // beginTransaction(). Без неё SPIClass::transfer() (SPI.cpp:135,
        // `if (_isInited)`) молча возвращает 0 без обращения к шине. Поэтому
        // после begin() обязательно генерируем beginTransaction() с
        // настройками по умолчанию.
        // shortcut: clk_phase/clk_polarity/baud_rate_div из конфига не
        // применяются — используется SPISettings() по умолчанию
        // (SPI.h: 4 МГц, MSBFIRST, SPI_MODE0), т.к. Arduino-конвенция
        // настраивает конкретный режим через SPISettings прямо перед
        // transfer() в пользовательском коде, а не в setup(); если нужен
        // конкретный режим по умолчанию из конфига — отдельная задача.
        setup << "  " << spiObjectName(block.instance) << ".begin();\n";
        setup << "  " << spiObjectName(block.instance) << ".beginTransaction(SPISettings());\n";
        // Предупреждение (docs/open_tasks.md, п.2, найдено HW-тестом
        // 2026-10-26): пин SS_IN (P0_3 для SPI0, P1_3 для SPI1) физически
        // зарезервирован под аппаратную детекцию MODF в режиме мастера
        // (mik32_hal_spi.c:42-48 — HAL держит его в режиме SPI с подтяжкой
        // к питанию), программный digitalWrite() по нему как по ручному CS
        // бьёт по этой защите. Нет поля конфига, которое бы кодоген мог
        // проверить (пользователь выбирает CS-пин сам в своём коде вне
        // JSON), поэтому это только предупреждающий комментарий, не отказ.
        setup << "  // ВНИМАНИЕ: не используйте "
              << (block.instance == 0 ? "P0_3" : "P1_3")
              << " как ручной CS для " << spiObjectName(block.instance)
              << " — это аппаратный вход SS_IN (детекция MODF), не свободный GPIO.\n";
    }
    // PWM: analogWriteFrequency() общая на проект, пишется один раз до
    // первого analogWrite() (findUnsupportedArduinoFeature уже отбросил
    // конфликтующие частоты между Timer32-блоками).
    for (const Timer32Config& block : config.timer32_blocks)
    {
        if (block.has_frequency)
        {
            setup << "  analogWriteFrequency(" << static_cast<long long>(block.requested_frequency_hz) << ");\n";
            break;
        }
    }
    for (const Timer32Config& block : config.timer32_blocks)
    {
        // variant.c: P0_x -> TIMER32_1, P1_x -> TIMER32_2 (см. комментарий в
        // findUnsupportedArduinoFeature).
        const int gpio_instance = (block.instance == 1) ? 0 : 1;
        for (const Timer32ChannelConfig& channel : block.channels)
        {
            // analogWrite() default resolution — 8 бит (PWM_RESOLUTION_DEFAULT,
            // wiring_analog.c), т.е. значения 0..255.
            const int write_val = static_cast<int>(std::lround(channel.requested_duty_percent / 100.0 * 255.0));
            setup << "  analogWrite(" << pinConstant(gpio_instance, channel.index) << ", " << write_val << ");\n";
        }
    }

    // Timer16 — гибридный прямой HAL-вызов (findUnsupportedArduinoFeature
    // уже отклонил всё, кроме mode=period/internal_system/без
    // триггеров-фильтров-инверсии-прерываний). Хендл объявляется глобально
    // (по образцу htimer16_<instance> в C-ветке, codegen/project/timer16_contribution.cpp)
    // — Timer16_HandleTypeDef должен жить дольше setup(), иначе HAL держит
    // указатель на уничтоженный стековый объект.
    std::ostringstream timer16_globals;
    for (const Timer16Config& block : config.timer16_blocks)
    {
        const std::string handle_name = "htimer16_" + std::to_string(block.instance);
        timer16_globals << "Timer16_HandleTypeDef " << handle_name << ";\n";

        // Частота ШИМ — тот же расчёт, что в C-ветке (timer16_contribution.cpp):
        // sys_clk/(prescaler*(ARR+1)), sys_clk=32МГц фиксирован при
        // clock_source=internal_system (ТЗ §3.1.8).
        const double pwm_freq_hz = 32000000.0 / (static_cast<double>(block.prescaler) * (static_cast<double>(block.arr) + 1.0));

        setup << "  // ГИБРИДНЫЙ вызов (не чистый Arduino API — Tone.cpp исключён из сборки\n"
              << "  // ядра, см. комментарий в findUnsupportedArduinoFeature этого файла):\n"
              << "  // прямая инициализация Timer16_" << block.instance << " через HAL, как в C-ветке.\n"
              << "  // ШИМ-вывод (" << timer16FixedPwmPinComment(block.instance)
              << ") настраивается автоматически внутри HAL_Timer16_Init.\n"
              << "  // Расчётная частота ШИМ ~" << pwm_freq_hz << " Гц.\n"
              << "  " << handle_name << ".Instance = " << timer16InstanceHandle(block.instance) << ";\n"
              << "  " << handle_name << ".Clock.Source = TIMER16_SOURCE_INTERNAL_SYSTEM;\n"
              << "  " << handle_name << ".Clock.Prescaler = " << timer16PrescalerEnum(block.prescaler) << ";\n"
              << "  " << handle_name << ".CountMode = TIMER16_COUNTMODE_INTERNAL;\n"
              << "  " << handle_name << ".ActiveEdge = TIMER16_ACTIVEEDGE_RISING;\n"
              << "  " << handle_name << ".Preload = TIMER16_PRELOAD_AFTERWRITE;\n"
              << "  " << handle_name << ".Trigger.Source = 0;\n"
              << "  " << handle_name << ".Trigger.ActiveEdge = TIMER16_TRIGGER_ACTIVEEDGE_SOFTWARE;\n"
              << "  " << handle_name << ".Trigger.TimeOut = TIMER16_TIMEOUT_DISABLE;\n"
              << "  " << handle_name << ".Filter.ExternalClock = TIMER16_FILTER_NONE;\n"
              << "  " << handle_name << ".Filter.Trigger = TIMER16_FILTER_NONE;\n"
              << "  " << handle_name << ".Waveform.Enable = TIMER16_WAVEFORM_GENERATION_ENABLE;\n"
              << "  " << handle_name << ".Waveform.Polarity = TIMER16_WAVEFORM_POLARITY_NONINVERTED;\n"
              << "  " << handle_name << ".EncoderMode = TIMER16_ENCODER_DISABLE;\n"
              << "  HAL_Timer16_Init(&" << handle_name << ");\n"
              << "  HAL_Timer16_StartPWM(&" << handle_name << ", " << block.arr << ", " << block.cmp << ");\n";
    }

    // CRC — гибридный прямой HAL-вызов (категория 1: периферия, которую
    // Arduino-ядро вообще не трогает — ни один файл cores/arduino/ не
    // объявляет и не использует CRC, конфликтов регистров с Arduino-классами
    // быть не может). Та же логика расчёта полей, что в C-ветке
    // (codegen/project/crc_contribution.cpp) — hcrc объявляется глобально по
    // тому же образцу, что htimer16_<instance> выше.
    std::ostringstream crc_globals;
    if (config.crc_block)
    {
        const CrcConfig& block = *config.crc_block;
        crc_globals << "CRC_HandleTypeDef hcrc;\n";
        setup << "  // ГИБРИДНЫЙ вызов (не Arduino API — CRC не используется никаким\n"
              << "  // классом Arduino-ядра, конфликтов регистров нет): прямая\n"
              << "  // инициализация CRC через HAL, как в C-ветке.\n"
              << "  hcrc.Instance = CRC;\n"
              << "  hcrc.Poly = 0x" << std::hex << block.poly << ";\n"
              << "  hcrc.Init = 0x" << block.init << std::dec << ";\n"
              << "  hcrc.InputReverse = " << (block.input_reverse ? "CRC_REFIN_TRUE" : "CRC_REFIN_FALSE") << ";\n"
              << "  hcrc.OutputReverse = " << (block.output_reverse ? "CRC_REFOUT_TRUE" : "CRC_REFOUT_FALSE") << ";\n"
              << "  hcrc.OutputInversion = "
              << (block.output_inversion ? "CRC_OUTPUTINVERSION_ON" : "CRC_OUTPUTINVERSION_OFF") << ";\n"
              << "  HAL_CRC_Init(&hcrc);\n";
    }

    // TSENS — гибридный прямой HAL-вызов (категория 1, тот же критерий, что у
    // CRC выше: cores/arduino/ не объявляет и не использует TSENS — конфликтов
    // регистров нет). Та же логика полей, что в C-ветке
    // (codegen/project/tsens_contribution.cpp): только конфигурация,
    // HAL_TSENS_SingleStart/ContinuousStart остаются за пользователем.
    std::ostringstream tsens_globals;
    if (config.tsens_block)
    {
        const TsensConfig& block = *config.tsens_block;
        tsens_globals << "TSENS_HandleTypeDef htsens;\n";
        setup << "  // ГИБРИДНЫЙ вызов (не Arduino API — TSENS не используется\n"
              << "  // никаким классом Arduino-ядра, конфликтов регистров нет):\n"
              << "  // прямая инициализация TSENS через HAL, как в C-ветке.\n"
              << "  htsens.Instance = ANALOG_REG;\n"
              << "  htsens.Clock = " << tsensClockSourceMacro(block.clock_source) << ";\n"
              << "  htsens.Frequency = " << block.frequency_hz << ";\n"
              << "  HAL_TSENS_Init(&htsens);\n";
        if (block.has_low_threshold)
        {
            setup << "  HAL_TSENS_SetLowThreshold(&htsens, " << block.low_threshold_celsius << ");\n";
        }
        if (block.has_high_threshold)
        {
            setup << "  HAL_TSENS_SetHiThreshold(&htsens, " << block.high_threshold_celsius << ");\n";
        }
    }

    std::ostringstream includes;
    if (config.tsens_block)
    {
        // extern "C": тот же мотив, что у CRC/Timer16 — mik32_hal_tsens.h не
        // оборачивает объявления в #ifdef __cplusplus.
        includes << "extern \"C\" {\n#include <mik32_hal_tsens.h>\n#include <mik32_hal_pcc.h>\n}\n";
    }
    if (config.crc_block)
    {
        // extern "C": тот же мотив, что у mik32_hal_timer16.h чуть ниже —
        // mik32_hal_crc32.h не оборачивает объявления в #ifdef __cplusplus,
        // без extern "C" компилятор C++ мангл(ир)ует имена HAL_CRC_*, и
        // линковка падает "undefined reference" на реальной сборке.
        includes << "extern \"C\" {\n#include <mik32_hal_crc32.h>\n#include <mik32_hal_pcc.h>\n}\n";
    }
    if (!config.uart_blocks.empty())
    {
        includes << "// Serial0/Serial1 уже объявлены в Arduino.h\n";
    }
    if (!config.i2c_blocks.empty())
    {
        includes << "#include <Wire.h>\n";
    }
    if (!config.spi_blocks.empty())
    {
        includes << "#include <SPI.h>\n";
    }
    if (!config.timer16_blocks.empty())
    {
        // extern "C": mik32_hal_timer16.h не содержит #ifdef __cplusplus сам
        // (HW-находка 2026-10-26, HAL-снапшот framework-mik32v2-sdk) — без
        // этого компилятор C++ мангл(ир)ует имена вызовов (HAL_Timer16_Init
        // и т.п.), а HAL собран как C-библиотека, и линковка падает
        // "undefined reference" на реальной сборке PlatformIO.
        includes << "extern \"C\" {\n#include <mik32_hal_timer16.h>\n}\n";
    }

    std::ostringstream main_cpp;
    main_cpp << "// Сгенерировано MIK32-Configurator (Arduino)";
    if (!config.mcu.empty() || !config.version.empty())
    {
        main_cpp << " — mcu=" << config.mcu << ", version=" << config.version;
    }
    main_cpp << ".\n"
                "#include <Arduino.h>\n"
             << includes.str()
             << "\n"
                "// USER CODE BEGIN Includes\n"
                "// USER CODE END Includes\n"
                "\n"
             << timer16_globals.str()
             << crc_globals.str()
             << tsens_globals.str()
             << "\n"
                "void setup()\n"
                "{\n"
             << setup.str()
             << "  // USER CODE BEGIN Init\n"
                "  // USER CODE END Init\n"
                "}\n"
                "\n"
                "void loop()\n"
                "{\n"
                "  // USER CODE BEGIN Loop\n"
                "  // USER CODE END Loop\n"
                "}\n";

    GeneratedProjectFirmwareArduino result;
    result.main_cpp = main_cpp.str();
    result.system_init_pcc_block = renderArduinoSystemInitPccBlock(config.clock);
    result.ahb_frequency_hz = computeUartPeripheralFrequencyHz(config.clock);
    return ParseResult<GeneratedProjectFirmwareArduino>{true, "", result};
}

namespace
{
    // Независимая копия (тот же приём, что rust_project_codegen.cpp,
    // cmake_project_codegen.cpp — маленькая утилита на каждый
    // writeGenerated*Project, не вынесена ради одной функции на три места).
    std::string uniqueTempDirName(const std::string& output_project_dir)
    {
        static std::atomic<unsigned> counter{0};
        const auto now = std::chrono::steady_clock::now().time_since_epoch();
        const long long ticks = std::chrono::duration_cast<std::chrono::microseconds>(now).count();
        std::ostringstream out;
        out << output_project_dir << ".tmp-generating-" << ticks << "-" << counter.fetch_add(1);
        return out.str();
    }

    bool mergeMoveInto(const fs::path& tmp_root, const fs::path& to_root,
                        const std::vector<std::string>& protected_relative_paths,
                        std::error_code& fs_error)
    {
        fs::recursive_directory_iterator it(tmp_root, fs_error);
        if (fs_error)
        {
            return false;
        }
        for (const fs::recursive_directory_iterator end; it != end; it.increment(fs_error))
        {
            if (fs_error)
            {
                return false;
            }
            const fs::path relative = fs::relative(it->path(), tmp_root, fs_error);
            if (fs_error)
            {
                return false;
            }
            const std::string relative_generic = relative.generic_string();
            const fs::path destination = to_root / relative;
            const bool is_protected = std::find(protected_relative_paths.begin(), protected_relative_paths.end(),
                                                 relative_generic) != protected_relative_paths.end();

            if (it->is_directory(fs_error) && !fs_error)
            {
                fs::create_directories(destination, fs_error);
            }
            else if (!fs_error)
            {
                if (is_protected)
                {
                    std::error_code exists_error;
                    if (fs::exists(destination, exists_error))
                    {
                        continue;
                    }
                }
                fs::create_directories(destination.parent_path(), fs_error);
                if (fs_error)
                {
                    return false;
                }
                std::error_code remove_error;
                fs::remove(destination, remove_error);
                fs::rename(it->path(), destination, fs_error);
                if (fs_error)
                {
                    fs_error.clear();
                    fs::copy_file(it->path(), destination, fs::copy_options::overwrite_existing, fs_error);
                }
            }
            if (fs_error)
            {
                return false;
            }
        }
        return true;
    }
}

CodegenWriteResult writeGeneratedArduinoProject(
    const GeneratedProjectFirmwareArduino& firmware,
    const std::string& pio_skeleton_dir,
    const std::string& arduino_core_snapshot_dir,
    const std::string& output_project_dir,
    const nlohmann::json& project_json_copy)
{
    const fs::path tmp_dir = utf8Path(uniqueTempDirName(output_project_dir));

    std::error_code cleanup_leftover_error;
    fs::remove_all(tmp_dir, cleanup_leftover_error);

    auto fail = [&](const std::string& message) -> CodegenWriteResult
    {
        std::error_code ignore;
        fs::remove_all(tmp_dir, ignore);
        return CodegenWriteResult{false, message};
    };

    std::error_code fs_error;
    fs::create_directories(tmp_dir, fs_error);
    if (fs_error)
    {
        return fail("Не удалось создать временный каталог " + tmp_dir.u8string() + ": " + fs_error.message());
    }

    // --- 1. Скелет PlatformIO-проекта (platformio.ini: framework=arduino) --
    if (!copyDirectoryTreeSkipExisting(pio_skeleton_dir, tmp_dir.u8string(), fs_error))
    {
        return fail("Не удалось скопировать шаблон " + pio_skeleton_dir + ": " + fs_error.message());
    }

    // --- 1b. F_CPU в platformio.ini — патч только при нестандартном Clock
    // (firmware.ahb_frequency_hz != дефолтных 32 МГц). HardwareSerial.cpp и
    // др. вендоренного ядра считают тайминги от F_CPU, который иначе
    // остаётся захардкоженным 32000000UL независимо от реальных делителей
    // AHB/осциллятора — на железе это давало неверную реальную скорость
    // UART при нестандартном Clock-блоке (framing errors), найдено 2026-10-26.
    if (firmware.ahb_frequency_hz != 32000000LL)
    {
        const fs::path pio_ini_path = tmp_dir / "platformio.ini";
        const FileReadOutcome pio_ini_read = readUtf8PathFile(pio_ini_path.u8string());
        if (!pio_ini_read.success)
        {
            return fail("Не удалось прочитать platformio.ini для патча F_CPU: " + pio_ini_read.diagnostic);
        }
        const std::string f_cpu_marker = "-D F_CPU=32000000UL";
        const size_t f_cpu_pos = pio_ini_read.content.find(f_cpu_marker);
        if (f_cpu_pos == std::string::npos)
        {
            return fail("platformio.ini не содержит ожидаемого \"-D F_CPU=32000000UL\" — "
                         "шаблон изменился несовместимо с F_CPU-патчем Clock-блока");
        }
        std::string patched_pio_ini = pio_ini_read.content;
        patched_pio_ini.replace(f_cpu_pos, f_cpu_marker.size(),
            "-D F_CPU=" + std::to_string(firmware.ahb_frequency_hz) + "UL");
        const FileWriteOutcome pio_ini_write = writeUtf8PathFile(pio_ini_path.u8string(), patched_pio_ini);
        if (!pio_ini_write.success)
        {
            return fail("Не удалось записать патченный platformio.ini: " + pio_ini_write.diagnostic);
        }
    }

    // --- 2. Вендоренное Arduino-ядро — lib/mik32_arduino_core/ целиком,
    // включая library.json в корне снэпшота (templates/mik32_arduino_core_snapshot/).
    // Без library.json PlatformIO LDF не распознаёт каталог как библиотеку
    // ("Found 0 compatible libraries") и не компилирует ни cores/arduino/*.c*
    // (включая main.cpp — там определена main(), вызывающая setup()/loop()),
    // ни variants/rex_board/* — найдено при первой реальной сборке
    // сгенерированного проекта через platformio run (firmware.elf не
    // линковался: "undefined reference to `main'").
    std::error_code exists_error;
    if (!fs::exists(utf8Path(arduino_core_snapshot_dir), exists_error))
    {
        return fail("Снэпшот Arduino-ядра не найден: " + arduino_core_snapshot_dir);
    }
    if (!copyDirectoryTreeSkipExisting(arduino_core_snapshot_dir, (tmp_dir / "lib" / "mik32_arduino_core").u8string(), fs_error))
    {
        return fail("Не удалось скопировать Arduino-ядро из " + arduino_core_snapshot_dir + ": " + fs_error.message());
    }

    // --- 2b. Clock/PCC — патч блока PCC_OscInit внутри board.cpp::SystemInit()
    // (см. GeneratedProjectFirmwareArduino::system_init_pcc_block). board.cpp —
    // часть вендоренного ядра, не USER CODE область, поэтому просто
    // перезаписывается целиком (без мерджа) при каждой генерации — так же,
    // как остальные файлы шаблона/ядра, только этот один заменяется
    // целенаправленно уже ПОСЛЕ копирования ядра, а не пропускается
    // skip_existing-копированием.
    const fs::path board_cpp_path =
        tmp_dir / "lib" / "mik32_arduino_core" / "src" / "cores" / "arduino" / "board.cpp";
    const FileReadOutcome board_cpp_read = readUtf8PathFile(board_cpp_path.u8string());
    if (!board_cpp_read.success)
    {
        return fail("Не удалось прочитать board.cpp для патча Clock/PCC: " + board_cpp_read.diagnostic);
    }
    const std::string pcc_block_begin_marker = "    PCC_InitTypeDef PCC_OscInit = {0};\n";
    const std::string pcc_block_end_marker = "    HAL_PCC_Config(&PCC_OscInit);\n";
    const size_t pcc_block_begin = board_cpp_read.content.find(pcc_block_begin_marker);
    const size_t pcc_block_end_marker_pos = board_cpp_read.content.find(pcc_block_end_marker, pcc_block_begin);
    if (pcc_block_begin == std::string::npos || pcc_block_end_marker_pos == std::string::npos)
    {
        return fail("board.cpp не содержит ожидаемого блока PCC_OscInit — "
                     "Arduino-ядро изменилось несовместимо с Clock/PCC-кодогеном");
    }
    const size_t pcc_block_end = pcc_block_end_marker_pos + pcc_block_end_marker.size();
    std::string patched_board_cpp = board_cpp_read.content;
    patched_board_cpp.replace(pcc_block_begin, pcc_block_end - pcc_block_begin, firmware.system_init_pcc_block);
    const FileWriteOutcome board_cpp_write = writeUtf8PathFile(board_cpp_path.u8string(), patched_board_cpp);
    if (!board_cpp_write.success)
    {
        return fail("Не удалось записать патченный board.cpp: " + board_cpp_write.diagnostic);
    }

    // --- 3. src/main.cpp (USER CODE слияние с уже существующим на диске) --
    const fs::path tmp_src = tmp_dir / "src";
    fs::create_directories(tmp_src, fs_error);
    if (fs_error)
    {
        return fail("Не удалось создать " + tmp_src.u8string() + ": " + fs_error.message());
    }

    std::string main_cpp_to_write = firmware.main_cpp;
    const fs::path existing_main_cpp_path = utf8Path(output_project_dir) / "src" / "main.cpp";
    if (fs::exists(existing_main_cpp_path))
    {
        const FileReadOutcome existing = readUtf8PathFile(existing_main_cpp_path.u8string());
        if (existing.success)
        {
            // "//"-маркеры (не "/* */") -> тот же мерджер, что Rust-ветка
            // (user_code_markers_rust.h) использует для "//"-комментариев;
            // bug found by test: исходно здесь стоял C-style mergeUserCodeSections
            // ("/* */" regex) — он никогда не матчил "//"-маркеры main.cpp и
            // молча отдавал свежесгенерированный файл, т.е. терял правки
            // пользователя при каждой регенерации без единой ошибки.
            main_cpp_to_write = mergeUserCodeSectionsRust(firmware.main_cpp, existing.content);
        }
    }
    const FileWriteOutcome main_cpp_write = writeUtf8PathFile((tmp_src / "main.cpp").u8string(), main_cpp_to_write);
    if (!main_cpp_write.success)
    {
        return fail("Не удалось записать src/main.cpp: " + main_cpp_write.diagnostic);
    }

    // --- 4. project.mik32 ----------------------------------------------------
    const FileWriteOutcome project_write =
        writeUtf8PathFile((tmp_dir / "project.mik32").u8string(), project_json_copy.dump(4) + "\n");
    if (!project_write.success)
    {
        return fail("Не удалось записать project.mik32: " + project_write.diagnostic);
    }

    // --- 4b. локальный патч линкер-скрипта (spifi.ld в корне проекта) ------
    // board_build.ldscript=spifi в platformio.ini ищет "spifi.ld" сперва в
    // $PROJECT_DIR, потом в framework/shared/ldscripts — тот же механизм,
    // что уже используется в freertos_codegen.cpp для своего бага. Здесь
    // другая причина: shared/ldscripts/sections.lds установленного SDK не
    // содержит output-секции .init_array вообще — global-конструкторы
    // C++-объектов (Serial0 — HardwareSerial с виртуальными методами из
    // Print/Stream) никогда не вызываются, __libc_init_array() (вызывается
    // из вендоренного cores/arduino/main.cpp) проходит по пустому диапазону
    // __init_array_start==__init_array_end. На практике это means vtable-
    // указатель Serial0 остаётся нулевым: Serial0.write(c) — статический
    // вызов на конкретном объекте, компилятор резолвит его напрямую и
    // работает; Serial0.println()/print() вызывают write() через this
    // внутри Print:: — там точно виртуальный вызов по нулевому vtable,
    // трап, trap_handler не продвигает mepc -> бесконечный повторный вход,
    // внешне выглядит как полное зависание МК. Найдено на 1T Rex Board
    // 2026-10-26. Добавлена секция .init_array (вместе с .preinit_array)
    // в REGION_TEXT, по образцу стандартного newlib/picolibc layout.
    const std::string patched_spifi_ld =
        "OUTPUT_FORMAT(\"elf32-littleriscv\", \"elf32-littleriscv\", \"elf32-littleriscv\")\n"
        "OUTPUT_ARCH(riscv)\n\n"
        "ENTRY(_start)\n\n\n"
        "MEMORY {\n"
        "  spifi (RX): ORIGIN = 0x80000000, LENGTH = 2*1024M\n"
        "  ram (RWX): ORIGIN = 0x02000000, LENGTH = 16K\n"
        "}\n\n"
        "__stack_size = DEFINED(__stack_size) ? __stack_size : 1024;\n\n"
        "SECTION_ALIGNMENT = 4;\n\n"
        "REGION_ALIAS(\"REGION_TEXT\", spifi);\n"
        "REGION_ALIAS(\"REGION_RAM\", ram);\n\n"
        "SECTIONS {\n\n"
        "    .startup ORIGIN(REGION_TEXT) : {\n"
        "        *crt0.o(.text .text.*)\n"
        "    } >REGION_TEXT\n\n"
        "    .ram_text : ALIGN(SECTION_ALIGNMENT) {\n"
        "        PROVIDE(__RAM_TEXT_START__ = .);\n"
        "        *(.small_ram_text)\n"
        "        . = ORIGIN(REGION_RAM) + 0xC0;\n"
        "        PROVIDE(__TRAP_TEXT_START__ = .);\n"
        "        KEEP(*crt0.o(.trap_text))\n"
        "        KEEP(*(.trap_text))\n"
        "        KEEP(*(.trap_text.*))\n"
        "        *(.ram_text)\n"
        "        . = ALIGN(4);\n"
        "        PROVIDE(__RAM_TEXT_END__ = .);\n"
        "    } >REGION_RAM AT>REGION_TEXT\n\n"
        "    __RAM_TEXT_IMAGE_START__ = LOADADDR(.ram_text);\n"
        "    __RAM_TEXT_IMAGE_END__ = LOADADDR(.ram_text) + SIZEOF(.ram_text);\n"
        "    ASSERT(__RAM_TEXT_IMAGE_END__ < ORIGIN(REGION_TEXT) + LENGTH(REGION_TEXT), \"REGION_TEXT segment overflows\")\n\n"
        "    .text : ALIGN(SECTION_ALIGNMENT) {\n"
        "        PROVIDE(__TEXT_START__ = .);\n"
        "        *(.text.SmallSystemInit)\n"
        "        *(.text)\n"
        "        *(.text.*)\n"
        "        *(.gnu.linkonce.t.*)\n"
        "        *(.rodata)\n"
        "        *(.rodata.*)\n"
        "        *(.gnu.linkonce.r.*)\n"
        "        . = ALIGN(4);\n"
        "        PROVIDE(__TEXT_END__ = .);\n"
        "    } >REGION_TEXT\n\n"
        "    .preinit_array : ALIGN(SECTION_ALIGNMENT) {\n"
        "        PROVIDE(__preinit_array_start = .);\n"
        "        KEEP(*(.preinit_array))\n"
        "        PROVIDE(__preinit_array_end = .);\n"
        "    } >REGION_TEXT\n\n"
        "    .init_array : ALIGN(SECTION_ALIGNMENT) {\n"
        "        PROVIDE(__init_array_start = .);\n"
        "        KEEP(*(SORT(.init_array.*)))\n"
        "        KEEP(*(.init_array))\n"
        "        PROVIDE(__init_array_end = .);\n"
        "    } >REGION_TEXT\n\n"
        "    .tdata : ALIGN(SECTION_ALIGNMENT) {\n"
        "        PROVIDE(_tls_data = .);\n"
        "        PROVIDE(_tdata_start = .);\n"
        "        *(.tdata .tdata.*)\n"
        "        *(.gnu.linkonce.td.*)\n"
        "        . = ALIGN(4);\n"
        "        PROVIDE(_tdata_end = .);\n"
        "    } >REGION_RAM AT>REGION_TEXT\n\n"
        "    .tbss : ALIGN(SECTION_ALIGNMENT) {\n"
        "        PROVIDE(_tbss_start = .);\n"
        "        *(.tbss .tbss.*)\n"
        "        *(.gnu.linkonce.tb.*)\n"
        "        . = ALIGN(4);\n"
        "        PROVIDE(_tbss_end = .);\n"
        "    } >REGION_RAM\n\n"
        "    __TDATA_IMAGE_START__ = LOADADDR(.tdata);\n"
        "    __TDATA_IMAGE_END__ = LOADADDR(.tdata) + SIZEOF(.tdata);\n\n"
        "    .data : ALIGN(SECTION_ALIGNMENT) {\n"
        "        PROVIDE(__DATA_START__ = .);\n"
        "        *(.data .data.*)\n"
        "        *(.gnu.linkonce.d.*)\n"
        "        . = ALIGN(4);\n"
        "        PROVIDE(__DATA_END__ = .);\n"
        "    } >REGION_RAM AT>REGION_TEXT\n\n"
        "    .sdata : ALIGN(SECTION_ALIGNMENT) {\n"
        "        PROVIDE(__SDATA_START__ = .);\n"
        "        *(.sdata .sdata.* .sdata2.* .gnu.linkonce.s.*)\n"
        "    } >REGION_RAM AT>REGION_TEXT\n\n"
        "    .srodata : ALIGN(SECTION_ALIGNMENT) {\n"
        "        PROVIDE(__SRODATA_START__ = .);\n"
        "        *(.srodata.cst16) *(.srodata.cst8) *(.srodata.cst4) *(.srodata.cst2) *(.srodata*)\n"
        "    } >REGION_RAM AT>REGION_TEXT\n\n"
        "    __DATA_IMAGE_START__ = LOADADDR(.data);\n"
        "    __DATA_IMAGE_END__ = LOADADDR(.srodata) + SIZEOF(.srodata);\n\n"
        "    .sbss : ALIGN(SECTION_ALIGNMENT) {\n"
        "        PROVIDE(__SBSS_START__ = .);\n"
        "        *(.sbss .sbss.*)\n"
        "        *(.gnu.linkonce.sb.*)\n"
        "        *(.scommon)\n"
        "        . = ALIGN(4);\n"
        "        PROVIDE(__SBSS_END__ = .);\n"
        "    } >REGION_RAM\n\n"
        "    .bss : ALIGN(SECTION_ALIGNMENT) {\n"
        "        PROVIDE(__BSS_START__ = .);\n"
        "        *(.bss .bss.*)\n"
        "        *(.gnu.linkonce.b.*)\n"
        "        *(COMMON)\n"
        "        . = ALIGN(4);\n"
        "        PROVIDE(__BSS_END__ = .);\n"
        "    } >REGION_RAM\n\n"
        "    __global_pointer$ = MIN(__SDATA_START__ + 2016, MAX(__DATA_START__ + 2016, __BSS_END__ - 2016));\n\n"
        "    ASSERT(__BSS_END__ < ORIGIN(REGION_RAM) + LENGTH(REGION_RAM) - __stack_size, \"REGION_RAM section overflows\")\n\n"
        "    PROVIDE(_end = .);\n"
        "    PROVIDE(__end = .);\n"
        "    PROVIDE(end = .);\n\n"
        "    .stack ORIGIN(REGION_RAM) + LENGTH(REGION_RAM) - __stack_size : {\n"
        "        . = ALIGN(16);\n"
        "        PROVIDE( _heap_end = . );\n"
        "        PROVIDE(__STACK_START__ = .);\n"
        "        . = __stack_size;\n"
        "        PROVIDE(__C_STACK_TOP__ = .);\n"
        "        PROVIDE(__STACK_END__ = .);\n"
        "    } >REGION_RAM\n\n"
        "    /DISCARD/ : {\n"
        "        *(.eh_frame .eh_frame.*)\n"
        "    }\n"
        "}\n";

    const FileWriteOutcome ld_write =
        writeUtf8PathFile((tmp_dir / "spifi.ld").u8string(), patched_spifi_ld);
    if (!ld_write.success)
    {
        return fail("Не удалось записать spifi.ld: " + ld_write.diagnostic);
    }

    // --- 5. Перенос из временного каталога в output_project_dir ------------
    static const std::vector<std::string> kProtectedRelativePaths = {"platformio.ini"};

    std::error_code output_exists_error;
    const bool output_exists = fs::exists(utf8Path(output_project_dir), output_exists_error);

    if (!output_exists)
    {
        std::error_code rename_error;
        fs::rename(tmp_dir, utf8Path(output_project_dir), rename_error);
        if (rename_error)
        {
            std::error_code copy_error;
            if (!copyDirectoryTreeSkipExisting(tmp_dir.u8string(), output_project_dir, copy_error))
            {
                return fail("Не удалось перенести временный каталог в " + output_project_dir + ": " + copy_error.message());
            }
            std::error_code cleanup_error;
            fs::remove_all(tmp_dir, cleanup_error);
        }
    }
    else
    {
        std::error_code merge_error;
        if (!mergeMoveInto(tmp_dir, utf8Path(output_project_dir), kProtectedRelativePaths, merge_error))
        {
            return CodegenWriteResult{false, "Ошибка при переносе файлов в " + output_project_dir
                + " (часть файлов уже могла быть перенесена): " + merge_error.message()};
        }
        std::error_code cleanup_error;
        fs::remove_all(tmp_dir, cleanup_error);
    }

    return CodegenWriteResult{true, ""};
}
