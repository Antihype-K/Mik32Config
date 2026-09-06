#ifndef MIK32_CONFIGURATOR_CODEGEN_RESULT_H
#define MIK32_CONFIGURATOR_CODEGEN_RESULT_H
#include <string>

// Общий результат "запись сгенерированного проекта на диск" — используется
// и codegen/gpio, и codegen/uart (см. writeGeneratedGpioProject /
// writeGeneratedUartProject), поэтому вынесен сюда, а не продублирован в
// каждом codegen/<peripheral>/*.h по отдельности.
struct CodegenWriteResult
{
    bool success;
    std::string error;
};

#endif //MIK32_CONFIGURATOR_CODEGEN_RESULT_H
