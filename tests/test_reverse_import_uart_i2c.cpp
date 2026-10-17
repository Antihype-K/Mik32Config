//
// Тесты uart_reverse_mapper.{h,cpp} / i2c_reverse_mapper.{h,cpp}
// (parser/reverse_import/) — по стилю tests/test_parsers.cpp (GoogleTest,
// секции Given/When/Then).
//
// ВНИМАНИЕ: parser/reverse_import/ пока не добавлен ни в CORE_SOURCES, ни
// в target_include_directories в CMakeLists.txt (по договорённости этот
// файл не трогает CMakeLists.txt — интеграция в сборку остаётся за
// пользователем). Поэтому здесь используются относительные include
// ("../parser/reverse_import/...") вместо голых имён файлов, как в
// test_parsers.cpp (там include-пути покрыты PUBLIC target_include_directories
// у mik32-core) — так тест собирается независимо от того, добавлен ли
// новый каталог в include-пути цели.
//
#include <gtest/gtest.h>

#include "../parser/reverse_import/i2c_reverse_mapper.h"
#include "../parser/reverse_import/mapped_block.h"
#include "../parser/reverse_import/reverse_import_scanner.h"
#include "../parser/reverse_import/uart_reverse_mapper.h"

#include <algorithm>
#include <optional>
#include <string>

using reverse_import::FieldProvenance;
using reverse_import::MappedBlock;
using reverse_import::RawFieldAssignment;
using reverse_import::RawInitBlock;
using reverse_import::SourceLocation;
using reverse_import::mapI2cBlock;
using reverse_import::mapUartBlock;
using reverse_import::serializeI2cFieldValue;
using reverse_import::serializeUartFieldValue;

