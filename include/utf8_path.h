#ifndef UTF8_PATH_H
#define UTF8_PATH_H

#include <filesystem>
#include <string>

// Преобразование пути из UTF-8 std::string в std::filesystem::path.
//
// Зачем это нужно. На Linux путь — просто последовательность байт, и
// std::ifstream(std::string) работает с UTF-8 сам собой. На Windows файловая
// система хранит имена в UTF-16, а узкие функции CRT (fopen и всё, что на нём
// построено, включая конструкторы потоков от std::string и неявное
// преобразование std::string -> fs::path) трактуют байты в текущей ANSI-кодировке,
// то есть в русской локали — в CP1251. Байты UTF-8 в этом случае читаются как
// мусор, и файл не находится.
//
// Ошибка не теоретическая: GUI берёт каталог через
// QCoreApplication::applicationDirPath() и передаёт его как
// QString::toStdString(), а это UTF-8. При запуске из
// C:\Users\Студент\... получалось «Не удалось открыть knowledge_base»
// на существующем файле — воспроизведено на реальной Windows.
//
// std::filesystem::u8path выполняет ровно нужное: трактует входные байты как
// UTF-8 и конвертирует их в машинное представление пути (на Windows — UTF-16,
// на Linux — те же байты без изменений). В C++17 это штатный инструмент;
// он объявлен устаревшим только с C++20, а проект зафиксирован на C++17
// (CMakeLists.txt, CMAKE_CXX_STANDARD 17).
//
// Соглашение по проекту: любой путь, живущий в std::string, считается UTF-8.
// За приведение аргументов командной строки к этому виду отвечает
// include/utf8_arguments.h.
inline std::filesystem::path utf8Path(const std::string& utf8_path)
{
    return std::filesystem::u8path(utf8_path);
}

#endif // UTF8_PATH_H
