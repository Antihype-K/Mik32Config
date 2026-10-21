//
// Сквозной интеграционный тест reverse-import: НЕ мок RawInitBlock, а
// настоящий сканер (reverse_import_scanner) на настоящем временном дереве
// файлов -> все четыре маппера -> оркестратор importProjectFromDirectory ->
// правка поля в GUI-подобном json -> reverse_import_apply -> запись на диск
// -> повторный импорт того же дерева подтверждает, что правка реально там.
//
// Фикстура ниже — СИНТЕТИЧЕСКАЯ, написанная для этого теста с нуля (а не
// копия docs/сделать/mik32_fixed_v3 — тот каталог личный и не должен
// попадать в git, см. docs/implementations/reverse_import.md, раздел про
// границы фичи). Она воспроизводит те же паттерны (несколько файлов,
// #ifdef вокруг Instance, GPIO_InitStruct переиспользуется на несколько
// пинов, побитовое ИЛИ пинов, Timer32 с вычисляемым prescaler), но своими
// именами/числами.
//
#include <gtest/gtest.h>

#include <reverse_import.h>
#include <reverse_import_apply.h>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>

namespace fs = std::filesystem;
using namespace reverse_import;

namespace
{

class ReverseImportIntegrationTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        root_ = fs::temp_directory_path() / fs::path(
            "mik32_reverse_import_it_" + std::to_string(::testing::UnitTest::GetInstance()->random_seed()) +
            "_" + std::to_string(reinterpret_cast<std::uintptr_t>(this)));
        fs::create_directories(root_ / "src");
    }

    void TearDown() override
    {
        std::error_code ignored;
        fs::remove_all(root_, ignored);
    }

    void writeFile(const std::string& relative_path, const std::string& content)
    {
        std::ofstream stream(root_ / relative_path, std::ios::binary);
        stream << content;
    }

    std::string readFile(const std::string& relative_path) const
    {
        std::ifstream stream(root_ / relative_path, std::ios::binary);
        return std::string((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
    }

    fs::path root_;
};

// Своя, вымышленная "прошивка" из двух файлов: main.c (UART+GPIO) и
// devices.c (I2C+Timer32) — проверяет, что сканер действительно обходит
// ДЕРЕВО каталогов, а не один файл.
constexpr const char* kMainC = R"(
#include "mik32_hal.h"

USART_HandleTypeDef husart_demo;
// Инициализатор "= {0}" — ровно так собственный кодоген этого конфигуратора
// объявляет GPIO_InitStruct (codegen/project/gpio_contribution.cpp). Без
// поддержки этого синтаксиса в reverse_import_scanner декларация не
// находилась бы вовсе — обнаружено при импорте generated_projects/
// rex_project_all4, где GPIO из-за этого пропадал целиком.
GPIO_InitTypeDef GPIO_InitStruct = {0};

void USART_Demo_Init()
{
    husart_demo.Instance = UART_0;
    husart_demo.transmitting = Enable;
    husart_demo.receiving = Enable;
    husart_demo.frame = Frame_8bit;
    husart_demo.parity_bit = Disable;
    husart_demo.parity_bit_inversion = Disable;
    husart_demo.stop_bit = StopBit_1;
    husart_demo.Modem.rts = Disable;
    husart_demo.Modem.cts = Disable;
    husart_demo.baudrate = 9600;
    HAL_USART_Init(&husart_demo);
}

void GPIO_Demo_Init()
{
    GPIO_InitStruct.Pin = GPIO_PIN_0 | GPIO_PIN_1;
    GPIO_InitStruct.Mode = HAL_GPIO_MODE_GPIO_OUTPUT;
    GPIO_InitStruct.Pull = HAL_GPIO_PULL_NONE;
    HAL_GPIO_Init(GPIO_0, &GPIO_InitStruct);
}
)";

constexpr const char* kDevicesC = R"(
#include "mik32_hal.h"

I2C_HandleTypeDef hi2c_demo;
TIMER32_HandleTypeDef htimer32_demo;

