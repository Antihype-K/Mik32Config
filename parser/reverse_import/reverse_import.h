//
// Верхнеуровневая точка входа reverse-import: каталог произвольной, НЕ
// сгенерированной этим конфигуратором прошивки (пример — docs/сделать/
// mik32_fixed_v3) -> {mcu, version, peripherals:[...]} той же формы, что
// читает/пишет ProjectDocument, плюс провенанс каждого поля (нужен только
// для последующего reverse_import_apply — GUI на него не смотрит напрямую).
//
// Склеивает три уже готовых слоя: сканер (reverse_import_scanner.h) находит
// сырые блоки присваиваний, четыре типовых маппера (uart/i2c/timer32/gpio_
// reverse_mapper.h) переводят каждый в JSON-блок нужной формы, эта функция
// раскладывает результат в document["peripherals"] и собирает ВСЕ notes
// (и мапперов, и сканера) в одну сводку — цель фичи "вычленять ВСЮ
// периферию" требует, чтобы пользователь видел, что осталось нераспознанным,
// а не только то, что удалось разобрать.
//
#ifndef MIK32_CONFIGURATOR_REVERSE_IMPORT_H
#define MIK32_CONFIGURATOR_REVERSE_IMPORT_H

#include <string>
#include <vector>

#include "json.hpp"
#include "mapped_block.h"

namespace reverse_import
{

struct ImportedProject
{
    // Форма — ровно {mcu, version, peripherals:[...]}, совместимая с
    // ProjectDocument/parseProjectDocument без каких-либо изменений на их
    // стороне. mcu/version — те же дефолты, что ProjectDocument::ProjectDocument()
    // ставит для нового проекта ("MIK32"/"2.0") — при импорте эти сведения
    // просто не из чего взять точнее.
    nlohmann::json document;

    // Провенанс, параллельный document["peripherals"] по индексу: у i-го
    // блока провенанс — block_provenance[i]. Размеры массивов всегда равны.
    std::vector<std::vector<FieldProvenance>> block_provenance;

    // Все notes — от мапперов (примененные дефолты, нераспознанные значения)
    // и от сканера (unsupported_notes: кросс-файловые хендлы, многострочные
    // присваивания, неизвестные типы хендлов, каналы Timer32 и т.п.).
    std::vector<std::string> notes;

    std::vector<std::string> files_scanned;
};

// Обходит root_dir, вызывает scanDirectoryForHalHandles, диспетчеризует
// каждый RawInitBlock по handle_type в соответствующий map<Type>Block, и
// собирает результат. Ничего не пишет на диск и не трогает root_dir.
ImportedProject importProjectFromDirectory(const std::string& root_dir);

} // namespace reverse_import

#endif //MIK32_CONFIGURATOR_REVERSE_IMPORT_H
