#include "i2c_contribution.h"
#include "contribution_utils.h"
#include <map>
#include <sstream>
#include <vector>
#include <utility>

namespace
{
    // mik32_hal_i2c.h, HAL_I2C_OwnAddress2MaskTypeDef — имя схемы ("none",
    // "111111x"...) -> C-константа HAL.
    std::string ownAddress2MaskConstant(const std::string& mask)
    {
        static const std::map<std::string, std::string> kMap = {
            {"none", "I2C_OWNADDRESS2_MASK_DISABLE"},
            {"111111x", "I2C_OWNADDRESS2_MASK_111111x"},
            {"11111xx", "I2C_OWNADDRESS2_MASK_11111xx"},
            {"1111xxx", "I2C_OWNADDRESS2_MASK_1111xxx"},
            {"111xxxx", "I2C_OWNADDRESS2_MASK_111xxxx"},
            {"11xxxxx", "I2C_OWNADDRESS2_MASK_11xxxxx"},
            {"1xxxxxx", "I2C_OWNADDRESS2_MASK_1xxxxxx"},
            {"1111111", "I2C_OWNADDRESS2_MASK_1111111"}};
        const auto it = kMap.find(mask);
        return it != kMap.end() ? it->second : "I2C_OWNADDRESS2_MASK_DISABLE";
    }

    // mik32_hal_i2c.h, HAL_I2C_DigitalFilterTypeDef — число тактов -> C-константа HAL.
    std::string digitalFilterConstant(int cycles)
    {
        switch (cycles)
        {
            case 1: return "I2C_DIGITALFILTER_1CLOCKCYCLES";
            case 2: return "I2C_DIGITALFILTER_2CLOCKCYCLES";
            case 15: return "I2C_DIGITALFILTER_15CLOCKCYCLES";
            default: return "I2C_DIGITALFILTER_OFF";
        }
    }

    // mik32_hal_dma.h, HAL_DMA_ChannelRequestTypeDef: DMA_CHANNEL_I2C_0_REQUEST=5,
    // DMA_CHANNEL_I2C_1_REQUEST=6 — линия запроса ПДП конкретного блока I2C,
    // используется и для Read-, и для Write-стороны канала (оба примера ниже
    // ставят одно и то же значение в ReadRequest/WriteRequest).
    std::string dmaRequestEnum(int instance)
    {
        return "DMA_CHANNEL_I2C_" + std::to_string(instance) + "_REQUEST";
    }

    // mik32_hal_dma.h, HAL_DMA_ChannelIndexTypeDef.
    std::string dmaChannelEnum(int channel)
    {
        return "DMA_CHANNEL_" + std::to_string(channel);
    }

