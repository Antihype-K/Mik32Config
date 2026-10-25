//
// Тесты bus_watchdog_reverse_mapper.{h,cpp} (parser/reverse_import/). Нет
// HAL-обёртки/HandleTypeDef (Тир 3, регистровая работа) и нет вендорного
// примера для сверки — единственный известный проекту шаблон кода это
// codegen/project/bus_watchdog_contribution.cpp, поэтому тест строит
// синтетический файл по этому шаблону.
//
#include <gtest/gtest.h>

#include "../parser/reverse_import/bus_watchdog_reverse_mapper.h"

using reverse_import::FieldProvenance;
using reverse_import::MappedBlock;
using reverse_import::scanBusWatchdogBlockFromFileContent;

namespace
{

// Соответствует форме, генерируемой bus_watchdog_contribution.cpp
// (timeout=7, eeprom и dom3 включены, spifi выключен).
const char* kRealShapeFile =
    "void BusWatchdog_Init(void)\n"
    "{\n"
    "    __HAL_PCC_WDT_BUS_CLK_ENABLE();\n"
    "\n"
    "    WDT_BUS->TIMEOUT = 7u;\n"
    "\n"
    "    uint32_t bus_watchdog_enable = 0;\n"
    "    bus_watchdog_enable |= (1 << WDT_BUS_EEPROM_S);\n"
    "    bus_watchdog_enable |= (1 << WDT_BUS_DOM3_S);\n"
    "    WDT_BUS->ENABLE = bus_watchdog_enable;\n"
    "}\n";

const FieldProvenance* findProvenance(const MappedBlock& mapped, const std::string& field)
{
    for (const auto& provenance : mapped.provenance)
    {
        if (provenance.json_field == field)
        {
            return &provenance;
        }
    }
    return nullptr;
}

} // namespace

TEST(BusWatchdogReverseMapper, ResolvesTimeoutAndLinesFromRealShape)
{
    const std::optional<MappedBlock> mapped =
        scanBusWatchdogBlockFromFileContent(kRealShapeFile, "bus_watchdog_contribution_generated.c");
    ASSERT_TRUE(mapped.has_value());

    EXPECT_EQ(mapped->json_block.at("timeout").get<int>(), 7);
    EXPECT_EQ(mapped->json_block.at("eeprom_enabled").get<bool>(), true);
    EXPECT_EQ(mapped->json_block.at("spifi_enabled").get<bool>(), false);
    EXPECT_EQ(mapped->json_block.at("dom3_enabled").get<bool>(), true);

    const FieldProvenance* provenance = findProvenance(*mapped, "timeout");
    ASSERT_NE(provenance, nullptr);
    EXPECT_FALSE(provenance->unresolved);
}

TEST(BusWatchdogReverseMapper, AllLinesDisabledWhenNoFlagsSet)
{
    const char* content =
        "void BusWatchdog_Init(void)\n"
        "{\n"
        "    WDT_BUS->TIMEOUT = 0u;\n"
        "    uint32_t bus_watchdog_enable = 0;\n"
        "    WDT_BUS->ENABLE = bus_watchdog_enable;\n"
        "}\n";

    const std::optional<MappedBlock> mapped = scanBusWatchdogBlockFromFileContent(content, "f.c");
    ASSERT_TRUE(mapped.has_value());
    EXPECT_EQ(mapped->json_block.at("eeprom_enabled").get<bool>(), false);
    EXPECT_EQ(mapped->json_block.at("spifi_enabled").get<bool>(), false);
    EXPECT_EQ(mapped->json_block.at("dom3_enabled").get<bool>(), false);
}

TEST(BusWatchdogReverseMapper, NonLiteralTimeoutMarksUnresolved)
{
    std::string content = kRealShapeFile;
    const std::string literal_line = "    WDT_BUS->TIMEOUT = 7u;\n";
    const std::string expr_line = "    WDT_BUS->TIMEOUT = compute_timeout();\n";
    const std::size_t pos = content.find(literal_line);
    ASSERT_NE(pos, std::string::npos);
    content.replace(pos, literal_line.size(), expr_line);

    const std::optional<MappedBlock> mapped = scanBusWatchdogBlockFromFileContent(content, "f.c");
    ASSERT_TRUE(mapped.has_value());

    const FieldProvenance* provenance = findProvenance(*mapped, "timeout");
    ASSERT_NE(provenance, nullptr);
    EXPECT_TRUE(provenance->unresolved);
}

TEST(BusWatchdogReverseMapper, NoEnableWritebackReturnsNullopt)
{
    const std::string content = "void SomeOtherFunction(void) { int x = 1; }\n";
    EXPECT_FALSE(scanBusWatchdogBlockFromFileContent(content, "f.c").has_value());
}

TEST(BusWatchdogReverseMapper, SerializesTimeout)
{
    using reverse_import::serializeBusWatchdogFieldValue;

    EXPECT_EQ(serializeBusWatchdogFieldValue("timeout", 9, "7"), "9");
}

TEST(BusWatchdogReverseMapper, SerializeRejectsEnabledFlagsAndNonIntegerTimeout)
{
    using reverse_import::serializeBusWatchdogFieldValue;

    // eeprom_enabled/spifi_enabled/dom3_enabled кодируются вставкой/удалением
    // целой строки, не литералом — сериализатор их сознательно не поддерживает.
    EXPECT_FALSE(serializeBusWatchdogFieldValue("eeprom_enabled", true, "").has_value());
    EXPECT_FALSE(serializeBusWatchdogFieldValue("timeout", "9", "7").has_value());
}
