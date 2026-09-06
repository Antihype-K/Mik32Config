//
// Кодогенератор C-инициализации для периферии UART (по образцу
// codegen/gpio/gpio_codegen.h). HAL-based, только Asynchronous_Mode —
// синхронный режим и связанная с ним errata по CK-сигналу (§10.1) вне
// скоупа этой сессии, см. docs/implementations/uart.md.
//
#ifndef MIK32_CONFIGURATOR_UART_CODEGEN_H
#define MIK32_CONFIGURATOR_UART_CODEGEN_H

#include <string>
#include "usart_parser.h"
#include "uart_knowledge.h"
#include "parse_result.h"
#include "codegen_result.h"

struct GeneratedUartFirmware
{
    std::string mik32_init_h;
    std::string mik32_init_c;
    std::string main_c;
};

ParseResult<GeneratedUartFirmware> generateUartFirmware(
    const UartConfig& config,
    const UartKnowledge& knowledge);

// Идентичная семантика writeGeneratedGpioProject (codegen/gpio/gpio_codegen.h):
// копирует скелет один раз, затем перезаписывает только src/.
CodegenWriteResult writeGeneratedUartProject(
    const GeneratedUartFirmware& firmware,
    const std::string& skeleton_dir,
    const std::string& output_project_dir);

#endif //MIK32_CONFIGURATOR_UART_CODEGEN_H
