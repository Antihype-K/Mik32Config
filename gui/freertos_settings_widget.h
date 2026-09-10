//
// Редактор блока "freertos" верхнего уровня документа (parser/freertos/
// freertos_parser.h) — отдельные файлы, а не код прямо в
// project_manager_screen.cpp: блок настолько же велик, как весь остальной
// экран Project Manager (enable + source_mode + 7 полей os_config + пять
// списков объектов ОС), поэтому вынесен целиком по тому же принципу, что и
// остальные экраны (PropertiesPanel, DiagnosticsPanel) — самодостаточный
// QWidget, который отдаёт готовый nlohmann::json через сигнал changed()
// и умеет проинициализироваться из nlohmann::json через setValue().
//
// Виджет НЕ пишет в ProjectDocument сам (в отличие от отдельных полей
// ProjectManagerScreen) — так проще держать всю сборку/разбор JSON-объекта
// "freertos" в одном месте (см. value()/setValue()), а ProjectManagerScreen
// остаётся тонким и просто перекладывает готовый JSON в
// document_->setProjectSetting("freertos", ...), как ему уже предписывает
// её собственный .h (документ пишется только оттуда, ЧТЕНИЕ — отдельным
// путём).
//
#ifndef MIK32_CONFIGURATOR_FREERTOS_SETTINGS_WIDGET_H
#define MIK32_CONFIGURATOR_FREERTOS_SETTINGS_WIDGET_H

#include <QWidget>
#include <QVector>
#include <QStringList>
#include "json.hpp"

class QCheckBox;
class QButtonGroup;
class QRadioButton;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QComboBox;
class QTableWidget;
class QTabWidget;

// Одна из пяти таблиц (tasks/queues/semaphores/mutexes/timers). Параметризован
// списком колонок вместо пяти похожих классов — задача одна и та же:
// показать строки, дать добавить/удалить, отдать текущее содержимое как
// список строк. "kind"-специфичные колонки (max_count/initial_count у
// counting-семафоров, period_ms у таймеров) не прячутся по значению kind —
// они просто всегда видимы, что явно допущено заданием ради простоты.
class FreeRtosTableEditor : public QWidget
{
    Q_OBJECT

public:
    struct Column
    {
        QString header;
        bool is_combo = false;
        QStringList combo_options; // только при is_combo == true
        QString default_value;     // текст новой строки для этой колонки
    };

    FreeRtosTableEditor(const QVector<Column>& columns, QWidget* parent = nullptr);

    // Не эмитит changed() — вызывается при программном заполнении из
    // документа, тем же приёмом syncing_, что и в остальном экране.
    void setRows(const QVector<QStringList>& rows);
    QVector<QStringList> rows() const;

signals:
    void changed();

private:
    void addRow(const QStringList& initial_values);

    QVector<Column> columns_;
    QTableWidget* table_ = nullptr;
    QPushButton* add_button_ = nullptr;
    QPushButton* remove_button_ = nullptr;
    bool syncing_ = false;
};

class FreeRtosSettingsWidget : public QWidget
{
    Q_OBJECT

public:
    explicit FreeRtosSettingsWidget(QWidget* parent = nullptr);

    // Заполняет виджет из узла document["freertos"] (может быть JSON null,
    // если ключа в документе ещё нет вовсе — тогда подставляются дефолты из
    // parser/freertos/freertos_parser.h). Не эмитит changed().
    void setValue(const nlohmann::json& freertos_settings);

    // Собирает текущее состояние виджета обратно в тот же JSON-объект.
    nlohmann::json value() const;

signals:
    // Пользователь поменял что-то в виджете (а не программное setValue) —
    // ProjectManagerScreen обязан записать value() в документ.
    void changed();

private:
    void updateEnabledDependentState();
    void updateLocalPathEnabled();
    void emitChangedIfNotSyncing();

    bool syncing_ = false;

    QCheckBox* enabled_checkbox_ = nullptr;
    QWidget* body_ = nullptr;

    QButtonGroup* source_mode_group_ = nullptr;
    QRadioButton* source_bundled_radio_ = nullptr;
    QRadioButton* source_network_radio_ = nullptr;
    QRadioButton* source_local_path_radio_ = nullptr;
    QLineEdit* source_local_path_edit_ = nullptr;
    QPushButton* source_local_path_browse_ = nullptr;

    QSpinBox* tick_rate_hz_ = nullptr;
    QSpinBox* max_priorities_ = nullptr;
    QSpinBox* minimal_stack_size_words_ = nullptr;
    QSpinBox* total_heap_size_bytes_ = nullptr;
    QCheckBox* use_preemption_ = nullptr;
    QComboBox* stack_overflow_check_ = nullptr;
    QComboBox* heap_scheme_ = nullptr;

    QTabWidget* lists_tabs_ = nullptr;
    FreeRtosTableEditor* tasks_editor_ = nullptr;
    FreeRtosTableEditor* queues_editor_ = nullptr;
    FreeRtosTableEditor* semaphores_editor_ = nullptr;
    FreeRtosTableEditor* mutexes_editor_ = nullptr;
    FreeRtosTableEditor* timers_editor_ = nullptr;
};

#endif //MIK32_CONFIGURATOR_FREERTOS_SETTINGS_WIDGET_H