void I2C_Demo_Init()
{
    hi2c_demo.Instance = I2C_0;
    hi2c_demo.Init.Mode = HAL_I2C_MODE_MASTER;
    hi2c_demo.Init.DigitalFilter = I2C_DIGITALFILTER_2CLOCKCYCLES;
    hi2c_demo.Init.AnalogFilter = I2C_ANALOGFILTER_DISABLE;
    hi2c_demo.Init.AutoEnd = I2C_AUTOEND_ENABLE;
    hi2c_demo.Clock.PRESC = 0;
    hi2c_demo.Clock.SCLDEL = 8;
    hi2c_demo.Clock.SDADEL = 2;
    hi2c_demo.Clock.SCLH = 144;
    hi2c_demo.Clock.SCLL = 166;
    HAL_I2C_Init(&hi2c_demo);
}

void Timer32_Demo_Init(uint32_t clock_frequency, uint32_t timer_frequency)
{
    uint32_t prescaler = (clock_frequency / timer_frequency) - 1;
    htimer32_demo.Instance = TIMER32_0;
    htimer32_demo.Top = 0xFFFFFFFF;
    htimer32_demo.Clock.Prescaler = prescaler;
    htimer32_demo.CountMode = TIMER32_COUNTMODE_FORWARD;
    HAL_Timer32_Init(&htimer32_demo);
}

Timer16_HandleTypeDef htimer16_demo;

void Timer16_Demo_Init()
{
    htimer16_demo.Instance = TIMER16_0;
    htimer16_demo.Clock.Prescaler = TIMER16_PRESCALER_16;
    HAL_Timer16_Init(&htimer16_demo);
    HAL_Timer16_StartPWM(&htimer16_demo, 999, 499);
}
)";

TEST_F(ReverseImportIntegrationTest, ScansMultiFileTreeAndProducesAllFivePeripheralTypes)
{
    writeFile("src/main.c", kMainC);
    writeFile("src/devices.c", kDevicesC);

    const ImportedProject imported = importProjectFromDirectory(root_.string());

    ASSERT_EQ(imported.document["peripherals"].size(), 5u);
    ASSERT_EQ(imported.block_provenance.size(), 5u);

    std::set<std::string> types;
    for (const auto& block : imported.document["peripherals"])
    {
        types.insert(block.value("type", std::string()));
    }
    EXPECT_EQ(types, (std::set<std::string>{"uart", "i2c", "timer32", "gpio", "timer16"}));

    for (const auto& block : imported.document["peripherals"])
    {
        if (block["type"] == "uart")
        {
            EXPECT_EQ(block["baud_rate"], 9600);
            EXPECT_EQ(block["direction"], "duplex");
        }
        if (block["type"] == "i2c")
        {
            EXPECT_EQ(block["clock_speed"], 100000);
        }
        if (block["type"] == "gpio")
        {
            // Побитовое ИЛИ двух пинов в одной строке -> массив из двух чисел.
            ASSERT_TRUE(block["pins"].is_array());
            EXPECT_EQ(block["pins"].size(), 2u);
        }
        if (block["type"] == "timer32")
        {
            // Top — литерал (парсится); Prescaler — вычисляемое выражение,
            // не литерал -> должен быть отмечен как нередактируемый, не крашить импорт.
            EXPECT_EQ(block["top"], 4294967295u);
        }
        if (block["type"] == "timer16")
        {
            // Prescaler — обычное присваивание (редактируемо); mode/arr/cmp —
            // из HAL_Timer16_StartPWM(&htimer16_demo, 999, 499) ПОСЛЕ Init,
            // а не из присваивания поля (см. timer16_reverse_mapper.h).
            EXPECT_EQ(block["prescaler"], 16);
            EXPECT_EQ(block["mode"], "period");
            EXPECT_EQ(block["arr"], 999);
            EXPECT_EQ(block["cmp"], 499);
        }
    }
}

