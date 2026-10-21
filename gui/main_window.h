//
// Главное окно — единственное место, где сходятся документ, база знаний и
// четыре панели (ТЗ §3.1.9 п.2: дерево слева, схема корпуса в центре,
// свойства справа, диагностика снизу).
//
// Разделение ответственности выбрано так, чтобы панели оставались тупыми, а
// решения принимались в одном месте:
//   - PeripheralTree знает, какие блоки бывают у МК, но не знает, какие
//     включены;
//   - PackageView знает геометрию корпуса, но не знает, кто занял вывод;
//   - PropertiesPanel правит поля блока, но не знает, к чему это приводит;
//   - DiagnosticsPanel показывает список, но не знает, откуда он взялся.
// Всё «кто чем занят», «валидно ли это» и «что делать по клику» — здесь.
//
// Цикл обновления ровно один и намеренно не оптимизируется: любое изменение
// документа приводит к полной перевалидации и полной перекраске схемы
// (refreshEverything). Конфигурация — это десятки блоков, а не десятки тысяч;
// инкрементальное обновление здесь стоило бы дороже, чем экономило, и было бы
// источником рассинхрона панелей.
//
// Поверх этих четырёх панелей (которые остаются ЦЕЛИКОМ как есть) теперь
// живёт слой экранов верхнего уровня — Home / NewProject / Config /
// ProjectManager / CodePreview, см. gui/screen_navigator.h и
// MAKET_DESIGN_TZ.md. Четыре панели — это содержимое ровно ОДНОГО из этих
// экранов (Config); остальные экраны — отдельные QWidget, MainWindow лишь
// переключает между ними через QStackedWidget и накладывает NavViewModel на
// верхнюю строку/вкладки (applyViewModel).
//
#ifndef MIK32_CONFIGURATOR_MAIN_WINDOW_H
#define MIK32_CONFIGURATOR_MAIN_WINDOW_H

#include <QMainWindow>
#include <QString>
#include <QStringList>

#include "project_document.h"
#include "project_codegen.h"
#include "package_knowledge.h"
#include "peripheral_tree.h"
#include "screen_navigator.h"
#include "new_project_screen.h"
#include "reverse_import.h"

class PackageView;
class PropertiesPanel;
class DiagnosticsPanel;
class HomeScreen;
class NewProjectScreen;
class ProjectManagerScreen;
class ClockConfigScreen;
class CodePreviewScreen;
class QAction;
class QLabel;
class QPushButton;
class QScrollArea;
class QStackedWidget;
class QTabBar;
class QWidget;

// Пути к ресурсам конфигуратора приходят снаружи (их ищет gui_main.cpp),
// чтобы окно не зависело от того, из какого каталога запущено приложение.
struct GuiPaths
{
    std::string skeleton_dir;         // templates/pio_skeleton
    std::string generated_root_dir;   // generated_projects
    std::string cmake_skeleton_dir;   // templates/cmake_skeleton
    std::string sdk_snapshot_dir;     // templates/mik32_sdk_snapshot
    std::string skeleton_dir_freertos;        // templates/pio_skeleton_freertos
    std::string freertos_kernel_snapshot_dir; // templates/freertos_kernel_snapshot
    std::string cargo_skeleton_dir;      // templates/cargo_skeleton
    std::string rust_hal_snapshot_dir;   // templates/mik32_rust_hal_snapshot
};

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(const ProjectKnowledge& knowledge,
               const PackageKnowledge& package,
               const GuiPaths& paths,
               QWidget* parent = nullptr);

    // Открыть файл, переданный аргументом командной строки. Возвращает
    // false, если файл не открылся (документ в этом случае не меняется).
    bool openPath(const QString& path);

protected:
    // Несохранённая конфигурация не должна исчезать молча (ТЗ §3.2 п.1 — о
    // целостности файла проекта; здесь та же мысль про несохранённые правки).
    void closeEvent(QCloseEvent* event) override;

private slots:
    void onNew();
    bool onOpen();
    void onImportProject();
    bool onSave();
    bool onSaveAs();
    void onGenerate();
    void onAbout();

    void onSelectionChanged(const PeripheralRef& ref);
    void onEnableRequested(const PeripheralRef& ref);
    void onDisableRequested(const PeripheralRef& ref);
    void onRenameRequested(const PeripheralRef& ref);
    void onResetRequested(const PeripheralRef& ref);
    void onCopySettingsRequested(const PeripheralRef& ref);

    void onDocumentChanged();
    void onPinClicked(int pin_number);
    void onJumpToBlock(int block_index, const QString& field_name);

    // --- Навигация по экранам (MAKET_DESIGN_TZ.md, раздел 1 и "New Project /
    // ChipSelect") -------------------------------------------------------
    void onGoHome();
    void onTabStripChanged(int tab_index);
    void onHomeNewProjectRequested();
    void onHomeOpenProjectRequested();
    void onHomeRecentProjectChosen(const QString& path);
    void onHomeRecentProjectRemoveRequested(const QString& path);
    void onNewProjectCreateRequested(const NewProjectParams& params);

