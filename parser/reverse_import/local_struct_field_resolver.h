//
// resolveLocalStructFields — узкий паттерн для периферии без присваиваний
// полей на самом хендле (RTC: конфигурация передаётся через локальные
// структуры-значения RTC_TimeTypeDef/RTC_DateTypeDef/RTC_AlarmTypeDef,
// переданные по указателю в HAL_RTC_SetTime/SetDate/SetAlarm — см.
// docs/examples/mik32-examples/Hal_RTC/src/main.c:106-142). Сканер
// (reverse_import_scanner.h) группирует присваивания ТОЛЬКО для хендлов
// типа *HandleTypeDef/*InitTypeDef — локальные value-структуры в эту
// группировку не попадают, поэтому их поля ищутся отдельно, по имени
// переменной, во всём файле (как и остальные reverse-import хелперы —
// без учёта области видимости функции, см. ограничения scanner.h).
//
#ifndef MIK32_CONFIGURATOR_LOCAL_STRUCT_FIELD_RESOLVER_H
#define MIK32_CONFIGURATOR_LOCAL_STRUCT_FIELD_RESOLVER_H

#include <string>
#include <vector>

#include "reverse_import_scanner.h"

namespace reverse_import
{

// Находит все "<variable_name>.<field_path> = <value>;" в файле file_path,
// в порядке появления. Многострочные присваивания (без ';' на строке) и
// многострочные /* */-комментарии не поддерживаются — тот же набор
// ограничений, что у основного сканера.
std::vector<RawFieldAssignment> resolveLocalStructFields(
    const std::string& file_path, const std::string& variable_name);

} // namespace reverse_import

#endif //MIK32_CONFIGURATOR_LOCAL_STRUCT_FIELD_RESOLVER_H