TEST_F(ReverseImportIntegrationTest, EndToEndEditBaudRateAndApplyPatchesExactlyOneLineOnDisk)
{
    writeFile("src/main.c", kMainC);
    const std::string original_main_c = readFile("src/main.c");

    const ImportedProject imported = importProjectFromDirectory(root_.string());
    nlohmann::json edited_peripherals = imported.document["peripherals"];

    int uart_index = -1;
    for (std::size_t i = 0; i < edited_peripherals.size(); ++i)
    {
        if (edited_peripherals[i]["type"] == "uart")
        {
            uart_index = static_cast<int>(i);
        }
    }
    ASSERT_NE(uart_index, -1);

    // Ровно то сценарий из исходного запроса пользователя: "поменять бодрейт".
    edited_peripherals[uart_index]["baud_rate"] = 115200;

    const ApplyReport report = applyImportedChanges(
        imported.document["peripherals"], edited_peripherals, imported.block_provenance);

    EXPECT_TRUE(report.unpatchable.empty());
    ASSERT_FALSE(report.patch_results.empty());
    for (const PatchResult& result : report.patch_results)
    {
        EXPECT_TRUE(result.success) << result.error;
    }

    const std::string patched_main_c = readFile("src/main.c");
    EXPECT_NE(patched_main_c, original_main_c);
    EXPECT_NE(patched_main_c.find("husart_demo.baudrate = 115200;"), std::string::npos);

    // Остальной файл — байт-в-байт: убираем ОДНУ ожидаемую замену из обеих
    // версий и сравниваем то, что осталось.
    std::string original_without_baud = original_main_c;
    std::string patched_without_baud = patched_main_c;
    const std::string original_line = "husart_demo.baudrate = 9600;";
    const std::string patched_line = "husart_demo.baudrate = 115200;";
    original_without_baud.erase(original_without_baud.find(original_line), original_line.size());
    patched_without_baud.erase(patched_without_baud.find(patched_line), patched_line.size());
    EXPECT_EQ(original_without_baud, patched_without_baud);

    // Реимпорт того же дерева должен увидеть уже НОВОЕ значение — правка
    // реальна, а не только в памяти этого процесса.
    const ImportedProject reimported = importProjectFromDirectory(root_.string());
    bool found_new_baud_rate = false;
    for (const auto& block : reimported.document["peripherals"])
    {
        if (block["type"] == "uart" && block["baud_rate"] == 115200)
        {
            found_new_baud_rate = true;
        }
    }
    EXPECT_TRUE(found_new_baud_rate);
}

TEST_F(ReverseImportIntegrationTest, IgnoresBundledSdkDirectoryToAvoidFalsePositivesFromVendorSource)
{
    // Given: ровно то, что реально кладёт собственный CMake-тулчейн этого
    // конфигуратора (codegen/project/cmake_project_codegen.cpp:704) — копию
    // SDK рядом с прикладным кодом. Обнаружено на реальном
    // generated_projects/n422e2: без фильтра из ОДНОГО настоящего блока в
    // src/ сканер находил 45 (44 — ложные срабатывания внутри самого HAL).
    writeFile("src/main.c", kMainC);
    fs::create_directories(root_ / "mik32-sdk" / "hal");
    writeFile("mik32-sdk/hal/mik32_hal_i2c.c", R"(
I2C_HandleTypeDef hi2c_internal_example;
void HAL_I2C_SomeInternalExample()
{
    hi2c_internal_example.Instance = I2C_1;
    HAL_I2C_Init(&hi2c_internal_example);
}
)");

    const ImportedProject imported = importProjectFromDirectory(root_.string());

    // Только файлы src/ — mik32-sdk/ не должен даже попасть в files_scanned.
    for (const std::string& file : imported.files_scanned)
    {
        EXPECT_EQ(file.find("mik32-sdk"), std::string::npos) << file;
    }

    // Ровно то же, что нашлось бы без mik32-sdk/ вообще (2 блока из main.c —
    // uart и gpio, никакого лишнего i2c из "внутреннего примера").
    int i2c_count = 0;
    for (const auto& block : imported.document["peripherals"])
    {
        if (block["type"] == "i2c")
        {
            ++i2c_count;
        }
    }
    EXPECT_EQ(i2c_count, 0);
}

TEST_F(ReverseImportIntegrationTest, DoesNotMistakeCallLikeTextInsideStringLiteralsForARealCall)
{
    // Given: обнаружено на реальном mik32_fixed_v3 — строка лога ошибки
    // "xprintf(\"HAL_I2C_Init(&hi2c0) HAL_ERROR\\n\");" (main.cpp:389) была
    // ошибочно распознана как ВТОРОЙ настоящий вызов HAL_I2C_Init(&hi2c0),
    // что заводило ЛОЖНЫЙ второй блок I2C на instance 0 — parseProjectDocument
    // затем справедливо ругался на "instance 0 уже настроен другим блоком",
    // хотя дублирования не было вовсе, только текст внутри строки лога.
    writeFile("src/main.c", std::string(kDevicesC) + R"(
void I2C_Demo_LogError()
{
    xprintf("HAL_I2C_Init(&hi2c_demo) HAL_ERROR\n");
}
)");

    const ImportedProject imported = importProjectFromDirectory(root_.string());

    int i2c_count = 0;
    for (const auto& block : imported.document["peripherals"])
    {
        if (block["type"] == "i2c")
        {
            ++i2c_count;
        }
    }
    EXPECT_EQ(i2c_count, 1);
}

