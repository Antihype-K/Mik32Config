//
// Мелкие утилиты, общие для всех codegen/project/*_contribution.cpp.
//
// Почему они здесь, а не продублированы в каждом модуле. В удалённых
// однопериферийных генераторах findOrNull был скопирован пять раз подряд —
// каждый из них объявлялся самодостаточным и не тянул detail-функции соседа.
// Для них это было оправдано (независимы и заменяемы поодиночке), но вклады
// по определению собираются вместе одним сборщиком, поэтому общий заголовок
// для них — не связывание независимых модулей, а нормальная внутренняя
// утилита одного слоя.
//
#ifndef MIK32_CONFIGURATOR_CONTRIBUTION_UTILS_H
#define MIK32_CONFIGURATOR_CONTRIBUTION_UTILS_H

#include <cstdint>
#include <map>
#include <string>
#include <system_error>
#include <vector>

// Порядок вызова init-функций в сгенерированном main(). Значения с шагом 10,
// чтобы будущую периферию (SPI, таймеры 32 бит) можно было вставить между
// существующими, не переписывая остальные.
//
//  - GPIO первым: он задаёт функцию вывода в PAD_CONFIG. Электрически
//    порядок не критичен (HAL_GPIO_Init делает read-modify-write по каждому
//    пину отдельно, mik32_hal_gpio.c:47-72, и не может затереть мукс чужого
//    пина), но настраивать физический уровень до включения протокольной
//    периферии — предсказуемее.
//  - UART вторым: тогда любой диагностический вывод из инициализации
//    следующих модулей уже может быть напечатан. Тот же порядок в
//    docs/examples/mik32-examples/I2C_Scan/src/main.c:29-30 — сначала UART,
//    потом I2C.
//  - I2C после UART — по причине выше.
//  - Timer16 последним: он единственный, кто СРАЗУ начинает генерировать
//    сигнал (HAL_Timer16_StartPWM вызывается прямо в init-функции), поэтому
//    запускается, когда остальная периферия уже настроена.
constexpr int kInitOrderGpio = 10;
constexpr int kInitOrderUart = 20;
// SPI между UART и I2C: обе шинные периферии равноценны по порядку
// относительно печати диагностики (см. обоснование I2C выше), а не друг
// друга — значение выбрано просто чтобы оставить зазор с обеих сторон.
constexpr int kInitOrderSpi = 25;
constexpr int kInitOrderI2c = 30;
constexpr int kInitOrderTimer16 = 40;
// Timer32 после Timer16 по той же причине — начинает считать (и, при
// сконфигурированных каналах, генерировать прерывания) сразу внутри своей
// init-функции.
constexpr int kInitOrderTimer32 = 50;

template <typename ValueT>
const ValueT* findOrNull(const std::map<std::string, ValueT>& map, const std::string& key)
{
    auto it = map.find(key);
    return it == map.end() ? nullptr : &it->second;
}

template <typename ValueT>
const ValueT* findOrNull(const std::map<int, ValueT>& map, int key)
{
    auto it = map.find(key);
    return it == map.end() ? nullptr : &it->second;
}

// "GPIO_PIN_0 | GPIO_PIN_3" — HAL_PinsTypeDef (mik32_hal_gpio.h:33-53)
// определяет GPIO_PIN_<n> как (1 << n), побитовое ИЛИ и есть маска Pin,
// которую ждёт HAL_GPIO_Init.
std::string buildPinMaskExpression(const std::vector<uint8_t>& pins);

// "0, 3, 7" — только для текста комментария в сгенерированном коде.
std::string buildPinListComment(const std::vector<uint8_t>& pins);

// Рекурсивное копирование дерева каталогов с пропуском уже существующих
// файлов в месте назначения ("не перезаписывать то, что пользователь мог
// отредактировать руками" — тот же контракт, что и у skip_existing).
//
// Это независимая копия codegen/project/project_codegen.cpp::copySkeletonInto
// (та же причина существования: fs::copy(..., skip_existing) на каталог
// назначения ведёт себя по-разному на MinGW GCC 11.2.0 и на Linux GCC —
// см. комментарий у оригинала), а не рефакторинг с вызовом общей функции.
// project_codegen.cpp уже проверен HW-подтверждёнными golden-фикстурами
// PlatformIO-пути, и CLAUDE.md/спецификация этой сессии требуют не менять
// в нём НИ ОДНОЙ строки — поэтому его private-копию (анонимное пространство
// имён) нельзя ни экспортировать, ни удалить в пользу этой. Используется
// только codegen/project/cmake_project_codegen.cpp.
bool copyDirectoryTreeSkipExisting(const std::string& from_dir,
                                    const std::string& to_dir,
                                    std::error_code& fs_error);

#endif //MIK32_CONFIGURATOR_CONTRIBUTION_UTILS_H
