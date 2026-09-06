//
// Created by antihype on 31.07.2026.
//

#ifndef MIK32_CONFIGURATOR_PARSE_RESULT_H
#define MIK32_CONFIGURATOR_PARSE_RESULT_H
#include <string>
#include <optional>


template <typename T>
struct ParseResult
{
    bool success;
    std::string error;
    std::optional<T> config;
};


#endif //MIK32_CONFIGURATOR_PARSE_RESULT_H
