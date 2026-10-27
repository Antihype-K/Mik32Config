//
// Arduino-ветка кодогенерации — третий выходной режим (наравне с C и Rust,
// см. project_config.h, OutputLanguage). v1 решён в /grill-me сессии
// 2026-10-26 (docs/open_tasks.md): GPIO + Serial (UART0/1) + Wire (I2C0/1)
// + setup()/loop(), без ADC/SPI/прерываний. ШИМ (Timer32 -> analogWrite())
// добавлен следом в той же сессии — та же логика, по которой
// Rust-ветка (rust_project_codegen.h) не копирует слой
// PeripheralContribution/knowledge_base C-ветки: объём v1 мал, вся сборка
// main.cpp — в одной функции этого модуля.
//
// cores/arduino/* вендорены из docs/elron/elbear_arduino_bsp как
// templates/mik32_arduino_core_snapshot/ (LGPL/BSD-заголовки сохранены в
// самих файлах, без дополнительного attribution в GUI/генерируемом
// проекте — см. docs/implementations/arduino_codegen.md). Единственный
// вариант платы — rex_board (1T Rex Board/AmurWiFi, несущий модуль
// ELSOMIK SE — та же нумерация пинов P0_x/P1_x/P2_6/P2_7, что и готовый
// elbear_arduino_bsp/variants/elsomik).
//
#ifndef MIK32_CONFIGURATOR_ARDUINO_PROJECT_CODEGEN_H
#define MIK32_CONFIGURATOR_ARDUINO_PROJECT_CODEGEN_H

#include <string>
#include "project_config.h"
#include "parse_result.h"
#include "codegen_result.h"
#include "json.hpp"

struct GeneratedProjectFirmwareArduino
{
    std::string main_cpp;

    // Clock/PCC (CLAUDE.md, Tier 1 — закрыт в C-ветке). Arduino-ядро
    // (templates/mik32_arduino_core_snapshot/src/cores/arduino/board.cpp)
    // настраивает тактирование не в main.cpp, а в SystemInit() — зашитый
    // блок PCC_OscInit/HAL_PCC_Config, вызываемый ДО setup() (и даже до
    // глобальных C++-конструкторов). writeGeneratedArduinoProject патчит
    // этот блок в скопированном board.cpp этим сгенерированным текстом —
    // отрендерен из ProjectConfig.clock тем же набором HAL-макросов, что и
    // C-ветка (project_codegen.cpp::renderSystemClockConfig). При
    // config.clock == ClockConfig{} (поле "clock" не задано в JSON) текст
    // побайтово совпадает со старым хардкодом board.cpp — поведение не
    // меняется, см. project_config.cpp комментарий у разбора "clock".
    std::string system_init_pcc_block;

    // НЕ частота AHB, несмотря на имя поля (сохранено ради минимального
    // диффа) — это частота, от которой HAL реально считает BRR UART:
    // project_config.h::computeUartPeripheralFrequencyHz() (SysClk/(AHB+1)/
    // (APB_P+1), баг с одним делителем AHB найден HW-валидацией 2026-10-27).
    // HardwareSerial.cpp/Tone.cpp/SPI.cpp вендоренного Arduino-ядра считают
    // тайминги (BRR и т.п.) от макроса F_CPU, захардкоженного в
    // platformio.ini (-D F_CPU=32000000UL) — если Clock-блок задаёт
    // нестандартные делители/осциллятор, F_CPU расходится с реальной
    // частотой UART и все эти расчёты врут. writeGeneratedArduinoProject
    // патчит platformio.ini этим значением, только если оно отличается от
    // дефолтных 32000000 (т.е. Clock-блок реально нестандартный) — иначе
    // platformio.ini не трогается.
    long long ahb_frequency_hz = 32000000LL;
};

// Валидация "поддерживается ли фича в Arduino-ветке вообще" зашита прямо
// здесь (не отдельным validation/*_feature_validator.h, как у Rust) — v1
// объём настолько мал (GPIO/UART/I2C), что для единственной проверки
// (остальные типы периферии не должны попадать в проект) отдельный файл —
// лишний слой ради одной функции.
ParseResult<GeneratedProjectFirmwareArduino> generateProjectFirmwareArduino(
    const ProjectConfig& config);

// Транзакционная запись PlatformIO-проекта — тот же контракт, что
// writeGeneratedRustProject/writeGeneratedCmakeProject (временный каталог-
// брат output_project_dir, перенос только после того, как все файлы
// успешно собраны).
//
//   pio_skeleton_dir — templates/pio_skeleton_arduino (platformio.ini с
//     framework=arduino, board=rex_board).
//   arduino_core_snapshot_dir — templates/mik32_arduino_core_snapshot
//     (cores/arduino/* + variants/rex_board/*), копируется в проект под
//     lib/mik32_arduino_core/ (PlatformIO library-конвенция).
//   project_json_copy — сериализуется как <output_project_dir>/project.mik32.
//
// USER CODE секции main.cpp (// USER CODE BEGIN/END, тот же формат, что C
// main.c — codegen/user_code_markers.h) сливаются с уже существующим на
// диске main.cpp.
CodegenWriteResult writeGeneratedArduinoProject(
    const GeneratedProjectFirmwareArduino& firmware,
    const std::string& pio_skeleton_dir,
    const std::string& arduino_core_snapshot_dir,
    const std::string& output_project_dir,
    const nlohmann::json& project_json_copy);

#endif //MIK32_CONFIGURATOR_ARDUINO_PROJECT_CODEGEN_H
