#include "wdt_contribution.h"
#include "contribution_utils.h"
#include <sstream>

namespace
{
    // mik32_hal_wdt.h, HAL_WDT_Clocks.
    std::string clockSourceMacro(const std::string& clock_source)
    {
        if (clock_source == "osc32m") return "HAL_WDT_OSC32M";
        if (clock_source == "hsi32m") return "HAL_WDT_HSI32M";
        if (clock_source == "osc32k") return "HAL_WDT_OSC32K";
        return "HAL_WDT_LSI32K";
    }
}

// Концепция: WDT — единственный глобальный блок (hwdt.Instance = WDT,
// mik32_hal_wdt.h), без instance в имени хендла/функций — тот же приём, что
// у RTC/CRC/EEPROM. HAL_WDT_Init сама вызывает HAL_WDT_MspInit (включает
// тактирование) — тот же "безопасный" случай, что у CRC/EEPROM, не RTC.
ParseResult<PeripheralContribution> buildWdtContribution(const WdtConfig& config)
{
    PeripheralContribution contribution;
    contribution.label = "WDT";
    contribution.init_order = kInitOrderWdt;
    contribution.init_function_name = "WDT_Init";
    contribution.init_c_includes = {"mik32_hal_wdt.h", "mik32_hal_pcc.h"};
    contribution.public_prototypes = {"void " + contribution.init_function_name + "(void);"};
    contribution.global_definitions = "WDT_HandleTypeDef hwdt;\n";

    std::ostringstream init_c;
    init_c << "/*\n"
              " * WDT: clock_source=" << config.clock_source << ", reload_ms=" << config.reload_ms << ".\n"
              " * ВАЖНО: этот кодоген настраивает WDT (HAL_WDT_Init), но НЕ запускает его —\n"
              " * HAL_WDT_Start/HAL_WDT_Refresh остаются на стороне пользователя. Запуск без\n"
              " * гарантии регулярного вызова HAL_WDT_Refresh в вашем цикле приведёт к\n"
              " * безусловному циклическому сбросу платы через reload_ms миллисекунд после\n"
              " * HAL_WDT_Start (см. docs/examples/mik32-examples/HAL_WDT — тот же эффект там\n"
              " * показан намеренно, как демонстрация сброса).\n"
              " */\n"
              "void " << contribution.init_function_name << "(void)\n"
              "{\n"
              "    hwdt.Instance = WDT;\n"
              "    hwdt.Init.Clock = " << clockSourceMacro(config.clock_source) << ";\n"
              "    hwdt.Init.ReloadMs = " << config.reload_ms << ";\n"
              "\n"
              "    if (HAL_WDT_Init(&hwdt, WDT_TIMEOUT_DEFAULT) != HAL_OK)\n"
              "    {\n"
              "        /* USER TODO: обработать ошибку инициализации WDT (WDT_Init_Error). */\n"
              "    }\n"
              "}\n";
    contribution.init_function_text = init_c.str();

    return ParseResult<PeripheralContribution>{true, "", contribution};
}
