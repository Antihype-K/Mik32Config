#ifndef WIN_FILE_IO_H
#define WIN_FILE_IO_H

#include <string>

#ifdef _WIN32
#include <windows.h>
#else
#include "utf8_path.h"
#include <cerrno>
#include <cstring>
#include <fstream>
#include <sstream>
#endif

// Низкоуровневое чтение/запись файла по UTF-8 пути, в обход std::ifstream/
// std::ofstream от std::filesystem::path.
//
// Зачем это отдельный слой поверх utf8Path() (include/utf8_path.h). На
// реальной Windows (не под Wine) воспроизведена ошибка «Не удалось открыть
// knowledge_base» на существующем файле даже после перевода всех путей на
// std::filesystem::u8path — то есть сама конвертация UTF-8 -> путь, судя по
// всему, отрабатывает верно (иначе путь в диалоге ошибки выглядел бы
// побитым, а он читается нормально), но чтение через
// std::ifstream(const std::filesystem::path&) всё равно не открывает файл.
// Разобраться, какая именно прослойка MinGW/libstdc++ (basic_filebuf,
// __basic_file<char>::open, native()/c_str() у fs::path) ломается на
// конкретной машине пользователя, без доступа к этой машине нельзя — Wine
// эту ошибку не воспроизводит, кросс-компиляция под ней проходит успешно.
//
// Поэтому вместо очередной догадки поверх той же цепочки эта прослойка
// убирает саму цепочку: на Windows путь переводится в UTF-16 явным вызовом
// MultiByteToWideChar и файл открывается CreateFileW/ReadFile/WriteFile
// напрямую — три WinAPI-вызова, которые можно один за другим проверить по
// коду возврата, а не полагаться на то, как их обернул рантайм MinGW. На
// остальных платформах путь остаётся прежним: utf8Path() + std::ifstream/
// std::ofstream, что уже проверено (27/27 тестов на хосте).
struct FileReadOutcome
{
    bool success = false;
    std::string content;     // валиден только при success == true
    std::string diagnostic;  // человекочитаемая деталь ОС; пусто при success
};

struct FileWriteOutcome
{
    bool success = false;
    std::string diagnostic;  // человекочитаемая деталь ОС; пусто при success
};

#ifdef _WIN32
namespace win_file_io_detail
{
    // MultiByteToWideChar вызывается напрямую, а не через
    // std::filesystem::u8path, чтобы конвертация пути была тем же самым
    // WinAPI-вызовом, что и последующее открытие файла — единый механизм
    // проще проверить целиком, чем гадать про стык std::filesystem/CRT.
    inline std::wstring utf8ToWide(const std::string& utf8_text)
    {
        if (utf8_text.empty())
        {
            return std::wstring();
        }
        const int wide_len = MultiByteToWideChar(CP_UTF8, 0, utf8_text.c_str(), -1, nullptr, 0);
        if (wide_len <= 0)
        {
            return std::wstring();
        }
        std::wstring wide_text(static_cast<size_t>(wide_len), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, utf8_text.c_str(), -1, wide_text.data(), wide_len);
        // MultiByteToWideChar с cchWideChar, посчитанным через -1 в cbMultiByte,
        // включает завершающий L'\0' в результат и в его длину — обрезаем его,
        // иначе он попадёт как лишний символ в std::wstring::size().
        if (!wide_text.empty() && wide_text.back() == L'\0')
        {
            wide_text.pop_back();
        }
        return wide_text;
    }

    inline std::string diagnosticFor(const char* winapi_call, DWORD error_code)
    {
        return std::string(" (") + winapi_call + ": код ошибки " + std::to_string(error_code) + ")";
    }
}
#endif

inline FileReadOutcome readUtf8PathFile(const std::string& utf8_path)
{
#ifdef _WIN32
    const std::wstring wide_path = win_file_io_detail::utf8ToWide(utf8_path);
    HANDLE file_handle = CreateFileW(wide_path.c_str(), GENERIC_READ, FILE_SHARE_READ,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file_handle == INVALID_HANDLE_VALUE)
    {
        return FileReadOutcome{false, {}, win_file_io_detail::diagnosticFor("CreateFileW", GetLastError())};
    }

    LARGE_INTEGER file_size;
    if (!GetFileSizeEx(file_handle, &file_size))
    {
        const DWORD error_code = GetLastError();
        CloseHandle(file_handle);
        return FileReadOutcome{false, {}, win_file_io_detail::diagnosticFor("GetFileSizeEx", error_code)};
    }

    std::string content(static_cast<size_t>(file_size.QuadPart), '\0');
    DWORD bytes_read = 0;
    const BOOL read_ok = content.empty()
        ? TRUE
        : ReadFile(file_handle, content.data(), static_cast<DWORD>(content.size()), &bytes_read, nullptr);
    const DWORD error_code = read_ok ? 0 : GetLastError();
    CloseHandle(file_handle);
    if (!read_ok || bytes_read != content.size())
    {
        return FileReadOutcome{false, {}, win_file_io_detail::diagnosticFor("ReadFile", error_code)};
    }
    return FileReadOutcome{true, std::move(content), {}};
#else
    std::ifstream input_file(utf8Path(utf8_path), std::ios::binary);
    if (!input_file.is_open())
    {
        return FileReadOutcome{false, {}, std::string(" (errno ") + std::to_string(errno) + ": " + std::strerror(errno) + ")"};
    }
    std::ostringstream buffer;
    buffer << input_file.rdbuf();
    return FileReadOutcome{true, buffer.str(), {}};
#endif
}

inline FileWriteOutcome writeUtf8PathFile(const std::string& utf8_path, const std::string& content)
{
#ifdef _WIN32
    const std::wstring wide_path = win_file_io_detail::utf8ToWide(utf8_path);
    HANDLE file_handle = CreateFileW(wide_path.c_str(), GENERIC_WRITE, 0,
        nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file_handle == INVALID_HANDLE_VALUE)
    {
        return FileWriteOutcome{false, win_file_io_detail::diagnosticFor("CreateFileW", GetLastError())};
    }

    DWORD bytes_written = 0;
    const BOOL write_ok = content.empty()
        ? TRUE
        : WriteFile(file_handle, content.data(), static_cast<DWORD>(content.size()), &bytes_written, nullptr);
    const DWORD error_code = write_ok ? 0 : GetLastError();
    CloseHandle(file_handle);
    if (!write_ok || bytes_written != content.size())
    {
        return FileWriteOutcome{false, win_file_io_detail::diagnosticFor("WriteFile", error_code)};
    }
    return FileWriteOutcome{true, {}};
#else
    std::ofstream output_file(utf8Path(utf8_path), std::ios::trunc | std::ios::binary);
    if (!output_file.is_open())
    {
        return FileWriteOutcome{false, std::string(" (errno ") + std::to_string(errno) + ": " + std::strerror(errno) + ")"};
    }
    output_file << content;
    if (!output_file.good())
    {
        return FileWriteOutcome{false, " (запись не завершилась успешно)"};
    }
    return FileWriteOutcome{true, {}};
#endif
}

#endif // WIN_FILE_IO_H
