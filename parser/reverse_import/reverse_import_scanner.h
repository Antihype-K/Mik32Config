//
// Обратный сканер: вычленяет присваивания полей HAL-хендлов (I2C/USART/
// Timer32/GPIO) из произвольного, НЕ сгенерированного этим конфигуратором
// C/C++ исходника — например, docs/сделать/mik32_fixed_v3 (руками написанная
// прошивка робота, периферия настроена прямыми присваиваниями полей вроде
// `husart0.baudrate = 38400;`, без единого JSON/маркеров).
//
// Контракт с типовыми мапперами (uart_reverse_mapper.h и т.п.): этот файл
// НЕ знает, что такое "uart" или "instance" — он только группирует
// присваивания полей одного хендла в блоки по границам вызовов HAL_*_Init,
// сохраняя для каждого присваивания точный файл+строку+исходный текст
// строки (нужно двум потребителям: типовому мапперу — распознать поле по
// имени типа хендла, и reverse_patch_writer — переписать ровно эту строку
// при сохранении правки).
//
// Почему группировка "один блок = один вызов Init", а не "все присваивания
// хендла от объявления до конца файла": HAL_GPIO_Init вызывается по разу на
// каждый пин, каждый раз через ОДНУ и ту же переменную GPIO_InitStruct —
// `GPIO_InitStruct.Pin = GPIO_PIN_6; HAL_GPIO_Init(GPIO_1, &GPIO_InitStruct);`
// повторяется по числу пинов. Если бы сканер не резал по Init-вызовам, все
// пины слились бы в одно присваивание Pin (последнее победило бы), а не в
// N отдельных блоков — ровно так это и обрабатывает schemas/gpio.json
// (несколько блоков gpio на один instance, один блок на группу пинов).
//
// Ограничения (см. docs/implementations/reverse_import.md):
//  - многострочные блочные комментарии /* ... */, растянутые на несколько
//    строк, не вырезаются (вырезается только однострочный /* ... */ и //);
//  - хендл должен быть объявлен и инициализирован в одном и том же файле —
//    объявление в заголовке, Init в другом .cpp не поддерживается, такие
//    случаи попадают в ScannedProject::unsupported_notes, а не молча
//    пропускаются;
//  - многострочные присваивания (значение с переносом строки до ';') не
//    распознаются — попадают в unsupported_notes с указанием строки.
//
#ifndef MIK32_CONFIGURATOR_REVERSE_IMPORT_SCANNER_H
#define MIK32_CONFIGURATOR_REVERSE_IMPORT_SCANNER_H

