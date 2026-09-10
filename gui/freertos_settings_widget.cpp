#include "freertos_settings_widget.h"

#include <algorithm>

#include <QCheckBox>
#include <QButtonGroup>
#include <QRadioButton>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QComboBox>
#include <QTableWidget>
#include <QTabWidget>
#include <QHeaderView>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QFileDialog>

// --- FreeRtosTableEditor -----------------------------------------------------

FreeRtosTableEditor::FreeRtosTableEditor(const QVector<Column>& columns, QWidget* parent)
    : QWidget(parent), columns_(columns)
{
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    table_ = new QTableWidget(this);
    table_->setColumnCount(columns_.size());
    QStringList headers;
    for (const Column& column : columns_)
    {
        headers << column.header;
    }
    table_->setHorizontalHeaderLabels(headers);
    table_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    table_->verticalHeader()->setVisible(false);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    layout->addWidget(table_);

    QHBoxLayout* buttons = new QHBoxLayout();
    add_button_ = new QPushButton(QString::fromUtf8("Добавить"));
    remove_button_ = new QPushButton(QString::fromUtf8("Удалить выбранное"));
    buttons->addWidget(add_button_);
    buttons->addWidget(remove_button_);
    buttons->addStretch(1);
    layout->addLayout(buttons);

    // Правки существующих текстовых ячеек (не сама вставка строки — та
    // делается через addRow() и эмитит changed() из обработчика кнопки) —
    // syncing_ гасит эмиссию во время программного setRows().
    connect(table_, &QTableWidget::itemChanged, this, [this](QTableWidgetItem*)
    {
        if (!syncing_)
        {
            emit changed();
        }
    });

    connect(add_button_, &QPushButton::clicked, this, [this]()
    {
        QStringList defaults;
        for (const Column& column : columns_)
        {
            defaults << column.default_value;
        }
        addRow(defaults);
        emit changed();
    });

    connect(remove_button_, &QPushButton::clicked, this, [this]()
    {
        QList<int> selected_rows;
        for (const QModelIndex& index : table_->selectionModel()->selectedRows())
        {
            selected_rows << index.row();
        }
        std::sort(selected_rows.rbegin(), selected_rows.rend());
        for (int row : selected_rows)
        {
            table_->removeRow(row);
        }
        if (!selected_rows.isEmpty())
        {
            emit changed();
        }
    });
}

void FreeRtosTableEditor::addRow(const QStringList& initial_values)
{
    const int row = table_->rowCount();
    table_->insertRow(row);
    for (int column = 0; column < columns_.size(); ++column)
    {
        const QString value = column < initial_values.size()
            ? initial_values[column] : columns_[column].default_value;
        if (columns_[column].is_combo)
        {
            QComboBox* combo = new QComboBox();
            combo->addItems(columns_[column].combo_options);
            const int index = combo->findText(value);
            combo->setCurrentIndex(index >= 0 ? index : 0);
            connect(combo, &QComboBox::currentIndexChanged, this, [this](int)
            {
                if (!syncing_)
                {
                    emit changed();
                }
            });
            table_->setCellWidget(row, column, combo);
        }
        else
        {
            table_->setItem(row, column, new QTableWidgetItem(value));
        }
    }
}

void FreeRtosTableEditor::setRows(const QVector<QStringList>& rows)
{
    syncing_ = true;
    table_->setRowCount(0);
    for (const QStringList& row : rows)
    {
        addRow(row);
    }
    syncing_ = false;
}

QVector<QStringList> FreeRtosTableEditor::rows() const
{
    QVector<QStringList> result;
    for (int row = 0; row < table_->rowCount(); ++row)
    {
        QStringList values;
        for (int column = 0; column < columns_.size(); ++column)
        {
            if (columns_[column].is_combo)
            {
                auto* combo = qobject_cast<QComboBox*>(table_->cellWidget(row, column));
                values << (combo != nullptr ? combo->currentText() : QString());
            }
            else
            {
                QTableWidgetItem* item = table_->item(row, column);
                values << (item != nullptr ? item->text() : QString());
            }
        }
        result << values;
    }
    return result;
}

// --- FreeRtosSettingsWidget --------------------------------------------------

