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
#include <QCursor>
#include <QInputDialog>
#include <QLabel>
#include <QMenu>
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

#include <algorithm>
#include <map>
#include <vector>

#include "package_view.h"
#include "properties_panel.h"
#include "diagnostics_panel.h"
#include "project_config.h"
#include "cmake_project_codegen.h"
#include "rust_project_codegen.h"
#include "reverse_import_apply.h"
#include "home_screen.h"
#include "new_project_screen.h"
#include "project_manager_screen.h"
#include "clock_config_screen.h"
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
        {"gpio", 3}, {"uart", 2}, {"timer16", 3}, {"i2c", 2}, {"spi", 2}, {"timer32", 3},
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
    project_manager_screen_->setContext(&document_);
    clock_config_screen_->setContext(&document_);
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

    // ТЗ на эту фичу не заведён отдельным пунктом — решение из /grill-me:
    // "вычленить периферию" из ЧУЖОГО, не сгенерированного этим конфигуратором
    // проекта (docs/implementations/reverse_import.md). Отдельный пункт
    // меню, а не подмешано в "Открыть...", потому что результат — другого
    // происхождения документ (reverse_import_active_=true), который "Сгенерировать
    // код" обрабатывает иначе (см. onGenerate/applyReverseImportChanges).
    QAction* import_action = file_menu->addAction(QString::fromUtf8("&Импортировать проект..."));
    connect(import_action, &QAction::triggered, this, &MainWindow::onImportProject);

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
    clock_config_screen_ = new ClockConfigScreen();
    project_manager_screen_ = new ProjectManagerScreen();
    code_preview_screen_ = new CodePreviewScreen();

    connect(home_screen_, &HomeScreen::newProjectRequested, this, &MainWindow::onHomeNewProjectRequested);
    connect(home_screen_, &HomeScreen::openProjectRequested, this, &MainWindow::onHomeOpenProjectRequested);
    connect(home_screen_, &HomeScreen::recentProjectChosen, this, &MainWindow::onHomeRecentProjectChosen);
    connect(new_project_screen_, &NewProjectScreen::createRequested,
            this, &MainWindow::onNewProjectCreateRequested);
    connect(clock_config_screen_, &ClockConfigScreen::documentChanged,
            this, &MainWindow::onDocumentChanged);
    connect(project_manager_screen_, &ProjectManagerScreen::documentChanged,
            this, &MainWindow::onDocumentChanged);

    screens_stack_ = new QStackedWidget();
    // Порядок добавления обязан совпадать с NavViewModel::stackIndex
    // (screen_navigator.cpp): Home=0, NewProject=1, Config=2, ClockConfig=3,
    // ProjectManager=4, CodePreview=5.
    screens_stack_->addWidget(home_screen_);
    screens_stack_->addWidget(new_project_screen_);
    screens_stack_->addWidget(vertical);
    screens_stack_->addWidget(clock_config_screen_);
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
    // Порядок как в STM32CubeMX: Pinout & Configuration | Clock Configuration
    // | Project Manager — Clock Configuration верхнеуровневая вкладка, а не
    // вложена внутрь Project Manager (по прямому требованию пользователя).
    tab_strip_->addTab(QString::fromUtf8("Clock Configuration"));
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
    // Вкладка 3 — «Просмотр кода»: недоступна до первой успешной генерации
    // (vm.codeTabEnabled == state_.hasGeneratedOnce). Project Manager (вкладка
    // 2, тулчейн/HAL-источник/per_type_files) должна работать ДО генерации —
    // это настройки, которые читает сам кодоген, а не его результат.
    tab_strip_->setTabEnabled(3, vm.codeTabEnabled);
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
    // Открытие обычного project.json завершает режим импорта, если он был
    // активен (см. onImportProject) — иначе "Сгенерировать код" попытался бы
    // точечно патчить файлы ПРЕЖНЕГО импортированного проекта данными
    // только что открытого, не имеющего с ним ничего общего, документа.
    reverse_import_active_ = false;
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