TEST_F(ReverseImportIntegrationTest, AttachesTimer32ChannelToParentByNameEvenWhenChannelComesFirstInFile)
{
    // Given: канал объявлен/инициализирован РАНЬШЕ родительского таймера в
    // файле (сканер не должен зависеть от порядка — см. reverse_import.cpp,
    // двухпроходная обработка). Паттерн — ровно как в mik32_fixed_v3
    // (main.cpp:555 и Timer32_1_Init), только со своими именами.
    writeFile("src/main.c", R"(
#include "mik32_hal.h"

TIMER32_CHANNEL_HandleTypeDef motor_channel0;
TIMER32_HandleTypeDef htimer32_1;

void Channel_Init()
{
    motor_channel0.TimerInstance = htimer32_1.Instance;
    motor_channel0.ChannelIndex = TIMER32_CHANNEL_0;
    motor_channel0.PWM_Invert = TIMER32_CHANNEL_NON_INVERTED_PWM;
    motor_channel0.Mode = TIMER32_CHANNEL_MODE_PWM;
    motor_channel0.CaptureEdge = TIMER32_CHANNEL_CAPTUREEDGE_RISING;
    motor_channel0.OCR = 500;
    motor_channel0.Noise = TIMER32_CHANNEL_FILTER_OFF;
    HAL_Timer32_Channel_Init(&motor_channel0);
}

void Timer32_1_Init()
{
    htimer32_1.Instance = TIMER32_1;
    htimer32_1.Top = 999;
    htimer32_1.Clock.Prescaler = 31;
    htimer32_1.CountMode = TIMER32_COUNTMODE_FORWARD;
    HAL_Timer32_Init(&htimer32_1);
}
)");

    const ImportedProject imported = importProjectFromDirectory(root_.string());

    int timer32_index = -1;
    for (std::size_t i = 0; i < imported.document["peripherals"].size(); ++i)
    {
        if (imported.document["peripherals"][i]["type"] == "timer32")
        {
            timer32_index = static_cast<int>(i);
        }
    }
    ASSERT_NE(timer32_index, -1);

    const nlohmann::json& timer32_block = imported.document["peripherals"][timer32_index];
    ASSERT_TRUE(timer32_block.contains("channels"));
    ASSERT_EQ(timer32_block["channels"].size(), 1u);
    EXPECT_EQ(timer32_block["channels"][0]["index"], 0);
    EXPECT_EQ(timer32_block["channels"][0]["mode"], "pwm");
    EXPECT_EQ(timer32_block["channels"][0]["ocr"], 500);

    // Поканальные поля — по одному provenance-полю на "channels[0].<field>",
    // все реально литеральные значения (ocr=500 и т.д.) -> editable=true.
    const std::vector<FieldProvenance>& provenance = imported.block_provenance[timer32_index];
    const auto ocr_provenance = std::find_if(
        provenance.begin(), provenance.end(),
        [](const FieldProvenance& p) { return p.json_field == "channels[0].ocr"; });
    ASSERT_NE(ocr_provenance, provenance.end());
    EXPECT_TRUE(ocr_provenance->editable);
}