FreeRtosSettingsWidget::FreeRtosSettingsWidget(QWidget* parent)
    : QWidget(parent)
{
    QVBoxLayout* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);

    enabled_checkbox_ = new QCheckBox(QString::fromUtf8("Включить FreeRTOS"));
    root->addWidget(enabled_checkbox_);

    body_ = new QWidget();
    QVBoxLayout* body_layout = new QVBoxLayout(body_);
    body_layout->setContentsMargins(0, 12, 0, 0);
    root->addWidget(body_);

    // --- Источник ядра — то же трёхрежимное bundled/network/local_path,
    // что уже есть у HAL в CMake-тулчейне (project_manager_screen.cpp).
    QWidget* source_section = new QWidget();
    QVBoxLayout* source_layout = new QVBoxLayout(source_section);
    source_layout->setContentsMargins(0, 0, 0, 0);

    source_mode_group_ = new QButtonGroup(this);
    source_bundled_radio_ = new QRadioButton(QString::fromUtf8("Встроенный (Bundled)"));
    source_network_radio_ = new QRadioButton(QString::fromUtf8("Из сети (Network)"));
    source_local_path_radio_ = new QRadioButton(QString::fromUtf8("Локальный путь (Local path)"));
    source_bundled_radio_->setChecked(true);
    source_mode_group_->addButton(source_bundled_radio_);
    source_mode_group_->addButton(source_network_radio_);
    source_mode_group_->addButton(source_local_path_radio_);
    source_layout->addWidget(source_bundled_radio_);
    source_layout->addWidget(source_network_radio_);
    source_layout->addWidget(source_local_path_radio_);

    source_local_path_edit_ = new QLineEdit();
    source_local_path_edit_->setReadOnly(true);
    source_local_path_browse_ = new QPushButton(QString::fromUtf8("Обзор..."));
    QHBoxLayout* source_local_path_row = new QHBoxLayout();
    source_local_path_row->addWidget(source_local_path_edit_);
    source_local_path_row->addWidget(source_local_path_browse_);
    source_layout->addLayout(source_local_path_row);

    QFormLayout* body_form = new QFormLayout();
    body_form->addRow(QString::fromUtf8("Источник ядра FreeRTOS"), source_section);

    // --- os_config -----------------------------------------------------
    tick_rate_hz_ = new QSpinBox();
    tick_rate_hz_->setRange(1, 100000);
    tick_rate_hz_->setValue(100);
    body_form->addRow(QString::fromUtf8("Частота тика (Гц)"), tick_rate_hz_);

    max_priorities_ = new QSpinBox();
    max_priorities_->setRange(2, 64);
    max_priorities_->setValue(7);
    body_form->addRow(QString::fromUtf8("Число приоритетов"), max_priorities_);

    minimal_stack_size_words_ = new QSpinBox();
    minimal_stack_size_words_->setRange(32, 65536);
    minimal_stack_size_words_->setValue(256);
    body_form->addRow(QString::fromUtf8("Мин. размер стека (слов)"), minimal_stack_size_words_);

    total_heap_size_bytes_ = new QSpinBox();
    total_heap_size_bytes_->setRange(0, 8 * 1024 * 1024);
    total_heap_size_bytes_->setValue(10 * 1024);
    body_form->addRow(QString::fromUtf8("Размер кучи (байт)"), total_heap_size_bytes_);

    use_preemption_ = new QCheckBox(QString::fromUtf8("Вытесняющая многозадачность (preemption)"));
    use_preemption_->setChecked(true);
    body_form->addRow(QString(), use_preemption_);

    stack_overflow_check_ = new QComboBox();
    stack_overflow_check_->addItem(QString::fromUtf8("0 — выключено"), 0);
    stack_overflow_check_->addItem(QString::fromUtf8("1 — метод 1 (быстрый)"), 1);
    stack_overflow_check_->addItem(QString::fromUtf8("2 — метод 2 (штамп + заполнение)"), 2);
    body_form->addRow(QString::fromUtf8("Проверка переполнения стека"), stack_overflow_check_);

    heap_scheme_ = new QComboBox();
    heap_scheme_->addItems({"heap_1", "heap_2", "heap_3", "heap_4", "heap_5"});
    heap_scheme_->setCurrentText("heap_4");
    body_form->addRow(QString::fromUtf8("Схема управления кучей"), heap_scheme_);

    body_layout->addLayout(body_form);

    // --- Пять списков объектов ОС ---------------------------------------
    lists_tabs_ = new QTabWidget();

    const QVector<FreeRtosTableEditor::Column> task_columns = {
        {QString::fromUtf8("Имя"), false, {}, QString()},
        {QString::fromUtf8("Приоритет"), false, {}, QString("1")},
        {QString::fromUtf8("Размер стека (слов)"), false, {}, QString("128")},
    };
    tasks_editor_ = new FreeRtosTableEditor(task_columns);
    lists_tabs_->addTab(tasks_editor_, QString::fromUtf8("Задачи"));

    const QVector<FreeRtosTableEditor::Column> queue_columns = {
        {QString::fromUtf8("Имя"), false, {}, QString()},
        {QString::fromUtf8("Длина"), false, {}, QString("4")},
        {QString::fromUtf8("Размер элемента (байт)"), false, {}, QString("4")},
    };
    queues_editor_ = new FreeRtosTableEditor(queue_columns);
    lists_tabs_->addTab(queues_editor_, QString::fromUtf8("Очереди"));

    const QVector<FreeRtosTableEditor::Column> semaphore_columns = {
        {QString::fromUtf8("Имя"), false, {}, QString()},
        {QString::fromUtf8("Вид"), true, {"binary", "counting"}, QString("binary")},
        {QString::fromUtf8("Макс. счётчик (counting)"), false, {}, QString("1")},
        {QString::fromUtf8("Начальный счётчик (counting)"), false, {}, QString("0")},
    };
    semaphores_editor_ = new FreeRtosTableEditor(semaphore_columns);
    lists_tabs_->addTab(semaphores_editor_, QString::fromUtf8("Семафоры"));

    const QVector<FreeRtosTableEditor::Column> mutex_columns = {
        {QString::fromUtf8("Имя"), false, {}, QString()},
        {QString::fromUtf8("Вид"), true, {"normal", "recursive"}, QString("normal")},
    };
    mutexes_editor_ = new FreeRtosTableEditor(mutex_columns);
    lists_tabs_->addTab(mutexes_editor_, QString::fromUtf8("Мьютексы"));

    const QVector<FreeRtosTableEditor::Column> timer_columns = {
        {QString::fromUtf8("Имя"), false, {}, QString()},
        {QString::fromUtf8("Вид"), true, {"one_shot", "periodic"}, QString("periodic")},
        {QString::fromUtf8("Период (мс)"), false, {}, QString("1000")},
    };
    timers_editor_ = new FreeRtosTableEditor(timer_columns);
    lists_tabs_->addTab(timers_editor_, QString::fromUtf8("Программные таймеры"));

    body_layout->addWidget(lists_tabs_);

    updateEnabledDependentState();
    updateLocalPathEnabled();

    // --- Проводка: любое изменение виджета -> emit changed() ------------
    connect(enabled_checkbox_, &QCheckBox::toggled, this, [this](bool)
    {
        updateEnabledDependentState();
        emitChangedIfNotSyncing();
    });

    auto writeSourceMode = [this](bool checked)
    {
        if (!checked)
        {
            return;
        }
        updateLocalPathEnabled();
        emitChangedIfNotSyncing();
    };
    connect(source_bundled_radio_, &QRadioButton::toggled, this, writeSourceMode);
    connect(source_network_radio_, &QRadioButton::toggled, this, writeSourceMode);
    connect(source_local_path_radio_, &QRadioButton::toggled, this, writeSourceMode);

    connect(source_local_path_browse_, &QPushButton::clicked, this, [this]()
    {
        const QString chosen = QFileDialog::getExistingDirectory(
            this, QString::fromUtf8("Локальный путь к FreeRTOS-Kernel"), source_local_path_edit_->text());
        if (chosen.isEmpty())
        {
            return;
        }
        source_local_path_edit_->setText(chosen);
        emitChangedIfNotSyncing();
    });

    connect(tick_rate_hz_, &QSpinBox::valueChanged, this, [this](int) { emitChangedIfNotSyncing(); });
    connect(max_priorities_, &QSpinBox::valueChanged, this, [this](int) { emitChangedIfNotSyncing(); });
    connect(minimal_stack_size_words_, &QSpinBox::valueChanged, this, [this](int) { emitChangedIfNotSyncing(); });
    connect(total_heap_size_bytes_, &QSpinBox::valueChanged, this, [this](int) { emitChangedIfNotSyncing(); });
    connect(use_preemption_, &QCheckBox::toggled, this, [this](bool) { emitChangedIfNotSyncing(); });
    connect(stack_overflow_check_, &QComboBox::currentIndexChanged, this, [this](int) { emitChangedIfNotSyncing(); });
    connect(heap_scheme_, &QComboBox::currentIndexChanged, this, [this](int) { emitChangedIfNotSyncing(); });

    connect(tasks_editor_, &FreeRtosTableEditor::changed, this, [this]() { emitChangedIfNotSyncing(); });
    connect(queues_editor_, &FreeRtosTableEditor::changed, this, [this]() { emitChangedIfNotSyncing(); });
    connect(semaphores_editor_, &FreeRtosTableEditor::changed, this, [this]() { emitChangedIfNotSyncing(); });
    connect(mutexes_editor_, &FreeRtosTableEditor::changed, this, [this]() { emitChangedIfNotSyncing(); });
    connect(timers_editor_, &FreeRtosTableEditor::changed, this, [this]() { emitChangedIfNotSyncing(); });
}

