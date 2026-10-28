#include "rust_project_codegen.h"
#include "rust_feature_validator.h"
#include "user_code_markers_rust.h"
#include "contribution_utils.h"
#include "win_file_io.h"
#include "utf8_path.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <set>
#include <sstream>
#include <system_error>

namespace fs = std::filesystem;

namespace
{
    ParseResult<GeneratedProjectFirmwareRust> makeError(const std::string& message)
    {
        return ParseResult<GeneratedProjectFirmwareRust>{false, message, std::nullopt};
    }

    // instance -> имя поля Peripherals (mik32v2-pac/src/lib.rs:1832-1836).
    const char* gpioPeripheralField(int instance)
    {
        switch (instance)
        {
            case 0: return "gpio16_0";
            case 1: return "gpio16_1";
            default: return "gpio8_2"; // instance==2 — schemas/gpio.json не пускает другие значения
        }
    }

    // instance -> префикс имени поля Parts (mik32-hal-main/src/gpio.rs,
    // макрос gpio!, строки 231-278: p16_0_N/p16_1_N/p8_2_N).
    const char* gpioPinFieldPrefix(int instance)
    {
        switch (instance)
        {
            case 0: return "p16_0_";
            case 1: return "p16_1_";
            default: return "p8_2_";
        }
    }

    std::string gpioSplitVarName(int instance)
    {
        return "gpio" + std::to_string(instance);
    }

    std::string gpioPinFieldName(int instance, int pin)
    {
        return std::string(gpioPinFieldPrefix(instance)) + std::to_string(pin);
    }

    // mode="output"/"input"+pull -> метод into_* (hal/src/gpio/convert.rs).
    // mode="serial" не проходит через эту функцию — UART заявляет свои пины
    // отдельно (buildUartPinLines ниже), не через config.gpio_blocks.
    // mode="timer_serial"/"analog" отсеяны findUnsupportedRustFeature раньше.
    std::string gpioConvertMethod(const std::string& mode, const std::string& pull)
    {
        if (mode == "output")
        {
            return "into_output";
        }
        if (mode == "serial")
        {
            return "into_serial_port";
        }
        // mode == "input"
        if (pull == "pull_up") return "into_pull_up_input";
        if (pull == "pull_down") return "into_pull_down_input";
        return "into_floating_input"; // pull == "none"
    }

    // drive_strength (schemas/gpio.json enum: "2mA"/"4mA"/"8mA") ->
    // DriveStrength (hal/src/gpio/convert.rs, добавлено конфигуратором).
    std::string driveStrengthVariant(const std::string& drive_strength)
    {
        if (drive_strength == "4mA") return "DriveStrength::Ma4";
        if (drive_strength == "8mA") return "DriveStrength::Ma8";
        return "DriveStrength::Ma2"; // "2mA"
    }

    struct FixedPin { int gpio_instance; int pin; };
    // Датащит МИК32 v2.2.3, Таблица 193 «Назначение выводов», с.320 — те же
    // две пары, что уже зашиты в PinTx/PinRx trait-имплах
    // hal/src/serial.rs:23-27 (mik32-hal-main) и knowledge_base/uart.json:
    // instance_fixed_pins. Не настраиваемо иначе даже теоретически: HAL
    // просто не скомпилируется для другой пары пинов (Pins<U> реализован
    // только для этих конкретных типов).
    FixedPin uartTxPin(int instance) { return instance == 0 ? FixedPin{0, 6} : FixedPin{1, 9}; }
    FixedPin uartRxPin(int instance) { return instance == 0 ? FixedPin{0, 5} : FixedPin{1, 8}; }

    const char* usartPeripheralField(int instance) { return instance == 0 ? "usart_0" : "usart_1"; }

    std::string dataBitsVariant(int data_bits)
    {
        if (data_bits == 7) return "DataBits::Seven";
        if (data_bits == 9) return "DataBits::Nine";
        return "DataBits::Eight";
    }
    std::string stopBitsVariant(int stop_bits)
    {
        return stop_bits == 2 ? "StopBits::Two" : "StopBits::One";
    }
    std::string parityVariant(const std::string& parity)
    {
        if (parity == "even") return "Parity::Even";
        if (parity == "odd") return "Parity::Odd";
        return "Parity::None";
    }

    // Расширение паритета с C-веткой (uart_contribution.cpp:75-91) — 1:1
    // перевод строковых значений JSON (parser/USART/usart_parser.h) в
    // варианты HAL-перечислений serial.rs, те же имена полей/значений,
    // что уже проверил валидатор (schema + parseUsartConfig) к этому моменту.
    std::string bitDirectionVariant(const std::string& bit_direction)
    {
        return bit_direction == "msb_first" ? "BitDirection::Msb" : "BitDirection::Lsb";
    }
    std::string modeVariant(const std::string& mode)
    {
        return mode == "synchronous" ? "Mode::Synchronous" : "Mode::Asynchronous";
    }
    std::string xckModeVariant(const std::string& xck_mode)
    {
        if (xck_mode == "mode1") return "XckMode::Mode1";
        if (xck_mode == "mode2") return "XckMode::Mode2";
        if (xck_mode == "mode3") return "XckMode::Mode3";
        return "XckMode::Mode0";
    }
    std::string channelModeVariant(const std::string& channel_mode)
    {
        return channel_mode == "half_duplex" ? "ChannelMode::HalfDuplex" : "ChannelMode::Duplex";
    }

    // --- I2C ----------------------------------------------------------------
    // Те же фиксированные пины, что knowledge_base/i2c.json:instance_fixed_pins
    // (подтверждено HAL_I2C_MspInit, mik32_hal_i2c.c:3-26) и templates/
    // mik32_rust_hal_snapshot/hal/src/i2c.rs (impl PinI2c<...> для этих же
    // P16_0_9/P16_0_10/P16_1_12/P16_1_13). Пара неразличима (SDA/SCL не
    // подтверждены по отдельности) — здесь просто "первый"/"второй" пин.
    FixedPin i2cPinA(int instance) { return instance == 0 ? FixedPin{0, 9} : FixedPin{1, 12}; }
    FixedPin i2cPinB(int instance) { return instance == 0 ? FixedPin{0, 10} : FixedPin{1, 13}; }

    const char* i2cPeripheralField(int instance) { return instance == 0 ? "i2c_0" : "i2c_1"; }

    std::string i2cModeVariant(const std::string& mode)
    {
        return mode == "slave" ? "i2c::Mode::Slave" : "i2c::Mode::Master";
    }
    std::string i2cAddressModeVariant(const std::string& address_mode)
    {
        return address_mode == "10bit" ? "i2c::AddressMode::Ten" : "i2c::AddressMode::Seven";
    }
    // schemas/i2c.json enum "none".."1111111" -> i2c::OwnAddress2Mask — тот же
    // порядок, что ownAddress2MaskConstant (i2c_contribution.cpp) и
    // oar2.rs::Oa2msk (0..7).
    std::string i2cOwnAddress2MaskVariant(const std::string& mask)
    {
        static const std::map<std::string, std::string> kMap = {
            {"none", "i2c::OwnAddress2Mask::None"},
            {"111111x", "i2c::OwnAddress2Mask::Mask111111X"},
            {"11111xx", "i2c::OwnAddress2Mask::Mask11111XX"},
            {"1111xxx", "i2c::OwnAddress2Mask::Mask1111XXX"},
            {"111xxxx", "i2c::OwnAddress2Mask::Mask111XXXX"},
            {"11xxxxx", "i2c::OwnAddress2Mask::Mask11XXXXX"},
            {"1xxxxxx", "i2c::OwnAddress2Mask::Mask1XXXXXX"},
            {"1111111", "i2c::OwnAddress2Mask::Mask1111111"}};
        const auto it = kMap.find(mask);
        return it != kMap.end() ? it->second : "i2c::OwnAddress2Mask::None";
    }

    struct I2cTimingRust { long presc, scldel, sdadel, sclh, scll; };

    // Независимая копия calcI2cTimingr (codegen/project/i2c_contribution.cpp) —
    // тот же принцип, что и у остальных мелких утилит в этом файле
    // (uniqueTempDirName/insertAfterMarkerLine: маленькая функция,
    // растиражированная, а не вынесенная в общий заголовок ради одного
    // вызывающего места каждой из веток). Формула и HW-подтверждённый
    // пресет для 100 кГц — идентичны, см. комментарий в оригинале.
    I2cTimingRust calcI2cTimingRust(long sys_clk_hz, int clock_speed, int duty_factor)
    {
        if (clock_speed == 100000)
        {
            return I2cTimingRust{1, 15, 15, 75, 75};
        }
        constexpr long kTsyncSigmaFactor = 10;
        long i2c_cycles = sys_clk_hz / clock_speed - kTsyncSigmaFactor;
        const long presc_del = i2c_cycles / 256;
        i2c_cycles = i2c_cycles / (presc_del + 1);

        I2cTimingRust timing;
        timing.presc = presc_del;
        timing.scldel = 0;
        timing.sdadel = 0;
        timing.sclh = i2c_cycles / duty_factor - 1;
        timing.scll = i2c_cycles - i2c_cycles / duty_factor - 1;
        return timing;
    }

