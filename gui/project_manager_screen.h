//
// Вкладка «Project Manager» — есть в макете Claude Design, но отсутствует в
// userflow.drawio (там параметры проекта вводятся один раз при создании,
// см. new_project_screen.h). Оставлена как ДОПОЛНИТЕЛЬНЫЙ экран поверх
// официальной схемы (решение пользователя, MAKET_DESIGN_TZ.md).
//
// Макет предлагал здесь ещё и вкладку «Настройки генерации» с тремя
// чекбоксами (сохранять user code / парные .c+.h на периферию / бэкап перед
// регенерацией). Ни один из них не соответствует реальному кодогену:
//  - USER CODE BEGIN/END сохраняются ВСЕГДА и безусловно
//    (codegen/project/project_codegen.cpp, mergeUserCodeSections) — это не
//    настройка, а инвариант;
//  - кодоген пишет ОДНУ пару mik32_init.c/.h на весь проект, не по одной на
//    периферию (project_codegen.cpp, GeneratedProjectFirmware) — "парных
//    файлов на периферию" в архитектуре нет;
//  - резервной копии перед регенерацией нет нигде в кодовой базе.
// Строить рабочие с виду переключатели для несуществующего поведения было бы
// враньём пользователю интерфейса, поэтому вкладка не переносится — вместо
// неё этот экран показывает как есть.
//
#ifndef MIK32_CONFIGURATOR_PROJECT_MANAGER_SCREEN_H
#define MIK32_CONFIGURATOR_PROJECT_MANAGER_SCREEN_H

#include <QWidget>
#include <QString>

class QLabel;

class ProjectManagerScreen : public QWidget
{
    Q_OBJECT

public:
    explicit ProjectManagerScreen(QWidget* parent = nullptr);

    // file_path — пусто, если проект ещё не сохранён ни разу.
    void setProjectInfo(const QString& file_path, const QString& package_summary);

private:
    QLabel* file_name_value_ = nullptr;
    QLabel* location_value_ = nullptr;
    QLabel* package_value_ = nullptr;
};

#endif //MIK32_CONFIGURATOR_PROJECT_MANAGER_SCREEN_H
