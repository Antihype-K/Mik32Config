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

std::string buildPinListComment(const std::vector<uint8_t>& pins)
{
    std::ostringstream out;
    for (size_t i = 0; i < pins.size(); ++i)
    {
        if (i > 0)
        {
            out << ", ";
        }
        out << static_cast<int>(pins[i]);
    }
    return out.str();
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
