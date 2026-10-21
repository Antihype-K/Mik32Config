#include "home_screen.h"

#include <QAction>
#include <QFileInfo>
#include <QFont>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QPushButton>
#include <QVBoxLayout>

HomeScreen::HomeScreen(QWidget* parent)
    : QWidget(parent)
{
    QVBoxLayout* root = new QVBoxLayout(this);
    root->setContentsMargins(48, 48, 48, 48);
    root->setSpacing(18);

    QLabel* title = new QLabel(QString::fromUtf8("МИК32 Конфигуратор периферии"));
    QFont title_font = title->font();
    title_font.setPointSize(title_font.pointSize() + 6);
    title_font.setBold(true);
    title->setFont(title_font);
    root->addWidget(title);

    QHBoxLayout* actions = new QHBoxLayout();
    QPushButton* new_button = new QPushButton(
        QString::fromUtf8("Создать проект под МИК32 Амур"));
    QPushButton* open_button = new QPushButton(QString::fromUtf8("Открыть файл..."));
    actions->addWidget(new_button);
    actions->addWidget(open_button);
    actions->addStretch(1);
    root->addLayout(actions);
    connect(new_button, &QPushButton::clicked, this, &HomeScreen::newProjectRequested);
    connect(open_button, &QPushButton::clicked, this, &HomeScreen::openProjectRequested);

    root->addWidget(new QLabel(QString::fromUtf8("Недавние проекты")));

    recent_list_ = new QListWidget();
    recent_list_->setAlternatingRowColors(true);
    recent_list_->setContextMenuPolicy(Qt::CustomContextMenu);
    recent_list_->installEventFilter(this);
    connect(recent_list_, &QListWidget::itemActivated, this, [this](QListWidgetItem* item) {
        emit recentProjectChosen(item->data(Qt::UserRole).toString());
    });
    connect(recent_list_, &QListWidget::customContextMenuRequested, this,
            [this](const QPoint& pos) {
        QListWidgetItem* item = recent_list_->itemAt(pos);
        if (!item)
        {
            return;
        }
        QMenu menu(recent_list_);
        QAction* remove_action = menu.addAction(QString::fromUtf8("Убрать из списка"));
        connect(remove_action, &QAction::triggered, this, [this, item]() {
            emit recentProjectRemoveRequested(item->data(Qt::UserRole).toString());
        });
        menu.exec(recent_list_->mapToGlobal(pos));
    });
    root->addWidget(recent_list_, 1);

    // userflow.drawio, n04: «ссылки на документацию» — минимальная
    // текстовая справка, без встроенного просмотрщика (вне объёма этой
    // задачи, см. MAKET_DESIGN_TZ.md §5).
    QLabel* docs_note = new QLabel(QString::fromUtf8(
        "Документация: «МИК32 конфигуратор - Архитектура.md» в корне репозитория."));
    docs_note->setStyleSheet("color: gray;");
    root->addWidget(docs_note);
}

bool HomeScreen::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == recent_list_ && event->type() == QEvent::KeyPress)
    {
        auto* key_event = static_cast<QKeyEvent*>(event);
        if (key_event->key() == Qt::Key_Delete)
        {
            QListWidgetItem* item = recent_list_->currentItem();
            if (item)
            {
                emit recentProjectRemoveRequested(item->data(Qt::UserRole).toString());
            }
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

void HomeScreen::setRecentProjects(const QStringList& paths)
{
    recent_list_->clear();
    for (const QString& path : paths)
    {
        const QFileInfo info(path);
        QListWidgetItem* item = new QListWidgetItem(info.fileName());
        item->setToolTip(path);
        item->setData(Qt::UserRole, path);
        recent_list_->addItem(item);
    }
}
