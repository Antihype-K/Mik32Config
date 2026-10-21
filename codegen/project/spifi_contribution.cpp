#include "spifi_contribution.h"
#include "contribution_utils.h"
#include <sstream>

// Концепция: SPIFI — единственный глобальный блок (hspifi.Instance =
// SPIFI_CONFIG, mik32_memory_map.h), без instance в имени хендла/функций —
// тот же приём, что у CRC/RTC. HAL_SPIFI_MemoryMode_Init сама вызывает
// HAL_SPIFI_MspInit (включает тактирование SPIFI и настраивает выводы под
// serial-режим, mik32_hal_spifi.c:3-17/19-21) — как и у CRC, не нужен
// отдельный "забытый" вызов клок-энейбла.
ParseResult<PeripheralContribution> buildSpifiContribution(const SpifiConfig& config)
{
    PeripheralContribution contribution;
    contribution.label = "SPIFI";
    contribution.init_order = kInitOrderSpifi;
    contribution.init_function_name = "SPIFI_Init";
    contribution.init_c_includes = {"mik32_hal_spifi.h"};
    contribution.public_prototypes = {"void " + contribution.init_function_name + "(void);"};
    contribution.global_definitions = "SPIFI_MemoryModeConfig_HandleTypeDef hspifi;\n";

    std::ostringstream init_c;
    init_c << "/*\n"
              " * SPIFI (Memory Mode): cache_enabled=" << (config.cache_enabled ? "true" : "false")
           << ", cache_limit=0x" << std::hex << config.cache_limit << std::dec << ",\n"
              " * cmd_frame_form=\"" << config.cmd_frame_form << "\", cmd_field_form=\"" << config.cmd_field_form
           << "\", cmd_op_code=0x" << std::hex << static_cast<int>(config.cmd_op_code) << std::dec
           << ", cmd_interim_length=" << static_cast<int>(config.cmd_interim_length) << ".\n"
              " * Только режим работы с памятью — периферийный режим (HAL_SPIFI_SendCommand\n"
              " * на произвольных командах/адресах МФП) остаётся за пользователем, этот\n"
              " * кодоген не может угадать последовательность команд конкретной флеш-\n"
              " * микросхемы наперёд (см. docs/implementations/spifi.md).\n"
              " */\n"
              "void " << contribution.init_function_name << "(void)\n"
              "{\n"
              "    hspifi.Instance = SPIFI_CONFIG;\n"
              "\n"
              "    hspifi.CacheEnable = " << (config.cache_enabled ? "SPIFI_CACHE_ENABLE" : "SPIFI_CACHE_DISABLE") << ";\n"
              "    hspifi.CacheLimit = 0x" << std::hex << config.cache_limit << std::dec << ";\n"
              "\n"
              "    hspifi.Command.InterimLength = " << static_cast<int>(config.cmd_interim_length) << ";\n"
              "    hspifi.Command.FieldForm = " << spifiFieldFormMacro(config.cmd_field_form) << ";\n"
              "    hspifi.Command.FrameForm = " << spifiFrameFormMacro(config.cmd_frame_form) << ";\n"
              "    hspifi.Command.OpCode = 0x" << std::hex << static_cast<int>(config.cmd_op_code) << std::dec << ";\n"
              "\n"
              "    HAL_SPIFI_MemoryMode_Init(&hspifi);\n"
              "}\n";
    contribution.init_function_text = init_c.str();

    return ParseResult<PeripheralContribution>{true, "", contribution};
}
