//
// Created by antihype on 31.07.2026.
//

#ifndef MIK32_CONFIGURATOR_GPIO_PARSER_H
#define MIK32_CONFIGURATOR_GPIO_PARSER_H
#include <string>
#include <vector>
#include "parse_result.h"
#include "json.hpp"
#include <cstdint>



struct GpioConfig
{
    std::string type;
    int instance;
    std::string mode;
    std::string pull;
    std::string drive_strength;
    std::vector<uint8_t> pins;
};

ParseResult<GpioConfig> parseGpioConfig(const std::string& file_name_path);

// Блочная точка входа: валидирует ОДИН элемент массива "peripherals",
// уже извлечённый вызывающим кодом. Нужна мультипериферийному пайплайну
// (codegen/project/project_config.cpp), которому требуется разобрать ВСЕ
// блоки type="gpio" в файле, а не только первый. parseGpioConfig выше —
// тонкая обёртка: открыть файл, найти первый gpio-блок, вызвать эту функцию.
// Вся валидация схемы живёт здесь в единственном экземпляре.
ParseResult<GpioConfig> parseGpioBlock(const nlohmann::json& gpio);



#endif //MIK32_CONFIGURATOR_GPIO_PARSER_H
