#include "uart_contribution.h"
#include "contribution_utils.h"
#include <iomanip>
#include <sstream>
#include <utility>
#include <vector>

namespace
{
    const UartDmaConfig* findDmaConfig(const UartConfig& config, const std::string& direction)
    {
        for (const UartDmaConfig& dma : config.dma)
        {
            if (dma.direction == direction)
            {
                return &dma;
            }
        }
        return nullptr;
    }
}

ParseResult<PeripheralContribution> buildUartContribution(
    const UartConfig& config,
    const UartKnowledge& knowledge,
    bool is_console)
{
    const std::string* uart_handle = findOrNull(knowledge.instance_handle, config.instance);
    const std::string* frame_enum = findOrNull(knowledge.data_bits_hal_enum, std::to_string(config.data_bits));
    const std::string* stop_bit_enum = findOrNull(knowledge.stop_bits_hal_enum, std::to_string(config.stop_bits));
    const UartKnowledge::ParitySetting* parity_setting = findOrNull(knowledge.parity_hal_setting, config.parity);

    if (!uart_handle || !frame_enum || !stop_bit_enum || !parity_setting)
    {
        return ParseResult<PeripheralContribution>{
            false,
            "UartConfig ссылается на instance/data_bits/stop_bits/parity, отсутствующий в knowledge_base/uart.json",
            std::nullopt};
    }

    const std::string handle_name = "husart_" + std::to_string(config.instance);
    const UartDmaConfig* dma_tx = findDmaConfig(config, "tx");
    const UartDmaConfig* dma_rx = findDmaConfig(config, "rx");

    PeripheralContribution contribution;
    contribution.label = "UART_" + std::to_string(config.instance);
    contribution.init_order = kInitOrderUart;
    contribution.init_function_name = "UART_" + std::to_string(config.instance) + "_Init";
    // Только mik32_hal_usart.h: в отличие от GPIO/Timer16/I2C, тело
    // UART_x_Init не обращается ни к одному макросу PCC — HAL_USART_Init
    // включает тактирование самостоятельно. Лишний #include здесь был бы
    // безвреден (в SDK везде include guard'ы), но замусоривал бы вывод.
    contribution.init_c_includes = {"mik32_hal_usart.h"};
    contribution.public_prototypes = {"void " + contribution.init_function_name + "(void);"};
    if (dma_tx)
    {
        contribution.public_prototypes.push_back("void " + contribution.label + "_DMA_TX_Start(void);");
    }
    if (dma_rx)
    {
        contribution.public_prototypes.push_back("void " + contribution.label + "_DMA_RX_Start(void);");
    }
    contribution.global_definitions = "USART_HandleTypeDef " + handle_name + ";\n";

    // ТЗ §3.1.3: direction управляет тем, какие направления включены;
    // flow_control — аппаратным RTS/CTS.
    const std::string flow = config.flow_control ? "Enable" : "Disable";
    const std::string transmitting = (config.direction == "tx" || config.direction == "duplex") ? "Enable" : "Disable";
    const std::string receiving = (config.direction == "rx" || config.direction == "duplex") ? "Enable" : "Disable";

    // OVERNIGHT_FIELD_REFERENCE.md — новые необязательные поля этой сессии,
    // 1:1 перевод строковых значений JSON в литералы HAL-перечислений
    // (mik32_hal_usart.h). Валидатор (parser/USART/usart_parser.cpp) уже
    // гарантировал допустимость значений и запрет mode=synchronous на instance=1.
    const std::string bit_direction_literal = (config.bit_direction == "msb_first") ? "MSB_First" : "LSB_First";
    const std::string mode_literal = (config.mode == "synchronous") ? "Synchronous_Mode" : "Asynchronous_Mode";
    const std::string xck_mode_literal =
        (config.xck_mode == "mode1") ? "XCK_Mode1" :
        (config.xck_mode == "mode2") ? "XCK_Mode2" :
        (config.xck_mode == "mode3") ? "XCK_Mode3" : "XCK_Mode0";
    const std::string last_byte_clock_literal = config.last_byte_clock ? "Enable" : "Disable";
    const std::string channel_mode_literal = (config.channel_mode == "half_duplex") ? "Semiduplex_Mode" : "Duplex_Mode";
    const std::string tx_inversion_literal = config.tx_inversion ? "Enable" : "Disable";
    const std::string rx_inversion_literal = config.rx_inversion ? "Enable" : "Disable";
    const std::string swap_literal = config.swap ? "Enable" : "Disable";
    const std::string rts_mode_literal = (config.rts_mode == "modem") ? "Modem_mode" : "AlwaysEnable_mode";
    const std::string modem_dtr_literal = (config.has_modem_signals && config.modem_signals.dtr) ? "Enable" : "Disable";
    const std::string modem_dcd_literal = (config.has_modem_signals && config.modem_signals.dcd) ? "Enable" : "Disable";
    const std::string modem_dsr_literal = (config.has_modem_signals && config.modem_signals.dsr) ? "Enable" : "Disable";
    const std::string modem_ri_literal = (config.has_modem_signals && config.modem_signals.ri) ? "Enable" : "Disable";
    const std::string modem_ddis_literal = (config.has_modem_signals && config.modem_signals.ddis) ? "Enable" : "Disable";

    // ТЗ §3.1.3.2/§3.1.8: фактическая скорость получается делением HSI 32МГц
    // на целочисленный делитель, поэтому запрошенная скорость достижима не
    // точно. Погрешность выводится комментарием — GUI, который показал бы её
    // пользователю до генерации, ещё не написан.
    const long divider = knowledge.hsi_hz / config.baud_rate;
    const double actual_baud_rate = static_cast<double>(knowledge.hsi_hz) / static_cast<double>(divider);
    const double baud_error_percent = (actual_baud_rate - config.baud_rate) / config.baud_rate * 100.0;

    std::ostringstream init_c;
    init_c << std::fixed << std::setprecision(3);
    init_c << "/*\n"
              " * " << *uart_handle << ": baud_rate=" << config.baud_rate
           << ", data_bits=" << config.data_bits << ", stop_bits=" << config.stop_bits
           << ", parity=" << config.parity << ", flow_control=" << (config.flow_control ? "true" : "false")
           << ", direction=" << config.direction << ".\n"
              " * Фактическая скорость при HSI=" << knowledge.hsi_hz << "Гц: DIVIDER=" << divider
           << " -> " << actual_baud_rate << " бод, погрешность " << baud_error_percent << "%\n"
              " * mode=" << config.mode << ", bit_direction=" << config.bit_direction
           << ", channel_mode=" << config.channel_mode << ".\n"
              " */\n"
              "void " << contribution.init_function_name << "(void)\n"
              "{\n"
              "    " << handle_name << ".Instance = " << *uart_handle << ";\n"
              "    " << handle_name << ".transmitting = " << transmitting << ";\n"
              "    " << handle_name << ".receiving = " << receiving << ";\n"
              "    " << handle_name << ".frame = " << *frame_enum << ";\n"
              "    " << handle_name << ".parity_bit = " << parity_setting->parity_bit << ";\n"
              "    " << handle_name << ".parity_bit_inversion = " << parity_setting->parity_bit_inversion << ";\n"
              "    " << handle_name << ".bit_direction = " << bit_direction_literal << ";\n"
              "    " << handle_name << ".data_inversion = Disable;\n"
              "    " << handle_name << ".tx_inversion = " << tx_inversion_literal << ";\n"
              "    " << handle_name << ".rx_inversion = " << rx_inversion_literal << ";\n"
              "    " << handle_name << ".swap = " << swap_literal << ";\n"
              "    " << handle_name << ".lbm = Disable;\n"
              "    " << handle_name << ".stop_bit = " << *stop_bit_enum << ";\n"
              "    " << handle_name << ".mode = " << mode_literal << ";\n"
              "    " << handle_name << ".xck_mode = " << xck_mode_literal << ";\n"
              "    " << handle_name << ".last_byte_clock = " << last_byte_clock_literal << ";\n"
              "    " << handle_name << ".overwrite = Disable;\n"
              "    " << handle_name << ".rts_mode = " << rts_mode_literal << ";\n"
              "    " << handle_name << ".dma_tx_request = " << (dma_tx ? "Enable" : "Disable") << ";\n"
              "    " << handle_name << ".dma_rx_request = " << (dma_rx ? "Enable" : "Disable") << ";\n"
              "    " << handle_name << ".channel_mode = " << channel_mode_literal << ";\n"
              "    " << handle_name << ".tx_break_mode = Disable;\n"
              "    " << handle_name << ".Interrupt.ctsie = Disable;\n"
              "    " << handle_name << ".Interrupt.eie = Disable;\n"
              "    " << handle_name << ".Interrupt.idleie = Disable;\n"
              "    " << handle_name << ".Interrupt.lbdie = Disable;\n"
              "    " << handle_name << ".Interrupt.peie = Disable;\n"
              "    " << handle_name << ".Interrupt.rxneie = Disable;\n"
              "    " << handle_name << ".Interrupt.tcie = Disable;\n"
              "    " << handle_name << ".Interrupt.txeie = Disable;\n"
              "    " << handle_name << ".Modem.rts = " << flow << ";\n"
              "    " << handle_name << ".Modem.cts = " << flow << ";\n"
              "    " << handle_name << ".Modem.dtr = " << modem_dtr_literal << ";\n"
              "    " << handle_name << ".Modem.dcd = " << modem_dcd_literal << ";\n"
              "    " << handle_name << ".Modem.dsr = " << modem_dsr_literal << ";\n"
              "    " << handle_name << ".Modem.ri = " << modem_ri_literal << ";\n"
              "    " << handle_name << ".Modem.ddis = " << modem_ddis_literal << ";\n"
              "    " << handle_name << ".baudrate = " << config.baud_rate << ";\n"
              "\n"
              "    HAL_USART_Init(&" << handle_name << ");\n";

    // ТЗ §3.1.3.3: конфигурация DMA-канала — по образцу
    // docs/examples/mik32-examples/HAL_USART_DMA/src/main.c, DMA_CH0_Init
    // (строки 80-101). Настраивается здесь (после HAL_USART_Init, но в той
    // же функции), а не в отдельном init — DMA_Init() уже отработал раньше
    // из main() (см. project_codegen.cpp, any_uart_dma) и заполнил hdma,
    // на которую этот канал ссылается через ChannelInit-независимое поле .dma.
    std::vector<std::pair<const UartDmaConfig*, std::string>> dma_configs;
    if (dma_tx) dma_configs.push_back({dma_tx, "tx"});
    if (dma_rx) dma_configs.push_back({dma_rx, "rx"});

    std::ostringstream dma_globals;
    std::ostringstream dma_helpers;
    std::ostringstream dma_extern_declarations;
    for (const auto& [dma, direction] : dma_configs)
    {
        const std::string* channel_enum = findOrNull(knowledge.dma_channel_hal_enum, std::to_string(dma->channel));
        const std::string* request_enum = findOrNull(knowledge.dma_request_enum, config.instance);
        const std::string* size_enum = findOrNull(knowledge.dma_element_size_hal_enum, dma->element_size);
        if (!channel_enum || !request_enum || !size_enum)
        {
            return ParseResult<PeripheralContribution>{
                false,
                "UartDmaConfig ссылается на channel/instance/element_size, отсутствующий в knowledge_base/uart.json",
                std::nullopt};
        }

        const std::string channel_handle = "hdma_" + handle_name + "_" + direction;
        const std::string buffer_name = handle_name + "_" + direction + "_dma_buffer";
        const bool is_tx = (direction == "tx");
        const std::string inc_enum = dma->increment ? "DMA_CHANNEL_INC_ENABLE" : "DMA_CHANNEL_INC_DISABLE";

        dma_globals << "DMA_ChannelHandleTypeDef " << channel_handle << ";\n"
                    << "uint8_t " << buffer_name << "[" << dma->buffer_size_bytes << "];\n";
        // extern в main.c — иначе прикладной код не сможет ни заполнить TX-
        // буфер данными для отправки, ни прочитать RX-буфер после приёма:
        // сам буфер объявлен в mik32_init.c и снаружи не виден без этого.
        dma_extern_declarations << "extern uint8_t " << buffer_name << "[" << dma->buffer_size_bytes << "];\n";

        // TX: память (буфер, инкремент по настройке) -> периферия (TXDATA,
        // без инкремента — адрес регистра фиксирован). RX: наоборот.
        init_c << "\n"
                  "    " << channel_handle << ".dma = &hdma;\n"
                  "    " << channel_handle << ".ChannelInit.Channel = " << *channel_enum << ";\n"
                  "    " << channel_handle << ".ChannelInit.Priority = DMA_CHANNEL_PRIORITY_VERY_HIGH;\n"
                  "    " << channel_handle << ".ChannelInit.ReadMode = " << (is_tx ? "DMA_CHANNEL_MODE_MEMORY" : "DMA_CHANNEL_MODE_PERIPHERY") << ";\n"
                  "    " << channel_handle << ".ChannelInit.ReadInc = " << (is_tx ? inc_enum : "DMA_CHANNEL_INC_DISABLE") << ";\n"
                  "    " << channel_handle << ".ChannelInit.ReadSize = " << *size_enum << ";\n"
                  "    " << channel_handle << ".ChannelInit.ReadBurstSize = 0;\n"
                  "    " << channel_handle << ".ChannelInit.ReadRequest = " << *request_enum << ";\n"
                  "    " << channel_handle << ".ChannelInit.ReadAck = " << (is_tx ? "DMA_CHANNEL_ACK_DISABLE" : "DMA_CHANNEL_ACK_ENABLE") << ";\n"
                  "    " << channel_handle << ".ChannelInit.WriteMode = " << (is_tx ? "DMA_CHANNEL_MODE_PERIPHERY" : "DMA_CHANNEL_MODE_MEMORY") << ";\n"
                  "    " << channel_handle << ".ChannelInit.WriteInc = " << (is_tx ? "DMA_CHANNEL_INC_DISABLE" : inc_enum) << ";\n"
                  "    " << channel_handle << ".ChannelInit.WriteSize = " << *size_enum << ";\n"
                  "    " << channel_handle << ".ChannelInit.WriteBurstSize = 0;\n"
                  "    " << channel_handle << ".ChannelInit.WriteRequest = " << *request_enum << ";\n"
                  "    " << channel_handle << ".ChannelInit.WriteAck = " << (is_tx ? "DMA_CHANNEL_ACK_ENABLE" : "DMA_CHANNEL_ACK_DISABLE") << ";\n";

        dma_helpers << "/*\n"
                       " * buffer_type=" << dma->buffer_type << ".";
        if (dma->buffer_type == "circular")
        {
            dma_helpers << " ТЗ §3.1.3.3 просит кольцевой буфер, но HAL не умеет\n"
                           " * зацикливать передачу сам (HAL_DMA_Start — всегда одноразовая пересылка,\n"
                           " * см. knowledge_base/uart.json, not_covered_by_this_codegen.dma_circular_buffer) —\n"
                           " * вызвать эту функцию снова нужно из обработчика прерывания DMA (HAL_DMA_LocalIRQEnable\n"
                           " * + HAL_DMA_GetChannelIrq), это не реализовано автоматически.\n";
        }
        else
        {
            dma_helpers << " Одноразовая передача — вызвать функцию снова для следующей.\n";
        }
        dma_helpers << " */\n"
                       "void " << contribution.label << "_DMA_" << (is_tx ? "TX" : "RX") << "_Start(void)\n"
                       "{\n"
                       "    HAL_DMA_Start(&" << channel_handle << ", "
                    << (is_tx ? ("(void*)" + buffer_name) : ("(void*)&" + *uart_handle + "->RXDATA"))
                    << ", "
                    << (is_tx ? ("(void*)&" + *uart_handle + "->TXDATA") : ("(void*)" + buffer_name))
                    << ", " << dma->buffer_size_bytes << ");\n"
                       "}\n\n";
    }
    init_c << "}\n";
    contribution.init_function_text = init_c.str();
    contribution.global_definitions += dma_globals.str();
    contribution.extern_declarations += dma_extern_declarations.str();

    // Сильная (переопределяющая слабую) реализация xputc — обоснование в
    // uart_contribution.h. Прототип в mik32_init.h не выносится: xputc
    // объявлена в xprintf.h из SDK, и своё объявление создало бы риск
    // расхождения сигнатур.
    if (is_console)
    {
        std::ostringstream helpers;
        helpers << "/*\n"
                   " * Приёмник вывода xprintf. Штатная слабая реализация xputc\n"
                   " * (mik32_hal_usart.c:435) жёстко привязана к UART_0 — эта заменяет её\n"
                   " * и печатает в выбранный конфигурацией " << *uart_handle << ".\n"
                   " */\n"
                   "void xputc(char c)\n"
                   "{\n"
                   "    HAL_USART_Transmit(&" << handle_name << ", c, USART_TIMEOUT_DEFAULT);\n"
                   "}\n";
        contribution.helper_functions_text += helpers.str();
    }
    contribution.helper_functions_text += dma_helpers.str();

    return ParseResult<PeripheralContribution>{true, "", contribution};
}