namespace
{

// Строит одно присваивание "<handle>.<field> = <value>;" с точной
// файл+строка привязкой — так же, как реально заполняет их
// reverse_import_scanner.cpp (raw_value_text без ';' и без комментария).
RawFieldAssignment makeAssignment(const std::string& handle, const std::string& field,
                                   const std::string& value, const std::string& file, int line)
{
    RawFieldAssignment assignment;
    assignment.field_path = field;
    assignment.raw_value_text = value;
    assignment.location = SourceLocation{file, line};
    assignment.full_line_text = "    " + handle + "." + field + " = " + value + ";";
    return assignment;
}

// Полный блок husart0, слово в слово повторяющий
// docs/сделать/mik32_fixed_v3/mik32/controlFirmware/src/main.cpp:478-518
// (USART0_Init) — основной "тест-кейс на глаз" из задания.
RawInitBlock buildRealUsart0Block()
{
    const std::string file = "controlFirmware/src/main.cpp";
    RawInitBlock block;
    block.handle_name = "husart0";
    block.handle_type = "USART_HandleTypeDef";
    block.assignments = {
        makeAssignment("husart0", "Instance", "UART_1", file, 478),
        makeAssignment("husart0", "Instance", "UART_0", file, 480),
        makeAssignment("husart0", "transmitting", "Enable", file, 482),
        makeAssignment("husart0", "receiving", "Enable", file, 483),
        makeAssignment("husart0", "frame", "Frame_8bit", file, 484),
        makeAssignment("husart0", "parity_bit", "Disable", file, 485),
        makeAssignment("husart0", "parity_bit_inversion", "Disable", file, 486),
        makeAssignment("husart0", "bit_direction", "LSB_First", file, 487),
        makeAssignment("husart0", "data_inversion", "Disable", file, 488),
        makeAssignment("husart0", "tx_inversion", "Disable", file, 489),
        makeAssignment("husart0", "rx_inversion", "Disable", file, 490),
        makeAssignment("husart0", "swap", "Disable", file, 491),
        makeAssignment("husart0", "lbm", "Disable", file, 492),
        makeAssignment("husart0", "stop_bit", "StopBit_1", file, 493),
        makeAssignment("husart0", "mode", "Asynchronous_Mode", file, 494),
        makeAssignment("husart0", "xck_mode", "XCK_Mode3", file, 495),
        makeAssignment("husart0", "last_byte_clock", "Disable", file, 496),
        makeAssignment("husart0", "overwrite", "Disable", file, 497),
        makeAssignment("husart0", "rts_mode", "AlwaysEnable_mode", file, 498),
        makeAssignment("husart0", "dma_tx_request", "Disable", file, 499),
        makeAssignment("husart0", "dma_rx_request", "Enable", file, 500),
        makeAssignment("husart0", "channel_mode", "Duplex_Mode", file, 501),
        makeAssignment("husart0", "tx_break_mode", "Disable", file, 502),
        makeAssignment("husart0", "Interrupt.ctsie", "Disable", file, 503),
        makeAssignment("husart0", "Interrupt.eie", "Disable", file, 504),
        makeAssignment("husart0", "Interrupt.idleie", "Disable", file, 505),
        makeAssignment("husart0", "Interrupt.lbdie", "Disable", file, 506),
        makeAssignment("husart0", "Interrupt.peie", "Disable", file, 507),
        makeAssignment("husart0", "Interrupt.rxneie", "Disable", file, 508),
        makeAssignment("husart0", "Interrupt.tcie", "Disable", file, 509),
        makeAssignment("husart0", "Interrupt.txeie", "Disable", file, 510),
        makeAssignment("husart0", "Modem.rts", "Disable", file, 511),
        makeAssignment("husart0", "Modem.cts", "Disable", file, 512),
        makeAssignment("husart0", "Modem.dtr", "Disable", file, 513),
        makeAssignment("husart0", "Modem.dcd", "Disable", file, 514),
        makeAssignment("husart0", "Modem.dsr", "Disable", file, 515),
        makeAssignment("husart0", "Modem.ri", "Disable", file, 516),
        makeAssignment("husart0", "Modem.ddis", "Disable", file, 517),
        makeAssignment("husart0", "baudrate", "38400", file, 518),
    };
    block.init_function_name = "HAL_USART_Init";
    block.init_call_args = {"husart0"};
    block.init_call_location = SourceLocation{file, 519};
    return block;
}

// Блок hi2c0, ветка 32МГц/100кГц из
// docs/сделать/mik32_fixed_v3/.../main.cpp:314-328 (I2C0_Init).
RawInitBlock buildRealI2c0Block100kHz()
{
    const std::string file = "controlFirmware/src/main.cpp";
    RawInitBlock block;
    block.handle_name = "hi2c0";
    block.handle_type = "I2C_HandleTypeDef";
    block.assignments = {
        makeAssignment("hi2c0", "Instance", "I2C_0", file, 314),
        makeAssignment("hi2c0", "Init.Mode", "HAL_I2C_MODE_MASTER", file, 315),
        makeAssignment("hi2c0", "Init.DigitalFilter", "I2C_DIGITALFILTER_2CLOCKCYCLES", file, 316),
        makeAssignment("hi2c0", "Init.AnalogFilter", "I2C_ANALOGFILTER_DISABLE", file, 317),
        makeAssignment("hi2c0", "Init.AutoEnd", "I2C_AUTOEND_ENABLE", file, 318),
        makeAssignment("hi2c0", "Clock.PRESC", "0", file, 324),
        makeAssignment("hi2c0", "Clock.SCLDEL", "8", file, 325),
        makeAssignment("hi2c0", "Clock.SDADEL", "2", file, 326),
        makeAssignment("hi2c0", "Clock.SCLH", "144", file, 327),
        makeAssignment("hi2c0", "Clock.SCLL", "166", file, 328),
    };
    block.init_function_name = "HAL_I2C_Init";
    block.init_call_args = {"hi2c0"};
    block.init_call_location = SourceLocation{file, 387};
    return block;
}

} // namespace

// ---- (a) реальный сценарий: husart0, полный 44-строчный блок ----

TEST(UartReverseMapper, MapsRealUsart0BlockFromMik32FixedV3)
{
    // Given
    const RawInitBlock block = buildRealUsart0Block();

    // When
    const std::optional<MappedBlock> mapped = mapUartBlock(block);

    // Then
    ASSERT_TRUE(mapped.has_value());
    EXPECT_EQ(mapped->json_block.at("type"), "uart");
    EXPECT_EQ(mapped->json_block.at("baud_rate"), 38400);
    EXPECT_EQ(mapped->json_block.at("data_bits"), 8);
    EXPECT_EQ(mapped->json_block.at("stop_bits"), 1);
    EXPECT_EQ(mapped->json_block.at("parity"), "none");
    EXPECT_EQ(mapped->json_block.at("direction"), "duplex");
    EXPECT_EQ(mapped->json_block.at("flow_control"), false);
    // Instance взят из последнего по порядку появления присваивания
    // (UART_0 на строке 480, после ветки #ifdef USE_UART_1 на строке 478) —
    // см. большой комментарий в uart_reverse_mapper.cpp.
    EXPECT_EQ(mapped->json_block.at("instance"), 0);

    // Ambiguous Instance (#ifdef) должен быть честно отражён в notes.
    const bool has_instance_note = std::any_of(
        mapped->notes.begin(), mapped->notes.end(),
        [](const std::string& note) { return note.find("Instance") != std::string::npos; });
    EXPECT_TRUE(has_instance_note);

    // Обязательные поля схемы (schemas/uart.json required_fields) все
    // присутствуют.
    for (const char* required : {"type", "instance", "baud_rate", "data_bits", "stop_bits", "parity",
                                  "flow_control", "direction"})
    {
        EXPECT_TRUE(mapped->json_block.contains(required)) << "missing required field: " << required;
    }
}

