//
// Вклад I2C в общий сгенерированный проект (см. концепцию в
// codegen/project/peripheral_contribution.h).
//
// Отличия от удалённого codegen/i2c/:
//  - на выходе кусочки, а не три файла;
//  - хендл называется hi2c_<instance>, функции — I2C_<instance>_Init /
//    I2C_<instance>_Reset: в МИК32 два блока I2C, и оба могут быть в одном
//    конфиге;
//  - логика UART здесь больше НЕ дублируется. В старом I2C-генераторе лежала
//    третья независимая копия построения USART_HandleTypeDef (первая — в
//    генераторе UART, вторая — в GPIO+UART-сшивке), потому что он обязан был
//    выпустить main.c целиком, вместе с UART_Init. В схеме вкладов UART
//    настраивает себя сам — I2C только сообщает, что ему нужен консольный
//    вывод (флаг console_available).
//
// ТЗ по I2C молчит (см. docs/implementations/i2c.md); источники — черновик
// архитектуры §5.4 и реальный HAL.
//
#ifndef MIK32_CONFIGURATOR_I2C_CONTRIBUTION_H
#define MIK32_CONFIGURATOR_I2C_CONTRIBUTION_H

#include "i2c_parser.h"
#include "i2c_knowledge.h"
#include "parse_result.h"
#include "peripheral_contribution.h"

// console_available — в проекте есть UART, назначенный консолью xprintf
// (см. buildUartContribution, параметр is_console). От него зависит:
//  - demo="scanner" при mode="master" БЕЗ консоли — ошибка: сканер, который
//    не может напечатать найденные адреса, бесполезен, и генерировать немой
//    сканер молча нельзя;
//  - в остальных сценариях печать просто не добавляется в тело цикла.
//
// emit_demo_loop — общий флаг проекта. В отличие от GPIO/UART, где демо
// придумано кодогеном, у I2C сценарий задан пользователем в схеме
// (schemas/i2c.json, поле "demo"), поэтому при emit_demo_loop=false I2C
// генерирует только инициализацию, а сценарий не выполняется вообще.
ParseResult<PeripheralContribution> buildI2cContribution(
    const I2cConfig& config,
    const I2cKnowledge& knowledge,
    bool console_available,
    bool emit_demo_loop);

#endif //MIK32_CONFIGURATOR_I2C_CONTRIBUTION_H
