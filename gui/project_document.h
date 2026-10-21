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

#include <map>
#include <set>
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

    // Имя конкретного поля блока, если сообщение касается одного поля
    // (сейчас — только unresolved-поля reverse-import), иначе пусто.
    // Нужно, чтобы клик по сообщению подсвечивал не весь блок, а именно
    // проблемную строку в форме свойств.
    std::string field_name;

    // Нужен PropertiesPanel::setDiagnostics, чтобы не перестраивать форму
    // (refresh()) на каждый вызов, если список фактически не изменился —
    // refreshEverything() вызывается на каждое изменение документа.
    bool operator==(const Diagnostic& other) const
    {
        return severity == other.severity && message == other.message
            && block_index == other.block_index && field_name == other.field_name;
    }
};

class ProjectDocument
{
public:
    ProjectDocument();

    // --- Файл ---------------------------------------------------------------
    bool load(const std::string& path, std::string& error_out);

    // Заменяет документ уже готовым JSON без файла на диске — единственный
    // потребитель сейчас MainWindow::onImportProject (parser/reverse_import/
    // reverse_import.h: importProjectFromDirectory отдаёт JSON той же формы,
    // что и load() читает из файла, но собранный из чужих .c/.cpp, а не из
    // project.json). filePath() остаётся пустым — «Сохранить» для такого
    // документа обязан спросить путь (та же ветка, что у нового проекта, см.
    // MainWindow::onSave), а не тихо перезаписать что-то несуществующее.
    void loadFromDocument(const nlohmann::json& document);

    // Запись атомарная: сначала во временный файл рядом с целевым, затем
    // переименование поверх. ТЗ §3.2 п.1: «при ошибке записи исходный файл
    // остаётся неизменным» — при простом открытии на запись усечённый файл
    // уже уничтожил бы предыдущую версию.
    bool save(const std::string& path, std::string& error_out);

    // Пишет копию документа с проставленным "output_path" = output_dir в
    // "<output_dir>/project.json" — вызывается после генерации проекта
    // (ADR 0001), чтобы конфиг жил рядом со сгенерированным кодом и его
    // можно было открыть позже даже без знания, где исходный файл
    // настроек. В отличие от save() не трогает filePath()/isModified() —
    // это побочная копия, а не смена "текущего редактируемого файла".
    bool saveColocatedConfig(const std::string& output_dir, std::string& error_out) const;

    // Путь из поля верхнего уровня "output_path" (ADR 0001), либо пустая
    // строка, если поля нет (старый файл конфига или новый проект).
    std::string outputPath() const;

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

    // Убирает ключ из блока целиком (не присваивает null — у парсеров
    // contains(field) истинно и для null-значения, так что setField(...,
    // null) не эквивалентен отсутствию поля). Нужен для полей, которые
    // взаимоисключающи с другими (например, timer16 "frequency_hz"/
    // "duty_percent" против "prescaler"/"arr"/"cmp" — см.
    // OVERNIGHT_FIELD_REFERENCE.md) — переключение режима ввода в GUI обязано
    // реально убрать поля прежнего режима, а не оставить их лежать со
    // старыми значениями.
    void eraseField(int block_index, const std::string& field);

    // Поля ВЕРХНЕГО уровня документа (соседи "mcu"/"version"/"peripherals"),
    // а не внутри блока периферии — например "toolchain", "hal_source_mode"
    // (CMake-тулчейн, codegen/project/project_config.h::ProjectConfig).
    // В отличие от field()/setField() индекс блока не нужен: эти настройки
    // относятся к проекту в целом. Отсутствие ключа — штатный случай (старый
    // файл проекта, созданный до появления этих полей), а не ошибка: как и
    // field() выше, при отсутствии ключа возвращается null, а не исключение.
    nlohmann::json projectSetting(const std::string& key) const;
    void setProjectSetting(const std::string& key, const nlohmann::json& value);

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

    // --- Unresolved-поля reverse-import ---------------------------------
    // См. feedback_reverse_import_unresolved_fields_policy в памяти
    // ассистента: поля, которые reverse-import не смог разобрать, не
    // получают угаданный дефолт — вызывающий (MainWindow::onImportProject)
    // передаёт сюда их имена и исходный текст (detail — что именно не
    // разобралось, для отображения в форме) по индексу блока сразу после
    // loadFromDocument. Индексация соответствует document()["peripherals"]
    // НА МОМЕНТ импорта — как и block_provenance в
    // reverse_import::ImportedProject, этот список не переживает
    // удаление/переупорядочивание блоков.
    struct UnresolvedField
    {
        std::string field_name;
        std::string detail;   // исходный текст из .c-файла, если известен
    };

    void setUnresolvedFields(int block_index, std::vector<UnresolvedField> fields);
    void clearUnresolvedFields();
    std::vector<std::string> unresolvedFieldNames(int block_index) const;
    std::string unresolvedFieldDetail(int block_index, const std::string& field_name) const;
    bool blockHasUnresolvedFields(int block_index) const;
    bool hasAnyUnresolvedFields() const;

    // true <=> поле когда-то было unresolved и пользователь сам задал ему
    // значение через setField (щёлкнул по параметру и выбрал корректное) —
    // ошибка для этого поля уже снята, но форма помечает поле жёлтым, чтобы
    // отличить ручной выбор от значений, которые импорт распознал сам.
    bool wasManuallyResolved(int block_index, const std::string& field_name) const;

    // Пользователь вручную отметил, что проверил блок несмотря на
    // оставшиеся unresolved-поля ("плюсик" в форме свойств) — снимает
    // блокировку кодогенерации и красную подсветку для этого блока, не
    // трогая сами значения полей. Сбрасывается автоматически, если список
    // unresolved-полей блока меняется (новый импорт того же блока).
    void setBlockConfirmed(int block_index, bool confirmed);
    bool isBlockConfirmed(int block_index) const;

    // true <=> блок всё ещё блокирует кодогенерацию: есть unresolved-поля
    // И пользователь не подтвердил блок вручную. Дерево периферии и
    // ProjectDocument::validate() должны проверять именно это, а не
    // blockHasUnresolvedFields() напрямую.
    bool blockBlocksGeneration(int block_index) const;

private:
    nlohmann::json document_;
    std::string file_path_;
    bool modified_ = false;
    std::map<int, std::vector<UnresolvedField>> unresolved_fields_;
    std::set<int> confirmed_blocks_;
    std::map<int, std::set<std::string>> manually_resolved_fields_;
};

#endif //MIK32_CONFIGURATOR_PROJECT_DOCUMENT_H
