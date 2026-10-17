#include "clock_config_screen.h"
#include "project_document.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QSpinBox>
#include <QVBoxLayout>

#include <set>

namespace
{
    // codegen/project/project_config.h, ClockConfig — обе базовые частоты
    // 32 МГц (HSI32M/OSC32M) номинально равны 32 МГц; обе базовые частоты
    // 32 кГц (LSI32K/OSC32K) номинально равны 32 кГц — отличие внутри каждой
    // пары только в внутреннем/внешнем источнике сигнала, не в номинале.
    constexpr double kBase32MHz = 32000000.0;
    constexpr double kBase32KHz = 32000.0;

    QString formatFrequency(double hz)
    {
        if (hz >= 1000000.0) return QString::fromUtf8("%1 МГц").arg(hz / 1000000.0, 0, 'f', 3);
        if (hz >= 1000.0) return QString::fromUtf8("%1 кГц").arg(hz / 1000.0, 0, 'f', 3);
        return QString::fromUtf8("%1 Гц").arg(hz, 0, 'f', 1);
    }

    // Стрелка между блоками диаграммы — просто текстовая метка, без
    // кастомного QPainter: остальной GUI этого проекта нигде не рисует
    // вручную, нативные виджеты уже дают нужный вид "блок-схемы", когда
    // расставлены в ряд со стрелками между ними.
    QLabel* arrowLabel(const QString& glyph)
    {
        QLabel* label = new QLabel(glyph);
        QFont font = label->font();
        font.setPointSize(font.pointSize() + 6);
        font.setBold(true);
        label->setFont(font);
        label->setAlignment(Qt::AlignCenter);
        label->setStyleSheet("color: gray;");
        return label;
    }
}

