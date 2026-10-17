#include "spi_contribution.h"
#include "contribution_utils.h"
#include <sstream>
#include <map>

namespace
{
    // "SPI_BAUDRATE_DIV64" — HAL-макрос для делителя (mik32_hal_spi.h:28-35).
    std::string baudRateDivMacro(int value)
    {
        return "SPI_BAUDRATE_DIV" + std::to_string(value);
    }

    // "SPI_CS_2" — HAL-макрос для номера ведомого (mik32_hal_spi.h:22-26).
    std::string chipSelectMacro(const std::string& chip_select)
    {
        // "cs2" -> "SPI_CS_2": последний символ chip_select — уже нужная цифра.
        return "SPI_CS_" + chip_select.substr(2);
    }

    // "SPI_INT_STATUS_RX_FIFO_NOT_EMPTY_M" — HAL-маска прерывания
    // (mik32_hal_spi.h:259-267, см. schemas/spi.json про допустимый набор).
    std::string interruptMaskMacro(const std::string& interrupt)
    {
        static const std::map<std::string, std::string> kMacroByName = {
            {"rx_fifo_not_empty", "SPI_INT_STATUS_RX_FIFO_NOT_EMPTY_M"},
            {"tx_fifo_not_full", "SPI_INT_STATUS_TX_FIFO_NOT_FULL_M"},
            {"rx_overflow", "SPI_INT_STATUS_RX_OVERFLOW_M"},
            {"mode_fail", "SPI_INT_STATUS_MODE_FAIL_M"},
        };
        return kMacroByName.at(interrupt);
    }

    std::string irqHandlerFunctionName(int instance)
    {
        return "SPI_" + std::to_string(instance) + "_IRQHandler_Template";
    }
}