void MainWindow::onImportProject()
{
    if (!confirmDiscardChanges())
    {
        return;
    }

    const QString dir = QFileDialog::getExistingDirectory(
        this, QString::fromUtf8("Импортировать проект — выберите каталог с исходниками"));
    if (dir.isEmpty())
    {
        return;
    }

    reverse_import::ImportedProject imported = reverse_import::importProjectFromDirectory(dir.toStdString());

    if (imported.document["peripherals"].empty())
    {
        QString message = QString::fromUtf8(
            "В каталоге \"%1\" не найдено ни одной распознаваемой периферии (UART/I2C/"
            "Timer32/GPIO).").arg(dir);
        if (!imported.notes.empty())
        {
            message += QString::fromUtf8("\n\nПодробности:\n- ") +
                QString::fromStdString(imported.notes.front());
        }
        QMessageBox::warning(this, QString::fromUtf8("Периферия не найдена"), message);
        return;
    }

    document_.loadFromDocument(imported.document);
    reverse_import_root_ = dir;
    imported_project_ = imported;
    reverse_import_active_ = true;

    // feedback_reverse_import_unresolved_fields_policy: поля, которые
    // маппер не смог разобрать (FieldProvenance::unresolved), блокируют
    // кодогенерацию всего проекта и подсвечиваются красным — см.
    // ProjectDocument::validate()/peripheral_tree.cpp/properties_panel.cpp.
    document_.clearUnresolvedFields();
    for (std::size_t block_index = 0; block_index < imported.block_provenance.size(); ++block_index)
    {
        std::vector<std::string> unresolved_names;
        for (const reverse_import::FieldProvenance& field : imported.block_provenance[block_index])
        {
            if (field.unresolved)
            {
                unresolved_names.push_back(field.json_field);
            }
        }
        document_.setUnresolvedFields(static_cast<int>(block_index), unresolved_names);
    }

    properties_->showPeripheral(QString(), -1);
    refreshEverything();
    navigator_.goTo(Screen::Config);
    applyViewModel();

    statusBar()->showMessage(
        QString::fromUtf8("Импортировано блоков: %1 из %2")
            .arg(imported.document["peripherals"].size())
            .arg(dir), 8000);

    // Пользователь должен видеть, что НЕ распознано, а не только то, что
    // распознано — цель фичи. Полный список может быть длинным (десятки
    // файлов), поэтому в диалог — а не в statusBar на несколько секунд.
    if (!imported.notes.empty())
    {
        QString details;
        for (const std::string& note : imported.notes)
        {
            details += QString::fromStdString(note) + "\n\n";
        }
        QMessageBox::information(this, QString::fromUtf8("Импорт: %1 предупреждени(е/я/й)")
                                            .arg(imported.notes.size()),
                                  details.trimmed());
    }
}

