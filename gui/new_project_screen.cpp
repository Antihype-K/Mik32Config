#include "new_project_screen.h"

#include <QComboBox>
#include <QDir>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QStandardItemModel>
#include <QVBoxLayout>

NewProjectScreen::NewProjectScreen(QWidget* parent)
    : QWidget(parent)
{
    QVBoxLayout* root = new QVBoxLayout(this);
    root->setContentsMargins(48, 48, 48, 48);
    root->addStretch(1);

    QWidget* card = new QWidget();
    card->setMaximumWidth(460);
    QFormLayout* form = new QFormLayout(card);

    name_edit_ = new QLineEdit(QString::fromUtf8("мой_проект"));
    form->addRow(QString::fromUtf8("Наименование"), name_edit_);

    location_edit_ = new QLineEdit(QDir::homePath());
    QPushButton* browse_button = new QPushButton(QString::fromUtf8("Обзор..."));
    QHBoxLayout* location_row = new QHBoxLayout();
    location_row->addWidget(location_edit_);
    location_row->addWidget(browse_button);
    form->addRow(QString::fromUtf8("Каталог"), location_row);

    // Версия МК: заглушка (MAKET_DESIGN_TZ.md) — только V2 реально
    // поддержан knowledge_base/HAL-снэпшотом в этом репозитории.
    QComboBox* mcu_combo = new QComboBox();
    mcu_combo->addItem(QString::fromUtf8("МИК32 Амур V2 (QFN64)"));
    mcu_combo->addItem(QString::fromUtf8("МИК32 V0 (пока не поддержан)"));
    auto* mcu_model = qobject_cast<QStandardItemModel*>(mcu_combo->model());
    if (mcu_model != nullptr && mcu_model->item(1) != nullptr)
    {
        mcu_model->item(1)->setEnabled(false);
    }
    mcu_combo->setCurrentIndex(0);
    form->addRow(QString::fromUtf8("Версия МК"), mcu_combo);

    QLabel* toolchain_label = new QLabel(QString::fromUtf8("PlatformIO — framework-mik32v2-sdk"));
    toolchain_label->setStyleSheet("color: gray;");
    form->addRow(QString::fromUtf8("Тулчейн"), toolchain_label);

    QLabel* board_label = new QLabel(QString::fromUtf8("development-board (МИК32 Амур, QFN64)"));
    board_label->setStyleSheet("color: gray;");
    form->addRow(QString::fromUtf8("Отладочная плата"), board_label);

    continue_button_ = new QPushButton(QString::fromUtf8("Продолжить →"));
    form->addRow(QString(), continue_button_);

    QHBoxLayout* center = new QHBoxLayout();
    center->addStretch(1);
    center->addWidget(card);
    center->addStretch(1);
    root->addLayout(center);
    root->addStretch(1);

    connect(browse_button, &QPushButton::clicked, this, &NewProjectScreen::onBrowseClicked);
    connect(continue_button_, &QPushButton::clicked, this, &NewProjectScreen::onContinueClicked);
    connect(name_edit_, &QLineEdit::textChanged, this, &NewProjectScreen::updateContinueEnabled);
    connect(location_edit_, &QLineEdit::textChanged, this, &NewProjectScreen::updateContinueEnabled);
    updateContinueEnabled();
}

void NewProjectScreen::resetForm()
{
    name_edit_->setText(QString::fromUtf8("мой_проект"));
    location_edit_->setText(QDir::homePath());
}

void NewProjectScreen::onBrowseClicked()
{
    const QString chosen = QFileDialog::getExistingDirectory(
        this, QString::fromUtf8("Каталог проекта"), location_edit_->text());
    if (!chosen.isEmpty())
    {
        location_edit_->setText(chosen);
    }
}

void NewProjectScreen::updateContinueEnabled()
{
    continue_button_->setEnabled(
        !name_edit_->text().trimmed().isEmpty() && !location_edit_->text().trimmed().isEmpty());
}

void NewProjectScreen::onContinueClicked()
{
    NewProjectParams params;
    params.name = name_edit_->text().trimmed();
    params.directory = location_edit_->text().trimmed();
    emit createRequested(params);
}
