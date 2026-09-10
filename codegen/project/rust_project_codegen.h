//
// Rust-ветка кодогенерации — второй целевой язык внутри того же
// MIK32-Configurator (см. docs/implementations/rust_codegen.md).
//
// Концепция: в отличие от C-ветки (codegen/project/project_codegen.h),
// здесь нет отдельного слоя PeripheralContribution/knowledge_base — v1
// объём Rust-генерации (GPIO + UART + RCC, см. rust_codegen.md, «Итоговый
// v1 объём периферии») достаточно мал, чтобы вся сборка main.rs жила в
// одной функции этого модуля без риска повторения пяти генераторов, из-за
// которого в своё время появился слой PeripheralContribution для C.
// Отображение JSON-полей (mode/pull/drive_strength/data_bits/...) на
// Rust-перечисления (DriveStrength/DataBits/StopBits/Parity,
// templates/mik32_rust_hal_snapshot/hal) зашито здесь же constant-таблицами,
// а не в отдельном knowledge_base/rust_*.json — оно фиксировано устройством
// самого вендоренного HAL (какие instance/pin/enum-варианты у него вообще
// есть), а не датащитом напрямую, поэтому не нуждается в отдельном
// JSON-слое знаний, который можно было бы менять независимо от кода.
//
// Валидация "поддерживается ли эта фича в Rust вообще" —
// validation/rust_feature_validator.h, зовётся первым шагом
// generateProjectFirmwareRust ниже.
//
#ifndef MIK32_CONFIGURATOR_RUST_PROJECT_CODEGEN_H
#define MIK32_CONFIGURATOR_RUST_PROJECT_CODEGEN_H

#include <string>
#include "project_config.h"
#include "parse_result.h"
#include "codegen_result.h"
#include "json.hpp"

struct GeneratedProjectFirmwareRust
{
    std::string main_rs;
};

// emit_demo_loop — тот же смысл, что у одноимённого параметра
// generateProjectFirmware (project_codegen.h): наблюдаемая demo-логика
// (переключение выходных GPIO, периодическая строка в UART) для
// обязательной HW-проверки (CLAUDE.md, hardware validation gate). true по
// умолчанию — тот же дефолт, что у C-ветки.
ParseResult<GeneratedProjectFirmwareRust> generateProjectFirmwareRust(
    const ProjectConfig& config,
    bool emit_demo_loop = true);

// Транзакционная запись Cargo-проекта на диск — тот же контракт
// (временный каталог-брат output_project_dir, перенос только после того,
// как все файлы успешно собраны), что writeGeneratedCmakeProject
// (cmake_project_codegen.h); см. обоснование транзакционности там же —
// здесь риск частичной записи тот же (skeleton + потенциально сотни файлов
// снэпшота + возможный git clone при Network).
//
//   cargo_skeleton_dir — templates/cargo_skeleton (Cargo.toml с маркером
//     "# MIK32_CONFIGURATOR: DEPENDENCIES", .cargo/config.toml).
//   rust_hal_snapshot_dir — templates/mik32_rust_hal_snapshot (используется
//     при config.rust_hal_source_mode == Bundled ДЛЯ ВСЕХ ТРЁХ крейтов, и
//     при Network/LocalPath — только для mik32v2-hal: у него нет
//     публичного репозитория, см. VERSION.md в снэпшоте, поэтому он копи-
//     руется отсюда независимо от rust_hal_source_mode).
//   project_json_copy — сериализуется как <output_project_dir>/project.mik32,
//     тот же приём, что у writeGeneratedCmakeProject.
//
// USER CODE секции main.rs (// USER CODE BEGIN/END, codegen/
// user_code_markers_rust.h) сливаются с уже существующим на диске main.rs,
// если он там есть — тот же контракт, что main.c в C-ветке.
CodegenWriteResult writeGeneratedRustProject(
    const GeneratedProjectFirmwareRust& firmware,
    const std::string& cargo_skeleton_dir,
    const std::string& rust_hal_snapshot_dir,
    const std::string& output_project_dir,
    const ProjectConfig& config,
    const nlohmann::json& project_json_copy);

#endif //MIK32_CONFIGURATOR_RUST_PROJECT_CODEGEN_H
