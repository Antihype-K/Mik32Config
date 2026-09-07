//
// Редактируемая модель проекта — то, чем GUI владеет между открытием файла и
// генерацией кода.
//
// Концепция и границы. Ядро конфигуратора (mik32-core) работает с ФАЙЛОМ:
// parseProjectConfig открывает путь, разбирает JSON и отдаёт типизированный
// ProjectConfig. Для редактора такой контракт не годится: пользователь меняет
// поле — конфигурацию надо перепроверить немедленно (ТЗ §3.1.9 п.5, панель
// диагностики в реальном времени), а записывать на диск промежуточные
// состояния нельзя (ТЗ §3.2 п.1, файл проекта не должен оказываться в
// несогласованном виде). Поэтому модель хранит nlohmann::json в памяти и
// вызывает документные точки входа ядра — parseProjectDocument и
// validatePinConflictsInDocument.
//
// Почему модель — сырой JSON, а не ProjectConfig. ProjectConfig существует
// только для ВАЛИДНОЙ конфигурации: parseProjectDocument либо вернёт его
// целиком, либо не вернёт ничего. Редактор же обязан жить в невалидном
// состоянии — пользователь стирает значение в поле, чтобы ввести новое, и в
// этот момент конфигурация невалидна, но её нельзя терять. Сырой JSON
// переживает такие состояния, а ProjectConfig получается из него на каждой
// проверке как производная величина.
//
// Класс намеренно не наследует QObject и не знает про виджеты: его можно
// собрать и проверить без GUI.
//
#ifndef MIK32_CONFIGURATOR_PROJECT_DOCUMENT_H
#define MIK32_CONFIGURATOR_PROJECT_DOCUMENT_H

#include <string>
#include <vector>
#include "json.hpp"
#include "project_codegen.h"

// ТЗ §3.1.9 п.5: панель диагностических сообщений с фильтрацией по уровню
// (Ошибки / Предупреждения / Информация) и переходом к источнику по клику.
// До появления GUI диагностика ядра была плоской строкой в ParseResult::error —
// ни уровня, ни адреса источника. Этот тип добавляет и то, и другое, не меняя
// контракт ядра: уровень и адрес проставляет модель, разбирая результаты
// ядровых проверок.
enum class DiagnosticSeverity
{
    Error,        // блокирует кодогенерацию (ТЗ §3.1.6.3)
    Warning,      // не блокирует, но требует внимания
    Info          // справка (например, погрешность Baud Rate)
};

struct Diagnostic
{
    DiagnosticSeverity severity;
    std::string message;

    // Индекс блока в массиве "peripherals", к которому относится сообщение,
    // либо -1, если оно про конфигурацию в целом. Это и есть «переход к
    // источнику по клику» из ТЗ §3.1.9 п.5: панель диагностики отдаёт индекс,
    // дерево выделяет соответствующий блок.
    int block_index = -1;
};

class ProjectDocument
{
public:
    ProjectDocument();

    // --- Файл ---------------------------------------------------------------
    bool load(const std::string& path, std::string& error_out);

    // Запись атомарная: сначала во временный файл рядом с целевым, затем
    // переименование поверх. ТЗ §3.2 п.1: «при ошибке записи исходный файл
    // остаётся неизменным» — при простом открытии на запись усечённый файл
    // уже уничтожил бы предыдущую версию.
    bool save(const std::string& path, std::string& error_out);

    const std::string& filePath() const { return file_path_; }
    bool isModified() const { return modified_; }

    // --- Содержимое ---------------------------------------------------------
    const nlohmann::json& document() const { return document_; }

    // Индексы блоков "peripherals" данного типа и instance. Для gpio их может
    // быть несколько (разным пинам одного порта — разные режимы, см.
    // codegen/project/gpio_contribution.h); для остальных типов — не больше
    // одного, это обеспечивает parseProjectDocument.
    std::vector<int> blockIndices(const std::string& type, int instance) const;

    bool isEnabled(const std::string& type, int instance) const;

    // Добавляет блок со значениями по умолчанию и возвращает его индекс.
    int addBlock(const std::string& type, int instance);

    // Удаляет все блоки этого типа и instance («Отключить» в дереве).
    void removeBlocks(const std::string& type, int instance);

    void removeBlockAt(int block_index);

    // Правка одного поля блока. Значение кладётся как есть — типизацию
    // обеспечивает вызывающий (панель свойств знает, что baud_rate целое,
    // а parity строка).
    void setField(int block_index, const std::string& field, const nlohmann::json& value);

    nlohmann::json field(int block_index, const std::string& field) const;

    // --- Проверка -----------------------------------------------------------
    // Полный прогон обеих ядровых проверок: schema-валидация каждого блока
    // (parseProjectDocument) и кросс-модульные конфликты выводов
    // (validatePinConflictsInDocument). Порядок тот же, что в консольном
    // пайплайне — ТЗ §3.1.6 требует per-module раньше кросс-модульной.
    std::vector<Diagnostic> validate(const ProjectKnowledge& knowledge) const;

    // Выводы, занятые блоком с этим индексом: пара (gpio_instance, pin).
    // Для gpio берётся поле "pins", для остальных типов — фиксированные
    // выводы из базы знаний. Нужен схеме корпуса для подсветки.
    std::vector<std::pair<int, int>> claimedPins(int block_index,
                                                 const ProjectKnowledge& knowledge) const;

private:
    nlohmann::json document_;
    std::string file_path_;
    bool modified_ = false;
};

#endif //MIK32_CONFIGURATOR_PROJECT_DOCUMENT_H