#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace reverse_import
{

struct SourceLocation
{
    std::string file;   // абсолютный путь, как передан сканеру
    int line = 0;        // 1-based, как в редакторах и компиляторных диагностиках

    // Короткое имя файла для сообщений пользователю (notes/диагностика) —
    // полный путь там не нужен и только шумит, а patch-writer продолжает
    // читать полный file как есть.
    std::string fileName() const
    {
        const std::size_t slash = file.find_last_of("/\\");
        return slash == std::string::npos ? file : file.substr(slash + 1);
    }
};

// Одно присваивание вида "<handle>.<field_path> = <value>;" внутри одного
// Init-блока. field_path сохраняет вложенность как в исходнике ("Clock.PRESC",
// "Init.Mode") — типовой маппер сам решает, что с ней делать.
struct RawFieldAssignment
{
    std::string field_path;
    std::string raw_value_text;   // правая часть без ';' и без комментария, но БЕЗ обработки (как написано)
    SourceLocation location;
    std::string full_line_text;   // исходная строка целиком, без изменений — якорь для patch-writer
};

// Один вызов HAL_*_Init(...), закрывающий накопленные присваивания в блок.
// Вызов ЛЮБОЙ функции (не только HAL_*_Init), ссылающийся на хендл этого
// блока, ПОСЛЕ его Init-вызова — например "HAL_Timer16_StartPWM(&htimer16_0,
// 999, 499);". Сканер не знает, что такое "StartPWM" или что аргументы
// значат ARR/CMP — он только честно записывает, ЧТО было вызвано и с какими
// аргументами; смысл придаёт типовой маппер (см. timer16_reverse_mapper.h,
// где mode/arr/cmp Timer16 берутся ИМЕННО из этих вызовов, а не из
// присваиваний полей — HAL_Timer16_Init сам по себе ARR/CMP не задаёт).
struct RawTrailingCall
{
    std::string function_name;             // "HAL_Timer16_StartPWM" и т.п.
    std::vector<std::string> args;          // каждый без "&", в порядке следования
    SourceLocation location;
    std::string full_line_text;
};

struct RawInitBlock
{
    std::string handle_name;   // "hi2c0", "GPIO_InitStruct" — как написано в коде
    std::string handle_type;   // "I2C_HandleTypeDef", "USART_HandleTypeDef", "TIMER32_HandleTypeDef", "GPIO_InitTypeDef"
    std::vector<RawFieldAssignment> assignments;   // в порядке появления в файле

    std::string init_function_name;  // "HAL_I2C_Init", "HAL_GPIO_Init" и т.п.
    // Аргументы вызова HAL_*_Init в порядке следования, каждый без "&".
    // Для большинства периферий один аргумент — сам хендл. Для GPIO — два:
    // порт первым ("GPIO_1"), указатель на структуру вторым — маппер gpio
    // берёт instance из этого первого аргумента, а не из полей структуры
    // (в GPIO_InitTypeDef нет поля "порт" — порт передаётся отдельно).
    std::vector<std::string> init_call_args;
    SourceLocation init_call_location;

    // Вызовы ПОСЛЕ init_call_location, ссылающиеся на handle_name, до
    // следующего объявления/переинициализации этого же имени — см.
    // RawTrailingCall выше. Для большинства типов периферии остаётся пустым
    // (HAL_I2C_Init/HAL_USART_Init/HAL_Timer32_Init самодостаточны — вся
    // конфигурация в полях). Может содержать "шум" — вызовы, использующие
    // хендл не для донастройки (например передачу его в прикладную функцию
    // другого модуля) — типовые мапперы обязаны игнорировать всё, кроме
    // конкретных имён функций, которые они явно понимают.
    std::vector<RawTrailingCall> trailing_calls;

    // Имя функции-обёртки, внутри которой найден init_call_location (пусто —
    // блок лежит прямо в main() или на верхнем уровне файла). Используется
    // только для фильтра недостижимого кода: если эта функция нигде в
    // просканированном дереве не вызвана ни одной другой функцией, блок
    // считается мёртвым кодом и не участвует в итоговой конфигурации (см.
    // scanDirectoryForHalHandles, фильтр отбрасывает такие блоки в
    // unsupported_notes). main() и *_IRQHandler* всегда считаются
    // достижимыми — у main вызывающего нет по определению, у обработчика
    // прерывания вызов идёт из железа, а не из текста исходника.
    std::string containing_function_name;

    // Узкий паттерн: для DMA_ChannelHandleTypeDef, если trailing_calls
    // содержит "HAL_DMA_Start(&<handle>, src, dst, sizeof(<array>) [+-N]);"
    // и <array> — массив, объявленный ОДНОЙ строкой в этом же файле
    // (явный размер [N] или счёт элементов инициализатора) — байтовый
    // размер посчитан здесь. Межфункциональная связка (значение приходит
    // через ДРУГОЙ хендл, например I2C DMA-обёртки) не поддерживается —
    // см. dma_reverse_mapper.cpp.
    std::optional<int> resolved_buffer_size_bytes;
    SourceLocation resolved_buffer_size_bytes_location;
    std::string resolved_buffer_size_bytes_source_text;
};

// HAL_GPIO_InitInterruptLine(GPIO_MUX_PORT<P>_<N>_LINE_<L>, GPIO_INT_MODE_<X>)
// не принимает хендл вообще (GPIO не имеет хендла, см. gpio_contribution.cpp)
// — единственный вызов, не привязанный ни к какому RawInitBlock, поэтому
// собирается отдельным плоским списком, а не trailing_calls. Расшифровка
// mux_macro в (port, pin, line) и mode_macro в trigger — на стороне
// gpio_reverse_mapper, сканер только честно записывает, что вызвано.
struct RawGpioInterruptCall
{
    std::string mux_macro;   // "GPIO_MUX_PORT2_6_LINE_2" как написано в коде
    std::string mode_macro;  // "GPIO_INT_MODE_RISING" как написано в коде
    SourceLocation location;
    std::string full_line_text;
};

// HAL_EPIC_MaskEdgeSet(HAL_EPIC_ADC_MASK) / HAL_EPIC_MaskLevelSet(...) —
// включение линии EPIC для конкретной периферии. Как и GPIO-прерывание выше,
// не привязано ни к какому хендлу (это регистр ядра EPIC, не периферии),
// поэтому отдельный плоский список. Только однооперандный вызов с ОДНИМ
// макросом вида "HAL_EPIC_<ИМЯ>_MASK" — комбинированная маска через "|"
// (несколько периферий одним вызовом) не разбирается, такой вызов уходит в
// ScannedProject::unsupported_notes вместо молчания.
struct RawEpicMaskCall
{
    std::string function_name;  // "HAL_EPIC_MaskEdgeSet" / "HAL_EPIC_MaskLevelSet"
    std::string mask_macro;     // "HAL_EPIC_ADC_MASK" как написано в коде
    SourceLocation location;
    std::string full_line_text;
};

struct ScannedProject
{
    std::vector<RawInitBlock> blocks;          // по всем файлам, в порядке обхода + порядка в файле
    std::vector<RawGpioInterruptCall> gpio_interrupt_calls;
    std::vector<RawEpicMaskCall> epic_mask_calls;
    std::vector<std::string> files_scanned;
    // Человекочитаемые причины, почему что-то НЕ попало в blocks — сканер
    // обязан объяснять пропуски, а не тихо терять периферию (сама суть
    // фичи — "вычленять ВСЮ периферию").
    std::vector<std::string> unsupported_notes;

    // Проектные object-like макросы вида "#define NAME IDENT" (RHS — голый
    // идентификатор, не число) — например "#define STATUS_LED_PORT GPIO_2"
    // в docs/examples/mik32-examples/HAL_EEPROM/src/main.c. Мапперы читают
    // это как подсказку при резолве значений (GPIO instance/pins), но текст
    // в RawFieldAssignment/init_call_args остаётся КАК В ИСХОДНИКЕ — иначе
    // якорь для reverse_patch_writer перестанет совпадать со строкой файла.
    std::unordered_map<std::string, std::string> macro_aliases;

    // Имена всех идентификаторов, встреченных в позиции РЕАЛЬНОГО (не
    // закомментированного) вызова функции (`ident(...)`) ВНУТРИ тела
    // какой-либо функции — накапливается по всему дереву. НЕ используется
    // как позитивное условие "функция вызвана где-то" (большинство
    // reverse-import фикстур/реальных частичных деревьев определяют
    // *_Init()-обёртки без видимого вызова в просканированном подмножестве
    // файлов — это нормально, не признак мёртвого кода). Используется только
    // как перевес против commented_out_call_names ниже: если у функции есть
    // И закомментированный, И настоящий вызов — она реальная, не мёртвая.
    std::unordered_set<std::string> called_function_names;

    // Имена функций, встреченные в позиции вызова ВНУТРИ "//"-комментария
    // (`// ident(...)`) — конкретный сигнал "этот вызов когда-то был, но
    // его закомментировали" (см. docs/examples/mik32/controlFirmware/src/
    // utils.cpp:24, "//i2c_bus_recovery(i2c, 32000000, 100000);"). Вместе с
    // called_function_names определяет фильтр недостижимого кода в
    // scanDirectoryForHalHandles.
    std::unordered_set<std::string> commented_out_call_names;
};

// Рекурсивно обходит root_dir, читает *.c/*.cpp/*.h/*.hpp, ищет объявления
// "<Type>_HandleTypeDef <name>;" / "<Type>_InitTypeDef <name>;" и в том же
// файле, после объявления, присваивания и вызовы HAL_*_Init, ссылающиеся на
// это имя (по совпадению аргумента без "&"). known_handle_type_suffixes —
// список суффиксов типов, которые распознавать ("HandleTypeDef", "InitTypeDef"
// по умолчанию, см. .cpp) — параметр оставлен для тестов (можно сузить набор
// без изменения кода сканера).
ScannedProject scanDirectoryForHalHandles(const std::string& root_dir);

} // namespace reverse_import

#endif //MIK32_CONFIGURATOR_REVERSE_IMPORT_SCANNER_H
