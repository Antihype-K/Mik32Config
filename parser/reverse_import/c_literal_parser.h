//
// Разбор целочисленного литерала C для reverse-mapper'ов.
//
// Общий хелпер вместо копии в каждом маппере: копии ломались на разных
// классах литералов. `std::stoll("0xFFFF")` без базы молча возвращает 0
// (останавливается на 'x', без исключения), а отбрасывание C-суффикса по
// признаку "последний символ — буква" съедает hex-цифры A..F ("0x1F" ->
// "0x1" -> тихо неверное 1).
//
#ifndef MIK32_CONFIGURATOR_C_LITERAL_PARSER_H
#define MIK32_CONFIGURATOR_C_LITERAL_PARSER_H

#include <optional>
#include <string>

namespace reverse_import
{

// Один целочисленный литерал ЦЕЛИКОМ: десятичный/0x/0b/восьмеричный, со
// знаком, с суффиксом u/U/l/L, в окружении пробелов.
//
// nullopt, если это не литерал целиком — переменная/макрос ("htimer32.Top",
// "SPI_THRESHOLD_DEFAULT"), мусор. Вызывающий обязан выдать note, а не
// подставить молчаливый 0.
//
// Дополнительно понимает ОДНО бинарное выражение "литерал OP литерал" для
// "/", ">>", "<<" ("0xFFFF / 2", "7544 >> 1") — обе стороны должны быть
// литералами целиком, иначе nullopt. Приоритета операций и нескольких
// операторов нет — это осознанно вне скоупа, см. shortcut-комментарий в .cpp.
std::optional<long long> parseCIntegerLiteral(const std::string& text);

} // namespace reverse_import

#endif //MIK32_CONFIGURATOR_C_LITERAL_PARSER_H