    // --- Timer16 --------------------------------------------------------------
    // Те же фиксированные пины, что knowledge_base/timer16.json (instance_fixed_
    // pwm_pin/instance_fixed_input1_pin/instance_fixed_input2_pin), подтверждено
    // HAL_TIMER16_MspInit (mik32_hal_timer16.c) и templates/mik32_rust_hal_snapshot/
    // hal/src/timer16.rs (impl PinTimerPwm/Input1/Input2 для этих же типов).
    FixedPin timer16PwmPin(int instance)
    {
        switch (instance)
        {
            case 0: return FixedPin{0, 7};
            case 1: return FixedPin{0, 10};
            default: return FixedPin{0, 13};
        }
    }
    FixedPin timer16Input1Pin(int instance)
    {
        switch (instance)
        {
            case 0: return FixedPin{0, 5};
            case 1: return FixedPin{0, 8};
            default: return FixedPin{0, 11};
        }
    }
    FixedPin timer16Input2Pin(int instance)
    {
        switch (instance)
        {
            case 0: return FixedPin{0, 6};
            case 1: return FixedPin{0, 9};
            default: return FixedPin{0, 12};
        }
    }

    const char* timer16PeripheralField(int instance)
    {
        switch (instance)
        {
            case 0: return "timer16_0";
            case 1: return "timer16_1";
            default: return "timer16_2";
        }
    }

    // Rust-аналог renderSystemClockConfig (project_codegen.cpp, C-ветка) —
    // та же семантика ClockConfig, но против rcc::Config (templates/
    // mik32_rust_hal_snapshot/hal/src/rcc.rs), а не PCC_InitTypeDef.
    // Поля идентичны C-ветке (ahb/apbm/apbp_divider, калибровки,
    // rtc_clock_source/rtc_clock_cpu_source, oscillator_enable маска,
    // force_osc_sys/force_32k_clk) — здесь только перевод значений на
    // Rust-enum'ы rcc.rs вместо PCC_* макросов.
    std::string renderRustClockConfig(const ClockConfig& clock)
    {
        auto contains = [&clock](const std::string& name) {
            return std::find(clock.oscillator_enable.begin(), clock.oscillator_enable.end(), name)
                != clock.oscillator_enable.end();
        };
        const std::string sys = (clock.oscillator_system == "hsi32m") ? "Hsi32m" : "Osc32m";
        const std::string force_osc_sys = clock.force_osc_sys ? "Fixed" : "Unfixed";
        std::string force_32k_clk = "Automatic";
        if (clock.force_32k_clk == "lsi32k") force_32k_clk = "Lsi32k";
        else if (clock.force_32k_clk == "osc32k") force_32k_clk = "Osc32k";
        std::string rtcclk = "Automatic";
        if (clock.rtc_clock_source == "lsi32k") rtcclk = "Lsi32k";
        else if (clock.rtc_clock_source == "osc32k") rtcclk = "Osc32k";
        const std::string rtccpuclk = (clock.rtc_clock_cpu_source == "lsi32k") ? "Lsi32k" : "Osc32k";

        std::ostringstream out;
        out << "rcc::Config {\n"
               "        hsi32m: " << (contains("hsi32m") ? "true" : "false") << ",\n"
               "        osc32m: " << (contains("osc32m") ? "true" : "false") << ",\n"
               "        lsi32k: " << (contains("lsi32k") ? "true" : "false") << ",\n"
               "        osc32k: " << (contains("osc32k") ? "true" : "false") << ",\n"
               "        freq_monitor: rcc::FreqMonitir {\n"
               "            sys: mik32v2_pac::pm::ahb_mux::AhbClkMux::" << sys << ",\n"
               "            force_osc_sys: mik32v2_pac::pm::ahb_mux::ForceMux::" << force_osc_sys << ",\n"
               "            force32k_clk: mik32v2_pac::wake_up::clocks_sys::Force32kClk::" << force_32k_clk << ",\n"
               "        },\n"
               "        ahb_div: " << clock.ahb_divider << ",\n"
               "        apb_m_div: " << clock.apbm_divider << ",\n"
               "        apb_p_div: " << clock.apbp_divider << ",\n"
               "        hsi32m_calibration_value: " << clock.hsi32m_calibration << ",\n"
               "        lsi32k_calibration_value: " << clock.lsi32k_calibration << ",\n"
               "        rtcclk: mik32v2_pac::wake_up::clocks_bu::RtcClkMux::" << rtcclk << ",\n"
               "        rtccpuclk: mik32v2_pac::pm::cpu_rtc_clk_mux::CpuRtcClkMux::" << rtccpuclk << ",\n"
               "    }";
        return out.str();
    }

    // mik32_hal_timer16.h, HAL_Timer16_SourceTypeDef минус EXTERNAL_INPUT1 —
    // тот же список из 6 вариантов, что kClockSourceInfo в
    // codegen/project/timer16_contribution.cpp (C-ветка).
    std::string timer16ClockSourceVariant(const std::string& clock_source)
    {
        static const std::map<std::string, std::string> kMap = {
            {"internal_system", "timer16::ClockSource::SysClk"},
            {"internal_ahb",    "timer16::ClockSource::Hclk"},
            {"internal_osc32m", "timer16::ClockSource::Osc32m"},
            {"internal_hsi32m", "timer16::ClockSource::Hsi32m"},
            {"internal_osc32k", "timer16::ClockSource::Osc32k"},
            {"internal_lsi32k", "timer16::ClockSource::Lsi32k"}};
        const auto it = kMap.find(clock_source);
        return it != kMap.end() ? it->second : "timer16::ClockSource::SysClk";
    }

    // mik32_hal_timer16.h, HAL_Timer16_PrescalerTypeDef — 8 степеней двойки,
    // то же кодирование, что prescaler_hal_enum в knowledge_base/timer16.json.
    std::string timer16PrescalerVariant(int prescaler)
    {
        switch (prescaler)
        {
            case 1: return "timer16::Prescaler::P1";
            case 2: return "timer16::Prescaler::P2";
            case 4: return "timer16::Prescaler::P4";
            case 8: return "timer16::Prescaler::P8";
            case 16: return "timer16::Prescaler::P16";
            case 32: return "timer16::Prescaler::P32";
            case 64: return "timer16::Prescaler::P64";
            default: return "timer16::Prescaler::P128";
        }
    }

    // mik32_hal_timer16.h, HAL_Timer16_Trigger_ActiveEdgeTypeDef — та же
    // триада, что triggerEdgeConstant (C-ветка).
    std::string timer16TriggerEdgeVariant(const std::string& edge)
    {
        if (edge == "rising") return "timer16::TriggerEdge::Rising";
        if (edge == "falling") return "timer16::TriggerEdge::Falling";
        if (edge == "both") return "timer16::TriggerEdge::Both";
        return "timer16::TriggerEdge::Software";
    }

    // mik32_hal_timer16.h, HAL_Timer16_FilterTypeDef — та же четвёрка, что
    // filterConstant (C-ветка), общая для Filter.ExternalClock/Filter.Trigger.
    std::string timer16FilterVariant(const std::string& filter)
    {
        if (filter == "2clock") return "timer16::Filter::Clock2";
        if (filter == "4clock") return "timer16::Filter::Clock4";
        if (filter == "8clock") return "timer16::Filter::Clock8";
        return "timer16::Filter::None";
    }

    // --- Timer32 ----------------------------------------------------------
    // mik32_hal_timer32.c, HAL_TIMER32_Channel_MspInit:38-65 — Timer32_1
    // канал N всегда на GPIO_0/PIN_N, Timer32_2 канал N всегда на
    // GPIO_1/PIN_N (вызывается автоматически самим HAL в C-ветке через
    // слабую функцию MspInit — в Rust нет такого хука, поэтому кодоген
    // сам переводит пин в TimerSerialMode до создания канала).
    FixedPin timer32ChannelPin(int instance, int channel_index)
    {
        return instance == 1 ? FixedPin{0, channel_index} : FixedPin{1, channel_index};
    }

    const char* timer32PeripheralField(int instance)
    {
        switch (instance)
        {
            case 0: return "timer32_0";
            case 1: return "timer32_1";
            default: return "timer32_2";
        }
    }

    // mik32_hal_timer32.h, HAL_TIMER32_SourceTypeDef — то же покрытие (все 6
    // вариантов, НЕ подмножество, в отличие от Timer16), что kClockSourceInfo
    // в codegen/project/timer32_contribution.cpp (C-ветка).
    std::string timer32ClockSourceVariant(const std::string& clock_source)
    {
        static const std::map<std::string, std::string> kMap = {
            {"prescaler",    "timer32::ClockSource::Prescaler"},
            {"tim1_sys_clk", "timer32::ClockSource::Tim1SysClk"},
            {"tim1_hclk",    "timer32::ClockSource::Tim1Hclk"},
            {"tim2_osc32k",  "timer32::ClockSource::Tim2Osc32k"},
            {"tim2_lsi32k",  "timer32::ClockSource::Tim2Lsi32k"},
            {"tx_pad",       "timer32::ClockSource::TxPad"}};
        auto it = kMap.find(clock_source);
        return it != kMap.end() ? it->second : "timer32::ClockSource::Prescaler";
    }

