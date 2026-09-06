#include "uart_codegen.h"
#include "user_code_markers.h"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <iomanip>

namespace fs = std::filesystem;

namespace
{
    template <typename ValueT>
    const ValueT* findOrNull(const std::map<std::string, ValueT>& map, const std::string& key)
    {
        auto it = map.find(key);
        return it == map.end() ? nullptr : &it->second;
    }

    template <typename ValueT>
    const ValueT* findOrNull(const std::map<int, ValueT>& map, int key)
    {
        auto it = map.find(key);
        return it == map.end() ? nullptr : &it->second;
    }
}

ParseResult<GeneratedUartFirmware> generateUartFirmware(
    const UartConfig& config,
    const UartKnowledge& knowledge)
{
    const std::string* uart_handle = findOrNull(knowledge.instance_handle, config.instance);
    const std::string* frame_enum = findOrNull(knowledge.data_bits_hal_enum, std::to_string(config.data_bits));
    const std::string* stop_bit_enum = findOrNull(knowledge.stop_bits_hal_enum, std::to_string(config.stop_bits));
    const UartKnowledge::ParitySetting* parity_setting = findOrNull(knowledge.parity_hal_setting, config.parity);

    if (!uart_handle || !frame_enum || !stop_bit_enum || !parity_setting)
    {
        return ParseResult<GeneratedUartFirmware>{
            false,
            "UartConfig ссылается на instance/data_bits/stop_bits/parity, отсутствующий в knowledge_base/uart.json",
            std::nullopt};
    }

    GeneratedUartFirmware firmware;

    firmware.mik32_init_h =
        "#ifndef MIK32_INIT_H\n"
        "#define MIK32_INIT_H\n"
        "\n"
        "void UART_Init(void);\n"
        "\n"
        "#endif //MIK32_INIT_H\n";

    // Концепция сгенерированного тела: один USART_HandleTypeDef и один
    // вызов HAL_USART_Init — обоснование в knowledge_base/uart.json,
    // call_shape.single_call_covers_clock_and_pin_mux. Поля вне охвата
    // JSON-схемы (прерывания, DMA, инверсии, swap, break, полудуплекс,
    // синхронный режим) зафиксированы в безопасные дефолты по образцу
    // docs/examples/mik32-examples/HAL_USART/src/main.c.
    const std::string flow = config.flow_control ? "Enable" : "Disable";

    // ТЗ §3.1.3.2: направление настраивается пользователем, а не жёстко
    // Enable/Enable — mik32_hal_usart.h, USART_HandleTypeDef.transmitting/
    // receiving управляют этим по отдельности.
    const std::string transmitting = (config.direction == "tx" || config.direction == "duplex") ? "Enable" : "Disable";
    const std::string receiving = (config.direction == "rx" || config.direction == "duplex") ? "Enable" : "Disable";

    // ТЗ §3.1.3.2: отображать фактическую погрешность Baud Rate при
    // HSI=32МГц. HAL вычисляет DIVIDER целочисленным делением
    // (mik32_hal_usart.c:131-133) — здесь тот же расчёт на этапе
    // кодогенерации (частота фиксирована и известна заранее, §3.1.8),
    // результат кладётся в комментарий, т.к. GUI с графическим
    // отображением погрешности ещё не реализован.
    const long divider = knowledge.hsi_hz / config.baud_rate;
    const double actual_baud_rate = static_cast<double>(knowledge.hsi_hz) / static_cast<double>(divider);
    const double baud_error_percent = (actual_baud_rate - config.baud_rate) / config.baud_rate * 100.0;

    std::ostringstream init_c;
    init_c << std::fixed << std::setprecision(3);
    init_c << "#include \"mik32_init.h\"\n"
              "#include \"mik32_hal_usart.h\"\n"
              "\n"
              "USART_HandleTypeDef husart;\n"
              "\n"
              "/*\n"
              " * Сгенерировано MIK32-Configurator.\n"
              " * " << *uart_handle << ": baud_rate=" << config.baud_rate
           << ", data_bits=" << config.data_bits << ", stop_bits=" << config.stop_bits
           << ", parity=" << config.parity << ", flow_control=" << (config.flow_control ? "true" : "false")
           << ", direction=" << config.direction << ".\n"
              " * Фактическая скорость при HSI=" << knowledge.hsi_hz << "Гц: DIVIDER=" << divider
           << " -> " << actual_baud_rate << " бод, погрешность " << baud_error_percent << "%\n"
              "   (ТЗ §3.1.3.2 / §3.1.8; см. knowledge_base/uart.json, hsi_hz).\n"
              " * Только Asynchronous_Mode — синхронный режим вне скоупа этой сессии\n"
              " * (Errata MIK32V2 v1.0.1, §10.1, см. knowledge_base/uart.json).\n"
              " */\n"
              "void UART_Init(void)\n"
              "{\n"
              "    husart.Instance = " << *uart_handle << ";\n"
              "    husart.transmitting = " << transmitting << ";\n"
              "    husart.receiving = " << receiving << ";\n"
              "    husart.frame = " << *frame_enum << ";\n"
              "    husart.parity_bit = " << parity_setting->parity_bit << ";\n"
              "    husart.parity_bit_inversion = " << parity_setting->parity_bit_inversion << ";\n"
              "    husart.bit_direction = LSB_First;\n"
              "    husart.data_inversion = Disable;\n"
              "    husart.tx_inversion = Disable;\n"
              "    husart.rx_inversion = Disable;\n"
              "    husart.swap = Disable;\n"
              "    husart.lbm = Disable;\n"
              "    husart.stop_bit = " << *stop_bit_enum << ";\n"
              "    husart.mode = Asynchronous_Mode;\n"
              "    husart.xck_mode = XCK_Mode3;\n"
              "    husart.last_byte_clock = Disable;\n"
              "    husart.overwrite = Disable;\n"
              "    husart.rts_mode = AlwaysEnable_mode;\n"
              "    husart.dma_tx_request = Disable;\n"
              "    husart.dma_rx_request = Disable;\n"
              "    husart.channel_mode = Duplex_Mode;\n"
              "    husart.tx_break_mode = Disable;\n"
              "    husart.Interrupt.ctsie = Disable;\n"
              "    husart.Interrupt.eie = Disable;\n"
              "    husart.Interrupt.idleie = Disable;\n"
              "    husart.Interrupt.lbdie = Disable;\n"
              "    husart.Interrupt.peie = Disable;\n"
              "    husart.Interrupt.rxneie = Disable;\n"
              "    husart.Interrupt.tcie = Disable;\n"
              "    husart.Interrupt.txeie = Disable;\n"
              "    husart.Modem.rts = " << flow << ";\n"
              "    husart.Modem.cts = " << flow << ";\n"
              "    husart.Modem.dtr = Disable;\n"
              "    husart.Modem.dcd = Disable;\n"
              "    husart.Modem.dsr = Disable;\n"
              "    husart.Modem.ri = Disable;\n"
              "    husart.Modem.ddis = Disable;\n"
              "    husart.baudrate = " << config.baud_rate << ";\n"
              "\n"
              "    HAL_USART_Init(&husart);\n"
              "}\n";
    firmware.mik32_init_c = init_c.str();

    // main.c зашивает SystemClock_Config дефолтным блоком (та же причина,
    // что и в GPIO-модуле — PCC ещё не отдельный модуль конфигуратора), и,
    // в отличие от пустого GPIO while(1){}, периодически вызывает
    // HAL_USART_Print — иначе на линии TX никогда не появится сигнал,
    // который можно было бы увидеть логическим анализатором или в терминале.
    // Маркеры /* USER CODE BEGIN <ID> */.../* USER CODE END <ID> */ — §3.1.7.3
    // ТЗ. Тело цикла (HAL_USART_Print + задержка) — дефолт генератора внутри
    // маркера "Loop", а не жёстко зашитый код: пользователь может заменить
    // его своей логикой, и она переживёт повторную генерацию (см.
    // codegen/user_code_markers.h, применяется в writeGeneratedUartProject).
    firmware.main_c =
        "#include \"mik32_hal_pcc.h\"\n"
        "#include \"mik32_hal_usart.h\"\n"
        "#include \"mik32_init.h\"\n"
        "\n"
        "/* USER CODE BEGIN Includes */\n"
        "/* USER CODE END Includes */\n"
        "\n"
        "extern USART_HandleTypeDef husart;\n"
        "\n"
        "void SystemClock_Config(void);\n"
        "\n"
        "int main()\n"
        "{\n"
        "    SystemClock_Config();\n"
        "\n"
        "    UART_Init();\n"
        "\n"
        "    /* USER CODE BEGIN Init */\n"
        "    /* USER CODE END Init */\n"
        "\n"
        "    while (1)\n"
        "    {\n"
        "        /* USER CODE BEGIN Loop */\n"
        "        HAL_USART_Print(&husart, \"MIK32-Configurator UART test\\n\", USART_TIMEOUT_DEFAULT);\n"
        "        HAL_DelayMs(1000);\n"
        "        /* USER CODE END Loop */\n"
        "    }\n"
        "}\n"
        "\n"
        "void SystemClock_Config(void)\n"
        "{\n"
        "    PCC_InitTypeDef PCC_OscInit = {0};\n"
        "\n"
        "    PCC_OscInit.OscillatorEnable = PCC_OSCILLATORTYPE_ALL;\n"
        "    PCC_OscInit.FreqMon.OscillatorSystem = PCC_OSCILLATORTYPE_OSC32M;\n"
        "    PCC_OscInit.FreqMon.ForceOscSys = PCC_FORCE_OSC_SYS_UNFIXED;\n"
        "    PCC_OscInit.FreqMon.Force32KClk = PCC_FREQ_MONITOR_SOURCE_OSC32K;\n"
        "    PCC_OscInit.AHBDivider = 0;\n"
        "    PCC_OscInit.APBMDivider = 0;\n"
        "    PCC_OscInit.APBPDivider = 0;\n"
        "    PCC_OscInit.HSI32MCalibrationValue = 128;\n"
        "    PCC_OscInit.LSI32KCalibrationValue = 8;\n"
        "    PCC_OscInit.RTCClockSelection = PCC_RTC_CLOCK_SOURCE_AUTO;\n"
        "    PCC_OscInit.RTCClockCPUSelection = PCC_CPU_RTC_CLOCK_SOURCE_OSC32K;\n"
        "    HAL_PCC_Config(&PCC_OscInit);\n"
        "}\n";

    return ParseResult<GeneratedUartFirmware>{true, "", firmware};
}

