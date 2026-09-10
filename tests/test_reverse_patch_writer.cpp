//
// Тесты reverse_patch_writer (parser/reverse_import/reverse_patch_writer.h) —
// генерического построчного патчера, которым reverse-import переписывает
// ровно одну строку не-сгенерированного этим конфигуратором исходника после
// правки поля в GUI. Модуль ещё не подключён в CMakeLists.txt (та же
// ситуация, что у test_cross_module_severity_standalone.cpp — см. комментарий
// там же) — интеграцию делает пользователь сам.
//
// Стиль — GoogleTest, как в остальных tests/*.cpp. В отличие от них этот
// файл не использует tests/fixtures/*.json: каждый тест сам создаёт и
// удаляет одноразовый временный файл в std::filesystem::temp_directory_path(),
// потому что предмет проверки — побайтовая сохранность ЧУЖОГО файла на
// диске, а не разбор JSON-конфигурации.
//

#include <gtest/gtest.h>
#include <reverse_patch_writer.h>

#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;
using reverse_import::LineEditRequest;
using reverse_import::PatchResult;
using reverse_import::SourceLocation;
using reverse_import::applyLineEdit;
using reverse_import::applyLineEdits;

namespace
{

// Уникальный путь во временном каталоге для одного теста — имя включает имя
// теста, чтобы параллельный/повторный запуск ctest не сталкивал разные
// тесты на одном и том же файле.
fs::path uniqueTempFile(const std::string& test_name)
{
    static int counter = 0;
    ++counter;
    return fs::temp_directory_path() /
        ("reverse_patch_writer_test_" + test_name + "_" + std::to_string(counter) + ".c");
}

// Пишет content КАК ЕСТЬ (бинарно, без трансляции \n/\r\n) — тесты сами
// решают, какой стиль перевода строк использовать.
void writeRawFile(const fs::path& path, const std::string& content)
{
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    ASSERT_TRUE(out.is_open()) << path.string();
    out << content;
}

std::string readRawFile(const fs::path& path)
{
    std::ifstream in(path, std::ios::binary);
    EXPECT_TRUE(in.is_open()) << path.string();
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

} // namespace

TEST(ReversePatchWriter, AppliesSingleLineEditAndLeavesRestOfFileByteForByte)
{
    const fs::path path = uniqueTempFile("single_edit");
    const std::string original =
        "void UART_0_Init(void)\n"
        "{\n"
        "    husart0.baudrate = 9600;\n"
        "    husart0.Parity = USART_PARITY_NONE;\n"
        "}\n";
    writeRawFile(path, original);

    LineEditRequest request;
    request.location = SourceLocation{path.string(), 3};
    request.expected_full_line_text = "    husart0.baudrate = 9600;";
    request.expected_value_text = "9600";
    request.new_value_text = "38400";

    PatchResult result = applyLineEdit(request);
    ASSERT_TRUE(result.success) << result.error;

    const std::string expected =
        "void UART_0_Init(void)\n"
        "{\n"
        "    husart0.baudrate = 38400;\n"
        "    husart0.Parity = USART_PARITY_NONE;\n"
        "}\n";
    EXPECT_EQ(readRawFile(path), expected);

    fs::remove(path);
}

TEST(ReversePatchWriter, RejectsWhenAnchorLineChangedSinceImportAndLeavesFileUntouched)
{
    const fs::path path = uniqueTempFile("anchor_mismatch");
    const std::string original =
        "void UART_0_Init(void)\n"
        "    husart0.baudrate = 19200;\n"  // на диске уже НЕ 9600 (якорь стухший)
        "}\n";
    writeRawFile(path, original);

    LineEditRequest request;
    request.location = SourceLocation{path.string(), 2};
    request.expected_full_line_text = "    husart0.baudrate = 9600;";  // старый снимок
    request.expected_value_text = "9600";
    request.new_value_text = "38400";

    PatchResult result = applyLineEdit(request);
    EXPECT_FALSE(result.success);
    EXPECT_NE(result.error.find("изменилась с момента импорта"), std::string::npos) << result.error;
    EXPECT_NE(result.error.find(path.string() + ":2"), std::string::npos) << result.error;

    // Файл не тронут вообще, включая право на неизменность байт.
    EXPECT_EQ(readRawFile(path), original);

    fs::remove(path);
}

TEST(ReversePatchWriter, RejectsWhenExpectedValueNotFoundInRightHandSide)
{
    const fs::path path = uniqueTempFile("value_not_found");
    const std::string original = "    husart0.baudrate = 9600;\n";
    writeRawFile(path, original);

    LineEditRequest request;
    request.location = SourceLocation{path.string(), 1};
    request.expected_full_line_text = "    husart0.baudrate = 9600;";
    request.expected_value_text = "115200";   // не встречается в правой части строки
    request.new_value_text = "38400";

    PatchResult result = applyLineEdit(request);
    EXPECT_FALSE(result.success);
    EXPECT_NE(result.error.find("не найдено"), std::string::npos) << result.error;

    EXPECT_EQ(readRawFile(path), original);

    fs::remove(path);
}

TEST(ReversePatchWriter, ApplyLineEditsAppliesTwoEditsInSameFileBothCorrectly)
{
    const fs::path path = uniqueTempFile("batch_same_file");
    const std::string original =
        "    husart0.baudrate = 9600;\n"
        "    husart0.Parity = USART_PARITY_NONE;\n";
    writeRawFile(path, original);

    std::vector<LineEditRequest> requests(2);
    requests[0].location = SourceLocation{path.string(), 1};
    requests[0].expected_full_line_text = "    husart0.baudrate = 9600;";
    requests[0].expected_value_text = "9600";
    requests[0].new_value_text = "38400";

    requests[1].location = SourceLocation{path.string(), 2};
    requests[1].expected_full_line_text = "    husart0.Parity = USART_PARITY_NONE;";
    requests[1].expected_value_text = "USART_PARITY_NONE";
    requests[1].new_value_text = "USART_PARITY_EVEN";

    std::vector<PatchResult> results = applyLineEdits(requests);
    ASSERT_EQ(results.size(), 2u);
    EXPECT_TRUE(results[0].success) << results[0].error;
    EXPECT_TRUE(results[1].success) << results[1].error;

    const std::string expected =
        "    husart0.baudrate = 38400;\n"
        "    husart0.Parity = USART_PARITY_EVEN;\n";
    EXPECT_EQ(readRawFile(path), expected);

    fs::remove(path);
}

TEST(ReversePatchWriter, PreservesCrlfLineEndingsForChangedAndUnchangedLines)
{
    const fs::path path = uniqueTempFile("crlf");
    const std::string original =
        "void UART_0_Init(void)\r\n"
        "{\r\n"
        "    husart0.baudrate = 9600;\r\n"
        "    husart0.Parity = USART_PARITY_NONE;\r\n"
        "}\r\n";
    writeRawFile(path, original);

    LineEditRequest request;
    request.location = SourceLocation{path.string(), 3};
    // Якорь без \r — терминатор строки в full_line_text не участвует
    // (см. reverse_patch_writer.cpp::splitIntoLines).
    request.expected_full_line_text = "    husart0.baudrate = 9600;";
    request.expected_value_text = "9600";
    request.new_value_text = "38400";

    PatchResult result = applyLineEdit(request);
    ASSERT_TRUE(result.success) << result.error;

    const std::string expected =
        "void UART_0_Init(void)\r\n"
        "{\r\n"
        "    husart0.baudrate = 38400;\r\n"
        "    husart0.Parity = USART_PARITY_NONE;\r\n"
        "}\r\n";
    EXPECT_EQ(readRawFile(path), expected);

    fs::remove(path);
}

TEST(ReversePatchWriter, RejectsEmptyLocationFileWithoutCrashing)
{
    LineEditRequest request;
    request.location = SourceLocation{"", 1};
    request.expected_full_line_text = "x = 1;";
    request.expected_value_text = "1";
    request.new_value_text = "2";

    PatchResult result = applyLineEdit(request);
    EXPECT_FALSE(result.success);
    EXPECT_FALSE(result.error.empty());
}

TEST(ReversePatchWriter, RejectsLineNumberBeyondEndOfFile)
{
    const fs::path path = uniqueTempFile("line_out_of_range");
    writeRawFile(path, "single_line = 1;\n");

    LineEditRequest request;
    request.location = SourceLocation{path.string(), 5};
    request.expected_full_line_text = "single_line = 1;";
    request.expected_value_text = "1";
    request.new_value_text = "2";

    PatchResult result = applyLineEdit(request);
    EXPECT_FALSE(result.success);
    EXPECT_FALSE(result.error.empty());

    fs::remove(path);
}

TEST(ReversePatchWriter, ApplyLineEditsKeepsIndependentFilesIndependent)
{
    // Правка не проходит в одном файле (стухший якорь) — правка в ДРУГОМ
    // файле всё равно применяется и физически записывается на диск (best-
    // effort между файлами, см. .h).
    const fs::path stale_path = uniqueTempFile("independent_stale");
    const fs::path ok_path = uniqueTempFile("independent_ok");
    writeRawFile(stale_path, "    field = 111;\n");
    writeRawFile(ok_path, "    field = 1;\n");

    std::vector<LineEditRequest> requests(2);
    requests[0].location = SourceLocation{stale_path.string(), 1};
    requests[0].expected_full_line_text = "    field = 9;";   // не совпадёт
    requests[0].expected_value_text = "9";
    requests[0].new_value_text = "42";

    requests[1].location = SourceLocation{ok_path.string(), 1};
    requests[1].expected_full_line_text = "    field = 1;";
    requests[1].expected_value_text = "1";
    requests[1].new_value_text = "2";

    std::vector<PatchResult> results = applyLineEdits(requests);
    ASSERT_EQ(results.size(), 2u);
    EXPECT_FALSE(results[0].success);
    EXPECT_TRUE(results[1].success) << results[1].error;

    EXPECT_EQ(readRawFile(stale_path), "    field = 111;\n");
    EXPECT_EQ(readRawFile(ok_path), "    field = 2;\n");

    fs::remove(stale_path);
    fs::remove(ok_path);
}
