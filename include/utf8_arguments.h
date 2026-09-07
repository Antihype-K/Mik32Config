#ifndef UTF8_ARGUMENTS_H
#define UTF8_ARGUMENTS_H

#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

// Приведение аргументов командной строки к UTF-8 и настройка вывода консоли.
//
// Зачем это нужно. Проект принял соглашение: путь в std::string — всегда UTF-8
// (см. include/utf8_path.h). Но на Windows argv, который CRT передаёт в main(),
// закодирован в ANSI-кодировке процесса (в русской локали — CP1251), а не в
// UTF-8. Если не привести его, то путь с кириллицей, набранный в консоли,
// попадёт в utf8Path как невалидный UTF-8 и файл не откроется — то есть
// исправление UTF-8-путей само по себе сломало бы консольный сценарий,
// который до него работал случайно (ANSI-байты уходили в ANSI-функцию fopen).
//
// На Linux argv и так UTF-8, поэтому обе функции ниже там ничего не делают.
namespace utf8_arguments
{
#ifdef _WIN32
    // Перекодировка одной строки ANSI -> UTF-8 через UTF-16 как посредника:
    // MultiByteToWideChar/WideCharToMultiByte — единственный корректный способ,
    // побайтовые таблицы тут неприменимы. Обе функции из kernel32, так что
    // дополнительных библиотек линковать не нужно.
    inline std::string ansiToUtf8(const char* ansi_text)
    {
        const int wide_length = MultiByteToWideChar(CP_ACP, 0, ansi_text, -1, nullptr, 0);
        if (wide_length <= 0)
        {
            return std::string(ansi_text);
        }

        std::wstring wide_text(static_cast<size_t>(wide_length), L'\0');
        MultiByteToWideChar(CP_ACP, 0, ansi_text, -1, &wide_text[0], wide_length);

        const int utf8_length =
            WideCharToMultiByte(CP_UTF8, 0, wide_text.c_str(), -1, nullptr, 0, nullptr, nullptr);
        if (utf8_length <= 0)
        {
            return std::string(ansi_text);
        }

        std::string utf8_text(static_cast<size_t>(utf8_length), '\0');
        WideCharToMultiByte(CP_UTF8, 0, wide_text.c_str(), -1, &utf8_text[0], utf8_length,
                            nullptr, nullptr);

        // Обе WinAPI-функции считают завершающий '\0' частью строки, когда длина
        // задана как -1, поэтому он попадает в размер и его нужно отрезать.
        utf8_text.resize(static_cast<size_t>(utf8_length) - 1);
        return utf8_text;
    }
#endif

    // Аргументы в UTF-8. Возвращается вектор, а не char**, чтобы владение
    // строками было явным и не потребовалось ручное освобождение.
    inline std::vector<std::string> toUtf8(int argc, char** argv)
    {
        std::vector<std::string> arguments;
        arguments.reserve(static_cast<size_t>(argc));
        for (int i = 0; i < argc; ++i)
        {
#ifdef _WIN32
            arguments.push_back(ansiToUtf8(argv[i]));
#else
            arguments.push_back(argv[i]);
#endif
        }
        return arguments;
    }

    // Вывод сообщений программы — UTF-8, а консоль Windows по умолчанию работает
    // в CP866, из-за чего русский текст превращается в кракозябры (наблюдалось
    // на реальной Windows). CP_UTF8 = 65001 — то же, что даёт ручной chcp 65001.
    inline void configureConsole()
    {
#ifdef _WIN32
        SetConsoleOutputCP(CP_UTF8);
#endif
    }
}

#endif // UTF8_ARGUMENTS_H
