//
// Загрузчик knowledge_base/i2c.json — по образцу gpio_knowledge.h/
// uart_knowledge.h/timer16_knowledge.h.
//
#ifndef MIK32_CONFIGURATOR_I2C_KNOWLEDGE_H
#define MIK32_CONFIGURATOR_I2C_KNOWLEDGE_H

#include <string>
#include <map>
#include <vector>
#include "parse_result.h"

struct I2cKnowledge
{
    int max_instances;

    // instance (0/1) -> имя указателя-хендла HAL, например "I2C_1".
    std::map<int, std::string> instance_handle;

    struct FixedPin
    {
        int gpio_instance;
        int pin;
    };
    // instance (0/1) -> пара пинов (SDA+SCL, порядок не подтверждён датащитом
    // — см. _comment_instance_fixed_pins в knowledge_base/i2c.json).
    std::map<int, std::vector<FixedPin>> instance_fixed_pins;

    // duty_factor по умолчанию (~50/50 SCLH/SCLL) для расчёта TIMINGR.
    int duty_factor_default;

    // ТЗ §3.1.8: HSI 32МГц — константа для расчёта PRESC/SCLDEL/SDADEL/
    // SCLH/SCLL на этапе кодогенерации (см. codegen/i2c/i2c_codegen.cpp,
    // calcI2cTimingr) — не в рантайме, см. knowledge_base/i2c.json,
    // call_shape.timingr_computed_at_codegen_time_not_runtime.
    long sys_clk_hz;
};

ParseResult<I2cKnowledge> loadI2cKnowledge(const std::string& file_path);

#endif //MIK32_CONFIGURATOR_I2C_KNOWLEDGE_H
