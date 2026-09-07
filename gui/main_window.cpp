#include "main_window.h"

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QFileDialog>
#include <QHash>
#include <QInputDialog>
#include <QLabel>
#include <QMenuBar>
#include <QMessageBox>
#include <QScrollArea>
#include <QSet>
#include <QSplitter>
#include <QStatusBar>
#include <QVBoxLayout>

#include <map>
#include <vector>

#include "package_view.h"
#include "properties_panel.h"
#include "diagnostics_panel.h"
#include "project_config.h"

namespace
{
    // Список типов, которые конфигуратор умеет разбирать и генерировать.
    // Совпадает с набором листьев дерева, помеченных supported=true
    // (gui/peripheral_tree.cpp), и с диспетчером parseProjectDocument.
    const std::vector<std::pair<const char*, int>> kSupportedBlocks = {
        {"gpio", 3}, {"uart", 2}, {"timer16", 3}, {"i2c", 2},
    };
}

MainWindow::MainWindow(const ProjectKnowledge& knowledge,
                       const PackageKnowledge& package,
                       const GuiPaths& paths,
                       QWidget* parent)
    : QMainWindow(parent)
    , knowledge_(knowledge)
    , package_(package)
    , paths_(paths)
{
    buildMenus();
    buildLayout();

    properties_->setContext(&document_, &knowledge_);
    package_view_->setPackage(package_);

    resize(1280, 800);
    refreshEverything();
}

void MainWindow::buildMenus()
{
    QMenu* file_menu = menuBar()->addMenu(QString::fromUtf8("&Файл"));

    QAction* new_action = file_menu->addAction(QString::fromUtf8("&Новый проект"));
    new_action->setShortcut(QKeySequence::New);
    connect(new_action, &QAction::triggered, this, &MainWindow::onNew);

    QAction* open_action = file_menu->addAction(QString::fromUtf8("&Открыть..."));
    open_action->setShortcut(QKeySequence::Open);
    connect(open_action, &QAction::triggered, this, [this]() { onOpen(); });

    QAction* save_action = file_menu->addAction(QString::fromUtf8("&Сохранить"));
    save_action->setShortcut(QKeySequence::Save);
    connect(save_action, &QAction::triggered, this, [this]() { onSave(); });

    QAction* save_as_action = file_menu->addAction(QString::fromUtf8("Сохранить &как..."));
    save_as_action->setShortcut(QKeySequence::SaveAs);
    connect(save_as_action, &QAction::triggered, this, [this]() { onSaveAs(); });

    file_menu->addSeparator();
    QAction* quit_action = file_menu->addAction(QString::fromUtf8("В&ыход"));
    quit_action->setShortcut(QKeySequence::Quit);
    connect(quit_action, &QAction::triggered, this, &QWidget::close);

    QMenu* project_menu = menuBar()->addMenu(QString::fromUtf8("&Проект"));

    QAction* generate_action = project_menu->addAction(
        QString::fromUtf8("&Сгенерировать код..."));
    generate_action->setShortcut(QKeySequence("Ctrl+G"));
    connect(generate_action, &QAction::triggered, this, &MainWindow::onGenerate);

    // Тот же переключатель, что и --no-demo у консольного приложения
    // (src/main.cpp). Смысл флага — в project_codegen.h: демо-логика в while(1)
    // нужна для аппаратной проверки, но не нужна в «чистой» инициализации.
    demo_loops_action_ = project_menu->addAction(
        QString::fromUtf8("Генерировать демо-логику в while(1)"));
    demo_loops_action_->setCheckable(true);
    demo_loops_action_->setChecked(true);

    QMenu* help_menu = menuBar()->addMenu(QString::fromUtf8("&Справка"));
    QAction* about_action = help_menu->addAction(QString::fromUtf8("&О программе"));
    connect(about_action, &QAction::triggered, this, &MainWindow::onAbout);
}

