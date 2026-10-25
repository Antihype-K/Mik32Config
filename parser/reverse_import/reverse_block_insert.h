//
// Вставка НОВОГО периферийного блока (добавленного в GUI уже ПОСЛЕ
// reverse-import) в существующие исходники точечной правкой — без полной
// кодогенерации. Дополняет reverse_import_apply.h: тот умеет только менять
// значение в уже существующей строке (один блок 1:1 с моментом импорта),
// этот — добавляет совсем новые строки для блока, которого при импорте не
// было вовсе.
//
// Поддержаны только блоки с простой сигнатурой build<Type>Contribution(config,
// knowledge) — i2c/spi/timer16/timer32/dac. GPIO и USART сознательно
// исключены: их contribution-функции берут на себя кросс-блочную логику
// (группировка GPIO одного instance по нескольким JSON-блокам, выбор ЕДИНСТВЕННОГО
// UART-консоли для xputc) — вызывать их для одного блока в отрыве от всего
// проекта означало бы либо дублировать эту логику здесь, либо рисковать
// вставить код, конфликтующий с остальными блоками того же типа. Для
// gpio/uart новый блок по-прежнему требует полной кодогенерации
// ("Перегенерировать проект").
//
#ifndef MIK32_CONFIGURATOR_REVERSE_BLOCK_INSERT_H
#define MIK32_CONFIGURATOR_REVERSE_BLOCK_INSERT_H

#include <string>
#include <vector>

#include "json.hpp"
#include "project_codegen.h"

namespace reverse_import
{

struct BlockInsertResult
{
    bool success = false;
    std::string error;
};

// root_dir/files_scanned — тот же каталог и список файлов, что вернул
// importProjectFromDirectory (reverse_import.h). current_document — весь
// текущий документ GUI (document_.document()), уже включающий новый блок.
// block_index — индекс нового блока в current_document["peripherals"].
BlockInsertResult insertNewPeripheralBlock(
    const std::string& root_dir,
    const std::vector<std::string>& files_scanned,
    const nlohmann::json& current_document,
    int block_index,
    const ProjectKnowledge& knowledge);

} // namespace reverse_import

#endif //MIK32_CONFIGURATOR_REVERSE_BLOCK_INSERT_H
