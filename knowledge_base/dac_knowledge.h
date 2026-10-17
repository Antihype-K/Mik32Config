//
// Загрузчик knowledge_base/dac.json — по образцу timer32_knowledge.h.
//
#ifndef MIK32_CONFIGURATOR_DAC_KNOWLEDGE_H
#define MIK32_CONFIGURATOR_DAC_KNOWLEDGE_H

#include <string>
#include <map>
#include "parse_result.h"

struct DacKnowledge
{
    struct FixedPin
    {
        int gpio_instance;
        int pin;
    };

    // instance (0/1) -> имя HAL-макроса хендла ("HAL_DAC0"/"HAL_DAC1").
    std::map<int, std::string> instance_handle;

    // instance (0/1) -> жёстко закреплённый вывод (GPIO_1/PIN_12 или 13).
    std::map<int, FixedPin> instance_fixed_pin;

    // Общий (не per-instance) вывод внешнего опорного напряжения DAC_REF —
    // занимается только при ext_ref=true && ext_clb=true.
    FixedPin dac_ref_pin;
};

ParseResult<DacKnowledge> loadDacKnowledge(const std::string& file_path);

#endif //MIK32_CONFIGURATOR_DAC_KNOWLEDGE_H
