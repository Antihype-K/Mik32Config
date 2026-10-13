#include "clock_config_screen.h"
#include "project_document.h"

#include <QComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QSpinBox>
#include <QVBoxLayout>

ClockConfigScreen::ClockConfigScreen(QWidget* parent)
    : QWidget(parent)
{
    QVBoxLayout* root = new QVBoxLayout(this);
    root->setContentsMargins(40, 26, 40, 26);

    QWidget* card = new QWidget();
    card->setMaximumWidth(700);
    QFormLayout* form = new QFormLayout(card);

    // codegen/project/project_config.h, ClockConfig::oscillator_system —
    // "osc32m"/"hsi32m"; mik32_hal_pcc.h, HAL_PCC_OscillatorTypeTypeDef.
    oscillator_combo_ = new QComboBox();
    oscillator_combo_->addItem(
        QString::fromUtf8("Внешний кварц 32 МГц (OSC32M)"), QString::fromUtf8("osc32m"));
    oscillator_combo_->addItem(
        QString::fromUtf8("Внутренний RC-генератор 32 МГц (HSI32M)"), QString::fromUtf8("hsi32m"));
    form->addRow(QString::fromUtf8("Источник тактирования"), oscillator_combo_);

    // PCC_InitTypeDef: "делитель... число между Min=0 и Max=255"
    // (mik32_hal_pcc.h) — частота_шины = базовая / (делитель + 1).
    ahb_divider_ = new QSpinBox();
    ahb_divider_->setRange(0, 255);
    apbm_divider_ = new QSpinBox();
    apbm_divider_->setRange(0, 255);
    apbp_divider_ = new QSpinBox();
    apbp_divider_->setRange(0, 255);

    ahb_frequency_label_ = new QLabel();
    ahb_frequency_label_->setStyleSheet("color: gray;");
    apbm_frequency_label_ = new QLabel();
    apbm_frequency_label_->setStyleSheet("color: gray;");
    apbp_frequency_label_ = new QLabel();
    apbp_frequency_label_->setStyleSheet("color: gray;");

    auto makeDividerRow = [](QSpinBox* divider, QLabel* frequency_label)
    {
        QWidget* row = new QWidget();
        QHBoxLayout* row_layout = new QHBoxLayout(row);
        row_layout->setContentsMargins(0, 0, 0, 0);
        row_layout->addWidget(divider);
        row_layout->addWidget(frequency_label);
        row_layout->addStretch();
        return row;
    };
    form->addRow(QString::fromUtf8("Делитель AHB"), makeDividerRow(ahb_divider_, ahb_frequency_label_));
    form->addRow(QString::fromUtf8("Делитель APB_M"), makeDividerRow(apbm_divider_, apbm_frequency_label_));
    form->addRow(QString::fromUtf8("Делитель APB_P"), makeDividerRow(apbp_divider_, apbp_frequency_label_));

    root->addWidget(card);

    QLabel* note = new QLabel(QString::fromUtf8(
        "Полноценная визуальная диаграмма тактового дерева (как в "
        "STM32CubeMX) — отдельная будущая сессия, здесь только форма: "
        "источник + делители + вычисляемая частота. Обе частоты HSI32M и "
        "OSC32M номинально равны 32 МГц, отличие — во внутреннем/внешнем "
        "источнике сигнала."));
    note->setWordWrap(true);
    note->setStyleSheet("color: gray;");
    root->addSpacing(18);
    root->addWidget(note);
    root->addStretch(1);

    connect(oscillator_combo_, &QComboBox::currentIndexChanged, this, [this](int)
    {
        updateFrequencyLabels();
        writeClockSetting();
    });
    connect(ahb_divider_, &QSpinBox::valueChanged, this, [this](int)
    {
        updateFrequencyLabels();
        writeClockSetting();
    });
    connect(apbm_divider_, &QSpinBox::valueChanged, this, [this](int)
    {
        updateFrequencyLabels();
        writeClockSetting();
    });
    connect(apbp_divider_, &QSpinBox::valueChanged, this, [this](int)
    {
        updateFrequencyLabels();
        writeClockSetting();
    });
    updateFrequencyLabels();
}

void ClockConfigScreen::setContext(ProjectDocument* document)
{
    document_ = document;
}

void ClockConfigScreen::setClockSettings(const nlohmann::json& clock_settings)
{
    syncing_ = true;

    const std::string oscillator_system = (clock_settings.is_object() && clock_settings.contains("oscillator_system")
        && clock_settings["oscillator_system"].is_string())
        ? clock_settings["oscillator_system"].get<std::string>() : "osc32m";
    const int oscillator_index = oscillator_combo_->findData(QString::fromStdString(oscillator_system));
    oscillator_combo_->setCurrentIndex(oscillator_index >= 0 ? oscillator_index : 0);

    auto readDivider = [&clock_settings](const char* field) -> int
    {
        return (clock_settings.is_object() && clock_settings.contains(field) && clock_settings[field].is_number_integer())
            ? clock_settings[field].get<int>() : 0;
    };
    ahb_divider_->setValue(readDivider("ahb_divider"));
    apbm_divider_->setValue(readDivider("apbm_divider"));
    apbp_divider_->setValue(readDivider("apbp_divider"));

    syncing_ = false;
    updateFrequencyLabels();
}

void ClockConfigScreen::updateFrequencyLabels()
{
    // codegen/project/project_config.h, ClockConfig — обе базовые частоты
    // (HSI32M и OSC32M) номинально равны 32 МГц; частота_шины = базовая /
    // (делитель + 1) (mik32_hal_pcc.h, комментарий к AHBDivider/APBMDivider/
    // APBPDivider).
    constexpr double kBaseHz = 32000000.0;
    auto formatFrequency = [](double hz)
    {
        if (hz >= 1000000.0) return QString::fromUtf8("%1 МГц").arg(hz / 1000000.0, 0, 'f', 3);
        if (hz >= 1000.0) return QString::fromUtf8("%1 кГц").arg(hz / 1000.0, 0, 'f', 3);
        return QString::fromUtf8("%1 Гц").arg(hz, 0, 'f', 1);
    };
    ahb_frequency_label_->setText(formatFrequency(kBaseHz / (ahb_divider_->value() + 1.0)));
    apbm_frequency_label_->setText(formatFrequency(kBaseHz / (apbm_divider_->value() + 1.0)));
    apbp_frequency_label_->setText(formatFrequency(kBaseHz / (apbp_divider_->value() + 1.0)));
}

void ClockConfigScreen::writeClockSetting()
{
    if (syncing_ || document_ == nullptr)
    {
        return;
    }
    nlohmann::json clock;
    clock["oscillator_system"] = oscillator_combo_->currentData().toString().toStdString();
    clock["ahb_divider"] = ahb_divider_->value();
    clock["apbm_divider"] = apbm_divider_->value();
    clock["apbp_divider"] = apbp_divider_->value();
    document_->setProjectSetting("clock", clock);
    emit documentChanged();
}
