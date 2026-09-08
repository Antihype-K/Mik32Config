//
// Начальный экран (userflow.drawio, ЭТАП 2 «ВЫБОР ПРОЕКТА», узел n04):
// «новый проект, открыть проект, список последних проектов, ссылки на
// документацию». Экран не знает НИЧЕГО о ProjectDocument/knowledge_base —
// он только сообщает наверх намерение пользователя (какая кнопка нажата,
// какой путь выбран из списка), а MainWindow решает, что с этим делать
// (confirmDiscardChanges, ProjectDocument::load и т.д.) — та же граница
// ответственности, что и у PeripheralTree/PackageView.
//
// Список «последних проектов» подписывается снаружи через setRecentProjects
// (MainWindow читает/пишет его в QSettings — здесь персистентности нет).
//
#ifndef MIK32_CONFIGURATOR_HOME_SCREEN_H
#define MIK32_CONFIGURATOR_HOME_SCREEN_H

#include <QWidget>
#include <QStringList>

class QListWidget;

class HomeScreen : public QWidget
{
    Q_OBJECT

public:
    explicit HomeScreen(QWidget* parent = nullptr);

    // paths — абсолютные пути к файлам *.json, самый недавний первым.
    void setRecentProjects(const QStringList& paths);

signals:
    void newProjectRequested();
    void openProjectRequested();
    void recentProjectChosen(const QString& path);

private:
    QListWidget* recent_list_ = nullptr;
};

#endif //MIK32_CONFIGURATOR_HOME_SCREEN_H