TEST_F(ReverseImportIntegrationTest, EndToEndEditsOneChannelFieldAndPatchesExactlyThatLine)
{
    writeFile("src/main.c", R"(
#include "mik32_hal.h"

TIMER32_CHANNEL_HandleTypeDef motor_channel0;
TIMER32_HandleTypeDef htimer32_1;

void Channel_Init()
{
    motor_channel0.TimerInstance = htimer32_1.Instance;
    motor_channel0.ChannelIndex = TIMER32_CHANNEL_0;
    motor_channel0.PWM_Invert = TIMER32_CHANNEL_NON_INVERTED_PWM;
    motor_channel0.Mode = TIMER32_CHANNEL_MODE_PWM;
    motor_channel0.CaptureEdge = TIMER32_CHANNEL_CAPTUREEDGE_RISING;
    motor_channel0.OCR = 500;
    motor_channel0.Noise = TIMER32_CHANNEL_FILTER_OFF;
    HAL_Timer32_Channel_Init(&motor_channel0);
}

void Timer32_1_Init()
{
    htimer32_1.Instance = TIMER32_1;
    htimer32_1.Top = 999;
    htimer32_1.Clock.Prescaler = 31;
    htimer32_1.CountMode = TIMER32_COUNTMODE_FORWARD;
    HAL_Timer32_Init(&htimer32_1);
}
)");
    const std::string original_main_c = readFile("src/main.c");

    const ImportedProject imported = importProjectFromDirectory(root_.string());
    nlohmann::json edited_peripherals = imported.document["peripherals"];

    int timer32_index = -1;
    for (std::size_t i = 0; i < edited_peripherals.size(); ++i)
    {
        if (edited_peripherals[i]["type"] == "timer32")
        {
            timer32_index = static_cast<int>(i);
        }
    }
    ASSERT_NE(timer32_index, -1);

    // Ровно сценарий "поменять одно поле одного канала" — OCR 500 -> 750,
    // остальные каналы/поля родителя не трогаем.
    edited_peripherals[timer32_index]["channels"][0]["ocr"] = 750;

    const ApplyReport report = applyImportedChanges(
        imported.document["peripherals"], edited_peripherals, imported.block_provenance);

    EXPECT_TRUE(report.unpatchable.empty());
    ASSERT_EQ(report.patch_results.size(), 1u);
    EXPECT_TRUE(report.patch_results[0].success) << report.patch_results[0].error;

    const std::string patched_main_c = readFile("src/main.c");
    EXPECT_NE(patched_main_c.find("motor_channel0.OCR = 750;"), std::string::npos);

    std::string original_without_ocr = original_main_c;
    std::string patched_without_ocr = patched_main_c;
    const std::string original_line = "motor_channel0.OCR = 500;";
    const std::string patched_line = "motor_channel0.OCR = 750;";
    original_without_ocr.erase(original_without_ocr.find(original_line), original_line.size());
    patched_without_ocr.erase(patched_without_ocr.find(patched_line), patched_line.size());
    EXPECT_EQ(original_without_ocr, patched_without_ocr);
}

