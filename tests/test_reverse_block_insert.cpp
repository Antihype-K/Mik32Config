//
// Точечная вставка НОВОГО блока (добавленного в GUI после reverse-import) —
// проверка на настоящем временном дереве файлов, той же формы, что
// test_reverse_import_integration.cpp: минимальный main.c/mik32_init.c/.h,
// как их оставляет этот же конфигуратор при предыдущей генерации.
//
#include <gtest/gtest.h>

#include <reverse_block_insert.h>
#include <i2c_knowledge.h>
#include <gpio_knowledge.h>
#include <dac_knowledge.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;
using namespace reverse_import;

namespace
{

class ReverseBlockInsertTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        root_ = fs::temp_directory_path() / fs::path(
            "mik32_reverse_block_insert_" + std::to_string(::testing::UnitTest::GetInstance()->random_seed()) +
            "_" + std::to_string(reinterpret_cast<std::uintptr_t>(this)));
        fs::create_directories(root_ / "src");

        writeFile("src/mik32_init.h",
            "#ifndef MIK32_INIT_H\n"
            "#define MIK32_INIT_H\n"
            "\n"
            "void GPIO_0_Init(void);\n"
            "\n"
            "#endif //MIK32_INIT_H\n");

        writeFile("src/mik32_init.c",
            "#include \"mik32_init.h\"\n"
            "#include \"mik32_hal_gpio.h\"\n"
            "\n"
            "void GPIO_0_Init(void)\n"
            "{\n"
            "    HAL_GPIO_Init(GPIO_0, &(GPIO_InitTypeDef){0});\n"
            "}\n");

        writeFile("src/main.c",
            "#include \"mik32_hal_pcc.h\"\n"
            "#include \"mik32_init.h\"\n"
            "\n"
            "int main(void)\n"
            "{\n"
            "    GPIO_0_Init();\n"
            "    while (1)\n"
            "    {\n"
            "    }\n"
            "}\n");

        ParseResult<I2cKnowledge> i2c_knowledge_result = loadI2cKnowledge("../knowledge_base/i2c.json");
        ASSERT_TRUE(i2c_knowledge_result.success) << i2c_knowledge_result.error;
        knowledge_.i2c = *i2c_knowledge_result.config;

        ParseResult<GpioKnowledge> gpio_knowledge_result = loadGpioKnowledge("../knowledge_base/gpio.json");
        ASSERT_TRUE(gpio_knowledge_result.success) << gpio_knowledge_result.error;
        knowledge_.gpio = *gpio_knowledge_result.config;

        ParseResult<DacKnowledge> dac_knowledge_result = loadDacKnowledge("../knowledge_base/dac.json");
        ASSERT_TRUE(dac_knowledge_result.success) << dac_knowledge_result.error;
        knowledge_.dac = *dac_knowledge_result.config;
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

    std::vector<std::string> filesScanned() const
    {
        return {
            (root_ / "src/main.c").u8string(),
            (root_ / "src/mik32_init.c").u8string(),
            (root_ / "src/mik32_init.h").u8string(),
        };
    }

    fs::path root_;
    ProjectKnowledge knowledge_;
};

TEST_F(ReverseBlockInsertTest, InsertsNewI2cBlockCallDefinitionAndPrototype)
{
    nlohmann::json document;
    document["mcu"] = "MIK32";
    document["version"] = "2.0";
    document["peripherals"] = nlohmann::json::array();
    document["peripherals"].push_back({
        {"type", "i2c"},
        {"instance", 0},
        {"mode", "master"},
        {"clock_speed", 100000},
    });

    const BlockInsertResult result = insertNewPeripheralBlock(
        root_.u8string(), filesScanned(), document, 0, knowledge_);
    ASSERT_TRUE(result.success) << result.error;

    const std::string main_c = readFile("src/main.c");
    // Вызов нового Init() вставлен ПОСЛЕ существующего GPIO_0_Init(), сам
    // GPIO_0_Init() строка осталась на месте (якорь не затёрт).
    const std::size_t gpio_call_pos = main_c.find("GPIO_0_Init();");
    const std::size_t i2c_call_pos = main_c.find("I2C_0_Init();");
    ASSERT_NE(gpio_call_pos, std::string::npos);
    ASSERT_NE(i2c_call_pos, std::string::npos);
    EXPECT_GT(i2c_call_pos, gpio_call_pos);

    const std::string init_c = readFile("src/mik32_init.c");
    EXPECT_NE(init_c.find("void I2C_0_Init(void)"), std::string::npos);
    // Старая функция GPIO_0_Init осталась нетронутой.
    EXPECT_NE(init_c.find("void GPIO_0_Init(void)"), std::string::npos);

    const std::string init_h = readFile("src/mik32_init.h");
    EXPECT_NE(init_h.find("void I2C_0_Init(void);"), std::string::npos);
    EXPECT_NE(init_h.find("void GPIO_0_Init(void);"), std::string::npos);
}