    // HAL_TIMER32_CountModeTypeDef — "forward"/"reverse"/"bidirectional",
    // те же строки, что Timer32Config.count_mode (parser/timer32/timer32_parser.h).
    std::string timer32CountModeVariant(const std::string& count_mode)
    {
        if (count_mode == "reverse") return "timer32::CountMode::Reverse";
        if (count_mode == "bidirectional") return "timer32::CountMode::Bidirectional";
        return "timer32::CountMode::Forward";
    }

    // HAL_TIMER32_CHANNEL_ModeTypeDef — "pwm"/"capture"/"compare", те же
    // строки, что Timer32ChannelConfig.mode.
    std::string timer32ChannelModeVariant(const std::string& mode)
    {
        if (mode == "capture") return "timer32::ChannelMode::Capture";
        if (mode == "compare") return "timer32::ChannelMode::Compare";
        return "timer32::ChannelMode::Pwm";
    }

    // HAL_TIMER32_CHANNEL_CaptureEdgeTypeDef — "rising"/"falling".
    std::string timer32CaptureEdgeVariant(const std::string& edge)
    {
        return edge == "falling" ? "timer32::CaptureEdge::Falling" : "timer32::CaptureEdge::Rising";
    }

    // --- SPI ------------------------------------------------------------
    // knowledge_base/spi.json: instance_fixed_pins — SCK/MOSI/MISO/SS_IN,
    // резервируются ВСЕГДА (независимо от mode). Как и у I2C, роль каждого
    // пина внутри четвёрки не подтверждена отдельно (см. комментарий в
    // начале templates/mik32_rust_hal_snapshot/hal/src/spi.rs).
    FixedPin spiSckPin(int instance)  { return instance == 0 ? FixedPin{0, 0} : FixedPin{1, 0}; }
    FixedPin spiMosiPin(int instance) { return instance == 0 ? FixedPin{0, 1} : FixedPin{1, 1}; }
    FixedPin spiMisoPin(int instance) { return instance == 0 ? FixedPin{0, 2} : FixedPin{1, 2}; }
    FixedPin spiSsInPin(int instance) { return instance == 0 ? FixedPin{0, 3} : FixedPin{1, 3}; }

    // knowledge_base/spi.json: instance_chip_select_pin — только для
    // mode=master. SPI_0/cs3 физически на GPIO_2 (отличается от остальных
    // трёх, которые всегда на GPIO_1 для instance=1 и смешанно для instance=0).
    FixedPin spiChipSelectPin(int instance, const std::string& chip_select)
    {
        static const std::map<std::string, FixedPin> kInstance0 = {
            {"cs0", FixedPin{0, 4}}, {"cs1", FixedPin{1, 14}}, {"cs2", FixedPin{1, 15}}, {"cs3", FixedPin{2, 6}}};
        static const std::map<std::string, FixedPin> kInstance1 = {
            {"cs0", FixedPin{1, 4}}, {"cs1", FixedPin{1, 5}}, {"cs2", FixedPin{1, 6}}, {"cs3", FixedPin{1, 7}}};
        const auto& table = (instance == 0) ? kInstance0 : kInstance1;
        auto it = table.find(chip_select);
        return it != table.end() ? it->second : FixedPin{0, 4};
    }

    const char* spiPeripheralField(int instance) { return instance == 0 ? "spi_0" : "spi_1"; }

    std::string spiModeVariant(const std::string& mode)
    {
        return mode == "slave" ? "spi::Mode::Slave" : "spi::Mode::Master";
    }

    std::string spiClkPhaseVariant(const std::string& clk_phase)
    {
        return clk_phase == "on" ? "spi::ClkPhase::On" : "spi::ClkPhase::Off";
    }

    std::string spiClkPolarityVariant(const std::string& clk_polarity)
    {
        return clk_polarity == "high" ? "spi::ClkPolarity::High" : "spi::ClkPolarity::Low";
    }

    std::string spiBaudRateDivVariant(int value)
    {
        static const std::map<int, std::string> kMap = {
            {4, "spi::BaudRateDiv::Div4"}, {8, "spi::BaudRateDiv::Div8"}, {16, "spi::BaudRateDiv::Div16"},
            {32, "spi::BaudRateDiv::Div32"}, {64, "spi::BaudRateDiv::Div64"}, {128, "spi::BaudRateDiv::Div128"},
            {256, "spi::BaudRateDiv::Div256"}};
        auto it = kMap.find(value);
        return it != kMap.end() ? it->second : "spi::BaudRateDiv::Div4";
    }

    std::string spiChipSelectVariant(const std::string& chip_select)
    {
        static const std::map<std::string, std::string> kMap = {
            {"cs0", "spi::ChipSelect::Cs0"}, {"cs1", "spi::ChipSelect::Cs1"},
            {"cs2", "spi::ChipSelect::Cs2"}, {"cs3", "spi::ChipSelect::Cs3"}};
        auto it = kMap.find(chip_select);
        return it != kMap.end() ? it->second : "spi::ChipSelect::Cs0";
    }

    // TSENS: clock_source -> PAC ClkMux-вариант (tsens_cfg::ClkMux, сверено с
    // HAL_TSENS_ClockTypeDef, mik32_hal_tsens.h: значения 0-5 совпадают).
    std::string tsensClkMuxVariant(const std::string& clock_source)
    {
        static const std::map<std::string, std::string> kMap = {
            {"sys_clk", "tsens::ClkMux::SysClk"}, {"hclk", "tsens::ClkMux::Hclk"},
            {"osc32m", "tsens::ClkMux::Osc32m"}, {"hsi32m", "tsens::ClkMux::Hsi32m"},
            {"osc32k", "tsens::ClkMux::Osc32k"}, {"lsi32k", "tsens::ClkMux::Lsi32k"}};
        auto it = kMap.find(clock_source);
        return it != kMap.end() ? it->second : "tsens::ClkMux::SysClk";
    }

    // Частота источника tsens.clock_source в Гц — те же номиналы, что в
    // mik32_hal.h (OSC_SYSTEM_VALUE/HSI_VALUE=32000000, OSC_CLOCK_VALUE/
    // LSI_VALUE=32768), sys_clk/hclk считаются от ClockConfig проекта
    // (computeAhbFrequencyHz — см. project_config.h). sys_clk — частота ДО
    // AHB-делителя (сам номинал осциллятора), hclk — ПОСЛЕ (как HAL:
    // systemFreq/(PM->DIV_AHB+1), mik32_hal_tsens.c:160).
    long long tsensClockSourceFrequencyHz(const std::string& clock_source, const ClockConfig& clock)
    {
        if (clock_source == "hclk") return computeAhbFrequencyHz(clock);
        if (clock_source == "osc32k" || clock_source == "lsi32k") return 32768LL;
        return 32000000LL; // sys_clk/osc32m/hsi32m — номинал 32 МГц.
    }

    // Подбор делителя TSENS_CFG.DIV (0..1023) под tsens.frequency_hz —
    // портирован с HAL_TSENS_Clock (mik32_hal_tsens.c:142-171): делитель
    // увеличивается, пока реальная частота не попадёт в допустимые 0..100кГц
    // (Tsens = Fin/(2*(div+1))). Считается на этапе кодогена (как TIMINGR у
    // I2C), не угадано — формула переписана построчно по установленному HAL.
    int tsensComputeDivider(long long source_freq_hz, uint32_t target_freq_hz)
    {
        long long divider = (source_freq_hz / static_cast<long long>(target_freq_hz)) >> 1;
        if (divider < 1) divider = 1;
        long long pre_result = source_freq_hz / (divider << 1);
        while (pre_result > 100000 && divider <= 0x400)
        {
            divider += 1;
            pre_result = source_freq_hz / (divider << 1);
        }
        divider -= 1;
        if (divider < 0) divider = 0;
        if (divider > 0x3FF) divider = 0x3FF;
        return static_cast<int>(divider);
    }

    // TSENS_CELSIUS_TO_VALUE (shared/periphery/analog_reg.h:60) — перевод
    // градусов Цельсия в сырое пороговое значение регистра TRESHOLD (225..603).
    // Переписано побайтово той же целочисленной арифметикой, что в макросе,
    // не упрощено до double, чтобы не разойтись с реальным HAL на округлении.
    int tsensCelsiusToRaw(int celsius)
    {
        long long i100 = static_cast<long long>(celsius) * 100;
        long long denom_inner = i100 + 27315;
        long long denom = ((6406600 - 93 * denom_inner) * 100) / denom_inner;
        long long value = 40960 * 100 / denom;
        if (value < 225) value = 225;
        if (value > 603) value = 603;
        return static_cast<int>(value);
    }
}

