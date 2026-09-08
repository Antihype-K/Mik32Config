#include "spi_contribution.h"
#include "contribution_utils.h"
#include <sstream>

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
}

ParseResult<PeripheralContribution> buildSpiContribution(
    const SpiConfig& config,
    const SpiKnowledge& knowledge,
    bool console_available,
    bool emit_demo_loop)
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
              "    }\n"
              "}\n";
    contribution.init_function_text = init_c.str();

    if (!emit_demo_loop)
    {
        return ParseResult<PeripheralContribution>{true, "", contribution};
    }

    // Тело цикла: обмен фиксированным 4-байтным паттерном, как в обоих
    // вендорных примерах (docs/examples/mik32-examples/HAL_SPI_Master/Slave,
    // main.c) — HAL_SPI_Exchange (блокирующий, полнодуплексный). Печать в
    // UART не обязательна: обмен наблюдаем и без консоли (MOSI/MISO/SCK на
    // осциллографе/логическом анализаторе), в отличие от I2C-сканера.
    std::ostringstream loop;
    const std::string tx_pattern = is_master ? "0xA0, 0xA1, 0xA2, 0xA3" : "0xB0, 0xB1, 0xB2, 0xB3";
    loop << "            uint8_t spi" << suffix << "_tx[4] = {" << tx_pattern << "};\n"
            "            uint8_t spi" << suffix << "_rx[4];\n";
    if (is_master && config.manual_cs)
    {
        loop << "            __HAL_SPI_ENABLE(&" << handle_name << ");\n"
                "            HAL_SPI_CS_Enable(&" << handle_name << ", " << chipSelectMacro(config.chip_select) << ");\n";
    }
    loop << "            HAL_SPI_Exchange(&" << handle_name << ", spi" << suffix << "_tx, spi" << suffix
         << "_rx, 4, SPI_TIMEOUT_DEFAULT);\n";
    if (is_master && config.manual_cs)
    {
        loop << "            HAL_SPI_CS_Disable(&" << handle_name << ");\n"
                "            __HAL_SPI_DISABLE(&" << handle_name << ");\n";
    }
    if (console_available)
    {
        loop << "            xprintf(\"" << contribution.label
             << ": rx = %02X %02X %02X %02X\\r\\n\", spi" << suffix << "_rx[0], spi" << suffix
             << "_rx[1], spi" << suffix << "_rx[2], spi" << suffix << "_rx[3]);\n";
    }

    contribution.loop_body = loop.str();
    contribution.wants_loop_delay = true;
    contribution.main_c_includes = {"mik32_hal_spi.h"};
    if (console_available)
    {
        contribution.main_c_includes.push_back("xprintf.h");
    }
    contribution.extern_declarations = "extern SPI_HandleTypeDef " + handle_name + ";\n";

    return ParseResult<PeripheralContribution>{true, "", contribution};
}
