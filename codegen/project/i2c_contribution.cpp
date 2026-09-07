#include "i2c_contribution.h"
#include "contribution_utils.h"
#include <sstream>

namespace
{
    // Таймаут подбирается под сценарий — так делают все вендорные примеры, и
    // это НЕ косметика. HAL_I2C_Master_WaitRXNE (mik32_hal_i2c.c:324-354,
    // реально установленный SDK) проверяет только BERR/ARLO/RXNE и НЕ
    // проверяет флаг NACKF: на адресе, где устройства нет, она докручивает
    // счётчик Timeout до нуля и лишь потом возвращает HAL_TIMEOUT. Значит для
    // сканера цена одного «пустого» адреса равна всему таймауту, а проход —
    // 127 таких цен.
    //
    //  - сканер: 10000 — литерал из docs/examples/mik32-examples/I2C_Scan/
    //    src/main.c:40. С I2C_TIMEOUT_DEFAULT (=1000000, mik32_hal_i2c.h:17)
    //    проход стоил бы в 100 раз дороже — десятки секунд вместо долей.
    //  - master точка-точка: I2C_TIMEOUT_DEFAULT — docs/examples/
    //    mik32-examples/Hal_I2C_Master/src/main.c:43,59.
    //  - slave: 5*I2C_TIMEOUT_DEFAULT — docs/examples/mik32-examples/
    //    Hal_I2C_Slave/src/main.c:41,64 (ведомый ждёт ведущего, запас нужен
    //    наоборот больше).
    constexpr const char* kScannerTimeout = "10000";
    constexpr const char* kMasterTimeout = "I2C_TIMEOUT_DEFAULT";
    constexpr const char* kSlaveTimeout = "5*I2C_TIMEOUT_DEFAULT";

    struct I2cTimingr
    {
        long presc;
        long scldel;
        long sdadel;
        long sclh;
        long scll;
    };

    // Считает PRESC/SCLDEL/SDADEL/SCLH/SCLL на этапе кодогенерации, а не в
    // рантайме — по формуле HAL_I2C_calcFreqCoef (docs/examples/mik32-hal/
    // peripherals/Source/mik32_hal_i2c.c:199-217), которая физически
    // отсутствует в реально установленном framework-mik32v2-sdk@0.2.2
    // (проверено компиляцией на 1T Rex Board — "I2C_InitTypeDef has no member
    // named frequency"). См. knowledge_base/i2c.json, call_shape.
    //
    // ВАЖНО: эта формула (в т.ч. SCLDEL=SDADEL=0 — так делает и сама
    // HAL_I2C_calcFreqCoef на строках 211-212) на 1T Rex Board не даёт
    // рабочий I2C master. Два независимых примера, работающих на реальном
    // железе, сходятся на одних значениях для 100 кГц при HSI 32 МГц:
    //   docs/examples/mik32-examples/I2C_Scan/src/main.c:82-86 и
    //   docs/examples/MPU6050-MIK32-main/src/main.c:356-360
    // (PRESC=1, SCLDEL=15, SDADEL=15, SCLH=75, SCLL=75) — используем их как
    // подтверждённый пресет вместо расчёта. Для 400 кГц/1 МГц подтверждённых
    // железом значений нет, там оставлена расчётная формула без
    // HW-подтверждения (см. hardware validation gate в CLAUDE.md).
    //
    I2cTimingr calcI2cTimingr(long sys_clk_hz, int clock_speed, int duty_factor)
    {
        if (clock_speed == 100000)
        {
            return I2cTimingr{1, 15, 15, 75, 75};
        }

        constexpr long kTsyncSigmaFactor = 10;
        long i2c_cycles = sys_clk_hz / clock_speed - kTsyncSigmaFactor;
        const long presc_del = i2c_cycles / 256;
        i2c_cycles = i2c_cycles / (presc_del + 1);

        I2cTimingr timing;
        timing.presc = presc_del;
        timing.scldel = 0;
        timing.sdadel = 0;
        timing.sclh = i2c_cycles / duty_factor - 1;
        timing.scll = i2c_cycles - i2c_cycles / duty_factor - 1;
        return timing;
    }
}

