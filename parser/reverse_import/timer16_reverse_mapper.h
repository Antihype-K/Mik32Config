//
// Типовой маппер reverse-import для Timer16: превращает один RawInitBlock со
// handle_type == "Timer16_HandleTypeDef" (см. reverse_import_scanner.h) в
// MappedBlock формы schemas/timer16.json.
//
// ОСОБЕННОСТЬ, из-за которой этого маппера не было в первой версии фичи:
// mode/arr/cmp у Timer16 — это НЕ присваивания полей структуры, а АРГУМЕНТЫ
// вызова HAL_Timer16_StartPWM/StartOneShot/Counter_Start ПОСЛЕ HAL_Timer16_Init
// (mik32_hal_timer16.h: сам HAL_Timer16_Init их не принимает). Именно эти
// вызовы и есть RawInitBlock::trailing_calls (добавлены в сканер специально
// для этого маппера, см. reverse_import_scanner.h). instance/prescaler,
// наоборот, обычные присваивания полей — как у остальных типов.
//
// Из-за этого mode/arr/cmp ВСЕГДА read-only (editable=false) в этой версии:
// reverse_patch_writer переписывает только строки вида "поле = значение;"
// (см. его .cpp, tryApplyEdit требует '=' в строке) — у вызова
// "HAL_Timer16_StartPWM(&htimer16_0, 999, 499);" знака '=' нет вообще,
// патчер не сможет туда написать. Показывать эти поля в GUI editable=true,
// а потом отказывать при "Сгенерировать код", было бы хуже, чем честно
// показать их нередактируемыми сразу — см. mapped_block.h.
//
#ifndef MIK32_CONFIGURATOR_TIMER16_REVERSE_MAPPER_H
#define MIK32_CONFIGURATOR_TIMER16_REVERSE_MAPPER_H

#include <optional>
#include <string>

#include "json.hpp"
#include "mapped_block.h"
#include "reverse_import_scanner.h"

namespace reverse_import
{

// Разбирает один Init-блок Timer16_HandleTypeDef. Возвращает nullopt только
// если block.handle_type не Timer16_HandleTypeDef. Во всех остальных
// случаях, включая отсутствие узнаваемого HAL_Timer16_Start*-вызова среди
// trailing_calls, возвращает MappedBlock с notes, объясняющими заглушки.
std::optional<MappedBlock> mapTimer16Block(const RawInitBlock& block);

// "instance"/"prescaler" — обычные литералы, поддержаны для редактирования.
// "mode"/"arr"/"cmp" — всегда nullopt (см. объяснение выше про '=').
std::optional<std::string> serializeTimer16FieldValue(
    const std::string& field_name,
    const nlohmann::json& new_value,
    const std::string& original_raw_value_text);

} // namespace reverse_import

#endif //MIK32_CONFIGURATOR_TIMER16_REVERSE_MAPPER_H