TEST(UartReverseMapper, RejectsBlockWithWrongHandleType)
{
    // Given
    RawInitBlock block;
    block.handle_type = "I2C_HandleTypeDef"; // не USART

    // When
    const std::optional<MappedBlock> mapped = mapUartBlock(block);

    // Then
    EXPECT_FALSE(mapped.has_value());
}

// ---- (b) нестандартное значение baud_rate ----

TEST(UartReverseMapper, KeepsNonStandardBaudRateAsIsWithNote)
{
    // Given: baudrate = 4800 — не входит в enum_int schemas/uart.json
    // ({9600,19200,38400,57600,115200,230400,460800,921600}).
    RawInitBlock block = buildRealUsart0Block();
    for (RawFieldAssignment& assignment : block.assignments)
    {
        if (assignment.field_path == "baudrate")
        {
            assignment.raw_value_text = "4800";
        }
    }

    // When
    const std::optional<MappedBlock> mapped = mapUartBlock(block);

    // Then: значение сохранено как есть (не подменено ближайшим разрешённым),
    // с честным предупреждением в notes.
    ASSERT_TRUE(mapped.has_value());
    EXPECT_EQ(mapped->json_block.at("baud_rate"), 4800);
    const bool has_note = std::any_of(
        mapped->notes.begin(), mapped->notes.end(),
        [](const std::string& note) { return note.find("4800") != std::string::npos; });
    EXPECT_TRUE(has_note);
}

// ---- (c) serializeUartFieldValue round-trip для baud_rate ----

TEST(UartReverseMapper, SerializesBaudRateRoundTrip)
{
    // Given/When
    const std::optional<std::string> serialized = serializeUartFieldValue("baud_rate", 9600, "38400");

    // Then
    ASSERT_TRUE(serialized.has_value());
    EXPECT_EQ(*serialized, "9600");
}

TEST(UartReverseMapper, SerializeParityEvenIsAmbiguousReturnsNullopt)
{
    // parity_bit и parity_bit_inversion должны РАЗОЙТИСЬ (Enable/Disable)
    // для "even" — сигнатура не сообщает, какая из двух физических строк
    // сериализуется, поэтому функция обязана честно отказаться, а не
    // угадать (см. комментарий наверху uart_reverse_mapper.cpp).
    EXPECT_FALSE(serializeUartFieldValue("parity", std::string("even"), "Disable").has_value());
}

TEST(UartReverseMapper, SerializeParityNoneAndOddAreUnambiguous)
{
    // "none" и "odd" требуют ОДИНАКОВЫЙ текст для обеих физических строк —
    // безопасно сериализуются независимо от того, какая из них.
    EXPECT_EQ(*serializeUartFieldValue("parity", std::string("none"), "Enable"), "Disable");
    EXPECT_EQ(*serializeUartFieldValue("parity", std::string("odd"), "Disable"), "Enable");
}

TEST(UartReverseMapper, SerializesInstancePreservingPrefixFromOriginalText)
{
    // Given/When: текущий текст "UART_0", новое значение instance=1.
    const std::optional<std::string> serialized = serializeUartFieldValue("instance", 1, "UART_0");

    // Then: префикс "UART_" взят из исходного текста, а не захардкожен.
    ASSERT_TRUE(serialized.has_value());
    EXPECT_EQ(*serialized, "UART_1");
}

// ---- I2C: (a) реальный сценарий hi2c0 100кГц-ветка ----

