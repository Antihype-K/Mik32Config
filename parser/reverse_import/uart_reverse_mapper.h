//
// Типовой маппер reverse-import для UART: превращает RawInitBlock, собранный
// reverse_import_scanner.h из "husart0.<field> = <value>;"-присваиваний
// вокруг HAL_USART_Init, в JSON-блок формы schemas/uart.json — см.
// mapped_block.h за контрактом MappedBlock/FieldProvenance.
//
// Контракт с вызывающей стороной (GUI/оркестратор): mapUartBlock никогда не
// "выбрасывает" периферию молча — если что-то не распознано, поле получает
// консервативный дефолт и честную запись в MappedBlock::notes, а не
// отбрасывается и не подменяется угаданным значением.
//
#ifndef MIK32_CONFIGURATOR_UART_REVERSE_MAPPER_H
#define MIK32_CONFIGURATOR_UART_REVERSE_MAPPER_H

#include <optional>
#include <string>

#include "json.hpp"
#include "mapped_block.h"
#include "reverse_import_scanner.h"

namespace reverse_import
{

// Разбирает один RawInitBlock (block.handle_type должен быть
// "USART_HandleTypeDef", иначе nullopt) в MappedBlock с json_block по форме
// schemas/uart.json. instance не определён -> instance=0 + notes (блок всё
// равно возвращается, а не nullopt — nullopt здесь означает только "это
// вообще не про USART").
std::optional<MappedBlock> mapUartBlock(const RawInitBlock& block);

// Строит C-текст правой части присваивания для ОДНОГО конкретного
// provenance-поля (см. MappedBlock::provenance) при редактировании в GUI.
// field_name — имя JSON-поля схемы ("baud_rate", "data_bits", ...).
// new_value — новое значение, введённое пользователем.
// original_raw_value_text — текущий текст ИМЕННО ТОЙ строки, для которой
// вызывается сериализация (нужен, когда одно json-поле физически
// представлено несколькими независимыми C-строками — см. .cpp, разбор
// "parity" и "direction").
//
// nullopt означает: для этой КОНКРЕТНОЙ комбинации (это поле, это значение,
// эта исходная строка) безопасно построить текст нельзя — вызывающая
// сторона не должна писать патч для этой строки (см. подробное объяснение
// в .cpp у serializeUartFieldValue).
std::optional<std::string> serializeUartFieldValue(
    const std::string& field_name,
    const nlohmann::json& new_value,
    const std::string& original_raw_value_text);

} // namespace reverse_import

#endif //MIK32_CONFIGURATOR_UART_REVERSE_MAPPER_H