TEST_F(ReverseBlockInsertTest, InsertsNewCryptoBlockGlobalSingleton)
{
    nlohmann::json document;
    document["mcu"] = "MIK32";
    document["version"] = "2.0";
    document["peripherals"] = nlohmann::json::array();
    document["peripherals"].push_back({
        {"type", "crypto"},
        {"algorithm", "aes"},
        {"cipher_mode", "ecb"},
    });

    const BlockInsertResult result = insertNewPeripheralBlock(
        root_.u8string(), filesScanned(), document, 0, knowledge_);
    ASSERT_TRUE(result.success) << result.error;

    const std::string main_c = readFile("src/main.c");
    EXPECT_NE(main_c.find("CRYPTO_Init();"), std::string::npos);
    const std::string init_c = readFile("src/mik32_init.c");
    EXPECT_NE(init_c.find("void CRYPTO_Init(void)"), std::string::npos);
}

TEST_F(ReverseBlockInsertTest, InsertsNewWdtBlockGlobalSingleton)
{
    nlohmann::json document;
    document["mcu"] = "MIK32";
    document["version"] = "2.0";
    document["peripherals"] = nlohmann::json::array();
    document["peripherals"].push_back({
        {"type", "wdt"},
        {"clock_source", "osc32k"},
        {"reload_ms", 500},
    });

    const BlockInsertResult result = insertNewPeripheralBlock(
        root_.u8string(), filesScanned(), document, 0, knowledge_);
    ASSERT_TRUE(result.success) << result.error;

    const std::string main_c = readFile("src/main.c");
    EXPECT_NE(main_c.find("WDT_Init();"), std::string::npos);
}

TEST_F(ReverseBlockInsertTest, InsertsNewGpioBlockWithOwnInstance)
{
    nlohmann::json document;
    document["mcu"] = "MIK32";
    document["version"] = "2.0";
    document["peripherals"] = nlohmann::json::array();
    document["peripherals"].push_back({
        {"type", "gpio"},
        {"instance", 1},
        {"mode", "output"},
        {"pull", "none"},
        {"drive_strength", "4mA"},
        {"pins", nlohmann::json::array({3})},
    });

    const BlockInsertResult result = insertNewPeripheralBlock(
        root_.u8string(), filesScanned(), document, 0, knowledge_);
    ASSERT_TRUE(result.success) << result.error;

    const std::string main_c = readFile("src/main.c");
    EXPECT_NE(main_c.find("GPIO_1_Init();"), std::string::npos);
    const std::string init_c = readFile("src/mik32_init.c");
    EXPECT_NE(init_c.find("void GPIO_1_Init(void)"), std::string::npos);
}