ParseResult<PeripheralContribution> buildI2cContribution(
    const I2cConfig& config,
    const I2cKnowledge& knowledge,
    bool console_available,
    bool emit_demo_loop)
{
    const std::string* i2c_handle = findOrNull(knowledge.instance_handle, config.instance);
    if (!i2c_handle)
    {
        return ParseResult<PeripheralContribution>{
            false, "I2cConfig ссылается на instance, отсутствующий в knowledge_base/i2c.json", std::nullopt};
    }

    const bool is_master = (config.mode == "master");
    const bool is_scanner = (config.demo == "scanner");
    const bool is_ping_pong = (config.demo == "ping_pong");

    // Решение пользователя (сессия /grill-me): сканер без UART не может
    // напечатать результат — блокируем кодоген, а не генерируем немой сканер.
    if (emit_demo_loop && is_master && is_scanner && !console_available)
    {
        return ParseResult<PeripheralContribution>{
            false, "demo=\"scanner\" для mode=\"master\" требует блок \"uart\" в том же конфиге "
                   "(сканер печатает найденные адреса по UART)", std::nullopt};
    }

    const std::string handle_name = "hi2c_" + std::to_string(config.instance);
    const std::string suffix = std::to_string(config.instance);

    PeripheralContribution contribution;
    contribution.label = "I2C_" + suffix;
    contribution.init_order = kInitOrderI2c;
    contribution.init_function_name = "I2C_" + suffix + "_Init";
    contribution.init_c_includes = {"mik32_hal_i2c.h", "mik32_hal_pcc.h"};
    contribution.global_definitions = "I2C_HandleTypeDef " + handle_name + ";\n";
    contribution.public_prototypes = {
        "void " + contribution.init_function_name + "(void);",
        "void I2C_" + suffix + "_Reset(void);"};

    std::ostringstream init_c;
    if (!is_master)
    {
        // Errata MIK32V2 v1.0.1, §4.1 «Флаг TXIS» — актуальна только для
        // slave. NoStretchMode=DISABLE ниже захардкожен (растягивание
        // активно, errata не проявляется), комментарий оставлен на случай,
        // если NoStretchMode станет пользовательским полем.
        init_c << "/*\n"
                  " * Errata п. 4.1 (mik32v2errataV1_0_1_1 v1.0.1): флаг TXIS формируется в конце\n"
                  " * передачи байта, а не в начале. Если байт был предварительно загружен, первая\n"
                  " * установка TXIS происходит после отправки первого байта; в режиме без\n"
                  " * растягивания (NOSTRETCH=1), если за это время не были загружены новые данные,\n"
                  " * возникает недозагрузка. Обход: не использовать NOSTRETCH=1 без прерываний по\n"
                  " * TXIS с укороченным обработчиком, либо DMA. Этот кодоген хардкодит\n"
                  " * NoStretchMode=DISABLE (растягивание активно), errata здесь не проявляется.\n"
                  " */\n";
    }

    init_c << "/*\n"
              " * " << *i2c_handle << ": mode=" << config.mode
           << ", clock_speed=" << config.clock_speed << ", demo=" << config.demo << ".\n"
              " */\n"
              "void " << contribution.init_function_name << "(void)\n"
              "{\n"
              "    " << handle_name << ".Instance = " << *i2c_handle << ";\n"
              "\n";

    // master-поля (AutoEnd) и slave-поля (OwnAddress1/2, DualAddressMode,
    // GeneralCall, OwnAddress2Mask, NoStretchMode, SBCMode) не пересекаются —
    // читаются только в своей ветке HAL_I2C_Init (mik32_hal_i2c.c:178-197),
    // поэтому генерируются только для своего mode.
    if (is_master)
    {
        init_c << "    " << handle_name << ".Init.Mode = HAL_I2C_MODE_MASTER;\n"
                  "    " << handle_name << ".Init.DigitalFilter = I2C_DIGITALFILTER_OFF;\n"
                  "    " << handle_name << ".Init.AnalogFilter = I2C_ANALOGFILTER_DISABLE;\n"
                  "    " << handle_name << ".Init.AutoEnd = I2C_AUTOEND_ENABLE;\n";
    }
    else
    {
        init_c << "    " << handle_name << ".Init.Mode = HAL_I2C_MODE_SLAVE;\n"
                  "    " << handle_name << ".Init.DigitalFilter = I2C_DIGITALFILTER_OFF;\n"
                  "    " << handle_name << ".Init.AnalogFilter = I2C_ANALOGFILTER_DISABLE;\n"
                  "    " << handle_name << ".Init.OwnAddress1 = " << config.own_address << ";\n"
                  "    " << handle_name << ".Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;\n"
                  "    " << handle_name << ".Init.OwnAddress2 = 0;\n"
                  "    " << handle_name << ".Init.OwnAddress2Mask = I2C_OWNADDRESS2_MASK_DISABLE;\n"
                  "    " << handle_name << ".Init.GeneralCall = I2C_GENERALCALL_DISABLE;\n"
                  "    " << handle_name << ".Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;\n"
                  "    " << handle_name << ".Init.SBCMode = I2C_SBC_DISABLE;\n";
    }

    // HAL_I2C_Init НЕ считает TIMINGR сама (в отличие от GPIO/UART/Timer16,
    // где один вызов покрывает всё) — HAL_I2C_SetClockSpeed
    // (mik32_hal_i2c.c:64-87) просто копирует уже заполненные Clock.* в
    // регистр, а HAL_I2C_calcFreqCoef в установленном SDK отсутствует.
    const I2cTimingr timing = calcI2cTimingr(
        knowledge.sys_clk_hz, config.clock_speed, knowledge.duty_factor_default);
    init_c << "\n"
              "    " << handle_name << ".Clock.PRESC = " << timing.presc << ";\n"
              "    " << handle_name << ".Clock.SCLDEL = " << timing.scldel << ";\n"
              "    " << handle_name << ".Clock.SDADEL = " << timing.sdadel << ";\n"
              "    " << handle_name << ".Clock.SCLH = " << timing.sclh << ";\n"
              "    " << handle_name << ".Clock.SCLL = " << timing.scll << ";\n"
              "\n"
              "    HAL_I2C_Init(&" << handle_name << ");\n"
              "}\n";
    contribution.init_function_text = init_c.str();

    // Авторесет — тонкая обёртка над реальным HAL_I2C_Reset
    // (mik32_hal_i2c.c:32-38: сброс ErrorCode, снятие и повторная установка
    // PE в CR1). Вызывается пользовательским кодом при ошибке шины.
    std::ostringstream helpers;
    helpers << "/*\n"
               " * Авторесет " << *i2c_handle << " — вызывается пользовательским кодом\n"
               " * при ошибке шины (NACK/BERR/ARLO/OVR).\n"
               " */\n"
               "void I2C_" << suffix << "_Reset(void)\n"
               "{\n"
               "    HAL_I2C_Reset(&" << handle_name << ");\n"
               "}\n";
    contribution.helper_functions_text = helpers.str();

    if (!emit_demo_loop)
    {
        return ParseResult<PeripheralContribution>{true, "", contribution};
    }

    // Тело цикла: сценарий задан полем "demo" (schemas/i2c.json), см.
    // docs/implementations/i2c.md. Базовый отступ — 12 пробелов: сборщик
    // оборачивает каждый вклад в собственную фигурную скобку внутри while(1)
    // (см. codegen/project/peripheral_contribution.h).
    std::ostringstream loop;
    if (is_master && is_scanner)
    {
        loop << "            xprintf(\"" << contribution.label << ": searching for I2C devices...\\r\\n\");\n"
                "            for (uint8_t addr = 0; addr < 0x80; addr++)\n"
                "            {\n"
                "                uint8_t dummy;\n"
                "                if (HAL_I2C_Master_Receive(&" << handle_name << ", addr, &dummy, 1, "
             << kScannerTimeout << ") == HAL_OK)\n"
                "                {\n"
                "                    xprintf(\"" << contribution.label << ": device found at 0x%02X\\r\\n\", addr);\n"
                "                }\n"
                "            }\n"
                "            HAL_DelayMs(2000);\n";
    }
    else if (is_master && is_ping_pong)
    {
        loop << "            static uint8_t ping_pong_done = 0;\n"
                "            if (!ping_pong_done)\n"
                "            {\n"
                "                uint8_t sent;\n"
                "                uint8_t received;\n"
                "                for (sent = 1; sent <= 9; sent += 2)\n"
                "                {\n"
                "                    HAL_I2C_Master_Transmit(&" << handle_name << ", " << config.target_address
             << ", &sent, 1, " << kMasterTimeout << ");\n";
        if (console_available)
        {
            loop << "                    xprintf(\"" << contribution.label << ": master sent %u\\r\\n\", (unsigned)sent);\n";
        }
        loop << "                    HAL_DelayMs(1000);\n"
                "\n"
                "                    HAL_I2C_Master_Receive(&" << handle_name << ", " << config.target_address
             << ", &received, 1, " << kMasterTimeout << ");\n";
        if (console_available)
        {
            loop << "                    xprintf(\"" << contribution.label << ": master received %u\\r\\n\", (unsigned)received);\n";
        }
        loop << "                    HAL_DelayMs(1000);\n"
                "                }\n"
                "                ping_pong_done = 1;\n"
                "            }\n";
    }
    else if (!is_master && is_ping_pong)
    {
        loop << "            static uint8_t ping_pong_done = 0;\n"
                "            if (!ping_pong_done)\n"
                "            {\n"
                "                uint8_t received;\n"
                "                uint8_t to_send;\n"
                "                for (received = 1; received <= 9; received += 2)\n"
                "                {\n"
                "                    HAL_I2C_Slave_Receive(&" << handle_name << ", &received, 1, " << kSlaveTimeout << ");\n";
        if (console_available)
        {
            loop << "                    xprintf(\"" << contribution.label << ": slave received %u\\r\\n\", (unsigned)received);\n";
        }
        loop << "                    to_send = received + 1;\n"
                "                    HAL_I2C_Slave_Transmit(&" << handle_name << ", &to_send, 1, " << kSlaveTimeout << ");\n";
        if (console_available)
        {
            loop << "                    xprintf(\"" << contribution.label << ": slave sent %u\\r\\n\", (unsigned)to_send);\n";
        }
        loop << "                }\n"
                "                ping_pong_done = 1;\n"
                "            }\n";
    }
    else if (!is_master && config.demo == "scanner_target")
    {
        // HAL_I2C_Slave_Receive (mik32_hal_i2c.c:846-877) ждёт RXNE — то есть
        // ЗАПИСЬ от master — и не отвечает на HAL_I2C_Master_Receive сканера
        // (тот ждёт TXIS/данные). Поэтому у цели для сканера свой цикл
        // HAL_I2C_Slave_Transmit с фиксированным байтом, чтобы ACK'ать именно
        // чтение (см. docs/implementations/i2c.md).
        loop << "            uint8_t probe_response = 0x42;\n"
                "            HAL_I2C_Slave_Transmit(&" << handle_name << ", &probe_response, 1, " << kSlaveTimeout << ");\n";
    }
    else  // !is_master && demo == "receive"
    {
        loop << "            uint8_t received;\n"
                "            HAL_I2C_Slave_Receive(&" << handle_name << ", &received, 1, " << kSlaveTimeout << ");\n";
    }

    contribution.loop_body = loop.str();
    contribution.main_c_includes = {"mik32_hal_i2c.h"};
    // xprintf.h нужен main.c, только если в теле цикла реально есть печать:
    // у slave+receive и slave+scanner_target её нет.
    if (console_available && (is_scanner || is_ping_pong))
    {
        contribution.main_c_includes.push_back("xprintf.h");
    }
    contribution.extern_declarations = "extern I2C_HandleTypeDef " + handle_name + ";\n";

    return ParseResult<PeripheralContribution>{true, "", contribution};
}
