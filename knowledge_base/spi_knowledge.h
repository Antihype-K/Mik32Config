//
// Загрузчик knowledge_base/spi.json — по образцу i2c_knowledge.h/
// timer32_knowledge.h. ТЗ по SPI не содержит отдельного раздела
// (см. docs/implementations/spi.md) — источники: реальный HAL
// (mik32_hal_spi.h/.c) и datasheet (docs/MIK32_REFERENCE.md, §4).
//
#ifndef MIK32_CONFIGURATOR_SPI_KNOWLEDGE_H
#define MIK32_CONFIGURATOR_SPI_KNOWLEDGE_H

#include <map>
#include <string>
#include <vector>
#include "parse_result.h"

struct SpiKnowledge
{
    int max_instances;

    // instance (0/1) -> имя указателя-хендла HAL, например "SPI_1".
    std::map<int, std::string> instance_handle;

    struct FixedPin
    {
        int gpio_instance;
        int pin;
    };

    // instance (0/1) -> четыре всегда занятых пина (SCK/MOSI/MISO/SS_IN) —
    // резервируются при включении instance независимо от mode (см.
    // knowledge_base/spi.json, _comment_instance_fixed_pins).
    std::map<int, std::vector<FixedPin>> instance_fixed_pins;

    // instance -> chip_select ("cs0".."cs3") -> жёстко закреплённый вывод.
    // Заявка на этот пин формируется только при mode=master (см.
    // knowledge_base/spi.json, _comment_instance_chip_select_pin).
    std::map<int, std::map<std::string, FixedPin>> instance_chip_select_pin;
};

ParseResult<SpiKnowledge> loadSpiKnowledge(const std::string& file_path);

#endif //MIK32_CONFIGURATOR_SPI_KNOWLEDGE_H
