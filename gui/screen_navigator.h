//
// Слой навигации по экранам верхнего уровня (Главная / Новый проект /
// Pinout&Config / Project Manager / Просмотр кода) — см. MAKET_DESIGN_TZ.md,
// раздел 1, «Вариант 4» (спроектирован через /design-an-interface и утверждён
// пользователем).
//
// Концепция — калька с паттерна самого макета Claude Design (dc-файл держит
// один state{} и одну renderVals(), которая на каждое изменение пересчитывает
// ВСЮ производную UI-модель разом). Здесь то же самое в типизированном виде:
// NavState — что произошло (на каком экране мы, была ли уже генерация,
// сколько ошибок), NavViewModel — что из этого следует для виджетов (какая
// страница видна, включена ли кнопка «Сгенерировать», какая вкладка активна).
// MainWindow — единственный потребитель и единственное место, где
// NavViewModel накладывается на реальные QWidget (см. MainWindow::applyViewModel).
//
// Почему обычный класс, а не QObject с сигналами. Единственный, кто дёргает
// мутаторы (goTo/setDiagnosticErrors/...), — сам MainWindow, он же слушатель
// результата: раунд-трип через сигнал/слот здесь ничего бы не развязывал,
// только добавил косвенность. Это и делает класс тривиально тестируемым
// GoogleTest-ом без запуска Qt-рантайма (в отличие от PeripheralTree/
// PackageView и прочих QWidget-панелей).
//
#ifndef MIK32_CONFIGURATOR_SCREEN_NAVIGATOR_H
#define MIK32_CONFIGURATOR_SCREEN_NAVIGATOR_H

#include <QString>

// Порядок значений — это и есть порядок страниц в QStackedWidget
// (MainWindow::screens_stack_), см. NavViewModel::stackIndex.
enum class Screen
{
    Home,
    NewProject,
    Config,
    ProjectManager,
    CodePreview
};

struct NavState
{
    Screen screen = Screen::Home;

    // Гейт вкладки «Просмотр кода» (MAKET_DESIGN_TZ.md, §1): до первой
    // успешной генерации в этой сессии показывать сгенерированный код
    // неоткуда — вкладка неактивна, а не просто пуста.
    bool hasGeneratedOnce = false;

    // Гейт кнопки «Сгенерировать». Источник истины — DiagnosticsPanel::errorCount(),
    // навигатор его не пересчитывает, только хранит последнее значение.
    int diagnosticErrors = 0;

    // Импортированный (не наш собственный, см. MainWindow::onImportProject/
    // reverse_import.h) проект — для него "Сгенерировать код" не полная
    // кодогенерация, а точечное применение правок обратно в чужие файлы
    // (MainWindow::applyReverseImportChanges). Ошибки диагностики здесь —
    // это конфликты в САМОМ импортированном проекте (например, один физический
    // вывод одновременно занят GPIO и Timer32-каналом в исходнике) — они не
    // мешают точечному патчу конкретного изменённого поля и не должны
    // блокировать кнопку, как это оправданно происходит для полной
    // кодогенерации своего проекта (ТЗ §3.1.6.3).
    bool reverseImportActive = false;

    // Для breadcrumb. Пусто, пока файл не открыт/не создан.
    QString activeProjectName;
};

struct NavViewModel
{
    int stackIndex = 0;
    bool topBarVisible = false;
    bool tabStripVisible = false;
    bool generateButtonVisible = false;
    bool generateButtonEnabled = false;
    bool codeTabEnabled = false;

    // Индекс во вкладочной строке (0=Pinout&Config, 1=Project Manager,
    // 2=Просмотр кода); -1, когда вкладочная строка скрыта.
    int activeTabIndex = -1;

    QString breadcrumbText;
};

class ScreenNavigator
{
public:
    explicit ScreenNavigator(NavState initial = {});

    const NavState& state() const { return state_; }

    void goTo(Screen screen);
    void setDiagnosticErrors(int count);
    void setReverseImportActive(bool value);
    void setGeneratedOnce(bool value);
    void setActiveProjectName(const QString& name);

    // Pure-функция от state() — аналог renderVals() макета. Ничего не меняет,
    // можно звать сколько угодно раз подряд.
    NavViewModel viewModel() const;

private:
    NavState state_;
};

#endif //MIK32_CONFIGURATOR_SCREEN_NAVIGATOR_H
