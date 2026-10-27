//
// Экран «Новый проект» (userflow.drawio, ЭТАП 2, ветвь «создание нового»,
// узел n10: «наименование, каталог, версия МК (V0/V2), инструментальная
// цепочка, отладочная плата»). Отдельного экрана выбора МК (как в макете
// Claude Design) не делаем — по схеме это ОДНА форма, см. MAKET_DESIGN_TZ.md,
// раздел «New Project / ChipSelect».
//
// Версия МК V0/V2 — заглушка (по решению пользователя, MAKET_DESIGN_TZ.md):
// вся knowledge_base/ и HAL-снэпшот в репозитории собраны под MIK32V2
// (1T Rex Board), V0 показан в списке, но недоступен для выбора.
//
#ifndef MIK32_CONFIGURATOR_NEW_PROJECT_SCREEN_H
#define MIK32_CONFIGURATOR_NEW_PROJECT_SCREEN_H

#include <QWidget>
#include <QString>

class QLineEdit;
class QPushButton;
class QComboBox;

struct NewProjectParams
{
    QString name;
    QString directory;
    // Синтетическое значение combobox'а language+toolchain — тот же
    // словарь ("platformio"/"cmake"/"rust"/"arduino"), что и у
    // toolchain_combo_ в project_manager_screen.cpp, расшифровывается в
    // onNewProjectCreateRequested (gui/main_window.cpp) той же логикой.
    QString toolchain = QString::fromUtf8("platformio");
};

class NewProjectScreen : public QWidget
{
    Q_OBJECT

public:
    explicit NewProjectScreen(QWidget* parent = nullptr);

    // Сбрасывает поля формы к значениям по умолчанию — вызывается при
    // каждом переходе на этот экран, чтобы не показать данные предыдущего
    // «нового проекта», если пользователь ушёл на Home и вернулся.
    void resetForm();

signals:
    void createRequested(const NewProjectParams& params);

private slots:
    void onBrowseClicked();
    void onContinueClicked();
    void updateContinueEnabled();

private:
    QLineEdit* name_edit_ = nullptr;
    QLineEdit* location_edit_ = nullptr;
    QComboBox* toolchain_combo_ = nullptr;
    QPushButton* continue_button_ = nullptr;
};

#endif //MIK32_CONFIGURATOR_NEW_PROJECT_SCREEN_H
