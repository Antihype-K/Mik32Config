//
// Регрессионный прогон reverse-import по каталогу вендорных примеров.
//
// Зачем отдельный консольный драйвер, а не GoogleTest-тест: вендорные примеры
// (docs/examples/mik32-examples/*) лежат в gitignored docs/ — их нет ни в CI,
// ни в worktree-копиях репозитория, поэтому превращать их в assert-тест
// нельзя (тест падал бы от отсутствия данных, а не от регрессии). Драйвер
// просто печатает, ЧТО reverse-import распознал в каждом примере, а человек
// сверяет вывод с исходным main.c.
//
// Использование:
//   reverse-import-smoke <каталог-с-примерами> [имя-примера ...]
// Без имён примеров обходит все подкаталоги первого уровня.
//
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include "reverse_import.h"

namespace fs = std::filesystem;

namespace
{

// Печатает один разобранный пример: сначала распознанные блоки периферии
// (тип + все поля блока), затем notes — причины, почему что-то НЕ распознано.
void reportOneExample(const std::string& name, const std::string& dir)
{
    std::cout << "\n=== " << name << " ===\n";

    reverse_import::ImportedProject imported;
    try
    {
        imported = reverse_import::importProjectFromDirectory(dir);
    }
    catch (const std::exception& e)
    {
        // Исключение из reverse-import — всегда баг: контракт функции в
        // reverse_import.h не предусматривает выброса на произвольном вводе.
        std::cout << "  !!! ИСКЛЮЧЕНИЕ: " << e.what() << "\n";
        return;
    }

    const auto& peripherals = imported.document.value(
        "peripherals", nlohmann::json::array());

    std::cout << "  файлов просканировано: " << imported.files_scanned.size()
              << ", блоков периферии: " << peripherals.size() << "\n";

    for (const auto& block : peripherals)
    {
        // dump() одной строкой — блоки короткие, а сверять с main.c удобнее
        // когда один блок = одна строка вывода.
        std::cout << "  [блок] " << block.dump() << "\n";
    }

    for (const auto& note : imported.notes)
    {
        std::cout << "  [note] " << note << "\n";
    }
}

} // namespace

int main(int argc, char** argv)
{
    if (argc < 2)
    {
        std::cerr << "использование: " << argv[0]
                  << " <каталог-с-примерами> [имя-примера ...]\n";
        return 2;
    }

    const fs::path root = argv[1];
    if (!fs::is_directory(root))
    {
        std::cerr << "не каталог: " << root << "\n";
        return 2;
    }

    // Либо явно перечисленные примеры, либо все подкаталоги первого уровня
    // (файлы вроде README.md/CHANGELOG.md в корне набора примеров — не
    // примеры, поэтому фильтр именно по is_directory).
    std::vector<fs::path> examples;
    if (argc > 2)
    {
        for (int i = 2; i < argc; ++i)
        {
            examples.push_back(root / argv[i]);
        }
    }
    else
    {
        for (const auto& entry : fs::directory_iterator(root))
        {
            // .git и прочие скрытые каталоги — не примеры.
            if (entry.is_directory() && entry.path().filename().string().front() != '.')
            {
                examples.push_back(entry.path());
            }
        }
        std::sort(examples.begin(), examples.end());
    }

    std::cout << "примеров к прогону: " << examples.size() << "\n";
    for (const auto& example : examples)
    {
        reportOneExample(example.filename().string(), example.string());
    }

    return 0;
}
