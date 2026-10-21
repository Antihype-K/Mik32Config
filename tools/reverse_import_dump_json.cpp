//
// Разовый инструмент: reverse-import каталога -> чистый JSON-конфиг на
// stdout. Нужен, чтобы прогнать docs/examples/* через полный пайплайн
// generateProjectFirmware без GUI (см. docs/implementations/ заметку о
// регрессионном прогоне reverse-import на вендорных примерах).
//
// Использование: reverse-import-dump-json <каталог-примера>
//
#include <iostream>
#include "reverse_import.h"

int main(int argc, char** argv)
{
    if (argc != 2)
    {
        std::cerr << "использование: " << argv[0] << " <каталог-примера>\n";
        return 2;
    }

    reverse_import::ImportedProject imported = reverse_import::importProjectFromDirectory(argv[1]);
    std::cout << imported.document.dump(2) << "\n";

    std::cerr << "--- notes (" << imported.notes.size() << ") ---\n";
    for (const auto& note : imported.notes)
        std::cerr << note << "\n";

    std::cerr << "--- unresolved fields ---\n";
    for (std::size_t block_index = 0; block_index < imported.block_provenance.size(); ++block_index)
    {
        for (const auto& field : imported.block_provenance[block_index])
        {
            if (field.unresolved)
                std::cerr << "block[" << block_index << "]." << field.json_field << "\n";
        }
    }
    return 0;
}