void MainWindow::buildLayout()
{
    tree_ = new PeripheralTree();
    package_view_ = new PackageView();
    properties_ = new PropertiesPanel();
    diagnostics_ = new DiagnosticsPanel();

    // Панель свойств может быть длиннее экрана (у GPIO это 16 флажков выводов
    // плюс параметры группы), поэтому она живёт внутри прокрутки. Сама панель
    // об этом не знает — так её можно поставить и в другое место.
    QScrollArea* properties_scroll = new QScrollArea();
    properties_scroll->setWidget(properties_);
    properties_scroll->setWidgetResizable(true);
    properties_scroll->setMinimumWidth(320);

    QSplitter* horizontal = new QSplitter(Qt::Horizontal);
    horizontal->addWidget(tree_);
    horizontal->addWidget(package_view_);
    horizontal->addWidget(properties_scroll);
    horizontal->setStretchFactor(0, 1);
    horizontal->setStretchFactor(1, 3);
    horizontal->setStretchFactor(2, 2);
    // Стартовые ширины заданы явно: без них Qt раздаёт место по минимальным
    // размерам виджетов, и панель свойств открывается уже с горизонтальной
    // прокруткой — поля обрезаны на первом же экране.
    horizontal->setSizes({260, 700, 420});

    QSplitter* vertical = new QSplitter(Qt::Vertical);
    vertical->addWidget(horizontal);
    vertical->addWidget(diagnostics_);
    vertical->setStretchFactor(0, 4);
    vertical->setStretchFactor(1, 1);

    setCentralWidget(vertical);
    statusBar()->showMessage(QString::fromUtf8("Готово"));

    connect(tree_, &PeripheralTree::selectionChanged, this, &MainWindow::onSelectionChanged);
    connect(tree_, &PeripheralTree::enableRequested, this, &MainWindow::onEnableRequested);
    connect(tree_, &PeripheralTree::disableRequested, this, &MainWindow::onDisableRequested);
    connect(tree_, &PeripheralTree::renameRequested, this, &MainWindow::onRenameRequested);
    connect(tree_, &PeripheralTree::resetRequested, this, &MainWindow::onResetRequested);
    connect(tree_, &PeripheralTree::copySettingsRequested, this, &MainWindow::onCopySettingsRequested);

    connect(properties_, &PropertiesPanel::documentChanged, this, &MainWindow::onDocumentChanged);
    connect(package_view_, &PackageView::pinClicked, this, &MainWindow::onPinClicked);
    connect(diagnostics_, &DiagnosticsPanel::jumpToBlock, this, &MainWindow::onJumpToBlock);
}

QString MainWindow::defaultTitle(const QString& type, int instance)
{
    // Имена совпадают с подписями листьев дерева (gui/peripheral_tree.cpp):
    // в сообщениях об ошибках пользователь должен видеть ровно то, что видит
    // в дереве. "uart" отображается как USART — так он назван и в даташите,
    // и в ТЗ §3.1.1.3.
    if (type == "uart")     return QString("USART_%1").arg(instance);
    if (type == "gpio")     return QString("GPIO_%1").arg(instance);
    if (type == "timer16")  return QString("Timer16_%1").arg(instance);
    if (type == "i2c")      return QString("I2C_%1").arg(instance);
    return QString("%1_%2").arg(type).arg(instance);
}

QString MainWindow::blockTitle(const QString& type, int instance) const
{
    const std::vector<int> indices = document_.blockIndices(type.toStdString(), instance);
    if (!indices.empty())
    {
        const nlohmann::json name = document_.field(indices.front(), "name");
        if (name.is_string() && !name.get<std::string>().empty())
        {
            return QString::fromStdString(name.get<std::string>());
        }
    }
    return defaultTitle(type, instance);
}

// --- Файл -------------------------------------------------------------------

bool MainWindow::confirmDiscardChanges()
{
    if (!document_.isModified())
    {
        return true;
    }
    const QMessageBox::StandardButton answer = QMessageBox::question(
        this, QString::fromUtf8("Несохранённые изменения"),
        QString::fromUtf8("Конфигурация изменена. Сохранить перед продолжением?"),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);

    if (answer == QMessageBox::Cancel)
    {
        return false;
    }
    if (answer == QMessageBox::Save)
    {
        return onSave();
    }
    return true;
}

void MainWindow::onNew()
{
    if (!confirmDiscardChanges())
    {
        return;
    }
    document_ = ProjectDocument();
    properties_->setContext(&document_, &knowledge_);
    properties_->showPeripheral(QString(), -1);
    refreshEverything();
}

