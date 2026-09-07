//
// Загрузчик knowledge_base/package.json — по образцу gpio_knowledge.h и
// остальных загрузчиков базы знаний.
//
// Отличие от них по смыслу: все прочие knowledge_base/*.json описывают
// ПЕРИФЕРИЮ и отвечают на вопрос «какой вывод занимает блок X». Этот
// описывает КОРПУС и отвечает на обратный вопрос — «что физически висит на
// выводе N». Обратное отображение нужно только GUI (ТЗ §3.1.9 п.2, схема
// корпуса в центральной панели); кодогенерация его не читает и читать не
// должна — генератору достаточно прямого отображения из uart.json/i2c.json/
// timer16.json.
//
#ifndef MIK32_CONFIGURATOR_PACKAGE_KNOWLEDGE_H
#define MIK32_CONFIGURATOR_PACKAGE_KNOWLEDGE_H

#include <string>
#include <vector>
#include <optional>
#include "parse_result.h"

struct PackagePin
{
    // Номер вывода корпуса, 1..64 — как в Таблице 193 даташита.
    int pin;

    // Обозначение из даташита: "PORT_1_12" для мультиплексируемого вывода,
    // "VCC"/"VSS"/"RST#"/"BOOT_0"/"OSC32K_XO" и т.п. для служебного.
    std::string name;

    // Заполнены только для мультиплексируемых выводов (name вида PORT_x_y).
    // Для служебных выводов пусто: у VCC нет разряда GPIO, и GUI обязан
    // отличать «вывод, который можно назначить» от «вывода, который занят
    // питанием» — не по имени строкой, а по наличию этой пары.
    std::optional<int> gpio_instance;
    std::optional<int> gpio_pin;

    bool isMultiplexed() const { return gpio_instance.has_value(); }
};

struct PackageKnowledge
{
    std::string package;  // "QFN-64"
    int pin_count;        // 64

    // Все выводы в порядке возрастания номера. Индекс в векторе НЕ равен
    // номеру вывода (нумерация с 1) — искать через findPin ниже.
    std::vector<PackagePin> pins;

    // nullptr, если вывода с таким номером нет.
    const PackagePin* findPin(int pin_number) const;

    // nullptr, если этот разряд GPIO не разведён в корпусе. Именно этой
    // функцией GUI связывает конфигурацию (которая оперирует парой
    // instance+pin, см. schemas/gpio.json) с физическим выводом корпуса.
    const PackagePin* findByGpio(int gpio_instance, int gpio_pin) const;
};

ParseResult<PackageKnowledge> loadPackageKnowledge(const std::string& file_path);

#endif //MIK32_CONFIGURATOR_PACKAGE_KNOWLEDGE_H
