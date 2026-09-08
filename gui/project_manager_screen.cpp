#include "project_manager_screen.h"

#include <QFileInfo>
#include <QFormLayout>
#include <QLabel>
#include <QVBoxLayout>

ProjectManagerScreen::ProjectManagerScreen(QWidget* parent)
    : QWidget(parent)
{
    QVBoxLayout* root = new QVBoxLayout(this);
    root->setContentsMargins(40, 26, 40, 26);

    QWidget* card = new QWidget();
    card->setMaximumWidth(700);
    QFormLayout* form = new QFormLayout(card);

    file_name_value_ = new QLabel();
    form->addRow(QString::fromUtf8("Файл проекта"), file_name_value_);

    location_value_ = new QLabel();
    location_value_->setWordWrap(true);
    form->addRow(QString::fromUtf8("Расположение"), location_value_);

    QLabel* toolchain_label = new QLabel(QString::fromUtf8("PlatformIO — framework-mik32v2-sdk"));
    form->addRow(QString::fromUtf8("Тулчейн"), toolchain_label);

    package_value_ = new QLabel();
    form->addRow(QString::fromUtf8("Плата"), package_value_);

    root->addWidget(card);

    // См. комментарий в .h: макетные чекбоксы кодогенерации не соответствуют
    // реальному поведению — вместо них честная справка об инвариантах.
    QLabel* note = new QLabel(QString::fromUtf8(
        "Содержимое между USER CODE BEGIN/END в src/main.c сохраняется "
        "автоматически при каждой генерации и не настраивается.\n"
        "mik32_init.c/.h — один общий файл на весь проект, не по одному на "
        "периферию.\n"
        "Демо-логика в while(1) переключается пунктом меню "
        "«Проект → Генерировать демо-логику в while(1)»."));
    note->setWordWrap(true);
    note->setStyleSheet("color: gray;");
    root->addSpacing(18);
    root->addWidget(note);
    root->addStretch(1);
}

void ProjectManagerScreen::setProjectInfo(const QString& file_path, const QString& package_summary)
{
    if (file_path.isEmpty())
    {
        file_name_value_->setText(QString::fromUtf8("(не сохранён)"));
        location_value_->setText(QString::fromUtf8("—"));
    }
    else
    {
        const QFileInfo info(file_path);
        file_name_value_->setText(info.fileName());
        location_value_->setText(info.absolutePath());
    }
    package_value_->setText(package_summary);
}
