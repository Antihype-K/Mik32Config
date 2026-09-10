//
// Вкладка «Project Manager» — есть в макете Claude Design, но отсутствует в
// userflow.drawio (там параметры проекта вводятся один раз при создании,
// см. new_project_screen.h). Оставлена как ДОПОЛНИТЕЛЬНЫЙ экран поверх
// официальной схемы (решение пользователя, MAKET_DESIGN_TZ.md).
//
// Макет предлагал здесь ещё и вкладку «Настройки генерации» с тремя
// чекбоксами (сохранять user code / парные .c+.h на периферию / бэкап перед
// регенерацией). На момент первой версии этого экрана ни один из них не
// соответствовал реальному кодогену, поэтому вкладка была заменена честной
// read-only справкой об инвариантах (см. историю файла).
//
// С появлением CMake-тулчейна (codegen/project/project_config.h:
// ToolchainKind/HalSourceMode/per_type_files — расширение, сделанное
// параллельно этому экрану) часть тех переключателей стала РЕАЛЬНОЙ:
// выбор тулчейна, источник HAL (bundled/сеть/локальный путь), путь к
// компилятору и "по файлу на периферию" — теперь читаются и пишутся в
// document JSON (верхнеуровневые поля "toolchain"/"hal_source_mode"/
// "hal_source_local_path"/"compiler_path"/"per_type_files", см.
// ProjectDocument::projectSetting/setProjectSetting). Остаются заведомыми
// заглушками только "бэкап" и "удаление устаревших файлов" — за ними до сих
// пор нет никакой реализации в кодогене, поэтому их чекбоксы видны, но
// задизейблены: строить рабочий с виду переключатель для несуществующего
// поведения было бы враньём пользователю интерфейса (тот же принцип, что
// раньше применялся ко всей вкладке целиком). "Сохранять USER CODE" —
// не настройка вообще, а безусловный инвариант кодогена
// (codegen/project/project_codegen.cpp, mergeUserCodeSections), поэтому его
// чекбокс тоже задизейблен, но всегда включён — это не заглушка, а честное
// отображение поведения, которое нельзя выключить.
//
#ifndef MIK32_CONFIGURATOR_PROJECT_MANAGER_SCREEN_H
#define MIK32_CONFIGURATOR_PROJECT_MANAGER_SCREEN_H

#include <QWidget>
#include <QString>
#include "json.hpp"

class ProjectDocument;
class FreeRtosSettingsWidget;

class QLabel;
class QComboBox;
class QButtonGroup;
class QRadioButton;
class QLineEdit;
class QPushButton;
class QCheckBox;
class QWidget;

class ProjectManagerScreen : public QWidget
{
    Q_OBJECT

public:
    explicit ProjectManagerScreen(QWidget* parent = nullptr);

    // Документ нужен только для ЗАПИСИ (виджет пишет изменения напрямую
    // через ProjectDocument::setProjectSetting, тот же приём, что и
    // PropertiesPanel::setContext/document_->setField). ЧТЕНИЕ начального
    // состояния идёт отдельным путём — setGenerationSettings ниже, тем же
    // стилем, что уже используется в setProjectInfo (MainWindow сам решает,
    // когда и что читать из документа, экран остаётся тонким).
    void setContext(ProjectDocument* document);

    // file_path — пусто, если проект ещё не сохранён ни разу.
    void setProjectInfo(const QString& file_path, const QString& package_summary);

    // Начальное/актуальное состояние пяти настроек генерации верхнего уровня
    // документа. toolchain — "platformio"/"cmake"; hal_source_mode —
    // "bundled"/"network"/"local_path". Вызывается MainWindow из того же
    // цикла обновления, что и setProjectInfo (refreshEverything), поэтому
    // виджеты никогда не расходятся с документом даже при отмене/undo.
    void setGenerationSettings(const QString& toolchain,
                                const QString& hal_source_mode,
                                const QString& hal_source_local_path,
                                const QString& compiler_path,
                                bool per_type_files);

    // Текущее содержимое document["freertos"] (может быть JSON null, если
    // ключа ещё нет — см. FreeRtosSettingsWidget::setValue). Тот же вызов из
    // MainWindow::refreshEverything(), что и setGenerationSettings выше.
    void setFreeRtosSettings(const nlohmann::json& freertos_settings);

signals:
    // Тот же сигнал и тот же маршрут оповещения, что и
    // PropertiesPanel::documentChanged -> MainWindow::onDocumentChanged:
    // содержимое документа изменилось, окно обязано перевалидировать
    // конфигурацию (хотя ни одно из этих пяти полей само по себе не влияет
    // на диагностику — перевалидация здесь дешева и держит цикл обновления
    // единым, см. main_window.h, комментарий про "цикл обновления ровно один").
    void documentChanged();

private:
    void updateToolchainDependentVisibility();
    void updateHalLocalPathEnabled();

    ProjectDocument* document_ = nullptr;

    // Подавляет запись в документ и emit documentChanged() во время
    // программной установки значений (setGenerationSettings): без этого
    // combo->setCurrentIndex/radio->setChecked, вызванные MainWindow при
    // каждом refreshEverything(), сами становились бы источником изменений
    // документа и заново дёргали бы refreshEverything() — тот же класс
    // проблемы, что и с рекурсивным обновлением дерева.
    bool syncing_ = false;

    QLabel* file_name_value_ = nullptr;
    QLabel* location_value_ = nullptr;
    QLabel* package_value_ = nullptr;

    // --- Настройки генерации ----------------------------------------------
    QComboBox* toolchain_combo_ = nullptr;

    QWidget* hal_source_section_ = nullptr;
    QButtonGroup* hal_source_group_ = nullptr;
    QRadioButton* hal_bundled_radio_ = nullptr;
    QRadioButton* hal_network_radio_ = nullptr;
    QRadioButton* hal_local_path_radio_ = nullptr;
    QLineEdit* hal_local_path_edit_ = nullptr;
    QPushButton* hal_local_path_browse_ = nullptr;

    QWidget* compiler_path_section_ = nullptr;
    QLineEdit* compiler_path_edit_ = nullptr;

    QWidget* per_type_files_section_ = nullptr;
    QCheckBox* per_type_files_checkbox_ = nullptr;

    QCheckBox* keep_user_code_checkbox_ = nullptr;
    QCheckBox* backup_checkbox_ = nullptr;
    QCheckBox* delete_stale_checkbox_ = nullptr;

    // --- Вкладка «FreeRTOS» --------------------------------------------------
    FreeRtosSettingsWidget* freertos_widget_ = nullptr;
};

#endif //MIK32_CONFIGURATOR_PROJECT_MANAGER_SCREEN_H