ParseResult<GeneratedProjectFirmwareRust> generateProjectFirmwareRust(
    const ProjectConfig& config)
{
    const std::string unsupported = findUnsupportedRustFeature(config);
    if (!unsupported.empty())
    {
        return makeError(unsupported);
    }

    // --- какие GPIO-инстансы нужно split()'ить -----------------------------
    // Объединение явных gpio_blocks и инстансов, неявно нужных UART (fixed
    // pins) — оба потребителя берут поля ИЗ ОДНОГО split(): Parts — обычная
    // struct, разные поля можно "вынести" по отдельности сколько угодно раз
    // разным потребителям, но сам split() можно вызвать только один раз на
    // instance (peripheral.split(self) потребляет исходный PAC-хендл по
    // значению) — см. rust_project_codegen.h.
    std::set<int> gpio_instances;
    for (const GpioConfig& block : config.gpio_blocks)
    {
        gpio_instances.insert(block.instance);
    }
    for (const UartConfig& block : config.uart_blocks)
    {
        gpio_instances.insert(uartTxPin(block.instance).gpio_instance);
        gpio_instances.insert(uartRxPin(block.instance).gpio_instance);
    }
    for (const I2cConfig& block : config.i2c_blocks)
    {
        gpio_instances.insert(i2cPinA(block.instance).gpio_instance);
        gpio_instances.insert(i2cPinB(block.instance).gpio_instance);
    }
    for (const Timer16Config& block : config.timer16_blocks)
    {
        // mode_waveform_enable (knowledge_base/timer16.json): PWM-пин занят
        // только period/one_shot; Input1 — external_event_count/encoder;
        // Input2 — только encoder. Тот же набор условий, что
        // HAL_TIMER16_MspInit (mik32_hal_timer16.c).
        if (block.mode == "period" || block.mode == "one_shot")
        {
            gpio_instances.insert(timer16PwmPin(block.instance).gpio_instance);
        }
        if (block.mode == "external_event_count" || block.mode == "encoder")
        {
            gpio_instances.insert(timer16Input1Pin(block.instance).gpio_instance);
        }
        if (block.mode == "encoder")
        {
            gpio_instances.insert(timer16Input2Pin(block.instance).gpio_instance);
        }
    }
    for (const Timer32Config& block : config.timer32_blocks)
    {
        // Instance=0 без каналов (ТЗ §3.1.5 п.2) — пинов не занимает.
        for (const Timer32ChannelConfig& channel : block.channels)
        {
            gpio_instances.insert(timer32ChannelPin(block.instance, channel.index).gpio_instance);
        }
    }
    for (const SpiConfig& block : config.spi_blocks)
    {
        // SCK/MOSI/MISO/SS_IN резервируются всегда, CS — только для master
        // (mik32_hal_spi.c, HAL_SPI_MspInit).
        gpio_instances.insert(spiSckPin(block.instance).gpio_instance);
        gpio_instances.insert(spiMosiPin(block.instance).gpio_instance);
        gpio_instances.insert(spiMisoPin(block.instance).gpio_instance);
        gpio_instances.insert(spiSsInPin(block.instance).gpio_instance);
        if (block.mode == "master")
        {
            gpio_instances.insert(spiChipSelectPin(block.instance, block.chip_select).gpio_instance);
        }
    }

    const bool has_gpio = !gpio_instances.empty();
    const bool has_uart = !config.uart_blocks.empty();
    const bool has_i2c = !config.i2c_blocks.empty();
    const bool has_timer16 = !config.timer16_blocks.empty();
    const bool has_timer32 = !config.timer32_blocks.empty();
    const bool has_spi = !config.spi_blocks.empty();
    const bool has_rtc = config.rtc_block.has_value();
    const bool has_tsens = config.tsens_block.has_value();
    const bool has_drive_strength = !config.gpio_blocks.empty(); // обязательное поле каждого блока gpio

    // --- код для тела main() -----------------------------------------------
    std::ostringstream splits;
    for (int instance : gpio_instances)
    {
        splits << "    let " << gpioSplitVarName(instance) << " = p." << gpioPeripheralField(instance)
               << ".split();\n";
    }

    std::ostringstream gpio_setup;
    for (const GpioConfig& block : config.gpio_blocks)
    {
        const std::string method = gpioConvertMethod(block.mode, block.pull);
        for (uint8_t pin : block.pins)
        {
            const std::string var_name = "gpio" + std::to_string(block.instance) + "_p" + std::to_string(pin);
            gpio_setup << "    let mut " << var_name << " = "
                       << gpioSplitVarName(block.instance) << "."
                       << gpioPinFieldName(block.instance, pin) << "." << method << "();\n";
            gpio_setup << "    " << var_name << ".set_drive_strength("
                       << driveStrengthVariant(block.drive_strength) << ");\n";
        }
    }

    std::ostringstream uart_setup;
    for (const UartConfig& block : config.uart_blocks)
    {
        const FixedPin tx_pin = uartTxPin(block.instance);
        const FixedPin rx_pin = uartRxPin(block.instance);
        const std::string tx_var = "uart" + std::to_string(block.instance) + "_tx_pin";
        const std::string rx_var = "uart" + std::to_string(block.instance) + "_rx_pin";
        const std::string serial_var = "serial" + std::to_string(block.instance);
        const std::string tx_half = "uart" + std::to_string(block.instance) + "_tx";
        const std::string rx_half = "uart" + std::to_string(block.instance) + "_rx";

        // ТЗ §3.1.3: direction управляет тем, какие направления включены —
        // тот же смысл, что transmitting/receiving в C-ветке
        // (codegen/project/uart_contribution.cpp:69-70), физические пины
        // заявляются безусловно (см. FixedPin выше — HAL сам настраивает
        // TX/RX в serial-режим независимо от направления).
        const std::string transmitting = (block.direction == "tx" || block.direction == "duplex") ? "true" : "false";
        const std::string receiving = (block.direction == "rx" || block.direction == "duplex") ? "true" : "false";

        uart_setup << "    let " << tx_var << " = " << gpioSplitVarName(tx_pin.gpio_instance) << "."
                   << gpioPinFieldName(tx_pin.gpio_instance, tx_pin.pin) << ".into_serial_port();\n";
        uart_setup << "    let " << rx_var << " = " << gpioSplitVarName(rx_pin.gpio_instance) << "."
                   << gpioPinFieldName(rx_pin.gpio_instance, rx_pin.pin) << ".into_serial_port();\n";
        uart_setup << "    let " << serial_var << " = Serial::new(\n"
                   << "        p." << usartPeripheralField(block.instance) << ",\n"
                   << "        (" << tx_var << ", " << rx_var << "),\n"
                   << "        UartConfig {\n"
                   << "            baud_rate: " << block.baud_rate << ",\n"
                   << "            data_bits: " << dataBitsVariant(block.data_bits) << ",\n"
                   << "            stop_bits: " << stopBitsVariant(block.stop_bits) << ",\n"
                   << "            parity: " << parityVariant(block.parity) << ",\n"
                   << "            transmitting: " << transmitting << ",\n"
                   << "            receiving: " << receiving << ",\n"
                   << "            bit_direction: " << bitDirectionVariant(block.bit_direction) << ",\n"
                   << "            mode: " << modeVariant(block.mode) << ",\n"
                   << "            xck_mode: " << xckModeVariant(block.xck_mode) << ",\n"
                   << "            last_byte_clock: " << (block.last_byte_clock ? "true" : "false") << ",\n"
                   << "            channel_mode: " << channelModeVariant(block.channel_mode) << ",\n"
                   << "            tx_inversion: " << (block.tx_inversion ? "true" : "false") << ",\n"
                   << "            rx_inversion: " << (block.rx_inversion ? "true" : "false") << ",\n"
                   << "            swap: " << (block.swap ? "true" : "false") << ",\n"
                   << "            flow_control: " << (block.flow_control ? "true" : "false") << ",\n"
                   << "        },\n"
                   << "    );\n";
        // Ни tx_half, ни rx_half не мутируются — пустой while(1) ничего в них
        // не пишет и не читает, без "let _ = &..." компилятор дал бы
        // unused_variables warning.
        uart_setup << "    let (" << tx_half << ", " << rx_half << ") = "
                   << serial_var << ".split();\n";
        uart_setup << "    let _ = &" << tx_half << ";\n";
        uart_setup << "    let _ = &" << rx_half << ";\n";
    }

    // ТЗ по I2C молчит (см. docs/implementations/i2c.md) — то же, что и
    // C-ветка. sys_clk_hz=32МГц/duty_factor=2 — те же хардкоды, что
    // knowledge_base/i2c.json (sys_clk_hz, duty_factor_default), используемые
    // calcI2cTimingr в C-ветке; здесь не читаем JSON knowledge_base заново
    // (RustProjectCodegen работает только с ProjectConfig) — тот же приём,
    // что HSI_HZ=32_000_000 хардкод прямо в serial.rs/rust_project_codegen.cpp
    // для UART.
    std::ostringstream i2c_setup;
    for (const I2cConfig& block : config.i2c_blocks)
    {
        const FixedPin pin_a = i2cPinA(block.instance);
        const FixedPin pin_b = i2cPinB(block.instance);
        const std::string pin_a_var = "i2c" + std::to_string(block.instance) + "_pin_a";
        const std::string pin_b_var = "i2c" + std::to_string(block.instance) + "_pin_b";
        const std::string i2c_var = "i2c" + std::to_string(block.instance);
        constexpr long kSysClkHz = 32000000;
        constexpr int kDutyFactor = 2;
        const I2cTimingRust timing = calcI2cTimingRust(kSysClkHz, block.clock_speed, kDutyFactor);

        // Открытый сток I2C требует подтяжки на обоих пинах (знание
        // knowledge_base/i2c.json:instance_fixed_pins, подтверждено
        // HAL_I2C_MspInit: Pull=HAL_GPIO_PULL_UP) — into_serial_port() сам
        // подтяжку не включает (см. gpio/convert.rs), добавляем set_pull_up().
        i2c_setup << "    let mut " << pin_a_var << " = " << gpioSplitVarName(pin_a.gpio_instance) << "."
                  << gpioPinFieldName(pin_a.gpio_instance, pin_a.pin) << ".into_serial_port();\n";
        i2c_setup << "    " << pin_a_var << ".set_pull_up();\n";
        i2c_setup << "    let mut " << pin_b_var << " = " << gpioSplitVarName(pin_b.gpio_instance) << "."
                  << gpioPinFieldName(pin_b.gpio_instance, pin_b.pin) << ".into_serial_port();\n";
        i2c_setup << "    " << pin_b_var << ".set_pull_up();\n";
        i2c_setup << "    let mut " << i2c_var << " = I2c::new(\n"
                   << "        p." << i2cPeripheralField(block.instance) << ",\n"
                   << "        (" << pin_a_var << ", " << pin_b_var << "),\n"
                   << "        i2c::Config {\n"
                   << "            mode: " << i2cModeVariant(block.mode) << ",\n"
                   << "            timing: i2c::Timing { presc: " << timing.presc << ", scldel: " << timing.scldel
                   << ", sdadel: " << timing.sdadel << ", sclh: " << timing.sclh << ", scll: " << timing.scll << " },\n"
                   << "            digital_filter: " << block.digital_filter << ",\n"
                   << "            analog_filter: " << (block.analog_filter ? "true" : "false") << ",\n"
                   << "            own_address: " << block.own_address << ",\n"
                   << "            address_mode: " << i2cAddressModeVariant(block.address_mode) << ",\n"
                   << "            dual_address_enabled: " << (block.dual_address_enabled ? "true" : "false") << ",\n"
                   << "            own_address_2: " << block.own_address_2 << ",\n"
                   << "            own_address_2_mask: " << i2cOwnAddress2MaskVariant(block.own_address_2_mask) << ",\n"
                   << "            general_call: " << (block.general_call ? "true" : "false") << ",\n"
                   << "            no_stretch_mode: " << (block.no_stretch_mode ? "true" : "false") << ",\n"
                   << "        },\n"
                   << "    );\n";
        // Ни IRQ/DMA, ни фактические транзакции кодоген не генерирует (как и
        // в C-ветке, где кодоген ограничивается Init/Reset) — "let _ = &..."
        // по тому же принципу, что tx_half/rx_half у UART, чтобы пустой
        // while(1) не давал unused_variables/unused_mut warning.
        i2c_setup << "    let _ = &" << i2c_var << ";\n";
    }

    // Timer16 — Rust-паритет с timer16_contribution.cpp (C-ветка): mode=
    // period/one_shot/external_event_count/encoder, 6 из 7 clock_source,
    // trigger_edge, фильтры, waveform_inverted. Trigger.Source НЕ реализован
    // (отклоняется rust_feature_validator.cpp) — тот же пробел, что и в
    // C-ветке (knowledge_base/timer16.json: not_covered_by_this_codegen.trigger).
    // Таймер после запуска работает аппаратно без участия CPU — то же
    // обоснование, что уже дано в timer16_contribution.h ("Флага emit_demo_loop
    // нет намеренно"), поэтому переменная остаётся непотрогана дальше
    // ("let _ = &...", как у i2c_var/tx_half/rx_half выше).
    std::ostringstream timer16_setup;
    for (const Timer16Config& block : config.timer16_blocks)
    {
        const std::string var = "timer16_" + std::to_string(block.instance);
        const std::string config_var = var + "_config";
        timer16_setup << "    let " << config_var << " = timer16::Config {\n"
                      << "        prescaler: " << timer16PrescalerVariant(block.prescaler) << ",\n"
                      << "        trigger_edge: " << timer16TriggerEdgeVariant(block.trigger_edge) << ",\n"
                      << "        external_clock_filter: " << timer16FilterVariant(block.external_clock_filter) << ",\n"
                      << "        trigger_filter: " << timer16FilterVariant(block.trigger_filter) << ",\n"
                      << "    };\n";

        if (block.mode == "period" || block.mode == "one_shot")
        {
            const FixedPin pwm_pin = timer16PwmPin(block.instance);
            const std::string pwm_var = var + "_pwm_pin";
            timer16_setup << "    let " << pwm_var << " = " << gpioSplitVarName(pwm_pin.gpio_instance) << "."
                          << gpioPinFieldName(pwm_pin.gpio_instance, pwm_pin.pin) << ".into_timer_serial();\n";
            timer16_setup << "    let mut " << var << " = Timer16::new_pwm(\n"
                          << "        p." << timer16PeripheralField(block.instance) << ",\n"
                          << "        " << pwm_var << ",\n"
                          << "        " << timer16ClockSourceVariant(block.clock_source) << ",\n"
                          << "        " << config_var << ",\n"
                          << "        " << (block.waveform_inverted ? "true" : "false") << ",\n"
                          << "        " << (block.mode == "one_shot" ? "true" : "false") << ",\n"
                          << "        " << block.arr << ",\n"
                          << "        " << block.cmp << ",\n"
                          << "    );\n";
        }
        else if (block.mode == "external_event_count")
        {
            const FixedPin input1_pin = timer16Input1Pin(block.instance);
            const std::string input1_var = var + "_input1_pin";
            timer16_setup << "    let " << input1_var << " = " << gpioSplitVarName(input1_pin.gpio_instance) << "."
                          << gpioPinFieldName(input1_pin.gpio_instance, input1_pin.pin) << ".into_timer_serial();\n";
            timer16_setup << "    let mut " << var << " = Timer16::new_counter(\n"
                          << "        p." << timer16PeripheralField(block.instance) << ",\n"
                          << "        " << input1_var << ",\n"
                          << "        " << config_var << ",\n"
                          << "        " << block.arr << ",\n"
                          << "    );\n";
        }
        else // mode == "encoder"
        {
            const FixedPin input1_pin = timer16Input1Pin(block.instance);
            const FixedPin input2_pin = timer16Input2Pin(block.instance);
            const std::string input1_var = var + "_input1_pin";
            const std::string input2_var = var + "_input2_pin";
            timer16_setup << "    let " << input1_var << " = " << gpioSplitVarName(input1_pin.gpio_instance) << "."
                          << gpioPinFieldName(input1_pin.gpio_instance, input1_pin.pin) << ".into_timer_serial();\n";
            timer16_setup << "    let " << input2_var << " = " << gpioSplitVarName(input2_pin.gpio_instance) << "."
                          << gpioPinFieldName(input2_pin.gpio_instance, input2_pin.pin) << ".into_timer_serial();\n";
            timer16_setup << "    let mut " << var << " = Timer16::new_encoder(\n"
                          << "        p." << timer16PeripheralField(block.instance) << ",\n"
                          << "        " << input1_var << ",\n"
                          << "        " << input2_var << ",\n"
                          << "        " << config_var << ",\n"
                          << "        " << block.arr << ",\n"
                          << "    );\n";
        }
        timer16_setup << "    let _ = &" << var << ";\n";
    }

    // Timer32 — Rust-паритет с timer32_contribution.cpp (C-ветка): count_mode
    // (forward/reverse/bidirectional), все 6 clock_source, prescaler/top как
    // сырые числа (НЕ индекс enum'а), каналы 0-3 (pwm/capture/compare) ТОЛЬКО
    // для instance 1/2 — Timer32::new() настраивает счётчик без запуска,
    // configure_channel() — по одному каналу, start() — единственный финальный
    // вызов (порядок как в C: Channel_Init для всех каналов, затем Start).
    // Errata §9.1 (сброс флагов) переносится в сгенерированный код только как
    // шаблон-комментарий, аналогично buildErrata91IrqTemplate в C-ветке —
    // сам обработчик прерываний эта кодогенерация не создаёт ни в C, ни в
    // Rust (knowledge_base/timer32.json: not_covered_by_this_codegen.interrupt_wiring).
    std::ostringstream timer32_setup;
    for (const Timer32Config& block : config.timer32_blocks)
    {
        const std::string var = "timer32_" + std::to_string(block.instance);
        timer32_setup << "    let mut " << var << " = timer32::Timer32::new(\n"
                      << "        p." << timer32PeripheralField(block.instance) << ",\n"
                      << "        " << timer32ClockSourceVariant(block.clock_source) << ",\n"
                      << "        " << timer32CountModeVariant(block.count_mode) << ",\n"
                      << "        " << block.prescaler << ",\n"
                      << "        " << block.top << ",\n"
                      << "    );\n";

        for (const Timer32ChannelConfig& channel : block.channels)
        {
            const FixedPin channel_pin = timer32ChannelPin(block.instance, channel.index);
            const std::string pin_var = var + "_ch" + std::to_string(channel.index) + "_pin";
            timer32_setup << "    let " << pin_var << " = " << gpioSplitVarName(channel_pin.gpio_instance) << "."
                          << gpioPinFieldName(channel_pin.gpio_instance, channel_pin.pin) << ".into_timer_serial();\n"
                          << "    let _ = &" << pin_var << ";\n"
                          << "    " << var << ".configure_channel(\n"
                          << "        " << channel.index << ",\n"
                          << "        timer32::ChannelConfig {\n"
                          << "            mode: " << timer32ChannelModeVariant(channel.mode) << ",\n"
                          << "            ocr: " << channel.ocr << ",\n"
                          << "            pwm_invert: " << (channel.pwm_invert ? "true" : "false") << ",\n"
                          << "            capture_edge: " << timer32CaptureEdgeVariant(channel.capture_edge) << ",\n"
                          << "            noise_filter: " << (channel.noise_filter ? "true" : "false") << ",\n"
                          << "        },\n"
                          << "    );\n";
        }

        timer32_setup << "    " << var << ".start("
                      << (block.overflow_interrupt ? "true" : "false") << ", "
                      << (block.underflow_interrupt ? "true" : "false") << ");\n";
        timer32_setup << "    let _ = &" << var << ";\n";
    }

    // SPI — Rust-паритет с spi_contribution.cpp (C-ветка) за вычетом поля
    // interrupts (отклонено в rust_feature_validator.cpp, см. spi.rs).
    // SCK/MOSI/MISO/SS_IN заявляются всегда, CS-пин — отдельным аргументом
    // только для master (slave вообще не вызывает new_master).
    std::ostringstream spi_setup;
    for (const SpiConfig& block : config.spi_blocks)
    {
        const std::string var = "spi" + std::to_string(block.instance);
        const FixedPin sck = spiSckPin(block.instance);
        const FixedPin mosi = spiMosiPin(block.instance);
        const FixedPin miso = spiMisoPin(block.instance);
        const FixedPin ss_in = spiSsInPin(block.instance);
        const std::string sck_var = var + "_sck";
        const std::string mosi_var = var + "_mosi";
        const std::string miso_var = var + "_miso";
        const std::string ss_in_var = var + "_ss_in";

        spi_setup << "    let " << sck_var << " = " << gpioSplitVarName(sck.gpio_instance) << "."
                  << gpioPinFieldName(sck.gpio_instance, sck.pin) << ".into_serial_port();\n";
        spi_setup << "    let " << mosi_var << " = " << gpioSplitVarName(mosi.gpio_instance) << "."
                  << gpioPinFieldName(mosi.gpio_instance, mosi.pin) << ".into_serial_port();\n";
        spi_setup << "    let " << miso_var << " = " << gpioSplitVarName(miso.gpio_instance) << "."
                  << gpioPinFieldName(miso.gpio_instance, miso.pin) << ".into_serial_port();\n";
        // n_ss_in — подтяжка к питанию только при mode=master (mik32_hal_spi.c:
        // "в режиме ведущего вывод SPIx_N_SS_IN должен быть ... с подтяжкой").
        spi_setup << "    let mut " << ss_in_var << " = " << gpioSplitVarName(ss_in.gpio_instance) << "."
                  << gpioPinFieldName(ss_in.gpio_instance, ss_in.pin) << ".into_serial_port();\n";
        if (block.mode == "master")
        {
            spi_setup << "    " << ss_in_var << ".set_pull_up();\n";
        }

        const std::string config_expr = "spi::Config {\n"
            "            mode: " + spiModeVariant(block.mode) + ",\n"
            "            clk_phase: " + spiClkPhaseVariant(block.clk_phase) + ",\n"
            "            clk_polarity: " + spiClkPolarityVariant(block.clk_polarity) + ",\n"
            "            threshold_tx: " + std::to_string(block.threshold_tx) + ",\n"
            "            baud_rate_div: " + spiBaudRateDivVariant(block.baud_rate_div) + ",\n"
            "            decoder: " + (block.decoder ? "true" : "false") + ",\n"
            "            manual_cs: " + (block.manual_cs ? "true" : "false") + ",\n"
            "            chip_select: " + spiChipSelectVariant(block.chip_select) + ",\n"
            "        }";

        if (block.mode == "master")
        {
            const FixedPin cs = spiChipSelectPin(block.instance, block.chip_select);
            const std::string cs_var = var + "_cs";
            spi_setup << "    let " << cs_var << " = " << gpioSplitVarName(cs.gpio_instance) << "."
                      << gpioPinFieldName(cs.gpio_instance, cs.pin) << ".into_serial_port();\n";
            spi_setup << "    let mut " << var << " = Spi::new_master(\n"
                      << "        p." << spiPeripheralField(block.instance) << ",\n"
                      << "        (" << sck_var << ", " << mosi_var << ", " << miso_var << ", " << ss_in_var << "),\n"
                      << "        " << cs_var << ",\n"
                      << "        " << config_expr << ",\n"
                      << "    );\n";
        }
        else
        {
            spi_setup << "    let mut " << var << " = Spi::new_slave(\n"
                      << "        p." << spiPeripheralField(block.instance) << ",\n"
                      << "        (" << sck_var << ", " << mosi_var << ", " << miso_var << ", " << ss_in_var << "),\n"
                      << "        " << config_expr << ",\n"
                      << "    );\n";
        }
        // Как и у I2C/Timer16 — только Init, без транзакций (их делает
        // пользователь вручную через .free()), поэтому "let _ = &...".
        spi_setup << "    let _ = &" << var << ";\n";
    }

    // RTC — Rust-паритет с rtc_contribution.cpp (C-ветка): время+дата+
    // будильник, частичное сравнение полей. EPIC/trap_handler-подключение
    // interrupt_enabled остаётся на стороне пользователя даже в C-ветке —
    // поэтому Rust-ветка не отклоняет это поле (docs/implementations/rust/rtc.md).
    std::ostringstream rtc_setup;
    if (config.rtc_block.has_value())
    {
        const RtcConfig& rtc = *config.rtc_block;
        rtc_setup << "    let rtc = mik32v2_hal::rtc::Rtc::new(p.rtc, &p.pm, mik32v2_hal::rtc::Config {\n"
                     "        time: mik32v2_hal::rtc::Time { dow: " << rtc.dow << ", hours: " << rtc.hours
                  << ", minutes: " << rtc.minutes << ", seconds: " << rtc.seconds << " },\n"
                     "        date: mik32v2_hal::rtc::Date { century: " << rtc.century << ", day: " << rtc.day
                  << ", month: " << rtc.month << ", year: " << rtc.year << " },\n";
        if (rtc.has_alarm)
        {
            auto has = [&](const char* field)
            {
                return std::find(rtc.alarm_compare_fields.begin(), rtc.alarm_compare_fields.end(), field)
                       != rtc.alarm_compare_fields.end() ? "true" : "false";
            };
            rtc_setup << "        alarm: Some(mik32v2_hal::rtc::Alarm {\n"
                         "            time: mik32v2_hal::rtc::Time { dow: " << rtc.alarm_dow << ", hours: "
                      << rtc.alarm_hours << ", minutes: " << rtc.alarm_minutes << ", seconds: " << rtc.alarm_seconds
                      << " },\n"
                         "            date: mik32v2_hal::rtc::Date { century: " << rtc.alarm_century << ", day: "
                      << rtc.alarm_day << ", month: " << rtc.alarm_month << ", year: " << rtc.alarm_year << " },\n"
                         "            compare: mik32v2_hal::rtc::AlarmCompareFields {\n"
                         "                dow: " << has("dow") << ", hours: " << has("hours") << ", minutes: "
                      << has("minutes") << ", seconds: " << has("seconds") << ",\n"
                         "                century: " << has("century") << ", day: " << has("day") << ", month: "
                      << has("month") << ", year: " << has("year") << ",\n"
                         "            },\n"
                         "            interrupt_enabled: " << (rtc.alarm_interrupt_enabled ? "true" : "false") << ",\n"
                         "        }),\n";
        }
        else
        {
            rtc_setup << "        alarm: None,\n";
        }
        rtc_setup << "    });\n"
                     "    let _ = &rtc;\n";
    }

    // CRC — Rust-паритет с crc_contribution.cpp (C-ветка): только
    // конфигурация алгоритма, write_data/read_crc остаются за пользователем
    // (как в C — нечем угадать данные наперёд), docs/implementations/rust/crc.md.
    std::ostringstream crc_setup;
    if (config.crc_block.has_value())
    {
        const CrcConfig& crc = *config.crc_block;
        std::ostringstream poly_hex, init_hex;
        poly_hex << std::hex << crc.poly;
        init_hex << std::hex << crc.init;
        crc_setup << "    let crc = mik32v2_hal::crc::Crc::new(p.crc, &p.pm, mik32v2_hal::crc::Config {\n"
                     "        poly: 0x" << poly_hex.str() << ",\n"
                     "        init: 0x" << init_hex.str() << ",\n"
                     "        input_reverse: " << (crc.input_reverse ? "true" : "false") << ",\n"
                     "        output_reverse: " << (crc.output_reverse ? "true" : "false") << ",\n"
                     "        output_inversion: " << (crc.output_inversion ? "true" : "false") << ",\n"
                     "    });\n"
                     "    let _ = &crc;\n";
    }

    // TSENS — Rust-паритет с tsens_contribution.cpp (C-ветка): только
    // конфигурация тактирования/порогов/прерываний. SingleStart/
    // ContinuousStart остаются за пользователем (docs/implementations/rust/tsens.md).
    // div/raw-пороги считаются здесь (codegen-time), не угаданы в рантайме —
    // см. tsensComputeDivider/tsensCelsiusToRaw выше.
    std::ostringstream tsens_setup;
    if (config.tsens_block.has_value())
    {
        const TsensConfig& tsens = *config.tsens_block;
        const long long source_freq = tsensClockSourceFrequencyHz(tsens.clock_source, config.clock);
        const int div = tsensComputeDivider(source_freq, tsens.frequency_hz);
        tsens_setup << "    let tsens = mik32v2_hal::tsens::Tsens::new(p.tsens, &p.pm, mik32v2_hal::tsens::Config {\n"
                       "        clk_mux: " << tsensClkMuxVariant(tsens.clock_source) << ",\n"
                       "        div: " << div << ",\n";
        if (tsens.has_low_threshold)
        {
            tsens_setup << "        low_threshold_raw: Some(" << tsensCelsiusToRaw(tsens.low_threshold_celsius) << "),\n";
        }
        else
        {
            tsens_setup << "        low_threshold_raw: None,\n";
        }
        if (tsens.has_high_threshold)
        {
            tsens_setup << "        high_threshold_raw: Some(" << tsensCelsiusToRaw(tsens.high_threshold_celsius) << "),\n";
        }
        else
        {
            tsens_setup << "        high_threshold_raw: None,\n";
        }
        auto hasInterrupt = [&](const char* name)
        {
            return std::find(tsens.interrupts.begin(), tsens.interrupts.end(), name) != tsens.interrupts.end()
                   ? "true" : "false";
        };
        tsens_setup << "        enable_low_irq: " << hasInterrupt("low") << ",\n"
                       "        enable_high_irq: " << hasInterrupt("high") << ",\n"
                       "        enable_eoc_irq: " << hasInterrupt("eoc") << ",\n"
                       "    });\n"
                       "    let _ = &tsens;\n";
    }

    std::ostringstream main_rs;
    main_rs << "//! Сгенерировано MIK32-Configurator (Rust)";
    if (!config.mcu.empty() || !config.version.empty())
    {
        main_rs << " — mcu=" << config.mcu << ", version=" << config.version;
    }
    main_rs << ".\n"
               "#![no_std]\n"
               "#![no_main]\n"
               "\n"
               "use core::panic::PanicInfo;\n"
               "use mik32_rt::entry;\n"
               // Обязательно (не просто стиль) — без явной ссылки на крейт
               // Rust не линкует его: mik32_rt::entry берётся из
               // mik32_rt_macros (proc-macro, не оставляет рантайм-символов
               // сама по себе), поэтому без "use mik32_rt as _;" линковщик
               // не подтягивает asm.rs (_start/trap_entry) из mik32-rt —
               // сборка формально проходит, но без точки входа вообще, без
               // единой ошибки (см. docs/implementations/rust_codegen.md).
               "use mik32_rt as _;\n"
               // Аналогично обязательно: mik32v2-pac по умолчанию требует
               // реализацию critical-section (default = ["critical-section"]),
               // которую регистрирует riscv (фича critical-section-single-hart,
               // Cargo.toml сгенерированного проекта) — но только если сам
               // крейт riscv на что-то сослались, иначе он не линкуется и
               // сборка падает с "undefined symbol: _critical_section_1_0_acquire".
               "use riscv as _;\n"
               "use mik32v2_pac::Peripherals;\n"
               "use mik32v2_hal::rcc;\n";
    if (has_gpio)
    {
        main_rs << "use mik32v2_hal::gpio::GpioExt;\n";
    }
    if (has_drive_strength)
    {
        main_rs << "use mik32v2_hal::gpio::DriveStrength;\n";
    }
    if (has_uart)
    {
        main_rs << "use mik32v2_hal::serial::{Serial, Config as UartConfig, DataBits, StopBits, Parity,\n"
                   "    BitDirection, Mode, XckMode, ChannelMode};\n";
    }
    if (has_i2c)
    {
        main_rs << "use mik32v2_hal::i2c::{self, I2c};\n";
    }
    if (has_timer16)
    {
        main_rs << "use mik32v2_hal::timer16::{self, Timer16};\n";
    }
    if (has_timer32)
    {
        main_rs << "use mik32v2_hal::timer32;\n";
    }
    if (has_spi)
    {
        main_rs << "use mik32v2_hal::spi::{self, Spi};\n";
    }
    if (has_tsens)
    {
        main_rs << "use mik32v2_hal::tsens;\n";
    }
    main_rs << "\n"
               "// USER CODE BEGIN Includes\n"
               "// USER CODE END Includes\n"
               "\n"
               "#[entry]\n"
               "fn main() -> ! {\n"
               "    rcc::Config::init(" << renderRustClockConfig(config.clock) << ");\n"
               "\n"
               "    let p = Peripherals::take().unwrap();\n"
               "\n"
            << splits.str();
    if (!splits.str().empty())
    {
        main_rs << "\n";
    }
    main_rs << gpio_setup.str();
    if (!gpio_setup.str().empty())
    {
        main_rs << "\n";
    }
    main_rs << uart_setup.str();
    if (!uart_setup.str().empty())
    {
        main_rs << "\n";
    }
    main_rs << i2c_setup.str();
    if (!i2c_setup.str().empty())
    {
        main_rs << "\n";
    }
    main_rs << timer16_setup.str();
    if (!timer16_setup.str().empty())
    {
        main_rs << "\n";
    }
    main_rs << timer32_setup.str();
    if (!timer32_setup.str().empty())
    {
        main_rs << "\n";
    }
    main_rs << spi_setup.str();
    if (!spi_setup.str().empty())
    {
        main_rs << "\n";
    }
    main_rs << rtc_setup.str();
    if (!rtc_setup.str().empty())
    {
        main_rs << "\n";
    }
    main_rs << crc_setup.str();
    if (!crc_setup.str().empty())
    {
        main_rs << "\n";
    }
    main_rs << tsens_setup.str();
    if (!tsens_setup.str().empty())
    {
        main_rs << "\n";
    }
    main_rs << "    // USER CODE BEGIN Init\n"
               "    // USER CODE END Init\n"
               "\n"
               "    loop {\n"
               "        // USER CODE BEGIN Loop\n"
               "        // USER CODE END Loop\n"
               "    }\n"
               "}\n"
               "\n"
               "#[unsafe(export_name = \"trap_handler\")]\n"
               "fn trap() {\n"
               "    loop {}\n"
               "}\n"
               "\n"
               "#[panic_handler]\n"
               "fn panic(_info: &PanicInfo) -> ! {\n"
               "    loop {}\n"
               "}\n";

    GeneratedProjectFirmwareRust result;
    result.main_rs = main_rs.str();
    return ParseResult<GeneratedProjectFirmwareRust>{true, "", result};
}