bool MainWindow::onOpen()
{
    if (!confirmDiscardChanges())
    {
        return false;
    }
    const QString path = QFileDialog::getOpenFileName(
        this, QString::fromUtf8("Открыть конфигурацию"), QString(),
        QString::fromUtf8("Конфигурация МИК32 (*.json);;Все файлы (*)"));
    if (path.isEmpty())
    {
        return false;
    }
    openPath(path);
    return true;
}

void MainWindow::openPath(const QString& path)
{
    std::string error;
    if (!document_.load(path.toStdString(), error))
    {
        QMessageBox::critical(this, QString::fromUtf8("Не удалось открыть файл"),
                              QString::fromStdString(error));
        return;
    }
    properties_->showPeripheral(QString(), -1);
    refreshEverything();
}

bool MainWindow::onSave()
{
    if (document_.filePath().empty())
    {
        return onSaveAs();
    }
    std::string error;
    if (!document_.save(document_.filePath(), error))
    {
        QMessageBox::critical(this, QString::fromUtf8("Не удалось сохранить"),
                              QString::fromStdString(error));
        return false;
    }
    statusBar()->showMessage(QString::fromUtf8("Сохранено: %1")
                                 .arg(QString::fromStdString(document_.filePath())), 5000);
    updateWindowTitle();
    return true;
}

bool MainWindow::onSaveAs()
{
    const QString path = QFileDialog::getSaveFileName(
        this, QString::fromUtf8("Сохранить конфигурацию"),
        document_.filePath().empty() ? QString("config.json")
                                     : QString::fromStdString(document_.filePath()),
        QString::fromUtf8("Конфигурация МИК32 (*.json)"));
    if (path.isEmpty())
    {
        return false;
    }
    std::string error;
    if (!document_.save(path.toStdString(), error))
    {
        QMessageBox::critical(this, QString::fromUtf8("Не удалось сохранить"),
                              QString::fromStdString(error));
        return false;
    }
    statusBar()->showMessage(QString::fromUtf8("Сохранено: %1").arg(path), 5000);
    updateWindowTitle();
    return true;
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    if (confirmDiscardChanges())
    {
        event->accept();
    }
    else
    {
        event->ignore();
    }
}

// --- Кодогенерация ----------------------------------------------------------

void MainWindow::onGenerate()
{
    // Порядок шагов ровно тот же, что в консольном пайплайне (src/main.cpp) и
    // тот, который требует ТЗ §3.1.6: сначала per-module валидация, затем
    // кросс-модульная, и только потом генерация. Ошибки уже посчитаны панелью
    // диагностики — второй раз их не проверяем, а спрашиваем у неё.
    if (diagnostics_->errorCount() > 0)
    {
        QMessageBox::critical(this, QString::fromUtf8("Генерация невозможна"),
            QString::fromUtf8("В конфигурации есть ошибки (%1). ТЗ §3.1.6.3: ошибки "
                              "блокируют кодогенерацию — исправьте их и повторите.")
                .arg(diagnostics_->errorCount()));
        return;
    }

    ParseResult<ProjectConfig> parsed = parseProjectDocument(document_.document());
    if (!parsed.success)
    {
        QMessageBox::critical(this, QString::fromUtf8("Ошибка разбора конфигурации"),
                              QString::fromStdString(parsed.error));
        return;
    }

    const QString output_dir = QFileDialog::getSaveFileName(
        this, QString::fromUtf8("Каталог проекта прошивки"),
        QString::fromStdString(paths_.generated_root_dir) + "/new_project",
        QString(), nullptr, QFileDialog::DontConfirmOverwrite);
    if (output_dir.isEmpty())
    {
        return;
    }

    ParseResult<GeneratedProjectFirmware> firmware = generateProjectFirmware(
        *parsed.config, knowledge_, demo_loops_action_->isChecked());
    if (!firmware.success)
    {
        QMessageBox::critical(this, QString::fromUtf8("Ошибка кодогенерации"),
                              QString::fromStdString(firmware.error));
        return;
    }

    CodegenWriteResult written = writeGeneratedProject(
        *firmware.config, paths_.skeleton_dir, output_dir.toStdString());
    if (!written.success)
    {
        QMessageBox::critical(this, QString::fromUtf8("Ошибка записи проекта"),
                              QString::fromStdString(written.error));
        return;
    }

    // Про USER CODE говорим явно: пользователь, правивший main.c руками,
    // должен знать, что его правки сохранены, а не затёрты (ТЗ §3.1.7.3).
    QMessageBox::information(this, QString::fromUtf8("Проект сгенерирован"),
        QString::fromUtf8("Проект записан в:\n%1\n\nСодержимое между маркерами "
                          "USER CODE BEGIN/END в src/main.c сохранено.")
            .arg(output_dir));
    statusBar()->showMessage(QString::fromUtf8("Сгенерировано: %1").arg(output_dir), 5000);
}

