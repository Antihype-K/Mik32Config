//
// Узкий паттерн: присваивание вида "<handle>.<field> = <local_var>;", где
// <local_var> — локальная переменная, объявленная в той же функции как
// арифметическое выражение над параметрами этой функции, а функция
// вызывается РОВНО ОДИН раз во всём файле с литеральными аргументами.
//
// Пример (docs/сделать/mik32_fixed_v3/.../main.cpp, см.
// docs/implementations/reverse_import_noise_reduction_2026-10-18.md):
//
//   void Timer32_1_Init(uint32_t clock_frequency, uint32_t timer_frequency, uint32_t pwm_frequency)
//   {
//       uint32_t prescaler = (clock_frequency / timer_frequency) - 1;
//       ...
//       htimer32_1.Clock.Prescaler = prescaler;
//   }
//   ...
//   Timer32_1_Init(32000000, 1000000, 50);
//
// Сознательно НЕ поддерживается (возвращается nullopt, не угадывается):
//  - функция вызывается больше одного раза (неоднозначно, чьи аргументы
//    брать — последнее слово за пользователем, не за молчаливым выбором);
//  - аргумент вызова сам не литерал (другая переменная, выражение);
//  - многострочная сигнатура функции или объявление переменной;
//  - переменная объявлена не как "ТИП имя = ВЫРАЖЕНИЕ;" одной строкой.
//
#ifndef MIK32_CONFIGURATOR_FUNCTION_PARAM_RESOLVER_H
#define MIK32_CONFIGURATOR_FUNCTION_PARAM_RESOLVER_H

#include <optional>
#include <string>

namespace reverse_import
{

// file_path — файл, где встретилось присваивание; variable_name — имя справа
// от "="; assignment_line — 1-based номер строки присваивания (нужен, чтобы
// найти ОБЪЕМЛЮЩУЮ функцию и не перепутать с одноимённой переменной в другой
// функции). nullopt — паттерн не подошёл (см. список ограничений выше).
std::optional<long long> resolveLocalVariableFromFunctionParams(
    const std::string& file_path,
    const std::string& variable_name,
    int assignment_line);

} // namespace reverse_import

#endif //MIK32_CONFIGURATOR_FUNCTION_PARAM_RESOLVER_H
