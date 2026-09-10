//
// Сборщик мультипериферийного проекта — единственное место в кодовой базе,
// где кусочки (PeripheralContribution) превращаются в файлы на диске.
//
// Концепция. Пайплайн целиком:
//
//   JSON-конфиг
//     -> parseProjectConfig            (codegen/project/project_config.h)
//          все блоки всех типов, каждый через свою schema-валидацию
//     -> validatePinConflicts          (validation/cross_module_validator.h)
//          ТЗ §3.1.6.2/3.1.6.3 — конфликт пинов блокирует кодогенерацию
//     -> build<X>Contribution          (codegen/project/*_contribution.h)
//          каждый блок отдаёт свои кусочки
//     -> generateProjectFirmware       (этот файл)
//          кусочки сшиваются в ОДИН mik32_init.h/.c и ОДИН main.c
//     -> writeGeneratedProject         (этот файл)
//          скелет + запись src/, main.c проходит через USER CODE маркеры
//
// Что здесь исчезает по сравнению со старой схемой:
//  - пять почти дословно одинаковых writeGenerated*Project (gpio, uart,
//    timer16, i2c, demo) сведены к одному writeGeneratedProject;
//  - SystemClock_Config, скопированная в пять генераторов, живёт в одном
//    месте;
//  - сшивка GPIO+UART больше не нужна как отдельный генератор: это просто
//    конфиг с двумя блоками.
//
// Однопериферийные генераторы (codegen/gpio/, codegen/uart/, codegen/timer16/,
// codegen/i2c/) и сшивка codegen/demo/ удалены — этот слой их полностью
// заменяет.
//
#ifndef MIK32_CONFIGURATOR_PROJECT_CODEGEN_H
#define MIK32_CONFIGURATOR_PROJECT_CODEGEN_H

#include <string>
#include "project_config.h"
#include "gpio_knowledge.h"
#include "uart_knowledge.h"
#include "timer16_knowledge.h"
#include "i2c_knowledge.h"
#include "timer32_knowledge.h"
#include "spi_knowledge.h"
#include "freertos_parser.h"
#include "parse_result.h"
#include "codegen_result.h"

// Все базы знаний передаются всегда, даже если соответствующей периферии в
// конфиге нет: их загрузка — дело вызывающего (одна точка чтения
// knowledge_base/*.json), а не сборщика, и они всё равно нужны целиком для
// кросс-модульной валидации пинов.
struct ProjectKnowledge
{
    GpioKnowledge gpio;
    UartKnowledge uart;
    Timer16Knowledge timer16;
    I2cKnowledge i2c;
    Timer32Knowledge timer32;
    SpiKnowledge spi;
};

struct GeneratedProjectFirmware
{
    std::string mik32_init_h;
    std::string mik32_init_c;
    std::string main_c;

    // Непусто, только если config.freertos.enabled — содержимое
    // src/FreeRTOSConfig.h (codegen/project/freertos_codegen.h). mik32_init.h/.c
    // FreeRTOS не меняет вовсе (см. project_codegen.cpp) — только main.c и
    // этот новый файл.
    std::string freertos_config_h;
};

// emit_demo_loops — генерировать ли в теле while(1) наблюдаемую демо-логику
// (мигание выходами GPIO, периодическая строка в UART, сценарий "demo" у I2C).
//
// Флаг существует, потому что здесь сталкиваются два разумных требования.
// С одной стороны, конфигуратор по ТЗ настраивает периферию, а не пишет
// прикладную логику — «чистая» генерация должна давать пустой while(1) под
// маркерами USER CODE. С другой стороны, аппаратная проверка (см. CLAUDE.md,
// hardware validation gate) требует, чтобы сгенерированная прошивка что-то
// делала наблюдаемое на осциллографе/в терминале, а поле "demo" в
// schemas/i2c.json уже описывает такие сценарии явно. Поэтому демо не
// вырезано и не зашито, а вынесено в один переключатель проекта: true —
// прошивка сразу демонстрирует настроенную периферию, false — генерируется
// только инициализация. Значение по умолчанию true сохраняет поведение
// уже проверенных на плате конфигураций (tests/fixtures/gpio_uart_demo.json,
// i2c_master_scanner.json и т.п.).
ParseResult<GeneratedProjectFirmware> generateProjectFirmware(
    const ProjectConfig& config,
    const ProjectKnowledge& knowledge,
    bool emit_demo_loops = true);

// Копирует skeleton_dir в output_project_dir (только если там ещё нет
// platformio.ini — повторная генерация не затирает то, что пользователь
// правил руками) и перезаписывает src/. main.c пропускается через
// mergeUserCodeSections: содержимое между /* USER CODE BEGIN <ID> */ и
// /* USER CODE END <ID> */ берётся из существующего файла (ТЗ §3.1.7.3).
// mik32_init.{h,c} маркеров не имеют и переписываются целиком.
// freertos_config/freertos_kernel_snapshot_dir — по умолчанию FreeRTOS
// выключен (существующие вызовы из gui/main_window.cpp, src/main.cpp и
// тестов не меняются вовсе). Когда freertos_config.enabled: дополнительно
// пишет src/FreeRTOSConfig.h (той же транзакцией, что mik32_init.h/.c/main.c)
// и копирует FreeRTOS-Kernel в src/FreeRTOS-Kernel/ (см. freertos_codegen.h) —
// отдельным, не транзакционным шагом, тем же принципом skip_existing, что и
// копирование skeleton_dir.
CodegenWriteResult writeGeneratedProject(
    const GeneratedProjectFirmware& firmware,
    const std::string& skeleton_dir,
    const std::string& output_project_dir,
    const FreeRtosConfig& freertos_config = FreeRtosConfig{},
    const std::string& freertos_kernel_snapshot_dir = "templates/freertos_kernel_snapshot");

#endif //MIK32_CONFIGURATOR_PROJECT_CODEGEN_H
