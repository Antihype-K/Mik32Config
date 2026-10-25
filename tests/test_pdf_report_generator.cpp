#include <gtest/gtest.h>

#include "pdf_report_generator.h"

TEST(PdfReportGenerator, ListsEachPeripheralBlockAndItsFields)
{
    const nlohmann::json doc = {
        {"mcu", "MIK32V2"},
        {"peripherals", {
            {{"type", "gpio"}, {"pin", 3}, {"mode", "output"}},
            {{"type", "timer16"}, {"frequency_hz", 1000}}
        }}
    };

    const std::string report = buildConfigReportText(doc);

    EXPECT_NE(report.find("MIK32V2"), std::string::npos);
    EXPECT_NE(report.find("gpio"), std::string::npos);
    EXPECT_NE(report.find("pin"), std::string::npos);
    EXPECT_NE(report.find("timer16"), std::string::npos);
    EXPECT_NE(report.find("frequency_hz"), std::string::npos);
}

TEST(PdfReportGenerator, EmptyPeripheralsProducesPlaceholderNotCrash)
{
    const nlohmann::json doc = {{"mcu", "MIK32V2"}, {"peripherals", nlohmann::json::array()}};

    const std::string report = buildConfigReportText(doc);

    EXPECT_NE(report.find("нет настроенных"), std::string::npos);
}
