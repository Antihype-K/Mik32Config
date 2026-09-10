//
// То же самое /* USER CODE BEGIN <ID> */ ... /* USER CODE END <ID> */
// слияние (codegen/user_code_markers.h, ТЗ §3.1.7.3), но с комментариями
// в стиле Rust ("//", а не "/* */") — так, как реально пишет rustfmt/сам
// язык. Отдельная функция, а не параметризация существующей: у Rust нет
// блочных комментариев в таком же ходовом стиле, а regex у оригинала
// жёстко зашит на "/* ... */" — проще независимая копия того же алгоритма
// с другим разделителем, чем добавлять enum CommentStyle в уже
// протестированный C-путь (см. docs/implementations/rust_codegen.md).
//
#ifndef MIK32_CONFIGURATOR_USER_CODE_MARKERS_RUST_H
#define MIK32_CONFIGURATOR_USER_CODE_MARKERS_RUST_H

#include <string>

std::string mergeUserCodeSectionsRust(
    const std::string& newly_generated_content,
    const std::string& existing_file_content);

#endif //MIK32_CONFIGURATOR_USER_CODE_MARKERS_RUST_H
