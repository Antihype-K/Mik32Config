//
// Маппер reverse-import для Bus Watchdog. Как и PVD (см. pvd_reverse_mapper.h),
// у Bus Watchdog нет HAL-обёртки инициализации и нет HandleTypeDef
// (AUDIT_TZ_PERIPHERY_GAPS.md, Тир 3) — конфигурация это прямая запись в
// регистры WDT_BUS->TIMEOUT/->ENABLE, см.
// codegen/project/bus_watchdog_contribution.cpp — единственный известный
// проекту шаблон (вендорного примера в docs/examples/ нет, искали и не
// нашли). Поэтому здесь НЕТ RawInitBlock — функция сама сканирует исходный
// текст файла в поиске этого конкретного паттерна.
//
#ifndef MIK32_CONFIGURATOR_BUS_WATCHDOG_REVERSE_MAPPER_H
#define MIK32_CONFIGURATOR_BUS_WATCHDOG_REVERSE_MAPPER_H

#include <optional>
#include <string>

#include "mapped_block.h"

namespace reverse_import
{

// Сканирует содержимое ОДНОГО файла (переданное как текст, а не путь — для
// тестируемости без временных файлов) на предмет паттерна Bus Watchdog.
// nullopt, если паттерн не найден вовсе (это нормально — Bus Watchdog не
// обязателен в проекте).
std::optional<MappedBlock> scanBusWatchdogBlockFromFileContent(
    const std::string& file_content, const std::string& file_path);

// Обратная сериализация для точечной правки. Поддерживает только "timeout"
// (единственное поле с literal-provenance с location) — enabled-флаги
// кодируются вставкой/удалением целой строки, не литералом, и не
// поддерживаются (см. .cpp).
std::optional<std::string> serializeBusWatchdogFieldValue(
    const std::string& field_name,
    const nlohmann::json& new_value,
    const std::string& original_raw_value_text);

} // namespace reverse_import

#endif //MIK32_CONFIGURATOR_BUS_WATCHDOG_REVERSE_MAPPER_H