void FreeRtosSettingsWidget::updateEnabledDependentState()
{
    body_->setEnabled(enabled_checkbox_->isChecked());
}

void FreeRtosSettingsWidget::updateLocalPathEnabled()
{
    const bool local_path_selected = source_local_path_radio_->isChecked();
    source_local_path_edit_->setEnabled(local_path_selected);
    source_local_path_browse_->setEnabled(local_path_selected);
}

void FreeRtosSettingsWidget::emitChangedIfNotSyncing()
{
    if (!syncing_)
    {
        emit changed();
    }
}

void FreeRtosSettingsWidget::setValue(const nlohmann::json& freertos_settings)
{
    syncing_ = true;

    const nlohmann::json settings = freertos_settings.is_object()
        ? freertos_settings : nlohmann::json::object();

    enabled_checkbox_->setChecked(settings.value("enabled", false));

    const std::string source_mode = settings.value("source_mode", std::string("bundled"));
    if (source_mode == "network")
    {
        source_network_radio_->setChecked(true);
    }
    else if (source_mode == "local_path")
    {
        source_local_path_radio_->setChecked(true);
    }
    else
    {
        source_bundled_radio_->setChecked(true);
    }
    source_local_path_edit_->setText(
        QString::fromStdString(settings.value("source_local_path", std::string())));

    const nlohmann::json os = settings.value("os_config", nlohmann::json::object());
    tick_rate_hz_->setValue(os.value("tick_rate_hz", 100));
    max_priorities_->setValue(os.value("max_priorities", 7));
    minimal_stack_size_words_->setValue(os.value("minimal_stack_size_words", 256));
    total_heap_size_bytes_->setValue(os.value("total_heap_size_bytes", 10 * 1024));
    use_preemption_->setChecked(os.value("use_preemption", true));

    const int stack_overflow_check = os.value("stack_overflow_check", 0);
    const int stack_overflow_index = stack_overflow_check_->findData(stack_overflow_check);
    stack_overflow_check_->setCurrentIndex(stack_overflow_index >= 0 ? stack_overflow_index : 0);

    const QString heap_scheme = QString::fromStdString(os.value("heap_scheme", std::string("heap_4")));
    const int heap_scheme_index = heap_scheme_->findText(heap_scheme);
    heap_scheme_->setCurrentIndex(heap_scheme_index >= 0 ? heap_scheme_index : 3);

    QVector<QStringList> task_rows;
    for (const auto& task : settings.value("tasks", nlohmann::json::array()))
    {
        task_rows << QStringList{
            QString::fromStdString(task.value("name", std::string())),
            QString::number(task.value("priority", 1)),
            QString::number(task.value("stack_size_words", 128)),
        };
    }
    tasks_editor_->setRows(task_rows);

    QVector<QStringList> queue_rows;
    for (const auto& queue : settings.value("queues", nlohmann::json::array()))
    {
        queue_rows << QStringList{
            QString::fromStdString(queue.value("name", std::string())),
            QString::number(queue.value("length", 1)),
            QString::number(queue.value("item_size_bytes", 4)),
        };
    }
    queues_editor_->setRows(queue_rows);

    QVector<QStringList> semaphore_rows;
    for (const auto& semaphore : settings.value("semaphores", nlohmann::json::array()))
    {
        semaphore_rows << QStringList{
            QString::fromStdString(semaphore.value("name", std::string())),
            QString::fromStdString(semaphore.value("kind", std::string("binary"))),
            QString::number(semaphore.value("max_count", 1)),
            QString::number(semaphore.value("initial_count", 0)),
        };
    }
    semaphores_editor_->setRows(semaphore_rows);

    QVector<QStringList> mutex_rows;
    for (const auto& mutex : settings.value("mutexes", nlohmann::json::array()))
    {
        mutex_rows << QStringList{
            QString::fromStdString(mutex.value("name", std::string())),
            QString::fromStdString(mutex.value("kind", std::string("normal"))),
        };
    }
    mutexes_editor_->setRows(mutex_rows);

    QVector<QStringList> timer_rows;
    for (const auto& timer : settings.value("timers", nlohmann::json::array()))
    {
        timer_rows << QStringList{
            QString::fromStdString(timer.value("name", std::string())),
            QString::fromStdString(timer.value("kind", std::string("periodic"))),
            QString::number(timer.value("period_ms", 1000)),
        };
    }
    timers_editor_->setRows(timer_rows);

    syncing_ = false;

    updateEnabledDependentState();
    updateLocalPathEnabled();
}

