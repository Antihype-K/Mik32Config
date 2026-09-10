//
// Тесты timer16_reverse_mapper (см. parser/reverse_import/timer16_reverse_mapper.h).
// RawInitBlock собран руками (не через сканер) по образцу РЕАЛЬНОГО вывода
// СОБСТВЕННОГО кодогена этого конфигуратора — generated_projects/
// rex_project_all4/src/mik32_init.c, TIMER16_0_Init (~строка 143) — то есть
// тот самый случай, из-за которого была обнаружена нехватка этого маппера:
// импорт своего же сгенерированного PlatformIO-проекта терял Timer16 целиком
// ("Неизвестный тип хендла \"Timer16_HandleTypeDef\"").
//
#include <gtest/gtest.h>

#include <timer16_reverse_mapper.h>

#include <algorithm>
#include <string>
#include <vector>

namespace
{

using reverse_import::RawFieldAssignment;
using reverse_import::RawInitBlock;
using reverse_import::RawTrailingCall;
using reverse_import::SourceLocation;

RawFieldAssignment makeAssignment(const std::string& field_path, const std::string& raw_value_text, int line)
{
    RawFieldAssignment assignment;
    assignment.field_path = field_path;
    assignment.raw_value_text = raw_value_text;
    assignment.location = SourceLocation{"mik32_init.c", line};
    assignment.full_line_text = "    htimer16_0." + field_path + " = " + raw_value_text + ";";
    return assignment;
}

RawInitBlock buildRealTimer16_0Block()
{
    RawInitBlock block;
    block.handle_name = "htimer16_0";
    block.handle_type = "Timer16_HandleTypeDef";
    block.assignments = {
        makeAssignment("Instance", "TIMER16_0", 143),
        makeAssignment("Clock.Source", "TIMER16_SOURCE_INTERNAL_SYSTEM", 145),
        makeAssignment("CountMode", "TIMER16_COUNTMODE_INTERNAL", 146),
        makeAssignment("Clock.Prescaler", "TIMER16_PRESCALER_16", 147),
    };
    block.init_function_name = "HAL_Timer16_Init";
    block.init_call_args = {"htimer16_0"};
    block.init_call_location = SourceLocation{"mik32_init.c", 164};
    return block;
}

bool hasNoteContaining(const std::vector<std::string>& notes, const std::string& needle)
{
    for (const std::string& note : notes)
    {
        if (note.find(needle) != std::string::npos)
        {
            return true;
        }
    }
    return false;
}

} // namespace

using namespace reverse_import;

TEST(Timer16ReverseMapper, RejectsBlockWithWrongHandleType)
{
    RawInitBlock block;
    block.handle_type = "TIMER32_HandleTypeDef";
    EXPECT_FALSE(mapTimer16Block(block).has_value());
}

TEST(Timer16ReverseMapper, MapsInstanceAndPrescalerFromAssignments)
{
    RawInitBlock block = buildRealTimer16_0Block();
    const std::optional<MappedBlock> mapped = mapTimer16Block(block);
    ASSERT_TRUE(mapped.has_value());
    EXPECT_EQ(mapped->json_block.at("type"), "timer16");
    EXPECT_EQ(mapped->json_block.at("instance"), 0);
    EXPECT_EQ(mapped->json_block.at("prescaler"), 16);
}