    std::string irqHandlerFunctionName(int instance)
    {
        return "I2C_" + std::to_string(instance) + "_IRQHandler_Template";
    }

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
    const I2cKnowledge& knowledge)
{
    const std::string* i2c_handle = findOrNull(knowledge.instance_handle, config.instance);
    if (!i2c_handle)
    {
        return ParseResult<PeripheralContribution>{
            false, "I2cConfig ссылается на instance, отсутствующий в knowledge_base/i2c.json", std::nullopt};
    }

    const bool is_master = (config.mode == "master");

    const std::string handle_name = "hi2c_" + std::to_string(config.instance);
    const std::string suffix = std::to_string(config.instance);

    const I2cDmaConfig* dma_tx = nullptr;
    const I2cDmaConfig* dma_rx = nullptr;
    for (const I2cDmaConfig& dma : config.dma)
    {
        if (dma.direction == "tx") dma_tx = &dma;
        else if (dma.direction == "rx") dma_rx = &dma;
    }

    PeripheralContribution contribution;
    contribution.label = "I2C_" + suffix;
    contribution.init_order = kInitOrderI2c;
    contribution.init_function_name = "I2C_" + suffix + "_Init";
    contribution.init_c_includes = {"mik32_hal_i2c.h", "mik32_hal_pcc.h"};
    if (dma_tx || dma_rx)
    {
        contribution.init_c_includes.push_back("mik32_hal_dma.h");
    }
    if (config.interrupt_mode)
    {
        contribution.init_c_includes.push_back("mik32_hal_irq.h");
    }
    contribution.global_definitions = "I2C_HandleTypeDef " + handle_name + ";\n";
    contribution.public_prototypes = {
        "void " + contribution.init_function_name + "(void);",
        "void I2C_" + suffix + "_Reset(void);"};

    // ТЗ §3.1.3.3 / §3.1.6 п.2 (тот же глобальный пул ПДП, что у UART) —
    // DMA_ChannelHandleTypeDef объявляются ДО функции Init, т.к. Init
    // ссылается на их адреса (hi2c.hdmatx/hdmarx), а C транслирует файл
    // сверху вниз (project_codegen.cpp уже объявляет общий "hdma" раньше
    // per-contribution глобалов). Поля ChannelInit — буквально по двум
    // реальным вендорным примерам docs/examples/mik32-examples/
    // HAL_DMA_I2C_Master и HAL_DMA_I2C_Slave (DMA_CH0_Init/DMA_CH1_Init),
    // значения в обоих идентичны: Priority=VERY_HIGH, Size=BYTE (TXDR/RXDR
    // 8-битные регистры), ReadAck=WriteAck=DISABLE.
    std::ostringstream dma_globals;
    for (const auto& [dma, is_tx] : std::vector<std::pair<const I2cDmaConfig*, bool>>{{dma_tx, true}, {dma_rx, false}})
    {
        if (!dma) continue;
        const std::string channel_handle = "hdma_" + handle_name + "_" + (is_tx ? "tx" : "rx");
        dma_globals << "DMA_ChannelHandleTypeDef " << channel_handle << ";\n";
    }
    contribution.global_definitions += dma_globals.str();

    std::ostringstream init_c;
    if (!is_master && config.no_stretch_mode)
    {
        // Errata MIK32V2 v1.0.1, §4.1 «Флаг TXIS»: флаг TXIS формируется в
        // конце передачи байта, а не в начале. При NOSTRETCH=1, если за это
        // время не были загружены новые данные, возникает недозагрузка.
        // Обход: не использовать NOSTRETCH=1 без прерываний по TXIS с
        // укороченным обработчиком, либо DMA — этот кодоген всегда генерирует
        // только блокирующие Slave_Transmit/Receive (без прерываний),
        // поэтому предупреждение вставляется безусловно при no_stretch_mode=true.
        init_c << "/*\n"
                  " * Errata п. 4.1 (mik32v2errataV1_0_1_1 v1.0.1): флаг TXIS формируется в конце\n"
                  " * передачи байта, а не в начале. no_stretch_mode=true (NOSTRETCH=1) без\n"
                  " * прерываний/DMA (этот кодоген генерирует только блокирующие вызовы) может\n"
                  " * привести к недозагрузке TXDR. Обход: обслуживать TXIS по прерыванию с\n"
                  " * укороченным обработчиком, либо DMA — ни то, ни другое этим кодогеном не\n"
                  " * генерируется.\n"
                  " */\n";
    }

    init_c << "/*\n"
              " * " << *i2c_handle << ": mode=" << config.mode
           << ", clock_speed=" << config.clock_speed << ".\n"
              " */\n"
              "void " << contribution.init_function_name << "(void)\n"
              "{\n"
              "    " << handle_name << ".Instance = " << *i2c_handle << ";\n"
              "\n";

    // master-поля (AutoEnd) и slave-поля (OwnAddress1/2, DualAddressMode,
    // GeneralCall, OwnAddress2Mask, NoStretchMode, SBCMode) не пересекаются —
    // читаются только в своей ветке HAL_I2C_Init (mik32_hal_i2c.c:178-197),
    // поэтому генерируются только для своего mode. DigitalFilter/AnalogFilter
    // читаются ОБЕИМИ ветками — отсюда общие schemas/i2c.json-поля.
    const std::string digital_filter_constant = digitalFilterConstant(config.digital_filter);
    // mik32_hal_i2c.h: значения инвертированы — ENABLE=0, DISABLE=1.
    const std::string analog_filter_constant =
        config.analog_filter ? "I2C_ANALOGFILTER_ENABLE" : "I2C_ANALOGFILTER_DISABLE";

    if (is_master)
    {
        init_c << "    " << handle_name << ".Init.Mode = HAL_I2C_MODE_MASTER;\n"
                  "    " << handle_name << ".Init.DigitalFilter = " << digital_filter_constant << ";\n"
                  "    " << handle_name << ".Init.AnalogFilter = " << analog_filter_constant << ";\n"
                  "    " << handle_name << ".Init.AutoEnd = I2C_AUTOEND_ENABLE;\n";
    }
    else
    {
        const std::string dual_address_constant =
            config.dual_address_enabled ? "I2C_DUALADDRESS_ENABLE" : "I2C_DUALADDRESS_DISABLE";
        const std::string general_call_constant =
            config.general_call ? "I2C_GENERALCALL_ENABLE" : "I2C_GENERALCALL_DISABLE";
        const std::string no_stretch_constant =
            config.no_stretch_mode ? "I2C_NOSTRETCH_ENABLE" : "I2C_NOSTRETCH_DISABLE";

        init_c << "    " << handle_name << ".Init.Mode = HAL_I2C_MODE_SLAVE;\n"
                  "    " << handle_name << ".Init.DigitalFilter = " << digital_filter_constant << ";\n"
                  "    " << handle_name << ".Init.AnalogFilter = " << analog_filter_constant << ";\n"
                  "    " << handle_name << ".Init.OwnAddress1 = " << config.own_address << ";\n"
                  "    " << handle_name << ".Init.DualAddressMode = " << dual_address_constant << ";\n"
                  "    " << handle_name << ".Init.OwnAddress2 = "
               << (config.dual_address_enabled ? config.own_address_2 : 0) << ";\n"
                  "    " << handle_name << ".Init.OwnAddress2Mask = "
               << (config.dual_address_enabled ? ownAddress2MaskConstant(config.own_address_2_mask)
                                                : "I2C_OWNADDRESS2_MASK_DISABLE") << ";\n"
                  "    " << handle_name << ".Init.GeneralCall = " << general_call_constant << ";\n"
                  "    " << handle_name << ".Init.NoStretchMode = " << no_stretch_constant << ";\n"
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
              "    " << handle_name << ".Clock.SCLL = " << timing.scll << ";\n";

    // hi2c.hdmatx/hdmarx должны быть заполнены ДО HAL_I2C_Init — сами
    // _DMA-функции (mik32_hal_i2c.c:983+) их не трогают, используют как
    // готовую ссылку в рантайме. По образцу I2C0_Init из обоих вендорных
    // примеров (HAL_DMA_I2C_Master/Slave, main.c): "Каналы DMA" идут сразу
    // после Clock.*, перед вызовом HAL_I2C_Init.
    if (dma_tx || dma_rx)
    {
        init_c << "\n"
                  "    /* Каналы DMA (ТЗ §3.1.6 п.2) */\n";
        if (dma_tx)
        {
            init_c << "    " << handle_name << ".hdmatx = &hdma_" << handle_name << "_tx;\n";
        }
        if (dma_rx)
        {
            init_c << "    " << handle_name << ".hdmarx = &hdma_" << handle_name << "_rx;\n";
        }
    }

    init_c << "\n"
              "    HAL_I2C_Init(&" << handle_name << ");\n";

    // Настройка ChannelInit самих каналов — ПОСЛЕ HAL_I2C_Init, по тому же
    // порядку, что в вендорных DMA_CH0_Init/DMA_CH1_Init (там это отдельные
    // функции, вызываемые из общего DMA_Init() после настройки DMA_CONFIG;
    // здесь порядок внутри одной функции не важен для корректности — HAL
    // не читает ChannelInit внутри HAL_I2C_Init, — но сохраняется для
    // единообразия с остальными полями .dma у этого же хендла).
    for (const auto& [dma, is_tx] : std::vector<std::pair<const I2cDmaConfig*, bool>>{{dma_tx, true}, {dma_rx, false}})
    {
        if (!dma) continue;
        const std::string channel_handle = "hdma_" + handle_name + "_" + (is_tx ? "tx" : "rx");
        const std::string request = dmaRequestEnum(config.instance);
        init_c << "\n"
                  "    " << channel_handle << ".dma = &hdma;\n"
                  "    " << channel_handle << ".ChannelInit.Channel = " << dmaChannelEnum(dma->channel) << ";\n"
                  "    " << channel_handle << ".ChannelInit.Priority = DMA_CHANNEL_PRIORITY_VERY_HIGH;\n"
                  "    " << channel_handle << ".ChannelInit.ReadMode = " << (is_tx ? "DMA_CHANNEL_MODE_MEMORY" : "DMA_CHANNEL_MODE_PERIPHERY") << ";\n"
                  "    " << channel_handle << ".ChannelInit.ReadInc = " << (is_tx ? "DMA_CHANNEL_INC_ENABLE" : "DMA_CHANNEL_INC_DISABLE") << ";\n"
                  "    " << channel_handle << ".ChannelInit.ReadSize = DMA_CHANNEL_SIZE_BYTE;\n"
                  "    " << channel_handle << ".ChannelInit.ReadBurstSize = 0;\n"
                  "    " << channel_handle << ".ChannelInit.ReadRequest = " << request << ";\n"
                  "    " << channel_handle << ".ChannelInit.ReadAck = DMA_CHANNEL_ACK_DISABLE;\n"
                  "    " << channel_handle << ".ChannelInit.WriteMode = " << (is_tx ? "DMA_CHANNEL_MODE_PERIPHERY" : "DMA_CHANNEL_MODE_MEMORY") << ";\n"
                  "    " << channel_handle << ".ChannelInit.WriteInc = " << (is_tx ? "DMA_CHANNEL_INC_DISABLE" : "DMA_CHANNEL_INC_ENABLE") << ";\n"
                  "    " << channel_handle << ".ChannelInit.WriteSize = DMA_CHANNEL_SIZE_BYTE;\n"
                  "    " << channel_handle << ".ChannelInit.WriteBurstSize = 0;\n"
                  "    " << channel_handle << ".ChannelInit.WriteRequest = " << request << ";\n"
                  "    " << channel_handle << ".ChannelInit.WriteAck = DMA_CHANNEL_ACK_DISABLE;\n";
    }

    init_c << "}\n";
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

    // interrupt_mode: шаблон обработчика — по тому же паттерну, что и
    // codegen/project/spi_contribution.cpp (irqHandlerFunctionName). Линия
    // EPIC I2C подтверждена shared/periphery/epic.h (EPIC_LINE_I2C_0_S=6,
    // EPIC_LINE_I2C_1_S=7) и mik32_hal_irq.h (HAL_EPIC_I2C_x_MASK,
    // EPIC_CHECK_I2C_x) — в отличие от SPI, сами HAL_I2C_*_IT функции
    // настраивают маску I2C_CR1 прерываний внутри себя (mik32_hal_i2c.c:
    // 1139+, HAL_I2C_InterruptEnable), поэтому init-функции здесь ничего
    // дополнительно включать не нужно — только обработчик.
    if (config.interrupt_mode)
    {
        helpers << "\n"
                   "/*\n"
                   " * Шаблон обработчика прерывания " << *i2c_handle << ". Вызовите его из своего\n"
                   " * raw_trap_handler внутри if (EPIC_CHECK_I2C_" << suffix << "()) { ... }, после\n"
                   " * включения линии в EPIC (HAL_EPIC_MaskLevelSet(HAL_EPIC_I2C_" << suffix << "_MASK))\n"
                   " * и HAL_IRQ_EnableInterrupts() — см. mik32_hal_irq.h, shared/periphery/epic.h.\n"
                   " * Сами HAL_I2C_Master/Slave_*_IT функции настраивают маску прерываний\n"
                   " * I2C самостоятельно (mik32_hal_i2c.c, HAL_I2C_InterruptEnable).\n"
                   " * HW-подтверждённый баг HAL (2026-10-21, см. docs/implementations/i2c.md):\n"
                   " * HAL_I2C_Master_Receive_IT НЕ включает NACKIE — NACK на фазе чтения\n"
                   " * вешает ожидание state==END навечно. Добавьте вручную сразу после вызова:\n"
                   " * HAL_I2C_InterruptEnable(&" << handle_name << ", I2C_CR1_NACKIE_M);\n"
                   " */\n"
                   "void " << irqHandlerFunctionName(config.instance) << "(void)\n"
                   "{\n"
                   "    HAL_I2C_IRQHandler(&" << handle_name << ");\n"
                   "}\n";
        contribution.irq_handler_function_name = irqHandlerFunctionName(config.instance);
    }
    contribution.helper_functions_text = helpers.str();

    return ParseResult<PeripheralContribution>{true, "", contribution};
}