nlohmann::json FreeRtosSettingsWidget::value() const
{
    nlohmann::json settings = nlohmann::json::object();
    settings["enabled"] = enabled_checkbox_->isChecked();
    settings["source_mode"] = source_network_radio_->isChecked()
        ? "network"
        : (source_local_path_radio_->isChecked() ? "local_path" : "bundled");
    settings["source_local_path"] = source_local_path_edit_->text().toStdString();

    nlohmann::json os = nlohmann::json::object();
    os["tick_rate_hz"] = tick_rate_hz_->value();
    os["max_priorities"] = max_priorities_->value();
    os["minimal_stack_size_words"] = minimal_stack_size_words_->value();
    os["total_heap_size_bytes"] = total_heap_size_bytes_->value();
    os["use_preemption"] = use_preemption_->isChecked();
    os["stack_overflow_check"] = stack_overflow_check_->currentData().toInt();
    os["heap_scheme"] = heap_scheme_->currentText().toStdString();
    settings["os_config"] = os;

    nlohmann::json tasks = nlohmann::json::array();
    for (const QStringList& row : tasks_editor_->rows())
    {
        nlohmann::json task = nlohmann::json::object();
        task["name"] = row.value(0, QString()).toStdString();
        task["priority"] = row.value(1, QString("1")).toInt();
        task["stack_size_words"] = row.value(2, QString("128")).toInt();
        tasks.push_back(task);
    }
    settings["tasks"] = tasks;

    nlohmann::json queues = nlohmann::json::array();
    for (const QStringList& row : queues_editor_->rows())
    {
        nlohmann::json queue = nlohmann::json::object();
        queue["name"] = row.value(0, QString()).toStdString();
        queue["length"] = row.value(1, QString("4")).toInt();
        queue["item_size_bytes"] = row.value(2, QString("4")).toInt();
        queues.push_back(queue);
    }
    settings["queues"] = queues;

    nlohmann::json semaphores = nlohmann::json::array();
    for (const QStringList& row : semaphores_editor_->rows())
    {
        nlohmann::json semaphore = nlohmann::json::object();
        semaphore["name"] = row.value(0, QString()).toStdString();
        const std::string kind = row.value(1, QString("binary")).toStdString();
        semaphore["kind"] = kind;
        // max_count/initial_count используются парсером только при
        // kind=="counting" (parser/freertos/freertos_parser.h) — для binary
        // не пишем их вовсе, чтобы JSON совпадал со схемой из задания.
        if (kind == "counting")
        {
            semaphore["max_count"] = row.value(2, QString("1")).toInt();
            semaphore["initial_count"] = row.value(3, QString("0")).toInt();
        }
        semaphores.push_back(semaphore);
    }
    settings["semaphores"] = semaphores;

    nlohmann::json mutexes = nlohmann::json::array();
    for (const QStringList& row : mutexes_editor_->rows())
    {
        nlohmann::json mutex = nlohmann::json::object();
        mutex["name"] = row.value(0, QString()).toStdString();
        mutex["kind"] = row.value(1, QString("normal")).toStdString();
        mutexes.push_back(mutex);
    }
    settings["mutexes"] = mutexes;

    nlohmann::json timers = nlohmann::json::array();
    for (const QStringList& row : timers_editor_->rows())
    {
        nlohmann::json timer = nlohmann::json::object();
        timer["name"] = row.value(0, QString()).toStdString();
        timer["kind"] = row.value(1, QString("periodic")).toStdString();
        timer["period_ms"] = row.value(2, QString("1000")).toInt();
        timers.push_back(timer);
    }
    settings["timers"] = timers;

    return settings;
}