TEST(I2cReverseMapper, MapsRealI2c0BlockAt100kHzFromMik32FixedV3)
{
    // Given
    const RawInitBlock block = buildRealI2c0Block100kHz();

    // When
    const std::optional<MappedBlock> mapped = mapI2cBlock(block);

    // Then
    ASSERT_TRUE(mapped.has_value());
    EXPECT_EQ(mapped->json_block.at("type"), "i2c");
    EXPECT_EQ(mapped->json_block.at("instance"), 0);
    EXPECT_EQ(mapped->json_block.at("mode"), "master");
    EXPECT_EQ(mapped->json_block.at("clock_speed"), 100000);

    for (const char* required : {"type", "instance", "mode", "clock_speed"})
    {
        EXPECT_TRUE(mapped->json_block.contains(required)) << "missing required field: " << required;
    }

    // clock_speed обязан быть read-only после импорта (см. i2c_reverse_mapper.cpp).
    const auto clock_speed_provenance = std::find_if(
        mapped->provenance.begin(), mapped->provenance.end(),
        [](const reverse_import::FieldProvenance& p) { return p.json_field == "clock_speed"; });
    ASSERT_NE(clock_speed_provenance, mapped->provenance.end());
    EXPECT_FALSE(clock_speed_provenance->editable);
}

TEST(I2cReverseMapper, RecognizesOwnCodegenPresetEvenUnderMik32FixedV3sConfusingLabel)
{
    // Given: пятёрка (PRESC=1,SCLDEL=15,SDADEL=15,SCLH=75,SCLL=75) встречается
    // в mik32_fixed_v3 под веткой "i2c_frequency==1" (main.cpp:379-383) —
    // ИМЯ переменной наводит на мысль, что это что-то отдельное от 100/400/
    // 1000 кГц веток чуть выше в том же файле. На самом деле это ТА ЖЕ
    // самая HW-подтверждённая 100 кГц комбинация, которую собственный
    // кодоген этого конфигуратора всегда выдаёт для clock_speed=100000
    // (codegen/project/i2c_contribution.cpp::calcI2cTimingr — используется
    // как пресет, а не считается по формуле, потому что два независимых
    // рабочих примера на 1T Rex Board сходятся именно на этих числах).
    // Обнаружено при импорте generated_projects/rex_project_all4 (собственный
    // сгенерированный проект): без этой строки в таблице i2c_reverse_mapper
    // получал "не удалось определить clock_speed" на СВОЁМ ЖЕ выводе.
    const std::string file = "controlFirmware/src/main.cpp";
    RawInitBlock block = buildRealI2c0Block100kHz();
    block.assignments.push_back(makeAssignment("hi2c0", "Clock.PRESC", "1", file, 379));
    block.assignments.push_back(makeAssignment("hi2c0", "Clock.SCLDEL", "15", file, 380));
    block.assignments.push_back(makeAssignment("hi2c0", "Clock.SDADEL", "15", file, 381));
    block.assignments.push_back(makeAssignment("hi2c0", "Clock.SCLH", "75", file, 382));
    block.assignments.push_back(makeAssignment("hi2c0", "Clock.SCLL", "75", file, 383));

    // When
    const std::optional<MappedBlock> mapped = mapI2cBlock(block);

    // Then
    ASSERT_TRUE(mapped.has_value());
    EXPECT_EQ(mapped->json_block.at("clock_speed"), 100000);
    const bool has_fallback_note = std::any_of(
        mapped->notes.begin(), mapped->notes.end(),
        [](const std::string& note) { return note.find("Не удалось определить clock_speed") != std::string::npos; });
    EXPECT_FALSE(has_fallback_note);
}

TEST(I2cReverseMapper, GenuinelyUnrecognizedTimingQuintetFallsBackToDefaultWithNote)
{
    // Given: пятёрка, не совпадающая ни с одной из четырёх известных строк
    // таблицы (см. предыдущий тест про то, почему таблица теперь из четырёх,
    // не трёх, строк) — придуманные числа, которых не бывает ни в одном из
    // разобранных реальных проектов.
    const std::string file = "controlFirmware/src/main.cpp";
    RawInitBlock block = buildRealI2c0Block100kHz();
    block.assignments.push_back(makeAssignment("hi2c0", "Clock.PRESC", "3", file, 379));
    block.assignments.push_back(makeAssignment("hi2c0", "Clock.SCLDEL", "9", file, 380));
    block.assignments.push_back(makeAssignment("hi2c0", "Clock.SDADEL", "6", file, 381));
    block.assignments.push_back(makeAssignment("hi2c0", "Clock.SCLH", "200", file, 382));
    block.assignments.push_back(makeAssignment("hi2c0", "Clock.SCLL", "210", file, 383));

    // When
    const std::optional<MappedBlock> mapped = mapI2cBlock(block);

    // Then
    ASSERT_TRUE(mapped.has_value());
    EXPECT_EQ(mapped->json_block.at("clock_speed"), 100000);
    const bool has_note = std::any_of(
        mapped->notes.begin(), mapped->notes.end(),
        [](const std::string& note) { return note.find("Не удалось определить clock_speed") != std::string::npos; });
    EXPECT_TRUE(has_note);
}

