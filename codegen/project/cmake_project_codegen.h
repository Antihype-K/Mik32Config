//
// Второй, независимый способ упаковки сгенерированного проекта — CMake
// вместо PlatformIO (codegen/project/project_codegen.h остаётся дефолтным
// путём и не меняется вообще, ни строкой — см. обоснование ниже).
//
// Концепция. project_codegen.cpp уже умеет сшивать вклады периферии
// (PeripheralContribution) в единственные mik32_init.h/.c/main.c и знает,
// как их читать/писать (win_file_io.h) и как сохранять USER CODE секции
// между регенерациями. Всё это переиспользуется как есть: этот модуль сначала
// зовёт generateProjectFirmware/writeGeneratedProject-совместимую логику, а
// затем либо просто переупаковывает результат под другое имя набора файлов
// (per_type_files == false), либо перестраивает вклады периферии заново по
// тем же build<X>Contribution функциям, что и project_codegen.cpp, но сшивает
// их в отдельные файлы НА ТИП периферии, а не в один общий (per_type_files ==
// true). Второй путь неизбежно дублирует ~60 строк «кто из config.*_blocks
// какой build*Contribution зовёт» из project_codegen.cpp — альтернатива была
// бы либо экспортировать это как отдельную функцию из project_codegen.cpp
// (что нарушило бы требование «не трогать этот файл вообще»), либо
// переписывать сами *_contribution.cpp (что нарушило бы «не переписывать
// build<X>Contribution заново»). Дублирование ~60 строк итерации — меньшее
// зло, чем любой из этих вариантов.
//
// Почему PlatformIO-путь заморожен буквально. project_codegen.cpp содержит
// HW-подтверждённые golden-фикстуры (docs/implementations/gpio_uart_demo.md,
// dma.md) — малейшее изменение форматирования там ломает побайтовое
// сравнение с уже провалидированным на 1T Rex Board выводом. CMake — новый,
// ещё НЕ провалидированный на железе путь (см. docs/implementations/
// cmake_toolchain.md, раздел «Обязательная аппаратная проверка»), поэтому он
// живёт полностью отдельно и ничем не рискует существующим.
//
#ifndef MIK32_CONFIGURATOR_CMAKE_PROJECT_CODEGEN_H
#define MIK32_CONFIGURATOR_CMAKE_PROJECT_CODEGEN_H

#include <string>
#include <vector>
#include "project_config.h"
#include "project_codegen.h"
#include "parse_result.h"
#include "codegen_result.h"
#include "json.hpp"

// Один сгенерированный файл инициализации, путь — относительно <project>/src/.
struct GeneratedFile
{
    std::string relative_path;
    std::string content;
};

struct GeneratedProjectFirmwareCmake
{
    std::vector<GeneratedFile> init_files;
    std::string main_c;
};

// Спецификация этой сессии буквально просила сигнатуру без ParseResult
// (`GeneratedProjectFirmwareCmake generateProjectFirmwareCmake(...)`), но
// внутри эта функция обязана звать уже существующий generateProjectFirmware
// (project_codegen.h), который может завершиться ошибкой (например, конфликт
// выводов между блоками разных типов). Сигнатура без возможности сообщить об
// ошибке заставила бы либо тихо
// возвращать пустую структуру на ошибке (пользователь получил бы "проект
// сгенерирован" с нерабочей прошивкой без единого сообщения — ровно то, что
// запрещено комментарием в project_config.h про недопустимые типы периферии),
// либо бросать исключение (нарушает конвенцию всего остального кодогена —
// ParseResult<T> используется буквально везде, include/parse_result.h).
// Поэтому сознательное расхождение со спецификацией: возврат обёрнут в
// ParseResult<T>, как и generateProjectFirmware. Задокументировано в
// docs/implementations/cmake_toolchain.md.
ParseResult<GeneratedProjectFirmwareCmake> generateProjectFirmwareCmake(
    const ProjectConfig& config,
    const ProjectKnowledge& knowledge);

// Транзакционная запись CMake-варианта проекта на диск (ТЗ §3.2.2).
//
// Контракт транзакционности: если ЛЮБОЙ шаг ниже завершается ошибкой,
// output_project_dir не меняется НИ НА ОДИН БАЙТ (весь промежуточный
// результат живёт во временном каталоге-брате output_project_dir и
// удаляется целиком при неудаче). Это отличает данную функцию от
// writeGeneratedProject (project_codegen.h), которая пишет прямо в
// output_project_dir и полагается на то, что путь записи файлов настолько
// прост (три файла без внешних зависимостей вроде git clone), что риск
// частичной записи в существующей практике не был сочтён достаточным поводом
// для транзакции. Здесь запись многошаговая (skeleton + HAL-исходники,
// потенциально сотни файлов, потенциально сетевой git clone) — риск падения
// на середине уже реален, поэтому транзакция обязательна.
//
//   cmake_skeleton_dir — templates/cmake_skeleton (CMakeLists.txt с маркерами
//     "# MIK32_CONFIGURATOR: SOURCES" / "# MIK32_CONFIGURATOR: INCLUDE_DIRS",
//     cmake/mik32-riscv-toolchain.cmake).
//   sdk_snapshot_dir — templates/mik32_sdk_snapshot (используется только при
//     config.hal_source_mode == Bundled; для Network/LocalPath игнорируется).
//   project_json_copy — сериализуется как <output_project_dir>/project.mik32
//     (nlohmann::json::dump(4), по образцу ProjectDocument::save в gui/).
//
// Известные, осознанно принятые ограничения (подробности и обоснование —
// docs/implementations/cmake_toolchain.md):
//  - Network-режим (git clone) не имеет offline-фолбэка — это единственный
//    из трёх hal_source_mode, которому требуется сеть, ровно по этой причине
//    он не дефолтный (Bundled — дефолт).
//  - Полная атомарность гарантирована только при ПЕРВОЙ генерации в чистый
//    (ещё не существующий) output_project_dir — тогда перенос сводится к
//    одному fs::rename каталога целиком. При повторной генерации в уже
//    существующий каталог перенос идёт файл за файлом (ОС не даёт
//    кроссплатформенного atomic rename каталога поверх непустого каталога);
//    риск частичной записи на этом шаге сведён к минимуму (все проверки уже
//    пройдены во временном каталоге до начала переноса), но не равен нулю.
CodegenWriteResult writeGeneratedCmakeProject(
    const GeneratedProjectFirmwareCmake& firmware,
    const std::string& cmake_skeleton_dir,
    const std::string& sdk_snapshot_dir,
    const std::string& output_project_dir,
    const ProjectConfig& config,
    const nlohmann::json& project_json_copy);

#endif //MIK32_CONFIGURATOR_CMAKE_PROJECT_CODEGEN_H