private:
    void buildMenus();
    void buildLayout();
    void buildChrome(QWidget* central);

    // Аналог MainWindow::applyViewModel из проектирования ScreenNavigator:
    // единственное место, где NavViewModel накладывается на реальные
    // виджеты (стек страниц, верхняя строка, вкладочная строка).
    void applyViewModel();

    // Список последних проектов — персистентность через QSettings (её в
    // приложении раньше не было вообще, см. MAKET_DESIGN_TZ.md §5).
    void loadRecentProjects();
    void pushRecentProject(const QString& path);

    QString packageSummary() const;

    // Полный цикл обновления: перевалидировать документ, обновить дерево,
    // перекрасить схему, обновить заголовок окна и строку состояния.
    void refreshEverything();

    // Раскраска схемы корпуса: считает, кто претендует на каждый вывод.
    void updatePackageColors();

    void updateWindowTitle();

    // Возвращает false, если пользователь отменил операцию: вызывающий обязан
    // тогда прервать и своё действие (открытие файла, выход).
    bool confirmDiscardChanges();

    // Ветка onGenerate() для проектов, открытых через onImportProject: вместо
    // полной кодогенерации (шаблон + USER CODE) точечно переписывает в чужих
    // .c/.cpp РОВНО те строки, чьи поля пользователь поменял в PropertiesPanel
    // — parser/reverse_import/reverse_import_apply.h. См. её же за тем, почему
    // после применения документ переимпортируется заново с диска.
    void applyReverseImportChanges();

    // Общая часть onImportProject/openPath: каталог в "недавних" может быть
    // импортированным C-проектом, а не project.json — его открытие означает
    // повторный reverse-import с диска (project.json туда никогда не пишется).
    bool importFromDirectory(const QString& dir);

    // Человекочитаемое имя блока для сообщений и подсказок: пользовательское
    // имя, если задано, иначе штатное "USART_0".
    QString blockTitle(const QString& type, int instance) const;
    static QString defaultTitle(const QString& type, int instance);
    void showProjectGeneratedDialog(const QString& output_dir, const QString& main_file_name);

    ProjectKnowledge knowledge_;
    PackageKnowledge package_;
    GuiPaths paths_;
    ProjectDocument document_;

    // Заполняются onImportProject, если текущий документ пришёл не из
    // project.json, а из чужой прошивки на диске (см. reverse_import.h).
    // reverse_import_active_ — единственный переключатель ветки в onGenerate();
    // imported_project_.document["peripherals"] служит "снимком ДО правок"
    // для reverse_import_apply::applyImportedChanges (само окно правки не
    // отслеживает построчно — только целиком до/после).
    bool reverse_import_active_ = false;
    QString reverse_import_root_;
    reverse_import::ImportedProject imported_project_;

    PeripheralTree* tree_ = nullptr;
    PackageView* package_view_ = nullptr;
    PropertiesPanel* properties_ = nullptr;
    QScrollArea* properties_scroll_ = nullptr;
    DiagnosticsPanel* diagnostics_ = nullptr;

    // --- Навигация по экранам ------------------------------------------
    ScreenNavigator navigator_;
    QStackedWidget* screens_stack_ = nullptr;   // порядок страниц = Screen enum
    QWidget* top_bar_ = nullptr;
    QLabel* breadcrumb_label_ = nullptr;
    QPushButton* generate_button_ = nullptr;
    QTabBar* tab_strip_ = nullptr;               // 0=Config, 1=ClockConfig, 2=ProjectManager, 3=CodePreview

    HomeScreen* home_screen_ = nullptr;
    NewProjectScreen* new_project_screen_ = nullptr;
    ClockConfigScreen* clock_config_screen_ = nullptr;
    ProjectManagerScreen* project_manager_screen_ = nullptr;
    CodePreviewScreen* code_preview_screen_ = nullptr;

    QStringList recent_projects_;

    // Буфер «копировать настройки» из контекстного меню дерева
    // (ТЗ §3.1.9 п.3). Хранится блок целиком, кроме instance: копируются
    // именно НАСТРОЙКИ, а номер экземпляра остаётся у приёмника.
    nlohmann::json copy_buffer_;
    QString copy_buffer_type_;
};

#endif //MIK32_CONFIGURATOR_MAIN_WINDOW_H
