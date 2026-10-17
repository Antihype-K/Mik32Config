#include "pvd_contribution.h"
#include "contribution_utils.h"
#include <sstream>

// Концепция: PVD — единственный глобальный блок (PVD_CONTROL, pvd_control.h),
// без HAL-обёртки инициализации (AUDIT_TZ_PERIPHERY_GAPS.md, Тир 3) — только
// макрос включения тактирования __HAL_PCC_PVD_CONTROL_CLK_ENABLE. Биты
// CONFIG.NRESETU/NRESETO (сброс платы по порогу) кодоген не трогает — см.
// _comment_no_reset_fields в schemas/pvd.json и docs/implementations/pvd.md.
ParseResult<PeripheralContribution> buildPvdContribution(const PvdConfig& config)
{
    PeripheralContribution contribution;
    contribution.label = "PVD";
    contribution.init_order = kInitOrderPvd;
    contribution.init_function_name = "PVD_Init";
    contribution.init_c_includes = {"pvd_control.h", "mik32_hal_pcc.h"};
    contribution.public_prototypes = {"void " + contribution.init_function_name + "(void);"};

    std::ostringstream init_c;
    init_c << "/*\n"
              " * PVD (монитор напряжения VCC/AVCC): under_threshold=" << config.under_threshold
           << ", over_threshold=" << config.over_threshold
           << ", digital_filter_value=" << config.digital_filter_value << ".\n"
              " * Коды порогов — сырые значения регистра CONFIG.UNDER_THRESH/OVER_THRESH\n"
              " * (shared/periphery/pvd_control.h), таблица код->милливольты не подтверждена\n"
              " * ни в одном доступном материале проекта, поэтому не переведена в милливольты.\n"
              " *\n"
              " * ВАЖНО: CONFIG.NRESETU/NRESETO (сброс платы по порогу напряжения) этот\n"
              " * кодоген НЕ настраивает — полярность этих битов не подтверждена ни в HAL,\n"
              " * ни в примерах (см. docs/implementations/pvd.md). Значение после сброса\n"
              " * остаётся как есть. Если нужен сброс по порогу — впишите биты вручную\n"
              " * после HAL_Init(), предварительно сверив полярность с датащитом/errata.\n"
              " *\n"
              " * Чтение срабатывания — PVD_CONTROL->STATUS (UNDER_VALUE/OVER_VALUE/UNDER_FLAG/\n"
              " * OVER_FLAG) и/или прерывания EPIC (HAL_EPIC_PVD_VCC_UNDER и т.п.,\n"
              " * mik32_hal_irq.h) — не настраиваются здесь, см. AUDIT_TZ_PERIPHERY_GAPS.md.\n"
              " */\n"
              "void " << contribution.init_function_name << "(void)\n"
              "{\n"
              "    __HAL_PCC_PVD_CONTROL_CLK_ENABLE();\n"
              "\n"
              "    PVD_CONTROL->DPF_VALUE = " << config.digital_filter_value << ";\n"
              "\n"
              "    uint32_t pvd_config = PVD_CONTROL->CONFIG;\n"
              "    pvd_config &= ~(PVD_CONFIG_UNDER_THRESH_M | PVD_CONFIG_OVER_THRESH_M | PVD_CONFIG_EN_VREFCLB_M);\n"
              "    pvd_config |= (((uint32_t)" << config.under_threshold << " << PVD_CONFIG_UNDER_THRESH_S) & PVD_CONFIG_UNDER_THRESH_M);\n"
              "    pvd_config |= (((uint32_t)" << config.over_threshold << " << PVD_CONFIG_OVER_THRESH_S) & PVD_CONFIG_OVER_THRESH_M);\n";
    if (config.enable_reference_calibration)
    {
        init_c << "    pvd_config |= PVD_CONFIG_EN_VREFCLB_M;\n";
    }
    init_c << "    PVD_CONTROL->CONFIG = pvd_config;\n"
              "}\n";
    contribution.init_function_text = init_c.str();

    return ParseResult<PeripheralContribution>{true, "", contribution};
}
