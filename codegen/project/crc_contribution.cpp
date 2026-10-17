#include "crc_contribution.h"
#include "contribution_utils.h"
#include <sstream>

// Концепция: CRC — единственный глобальный блок (hcrc.Instance = CRC,
// mik32_memory_map.h), без instance в имени хендла/функций — тот же приём,
// что у RTC. HAL_CRC_Init сама вызывает HAL_CRC32_MspInit (включает
// тактирование CRC) — в отличие от RTC, здесь нет отдельного "забытого"
// вызова клок-энейбла, кодогену достаточно одного HAL_CRC_Init.
ParseResult<PeripheralContribution> buildCrcContribution(const CrcConfig& config)
{
    PeripheralContribution contribution;
    contribution.label = "CRC";
    contribution.init_order = kInitOrderCrc;
    contribution.init_function_name = "CRC_Init";
    contribution.init_c_includes = {"mik32_hal_crc32.h", "mik32_hal_pcc.h"};
    contribution.public_prototypes = {"void " + contribution.init_function_name + "(void);"};
    contribution.global_definitions = "CRC_HandleTypeDef hcrc;\n";

    std::ostringstream init_c;
    init_c << "/*\n"
              " * CRC: poly=0x" << std::hex << config.poly << ", init=0x" << config.init << std::dec << ",\n"
              " * input_reverse=" << (config.input_reverse ? "true" : "false")
           << ", output_reverse=" << (config.output_reverse ? "true" : "false")
           << ", output_inversion=" << (config.output_inversion ? "true" : "false") << ".\n";
    if (!config.requested_preset.empty())
    {
        init_c << " * Запрошен preset=\"" << config.requested_preset
               << "\" (mik32_hal_crc32.h, строки 240-283) — значения выше подобраны автоматически по нему.\n";
    }
    init_c << " * Только конфигурация алгоритма — HAL_CRC_WriteData/WriteData32/ReadCRC\n"
              " * на собственных данных остаются за пользователем, этот кодоген не может\n"
              " * угадать их наперёд (см. docs/implementations/crc.md).\n"
              " */\n"
              "void " << contribution.init_function_name << "(void)\n"
              "{\n"
              "    hcrc.Instance = CRC;\n"
              "\n"
              "    hcrc.Poly = 0x" << std::hex << config.poly << ";\n"
              "    hcrc.Init = 0x" << config.init << std::dec << ";\n"
              "    hcrc.InputReverse = " << (config.input_reverse ? "CRC_REFIN_TRUE" : "CRC_REFIN_FALSE") << ";\n"
              "    hcrc.OutputReverse = " << (config.output_reverse ? "CRC_REFOUT_TRUE" : "CRC_REFOUT_FALSE") << ";\n"
              "    hcrc.OutputInversion = "
           << (config.output_inversion ? "CRC_OUTPUTINVERSION_ON" : "CRC_OUTPUTINVERSION_OFF") << ";\n"
              "\n"
              "    HAL_CRC_Init(&hcrc);\n"
              "}\n";
    contribution.init_function_text = init_c.str();

    return ParseResult<PeripheralContribution>{true, "", contribution};
}
