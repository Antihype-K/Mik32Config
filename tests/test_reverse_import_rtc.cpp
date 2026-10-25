//
// Тесты rtc_reverse_mapper.{h,cpp} (parser/reverse_import/). RTC — особый
// случай: нет HAL_RTC_Init, время/дата/будильник приходят через локальные
// структуры-значения (sTime/sDate/sAlarm), читаемые local_struct_field_
// resolver.h прямо с диска — поэтому, как и у function_param_resolver,
// тесты пишут реальный временный файл (TempDir), а не строят RawInitBlock
// целиком в памяти.
//
#include <gtest/gtest.h>

#include <fstream>

#include "../parser/reverse_import/rtc_reverse_mapper.h"

using reverse_import::FieldProvenance;
using reverse_import::MappedBlock;
using reverse_import::RawFieldAssignment;
using reverse_import::RawInitBlock;
using reverse_import::RawTrailingCall;
using reverse_import::SourceLocation;
using reverse_import::mapRtcBlock;

namespace
{

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

// Воспроизводит docs/examples/mik32-examples/Hal_RTC/src/main.c:102-145.
TEST(RtcReverseMapper, ResolvesTimeDateAndAlarmFromLocalStructs)
{
    const std::string path = ::testing::TempDir() + "rtc_hal_rtc_test.c";
    {
        std::ofstream out(path);
        out <<
            "static void RTC_Init(void)\n"
            "{\n"
            "    RTC_TimeTypeDef sTime = {0};\n"
            "    RTC_DateTypeDef sDate = {0};\n"
            "    RTC_AlarmTypeDef sAlarm = {0};\n"
            "\n"
            "    hrtc.Instance = RTC;\n"
            "    HAL_RTC_Disable(&hrtc);\n"
            "\n"
            "    sTime.Dow = RTC_WEEKDAY_TUESDAY;\n"
            "    sTime.Hours = 20;\n"
            "    sTime.Minutes = 30;\n"
            "    sTime.Seconds = 0;\n"
            "    HAL_RTC_SetTime(&hrtc, &sTime);\n"
            "\n"
            "    sDate.Century = 21;\n"
            "    sDate.Day = 19;\n"
            "    sDate.Month = RTC_MONTH_JULY;\n"
            "    sDate.Year = 22;\n"
            "    HAL_RTC_SetDate(&hrtc, &sDate);\n"
            "\n"
            "    sAlarm.AlarmTime.Dow = RTC_WEEKDAY_TUESDAY;\n"
            "    sAlarm.AlarmTime.Hours = 20;\n"
            "    sAlarm.AlarmTime.Minutes = 30;\n"
            "    sAlarm.AlarmTime.Seconds = 5;\n"
            "    sAlarm.AlarmDate.Century = 21;\n"
            "    sAlarm.AlarmDate.Day = 19;\n"
            "    sAlarm.AlarmDate.Month = RTC_MONTH_JULY;\n"
            "    sAlarm.AlarmDate.Year = 22;\n"
            "    sAlarm.MaskAlarmTime = RTC_TALRM_CDOW_M | RTC_TALRM_CH_M | RTC_TALRM_CM_M | RTC_TALRM_CS_M;\n"
            "    sAlarm.MaskAlarmDate = RTC_DALRM_CC_M | RTC_DALRM_CD_M | RTC_DALRM_CM_M | RTC_DALRM_CY_M;\n"
            "    HAL_RTC_SetAlarm(&hrtc, &sAlarm);\n"
            "\n"
            "    HAL_RTC_Enable(&hrtc);\n"
            "}\n";
    }

    RawInitBlock block;
    block.handle_name = "hrtc";
    block.handle_type = "RTC_HandleTypeDef";
    block.init_function_name = "HAL_RTC_Enable";
    block.init_call_args = {"hrtc"};
    block.init_call_location = SourceLocation{path, 30};
    block.assignments = {
        RawFieldAssignment{"Instance", "RTC", SourceLocation{path, 7}, "    hrtc.Instance = RTC;"},
    };
    block.trailing_calls = {
        RawTrailingCall{"HAL_RTC_Disable", {"hrtc"}, SourceLocation{path, 8}, ""},
        RawTrailingCall{"HAL_RTC_SetTime", {"hrtc", "sTime"}, SourceLocation{path, 14}, ""},
        RawTrailingCall{"HAL_RTC_SetDate", {"hrtc", "sDate"}, SourceLocation{path, 20}, ""},
        RawTrailingCall{"HAL_RTC_SetAlarm", {"hrtc", "sAlarm"}, SourceLocation{path, 29}, ""},
    };

    const std::optional<MappedBlock> mapped = mapRtcBlock(block);
    ASSERT_TRUE(mapped.has_value());

    EXPECT_EQ(mapped->json_block.at("time").at("dow").get<int>(), 2);
    EXPECT_EQ(mapped->json_block.at("time").at("hours").get<int>(), 20);
    EXPECT_EQ(mapped->json_block.at("time").at("minutes").get<int>(), 30);
    EXPECT_EQ(mapped->json_block.at("time").at("seconds").get<int>(), 0);

    EXPECT_EQ(mapped->json_block.at("date").at("century").get<int>(), 21);
    EXPECT_EQ(mapped->json_block.at("date").at("day").get<int>(), 19);
    EXPECT_EQ(mapped->json_block.at("date").at("month").get<int>(), 7);
    EXPECT_EQ(mapped->json_block.at("date").at("year").get<int>(), 22);

    ASSERT_TRUE(mapped->json_block.contains("alarm"));
    EXPECT_EQ(mapped->json_block.at("alarm").at("time").at("seconds").get<int>(), 5);
    EXPECT_EQ(mapped->json_block.at("alarm").at("date").at("day").get<int>(), 19);

    const std::vector<std::string> expected_fields = {
        "dow", "hours", "minutes", "seconds", "century", "day", "month", "year"};
    std::vector<std::string> compare_fields =
        mapped->json_block.at("alarm").at("compare_fields").get<std::vector<std::string>>();
    for (const std::string& field : expected_fields)
    {
        EXPECT_NE(std::find(compare_fields.begin(), compare_fields.end(), field), compare_fields.end())
            << "missing " << field;
    }

    for (const char* field : {"time.dow", "time.hours", "date.century", "alarm.time.seconds"})
    {
        const FieldProvenance* provenance = findProvenance(*mapped, field);
        ASSERT_NE(provenance, nullptr) << field;
        EXPECT_FALSE(provenance->unresolved) << field;
    }
}

// Нет HAL_RTC_SetDate в trailing_calls — date-поля должны стать unresolved,
// а не угаданным дефолтом (feedback_reverse_import_unresolved_fields_policy).
TEST(RtcReverseMapper, MissingSetDateCallMarksDateFieldsUnresolved)
{
    const std::string path = ::testing::TempDir() + "rtc_no_date_test.c";
    {
        std::ofstream out(path);
        out <<
            "static void RTC_Init(void)\n"
            "{\n"
            "    RTC_TimeTypeDef sTime = {0};\n"
            "    hrtc.Instance = RTC;\n"
            "    sTime.Dow = RTC_WEEKDAY_MONDAY;\n"
            "    sTime.Hours = 0;\n"
            "    sTime.Minutes = 0;\n"
            "    sTime.Seconds = 0;\n"
            "    HAL_RTC_SetTime(&hrtc, &sTime);\n"
            "    HAL_RTC_Enable(&hrtc);\n"
            "}\n";
    }

    RawInitBlock block;
    block.handle_name = "hrtc";
    block.handle_type = "RTC_HandleTypeDef";
    block.init_function_name = "HAL_RTC_Enable";
    block.init_call_args = {"hrtc"};
    block.init_call_location = SourceLocation{path, 10};
    block.assignments = {
        RawFieldAssignment{"Instance", "RTC", SourceLocation{path, 4}, "    hrtc.Instance = RTC;"},
    };
    block.trailing_calls = {
        RawTrailingCall{"HAL_RTC_SetTime", {"hrtc", "sTime"}, SourceLocation{path, 9}, ""},
    };

    const std::optional<MappedBlock> mapped = mapRtcBlock(block);
    ASSERT_TRUE(mapped.has_value());

    EXPECT_FALSE(mapped->json_block.contains("alarm"));

    const FieldProvenance* century = findProvenance(*mapped, "date.century");
    ASSERT_NE(century, nullptr);
    EXPECT_TRUE(century->unresolved);

    const FieldProvenance* dow = findProvenance(*mapped, "time.dow");
    ASSERT_NE(dow, nullptr);
    EXPECT_FALSE(dow->unresolved);
}

TEST(RtcReverseMapper, RejectsNonRtcHandleType)
{
    RawInitBlock block;
    block.handle_type = "I2C_HandleTypeDef";
    EXPECT_FALSE(mapRtcBlock(block).has_value());
}

TEST(RtcReverseMapper, SerializesIntFieldsPreservingMacroOrLiteralStyle)
{
    using reverse_import::serializeRtcFieldValue;

    // dow: оригинал был макросом RTC_WEEKDAY_* -> пишем макрос того же семейства.
    EXPECT_EQ(serializeRtcFieldValue("dow", 3, "RTC_WEEKDAY_MONDAY"), "RTC_WEEKDAY_WEDNESDAY");
    // dow: оригинал был голым литералом -> остаётся литералом.
    EXPECT_EQ(serializeRtcFieldValue("dow", 3, "1"), "3");
    // month: аналогично через RTC_MONTH_*.
    EXPECT_EQ(serializeRtcFieldValue("month", 12, "RTC_MONTH_JANUARY"), "RTC_MONTH_DECEMBER");
    // hours/minutes/seconds/century/day/year: всегда голый литерал.
    EXPECT_EQ(serializeRtcFieldValue("hours", 23, "0"), "23");
    EXPECT_EQ(serializeRtcFieldValue("year", 26, "25"), "26");
}

TEST(RtcReverseMapper, SerializesInterruptEnabledAsAlarmIrqMacro)
{
    using reverse_import::serializeRtcFieldValue;

    EXPECT_EQ(serializeRtcFieldValue("interrupt_enabled", true, "RTC_ALARM_IRQ_DISABLE"),
              "RTC_ALARM_IRQ_ENABLE");
    EXPECT_EQ(serializeRtcFieldValue("interrupt_enabled", false, "RTC_ALARM_IRQ_ENABLE"),
              "RTC_ALARM_IRQ_DISABLE");
}

TEST(RtcReverseMapper, SerializeRejectsOutOfRangeWeekdayAndWrongType)
{
    using reverse_import::serializeRtcFieldValue;

    // dow=0 вне диапазона 1..7 (weekday-макросов нет для 0) — не угадываем.
    EXPECT_FALSE(serializeRtcFieldValue("dow", 0, "RTC_WEEKDAY_MONDAY").has_value());
    EXPECT_FALSE(serializeRtcFieldValue("hours", "23", "0").has_value());
    EXPECT_FALSE(serializeRtcFieldValue("interrupt_enabled", 1, "RTC_ALARM_IRQ_ENABLE").has_value());
    // compare_fields (массив из нескольких ORed-макросов) не поддерживается вовсе.
    EXPECT_FALSE(serializeRtcFieldValue("compare_fields", nlohmann::json::array(), "").has_value());
}
