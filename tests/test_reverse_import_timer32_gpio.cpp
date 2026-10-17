//
// Тесты для timer32_reverse_mapper и gpio_reverse_mapper (см. parser/reverse_import/).
// Сценарии (a)-(b) собраны напрямую из реальных строк
// docs/сделать/mik32_fixed_v3/mik32/controlFirmware/src/main.cpp
// (Timer32_0_Init, ~строка 522; блок ручной настройки GPIO, ~строка 625) —
// RawInitBlock здесь строится руками, а не через reverse_import_scanner,
// чтобы тест маппера не зависел от поведения сканера.
//

#include <gtest/gtest.h>

#include <gpio_reverse_mapper.h>
#include <timer32_reverse_mapper.h>

namespace
{

using reverse_import::RawFieldAssignment;
using reverse_import::RawInitBlock;
using reverse_import::SourceLocation;

RawFieldAssignment makeAssignment(const std::string& field_path, const std::string& raw_value_text,
                                   int line, const std::string& full_line_text)
{
    RawFieldAssignment assignment;
    assignment.field_path = field_path;
    assignment.raw_value_text = raw_value_text;
    assignment.location = SourceLocation{"main.cpp", line};
    assignment.full_line_text = full_line_text;
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

// (a) Timer32_0_Init целиком: Top = 0xFFFFFFFF (hex литерал) должен
// распознаться как 4294967295, а Clock.Prescaler = prescaler (локальная
// переменная, не литерал) должен стать editable=false с понятной note.
TEST(Timer32ReverseMapper, MapsRealTimer32_0_InitBlock)
{
    RawInitBlock block;
    block.handle_name = "htimer32_0";
    block.handle_type = "TIMER32_HandleTypeDef";
    block.init_function_name = "HAL_Timer32_Init";
    block.init_call_args = {"htimer32_0"};
    block.init_call_location = SourceLocation{"main.cpp", 534};
    block.assignments = {
        makeAssignment("Instance", "TIMER32_0", 527, "    htimer32_0.Instance = TIMER32_0;"),
        makeAssignment("Top", "0xFFFFFFFF", 528, "    htimer32_0.Top = 0xFFFFFFFF;"),
        makeAssignment("State", "TIMER32_STATE_DISABLE", 529, "    htimer32_0.State = TIMER32_STATE_DISABLE;"),
        makeAssignment("Clock.Source", "TIMER32_SOURCE_PRESCALER", 530, "    htimer32_0.Clock.Source = TIMER32_SOURCE_PRESCALER;"),
        makeAssignment("Clock.Prescaler", "prescaler", 531, "    htimer32_0.Clock.Prescaler = prescaler;"),
        makeAssignment("InterruptMask", "0", 532, "    htimer32_0.InterruptMask = 0;"),
        makeAssignment("CountMode", "TIMER32_COUNTMODE_FORWARD", 533, "    htimer32_0.CountMode = TIMER32_COUNTMODE_FORWARD;"),
    };

    std::optional<reverse_import::MappedBlock> mapped = reverse_import::mapTimer32Block(block);
    ASSERT_TRUE(mapped.has_value());

    EXPECT_EQ((*mapped).json_block.at("type").get<std::string>(), "timer32");
    EXPECT_EQ((*mapped).json_block.at("instance").get<int>(), 0);
    EXPECT_EQ((*mapped).json_block.at("top").get<std::uint32_t>(), 4294967295u);
    EXPECT_EQ((*mapped).json_block.at("count_mode").get<std::string>(), "forward");
    EXPECT_EQ((*mapped).json_block.at("prescaler").get<int>(), 0);

    bool found_prescaler_provenance = false;
    for (const reverse_import::FieldProvenance& provenance : (*mapped).provenance)
    {
        if (provenance.json_field == "prescaler")
        {
            found_prescaler_provenance = true;
            EXPECT_FALSE(provenance.editable);
        }
        if (provenance.json_field == "top")
        {
            EXPECT_TRUE(provenance.editable);
        }
    }
    EXPECT_TRUE(found_prescaler_provenance);
    EXPECT_TRUE(hasNoteContaining((*mapped).notes, "Clock.Prescaler"));
    EXPECT_TRUE(hasNoteContaining((*mapped).notes, "prescaler"));
    EXPECT_TRUE(hasNoteContaining((*mapped).notes, "Timer32_0_Init"));
}

// TIMER32_CHANNEL_HandleTypeDef — сознательно не поддержан (см. .h) —
// mapTimer32Block должен вернуть nullopt, а не выдумывать привязку канала.
TEST(Timer32ReverseMapper, ReturnsNulloptForChannelHandleType)
{
    RawInitBlock block;
    block.handle_name = "motor_htimer32_1_channel0";
    block.handle_type = "TIMER32_CHANNEL_HandleTypeDef";
    block.init_function_name = "HAL_Timer32_Channel_Init";
    block.init_call_args = {"motor_htimer32_1_channel0"};

    EXPECT_FALSE(reverse_import::mapTimer32Block(block).has_value());
}

// serializeTimer32FieldValue: Top в hex-стиле должен и обратно печататься в
// hex (сохраняем стиль исходника), prescaler без hex-подсказки — в decimal.
TEST(Timer32ReverseMapper, SerializesTopAsHexAndPrescalerAsDecimal)
{
    std::optional<std::string> top = reverse_import::serializeTimer32FieldValue(
        "top", nlohmann::json(65535), "0xFFFFFFFF");
    ASSERT_TRUE(top.has_value());
    EXPECT_EQ(*top, "0xFFFF");

    std::optional<std::string> prescaler = reverse_import::serializeTimer32FieldValue(
        "prescaler", nlohmann::json(31), "prescaler");
    ASSERT_TRUE(prescaler.has_value());
    EXPECT_EQ(*prescaler, "31");

    std::optional<std::string> count_mode = reverse_import::serializeTimer32FieldValue(
        "count_mode", nlohmann::json("bidirectional"), "TIMER32_COUNTMODE_FORWARD");
    ASSERT_TRUE(count_mode.has_value());
    EXPECT_EQ(*count_mode, "TIMER32_COUNTMODE_BIDIRECTIONAL");

    EXPECT_FALSE(reverse_import::serializeTimer32FieldValue("channels", nlohmann::json::array(), "").has_value());
}

// ---- unresolved-политика (feedback_reverse_import_unresolved_fields_policy) ----

TEST(Timer32ReverseMapper, MissingInstanceIsUnresolvedNotDefaulted)
{
    RawInitBlock block;
    block.handle_name = "htimer32_0";
    block.handle_type = "TIMER32_HandleTypeDef";
    block.assignments = {
        makeAssignment("Top", "100", 1, "    htimer32_0.Top = 100;"),
    };

    std::optional<reverse_import::MappedBlock> mapped = reverse_import::mapTimer32Block(block);
    ASSERT_TRUE(mapped.has_value());

    bool found = false;
    for (const reverse_import::FieldProvenance& provenance : (*mapped).provenance)
    {
        if (provenance.json_field == "instance")
        {
            found = true;
            EXPECT_TRUE(provenance.unresolved);
        }
    }
    EXPECT_TRUE(found);
}

TEST(Timer32ReverseMapper, UnparseablePrescalerExpressionIsUnresolvedNotDefaulted)
{
    RawInitBlock block;
    block.handle_name = "htimer32_0";
    block.handle_type = "TIMER32_HandleTypeDef";
    block.assignments = {
        makeAssignment("Instance", "TIMER32_0", 1, "    htimer32_0.Instance = TIMER32_0;"),
        makeAssignment("Clock.Prescaler", "prescaler", 2, "    htimer32_0.Clock.Prescaler = prescaler;"),
    };

    std::optional<reverse_import::MappedBlock> mapped = reverse_import::mapTimer32Block(block);
    ASSERT_TRUE(mapped.has_value());

    bool found = false;
    for (const reverse_import::FieldProvenance& provenance : (*mapped).provenance)
    {
        if (provenance.json_field == "prescaler")
        {
            found = true;
            EXPECT_TRUE(provenance.unresolved);
        }
    }
    EXPECT_TRUE(found);
}

// (b) один пин GPIO из реального файла: GPIO_PIN_6, GPIO_1, output, pull_none.
// instance должен взяться из аргумента вызова (GPIO_1 -> 1), а не из полей
// структуры (в GPIO_InitTypeDef физически нет поля "порт").
TEST(GpioReverseMapper, MapsRealSinglePinOutputBlock)
{
    RawInitBlock block;
    block.handle_name = "GPIO_InitStruct";
    block.handle_type = "GPIO_InitTypeDef";
    block.init_function_name = "HAL_GPIO_Init";
    block.init_call_args = {"GPIO_1", "GPIO_InitStruct"};
    block.init_call_location = SourceLocation{"main.cpp", 641};
    block.assignments = {
        makeAssignment("Pin", "GPIO_PIN_6", 638, "    GPIO_InitStruct.Pin = GPIO_PIN_6;"),
        makeAssignment("Mode", "HAL_GPIO_MODE_GPIO_OUTPUT", 639, "    GPIO_InitStruct.Mode = HAL_GPIO_MODE_GPIO_OUTPUT;"),
        makeAssignment("Pull", "HAL_GPIO_PULL_NONE", 640, "    GPIO_InitStruct.Pull = HAL_GPIO_PULL_NONE;"),
    };

    std::optional<reverse_import::MappedBlock> mapped = reverse_import::mapGpioBlock(block);
    ASSERT_TRUE(mapped.has_value());

    EXPECT_EQ((*mapped).json_block.at("type").get<std::string>(), "gpio");
    EXPECT_EQ((*mapped).json_block.at("instance").get<int>(), 1);
    EXPECT_EQ((*mapped).json_block.at("mode").get<std::string>(), "output");
    EXPECT_EQ((*mapped).json_block.at("pull").get<std::string>(), "none");
    ASSERT_TRUE((*mapped).json_block.at("pins").is_array());
    ASSERT_EQ((*mapped).json_block.at("pins").size(), 1u);
    EXPECT_EQ((*mapped).json_block.at("pins")[0].get<int>(), 6);

    // (c) drive_strength не задан в исходнике этого блока — дефолт 4mA,
    // editable=false, понятная note.
    EXPECT_EQ((*mapped).json_block.at("drive_strength").get<std::string>(), "4mA");
    bool found_ds_provenance = false;
    for (const reverse_import::FieldProvenance& provenance : (*mapped).provenance)
    {
        if (provenance.json_field == "drive_strength")
        {
            found_ds_provenance = true;
            EXPECT_FALSE(provenance.editable);
        }
        if (provenance.json_field == "instance")
        {
            // instance приходит из аргумента вызова, не из поля структуры -
            // в этой версии не редактируется через reverse_patch_writer.
            EXPECT_FALSE(provenance.editable);
        }
    }
    EXPECT_TRUE(found_ds_provenance);
    EXPECT_TRUE(hasNoteContaining((*mapped).notes, "drive_strength"));
    EXPECT_TRUE(hasNoteContaining((*mapped).notes, "4mA"));
}

// Битовое ИЛИ нескольких пинов в одном блоке (GPIO_PIN_0 | GPIO_PIN_1)
// должно распарситься в массив из нескольких чисел.
TEST(GpioReverseMapper, ParsesBitwiseOrPinsIntoArray)
{
    RawInitBlock block;
    block.handle_name = "GPIO_InitStruct";
    block.handle_type = "GPIO_InitTypeDef";
    block.init_function_name = "HAL_GPIO_Init";
    block.init_call_args = {"GPIO_0", "GPIO_InitStruct"};
    block.assignments = {
        makeAssignment("Pin", "GPIO_PIN_0 | GPIO_PIN_1", 10, "GPIO_InitStruct.Pin = GPIO_PIN_0 | GPIO_PIN_1;"),
        makeAssignment("Mode", "HAL_GPIO_MODE_GPIO_INPUT", 11, "GPIO_InitStruct.Mode = HAL_GPIO_MODE_GPIO_INPUT;"),
        makeAssignment("Pull", "HAL_GPIO_PULL_UP", 12, "GPIO_InitStruct.Pull = HAL_GPIO_PULL_UP;"),
    };

    std::optional<reverse_import::MappedBlock> mapped = reverse_import::mapGpioBlock(block);
    ASSERT_TRUE(mapped.has_value());
    ASSERT_TRUE((*mapped).json_block.at("pins").is_array());
    ASSERT_EQ((*mapped).json_block.at("pins").size(), 2u);
    EXPECT_EQ((*mapped).json_block.at("pins")[0].get<int>(), 0);
    EXPECT_EQ((*mapped).json_block.at("pins")[1].get<int>(), 1);
}

// Нераспознаваемый Pin (не GPIO_PIN_<N> и не побитовое ИЛИ таких же) не
// должен приводить к падению — подставляется явно отмеченная заглушка [0].
TEST(GpioReverseMapper, FallsBackToPlaceholderPinsWhenUnrecognizable)
{
    RawInitBlock block;
    block.handle_name = "GPIO_InitStruct";
    block.handle_type = "GPIO_InitTypeDef";
    block.init_function_name = "HAL_GPIO_Init";
    block.init_call_args = {"GPIO_0", "GPIO_InitStruct"};
    block.assignments = {
        makeAssignment("Pin", "some_runtime_pin_variable", 10, "GPIO_InitStruct.Pin = some_runtime_pin_variable;"),
    };

    std::optional<reverse_import::MappedBlock> mapped = reverse_import::mapGpioBlock(block);
    ASSERT_TRUE(mapped.has_value());
    ASSERT_TRUE((*mapped).json_block.at("pins").is_array());
    ASSERT_EQ((*mapped).json_block.at("pins").size(), 1u);
    EXPECT_EQ((*mapped).json_block.at("pins")[0].get<int>(), 0);
    EXPECT_TRUE(hasNoteContaining((*mapped).notes, "не удалось разобрать номер"));
}

// Пустой init_call_args / нераспознанный порт -> instance=0 с note, блок
// всё равно возвращается (не nullopt) — цель фичи "вычленять ВСЮ периферию".
TEST(GpioReverseMapper, FallsBackToInstanceZeroWhenPortArgMissing)
{
    RawInitBlock block;
    block.handle_name = "GPIO_InitStruct";
    block.handle_type = "GPIO_InitTypeDef";
    block.init_function_name = "HAL_GPIO_Init";
    block.init_call_args = {};
    block.assignments = {
        makeAssignment("Pin", "GPIO_PIN_2", 1, "GPIO_InitStruct.Pin = GPIO_PIN_2;"),
    };

    std::optional<reverse_import::MappedBlock> mapped = reverse_import::mapGpioBlock(block);
    ASSERT_TRUE(mapped.has_value());
    EXPECT_EQ((*mapped).json_block.at("instance").get<int>(), 0);
    EXPECT_TRUE(hasNoteContaining((*mapped).notes,
        "не удалось определить instance порта из аргумента HAL_GPIO_Init"));
}

// (d) serializeGpioFieldValue round-trip для pins/mode/pull.
TEST(GpioReverseMapper, SerializeFieldValueRoundTripsPinsModePull)
{
    std::optional<std::string> pins = reverse_import::serializeGpioFieldValue(
        "pins", nlohmann::json::array({0, 1}), "GPIO_PIN_0 | GPIO_PIN_1");
    ASSERT_TRUE(pins.has_value());
    EXPECT_EQ(*pins, "GPIO_PIN_0 | GPIO_PIN_1");

    // Подать результат обратно как raw_value_text реального присваивания и
    // убедиться, что mapGpioBlock распарсит его в те же [0, 1].
    RawInitBlock block;
    block.handle_type = "GPIO_InitTypeDef";
    block.init_function_name = "HAL_GPIO_Init";
    block.init_call_args = {"GPIO_0", "GPIO_InitStruct"};
    block.assignments = {makeAssignment("Pin", *pins, 1, "GPIO_InitStruct.Pin = " + *pins + ";")};
    std::optional<reverse_import::MappedBlock> mapped = reverse_import::mapGpioBlock(block);
    ASSERT_TRUE(mapped.has_value());
    ASSERT_EQ((*mapped).json_block.at("pins").size(), 2u);
    EXPECT_EQ((*mapped).json_block.at("pins")[0].get<int>(), 0);
    EXPECT_EQ((*mapped).json_block.at("pins")[1].get<int>(), 1);

    std::optional<std::string> mode = reverse_import::serializeGpioFieldValue(
        "mode", nlohmann::json("timer_serial"), "");
    ASSERT_TRUE(mode.has_value());
    EXPECT_EQ(*mode, "HAL_GPIO_MODE_TIMER_SERIAL");

    std::optional<std::string> pull = reverse_import::serializeGpioFieldValue(
        "pull", nlohmann::json("pull_down"), "");
    ASSERT_TRUE(pull.has_value());
    EXPECT_EQ(*pull, "HAL_GPIO_PULL_DOWN");

    EXPECT_FALSE(reverse_import::serializeGpioFieldValue("mode", nlohmann::json("not_a_mode"), "").has_value());
    EXPECT_FALSE(reverse_import::serializeGpioFieldValue("pins", nlohmann::json::array(), "").has_value());
}

// ---- GPIO: unresolved-политика (feedback_reverse_import_unresolved_fields_policy) ----

namespace
{
const reverse_import::FieldProvenance* findGpioProvenance(
    const reverse_import::MappedBlock& mapped, const std::string& field)
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

TEST(GpioReverseMapper, UnrecognizablePinsAreUnresolvedNotDefaulted)
{
    RawInitBlock block;
    block.handle_type = "GPIO_InitTypeDef";
    block.init_call_args = {"GPIO_0"};
    block.assignments = {makeAssignment("Pin", "some_runtime_variable", 1, "    GPIO_InitStruct.Pin = some_runtime_variable;")};

    const std::optional<reverse_import::MappedBlock> mapped = reverse_import::mapGpioBlock(block);
    ASSERT_TRUE(mapped.has_value());

    const reverse_import::FieldProvenance* provenance = findGpioProvenance(*mapped, "pins");
    ASSERT_NE(provenance, nullptr);
    EXPECT_TRUE(provenance->unresolved);
}

TEST(GpioReverseMapper, MissingPortArgIsUnresolvedNotDefaulted)
{
    RawInitBlock block;
    block.handle_type = "GPIO_InitTypeDef";
    block.assignments = {makeAssignment("Pin", "GPIO_PIN_6", 1, "    GPIO_InitStruct.Pin = GPIO_PIN_6;")};

    const std::optional<reverse_import::MappedBlock> mapped = reverse_import::mapGpioBlock(block);
    ASSERT_TRUE(mapped.has_value());

    const reverse_import::FieldProvenance* provenance = findGpioProvenance(*mapped, "instance");
    ASSERT_NE(provenance, nullptr);
    EXPECT_TRUE(provenance->unresolved);
}

TEST(GpioReverseMapper, MissingModeIsUnresolvedNotDefaulted)
{
    RawInitBlock block;
    block.handle_type = "GPIO_InitTypeDef";
    block.init_call_args = {"GPIO_0"};
    block.assignments = {makeAssignment("Pin", "GPIO_PIN_6", 1, "    GPIO_InitStruct.Pin = GPIO_PIN_6;")};

    const std::optional<reverse_import::MappedBlock> mapped = reverse_import::mapGpioBlock(block);
    ASSERT_TRUE(mapped.has_value());

    const reverse_import::FieldProvenance* provenance = findGpioProvenance(*mapped, "mode");
    ASSERT_NE(provenance, nullptr);
    EXPECT_TRUE(provenance->unresolved);
}
