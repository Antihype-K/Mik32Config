#include "main_window.h"

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHash>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QMenuBar>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QSet>
#include <QSettings>
#include <QSplitter>
#include <QStackedWidget>
#include <QStatusBar>
#include <QTabBar>
#include <QTextStream>
#include <QVBoxLayout>

#include <map>
#include <vector>

#include "package_view.h"
#include "properties_panel.h"
#include "diagnostics_panel.h"
#include "project_config.h"
#include "home_screen.h"
#include "new_project_screen.h"
#include "project_manager_screen.h"
#include "code_preview_screen.h"

namespace
{
    // Кап на длину списка «недавних проектов» — QSettings хранит его как
    // QStringList под одним ключом, без капа список рос бы бесконечно.
    constexpr int kMaxRecentProjects = 8;

    QString readFileUtf8(const QString& path)
    {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        {
            return QString();
        }
        QTextStream stream(&file);
        stream.setEncoding(QStringConverter::Utf8);
        return stream.readAll();
    }
}

namespace
{
    // Список типов, которые конфигуратор умеет разбирать и генерировать.
    // Совпадает с набором листьев дерева, помеченных supported=true
    // (gui/peripheral_tree.cpp), и с диспетчером parseProjectDocument.
    const std::vector<std::pair<const char*, int>> kSupportedBlocks = {
        {"gpio", 3}, {"uart", 2}, {"timer16", 3}, {"i2c", 2}, {"spi", 2},
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

    loadRecentProjects();
    home_screen_->setRecentProjects(recent_projects_);

    resize(1280, 800);
    refreshEverything();  // заканчивается applyViewModel() — экран Home по умолчанию.
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

    // Тот же переключатель, что и --demo у консольного приложения
    // (src/main.cpp). Смысл флага — в project_codegen.h: демо-логика в while(1)
    // нужна для аппаратной проверки, но не нужна в «чистой» инициализации —
    // поэтому по умолчанию выключено, включается по явному запросу.
    demo_loops_action_ = project_menu->addAction(
        QString::fromUtf8("Генерировать демо-логику в while(1)"));
    demo_loops_action_->setCheckable(true);
    demo_loops_action_->setChecked(false);

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
    // "vertical" — это ровно содержимое экрана Screen::Config, см. buildChrome.

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

    home_screen_ = new HomeScreen();
    new_project_screen_ = new NewProjectScreen();
    project_manager_screen_ = new ProjectManagerScreen();
    code_preview_screen_ = new CodePreviewScreen();

    connect(home_screen_, &HomeScreen::newProjectRequested, this, &MainWindow::onHomeNewProjectRequested);
    connect(home_screen_, &HomeScreen::openProjectRequested, this, &MainWindow::onHomeOpenProjectRequested);
    connect(home_screen_, &HomeScreen::recentProjectChosen, this, &MainWindow::onHomeRecentProjectChosen);
    connect(new_project_screen_, &NewProjectScreen::createRequested,
            this, &MainWindow::onNewProjectCreateRequested);

    screens_stack_ = new QStackedWidget();
    // Порядок добавления обязан совпадать с NavViewModel::stackIndex
    // (screen_navigator.cpp): Home=0, NewProject=1, Config=2,
    // ProjectManager=3, CodePreview=4.
    screens_stack_->addWidget(home_screen_);
    screens_stack_->addWidget(new_project_screen_);
    screens_stack_->addWidget(vertical);
    screens_stack_->addWidget(project_manager_screen_);
    screens_stack_->addWidget(code_preview_screen_);

    QWidget* central = new QWidget();
    buildChrome(central);
}

void MainWindow::buildChrome(QWidget* central)
{
    top_bar_ = new QWidget();
    top_bar_->setStyleSheet("background: palette(alternate-base);");
    QHBoxLayout* top_bar_layout = new QHBoxLayout(top_bar_);
    top_bar_layout->setContentsMargins(12, 6, 12, 6);

    QPushButton* logo_button = new QPushButton(QString::fromUtf8("МИК32 Конфигуратор периферии"));
    logo_button->setFlat(true);
    logo_button->setCursor(Qt::PointingHandCursor);
    connect(logo_button, &QPushButton::clicked, this, &MainWindow::onGoHome);
    top_bar_layout->addWidget(logo_button);
    top_bar_layout->addStretch(1);

    breadcrumb_label_ = new QLabel();
    breadcrumb_label_->setStyleSheet("color: gray;");
    top_bar_layout->addWidget(breadcrumb_label_);
    top_bar_layout->addStretch(1);

    generate_button_ = new QPushButton(QString::fromUtf8("Сгенерировать код"));
    connect(generate_button_, &QPushButton::clicked, this, &MainWindow::onGenerate);
    top_bar_layout->addWidget(generate_button_);

    tab_strip_ = new QTabBar();
    tab_strip_->addTab(QString::fromUtf8("Pinout & Configuration"));
    tab_strip_->addTab(QString::fromUtf8("Project Manager"));
    tab_strip_->addTab(QString::fromUtf8("Просмотр кода"));
    connect(tab_strip_, &QTabBar::currentChanged, this, &MainWindow::onTabStripChanged);

    QVBoxLayout* central_layout = new QVBoxLayout(central);
    central_layout->setContentsMargins(0, 0, 0, 0);
    central_layout->setSpacing(0);
    central_layout->addWidget(top_bar_);
    central_layout->addWidget(tab_strip_);
    central_layout->addWidget(screens_stack_, 1);

    setCentralWidget(central);
}

void MainWindow::applyViewModel()
{
    const NavViewModel vm = navigator_.viewModel();

    screens_stack_->setCurrentIndex(vm.stackIndex);
    top_bar_->setVisible(vm.topBarVisible);
    tab_strip_->setVisible(vm.tabStripVisible);
    generate_button_->setVisible(vm.generateButtonVisible);
    generate_button_->setEnabled(vm.generateButtonEnabled);
    tab_strip_->setTabEnabled(2, vm.codeTabEnabled);
    breadcrumb_label_->setText(vm.breadcrumbText);

    if (vm.activeTabIndex >= 0 && tab_strip_->currentIndex() != vm.activeTabIndex)
    {
        // blockSignals — переключение экрана программно (например, кликом по
        // выводу схемы) не должно снова дёргать onTabStripChanged и приводить
        // к повторному (но безвредному) navigator_.goTo того же экрана; смысл
        // в том, чтобы вкладочная строка была следствием состояния, а не его
        // независимым источником.
        tab_strip_->blockSignals(true);
        tab_strip_->setCurrentIndex(vm.activeTabIndex);
        tab_strip_->blockSignals(false);
    }
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
    if (type == "spi")      return QString("SPI_%1").arg(instance);
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
    // userflow.drawio, ЭТАП 2 (n05/n06, ветвь «создание нового»): «Новый
    // проект» — это форма (имя/каталог/версия МК/тулчейн/плата), не мгновенный
    // сброс документа. Сам документ создаётся и сохраняется в
    // onNewProjectCreateRequested, когда пользователь заполнит форму.
    if (!confirmDiscardChanges())
    {
        return;
    }
    new_project_screen_->resetForm();
    navigator_.goTo(Screen::NewProject);
    applyViewModel();
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
    // userflow.drawio: «открытие существующего» ведёт сразу в главное окно
    // (коннектор Б), минуя Home/NewProject — сама навигация уже внутри
    // openPath (см. её комментарий).
    openPath(path);
    return true;
}

bool MainWindow::openPath(const QString& path)
{
    std::string error;
    if (!document_.load(path.toStdString(), error))
    {
        QMessageBox::critical(this, QString::fromUtf8("Не удалось открыть файл"),
                              QString::fromStdString(error));
        return false;
    }
    properties_->showPeripheral(QString(), -1);
    refreshEverything();

    // Открытие файла — из File>Open, из Home (недавние) или из аргумента
    // командной строки (gui_main.cpp) — всегда ведёт в главное окно и всегда
    // попадает в список недавних; логика собрана здесь одним местом, а не
    // повторена у каждого вызывающего.
    pushRecentProject(path);
    navigator_.goTo(Screen::Config);
    applyViewModel();
    return true;
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

    // userflow.drawio, ЭТАП 4 (n44/n45): предупреждения не блокируют
    // генерацию, но требуют явного подтверждения — раньше приложение молча
    // генерировало дальше, полагаясь только на счётчик в строке состояния.
    if (diagnostics_->warningCount() > 0)
    {
        const QMessageBox::StandardButton answer = QMessageBox::question(
            this, QString::fromUtf8("Есть предупреждения"),
            QString::fromUtf8("В конфигурации %1 предупреждени(е/я/й). Продолжить генерацию?")
                .arg(diagnostics_->warningCount()),
            QMessageBox::Yes | QMessageBox::No);
        if (answer != QMessageBox::Yes)
        {
            return;
        }
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

    // Вкладка «Просмотр кода» показывает то, что РЕАЛЬНО легло на диск
    // (см. code_preview_screen.h) — читаем обратно, а не второй раз строим
    // GeneratedProjectFirmware в памяти.
    code_preview_screen_->setContents(
        readFileUtf8(output_dir + "/src/main.c"),
        readFileUtf8(output_dir + "/src/mik32_init.c"),
        readFileUtf8(output_dir + "/src/mik32_init.h"),
        readFileUtf8(output_dir + "/platformio.ini"));
    navigator_.setGeneratedOnce(true);
    navigator_.goTo(Screen::CodePreview);
    applyViewModel();
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

    // 4. Навигация: кнопка «Сгенерировать» и breadcrumb зависят от текущего
    // документа, поэтому обновляются в одном цикле с остальным (см. §1
    // MAKET_DESIGN_TZ.md — генератор и есть единственный источник счётчика
    // ошибок).
    navigator_.setDiagnosticErrors(diagnostics_->errorCount());
    const QString project_name = document_.filePath().empty()
        ? QString::fromUtf8("(без имени)")
        : QFileInfo(QString::fromStdString(document_.filePath())).baseName();
    navigator_.setActiveProjectName(project_name);
    project_manager_screen_->setProjectInfo(
        QString::fromStdString(document_.filePath()), packageSummary());
    applyViewModel();
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

// --- Навигация по экранам -----------------------------------------------

void MainWindow::onGoHome()
{
    // Переход на Home — это смена ВИДА, а не отказ от документа: он остаётся
    // в памяти как есть (в отличие от onNew/onOpen, которые документ
    // заменяют и поэтому спрашивают confirmDiscardChanges). Если
    // пользователь оттуда выберет «новый проект» или откроет другой файл,
    // подтверждение потребуется уже там.
    home_screen_->setRecentProjects(recent_projects_);
    navigator_.goTo(Screen::Home);
    applyViewModel();
}

void MainWindow::onTabStripChanged(int tab_index)
{
    switch (tab_index)
    {
        case 0: navigator_.goTo(Screen::Config); break;
        case 1: navigator_.goTo(Screen::ProjectManager); break;
        case 2: navigator_.goTo(Screen::CodePreview); break;
        default: return;
    }
    applyViewModel();
}

void MainWindow::onHomeNewProjectRequested()
{
    new_project_screen_->resetForm();
    navigator_.goTo(Screen::NewProject);
    applyViewModel();
}

void MainWindow::onHomeOpenProjectRequested()
{
    onOpen();
}

void MainWindow::onHomeRecentProjectChosen(const QString& path)
{
    if (!confirmDiscardChanges())
    {
        return;
    }
    // При неудаче openPath уже показал причину (диалог ошибки) и не менял
    // ни документ, ни экран — остаёмся на Home.
    openPath(path);
}

void MainWindow::onNewProjectCreateRequested(const NewProjectParams& params)
{
    // userflow.drawio, n11: «Формирование конфигурации по умолчанию» — здесь
    // это буквально ProjectDocument() (mcu/version по умолчанию, пустой
    // peripherals) плюс немедленная запись по выбранному пути, чтобы у
    // проекта сразу было место на диске (а не отложенное «Сохранить как»).
    QDir dir(params.directory);
    if (!dir.exists() && !dir.mkpath("."))
    {
        QMessageBox::critical(this, QString::fromUtf8("Не удалось создать каталог"),
            QString::fromUtf8("Каталог не существует и не может быть создан:\n%1")
                .arg(params.directory));
        return;
    }
    const QString target_path = dir.filePath(params.name + ".json");

    ProjectDocument fresh;
    std::string error;
    if (!fresh.save(target_path.toStdString(), error))
    {
        QMessageBox::critical(this, QString::fromUtf8("Не удалось создать проект"),
                              QString::fromStdString(error));
        return;
    }

    document_ = std::move(fresh);
    properties_->setContext(&document_, &knowledge_);
    properties_->showPeripheral(QString(), -1);
    pushRecentProject(target_path);
    navigator_.goTo(Screen::Config);
    refreshEverything();
}

void MainWindow::loadRecentProjects()
{
    QSettings settings;
    recent_projects_ = settings.value("recentProjects").toStringList();
}

void MainWindow::pushRecentProject(const QString& path)
{
    recent_projects_.removeAll(path);
    recent_projects_.prepend(path);
    while (recent_projects_.size() > kMaxRecentProjects)
    {
        recent_projects_.removeLast();
    }
    QSettings settings;
    settings.setValue("recentProjects", recent_projects_);
    home_screen_->setRecentProjects(recent_projects_);
}

QString MainWindow::packageSummary() const
{
    return QString::fromUtf8("%1, %2 выводов")
        .arg(QString::fromStdString(package_.package)).arg(package_.pin_count);
}