TEST(I2cReverseMapper, RejectsBlockWithWrongHandleType)
{
    // Given
    RawInitBlock block;
    block.handle_type = "USART_HandleTypeDef"; // не I2C

    // When
    const std::optional<MappedBlock> mapped = mapI2cBlock(block);

    // Then
    EXPECT_FALSE(mapped.has_value());
}

// ---- I2C: (c) serializeI2cFieldValue ----

TEST(I2cReverseMapper, SerializesOwnAddressRoundTrip)
{
    const std::optional<std::string> serialized = serializeI2cFieldValue("own_address", 66, "0x50");
    ASSERT_TRUE(serialized.has_value());
    EXPECT_EQ(*serialized, "66");
}

TEST(I2cReverseMapper, SerializesInstancePreservingPrefixFromOriginalText)
{
    const std::optional<std::string> serialized = serializeI2cFieldValue("instance", 1, "I2C_0");
    ASSERT_TRUE(serialized.has_value());
    EXPECT_EQ(*serialized, "I2C_1");
}

TEST(I2cReverseMapper, ClockSpeedIsNeverSerializable)
{
    // v1 не поддерживает запись clock_speed обратно (5 независимых
    // C-строк на одно JSON-значение) — см. обоснование в
    // i2c_reverse_mapper.cpp.
    EXPECT_FALSE(serializeI2cFieldValue("clock_speed", 400000, "0").has_value());
}

TEST(I2cReverseMapper, AddressModeIsNeverSerializable)
{
    // address_mode не имеет своей C-строки — выводится из величины
    // own_address, а не хранится отдельно.
    EXPECT_FALSE(serializeI2cFieldValue("address_mode", std::string("10bit"), "0x50").has_value());
}

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

// ---- UART: unresolved-политика (feedback_reverse_import_unresolved_fields_policy) ----

TEST(UartReverseMapper, MissingBaudRateIsUnresolvedNotDefaulted)
{
    const std::string file = "no_baud/src/main.c";
    RawInitBlock block;
    block.handle_name = "husart0";
    block.handle_type = "USART_HandleTypeDef";
    block.assignments = {
        makeAssignment("husart0", "Instance", "UART_0", file, 1),
        makeAssignment("husart0", "frame", "Frame_8bit", file, 2),
    };

    const std::optional<MappedBlock> mapped = mapUartBlock(block);
    ASSERT_TRUE(mapped.has_value());
    ASSERT_TRUE(mapped->json_block.contains("baud_rate"));

    const FieldProvenance* provenance = findProvenance(*mapped, "baud_rate");
    ASSERT_NE(provenance, nullptr);
    EXPECT_TRUE(provenance->unresolved);
    EXPECT_FALSE(mapped->notes.empty());
}

TEST(UartReverseMapper, UnparseableBaudRateIsUnresolvedNotDefaulted)
{
    const std::string file = "expr_baud/src/main.c";
    RawInitBlock block;
    block.handle_name = "husart0";
    block.handle_type = "USART_HandleTypeDef";
    block.assignments = {
        makeAssignment("husart0", "Instance", "UART_0", file, 1),
        makeAssignment("husart0", "baudrate", "config.baud", file, 2),
    };

    const std::optional<MappedBlock> mapped = mapUartBlock(block);
    ASSERT_TRUE(mapped.has_value());

    const FieldProvenance* provenance = findProvenance(*mapped, "baud_rate");
    ASSERT_NE(provenance, nullptr);
    EXPECT_TRUE(provenance->unresolved);
    EXPECT_EQ(provenance->original_raw_value_text, "config.baud");
}