namespace
{
    // Независимая копия uniqueTempDirName (cmake_project_codegen.cpp) — та
    // же причина, что и copyDirectoryTreeSkipExisting в contribution_utils:
    // маленькая утилита, растиражированная по каждому writeGenerated*Project
    // независимо, а не вынесенная в общий заголовок ради одной функции на
    // три вызывающих места.
    std::string uniqueTempDirName(const std::string& output_project_dir)
    {
        static std::atomic<unsigned> counter{0};
        const auto now = std::chrono::steady_clock::now().time_since_epoch();
        const long long ticks = std::chrono::duration_cast<std::chrono::microseconds>(now).count();
        std::ostringstream out;
        out << output_project_dir << ".tmp-generating-" << ticks << "-" << counter.fetch_add(1);
        return out.str();
    }

    // Вставляет lines сразу после строки, содержащей marker — тот же приём,
    // что insertAfterMarkerLine (cmake_project_codegen.cpp), но с отступом
    // в четыре пробела (стиль Cargo.toml/TOML, а не восемь как в CMakeLists.txt).
    bool insertAfterMarkerLine(std::string& text, const std::string& marker, const std::vector<std::string>& lines)
    {
        const size_t marker_pos = text.find(marker);
        if (marker_pos == std::string::npos)
        {
            return false;
        }
        size_t line_end = text.find('\n', marker_pos);
        const size_t insert_pos = (line_end == std::string::npos) ? text.size() : line_end + 1;

        std::ostringstream block;
        for (const std::string& line : lines)
        {
            block << line << "\n";
        }
        text.insert(insert_pos, block.str());
        return true;
    }

