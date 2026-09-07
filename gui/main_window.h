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
#ifndef MIK32_CONFIGURATOR_MAIN_WINDOW_H
#define MIK32_CONFIGURATOR_MAIN_WINDOW_H

#include <QMainWindow>
#include <QString>

#include "project_document.h"
#include "project_codegen.h"
#include "package_knowledge.h"
#include "peripheral_tree.h"

class PackageView;
class PropertiesPanel;
class DiagnosticsPanel;
class QAction;

// Пути к ресурсам конфигуратора приходят снаружи (их ищет gui_main.cpp),
// чтобы окно не зависело от того, из какого каталога запущено приложение.
struct GuiPaths
{
    std::string skeleton_dir;         // templates/pio_skeleton
    std::string generated_root_dir;   // generated_projects
};

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(const ProjectKnowledge& knowledge,
               const PackageKnowledge& package,
               const GuiPaths& paths,
               QWidget* parent = nullptr);

    // Открыть файл, переданный аргументом командной строки.
    void openPath(const QString& path);

protected:
    // Несохранённая конфигурация не должна исчезать молча (ТЗ §3.2 п.1 — о
    // целостности файла проекта; здесь та же мысль про несохранённые правки).
    void closeEvent(QCloseEvent* event) override;

private slots:
    void onNew();
    bool onOpen();
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
    void onJumpToBlock(int block_index);

private:
    void buildMenus();
    void buildLayout();

    // Полный цикл обновления: перевалидировать документ, обновить дерево,
    // перекрасить схему, обновить заголовок окна и строку состояния.
    void refreshEverything();

    // Раскраска схемы корпуса: считает, кто претендует на каждый вывод.
    void updatePackageColors();

    void updateWindowTitle();

    // Возвращает false, если пользователь отменил операцию: вызывающий обязан
    // тогда прервать и своё действие (открытие файла, выход).
    bool confirmDiscardChanges();

    // Человекочитаемое имя блока для сообщений и подсказок: пользовательское
    // имя, если задано, иначе штатное "USART_0".
    QString blockTitle(const QString& type, int instance) const;
    static QString defaultTitle(const QString& type, int instance);

    ProjectKnowledge knowledge_;
    PackageKnowledge package_;
    GuiPaths paths_;
    ProjectDocument document_;

    PeripheralTree* tree_ = nullptr;
    PackageView* package_view_ = nullptr;
    PropertiesPanel* properties_ = nullptr;
    DiagnosticsPanel* diagnostics_ = nullptr;

    QAction* demo_loops_action_ = nullptr;  // переключатель emit_demo_loops

    // Буфер «копировать настройки» из контекстного меню дерева
    // (ТЗ §3.1.9 п.3). Хранится блок целиком, кроме instance: копируются
    // именно НАСТРОЙКИ, а номер экземпляра остаётся у приёмника.
    nlohmann::json copy_buffer_;
    QString copy_buffer_type_;
};

#endif //MIK32_CONFIGURATOR_MAIN_WINDOW_H