void MainWindow::onAbout()
{
    QMessageBox::about(this, QString::fromUtf8("О программе"),
        QString::fromUtf8(
            "<b>Конфигуратор МИК32 «Амур»</b><br><br>"
            "Настройка периферии и генерация кода инициализации.<br>"
            "Корпус: %1, %2 выводов.<br><br>"
            "Ядро (парсеры, база знаний, кодогенерация) общее с консольным "
            "приложением MIK32-Configurator.")
            .arg(QString::fromStdString(package_.package)).arg(package_.pin_count));
}

// --- Дерево -----------------------------------------------------------------

void MainWindow::onSelectionChanged(const PeripheralRef& ref)
{
    if (!ref.isValid())
    {
        properties_->showPeripheral(QString(), -1);
        return;
    }
    properties_->showPeripheral(ref.type, ref.instance);
}

void MainWindow::onEnableRequested(const PeripheralRef& ref)
{
    if (!ref.isValid() || document_.isEnabled(ref.type.toStdString(), ref.instance))
    {
        return;
    }
    document_.addBlock(ref.type.toStdString(), ref.instance);
    properties_->showPeripheral(ref.type, ref.instance);
    refreshEverything();
}

void MainWindow::onDisableRequested(const PeripheralRef& ref)
{
    if (!ref.isValid())
    {
        return;
    }
    document_.removeBlocks(ref.type.toStdString(), ref.instance);
    properties_->showPeripheral(ref.type, ref.instance);
    refreshEverything();
}

void MainWindow::onRenameRequested(const PeripheralRef& ref)
{
    if (!ref.isValid() || !document_.isEnabled(ref.type.toStdString(), ref.instance))
    {
        QMessageBox::information(this, QString::fromUtf8("Переименование"),
            QString::fromUtf8("Имя можно задать только включённому блоку."));
        return;
    }

    bool accepted = false;
    const QString name = QInputDialog::getText(
        this, QString::fromUtf8("Имя блока"),
        QString::fromUtf8("Пользовательское имя (пусто — вернуть стандартное):"),
        QLineEdit::Normal, blockTitle(ref.type, ref.instance), &accepted);
    if (!accepted)
    {
        return;
    }

    // Имя хранится в самом блоке конфигурации, полем "name". Парсеры
    // неизвестные поля не отвергают, поэтому файл остаётся валидным, а имя
    // переживает сохранение и открытие — отдельный «файл проекта GUI» ради
    // одной строки заводить незачем.
    for (int index : document_.blockIndices(ref.type.toStdString(), ref.instance))
    {
        if (name.trimmed().isEmpty() || name == defaultTitle(ref.type, ref.instance))
        {
            document_.setField(index, "name", nlohmann::json());
        }
        else
        {
            document_.setField(index, "name", name.toStdString());
        }
    }
    refreshEverything();
}

void MainWindow::onResetRequested(const PeripheralRef& ref)
{
    if (!ref.isValid())
    {
        return;
    }
    // «Сброс» — это возврат к значениям по умолчанию, а не отключение:
    // блок остаётся в конфигурации. Реализовано удалением с последующим
    // добавлением, чтобы значения по умолчанию оставались в одном месте
    // (ProjectDocument::defaultBlock), а не дублировались здесь.
    const bool was_enabled = document_.isEnabled(ref.type.toStdString(), ref.instance);
    document_.removeBlocks(ref.type.toStdString(), ref.instance);
    if (was_enabled)
    {
        document_.addBlock(ref.type.toStdString(), ref.instance);
    }
    properties_->showPeripheral(ref.type, ref.instance);
    refreshEverything();
}