    // Перенос дерева tmp_root поверх уже существующего to_root, файл за
    // файлом — независимая копия mergeMoveInto (cmake_project_codegen.cpp),
    // то же обоснование (см. .h там: "не полностью атомарно... но риск
    // минимален на практике").
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

    // Три строки Cargo.toml [dependencies] под маркер
    // "# MIK32_CONFIGURATOR: DEPENDENCIES" — по HalSourceMode
    // (rust_hal_source_mode), см. templates/mik32_rust_hal_snapshot/VERSION.md,
    // раздел «Назначение»: mik32v2-hal копируется из снэпшота ВСЕГДА (нет
    // публичного репозитория), pac/rt зависят от режима.
    //
    // mik32-rt = { ..., features = ["memory-spifi"] } обязателен во всех
    // режимах: vendor/rt/build.rs без этого фича-флага подставляет
    // memory_default.x (FLASH_TEXT = 0x01000000, 8K — тестовая заглушка
    // апстрима), а не memory_spifi.x (FLASH_TEXT = 0x80000000 — реальная
    // область загрузки МИК32, та же, куда прошивает elbear_uploader.py и
    // куда линкуется CMake/PlatformIO-прошивка). Без фичи линковка проходит
    // чисто, но МК не стартует — entry point уезжает в недостижимый
    // 0x01000000 (найдено реальной прошивкой на плате 2026-10-20: UART
    // молчит при чистой сборке `cargo build --release`).
    std::vector<std::string> buildDependencyLines(const ProjectConfig& config)
    {
        std::vector<std::string> lines;
        switch (config.rust_hal_source_mode)
        {
            case HalSourceMode::Network:
                // Версия/URL — ровно то, что использует сам апстрим
                // (mik32v2-pac-main/Cargo.toml: repository на crates.io;
                // mik32-hal-main/Cargo.toml: mik32-rt через этот же git URL).
                lines.push_back("mik32v2-pac = \"0.0.1\"");
                lines.push_back("mik32-rt = { git = \"https://github.com/mik32-rs/mik32-rt.git\", features = [\"memory-spifi\"] }");
                lines.push_back("mik32v2-hal = { path = \"vendor/hal\" }");
                break;
            case HalSourceMode::LocalPath:
                lines.push_back("mik32v2-pac = { path = \"" + config.rust_hal_source_local_path + "/pac\" }");
                lines.push_back("mik32-rt = { path = \"" + config.rust_hal_source_local_path + "/rt\", features = [\"memory-spifi\"] }");
                lines.push_back("mik32v2-hal = { path = \"" + config.rust_hal_source_local_path + "/hal\" }");
                break;
            case HalSourceMode::Bundled:
            default:
                lines.push_back("mik32v2-pac = { path = \"vendor/pac\" }");
                lines.push_back("mik32-rt = { path = \"vendor/rt\", features = [\"memory-spifi\"] }");
                lines.push_back("mik32v2-hal = { path = \"vendor/hal\" }");
                break;
        }
        return lines;
    }
}

