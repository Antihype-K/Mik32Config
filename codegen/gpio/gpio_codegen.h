//
// Кодогенератор C-инициализации для периферии GPIO.
//
// Концепция: на входе один разобранный и провалидированный GpioConfig
// (см. parser/gpio/gpio_parser.h) плюс аппаратные факты из
// knowledge_base/gpio.json (см. knowledge_base/gpio_knowledge.h). На выходе —
// текст трёх файлов реального PlatformIO-проекта framework-mik32v2-sdk:
// mik32_init.h/.c (функция инициализации) и main.c (шаблон точки входа,
// вызывающий эту функцию). Уровень генерации — HAL-based, вызовы
// HAL_GPIO_Init/__HAL_PCC_GPIO_x_CLK_ENABLE из готового SDK, а не запись в
// регистры напрямую (см. МИК32 конфигуратор - Архитектура.md, §9.2 "MVP:
// HAL-based").
//
#ifndef MIK32_CONFIGURATOR_GPIO_CODEGEN_H
#define MIK32_CONFIGURATOR_GPIO_CODEGEN_H

#include <string>
#include "gpio_parser.h"
#include "gpio_knowledge.h"
#include "parse_result.h"
#include "codegen_result.h"

struct GeneratedGpioFirmware
{
    std::string mik32_init_h;
    std::string mik32_init_c;
    std::string main_c;
};

// Строит содержимое трёх файлов по одному провалидированному GpioConfig.
// Ошибка возможна только если config ссылается на mode/pull/drive_strength
// или instance, отсутствующие в knowledge — при валидации через
// parseGpioConfig (использующую ту же схему) такого не случится, проверка
// здесь на случай, если GpioConfig собран в тестах вручную, в обход парсера.
ParseResult<GeneratedGpioFirmware> generateGpioFirmware(
    const GpioConfig& config,
    const GpioKnowledge& knowledge);

// Копирует templates/pio_skeleton в output_project_dir (если там ещё нет
// project-файлов) и перезаписывает в нём только каталог src/ содержимым
// firmware. Не трогает platformio.ini/.vscode/lib/test при повторной
// генерации в тот же каталог.
CodegenWriteResult writeGeneratedGpioProject(
    const GeneratedGpioFirmware& firmware,
    const std::string& skeleton_dir,
    const std::string& output_project_dir);

#endif //MIK32_CONFIGURATOR_GPIO_CODEGEN_H