ClockConfigScreen::ClockConfigScreen(QWidget* parent)
    : QWidget(parent)
{
    QVBoxLayout* root = new QVBoxLayout(this);
    root->setContentsMargins(24, 20, 24, 20);
    root->setSpacing(10);

    QLabel* title = new QLabel(QString::fromUtf8(
        "Тактовое дерево МИК32 — в отличие от STM32 здесь нет PLL: два источника "
        "32 МГц идут через делители AHB/APB_M/APB_P, два источника 32 кГц идут "
        "отдельно в RTC."));
    title->setWordWrap(true);
    title->setStyleSheet("color: gray;");
    root->addWidget(title);

    // ====================================================================
    // Верхняя цепочка: источники 32 МГц -> системный мультиплексор ->
    // AHB -> {APB_M, APB_P}.
    // ====================================================================
    QHBoxLayout* main_row = new QHBoxLayout();
    main_row->setSpacing(6);

    // --- Колонка источников 32 МГц --------------------------------------
    QVBoxLayout* osc32m_column = new QVBoxLayout();

    QGroupBox* hsi32m_box = new QGroupBox(QString::fromUtf8("HSI32M (встроенный RC)"));
    QFormLayout* hsi32m_form = new QFormLayout(hsi32m_box);
    hsi32m_enable_ = new QCheckBox(QString::fromUtf8("Включён"));
    hsi32m_form->addRow(hsi32m_enable_);
    hsi32m_calibration_ = new QSpinBox();
    hsi32m_calibration_->setRange(0, 255);
    hsi32m_form->addRow(QString::fromUtf8("Калибровка"), hsi32m_calibration_);
    hsi32m_warning_ = new QLabel(QString::fromUtf8(
        "Errata п.12.4.1: при значениях ниже 16 работа HSI32M не гарантируется."));
    hsi32m_warning_->setWordWrap(true);
    hsi32m_warning_->setStyleSheet("color: #b36b00;");
    hsi32m_warning_->setVisible(false);
    hsi32m_form->addRow(hsi32m_warning_);
    osc32m_column->addWidget(hsi32m_box);

    QGroupBox* osc32m_box = new QGroupBox(QString::fromUtf8("OSC32M (внешний кварц, 32 МГц)"));
    QFormLayout* osc32m_form = new QFormLayout(osc32m_box);
    osc32m_enable_ = new QCheckBox(QString::fromUtf8("Включён"));
    osc32m_form->addRow(osc32m_enable_);
    osc32m_column->addWidget(osc32m_box);

    main_row->addLayout(osc32m_column, 2);
    main_row->addWidget(arrowLabel(QString::fromUtf8("→")));

    // --- Системный мультиплексор -----------------------------------------
    QGroupBox* sysclk_box = new QGroupBox(QString::fromUtf8("SYSCLK"));
    QFormLayout* sysclk_form = new QFormLayout(sysclk_box);
    oscillator_combo_ = new QComboBox();
    oscillator_combo_->addItem(
        QString::fromUtf8("OSC32M (внешний кварц)"), QString::fromUtf8("osc32m"));
    oscillator_combo_->addItem(
        QString::fromUtf8("HSI32M (встроенный RC)"), QString::fromUtf8("hsi32m"));
    sysclk_form->addRow(QString::fromUtf8("Источник"), oscillator_combo_);
    force_osc_sys_ = new QCheckBox(QString::fromUtf8("Зафиксировать источник (ForceOscSys)"));
    sysclk_form->addRow(force_osc_sys_);
    sysclk_frequency_label_ = new QLabel();
    sysclk_frequency_label_->setStyleSheet("font-weight: bold;");
    sysclk_form->addRow(QString::fromUtf8("Частота"), sysclk_frequency_label_);
    main_row->addWidget(sysclk_box, 2);
    main_row->addWidget(arrowLabel(QString::fromUtf8("→")));

    // --- AHB -> HCLK --------------------------------------------------------
    QGroupBox* ahb_box = new QGroupBox(QString::fromUtf8("AHB (HCLK)"));
    QFormLayout* ahb_form = new QFormLayout(ahb_box);
    ahb_divider_ = new QSpinBox();
    ahb_divider_->setRange(0, 255);
    ahb_form->addRow(QString::fromUtf8("Делитель"), ahb_divider_);
    ahb_frequency_label_ = new QLabel();
    ahb_frequency_label_->setStyleSheet("font-weight: bold;");
    ahb_form->addRow(QString::fromUtf8("Частота"), ahb_frequency_label_);
    main_row->addWidget(ahb_box, 2);

    root->addLayout(main_row);

    // --- Разветвление AHB -> {APB_M, APB_P} --------------------------------
    QHBoxLayout* branch_arrows_row = new QHBoxLayout();
    branch_arrows_row->addStretch(1);
    QLabel* down_left = arrowLabel(QString::fromUtf8("↓"));
    QLabel* down_right = arrowLabel(QString::fromUtf8("↓"));
    branch_arrows_row->addWidget(down_left);
    branch_arrows_row->addSpacing(40);
    branch_arrows_row->addWidget(down_right);
    branch_arrows_row->addStretch(4);
    root->addLayout(branch_arrows_row);

    QHBoxLayout* apb_row = new QHBoxLayout();
    apb_row->setSpacing(6);

    QGroupBox* apbm_box = new QGroupBox(QString::fromUtf8("APB_M (PCLK_M)"));
    QFormLayout* apbm_form = new QFormLayout(apbm_box);
    apbm_divider_ = new QSpinBox();
    apbm_divider_->setRange(0, 255);
    apbm_form->addRow(QString::fromUtf8("Делитель"), apbm_divider_);
    apbm_frequency_label_ = new QLabel();
    apbm_frequency_label_->setStyleSheet("font-weight: bold;");
    apbm_form->addRow(QString::fromUtf8("Частота"), apbm_frequency_label_);
    apb_row->addWidget(apbm_box, 1);

    QGroupBox* apbp_box = new QGroupBox(QString::fromUtf8("APB_P (PCLK_P)"));
    QFormLayout* apbp_form = new QFormLayout(apbp_box);
    apbp_divider_ = new QSpinBox();
    apbp_divider_->setRange(0, 255);
    apbp_form->addRow(QString::fromUtf8("Делитель"), apbp_divider_);
    apbp_frequency_label_ = new QLabel();
    apbp_frequency_label_->setStyleSheet("font-weight: bold;");
    apbp_form->addRow(QString::fromUtf8("Частота"), apbp_frequency_label_);
    apb_row->addWidget(apbp_box, 1);
    apb_row->addStretch(2);

    root->addLayout(apb_row);

    // ====================================================================
    // Нижняя цепочка: источники 32 кГц -> RTC.
    // ====================================================================
    QHBoxLayout* rtc_row = new QHBoxLayout();
    rtc_row->setSpacing(6);

    QVBoxLayout* osc32k_column = new QVBoxLayout();
    QGroupBox* lsi32k_box = new QGroupBox(QString::fromUtf8("LSI32K (встроенный RC, 32 кГц)"));
    QFormLayout* lsi32k_form = new QFormLayout(lsi32k_box);
    lsi32k_enable_ = new QCheckBox(QString::fromUtf8("Включён"));
    lsi32k_form->addRow(lsi32k_enable_);
    lsi32k_calibration_ = new QSpinBox();
    lsi32k_calibration_->setRange(0, 15);
    lsi32k_form->addRow(QString::fromUtf8("Калибровка"), lsi32k_calibration_);
    osc32k_column->addWidget(lsi32k_box);

    QGroupBox* osc32k_box = new QGroupBox(QString::fromUtf8("OSC32K (внешний кварц, 32 кГц)"));
    QFormLayout* osc32k_form = new QFormLayout(osc32k_box);
    osc32k_enable_ = new QCheckBox(QString::fromUtf8("Включён"));
    osc32k_form->addRow(osc32k_enable_);
    force_32k_clk_combo_ = new QComboBox();
    force_32k_clk_combo_->addItem(QString::fromUtf8("Авто"), QString::fromUtf8("auto"));
    force_32k_clk_combo_->addItem(QString::fromUtf8("OSC32K"), QString::fromUtf8("osc32k"));
    force_32k_clk_combo_->addItem(QString::fromUtf8("LSI32K"), QString::fromUtf8("lsi32k"));
    osc32k_form->addRow(QString::fromUtf8("Монитор частоты (ForceOscSys 32к)"), force_32k_clk_combo_);
    osc32k_column->addWidget(osc32k_box);

    rtc_row->addLayout(osc32k_column, 2);
    rtc_row->addWidget(arrowLabel(QString::fromUtf8("→")));

    QGroupBox* rtc_box = new QGroupBox(QString::fromUtf8("RTC"));
    QFormLayout* rtc_form = new QFormLayout(rtc_box);
    rtc_clock_source_combo_ = new QComboBox();
    rtc_clock_source_combo_->addItem(QString::fromUtf8("Авто"), QString::fromUtf8("auto"));
    rtc_clock_source_combo_->addItem(QString::fromUtf8("LSI32K"), QString::fromUtf8("lsi32k"));
    rtc_clock_source_combo_->addItem(QString::fromUtf8("OSC32K"), QString::fromUtf8("osc32k"));
    rtc_form->addRow(QString::fromUtf8("Источник RTC"), rtc_clock_source_combo_);
    rtc_clock_cpu_source_combo_ = new QComboBox();
    rtc_clock_cpu_source_combo_->addItem(QString::fromUtf8("OSC32K"), QString::fromUtf8("osc32k"));
    rtc_clock_cpu_source_combo_->addItem(QString::fromUtf8("LSI32K"), QString::fromUtf8("lsi32k"));
    rtc_form->addRow(QString::fromUtf8("Источник для ядра (CPU RTC)"), rtc_clock_cpu_source_combo_);
    rtc_frequency_label_ = new QLabel();
    rtc_frequency_label_->setStyleSheet("font-weight: bold;");
    rtc_form->addRow(QString::fromUtf8("Частота"), rtc_frequency_label_);
    rtc_row->addWidget(rtc_box, 2);
    rtc_row->addStretch(3);

    root->addLayout(rtc_row);
    root->addStretch(1);

    // --- Проводка: каждое поле -> пересчёт -> запись в документ -----------
    // Qt::connect с лямбдой требует фиксированной (не шаблонной) сигнатуры
    // operator() — generic lambda ("auto...") здесь не годится, поэтому
    // каждый коннект получает свою тонкую обёртку с сигнатурой ровно под
    // сигнал виджета, все они просто отбрасывают аргумент и вызывают общий
    // приватный обработчик.
    auto handle = [this]()
    {
        enforceSystemOscillatorInvariant();
        updateComputedState();
        writeClockSetting();
    };
    connect(hsi32m_enable_, &QCheckBox::toggled, this, [handle](bool) { handle(); });
    connect(hsi32m_calibration_, &QSpinBox::valueChanged, this, [handle](int) { handle(); });
    connect(osc32m_enable_, &QCheckBox::toggled, this, [handle](bool) { handle(); });
    connect(lsi32k_enable_, &QCheckBox::toggled, this, [handle](bool) { handle(); });
    connect(lsi32k_calibration_, &QSpinBox::valueChanged, this, [handle](int) { handle(); });
    connect(osc32k_enable_, &QCheckBox::toggled, this, [handle](bool) { handle(); });
    connect(oscillator_combo_, &QComboBox::currentIndexChanged, this, [handle](int) { handle(); });
    connect(force_osc_sys_, &QCheckBox::toggled, this, [handle](bool) { handle(); });
    connect(force_32k_clk_combo_, &QComboBox::currentIndexChanged, this, [handle](int) { handle(); });
    connect(ahb_divider_, &QSpinBox::valueChanged, this, [handle](int) { handle(); });
    connect(apbm_divider_, &QSpinBox::valueChanged, this, [handle](int) { handle(); });
    connect(apbp_divider_, &QSpinBox::valueChanged, this, [handle](int) { handle(); });
    connect(rtc_clock_source_combo_, &QComboBox::currentIndexChanged, this, [handle](int) { handle(); });
    connect(rtc_clock_cpu_source_combo_, &QComboBox::currentIndexChanged, this, [handle](int) { handle(); });

    updateComputedState();
}