ParseResult<PeripheralContribution> buildSpiContribution(
    const SpiConfig& config,
    const SpiKnowledge& knowledge)
{
    const std::string* spi_handle = findOrNull(knowledge.instance_handle, config.instance);
    if (!spi_handle)
    {
        return ParseResult<PeripheralContribution>{
            false, "SpiConfig ссылается на instance, отсутствующий в knowledge_base/spi.json", std::nullopt};
    }

    const bool is_master = (config.mode == "master");
    const std::string handle_name = "hspi" + std::to_string(config.instance);
    const std::string suffix = std::to_string(config.instance);

    PeripheralContribution contribution;
    contribution.label = "SPI_" + suffix;
    contribution.init_order = kInitOrderSpi;
    contribution.init_function_name = "SPI_" + suffix + "_Init";
    contribution.init_c_includes = {"mik32_hal_spi.h", "mik32_hal_pcc.h"};
    if (!config.interrupts.empty())
    {
        contribution.init_c_includes.push_back("mik32_hal_irq.h");
    }
    contribution.global_definitions = "SPI_HandleTypeDef " + handle_name + ";\n";
    contribution.public_prototypes = {
        "void " + contribution.init_function_name + "(void);"};

    std::ostringstream init_c;

    // Errata MIK32V2 v1.0.1, §7.1 «Автоматический выбор ведомых SPI0» —
    // актуальна только для instance=0, mode=master, manual_cs=false (авто-CS)
    // и chip_select=cs3: SPI0_n_ss_out_3 дублирует SPI0_n_ss_out_2, реального
    // 4-го CS в этой комбинации не существует. Ситуации из Errata по
    // конвенции проекта — Предупреждение (не Ошибка, кодоген не
    // блокируется), см. codegen/project/gpio_contribution.cpp (errata п.3.1)
    // и codegen/project/timer32_contribution.cpp (errata п.9.1) для того же
    // паттерна "комментарий вместо отказа".
    const bool errata_7_1_applies = (config.instance == 0) && is_master
        && !config.manual_cs && (config.chip_select == "cs3");
    if (errata_7_1_applies)
    {
        init_c << "/*\n"
                  " * Errata п. 7.1 (mik32v2errataV1_0_1_1 v1.0.1): в автоматическом режиме\n"
                  " * выбора ведомых (MANUAL_CS=0) на SPI_0 вывод SPI0_n_ss_out_3 не работает\n"
                  " * и дублирует SPI0_n_ss_out_2 — фактически будет выбран тот же ведомый,\n"
                  " * что и для chip_select=cs2. Обход: используйте manual_cs=true и любой\n"
                  " * GPIO для ручного управления 4-м CS вместо встроенного авто-выбора.\n"
                  " */\n";
    }

    init_c << "/*\n"
              " * " << *spi_handle << ": mode=" << config.mode
           << ", clk_phase=" << config.clk_phase << ", clk_polarity=" << config.clk_polarity;
    if (is_master)
    {
        init_c << ", baud_rate_div=" << config.baud_rate_div << ", chip_select=" << config.chip_select;
    }
    init_c << ".\n"
              " */\n"
              "void " << contribution.init_function_name << "(void)\n"
              "{\n"
              "    " << handle_name << ".Instance = " << *spi_handle << ";\n"
              "\n"
              "    " << handle_name << ".Init.SPI_Mode = "
           << (is_master ? "HAL_SPI_MODE_MASTER" : "HAL_SPI_MODE_SLAVE") << ";\n"
              "    " << handle_name << ".Init.CLKPhase = "
           << (config.clk_phase == "on" ? "SPI_PHASE_ON" : "SPI_PHASE_OFF") << ";\n"
              "    " << handle_name << ".Init.CLKPolarity = "
           << (config.clk_polarity == "high" ? "SPI_POLARITY_HIGH" : "SPI_POLARITY_LOW") << ";\n"
              "    " << handle_name << ".Init.ThresholdTX = " << config.threshold_tx << ";\n";

    // Поля ниже читает только master-ветка SPI_config в HAL_SPI_Init
    // (mik32_hal_spi.c:232-241) — как и вендорный пример HAL_SPI_Slave,
    // для slave их не пишем вовсе, а не заполняем нулями.
    if (is_master)
    {
        init_c << "\n"
                  "    " << handle_name << ".Init.BaudRateDiv = " << baudRateDivMacro(config.baud_rate_div) << ";\n"
                  "    " << handle_name << ".Init.Decoder = "
               << (config.decoder ? "SPI_DECODER_USE" : "SPI_DECODER_NONE") << ";\n"
                  "    " << handle_name << ".Init.ManualCS = "
               << (config.manual_cs ? "SPI_MANUALCS_ON" : "SPI_MANUALCS_OFF") << ";\n"
                  "    " << handle_name << ".Init.ChipSelect = " << chipSelectMacro(config.chip_select) << ";\n";
    }

    init_c << "\n"
              "    if (HAL_SPI_Init(&" << handle_name << ") != HAL_OK)\n"
              "    {\n"
              "        /* USER TODO: обработать ошибку инициализации SPI (SPI_Init_Error). */\n"
              "    }\n";

    // Прерывания — единственный закрываемый пробел по SPI из
    // AUDIT_TZ_PERIPHERY_GAPS.md. HAL_SPI_InterruptEnable пишет в
    // INT_ENABLE маску выбранных флагов (mik32_hal_spi.h:259-267) — по
    // образцу обоих вендорных примеров HAL_SPI_Master_IRQ/HAL_SPI_Slave_IRQ
    // (main.c, SPIx_Init). Включение самой линии в EPIC
    // (HAL_EPIC_MaskLevelSet(HAL_EPIC_SPI_x_MASK)) и HAL_IRQ_EnableInterrupts()
    // остаются на стороне пользователя — тот же паттерн, что у GPIO IRQ
    // (см. codegen/project/gpio_contribution.cpp).
    if (!config.interrupts.empty())
    {
        std::ostringstream mask;
        for (size_t i = 0; i < config.interrupts.size(); ++i)
        {
            if (i > 0)
            {
                mask << " |\n        ";
            }
            mask << interruptMaskMacro(config.interrupts[i]);
        }
        init_c << "\n"
                  "    /*\n"
                  "     * Включение прерываний SPI. Саму линию SPI_" << suffix << " в EPIC\n"
                  "     * (HAL_EPIC_MaskLevelSet(HAL_EPIC_SPI_" << suffix << "_MASK)) и\n"
                  "     * HAL_IRQ_EnableInterrupts() нужно включить отдельно на стороне\n"
                  "     * пользователя, как и подключение " << irqHandlerFunctionName(config.instance)
               << "() к своему raw_trap_handler.\n"
                  "     */\n"
                  "    HAL_SPI_InterruptEnable(&" << handle_name << ", " << mask.str() << ");\n";
    }

    init_c << "}\n";
    contribution.init_function_text = init_c.str();

    if (!config.interrupts.empty())
    {
        std::ostringstream irq_template;
        irq_template << "/*\n"
                        " * Шаблон обработчика прерывания SPI_" << suffix << ". Вызовите его из своего\n"
                        " * raw_trap_handler внутри if (EPIC_CHECK_SPI_" << suffix << "()) { ... } — см.\n"
                        " * docs/examples/mik32-examples/HAL_SPI_Master_IRQ/src/main.c, trap_handler.\n"
                        " */\n"
                        "void " << irqHandlerFunctionName(config.instance) << "(void)\n"
                        "{\n"
                        "    HAL_SPI_IRQHandler(&" << handle_name << ");\n"
                        "}\n";
        contribution.helper_functions_text = irq_template.str();
        contribution.irq_handler_function_name = irqHandlerFunctionName(config.instance);
    }

    return ParseResult<PeripheralContribution>{true, "", contribution};
}
