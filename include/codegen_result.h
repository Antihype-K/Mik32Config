#ifndef MIK32_CONFIGURATOR_CODEGEN_RESULT_H
#define MIK32_CONFIGURATOR_CODEGEN_RESULT_H
#include <string>

// Общий результат "запись сгенерированного проекта на диск". Вынесен в
// include/, а не в заголовок конкретного модуля, ещё со времён пяти
// однопериферийных генераторов; сейчас его использует единственный
// writeGeneratedProject (codegen/project/project_codegen.h).
struct CodegenWriteResult
{
    bool success;
    std::string error;
};

#endif //MIK32_CONFIGURATOR_CODEGEN_RESULT_H