TEST(Timer16ReverseMapper, DerivesModeArrCmpFromStartPwmTrailingCallAndMarksReadOnly)
{
    // Given: ровно вызов из rex_project_all4 — "HAL_Timer16_StartPWM(&htimer16_0, 999, 499);".
    RawInitBlock block = buildRealTimer16_0Block();
    block.trailing_calls.push_back(RawTrailingCall{
        "HAL_Timer16_StartPWM", {"htimer16_0", "999", "499"},
        SourceLocation{"mik32_init.c", 166},
        "    HAL_Timer16_StartPWM(&htimer16_0, 999, 499);"});

    // When
    const std::optional<MappedBlock> mapped = mapTimer16Block(block);

    // Then
    ASSERT_TRUE(mapped.has_value());
    EXPECT_EQ(mapped->json_block.at("mode"), "period");
    EXPECT_EQ(mapped->json_block.at("arr"), 999);
    EXPECT_EQ(mapped->json_block.at("cmp"), 499);

    for (const std::string& field : {std::string("mode"), std::string("arr"), std::string("cmp")})
    {
        const auto provenance = std::find_if(
            mapped->provenance.begin(), mapped->provenance.end(),
            [&](const FieldProvenance& p) { return p.json_field == field; });
        ASSERT_NE(provenance, mapped->provenance.end());
        EXPECT_FALSE(provenance->editable) << field;
    }
    EXPECT_TRUE(hasNoteContaining(mapped->notes, "HAL_Timer16_StartPWM"));
}

TEST(Timer16ReverseMapper, CounterStartModeHasNoCompareArgument)
{
    RawInitBlock block = buildRealTimer16_0Block();
    block.trailing_calls.push_back(RawTrailingCall{
        "HAL_Timer16_Counter_Start", {"htimer16_0", "65535"},
        SourceLocation{"mik32_init.c", 166}, "    HAL_Timer16_Counter_Start(&htimer16_0, 65535);"});

    const std::optional<MappedBlock> mapped = mapTimer16Block(block);
    ASSERT_TRUE(mapped.has_value());
    EXPECT_EQ(mapped->json_block.at("mode"), "external_event_count");
    EXPECT_EQ(mapped->json_block.at("arr"), 65535);
    EXPECT_EQ(mapped->json_block.at("cmp"), 0);
}

TEST(Timer16ReverseMapper, NoRecognizedTrailingCallFallsBackToPlaceholderWithNote)
{
    RawInitBlock block = buildRealTimer16_0Block();  // без trailing_calls вовсе

    const std::optional<MappedBlock> mapped = mapTimer16Block(block);
    ASSERT_TRUE(mapped.has_value());
    EXPECT_EQ(mapped->json_block.at("mode"), "period");
    EXPECT_EQ(mapped->json_block.at("arr"), 0);
    EXPECT_EQ(mapped->json_block.at("cmp"), 0);
    EXPECT_TRUE(hasNoteContaining(mapped->notes, "не найден вызов"));
}

TEST(Timer16ReverseMapper, IgnoresUnrelatedTrailingCallsReferencingTheSameHandle)
{
    // "Шум" — вызов, использующий хендл не для смены mode/arr/cmp.
    RawInitBlock block = buildRealTimer16_0Block();
    block.trailing_calls.push_back(RawTrailingCall{
        "HAL_Timer16_Value_Clear", {"htimer16_0"},
        SourceLocation{"mik32_init.c", 165}, "    HAL_Timer16_Value_Clear(&htimer16_0);"});

    const std::optional<MappedBlock> mapped = mapTimer16Block(block);
    ASSERT_TRUE(mapped.has_value());
    EXPECT_TRUE(hasNoteContaining(mapped->notes, "не найден вызов"));
}

TEST(Timer16ReverseMapper, SerializeInstanceAndPrescalerRoundTrip)
{
    EXPECT_EQ(serializeTimer16FieldValue("instance", 2, ""), "TIMER16_2");
    EXPECT_EQ(serializeTimer16FieldValue("prescaler", 64, ""), "TIMER16_PRESCALER_64");
    EXPECT_FALSE(serializeTimer16FieldValue("prescaler", 3, "").has_value());  // 3 не степень двойки из набора
}

TEST(Timer16ReverseMapper, SerializeModeArrCmpAlwaysNullopt)
{
    EXPECT_FALSE(serializeTimer16FieldValue("mode", "one_shot", "").has_value());
    EXPECT_FALSE(serializeTimer16FieldValue("arr", 100, "").has_value());
    EXPECT_FALSE(serializeTimer16FieldValue("cmp", 50, "").has_value());
}