TEST(UartReverseMapper, AmbiguousParityEvenIsUnresolvedNotDefaulted)
{
    // parity_bit=Enable, но parity_bit_inversion нераспознаваем ->
    // нельзя честно сказать odd/even, раньше тихо угадывалось "even".
    const std::string file = "weird_parity/src/main.c";
    RawInitBlock block;
    block.handle_name = "husart0";
    block.handle_type = "USART_HandleTypeDef";
    block.assignments = {
        makeAssignment("husart0", "Instance", "UART_0", file, 1),
        makeAssignment("husart0", "parity_bit", "Enable", file, 2),
        makeAssignment("husart0", "parity_bit_inversion", "config.inv", file, 3),
    };

    const std::optional<MappedBlock> mapped = mapUartBlock(block);
    ASSERT_TRUE(mapped.has_value());

    const FieldProvenance* pb = findProvenance(*mapped, "parity");
    ASSERT_NE(pb, nullptr);
    EXPECT_TRUE(pb->unresolved);
}

// ---- I2C: unresolved-политика (feedback_reverse_import_unresolved_fields_policy) ----

TEST(I2cReverseMapper, MissingInstanceIsUnresolvedNotDefaulted)
{
    RawInitBlock block;
    block.handle_name = "hi2c0";
    block.handle_type = "I2C_HandleTypeDef";
    block.assignments = {
        makeAssignment("hi2c0", "Init.Mode", "HAL_I2C_MODE_MASTER", "no_instance/src/main.c", 1),
    };

    const std::optional<MappedBlock> mapped = mapI2cBlock(block);
    ASSERT_TRUE(mapped.has_value());

    const FieldProvenance* provenance = findProvenance(*mapped, "instance");
    ASSERT_NE(provenance, nullptr);
    EXPECT_TRUE(provenance->unresolved);
}

TEST(I2cReverseMapper, UnmatchedTimingQuintetIsUnresolvedNotDefaulted)
{
    const std::string file = "unknown_timing/src/main.c";
    RawInitBlock block;
    block.handle_name = "hi2c0";
    block.handle_type = "I2C_HandleTypeDef";
    block.assignments = {
        makeAssignment("hi2c0", "Instance", "I2C_0", file, 1),
        makeAssignment("hi2c0", "Init.Mode", "HAL_I2C_MODE_MASTER", file, 2),
        makeAssignment("hi2c0", "Clock.PRESC", "9", file, 3),
        makeAssignment("hi2c0", "Clock.SCLDEL", "9", file, 4),
        makeAssignment("hi2c0", "Clock.SDADEL", "9", file, 5),
        makeAssignment("hi2c0", "Clock.SCLH", "9", file, 6),
        makeAssignment("hi2c0", "Clock.SCLL", "9", file, 7),
    };

    const std::optional<MappedBlock> mapped = mapI2cBlock(block);
    ASSERT_TRUE(mapped.has_value());

    const FieldProvenance* provenance = findProvenance(*mapped, "clock_speed");
    ASSERT_NE(provenance, nullptr);
    EXPECT_TRUE(provenance->unresolved);
}

TEST(I2cReverseMapper, MissingOwnAddressForSlaveIsUnresolvedNotDefaulted)
{
    const std::string file = "no_address/src/main.c";
    RawInitBlock block;
    block.handle_name = "hi2c0";
    block.handle_type = "I2C_HandleTypeDef";
    block.assignments = {
        makeAssignment("hi2c0", "Instance", "I2C_0", file, 1),
        makeAssignment("hi2c0", "Init.Mode", "HAL_I2C_MODE_SLAVE", file, 2),
    };

    const std::optional<MappedBlock> mapped = mapI2cBlock(block);
    ASSERT_TRUE(mapped.has_value());

    const FieldProvenance* own_address = findProvenance(*mapped, "own_address");
    ASSERT_NE(own_address, nullptr);
    EXPECT_TRUE(own_address->unresolved);

    const FieldProvenance* address_mode = findProvenance(*mapped, "address_mode");
    ASSERT_NE(address_mode, nullptr);
    EXPECT_TRUE(address_mode->unresolved);
}

TEST(UartReverseMapper, MissingDirectionFieldsIsUnresolvedNotDefaulted)
{
    const std::string file = "no_direction/src/main.c";
    RawInitBlock block;
    block.handle_name = "husart0";
    block.handle_type = "USART_HandleTypeDef";
    block.assignments = {
        makeAssignment("husart0", "Instance", "UART_0", file, 1),
    };

    const std::optional<MappedBlock> mapped = mapUartBlock(block);
    ASSERT_TRUE(mapped.has_value());

    const FieldProvenance* provenance = findProvenance(*mapped, "direction");
    ASSERT_NE(provenance, nullptr);
    EXPECT_TRUE(provenance->unresolved);
}