void MainWindow::onCopySettingsRequested(const PeripheralRef& ref)
{
    if (!ref.isValid() || !document_.isEnabled(ref.type.toStdString(), ref.instance))
    {
        QMessageBox::information(this, QString::fromUtf8("Копирование настроек"),
            QString::fromUtf8("Копировать можно только настройки включённого блока."));
        return;
    }

    // Копирование в ДРУГОЙ экземпляр того же типа: настройки UART_0 бессмысленно
    // переносить в I2C_1, а перенос между экземплярами одного типа — типовая
    // задача (два одинаковых UART, три одинаковых таймера).
    QStringList targets;
    for (const auto& supported : kSupportedBlocks)
    {
        if (ref.type != QString::fromUtf8(supported.first))
        {
            continue;
        }
        for (int instance = 0; instance < supported.second; ++instance)
        {
            if (instance != ref.instance)
            {
                targets << defaultTitle(ref.type, instance);
            }
        }
    }
    if (targets.isEmpty())
    {
        return;
    }

    bool accepted = false;
    const QString chosen = QInputDialog::getItem(
        this, QString::fromUtf8("Копировать настройки"),
        QString::fromUtf8("Скопировать настройки %1 в:").arg(blockTitle(ref.type, ref.instance)),
        targets, 0, false, &accepted);
    if (!accepted || chosen.isEmpty())
    {
        return;
    }
    const int target_instance = chosen.section('_', -1).toInt();

    // Копируются все поля, кроме instance и name: instance — это адрес
    // приёмника (иначе получились бы два блока с одним номером, что
    // parseProjectDocument считает ошибкой), а имя — свойство конкретного
    // блока, а не его настроек.
    document_.removeBlocks(ref.type.toStdString(), target_instance);
    for (int source_index : document_.blockIndices(ref.type.toStdString(), ref.instance))
    {
        const int new_index = document_.addBlock(ref.type.toStdString(), target_instance);
        for (const auto& field : document_.document()["peripherals"][source_index].items())
        {
            if (field.key() == "instance" || field.key() == "type" || field.key() == "name")
            {
                continue;
            }
            document_.setField(new_index, field.key(), field.value());
        }
    }
    properties_->showPeripheral(ref.type, target_instance);
    tree_->selectPeripheral(ref.type, target_instance);
    refreshEverything();
}

// --- Обновление -------------------------------------------------------------

void MainWindow::onDocumentChanged()
{
    refreshEverything();
}

void MainWindow::refreshEverything()
{
    // 1. Состояние дерева: какие блоки включены и как называются.
    QSet<QString> enabled;
    QHash<QString, QString> names;
    for (const auto& supported : kSupportedBlocks)
    {
        const QString type = QString::fromUtf8(supported.first);
        for (int instance = 0; instance < supported.second; ++instance)
        {
            if (!document_.isEnabled(type.toStdString(), instance))
            {
                continue;
            }
            enabled.insert(PeripheralTree::keyFor(type, instance));
            const QString title = blockTitle(type, instance);
            if (title != defaultTitle(type, instance))
            {
                names.insert(PeripheralTree::keyFor(type, instance), title);
            }
        }
    }
    tree_->refreshState(enabled, names);

    // 2. Диагностика — единственный источник истины о валидности: и статусная
    // строка, и запрет генерации смотрят на её счётчики, а не считают заново.
    const std::vector<Diagnostic> diagnostics = document_.validate(knowledge_);
    diagnostics_->setDiagnostics(diagnostics);

    // 3. Схема корпуса.
    updatePackageColors();

    updateWindowTitle();
    statusBar()->showMessage(QString::fromUtf8("Ошибок: %1, предупреждений: %2")
        .arg(diagnostics_->errorCount()).arg(diagnostics_->warningCount()));
}

