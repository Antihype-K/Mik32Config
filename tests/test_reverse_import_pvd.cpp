//
// Тесты pvd_reverse_mapper.{h,cpp} (parser/reverse_import/). У PVD нет
// HAL-обёртки/HandleTypeDef (Тир 3, регистровая работа) и нет вендорного
// примера для сверки — единственный известный проекту шаблон кода это
// codegen/project/pvd_contribution.cpp, поэтому тест строит синтетический
// файл по этому шаблону, а не берёт реальный пример из docs/examples/.
//
#include <gtest/gtest.h>

#include "../parser/reverse_import/pvd_reverse_mapper.h"

using reverse_import::FieldProvenance;
using reverse_import::MappedBlock;
using reverse_import::scanPvdBlockFromFileContent;

namespace
{

// Соответствует форме, генерируемой pvd_contribution.cpp (под_threshold=3,
// over_threshold=9, digital_filter_value=42, калибровка включена).
const char* kRealShapeFile =
    "void PVD_Init(void)\n"
    "{\n"
    "    __HAL_PCC_PVD_CONTROL_CLK_ENABLE();\n"
    "\n"
    "    PVD_CONTROL->DPF_VALUE = 42;\n"
    "\n"
    "    uint32_t pvd_config = PVD_CONTROL->CONFIG;\n"
    "    pvd_config &= ~(PVD_CONFIG_UNDER_THRESH_M | PVD_CONFIG_OVER_THRESH_M | PVD_CONFIG_EN_VREFCLB_M);\n"
    "    pvd_config |= (((uint32_t)3 << PVD_CONFIG_UNDER_THRESH_S) & PVD_CONFIG_UNDER_THRESH_M);\n"
    "    pvd_config |= (((uint32_t)9 << PVD_CONFIG_OVER_THRESH_S) & PVD_CONFIG_OVER_THRESH_M);\n"
    "    pvd_config |= PVD_CONFIG_EN_VREFCLB_M;\n"
    "    PVD_CONTROL->CONFIG = pvd_config;\n"
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

TEST(PvdReverseMapper, ResolvesThresholdsFilterAndCalibrationFromRealShape)
{
    const std::optional<MappedBlock> mapped =
        scanPvdBlockFromFileContent(kRealShapeFile, "pvd_contribution_generated.c");
    ASSERT_TRUE(mapped.has_value());

    EXPECT_EQ(mapped->json_block.at("under_threshold").get<int>(), 3);
    EXPECT_EQ(mapped->json_block.at("over_threshold").get<int>(), 9);
    EXPECT_EQ(mapped->json_block.at("digital_filter_value").get<std::uint32_t>(), 42u);
    EXPECT_EQ(mapped->json_block.at("enable_reference_calibration").get<bool>(), true);

    for (const char* field : {"under_threshold", "over_threshold", "digital_filter_value"})
    {
        const FieldProvenance* provenance = findProvenance(*mapped, field);
        ASSERT_NE(provenance, nullptr) << field;
        EXPECT_FALSE(provenance->unresolved) << field;
    }
}

TEST(PvdReverseMapper, CalibrationOffWhenVrefClbLineAbsent)
{
    std::string content = kRealShapeFile;
    const std::string calib_line = "    pvd_config |= PVD_CONFIG_EN_VREFCLB_M;\n";
    content.erase(content.find(calib_line), calib_line.size());

    const std::optional<MappedBlock> mapped = scanPvdBlockFromFileContent(content, "f.c");
    ASSERT_TRUE(mapped.has_value());
    EXPECT_EQ(mapped->json_block.at("enable_reference_calibration").get<bool>(), false);
}

TEST(PvdReverseMapper, NonLiteralThresholdExpressionMarksUnresolved)
{
    std::string content = kRealShapeFile;
    const std::string literal_line =
        "    pvd_config |= (((uint32_t)3 << PVD_CONFIG_UNDER_THRESH_S) & PVD_CONFIG_UNDER_THRESH_M);\n";
    const std::string expr_line =
        "    pvd_config |= (((uint32_t)compute_threshold() << PVD_CONFIG_UNDER_THRESH_S) & "
        "PVD_CONFIG_UNDER_THRESH_M);\n";
    const std::size_t pos = content.find(literal_line);
    ASSERT_NE(pos, std::string::npos);
    content.replace(pos, literal_line.size(), expr_line);

    const std::optional<MappedBlock> mapped = scanPvdBlockFromFileContent(content, "f.c");
    ASSERT_TRUE(mapped.has_value());

    const FieldProvenance* provenance = findProvenance(*mapped, "under_threshold");
    ASSERT_NE(provenance, nullptr);
    EXPECT_TRUE(provenance->unresolved);
}

TEST(PvdReverseMapper, NoConfigWritebackReturnsNullopt)
{
    const std::string content = "void SomeOtherFunction(void) { int x = 1; }\n";
    EXPECT_FALSE(scanPvdBlockFromFileContent(content, "f.c").has_value());
}
