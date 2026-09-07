#include "contribution_utils.h"
#include <sstream>

std::string buildPinMaskExpression(const std::vector<uint8_t>& pins)
{
    std::ostringstream out;
    for (size_t i = 0; i < pins.size(); ++i)
    {
        if (i > 0)
        {
            out << " | ";
        }
        out << "GPIO_PIN_" << static_cast<int>(pins[i]);
    }
    return out.str();
}

std::string buildPinListComment(const std::vector<uint8_t>& pins)
{
    std::ostringstream out;
    for (size_t i = 0; i < pins.size(); ++i)
    {
        if (i > 0)
        {
            out << ", ";
        }
        out << static_cast<int>(pins[i]);
    }
    return out.str();
}