CodegenWriteResult writeGeneratedUartProject(
    const GeneratedUartFirmware& firmware,
    const std::string& skeleton_dir,
    const std::string& output_project_dir)
{
    std::error_code fs_error;

    if (!fs::exists(fs::path(output_project_dir) / "platformio.ini"))
    {
        fs::create_directories(output_project_dir, fs_error);
        if (fs_error)
        {
            return CodegenWriteResult{false, "Не удалось создать " + output_project_dir + ": " + fs_error.message()};
        }

        fs::copy(skeleton_dir, output_project_dir,
                  fs::copy_options::recursive | fs::copy_options::skip_existing,
                  fs_error);
        if (fs_error)
        {
            return CodegenWriteResult{false, "Не удалось скопировать templates/pio_skeleton: " + fs_error.message()};
        }
    }

    const fs::path src_dir = fs::path(output_project_dir) / "src";
    fs::create_directories(src_dir, fs_error);
    if (fs_error)
    {
        return CodegenWriteResult{false, "Не удалось создать каталог src/: " + fs_error.message()};
    }

    // main.c проходит через mergeUserCodeSections (§3.1.7.3 ТЗ) — см.
    // идентичную логику и обоснование в codegen/gpio/gpio_codegen.cpp.
    std::string main_c_to_write = firmware.main_c;
    const fs::path main_c_path = src_dir / "main.c";
    if (fs::exists(main_c_path))
    {
        std::ifstream existing(main_c_path);
        std::ostringstream existing_buffer;
        existing_buffer << existing.rdbuf();
        main_c_to_write = mergeUserCodeSections(firmware.main_c, existing_buffer.str());
    }

    const std::pair<const char*, const std::string*> files_to_write[] = {
        {"mik32_init.h", &firmware.mik32_init_h},
        {"mik32_init.c", &firmware.mik32_init_c},
        {"main.c", &main_c_to_write},
    };

    for (const auto& [file_name, content] : files_to_write)
    {
        std::ofstream out(src_dir / file_name, std::ios::trunc);
        if (!out.is_open())
        {
            return CodegenWriteResult{false, std::string("Не удалось записать файл src/") + file_name};
        }
        out << *content;
    }

    return CodegenWriteResult{true, ""};
}