TEST_F(ReverseImportIntegrationTest, MergesIdenticallyConfiguredGpioBlocksIntoOneGroup)
{
    // Given: ровно паттерн mik32_fixed_v3 — HAL_GPIO_Init вызван отдельно на
    // каждый пин, даже когда mode/pull/drive_strength у соседних пинов
    // совпадают (тот самый случай, из-за которого GPIO_1 показывал 11
    // "групп" вместо 2-3 естественных).
    writeFile("src/main.c", R"(
#include "mik32_hal.h"

GPIO_InitTypeDef GPIO_InitStruct;

void GPIO_Demo_Init()
{
    GPIO_InitStruct.Pin = GPIO_PIN_0;
    GPIO_InitStruct.Mode = HAL_GPIO_MODE_GPIO_OUTPUT;
    GPIO_InitStruct.Pull = HAL_GPIO_PULL_NONE;
    HAL_GPIO_Init(GPIO_1, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = GPIO_PIN_1;
    GPIO_InitStruct.Mode = HAL_GPIO_MODE_GPIO_OUTPUT;
    GPIO_InitStruct.Pull = HAL_GPIO_PULL_NONE;
    HAL_GPIO_Init(GPIO_1, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = GPIO_PIN_2;
    GPIO_InitStruct.Mode = HAL_GPIO_MODE_ANALOG;
    GPIO_InitStruct.Pull = HAL_GPIO_PULL_NONE;
    HAL_GPIO_Init(GPIO_1, &GPIO_InitStruct);
}
)");

    const ImportedProject imported = importProjectFromDirectory(root_.string());

    // Then: два output/none пина слились в одну группу с pins=[0,1]; analog
    // пин остался отдельной группой (другой mode).
    ASSERT_EQ(imported.document["peripherals"].size(), 2u);

    int output_index = -1, analog_index = -1;
    for (std::size_t i = 0; i < imported.document["peripherals"].size(); ++i)
    {
        const auto& block = imported.document["peripherals"][i];
        if (block["mode"] == "output") output_index = static_cast<int>(i);
        if (block["mode"] == "analog") analog_index = static_cast<int>(i);
    }
    ASSERT_NE(output_index, -1);
    ASSERT_NE(analog_index, -1);

    EXPECT_EQ(imported.document["peripherals"][output_index]["pins"], nlohmann::json({0, 1}));
    EXPECT_EQ(imported.document["peripherals"][analog_index]["pins"], nlohmann::json({2}));

    // "pins" объединённой группы — read-only; mode/pull/drive_strength — editable.
    const std::vector<FieldProvenance>& output_provenance = imported.block_provenance[output_index];
    for (const FieldProvenance& p : output_provenance)
    {
        if (p.json_field == "pins")
        {
            EXPECT_FALSE(p.editable);
        }
        if (p.json_field == "mode" || p.json_field == "pull")
        {
            EXPECT_TRUE(p.editable);
        }
    }
}

TEST_F(ReverseImportIntegrationTest, DoesNotMergeGpioBlocksWithDifferentConfiguration)
{
    writeFile("src/main.c", R"(
#include "mik32_hal.h"

GPIO_InitTypeDef GPIO_InitStruct;

void GPIO_Demo_Init()
{
    GPIO_InitStruct.Pin = GPIO_PIN_0;
    GPIO_InitStruct.Mode = HAL_GPIO_MODE_GPIO_OUTPUT;
    GPIO_InitStruct.Pull = HAL_GPIO_PULL_NONE;
    HAL_GPIO_Init(GPIO_1, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = GPIO_PIN_1;
    GPIO_InitStruct.Mode = HAL_GPIO_MODE_GPIO_INPUT;
    GPIO_InitStruct.Pull = HAL_GPIO_PULL_UP;
    HAL_GPIO_Init(GPIO_1, &GPIO_InitStruct);
}
)");

    const ImportedProject imported = importProjectFromDirectory(root_.string());
    ASSERT_EQ(imported.document["peripherals"].size(), 2u);
}

TEST_F(ReverseImportIntegrationTest, AttachesGpioInterruptLineToMatchingPinBlock)
{
    // Given: docs/examples/mik32-examples/HAL_Blink_IRQ — вызов
    // HAL_GPIO_InitInterruptLine не принимает хендл вообще, поэтому обычный
    // RawInitBlock/trailing_calls механизм его не видит (см.
    // attachGpioInterrupts в reverse_import.cpp).
    writeFile("src/main.c", R"(
#include "mik32_hal.h"

GPIO_InitTypeDef GPIO_InitStruct;

void GPIO_Init()
{
    GPIO_InitStruct.Pin = GPIO_PIN_6;
    GPIO_InitStruct.Mode = HAL_GPIO_MODE_GPIO_INPUT;
    GPIO_InitStruct.Pull = HAL_GPIO_PULL_NONE;
    HAL_GPIO_Init(GPIO_2, &GPIO_InitStruct);

    HAL_GPIO_InitInterruptLine(GPIO_MUX_PORT2_6_LINE_2, GPIO_INT_MODE_RISING);
}
)");

    const ImportedProject imported = importProjectFromDirectory(root_.string());

    ASSERT_EQ(imported.document["peripherals"].size(), 1u);
    const auto& block = imported.document["peripherals"][0];
    ASSERT_TRUE(block.contains("interrupts"));
    ASSERT_EQ(block["interrupts"].size(), 1u);
    EXPECT_EQ(block["interrupts"][0]["pin"], 6);
    EXPECT_EQ(block["interrupts"][0]["line"], 2);
    EXPECT_EQ(block["interrupts"][0]["trigger"], "rising");
}

TEST_F(ReverseImportIntegrationTest, GpioInterruptLineForUnknownPinIsReportedNotSilentlyDropped)
{
    writeFile("src/main.c", R"(
#include "mik32_hal.h"

GPIO_InitTypeDef GPIO_InitStruct;

void GPIO_Init()
{
    GPIO_InitStruct.Pin = GPIO_PIN_5;
    GPIO_InitStruct.Mode = HAL_GPIO_MODE_GPIO_INPUT;
    GPIO_InitStruct.Pull = HAL_GPIO_PULL_NONE;
    HAL_GPIO_Init(GPIO_2, &GPIO_InitStruct);

    HAL_GPIO_InitInterruptLine(GPIO_MUX_PORT2_6_LINE_2, GPIO_INT_MODE_RISING);
}
)");

    const ImportedProject imported = importProjectFromDirectory(root_.string());

    ASSERT_EQ(imported.document["peripherals"].size(), 1u);
    EXPECT_FALSE(imported.document["peripherals"][0].contains("interrupts"));

    const bool has_note = std::any_of(imported.notes.begin(), imported.notes.end(),
        [](const std::string& note) { return note.find("не найденный ни в одном распознанном блоке GPIO") != std::string::npos; });
    EXPECT_TRUE(has_note);
}

TEST_F(ReverseImportIntegrationTest, ChangingUnmappedFieldIsReportedAsUnpatchableNotSilentlyDropped)
{
    writeFile("src/devices.c", kDevicesC);

    const ImportedProject imported = importProjectFromDirectory(root_.string());
    nlohmann::json edited_peripherals = imported.document["peripherals"];

    int i2c_index = -1;
    for (std::size_t i = 0; i < edited_peripherals.size(); ++i)
    {
        if (edited_peripherals[i]["type"] == "i2c")
        {
            i2c_index = static_cast<int>(i);
        }
    }
    ASSERT_NE(i2c_index, -1);

    // clock_speed — сознательно нередактируемое поле (см. i2c_reverse_mapper) —
    // изменение в GUI не должно молча потеряться, оно обязано попасть в unpatchable.
    edited_peripherals[i2c_index]["clock_speed"] = 400000;

    const ApplyReport report = applyImportedChanges(
        imported.document["peripherals"], edited_peripherals, imported.block_provenance);

    ASSERT_FALSE(report.unpatchable.empty());
    bool found = false;
    for (const UnpatchableChange& change : report.unpatchable)
    {
        if (change.field_name == "clock_speed")
        {
            found = true;
        }
    }
    EXPECT_TRUE(found);
}

// Реальный случай с docs/examples/mik32/controlFirmware/src/utils.cpp:
// функция-обёртка временно переводит I2C-пины в чистый GPIO для бит-бэнгинга
// шины и возвращает их обратно, но единственный вызов этой обёртки
// закомментирован — её GPIO-блок не должен создавать конфликт с реальным I2C.
TEST_F(ReverseImportIntegrationTest, InitBlockInsideFunctionWithOnlyCommentedOutCallIsSkipped)
{
    writeFile("src/main.c", R"(
#include "mik32_hal.h"

I2C_HandleTypeDef hi2c1;
GPIO_InitTypeDef GPIO_InitStruct;

void tca_select(uint8_t channel)
{
    //i2c_bus_recovery(hi2c1, 32000000, 100000);
}

void i2c_bus_recovery(I2C_HandleTypeDef &i2c, uint32_t clock_frequency, uint32_t i2c_frequency)
{
    GPIO_InitStruct.Pin = GPIO_PIN_12;
    GPIO_InitStruct.Mode = HAL_GPIO_MODE_GPIO_OUTPUT;
    GPIO_InitStruct.Pull = HAL_GPIO_PULL_UP;
    HAL_GPIO_Init(GPIO_1, &GPIO_InitStruct);
}

void I2C1_Init()
{
    hi2c1.Instance = I2C_1;
    hi2c1.Init.Mode = HAL_I2C_MODE_MASTER;
    HAL_I2C_Init(&hi2c1);
}
)");

    const ImportedProject imported = importProjectFromDirectory(root_.string());

    std::set<std::string> types;
    for (const auto& block : imported.document["peripherals"])
    {
        types.insert(block.value("type", std::string()));
    }
    EXPECT_EQ(types, (std::set<std::string>{"i2c"}));

    const bool has_note = std::any_of(imported.notes.begin(), imported.notes.end(),
        [](const std::string& note) { return note.find("i2c_bus_recovery") != std::string::npos; });
    EXPECT_TRUE(has_note);
}

} // namespace
