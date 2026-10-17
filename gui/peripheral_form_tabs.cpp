#include "peripheral_form_tabs.h"

#include <QVBoxLayout>
#include <QGridLayout>
#include <QTabWidget>
#include <QPushButton>
#include <QLabel>
#include <QFont>
#include <QFrame>

namespace
{
    // Отступ строки-поля против строки-категории (которая начинается от 0) —
    // калька внешнего вида CubeMX (жирный заголовок левее своих полей).
    constexpr int kFieldIndent = 18;

    QWidget* buildEmptyPage(QVBoxLayout** out_layout)
    {
        QWidget* page = new QWidget();
        auto* layout = new QVBoxLayout(page);
        layout->setAlignment(Qt::AlignTop);
        *out_layout = layout;
        return page;
    }
}

PeripheralFormTabs::PeripheralFormTabs(QWidget* parent)
    : QWidget(parent)
{
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);

    // --- Mode ---------------------------------------------------------
    QLabel* mode_title = new QLabel(QString::fromUtf8("<b>Mode</b>"));
    outer->addWidget(mode_title);

    QFrame* mode_frame = new QFrame();
    mode_frame->setFrameShape(QFrame::StyledPanel);
    mode_layout_ = new QVBoxLayout(mode_frame);
    mode_layout_->setAlignment(Qt::AlignTop);
    outer->addWidget(mode_frame);

    // --- Reset Configuration -------------------------------------------
    reset_button_ = new QPushButton(QString::fromUtf8("Reset Configuration"));
    connect(reset_button_, &QPushButton::clicked, this, &PeripheralFormTabs::resetRequested);
    outer->addWidget(reset_button_);

    // --- Configuration (вкладки) ----------------------------------------
    QLabel* config_title = new QLabel(QString::fromUtf8("<b>Configuration</b>"));
    outer->addWidget(config_title);

    tab_widget_ = new QTabWidget();
    outer->addWidget(tab_widget_, 1);

    // Parameter Settings всегда первая и единственная обязательная вкладка.
    parameter_settings_page_ = new QWidget();
    parameter_grid_ = new QGridLayout(parameter_settings_page_);
    parameter_grid_->setAlignment(Qt::AlignTop);
    parameter_grid_->setColumnStretch(1, 1);
    tab_widget_->addTab(parameter_settings_page_, QString::fromUtf8("Parameter Settings"));
}

QVBoxLayout* PeripheralFormTabs::modeLayout()
{
    return mode_layout_;
}

void PeripheralFormTabs::setModeWidget(QWidget* widget)
{
    if (mode_widget_ != nullptr)
    {
        mode_layout_->removeWidget(mode_widget_);
        mode_widget_->setParent(nullptr);
        mode_widget_->deleteLater();
    }
    mode_widget_ = widget;
    if (widget != nullptr)
    {
        mode_layout_->addWidget(widget);
    }
}

void PeripheralFormTabs::addParameterCategory(const QString& category_name)
{
    QLabel* heading = new QLabel(QString("<b>%1</b>").arg(category_name));
    parameter_grid_->addWidget(heading, parameter_row_, 0, 1, 2);
    ++parameter_row_;
}

void PeripheralFormTabs::addParameterField(const QString& label, QWidget* control)
{
    QLabel* field_label = new QLabel(label);
    field_label->setContentsMargins(kFieldIndent, 0, 0, 0);
    parameter_grid_->addWidget(field_label, parameter_row_, 0);
    parameter_grid_->addWidget(control, parameter_row_, 1);
    ++parameter_row_;
}

QVBoxLayout* PeripheralFormTabs::addNvicSettingsTab()
{
    if (nvic_page_ == nullptr)
    {
        nvic_page_ = buildEmptyPage(&nvic_layout_);
        tab_widget_->addTab(nvic_page_, QString::fromUtf8("NVIC Settings"));
    }
    return nvic_layout_;
}

QVBoxLayout* PeripheralFormTabs::addDmaSettingsTab()
{
    if (dma_page_ == nullptr)
    {
        dma_page_ = buildEmptyPage(&dma_layout_);
        tab_widget_->addTab(dma_page_, QString::fromUtf8("DMA Settings"));
    }
    return dma_layout_;
}

QVBoxLayout* PeripheralFormTabs::addGpioSettingsTab()
{
    if (gpio_page_ == nullptr)
    {
        gpio_page_ = buildEmptyPage(&gpio_layout_);
        tab_widget_->addTab(gpio_page_, QString::fromUtf8("GPIO Settings"));
    }
    return gpio_layout_;
}

void PeripheralFormTabs::resetContents()
{
    setModeWidget(nullptr);

    // Parameter Settings: удалить все виджеты из сетки и обнулить счётчик строк.
    QLayoutItem* item = nullptr;
    while ((item = parameter_grid_->takeAt(0)) != nullptr)
    {
        if (QWidget* widget = item->widget())
        {
            widget->setParent(nullptr);
            widget->deleteLater();
        }
        delete item;
    }
    parameter_row_ = 0;

    // Опциональные вкладки — убрать целиком (следующий build*Form решит
    // заново, какие из них актуальны для нового выбранного блока).
    auto removeOptionalTab = [this](QWidget*& page, QVBoxLayout*& layout)
    {
        if (page == nullptr)
        {
            return;
        }
        const int index = tab_widget_->indexOf(page);
        if (index >= 0)
        {
            tab_widget_->removeTab(index);
        }
        page->deleteLater();
        page = nullptr;
        layout = nullptr;
    };
    removeOptionalTab(nvic_page_, nvic_layout_);
    removeOptionalTab(dma_page_, dma_layout_);
    removeOptionalTab(gpio_page_, gpio_layout_);
}