TEST_F(ReverseBlockInsertTest, AnchorStaysInsideMainEvenWhenLaterFunctionHasMatchingInitCallLine)
{
    // Регрессия на реальный кейс: HAL_ADC_MspInit() стоит В ФАЙЛЕ НИЖЕ main()
    // и содержит "HAL_GPIO_Init(GPIO_1, &GPIO_InitStruct);" — строка матчит
    // тот же паттерн "<Имя>_Init(...);", что и якорь вставки. До фикса
    // findLastInitCallLine сканировала весь файл и якорь уезжал в MspInit.
    writeFile("src/main.c",
        "#include \"mik32_hal_pcc.h\"\n"
        "#include \"mik32_init.h\"\n"
        "\n"
        "int main(void)\n"
        "{\n"
        "    GPIO_0_Init();\n"
        "    while (1)\n"
        "    {\n"
        "    }\n"
        "}\n"
        "\n"
        "void HAL_ADC_MspInit(ADC_HandleTypeDef *hadc)\n"
        "{\n"
        "    GPIO_InitTypeDef GPIO_InitStruct = {0};\n"
        "    HAL_GPIO_Init(GPIO_1, &GPIO_InitStruct);\n"
        "}\n");

    nlohmann::json document;
    document["mcu"] = "MIK32";
    document["version"] = "2.0";
    document["peripherals"] = nlohmann::json::array();
    document["peripherals"].push_back({
        {"type", "dac"},
        {"instance", 1},
        {"div", 0},
        {"ext_ref", false},
        {"ext_clb", false},
    });

    const BlockInsertResult result = insertNewPeripheralBlock(
        root_.u8string(), filesScanned(), document, 0, knowledge_);
    ASSERT_TRUE(result.success) << result.error;

    const std::string main_c = readFile("src/main.c");
    const std::size_t main_open = main_c.find("int main(void)");
    const std::size_t main_close = main_c.find("HAL_ADC_MspInit");
    const std::size_t dac_call_pos = main_c.find("DAC_1_Init();");
    ASSERT_NE(dac_call_pos, std::string::npos);
    EXPECT_GT(dac_call_pos, main_open);
    EXPECT_LT(dac_call_pos, main_close); // вызов внутри main(), не в MspInit ниже
}

TEST_F(ReverseBlockInsertTest, VendorProjectWithoutInitFilesInsertsDirectlyIntoMainC)
{
    // Вендорский проект без mik32_init.c/h — только main.c (типовая
    // структура примеров из docs/examples/mik32-examples/*).
    const fs::path vendor_root = fs::temp_directory_path() / fs::path(
        "mik32_reverse_block_insert_vendor_" + std::to_string(::testing::UnitTest::GetInstance()->random_seed()));
    fs::create_directories(vendor_root / "src");
    {
        std::ofstream stream(vendor_root / "src/main.c", std::ios::binary);
        stream <<
            "#include \"mik32_hal_pcc.h\"\n"
            "\n"
            "void GPIO_0_Init(void)\n"
            "{\n"
            "}\n"
            "\n"
            "int main(void)\n"
            "{\n"
            "    GPIO_0_Init();\n"
            "    while (1)\n"
            "    {\n"
            "    }\n"
            "}\n";
    }

    nlohmann::json document;
    document["mcu"] = "MIK32";
    document["version"] = "2.0";
    document["peripherals"] = nlohmann::json::array();
    document["peripherals"].push_back({
        {"type", "i2c"},
        {"instance", 0},
        {"mode", "master"},
        {"clock_speed", 100000},
    });

    const std::vector<std::string> files_scanned = {(vendor_root / "src/main.c").u8string()};
    const BlockInsertResult result = insertNewPeripheralBlock(
        vendor_root.u8string(), files_scanned, document, 0, knowledge_);
    ASSERT_TRUE(result.success) << result.error;

    std::ifstream stream(vendor_root / "src/main.c", std::ios::binary);
    const std::string main_c((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());

    // Определение новой функции стоит перед main() (нет mik32_init.c/h, прототип не нужен).
    const std::size_t def_pos = main_c.find("void I2C_0_Init(void)");
    const std::size_t main_pos = main_c.find("int main(void)");
    const std::size_t call_pos = main_c.find("I2C_0_Init();");
    ASSERT_NE(def_pos, std::string::npos);
    ASSERT_NE(main_pos, std::string::npos);
    ASSERT_NE(call_pos, std::string::npos);
    EXPECT_LT(def_pos, main_pos);
    EXPECT_GT(call_pos, main_pos);

    std::error_code ignored;
    fs::remove_all(vendor_root, ignored);
}

TEST_F(ReverseBlockInsertTest, UnsupportedTypeReportsErrorInsteadOfGuessing)
{
    nlohmann::json document;
    document["mcu"] = "MIK32";
    document["version"] = "2.0";
    document["peripherals"] = nlohmann::json::array();
    document["peripherals"].push_back({
        {"type", "gpio"},
        {"instance", 1},
        {"pins", nlohmann::json::array({
            {{"pin", "PORT_0_0"}, {"mode", "output"}}
        })},
    });

    const BlockInsertResult result = insertNewPeripheralBlock(
        root_.u8string(), filesScanned(), document, 0, knowledge_);
    EXPECT_FALSE(result.success);
    EXPECT_NE(result.error.find("gpio"), std::string::npos);
}

} // namespace