void MainWindow::updatePackageColors()
{
    // Претенденты на каждый вывод собираются один раз: сначала для всех блоков
    // строится список (gpio_instance, pin) -> кто занял, потом список
    // переводится в номера выводов корпуса через PackageKnowledge::findByGpio.
    // Обратное отображение (номер вывода -> разряд GPIO) есть только в
    // package.json — периферийные базы знаний про корпус ничего не знают.
    std::map<std::pair<int, int>, QStringList> owners;
    std::map<std::pair<int, int>, QString> owner_types;

    const nlohmann::json& peripherals = document_.document()["peripherals"];
    for (size_t index = 0; index < peripherals.size(); ++index)
    {
        const QString type = QString::fromStdString(
            peripherals[index].value("type", std::string()));
        const int instance = peripherals[index].value("instance", -1);
        const QString title = blockTitle(type, instance);

        for (const auto& claimed : document_.claimedPins(static_cast<int>(index), knowledge_))
        {
            // Один и тот же блок GPIO может «занимать» вывод дважды, если он
            // описан двумя группами — это не конфликт, поэтому дубликаты
            // владельцев с одинаковым именем не накапливаем.
            if (!owners[claimed].contains(title))
            {
                owners[claimed] << title;
            }
            owner_types[claimed] = type;
        }
    }

    QHash<int, PinState> states;
    QHash<int, QString> tooltips;
    for (const PackagePin& pin : package_.pins)
    {
        if (!pin.isMultiplexed())
        {
            states.insert(pin.pin, PinState::Service);
            tooltips.insert(pin.pin, QString::fromUtf8("%1 — служебный вывод")
                                         .arg(QString::fromStdString(pin.name)));
            continue;
        }

        const std::pair<int, int> key(*pin.gpio_instance, *pin.gpio_pin);
        const auto found = owners.find(key);
        const QString base = QString::fromUtf8("Вывод %1 — %2 (GPIO_%3.%4)")
            .arg(pin.pin).arg(QString::fromStdString(pin.name))
            .arg(*pin.gpio_instance).arg(*pin.gpio_pin);

        if (found == owners.end() || found->second.isEmpty())
        {
            states.insert(pin.pin, PinState::Free);
            tooltips.insert(pin.pin, base + QString::fromUtf8("\nСвободен"));
            continue;
        }
        if (found->second.size() > 1)
        {
            // ТЗ §3.1.6.3: конфликт — ошибка, блокирующая генерацию. На схеме
            // он должен быть виден сразу, без чтения списка диагностики.
            states.insert(pin.pin, PinState::Conflict);
            tooltips.insert(pin.pin, base + QString::fromUtf8("\nКОНФЛИКТ: ")
                                         + found->second.join(QString::fromUtf8(" и ")));
            continue;
        }
        states.insert(pin.pin, owner_types[key] == "gpio" ? PinState::Gpio : PinState::Peripheral);
        tooltips.insert(pin.pin, base + QString::fromUtf8("\nЗанят: ") + found->second.first());
    }

    package_view_->setPinStates(states, tooltips);
}

void MainWindow::updateWindowTitle()
{
    const QString file = document_.filePath().empty()
        ? QString::fromUtf8("(без имени)")
        : QString::fromStdString(document_.filePath());
    setWindowTitle(QString::fromUtf8("%1%2 — Конфигуратор МИК32")
                       .arg(file, document_.isModified() ? "*" : ""));
}

void MainWindow::onPinClicked(int pin_number)
{
    // Клик по выводу — это вопрос «кто его занял». Отвечаем выделением блока
    // в дереве; если вывод свободен, показываем это в строке состояния, а не
    // молчим (молчание неотличимо от «программа зависла»).
    const PackagePin* pin = package_.findPin(pin_number);
    if (pin == nullptr || !pin->isMultiplexed())
    {
        statusBar()->showMessage(QString::fromUtf8("Вывод %1 — служебный, назначению не подлежит")
                                     .arg(pin_number), 5000);
        return;
    }

    const nlohmann::json& peripherals = document_.document()["peripherals"];
    for (size_t index = 0; index < peripherals.size(); ++index)
    {
        for (const auto& claimed : document_.claimedPins(static_cast<int>(index), knowledge_))
        {
            if (claimed.first == *pin->gpio_instance && claimed.second == *pin->gpio_pin)
            {
                const QString type = QString::fromStdString(
                    peripherals[index].value("type", std::string()));
                tree_->selectPeripheral(type, peripherals[index].value("instance", -1));
                return;
            }
        }
    }
    statusBar()->showMessage(QString::fromUtf8("Вывод %1 (GPIO_%2.%3) свободен")
        .arg(pin_number).arg(*pin->gpio_instance).arg(*pin->gpio_pin), 5000);
}

void MainWindow::onJumpToBlock(int block_index)
{
    const nlohmann::json& peripherals = document_.document()["peripherals"];
    if (block_index < 0 || block_index >= static_cast<int>(peripherals.size()))
    {
        return;
    }
    tree_->selectPeripheral(
        QString::fromStdString(peripherals[block_index].value("type", std::string())),
        peripherals[block_index].value("instance", -1));
}
