//
// Created by antihype on 31.07.2026.
//

#ifndef MIK32_CONFIGURATOR_GPIO_PARSER_H
#define MIK32_CONFIGURATOR_GPIO_PARSER_H
#include <string>
#include <vector>
#include "parse_result.h"
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



#endif //MIK32_CONFIGURATOR_GPIO_PARSER_H