void ClockConfigScreen::setContext(ProjectDocument* document)
{
    document_ = document;
}

void ClockConfigScreen::setClockSettings(const nlohmann::json& clock_settings)
{
    syncing_ = true;

    const bool has_object = clock_settings.is_object();

    auto readString = [&](const char* field, const char* fallback) -> std::string
    {
        return (has_object && clock_settings.contains(field) && clock_settings[field].is_string())
            ? clock_settings[field].get<std::string>() : std::string(fallback);
    };
    auto readInt = [&](const char* field, int fallback) -> int
    {
        return (has_object && clock_settings.contains(field) && clock_settings[field].is_number_integer())
            ? clock_settings[field].get<int>() : fallback;
    };
    auto readBool = [&](const char* field, bool fallback) -> bool
    {
        return (has_object && clock_settings.contains(field) && clock_settings[field].is_boolean())
            ? clock_settings[field].get<bool>() : fallback;
    };

    const std::string oscillator_system = readString("oscillator_system", "osc32m");
    const int oscillator_index = oscillator_combo_->findData(QString::fromStdString(oscillator_system));
    oscillator_combo_->setCurrentIndex(oscillator_index >= 0 ? oscillator_index : 0);

    ahb_divider_->setValue(readInt("ahb_divider", 0));
    apbm_divider_->setValue(readInt("apbm_divider", 0));
    apbp_divider_->setValue(readInt("apbp_divider", 0));

    hsi32m_calibration_->setValue(readInt("hsi32m_calibration", 128));
    lsi32k_calibration_->setValue(readInt("lsi32k_calibration", 8));

    const std::string rtc_clock_source = readString("rtc_clock_source", "auto");
    const int rtc_source_index = rtc_clock_source_combo_->findData(QString::fromStdString(rtc_clock_source));
    rtc_clock_source_combo_->setCurrentIndex(rtc_source_index >= 0 ? rtc_source_index : 0);

    const std::string rtc_clock_cpu_source = readString("rtc_clock_cpu_source", "osc32k");
    const int rtc_cpu_source_index = rtc_clock_cpu_source_combo_->findData(QString::fromStdString(rtc_clock_cpu_source));
    rtc_clock_cpu_source_combo_->setCurrentIndex(rtc_cpu_source_index >= 0 ? rtc_cpu_source_index : 0);

    force_osc_sys_->setChecked(readBool("force_osc_sys", false));

    const std::string force_32k_clk = readString("force_32k_clk", "osc32k");
    const int force_32k_index = force_32k_clk_combo_->findData(QString::fromStdString(force_32k_clk));
    force_32k_clk_combo_->setCurrentIndex(force_32k_index >= 0 ? force_32k_index : 0);

    // oscillator_enable — дефолт "все четыре включены" (ClockConfig).
    std::set<std::string> enabled = {"hsi32m", "osc32m", "lsi32k", "osc32k"};
    if (has_object && clock_settings.contains("oscillator_enable") && clock_settings["oscillator_enable"].is_array())
    {
        enabled.clear();
        for (const nlohmann::json& item : clock_settings["oscillator_enable"])
        {
            if (item.is_string())
            {
                enabled.insert(item.get<std::string>());
            }
        }
    }
    hsi32m_enable_->setChecked(enabled.count("hsi32m") > 0);
    osc32m_enable_->setChecked(enabled.count("osc32m") > 0);
    lsi32k_enable_->setChecked(enabled.count("lsi32k") > 0);
    osc32k_enable_->setChecked(enabled.count("osc32k") > 0);

    syncing_ = false;
    enforceSystemOscillatorInvariant();
    updateComputedState();
}

