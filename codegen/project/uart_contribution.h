//
// Вклад UART в общий сгенерированный проект (см. концепцию в
// codegen/project/peripheral_contribution.h).
//
// Отличия от удалённого codegen/uart/:
//  - на выходе кусочки, а не три файла;
//  - хендл называется husart_<instance>, а не husart: при двух блоках uart
//    в одном проекте два глобальных "USART_HandleTypeDef husart" дали бы
//    переопределение символа на этапе линковки;
//  - появился флаг is_console (см. ниже).
//
// Синхронный режим (mode="synchronous") поддерживается с OVERNIGHT-сессии
// 12->13.10.2026, но ЗАПРЕЩЁН на instance=1 — Errata MIK32V2 v1.0.1 §10.1:
// паразитный сигнал CK на PORT2_6 без обхода (тот же пин, где на 1T Rex
// Board светодиод), см. docs/implementations/uart.md раздел 4 и
// OVERNIGHT_FIELD_REFERENCE.md. Запрет проверяется в
// parser/USART/usart_parser.cpp, не здесь.
//
#ifndef MIK32_CONFIGURATOR_UART_CONTRIBUTION_H
#define MIK32_CONFIGURATOR_UART_CONTRIBUTION_H

#include "usart_parser.h"
#include "uart_knowledge.h"
#include "parse_result.h"
#include "peripheral_contribution.h"

// is_console — этот UART назначен приёмником вывода xprintf. Ровно один UART
// в проекте может им быть, и назначает его сборщик (первый блок uart в
// конфиге). Причина: xprintf (framework-mik32v2-sdk/shared/libs/xprintf.c)
// печатает через xputc, а штатная слабая реализация xputc
// (mik32_hal_usart.c:435) жёстко привязана к UART_0 и не знает ни про
// выбранный пользователем instance, ни про наш хендл. Поэтому для консольного
// UART генерируется собственная, сильная реализация xputc — но именно одна:
// два определения xputc снова дали бы конфликт символов.
ParseResult<PeripheralContribution> buildUartContribution(
    const UartConfig& config,
    const UartKnowledge& knowledge,
    bool is_console);

#endif //MIK32_CONFIGURATOR_UART_CONTRIBUTION_H