void MainWindow::applyReverseImportChanges()
{
    const reverse_import::ApplyReport report = reverse_import::applyImportedChanges(
        imported_project_.document["peripherals"], document_.document()["peripherals"],
        imported_project_.block_provenance);

    int success_count = 0;
    QString failures;
    for (const reverse_import::PatchResult& result : report.patch_results)
    {
        if (result.success)
        {
            ++success_count;
        }
        else
        {
            failures += QString::fromStdString(result.error) + "\n";
        }
    }

    QString unpatchable_text;
    for (const reverse_import::UnpatchableChange& change : report.unpatchable)
    {
        unpatchable_text += QString::fromUtf8("Блок #%1, поле \"%2\": %3\n")
            .arg(change.block_index)
            .arg(QString::fromStdString(change.field_name))
            .arg(QString::fromStdString(change.reason));
    }

    // Переимпорт с диска, а не ручная сборка нового "снимка ДО": строки,
    // которые applyImportedChanges успешно переписал, теперь на диске другие,
    // а те, что не удалось (unpatchable) — остались как были ДО правки в GUI.
    // Документ после этого обязан отражать РЕАЛЬНОЕ содержимое файлов, а не
    // то, что пользователь временно видел в PropertiesPanel — иначе GUI
    // соврёт о том, что действительно сохранено (см. reverse_import_apply.h).
    reverse_import::ImportedProject reimported =
        reverse_import::importProjectFromDirectory(reverse_import_root_.toStdString());
    document_.loadFromDocument(reimported.document);
    imported_project_ = reimported;
    refreshEverything();

    QString summary = QString::fromUtf8("Применено строк: %1.").arg(success_count);
    if (!failures.isEmpty())
    {
        summary += QString::fromUtf8("\n\nНе удалось записать (изменилось на диске с момента "
                                     "импорта?):\n") + failures;
    }
    if (!unpatchable_text.isEmpty())
    {
        summary += QString::fromUtf8("\n\nЭти поля изменены в GUI, но НЕ записаны обратно в "
                                     "исходник (нет строки-источника или значение "
                                     "нередактируемое) — значение в GUI сброшено на то, что "
                                     "реально лежит на диске:\n") + unpatchable_text;
    }

    QMessageBox::information(this, QString::fromUtf8("Применение изменений к исходникам"), summary);
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
    //
    // Импортированный (не наш собственный) проект — исключение из этого
    // порядка: кнопка "Сгенерировать код" здесь означает не полную
    // кодогенерацию по шаблону, а точечное применение правок обратно в
    // чужие файлы (см. onImportProject/applyReverseImportChanges) — ровно
    // так, как это было описано изначально: "меняем бодрейт — он меняется
    // просто в переменной, остальное не трогается". Полная schema-валидация
    // (parseProjectDocument) здесь неприменима буквально — часть значений в
    // чужом проекте намеренно не приводится к нашим enum'ам (см.
    // uart_reverse_mapper: "нестандартный baud_rate сохраняется как есть, а
    // не подгоняется"), а диагностические "Ошибки" здесь обычно означают
    // конфликты, УЖЕ существующие в исходном коде проекта (например, один
    // физический вывод одновременно занят GPIO и Timer32-каналом или I2C) —
    // точечная правка изменённого поля их не создаёт и не устраняет.
    // Кнопка (см. ScreenNavigator::viewModel — generateButtonEnabled) для
    // такого проекта остаётся включённой при ошибках, но нажатие требует
    // ЯВНОГО подтверждения "игнорировать и продолжить" — тот же принцип
    // explicit acknowledgment, что уже применяется ниже к предупреждениям
    // для обычных проектов (userflow.drawio, ЭТАП 4), а не молчаливый обход.
    if (reverse_import_active_)
    {
        if (diagnostics_->errorCount() > 0)
        {
            const QMessageBox::StandardButton answer = QMessageBox::warning(
                this, QString::fromUtf8("В импортированном проекте есть ошибки"),
                QString::fromUtf8(
                    "Диагностика нашла %1 ошибк(у/и) — как правило, это конфликты, уже "
                    "существующие в исходном коде самого проекта (например, один вывод "
                    "занят сразу двумя периферийными блоками), а не что-то, что вносит "
                    "точечное применение правки. Игнорировать и всё равно применить "
                    "изменённые поля?")
                    .arg(diagnostics_->errorCount()),
                QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
            if (answer != QMessageBox::Yes)
            {
                return;
            }
        }
        applyReverseImportChanges();
        return;
    }

    if (diagnostics_->errorCount() > 0)
    {
        QMessageBox::critical(this, QString::fromUtf8("Генерация невозможна"),
            QString::fromUtf8("В конфигурации есть ошибки (%1). Ошибки "
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

    // FreeRTOS v1 — только PlatformIO (см. src/main.cpp, тот же явный отказ
    // вместо молчаливой генерации без FreeRTOS при toolchain=cmake).
    if (parsed.config->freertos.enabled && parsed.config->toolchain == ToolchainKind::CMake)
    {
        QMessageBox::critical(this, QString::fromUtf8("Несовместимые настройки"),
            QString::fromUtf8("FreeRTOS пока не поддержан для тулчейна CMake (только PlatformIO). "
                              "Выберите тулчейн PlatformIO во вкладке «Настройки генерации» или "
                              "отключите FreeRTOS."));
        return;
    }

    // language=rust уходит по отдельной ветке ДО C-специфичного пути ниже —
    // тот же порядок, что в src/main.cpp (CLI), и то же обоснование
    // (generateProjectFirmware — C-специфичный пайплайн, не должен звать
    // Rust-конфиг вовсе, см. docs/implementations/rust_codegen.md).
    if (parsed.config->language == OutputLanguage::Rust)
    {
        ParseResult<GeneratedProjectFirmwareRust> rust_firmware = generateProjectFirmwareRust(
            *parsed.config);
        if (!rust_firmware.success)
        {
            QMessageBox::critical(this, QString::fromUtf8("Ошибка кодогенерации"),
                                  QString::fromStdString(rust_firmware.error));
            return;
        }

        CodegenWriteResult rust_written = writeGeneratedRustProject(
            *rust_firmware.config, paths_.cargo_skeleton_dir, paths_.rust_hal_snapshot_dir,
            output_dir.toStdString(), *parsed.config, document_.document());
        if (!rust_written.success)
        {
            QMessageBox::critical(this, QString::fromUtf8("Ошибка записи проекта"),
                                  QString::fromStdString(rust_written.error));
            return;
        }

        QMessageBox::information(this, QString::fromUtf8("Проект сгенерирован"),
            QString::fromUtf8("Проект записан в:\n%1\n\nСодержимое между маркерами "
                              "USER CODE BEGIN/END в src/main.rs сохранено.")
                .arg(output_dir));
        statusBar()->showMessage(QString::fromUtf8("Сгенерировано: %1").arg(output_dir), 5000);

        code_preview_screen_->setContentsRust(
            readFileUtf8(output_dir + "/src/main.rs"),
            readFileUtf8(output_dir + "/Cargo.toml"));
        navigator_.setGeneratedOnce(true);
        navigator_.goTo(Screen::CodePreview);
        applyViewModel();
        return;
    }

    // Тулчейн выбирает поле "toolchain" в самом конфиге (project_config.h,
    // ToolchainKind) — тот же switch, что и в src/main.cpp (CLI), иначе GUI
    // молча игнорирует выбор в ProjectManagerScreen и всегда пишет PlatformIO.
    CodegenWriteResult written;
    if (parsed.config->toolchain == ToolchainKind::CMake)
    {
        ParseResult<GeneratedProjectFirmwareCmake> firmware = generateProjectFirmwareCmake(
            *parsed.config, knowledge_);
        if (!firmware.success)
        {
            QMessageBox::critical(this, QString::fromUtf8("Ошибка кодогенерации"),
                                  QString::fromStdString(firmware.error));
            return;
        }

        written = writeGeneratedCmakeProject(
            *firmware.config, paths_.cmake_skeleton_dir, paths_.sdk_snapshot_dir,
            output_dir.toStdString(), *parsed.config, document_.document());
    }
    else
    {
        ParseResult<GeneratedProjectFirmware> firmware = generateProjectFirmware(
            *parsed.config, knowledge_);
        if (!firmware.success)
        {
            QMessageBox::critical(this, QString::fromUtf8("Ошибка кодогенерации"),
                                  QString::fromStdString(firmware.error));
            return;
        }

        const std::string skeleton_dir = parsed.config->freertos.enabled
            ? paths_.skeleton_dir_freertos
            : paths_.skeleton_dir;
        written = writeGeneratedProject(
            *firmware.config, skeleton_dir, output_dir.toStdString(),
            parsed.config->freertos, paths_.freertos_kernel_snapshot_dir);
    }
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
    // Unresolved-поля reverse-import (feedback_reverse_import_unresolved_
    // fields_policy) — красная подсветка дерева по block_index ->
    // (type, instance) через blockIndices.
    QSet<QString> unresolved;
    for (const auto& supported : kSupportedBlocks)
    {
        const QString type = QString::fromUtf8(supported.first);
        for (int instance = 0; instance < supported.second; ++instance)
        {
            for (int block_index : document_.blockIndices(type.toStdString(), instance))
            {
                if (document_.blockHasUnresolvedFields(block_index))
                {
                    unresolved.insert(PeripheralTree::keyFor(type, instance));
                    break;
                }
            }
        }
    }
    tree_->refreshState(enabled, names, unresolved);

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
    navigator_.setReverseImportActive(reverse_import_active_);
    const QString project_name = document_.filePath().empty()
        ? QString::fromUtf8("(без имени)")
        : QFileInfo(QString::fromStdString(document_.filePath())).baseName();
    navigator_.setActiveProjectName(project_name);
    project_manager_screen_->setProjectInfo(
        QString::fromStdString(document_.filePath()), packageSummary());

    // Настройки генерации (CMake-тулчейн, codegen/project/project_config.h):
    // верхнеуровневые поля документа, отсутствие ключа (старый файл проекта)
    // читается как null и подставляется дефолт тем же способом, что и ниже
    // для projectSetting("toolchain") — ProjectDocument::projectSetting не
    // кидает исключение на отсутствующий ключ, см. её комментарий.
    const nlohmann::json language_setting = document_.projectSetting("language");
    const bool is_rust = language_setting.is_string() && language_setting.get<std::string>() == "rust";

    // "rust" — синтетическое значение пункта комбобокса (ProjectManagerScreen),
    // не значение самого поля JSON "toolchain" (там для Rust лежит "cargo"
    // или поле вовсе отсутствует, см. project_config.cpp) — см. комментарий
    // над toolchain_combo_ в project_manager_screen.cpp.
    const nlohmann::json toolchain_setting = document_.projectSetting("toolchain");
    // Rust использует ОТДЕЛЬНУЮ пару полей (rust_hal_source_mode/
    // rust_hal_source_local_path, project_config.h) — общий виджет
    // hal_source_section_ переиспользуется для обоих языков, но пишет и
    // читает разные ключи документа в зависимости от того, что выбрано.
    const nlohmann::json hal_source_mode_setting =
        document_.projectSetting(is_rust ? "rust_hal_source_mode" : "hal_source_mode");
    const nlohmann::json hal_source_local_path_setting =
        document_.projectSetting(is_rust ? "rust_hal_source_local_path" : "hal_source_local_path");
    const nlohmann::json compiler_path_setting = document_.projectSetting("compiler_path");
    const nlohmann::json per_type_files_setting = document_.projectSetting("per_type_files");
    project_manager_screen_->setGenerationSettings(
        is_rust
            ? QString::fromUtf8("rust")
            : (toolchain_setting.is_string()
                ? QString::fromStdString(toolchain_setting.get<std::string>())
                : QString::fromUtf8("platformio")),
        hal_source_mode_setting.is_string()
            ? QString::fromStdString(hal_source_mode_setting.get<std::string>())
            : QString::fromUtf8("bundled"),
        hal_source_local_path_setting.is_string()
            ? QString::fromStdString(hal_source_local_path_setting.get<std::string>())
            : QString(),
        compiler_path_setting.is_string()
            ? QString::fromStdString(compiler_path_setting.get<std::string>())
            : QString(),
        per_type_files_setting.is_boolean() && per_type_files_setting.get<bool>());

    // FreeRTOS (parser/freertos/freertos_parser.h) переехал из
    // ProjectManagerScreen в PropertiesPanel (дерево периферии, группа
    // "Middleware and Software Packs") — отдельного push-вызова отсюда
    // больше не требуется: buildFreertosForm читает document_->projectSetting
    // ("freertos") сам внутри properties_panel_->refresh(), тем же путём,
    // что и RTC/CRC/... читают свои поля через document_->field(...).

    // Тактирование (codegen/project/project_config.h, ClockConfig) — тот же
    // приём, что и FreeRTOS выше: отсутствующий ключ "clock" читается как
    // JSON null, дефолты подставляет сам экран (ClockConfigScreen::
    // setClockSettings). Верхнеуровневая вкладка, не часть Project Manager.
    clock_config_screen_->setClockSettings(document_.projectSetting("clock"));

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
    // Короткая подпись текущей функции вывода — как в STM32CubeMX (PackageView,
    // setPinStates). Заполняется только там, где вывод занят РОВНО одним
    // блоком (found->second.first() ниже) — иначе (свободен/служебный/
    // конфликт) на схеме остаётся старый статический текст из даташита.
    QHash<int, QString> labels;
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
        labels.insert(pin.pin, found->second.first());
    }

    package_view_->setPinStates(states, tooltips, labels);
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
    // Вывод свободен: предложить, чем его занять, а не только сообщить об
    // этом статус-строкой. GPIO применим всегда (любой мультиплексируемый
    // вывод можно объявить обычным входом/выходом); остальные пункты —
    // только для периферии, у которой этот КОНКРЕТНЫЙ вывод жёстко закреплён
    // датащитом (knowledge_base/*_knowledge.h, instance_fixed_pins и
    // аналоги) и которая ещё не добавлена в документ — предлагать "добавить
    // I2C_0", когда I2C_0 уже сконфигурирован (пусть даже другими пинами
    // того же набора), бессмысленно: instance у i2c/uart/spi/timer16 либо
    // включён целиком, либо нет.
    const int gpio_instance = *pin->gpio_instance;
    const int gpio_pin = *pin->gpio_pin;

    struct FixedCandidate { QString type; int instance; };
    std::vector<FixedCandidate> candidates;

    for (const auto& entry : knowledge_.uart.instance_fixed_pins)
    {
        const int instance = entry.first;
        const auto& fixed = entry.second;
        const bool matches =
            (fixed.tx.gpio_instance == gpio_instance && fixed.tx.pin == gpio_pin) ||
            (fixed.rx.gpio_instance == gpio_instance && fixed.rx.pin == gpio_pin) ||
            (fixed.rts.gpio_instance == gpio_instance && fixed.rts.pin == gpio_pin) ||
            (fixed.cts.gpio_instance == gpio_instance && fixed.cts.pin == gpio_pin);
        if (matches && document_.blockIndices("uart", instance).empty())
        {
            candidates.push_back({"uart", instance});
        }
    }

    for (const auto& entry : knowledge_.i2c.instance_fixed_pins)
    {
        const int instance = entry.first;
        const bool matches = std::any_of(entry.second.begin(), entry.second.end(),
            [&](const I2cKnowledge::FixedPin& fp)
            { return fp.gpio_instance == gpio_instance && fp.pin == gpio_pin; });
        if (matches && document_.blockIndices("i2c", instance).empty())
        {
            candidates.push_back({"i2c", instance});
        }
    }

    // SPI: только SCK/MOSI/MISO/SS_IN (instance_fixed_pins) — эти четыре
    // резервируются при включении instance независимо от режима. Выводы
    // ручного выбора ведомых (instance_chip_select_pin) сюда намеренно не
    // включены: конкретный CS-вывод определяется полем "chip_select",
    // которое блок по умолчанию (ProjectDocument::defaultBlock) всегда
    // ставит в "cs0" — добавление блока по клику на, например, cs3 создало
    // бы блок, который этот самый вывод не занимает, то есть меню солгало
    // бы о том, что произойдёт. Выбор конкретного CS остаётся за панелью
    // свойств.
    for (const auto& entry : knowledge_.spi.instance_fixed_pins)
    {
        const int instance = entry.first;
        const bool matches = std::any_of(entry.second.begin(), entry.second.end(),
            [&](const SpiKnowledge::FixedPin& fp)
            { return fp.gpio_instance == gpio_instance && fp.pin == gpio_pin; });
        if (matches && document_.blockIndices("spi", instance).empty())
        {
            candidates.push_back({"spi", instance});
        }
    }

    for (const auto& entry : knowledge_.timer16.instance_fixed_pwm_pin)
    {
        const int instance = entry.first;
        const auto& fixed = entry.second;
        if (fixed.gpio_instance == gpio_instance && fixed.pin == gpio_pin
            && document_.blockIndices("timer16", instance).empty())
        {
            candidates.push_back({"timer16", instance});
        }
    }

    // Timer32 намеренно исключён: его фиксированные выводы закреплены не за
    // instance целиком, а за парой (instance, channel_index)
    // (Timer32Knowledge::instance_channel_fixed_pin), а канал — это часть
    // конфигурации канала, которую ProjectDocument::defaultBlock для типа
    // "timer32" не заполняет (в отличие от gpio/uart/timer16/i2c/spi). Кроме
    // того, "timer32" ещё не входит в kSupportedBlocks этого файла — дерево
    // блоков его тоже пока не показывает как включаемый. Добавление пункта
    // меню создавало бы блок, который построением не проходит
    // schema-валидацию без дополнительной ручной настройки в панели свойств.

    QMenu menu(this);
    QAction* gpio_action = menu.addAction(QString::fromUtf8("Настроить как GPIO (вход/выход)"));

    std::vector<QAction*> candidate_actions;
    candidate_actions.reserve(candidates.size());
    for (const FixedCandidate& candidate : candidates)
    {
        candidate_actions.push_back(menu.addAction(
            QString::fromUtf8("Добавить %1").arg(defaultTitle(candidate.type, candidate.instance))));
    }

    QAction* chosen = menu.exec(QCursor::pos());
    if (chosen == nullptr)
    {
        return;
    }

    if (chosen == gpio_action)
    {
        // Найти существующий GPIO-блок этого instance (первую из возможных
        // нескольких групп — см. gui/properties_panel.cpp, buildGpioForm) и
        // дописать в него этот пин, если его там ещё нет; иначе создать
        // блок со значениями по умолчанию и сразу подставить именно этот
        // пин вместо дефолтного [0] (ProjectDocument::defaultBlock).
        const std::vector<int> indices = document_.blockIndices("gpio", gpio_instance);
        int block_index;
        if (indices.empty())
        {
            block_index = document_.addBlock("gpio", gpio_instance);
            document_.setField(block_index, "pins", nlohmann::json::array({gpio_pin}));
        }
        else
        {
            block_index = indices.front();
            nlohmann::json pins = document_.field(block_index, "pins");
            if (!pins.is_array())
            {
                pins = nlohmann::json::array();
            }
            const bool already_present = std::any_of(pins.begin(), pins.end(),
                [&](const nlohmann::json& item)
                { return item.is_number_integer() && item.get<int>() == gpio_pin; });
            if (!already_present)
            {
                pins.push_back(gpio_pin);
                document_.setField(block_index, "pins", pins);
            }
        }
        tree_->selectPeripheral("gpio", gpio_instance);
        properties_->showPeripheral("gpio", gpio_instance);
        refreshEverything();
        return;
    }

    for (size_t i = 0; i < candidate_actions.size(); ++i)
    {
        if (chosen != candidate_actions[i])
        {
            continue;
        }
        const FixedCandidate& candidate = candidates[i];
        document_.addBlock(candidate.type.toStdString(), candidate.instance);
        tree_->selectPeripheral(candidate.type, candidate.instance);
        properties_->showPeripheral(candidate.type, candidate.instance);
        refreshEverything();
        return;
    }
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
        case 1: navigator_.goTo(Screen::ClockConfig); break;
        case 2: navigator_.goTo(Screen::ProjectManager); break;
        case 3: navigator_.goTo(Screen::CodePreview); break;
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
    reverse_import_active_ = false;  // см. openPath — тот же случай, новый проект с нуля
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
