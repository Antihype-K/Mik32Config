#include "pdf_report_generator.h"

std::string buildConfigReportText(const nlohmann::json& projectDocument)
{
    std::string text = "Отчёт о конфигурации проекта МИК32\n\n";

    const auto mcu_it = projectDocument.find("mcu");
    if (mcu_it != projectDocument.end() && mcu_it->is_string())
    {
        text += "МК: " + mcu_it->get<std::string>() + "\n\n";
    }

    const auto peripherals_it = projectDocument.find("peripherals");
    if (peripherals_it == projectDocument.end() || !peripherals_it->is_array()
        || peripherals_it->empty())
    {
        text += "В конфигурации нет настроенных периферийных блоков.\n";
        return text;
    }

    for (const nlohmann::json& block : *peripherals_it)
    {
        const auto type_it = block.find("type");
        text += "- " + (type_it != block.end() && type_it->is_string()
                             ? type_it->get<std::string>()
                             : std::string("?"))
            + "\n";
        for (auto field_it = block.begin(); field_it != block.end(); ++field_it)
        {
            if (field_it.key() == "type")
            {
                continue;
            }
            text += "    " + field_it.key() + ": " + field_it.value().dump() + "\n";
        }
    }

    return text;
}
