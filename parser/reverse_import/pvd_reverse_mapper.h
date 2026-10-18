//
// Маппер reverse-import для PVD (монитор напряжения VCC/AVCC). В отличие от
// всех остальных периферийных блоков, у PVD нет HAL-обёртки инициализации и
// нет HandleTypeDef (AUDIT_TZ_PERIPHERY_GAPS.md, Тир 3) — конфигурация это
// прямой регистровый RMW над PVD_CONTROL->CONFIG битовыми масками
// (PVD_CONFIG_UNDER_THRESH_M/_S, PVD_CONFIG_OVER_THRESH_M/_S,
// PVD_CONFIG_EN_VREFCLB_M), см. codegen/project/pvd_contribution.cpp —
// единственный известный проекту шаблон этого кода (вендорного примера PVD
// в docs/examples/ нет, искали и не нашли). Поэтому, в отличие от всех
// остальных мапперов, здесь НЕТ RawInitBlock — функция сама сканирует
// исходный текст файла в поиске этого конкретного паттерна.
//
// CONFIG.NRESETU/NRESETO (сброс платы по порогу) сознательно не читаются
// и не отражаются в JSON — полярность этих битов нигде не подтверждена
// (см. docs/implementations/pvd.md, feedback-память
// no_invented_register_bit_polarity), поэтому их текущее значение в
// исходнике, какое бы оно ни было, не является частью конфигурации в
// понимании этого конфигуратора.
//
#ifndef MIK32_CONFIGURATOR_PVD_REVERSE_MAPPER_H
#define MIK32_CONFIGURATOR_PVD_REVERSE_MAPPER_H

#include <optional>
#include <string>

#include "mapped_block.h"

namespace reverse_import
{

// Сканирует содержимое ОДНОГО файла (переданное как текст, а не путь — для
// тестируемости без временных файлов) на предмет PVD-паттерна. nullopt, если
// паттерн не найден вовсе (это нормально — PVD не обязателен в проекте).
std::optional<MappedBlock> scanPvdBlockFromFileContent(
    const std::string& file_content, const std::string& file_path);

} // namespace reverse_import

#endif //MIK32_CONFIGURATOR_PVD_REVERSE_MAPPER_H