void ClockConfigScreen::enforceSystemOscillatorInvariant()
{
    // oscillator_system обязан входить в oscillator_enable — иначе
    // HAL_PCC_Config выключит источник, от которого тактируется ядро
    // (project_config.cpp так и проверяет: "Поле oscillator_enable не
    // содержит ..." -> отказ в генерации). Здесь это предотвращается на
    // уровне UI, а не ловится после факта диагностикой: чекбокс активного
    // системного источника блокируется от снятия, а при смене системного
    // источника его чекбокс автоматически включается.
    const bool syncing_before = syncing_;
    syncing_ = true;

    const bool system_is_hsi32m = oscillator_combo_->currentData().toString() == QString::fromUtf8("hsi32m");
    if (system_is_hsi32m)
    {
        hsi32m_enable_->setChecked(true);
    }
    else
    {
        osc32m_enable_->setChecked(true);
    }
    hsi32m_enable_->setEnabled(!system_is_hsi32m);
    osc32m_enable_->setEnabled(system_is_hsi32m);

    syncing_ = syncing_before;
}

void ClockConfigScreen::updateComputedState()
{
    const bool system_is_hsi32m = oscillator_combo_->currentData().toString() == QString::fromUtf8("hsi32m");
    const double sysclk_hz = kBase32MHz;  // HSI32M и OSC32M оба номинально 32 МГц
    sysclk_frequency_label_->setText(formatFrequency(sysclk_hz));

    const double ahb_hz = sysclk_hz / (ahb_divider_->value() + 1.0);
    ahb_frequency_label_->setText(formatFrequency(ahb_hz));
    apbm_frequency_label_->setText(formatFrequency(ahb_hz / (apbm_divider_->value() + 1.0)));
    apbp_frequency_label_->setText(formatFrequency(ahb_hz / (apbp_divider_->value() + 1.0)));

    // RTC — источник выбирается явным образом (rtc_clock_cpu_source), не
    // через систему 32 МГц; "auto" в rtc_clock_source не меняет частоту
    // источника для ядра (rtc_clock_cpu_source — отдельное поле), поэтому
    // для отображения частоты используем именно его.
    const bool rtc_cpu_is_lsi32k =
        rtc_clock_cpu_source_combo_->currentData().toString() == QString::fromUtf8("lsi32k");
    rtc_frequency_label_->setText(formatFrequency(kBase32KHz));
    Q_UNUSED(rtc_cpu_is_lsi32k);  // оба источника 32 кГц номинально одинаковы — только для будущего различия

    // Errata §12.4.1 — предупреждение показывается только пока HSI32M
    // реально выбран как активный системный источник ИЛИ включён как
    // дополнительный осциллятор (калибровка на него всё равно действует).
    hsi32m_warning_->setVisible(hsi32m_calibration_->value() < 16 && hsi32m_enable_->isChecked());
    Q_UNUSED(system_is_hsi32m);
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
    clock["hsi32m_calibration"] = hsi32m_calibration_->value();
    clock["lsi32k_calibration"] = lsi32k_calibration_->value();
    clock["rtc_clock_source"] = rtc_clock_source_combo_->currentData().toString().toStdString();
    clock["rtc_clock_cpu_source"] = rtc_clock_cpu_source_combo_->currentData().toString().toStdString();
    clock["force_osc_sys"] = force_osc_sys_->isChecked();
    clock["force_32k_clk"] = force_32k_clk_combo_->currentData().toString().toStdString();

    nlohmann::json oscillator_enable = nlohmann::json::array();
    if (hsi32m_enable_->isChecked()) oscillator_enable.push_back("hsi32m");
    if (osc32m_enable_->isChecked()) oscillator_enable.push_back("osc32m");
    if (lsi32k_enable_->isChecked()) oscillator_enable.push_back("lsi32k");
    if (osc32k_enable_->isChecked()) oscillator_enable.push_back("osc32k");
    clock["oscillator_enable"] = oscillator_enable;

    document_->setProjectSetting("clock", clock);
    emit documentChanged();
}