CodegenWriteResult writeGeneratedRustProject(
    const GeneratedProjectFirmwareRust& firmware,
    const std::string& cargo_skeleton_dir,
    const std::string& rust_hal_snapshot_dir,
    const std::string& output_project_dir,
    const ProjectConfig& config,
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

    // --- 1. Скелет Cargo-проекта --------------------------------------------
    if (!copyDirectoryTreeSkipExisting(cargo_skeleton_dir, tmp_dir.u8string(), fs_error))
    {
        return fail("Не удалось скопировать шаблон " + cargo_skeleton_dir + ": " + fs_error.message());
    }

    // --- 2. mik32v2-hal — копируется из снэпшота ВСЕГДА, независимо от
    // rust_hal_source_mode (см. buildDependencyLines) -----------------------
    if (config.rust_hal_source_mode == HalSourceMode::Bundled
        || config.rust_hal_source_mode == HalSourceMode::Network)
    {
        const fs::path hal_src = utf8Path(rust_hal_snapshot_dir) / "hal";
        std::error_code exists_error;
        if (!fs::exists(hal_src, exists_error))
        {
            return fail("Снэпшот Rust HAL не найден (нет hal/): " + rust_hal_snapshot_dir);
        }
        if (!copyDirectoryTreeSkipExisting(hal_src.u8string(), (tmp_dir / "vendor" / "hal").u8string(), fs_error))
        {
            return fail("Не удалось скопировать mik32v2-hal из " + rust_hal_snapshot_dir + ": " + fs_error.message());
        }
    }

    if (config.rust_hal_source_mode == HalSourceMode::Bundled)
    {
        const fs::path pac_src = utf8Path(rust_hal_snapshot_dir) / "pac";
        const fs::path rt_src = utf8Path(rust_hal_snapshot_dir) / "rt";
        std::error_code exists_error;
        if (!fs::exists(pac_src, exists_error) || !fs::exists(rt_src, exists_error))
        {
            return fail("Снэпшот Rust HAL не содержит pac/ и rt/: " + rust_hal_snapshot_dir);
        }
        if (!copyDirectoryTreeSkipExisting(pac_src.u8string(), (tmp_dir / "vendor" / "pac").u8string(), fs_error)
            || !copyDirectoryTreeSkipExisting(rt_src.u8string(), (tmp_dir / "vendor" / "rt").u8string(), fs_error))
        {
            return fail("Не удалось скопировать pac/rt из " + rust_hal_snapshot_dir + ": " + fs_error.message());
        }
    }
    else if (config.rust_hal_source_mode == HalSourceMode::LocalPath)
    {
        if (config.rust_hal_source_local_path.empty())
        {
            return fail("rust_hal_source_mode=local_path, но rust_hal_source_local_path не задан");
        }
        const fs::path local_root = utf8Path(config.rust_hal_source_local_path);
        std::error_code exists_error;
        if (!fs::exists(local_root / "hal", exists_error) || !fs::exists(local_root / "pac", exists_error)
            || !fs::exists(local_root / "rt", exists_error))
        {
            return fail("Каталог rust_hal_source_local_path (" + config.rust_hal_source_local_path
                + ") не содержит подкаталоги hal/, pac/, rt/");
        }
        // При local_path сам HAL/PAC/RT не копируется вообще — Cargo.toml
        // ссылается напрямую на config.rust_hal_source_local_path (см.
        // buildDependencyLines), путь остаётся вне output_project_dir.
    }
    // HalSourceMode::Network для pac/rt ничего не копирует — Cargo.toml
    // ссылается на них напрямую (crates.io/git), сеть трогает сам cargo
    // при первой сборке, не этот код (тот же принцип, что git clone в
    // cmake_project_codegen.cpp, только сеть здесь дергает не конфигуратор,
    // а Cargo).

    // --- 3. Подстановка зависимостей в Cargo.toml ---------------------------
    const fs::path cargo_toml_path = tmp_dir / "Cargo.toml";
    const FileReadOutcome cargo_toml_read = readUtf8PathFile(cargo_toml_path.u8string());
    if (!cargo_toml_read.success)
    {
        return fail("Не удалось прочитать " + cargo_toml_path.u8string() + " из шаблона: " + cargo_toml_read.diagnostic);
    }
    std::string cargo_toml_content = cargo_toml_read.content;
    if (!insertAfterMarkerLine(cargo_toml_content, "# MIK32_CONFIGURATOR: DEPENDENCIES", buildDependencyLines(config)))
    {
        return fail("Маркер \"# MIK32_CONFIGURATOR: DEPENDENCIES\" не найден в шаблоне " + cargo_toml_path.u8string());
    }
    const FileWriteOutcome cargo_toml_write = writeUtf8PathFile(cargo_toml_path.u8string(), cargo_toml_content);
    if (!cargo_toml_write.success)
    {
        return fail("Не удалось записать " + cargo_toml_path.u8string() + ": " + cargo_toml_write.diagnostic);
    }

    // --- 4. src/main.rs (USER CODE слияние с уже существующим на диске) ---
    const fs::path tmp_src = tmp_dir / "src";
    fs::create_directories(tmp_src, fs_error);
    if (fs_error)
    {
        return fail("Не удалось создать " + tmp_src.u8string() + ": " + fs_error.message());
    }

    std::string main_rs_to_write = firmware.main_rs;
    const fs::path existing_main_rs_path = utf8Path(output_project_dir) / "src" / "main.rs";
    if (fs::exists(existing_main_rs_path))
    {
        const FileReadOutcome existing = readUtf8PathFile(existing_main_rs_path.u8string());
        if (existing.success)
        {
            main_rs_to_write = mergeUserCodeSectionsRust(firmware.main_rs, existing.content);
        }
    }
    const FileWriteOutcome main_rs_write = writeUtf8PathFile((tmp_src / "main.rs").u8string(), main_rs_to_write);
    if (!main_rs_write.success)
    {
        return fail("Не удалось записать src/main.rs: " + main_rs_write.diagnostic);
    }

    // --- 5. project.mik32 ---------------------------------------------------
    const FileWriteOutcome project_write =
        writeUtf8PathFile((tmp_dir / "project.mik32").u8string(), project_json_copy.dump(4) + "\n");
    if (!project_write.success)
    {
        return fail("Не удалось записать project.mik32: " + project_write.diagnostic);
    }

    // --- 6. Перенос из временного каталога в output_project_dir ------------
    // Cargo.toml и .cargo/config.toml защищены — пользователь мог
    // отредактировать их руками после первой генерации (тот же принцип,
    // что CMakeLists.txt в cmake_project_codegen.cpp).
    static const std::vector<std::string> kProtectedRelativePaths = {
        "Cargo.toml", ".cargo/config.toml"};

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
