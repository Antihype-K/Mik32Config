#include "reverse_patch_writer.h"

#include <filesystem>
#include <unordered_map>

#include "utf8_path.h"
#include "win_file_io.h"

namespace reverse_import
{

namespace
{

namespace fs = std::filesystem;

// Одна физическая строка файла, разложенная на содержимое (без терминатора)
// и сам терминатор ("" — последняя строка без завершающего перевода строки,
// "\n" или "\r\n"). Терминатор хранится и восстанавливается ОТДЕЛЬНО от
// содержимого специально для того, чтобы строки, которые правка не
// затрагивает, дописывались назад байт-в-байт, независимо от того, какой
// стиль перевода строк был именно у них (файл в принципе может быть смешанным
// — эта разбивка не требует, чтобы терминатор был одинаковым по всему файлу).
struct FileLine
{
    std::string content;
    std::string terminator;
};

// Разбивает сырой (прочитанный в бинарном режиме, без CRLF-трансляции)
// текст файла на строки. Терминатором считается только символ '\n' —
// предшествующий ему '\r' (если есть) относится к терминатору "\r\n", а не к
// содержимому строки, поэтому сравнение содержимого с expected_full_line_text
// (якорем, полученным сканером через std::getline) не должно видеть разницы
// из-за стиля перевода строк.
std::vector<FileLine> splitIntoLines(const std::string& raw_content)
{
    std::vector<FileLine> lines;
    size_t pos = 0;
    while (pos < raw_content.size())
    {
        const size_t newline_pos = raw_content.find('\n', pos);
        if (newline_pos == std::string::npos)
        {
            // Последняя строка без завершающего перевода строки.
            lines.push_back(FileLine{raw_content.substr(pos), std::string()});
            break;
        }

        std::string content = raw_content.substr(pos, newline_pos - pos);
        std::string terminator = "\n";
        if (!content.empty() && content.back() == '\r')
        {
            content.pop_back();
            terminator = "\r\n";
        }
        lines.push_back(FileLine{std::move(content), std::move(terminator)});
        pos = newline_pos + 1;
    }
    return lines;
}

std::string joinLines(const std::vector<FileLine>& lines)
{
    std::string result;
    for (const FileLine& line : lines)
    {
        result += line.content;
        result += line.terminator;
    }
    return result;
}

std::string locationLabel(const SourceLocation& location)
{
    return location.file + ":" + std::to_string(location.line);
}

// Ядро правки: пытается применить одну правку к уже загруженному в память
// набору строк ОДНОГО файла. Не делает никакого ввода-вывода — этим
// специально управляют вызывающие функции (applyLineEdit/applyLineEdits),
// чтобы batch-вариант мог прогнать несколько правок через один и тот же
// вектор строк перед единственной записью на диск.
bool tryApplyEdit(std::vector<FileLine>& lines, const LineEditRequest& request, std::string& error_out)
{
    // Шаг 1 (часть про "нет такой строки"): номера строк 1-based, как в
    // SourceLocation (см. reverse_import_scanner.h).
    if (request.location.line <= 0 || static_cast<size_t>(request.location.line) > lines.size())
    {
        error_out = "в файле нет строки номер " + std::to_string(request.location.line) +
            " (файл содержит " + std::to_string(lines.size()) + " строк): " + request.location.file;
        return false;
    }

    FileLine& target_line = lines[static_cast<size_t>(request.location.line - 1)];

    // Шаг 2: главное защитное правило — якорь должен совпасть символ в
    // символ. Формулировка ошибки зафиксирована постановкой задачи.
    if (target_line.content != request.expected_full_line_text)
    {
        error_out = "исходная строка изменилась с момента импорта, изменение не применено: " +
            locationLabel(request.location);
        return false;
    }

    // Шаг 3: правая часть присваивания — после последнего '=' и до
    // последнего ';' в строке (правее ';' может быть "// комментарий",
    // который мы обязаны не трогать).
    const size_t equals_pos = target_line.content.rfind('=');
    if (equals_pos == std::string::npos)
    {
        error_out = "в строке не найден оператор присваивания '=': " + locationLabel(request.location);
        return false;
    }

    const size_t semicolon_pos = target_line.content.rfind(';');
    if (semicolon_pos == std::string::npos || semicolon_pos <= equals_pos)
    {
        error_out = "в строке не найден завершающий ';' после '=': " + locationLabel(request.location);
        return false;
    }

    const size_t right_hand_begin = equals_pos + 1;
    const std::string right_hand_side =
        target_line.content.substr(right_hand_begin, semicolon_pos - right_hand_begin);

    const size_t value_pos_local = right_hand_side.find(request.expected_value_text);
    if (value_pos_local == std::string::npos)
    {
        error_out = "ожидаемое значение \"" + request.expected_value_text +
            "\" не найдено в правой части строки: " + locationLabel(request.location);
        return false;
    }

    // Шаг 4: заменяем ровно первое найденное вхождение, всё остальное в
    // строке (отступы, "поле = ", ';', хвостовой комментарий) остаётся как
    // было.
    const size_t value_pos_absolute = right_hand_begin + value_pos_local;
    std::string new_content = target_line.content.substr(0, value_pos_absolute);
    new_content += request.new_value_text;
    new_content += target_line.content.substr(value_pos_absolute + request.expected_value_text.size());

    target_line.content = std::move(new_content);
    return true;
}

// Атомарная запись — тот же приём, что ProjectDocument::save (см.
// gui/project_document.cpp): временный файл В ТОЙ ЖЕ директории, что и
// целевой (rename атомарен только внутри одной файловой системы), затем
// переименование поверх исходного. path — UTF-8 std::string, как принято по
// всему проекту (см. include/utf8_path.h) — поэтому используем utf8Path,
// а не неявное преобразование std::string -> std::filesystem::path.
bool writeFileAtomically(const std::string& path, const std::string& content, std::string& error_out)
{
    const fs::path target = utf8Path(path);
    fs::path temporary = target;
    temporary += ".tmp";

    const FileWriteOutcome write_result = writeUtf8PathFile(temporary.u8string(), content);
    if (!write_result.success)
    {
        std::error_code ignored;
        fs::remove(temporary, ignored);
        error_out = "не удалось записать временный файл: " + temporary.u8string() + write_result.diagnostic;
        return false;
    }

    std::error_code rename_error;
    fs::rename(temporary, target, rename_error);
    if (rename_error)
    {
        std::error_code ignored;
        fs::remove(temporary, ignored);
        error_out = "не удалось заменить файл: " + target.u8string() + " (" + rename_error.message() + ")";
        return false;
    }

    return true;
}

} // namespace

PatchResult applyLineEdit(const LineEditRequest& request)
{
    PatchResult result;

    if (request.location.file.empty())
    {
        result.error = "пустой путь к файлу — поле не привязано к исходнику (see FieldProvenance::editable)";
        return result;
    }

    const FileReadOutcome read_result = readUtf8PathFile(request.location.file);
    if (!read_result.success)
    {
        result.error = "не удалось открыть файл: " + request.location.file + read_result.diagnostic;
        return result;
    }

    std::vector<FileLine> lines = splitIntoLines(read_result.content);

    std::string apply_error;
    if (!tryApplyEdit(lines, request, apply_error))
    {
        result.error = apply_error;
        return result;
    }

    std::string write_error;
    if (!writeFileAtomically(request.location.file, joinLines(lines), write_error))
    {
        result.error = write_error;
        return result;
    }

    result.success = true;
    return result;
}

std::vector<PatchResult> applyLineEdits(const std::vector<LineEditRequest>& requests)
{
    std::vector<PatchResult> results(requests.size());

    // Строки каждого файла грузятся в память не более одного раза: правки
    // для одного и того же файла могут быть разбросаны по вектору не подряд
    // (одно логическое поле бьётся на несколько физических строк, см.
    // reverse_patch_writer.h), поэтому кэш ключуется путём файла, а не
    // позицией в векторе.
    struct FileState
    {
        bool load_failed = false;
        std::string load_error;
        std::vector<FileLine> lines;
        bool dirty = false;   // хотя бы одна правка применилась в памяти
    };
    std::unordered_map<std::string, FileState> file_states;

    for (size_t i = 0; i < requests.size(); ++i)
    {
        const LineEditRequest& request = requests[i];

        if (request.location.file.empty())
        {
            results[i].error = "пустой путь к файлу — поле не привязано к исходнику (see FieldProvenance::editable)";
            continue;
        }

        auto it = file_states.find(request.location.file);
        if (it == file_states.end())
        {
            FileState state;
            const FileReadOutcome read_result = readUtf8PathFile(request.location.file);
            if (!read_result.success)
            {
                state.load_failed = true;
                state.load_error = "не удалось открыть файл: " + request.location.file + read_result.diagnostic;
            }
            else
            {
                state.lines = splitIntoLines(read_result.content);
            }
            it = file_states.emplace(request.location.file, std::move(state)).first;
        }

        FileState& state = it->second;
        if (state.load_failed)
        {
            results[i].error = state.load_error;
            continue;
        }

        // Якорь этой правки проверяется против ТЕКУЩЕГО содержимого строк в
        // памяти — то есть с учётом правок, уже применённых раньше в этом же
        // батче для этого же файла (см. .h про порядок применения).
        std::string apply_error;
        if (!tryApplyEdit(state.lines, request, apply_error))
        {
            results[i].error = apply_error;
            continue;
        }

        state.dirty = true;
        results[i].success = true;
    }

    // Запись — один раз на файл, только для файлов, где хотя бы одна правка
    // реально применилась в памяти. Файлы без единой успешной правки не
    // трогаются на диске вовсе (даже если были прочитаны).
    for (auto& entry : file_states)
    {
        FileState& state = entry.second;
        if (!state.dirty)
        {
            continue;
        }

        std::string write_error;
        if (writeFileAtomically(entry.first, joinLines(state.lines), write_error))
        {
            continue;
        }

        // Запись не удалась уже после того, как правки для этого файла были
        // помечены успешными в памяти — откатываем их success на false и
        // разносим причину каждой из них, иначе GUI показал бы "применено"
        // для правки, которая физически не попала на диск.
        for (size_t i = 0; i < requests.size(); ++i)
        {
            if (requests[i].location.file == entry.first && results[i].success)
            {
                results[i].success = false;
                results[i].error = write_error;
            }
        }
    }

    return results;
}

} // namespace reverse_import
