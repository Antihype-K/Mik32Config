#include "contribution_utils.h"
#include "utf8_path.h"
#include <filesystem>
#include <sstream>

std::string buildPinMaskExpression(const std::vector<uint8_t>& pins)
{
    std::ostringstream out;
    for (size_t i = 0; i < pins.size(); ++i)
    {
        if (i > 0)
        {
            out << " | ";
        }
        out << "GPIO_PIN_" << static_cast<int>(pins[i]);
    }
    return out.str();
}

std::string wrapInitFunctionWithUserCodeMarkers(const std::string& label, const std::string& function_text)
{
    const size_t open_brace = function_text.find("{\n");
    const size_t close_brace = function_text.rfind("}\n");
    if (open_brace == std::string::npos || close_brace == std::string::npos || close_brace <= open_brace)
    {
        // Неожиданная форма (нет "{\n"/"}\n") — отдаём текст как есть, не
        // угадываем структуру чужой функции.
        return function_text;
    }

    const size_t insert_after_open = open_brace + 2;
    std::string result = function_text.substr(0, insert_after_open);
    result += "    /* USER CODE BEGIN " + label + "_Init 0 */\n"
              "    /* USER CODE END " + label + "_Init 0 */\n\n";
    result += function_text.substr(insert_after_open, close_brace - insert_after_open);
    result += "\n    /* USER CODE BEGIN " + label + "_Init 2 */\n"
              "    /* USER CODE END " + label + "_Init 2 */\n";
    result += function_text.substr(close_brace);
    return result;
}

bool copyDirectoryTreeSkipExisting(const std::string& from_dir,
                                    const std::string& to_dir,
                                    std::error_code& fs_error)
{
    namespace fs = std::filesystem;

    // to_dir создаётся явно ДО обхода: recursive_directory_iterator создаёт
    // подкаталоги внутри to_dir только когда встречает соответствующие
    // директории внутри from_dir, но сам to_dir как корень назначения нигде
    // не создаётся — если в корне from_dir лежит хотя бы один файл (а не
    // только подкаталоги, как в hal/CHANGELOG.md/README.md/.gitignore),
    // copy_file падает с ENOENT, потому что to_dir ещё не существует. Найдено
    // ручной проверкой на реальном templates/mik32_sdk_snapshot — синтетическое
    // тестовое дерево в tests/test_parsers.cpp этот случай не покрывало.
    fs::create_directories(utf8Path(to_dir), fs_error);
    if (fs_error)
    {
        return false;
    }

    fs::recursive_directory_iterator it(utf8Path(from_dir), fs_error);
    if (fs_error)
    {
        return false;
    }

    for (const fs::recursive_directory_iterator end; it != end; it.increment(fs_error))
    {
        if (fs_error)
        {
            return false;
        }

        const fs::path relative = fs::relative(it->path(), utf8Path(from_dir), fs_error);
        if (fs_error)
        {
            return false;
        }
        const fs::path destination = utf8Path(to_dir) / relative;

        if (it->is_directory(fs_error))
        {
            fs::create_directories(destination, fs_error);
        }
        else if (!fs_error)
        {
            fs::copy_file(it->path(), destination, fs::copy_options::skip_existing, fs_error);
        }

        if (fs_error)
        {
            return false;
        }
    }

    return true;
}
