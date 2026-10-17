//
// Тесты timer32_channel_reverse_mapper (см. parser/reverse_import/
// timer32_channel_reverse_mapper.h). RawInitBlock собран по образцу
// РЕАЛЬНОГО кода mik32_fixed_v3 (main.cpp:555-562 — motor_htimer32_1_channel0,
// main.cpp:610-617 — RGB_B_htimer32_2_channel3) — обнаружено при разборе
// жалобы "должно полностью импортироваться": канал раньше считался
// принципиально непривязываемым к родителю, хотя имя родителя лежит прямо
// в тексте присваивания "TimerInstance = htimer32_1.Instance;".
//
#include <gtest/gtest.h>

#include <timer32_channel_reverse_mapper.h>

#include <algorithm>
#include <string>
#include <vector>

namespace
{

using reverse_import::RawFieldAssignment;
using reverse_import::RawInitBlock;
using reverse_import::SourceLocation;

RawFieldAssignment makeAssignment(const std::string& field_path, const std::string& raw_value_text, int line)
{
    RawFieldAssignment assignment;
    assignment.field_path = field_path;
    assignment.raw_value_text = raw_value_text;
    assignment.location = SourceLocation{"main.cpp", line};
    assignment.full_line_text = "    motor_htimer32_1_channel0." + field_path + " = " + raw_value_text + ";";
    return assignment;
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

TEST(Timer32ChannelReverseMapper, RejectsBlockWithWrongHandleType)
{
    RawInitBlock block;
    block.handle_type = "TIMER32_HandleTypeDef";
    EXPECT_FALSE(mapTimer32ChannelBlock(block).has_value());
}

TEST(Timer32ChannelReverseMapper, ExtractsParentHandleNameFromTimerInstanceText)
{
    // Given: РОВНО текст из main.cpp:555 — "TimerInstance = htimer32_1.Instance;".
    RawInitBlock block;
    block.handle_name = "motor_htimer32_1_channel0";
    block.handle_type = "TIMER32_CHANNEL_HandleTypeDef";
    block.assignments = {
        makeAssignment("TimerInstance", "htimer32_1.Instance", 555),
        makeAssignment("ChannelIndex", "TIMER32_CHANNEL_0", 556),
        makeAssignment("PWM_Invert", "TIMER32_CHANNEL_NON_INVERTED_PWM", 557),
        makeAssignment("Mode", "TIMER32_CHANNEL_MODE_PWM", 558),
        makeAssignment("CaptureEdge", "TIMER32_CHANNEL_CAPTUREEDGE_RISING", 559),
        makeAssignment("OCR", "0", 560),
        makeAssignment("Noise", "TIMER32_CHANNEL_FILTER_OFF", 561),
    };
    block.init_function_name = "HAL_Timer32_Channel_Init";
    block.init_call_args = {"motor_htimer32_1_channel0"};
    block.init_call_location = SourceLocation{"main.cpp", 562};

    // When
    const std::optional<MappedTimer32Channel> mapped = mapTimer32ChannelBlock(block);

    // Then
    ASSERT_TRUE(mapped.has_value());
    EXPECT_EQ(mapped->parent_handle_name, "htimer32_1");
    EXPECT_EQ(mapped->channel_json.at("index"), 0);
    EXPECT_EQ(mapped->channel_json.at("mode"), "pwm");
    EXPECT_EQ(mapped->channel_json.at("ocr"), 0);
    EXPECT_EQ(mapped->channel_json.at("pwm_invert"), false);
    EXPECT_EQ(mapped->channel_json.at("noise_filter"), false);
}

TEST(Timer32ChannelReverseMapper, DifferentInstanceAndChannelIndexFromRealRgbBExample)
{
    // Given: main.cpp:610-617 — RGB_B_htimer32_2_channel3, родитель htimer32_2, index 3.
    RawInitBlock block;
    block.handle_name = "RGB_B_htimer32_2_channel3";
    block.handle_type = "TIMER32_CHANNEL_HandleTypeDef";
    block.assignments = {
        makeAssignment("TimerInstance", "htimer32_2.Instance", 610),
        makeAssignment("ChannelIndex", "TIMER32_CHANNEL_3", 611),
        makeAssignment("PWM_Invert", "TIMER32_CHANNEL_NON_INVERTED_PWM", 612),
        makeAssignment("Mode", "TIMER32_CHANNEL_MODE_PWM", 613),
        makeAssignment("CaptureEdge", "TIMER32_CHANNEL_CAPTUREEDGE_RISING", 614),
        makeAssignment("OCR", "0", 615),
        makeAssignment("Noise", "TIMER32_CHANNEL_FILTER_OFF", 616),
    };

    const std::optional<MappedTimer32Channel> mapped = mapTimer32ChannelBlock(block);
    ASSERT_TRUE(mapped.has_value());
    EXPECT_EQ(mapped->parent_handle_name, "htimer32_2");
    EXPECT_EQ(mapped->channel_json.at("index"), 3);
}

TEST(Timer32ChannelReverseMapper, MissingTimerInstanceLeavesParentEmptyWithNoteInsteadOfNullopt)
{
    // Given: нет присваивания TimerInstance вовсе.
    RawInitBlock block;
    block.handle_name = "orphan_channel";
    block.handle_type = "TIMER32_CHANNEL_HandleTypeDef";
    block.assignments = {
        makeAssignment("ChannelIndex", "TIMER32_CHANNEL_1", 100),
    };

    // When
    const std::optional<MappedTimer32Channel> mapped = mapTimer32ChannelBlock(block);

    // Then: значение ЕСТЬ (не nullopt), просто с пустым parent_handle_name и note.
    ASSERT_TRUE(mapped.has_value());
    EXPECT_TRUE(mapped->parent_handle_name.empty());
    EXPECT_TRUE(hasNoteContaining(mapped->notes, "нет присваивания TimerInstance"));
}

TEST(Timer32ChannelReverseMapper, UnrecognizedTimerInstanceTextLeavesParentEmptyWithNote)
{
    RawInitBlock block;
    block.handle_name = "weird_channel";
    block.handle_type = "TIMER32_CHANNEL_HandleTypeDef";
    block.assignments = {
        makeAssignment("TimerInstance", "TIMER32_1", 100),  // не форма "<handle>.Instance"
    };

    const std::optional<MappedTimer32Channel> mapped = mapTimer32ChannelBlock(block);
    ASSERT_TRUE(mapped.has_value());
    EXPECT_TRUE(mapped->parent_handle_name.empty());
    EXPECT_TRUE(hasNoteContaining(mapped->notes, "не похоже на"));
}

TEST(Timer32ChannelReverseMapper, CaptureModeDoesNotSynthesizeOcrButDoesSynthesizeCaptureEdge)
{
    RawInitBlock block;
    block.handle_name = "capture_channel";
    block.handle_type = "TIMER32_CHANNEL_HandleTypeDef";
    block.assignments = {
        makeAssignment("TimerInstance", "htimer32_1.Instance", 100),
        makeAssignment("ChannelIndex", "TIMER32_CHANNEL_2", 101),
        makeAssignment("Mode", "TIMER32_CHANNEL_MODE_CAPTURE", 102),
    };

    const std::optional<MappedTimer32Channel> mapped = mapTimer32ChannelBlock(block);
    ASSERT_TRUE(mapped.has_value());
    EXPECT_EQ(mapped->channel_json.at("mode"), "capture");
    EXPECT_FALSE(mapped->channel_json.contains("ocr"));
    EXPECT_EQ(mapped->channel_json.at("capture_edge"), "rising");
}

namespace
{
const FieldProvenance* findChannelProvenance(const MappedTimer32Channel& mapped, const std::string& field)
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

// ---- unresolved-политика (feedback_reverse_import_unresolved_fields_policy) ----

TEST(Timer32ChannelReverseMapper, MissingChannelIndexIsUnresolvedNotDefaulted)
{
    RawInitBlock block;
    block.handle_name = "pwm_channel";
    block.handle_type = "TIMER32_CHANNEL_HandleTypeDef";
    block.assignments = {
        makeAssignment("TimerInstance", "htimer32_1.Instance", 1),
        makeAssignment("Mode", "TIMER32_CHANNEL_MODE_PWM", 2),
    };

    const std::optional<MappedTimer32Channel> mapped = mapTimer32ChannelBlock(block);
    ASSERT_TRUE(mapped.has_value());

    const FieldProvenance* provenance = findChannelProvenance(*mapped, "index");
    ASSERT_NE(provenance, nullptr);
    EXPECT_TRUE(provenance->unresolved);
}

TEST(Timer32ChannelReverseMapper, UnparseableOcrIsUnresolvedNotDefaulted)
{
    RawInitBlock block;
    block.handle_name = "pwm_channel";
    block.handle_type = "TIMER32_CHANNEL_HandleTypeDef";
    block.assignments = {
        makeAssignment("TimerInstance", "htimer32_1.Instance", 1),
        makeAssignment("ChannelIndex", "TIMER32_CHANNEL_0", 2),
        makeAssignment("Mode", "TIMER32_CHANNEL_MODE_PWM", 3),
        makeAssignment("OCR", "duty_value", 4),
    };

    const std::optional<MappedTimer32Channel> mapped = mapTimer32ChannelBlock(block);
    ASSERT_TRUE(mapped.has_value());

    const FieldProvenance* provenance = findChannelProvenance(*mapped, "ocr");
    ASSERT_NE(provenance, nullptr);
    EXPECT_TRUE(provenance->unresolved);
}
