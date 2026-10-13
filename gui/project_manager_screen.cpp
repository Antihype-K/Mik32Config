#include "project_manager_screen.h"
#include "project_document.h"
#include "freertos_settings_widget.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QSpinBox>
#include <QTabWidget>
#include <QVBoxLayout>

ProjectManagerScreen::ProjectManagerScreen(QWidget* parent)
    : QWidget(parent)
{
    // Вкладки: «Проект» — весь прежний экран без изменений (первая вкладка),
    // «FreeRTOS» — новый редактор document["freertos"]
    // (gui/freertos_settings_widget.h). QTabWidget выбран потому, что
    // FreeRTOS-редактору нужны enable-чекбокс + 3-way source_mode + 7 полей
    // os_config + пять отдельных таблиц — заметно больше содержимого, чем
    // умещается рядом со «Настройками генерации» без потери читаемости.
    QVBoxLayout* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);

    QTabWidget* tabs = new QTabWidget();
    root->addWidget(tabs);

    QWidget* project_tab = new QWidget();
    QVBoxLayout* root_project = new QVBoxLayout(project_tab);
    root_project->setContentsMargins(40, 26, 40, 26);

    QWidget* card = new QWidget();
    card->setMaximumWidth(700);
    QFormLayout* form = new QFormLayout(card);

    file_name_value_ = new QLabel();
    form->addRow(QString::fromUtf8("Файл проекта"), file_name_value_);

    location_value_ = new QLabel();
    location_value_->setWordWrap(true);
    form->addRow(QString::fromUtf8("Расположение"), location_value_);

    package_value_ = new QLabel();
    form->addRow(QString::fromUtf8("Плата"), package_value_);

    root_project->addWidget(card);
    root_project->addSpacing(18);

    // --- Настройки генерации ------------------------------------------------
    QWidget* settings_card = new QWidget();
    settings_card->setMaximumWidth(700);
    QFormLayout* settings_form = new QFormLayout(settings_card);

    // 1. Toolchain / IDE.
    // Данные пункта "rust" — не значение поля JSON "toolchain" напрямую
    // (там допустимо только platformio/cmake/cargo, см. project_config.cpp):
    // это условное имя всей комбинации language=rust+toolchain=cargo,
    // расшифровывается в обработчике ниже. Единственный toolchain для Rust —
    // Cargo, отдельного выбора для него нет и не нужно (grill-решение
    // «Язык → автовыбор toolchain», docs/implementations/rust_codegen.md).
    toolchain_combo_ = new QComboBox();
    toolchain_combo_->addItem(QString::fromUtf8("PlatformIO"), QString::fromUtf8("platformio"));
    toolchain_combo_->addItem(QString::fromUtf8("CMake"), QString::fromUtf8("cmake"));
    toolchain_combo_->addItem(QString::fromUtf8("Rust (Cargo)"), QString::fromUtf8("rust"));
    settings_form->addRow(QString::fromUtf8("Тулчейн / IDE"), toolchain_combo_);

    // 2. HAL source — видна только при Toolchain == CMake.
    hal_source_section_ = new QWidget();
    QVBoxLayout* hal_source_layout = new QVBoxLayout(hal_source_section_);
    hal_source_layout->setContentsMargins(0, 0, 0, 0);

    hal_source_group_ = new QButtonGroup(this);
    hal_bundled_radio_ = new QRadioButton(QString::fromUtf8("Встроенный (Bundled)"));
    hal_network_radio_ = new QRadioButton(QString::fromUtf8("Из сети (Network)"));
    hal_local_path_radio_ = new QRadioButton(QString::fromUtf8("Локальный путь (Local path)"));
    hal_bundled_radio_->setChecked(true);
    hal_source_group_->addButton(hal_bundled_radio_);
    hal_source_group_->addButton(hal_network_radio_);
    hal_source_group_->addButton(hal_local_path_radio_);
    hal_source_layout->addWidget(hal_bundled_radio_);
    hal_source_layout->addWidget(hal_network_radio_);
    hal_source_layout->addWidget(hal_local_path_radio_);

    hal_local_path_edit_ = new QLineEdit();
    hal_local_path_edit_->setReadOnly(true);
    hal_local_path_browse_ = new QPushButton(QString::fromUtf8("Обзор..."));
    QHBoxLayout* hal_local_path_row = new QHBoxLayout();
    hal_local_path_row->addWidget(hal_local_path_edit_);
    hal_local_path_row->addWidget(hal_local_path_browse_);
    hal_source_layout->addLayout(hal_local_path_row);

    settings_form->addRow(QString::fromUtf8("Источник HAL"), hal_source_section_);

    // 3. Compiler path — видно только при Toolchain == CMake.
    compiler_path_section_ = new QWidget();
    QHBoxLayout* compiler_path_layout = new QHBoxLayout(compiler_path_section_);
    compiler_path_layout->setContentsMargins(0, 0, 0, 0);
    compiler_path_edit_ = new QLineEdit();
    compiler_path_edit_->setPlaceholderText(QString::fromUtf8("Автоопределение, если не задано"));
    QPushButton* compiler_path_browse = new QPushButton(QString::fromUtf8("Обзор..."));
    compiler_path_layout->addWidget(compiler_path_edit_);
    compiler_path_layout->addWidget(compiler_path_browse);
    settings_form->addRow(QString::fromUtf8("Путь к компилятору"), compiler_path_section_);

    // 4. Generate .c/.h per peripheral — видно только при Toolchain == CMake.
    per_type_files_section_ = new QWidget();
    QVBoxLayout* per_type_files_layout = new QVBoxLayout(per_type_files_section_);
    per_type_files_layout->setContentsMargins(0, 0, 0, 0);
    per_type_files_checkbox_ = new QCheckBox(
        QString::fromUtf8("Генерировать .c/.h отдельно на каждую периферию"));
    per_type_files_layout->addWidget(per_type_files_checkbox_);
    settings_form->addRow(QString(), per_type_files_section_);

    // 5. Keep User Code — всегда видим, всегда включён, всегда задизейблен:
    // это инвариант кодогена (см. комментарий в .h), а не настройка.
    keep_user_code_checkbox_ = new QCheckBox(
        QString::fromUtf8("Сохранять USER CODE при перегенерации"));
    keep_user_code_checkbox_->setChecked(true);
    keep_user_code_checkbox_->setEnabled(false);
    keep_user_code_checkbox_->setToolTip(QString::fromUtf8(
        "Недоступно для отключения: содержимое между USER CODE BEGIN/END "
        "сохраняется безусловно при каждой генерации "
        "(codegen/user_code_markers.cpp) — это гарантия кодогена, а не "
        "переключаемая опция."));
    settings_form->addRow(QString(), keep_user_code_checkbox_);

    // 6. Заготовки на будущее — видимы, но без функциональности за ними.
    backup_checkbox_ = new QCheckBox(
        QString::fromUtf8("Делать резервную копию ранее сгенерированных файлов"));
    backup_checkbox_->setEnabled(false);
    backup_checkbox_->setToolTip(QString::fromUtf8("Пока не реализовано."));
    settings_form->addRow(QString(), backup_checkbox_);

    delete_stale_checkbox_ = new QCheckBox(QString::fromUtf8(
        "Удалять ранее сгенерированные файлы, если они не перегенерируются"));
    delete_stale_checkbox_->setEnabled(false);
    delete_stale_checkbox_->setToolTip(QString::fromUtf8("Пока не реализовано."));
    settings_form->addRow(QString(), delete_stale_checkbox_);

    root_project->addWidget(settings_card);

    QLabel* note = new QLabel(QString::fromUtf8(
        "mik32_init.c/.h — один общий файл на весь проект по умолчанию; "
        "«по файлу на периферию» доступно только для тулчейна CMake.\n"
        "Демо-логика в while(1) переключается пунктом меню "
        "«Проект → Генерировать демо-логику в while(1)»."));
    note->setWordWrap(true);
    note->setStyleSheet("color: gray;");
    root_project->addSpacing(18);
    root_project->addWidget(note);
    root_project->addStretch(1);

    tabs->addTab(project_tab, QString::fromUtf8("Проект"));

    // --- Вкладка FreeRTOS ------------------------------------------------
    QWidget* freertos_tab = new QWidget();
    QVBoxLayout* root_freertos = new QVBoxLayout(freertos_tab);
    root_freertos->setContentsMargins(40, 26, 40, 26);
    freertos_widget_ = new FreeRtosSettingsWidget();
    root_freertos->addWidget(freertos_widget_);
    tabs->addTab(freertos_tab, QString::fromUtf8("FreeRTOS"));

    // --- Вкладка «Тактирование» (OVERNIGHT_FIELD_REFERENCE.md) -----------
    // Лайт-форма: выпадающий список источника + три делителя + вычисляемая
    // итоговая частота каждой шины текстом. НЕ визуальная диаграмма как в
    // STM32CubeMX — та сознательно отложена на отдельную будущую сессию
    // (grill-решение по вопросу 7 той же ночной сессии).
    QWidget* clock_tab = new QWidget();
    QVBoxLayout* root_clock = new QVBoxLayout(clock_tab);
    root_clock->setContentsMargins(40, 26, 40, 26);

    QWidget* clock_card = new QWidget();
    clock_card->setMaximumWidth(700);
    QFormLayout* clock_form = new QFormLayout(clock_card);

    // codegen/project/project_config.h, ClockConfig::oscillator_system —
    // "osc32m"/"hsi32m"; mik32_hal_pcc.h, HAL_PCC_OscillatorTypeTypeDef.
    clock_oscillator_combo_ = new QComboBox();
    clock_oscillator_combo_->addItem(
        QString::fromUtf8("Внешний кварц 32 МГц (OSC32M)"), QString::fromUtf8("osc32m"));
    clock_oscillator_combo_->addItem(
        QString::fromUtf8("Внутренний RC-генератор 32 МГц (HSI32M)"), QString::fromUtf8("hsi32m"));
    clock_form->addRow(QString::fromUtf8("Источник тактирования"), clock_oscillator_combo_);

    // PCC_InitTypeDef: "делитель... число между Min=0 и Max=255"
    // (mik32_hal_pcc.h) — частота_шины = базовая / (делитель + 1).
    clock_ahb_divider_ = new QSpinBox();
    clock_ahb_divider_->setRange(0, 255);
    clock_apbm_divider_ = new QSpinBox();
    clock_apbm_divider_->setRange(0, 255);
    clock_apbp_divider_ = new QSpinBox();
    clock_apbp_divider_->setRange(0, 255);

    clock_ahb_frequency_label_ = new QLabel();
    clock_ahb_frequency_label_->setStyleSheet("color: gray;");
    clock_apbm_frequency_label_ = new QLabel();
    clock_apbm_frequency_label_->setStyleSheet("color: gray;");
    clock_apbp_frequency_label_ = new QLabel();
    clock_apbp_frequency_label_->setStyleSheet("color: gray;");

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
    clock_form->addRow(QString::fromUtf8("Делитель AHB"), makeDividerRow(clock_ahb_divider_, clock_ahb_frequency_label_));
    clock_form->addRow(QString::fromUtf8("Делитель APB_M"), makeDividerRow(clock_apbm_divider_, clock_apbm_frequency_label_));
    clock_form->addRow(QString::fromUtf8("Делитель APB_P"), makeDividerRow(clock_apbp_divider_, clock_apbp_frequency_label_));

    root_clock->addWidget(clock_card);

    QLabel* clock_note = new QLabel(QString::fromUtf8(
        "Полноценная визуальная диаграмма тактового дерева (как в "
        "STM32CubeMX) — отдельная будущая сессия, здесь только форма: "
        "источник + делители + вычисляемая частота. Обе частоты HSI32M и "
        "OSC32M номинально равны 32 МГц, отличие — во внутреннем/внешнем "
        "источнике сигнала."));
    clock_note->setWordWrap(true);
    clock_note->setStyleSheet("color: gray;");
    root_clock->addSpacing(18);
    root_clock->addWidget(clock_note);
    root_clock->addStretch(1);

    tabs->addTab(clock_tab, QString::fromUtf8("Тактирование"));

    connect(clock_oscillator_combo_, &QComboBox::currentIndexChanged, this, [this](int)
    {
        updateClockFrequencyLabels();
        writeClockSetting();
    });
    connect(clock_ahb_divider_, &QSpinBox::valueChanged, this, [this](int)
    {
        updateClockFrequencyLabels();
        writeClockSetting();
    });
    connect(clock_apbm_divider_, &QSpinBox::valueChanged, this, [this](int)
    {
        updateClockFrequencyLabels();
        writeClockSetting();
    });
    connect(clock_apbp_divider_, &QSpinBox::valueChanged, this, [this](int)
    {
        updateClockFrequencyLabels();
        writeClockSetting();
    });
    updateClockFrequencyLabels();

    updateToolchainDependentVisibility();
    updateHalLocalPathEnabled();

    // --- Проводка: виджет меняется -> пишем в документ -> documentChanged --
    // Тот же путь, что и PropertiesPanel: document_->setProjectSetting(...)
    // прямо в обработчике, затем emit documentChanged() (MainWindow слушает
    // его так же, как PropertiesPanel::documentChanged).
    connect(toolchain_combo_, &QComboBox::currentIndexChanged, this, [this](int)
    {
        updateToolchainDependentVisibility();
        if (syncing_ || document_ == nullptr)
        {
            return;
        }
        // "rust" — синтетическое значение этого комбобокса, не буквальное
        // значение поля "toolchain" (project_config.cpp допускает там только
        // platformio/cmake/cargo) — расшифровывается в пару language=rust +
        // toolchain=cargo. Обратный случай (уход с Rust на C-тулчейн) обязан
        // явно вернуть language=c, иначе оно осталось бы "rust" от
        // предыдущего выбора и следующий парсинг документа отверг бы
        // выбранный здесь platformio/cmake как несовместимый с language=rust
        // (project_config.cpp, проверка "language==Rust но toolchain!=cargo").
        const std::string selection = toolchain_combo_->currentData().toString().toStdString();
        if (selection == "rust")
        {
            document_->setProjectSetting("language", std::string("rust"));
            document_->setProjectSetting("toolchain", std::string("cargo"));
        }
        else
        {
            document_->setProjectSetting("language", std::string("c"));
            document_->setProjectSetting("toolchain", selection);
        }
        emit documentChanged();
    });

    auto writeHalSourceMode = [this](const char* mode)
    {
        updateHalLocalPathEnabled();
        if (syncing_ || document_ == nullptr)
        {
            return;
        }
        // Rust использует отдельную пару полей документа (rust_hal_source_mode) —
        // см. комментарий у is_rust в main_window.cpp.
        const bool is_rust = toolchain_combo_->currentData().toString() == QString::fromUtf8("rust");
        document_->setProjectSetting(is_rust ? "rust_hal_source_mode" : "hal_source_mode", std::string(mode));
        emit documentChanged();
    };
    connect(hal_bundled_radio_, &QRadioButton::toggled, this, [this, writeHalSourceMode](bool checked)
    {
        if (checked) writeHalSourceMode("bundled");
    });
    connect(hal_network_radio_, &QRadioButton::toggled, this, [this, writeHalSourceMode](bool checked)
    {
        if (checked) writeHalSourceMode("network");
    });
    connect(hal_local_path_radio_, &QRadioButton::toggled, this, [this, writeHalSourceMode](bool checked)
    {
        if (checked) writeHalSourceMode("local_path");
    });

    connect(hal_local_path_browse_, &QPushButton::clicked, this, [this]()
    {
        const QString chosen = QFileDialog::getExistingDirectory(
            this, QString::fromUtf8("Локальный путь к HAL"), hal_local_path_edit_->text());
        if (chosen.isEmpty())
        {
            return;
        }
        hal_local_path_edit_->setText(chosen);
        if (document_ != nullptr)
        {
            const bool is_rust = toolchain_combo_->currentData().toString() == QString::fromUtf8("rust");
            document_->setProjectSetting(
                is_rust ? "rust_hal_source_local_path" : "hal_source_local_path", chosen.toStdString());
            emit documentChanged();
        }
    });

    connect(compiler_path_edit_, &QLineEdit::textChanged, this, [this](const QString& text)
    {
        if (syncing_ || document_ == nullptr)
        {
            return;
        }
        document_->setProjectSetting("compiler_path", text.toStdString());
        emit documentChanged();
    });
    connect(compiler_path_browse, &QPushButton::clicked, this, [this]()
    {
        const QString chosen = QFileDialog::getOpenFileName(
            this, QString::fromUtf8("Путь к компилятору"), compiler_path_edit_->text());
        if (!chosen.isEmpty())
        {
            compiler_path_edit_->setText(chosen);  // textChanged выше сам запишет в документ
        }
    });

    connect(per_type_files_checkbox_, &QCheckBox::toggled, this, [this](bool checked)
    {
        if (syncing_ || document_ == nullptr)
        {
            return;
        }
        document_->setProjectSetting("per_type_files", checked);
        emit documentChanged();
    });

    // FreeRtosSettingsWidget собирает весь document["freertos"] как один
    // nlohmann::json объект сам (value()) — здесь только пишем его в
    // документ целиком, тем же приёмом, что и остальные поля выше.
    connect(freertos_widget_, &FreeRtosSettingsWidget::changed, this, [this]()
    {
        if (syncing_ || document_ == nullptr)
        {
            return;
        }
        document_->setProjectSetting("freertos", freertos_widget_->value());
        emit documentChanged();
    });
}

void ProjectManagerScreen::setContext(ProjectDocument* document)
{
    document_ = document;
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

void ProjectManagerScreen::setGenerationSettings(const QString& toolchain,
                                                  const QString& hal_source_mode,
                                                  const QString& hal_source_local_path,
                                                  const QString& compiler_path,
                                                  bool per_type_files)
{
    // syncing_ подавляет обратную запись в документ: без него каждый из
    // setCurrentIndex/setChecked ниже сам стал бы источником "изменения",
    // хотя на самом деле мы просто отражаем то, что уже лежит в документе.
    syncing_ = true;

    const int toolchain_index = toolchain_combo_->findData(toolchain);
    toolchain_combo_->setCurrentIndex(toolchain_index >= 0 ? toolchain_index : 0);

    if (hal_source_mode == QString::fromUtf8("network"))
    {
        hal_network_radio_->setChecked(true);
    }
    else if (hal_source_mode == QString::fromUtf8("local_path"))
    {
        hal_local_path_radio_->setChecked(true);
    }
    else
    {
        // Пусто (старый файл проекта без этого поля) или "bundled" — дефолт.
        hal_bundled_radio_->setChecked(true);
    }
    hal_local_path_edit_->setText(hal_source_local_path);

    compiler_path_edit_->setText(compiler_path);

    per_type_files_checkbox_->setChecked(per_type_files);

    syncing_ = false;

    updateToolchainDependentVisibility();
    updateHalLocalPathEnabled();
}

void ProjectManagerScreen::setFreeRtosSettings(const nlohmann::json& freertos_settings)
{
    // syncing_ здесь избыточен относительно собственного syncing_ виджета
    // (FreeRtosSettingsWidget::setValue уже гасит свой changed() во время
    // программного заполнения), но выставляется для единообразия с
    // остальными setXxx-методами этого экрана.
    syncing_ = true;
    freertos_widget_->setValue(freertos_settings);
    syncing_ = false;
}

void ProjectManagerScreen::setClockSettings(const nlohmann::json& clock_settings)
{
    syncing_ = true;

    const std::string oscillator_system = (clock_settings.is_object() && clock_settings.contains("oscillator_system")
        && clock_settings["oscillator_system"].is_string())
        ? clock_settings["oscillator_system"].get<std::string>() : "osc32m";
    const int oscillator_index = clock_oscillator_combo_->findData(QString::fromStdString(oscillator_system));
    clock_oscillator_combo_->setCurrentIndex(oscillator_index >= 0 ? oscillator_index : 0);

    auto readDivider = [&clock_settings](const char* field) -> int
    {
        return (clock_settings.is_object() && clock_settings.contains(field) && clock_settings[field].is_number_integer())
            ? clock_settings[field].get<int>() : 0;
    };
    clock_ahb_divider_->setValue(readDivider("ahb_divider"));
    clock_apbm_divider_->setValue(readDivider("apbm_divider"));
    clock_apbp_divider_->setValue(readDivider("apbp_divider"));

    syncing_ = false;
    updateClockFrequencyLabels();
}

void ProjectManagerScreen::updateClockFrequencyLabels()
{
    // codegen/project/project_config.h, ClockConfig — обе базовые частоты
    // (HSI32M и OSC32M) номинально равны 32 МГц; частота_шины = базовая /
    // (делитель + 1) (mik32_hal_pcc.h, комментарий к AHBDivider/APBMDivider/
    // APBPDivider — "Частота делится на Prescaler + 1" у Timer32 это тот же
    // принцип).
    constexpr double kBaseHz = 32000000.0;
    auto formatFrequency = [](double hz)
    {
        if (hz >= 1000000.0) return QString::fromUtf8("%1 МГц").arg(hz / 1000000.0, 0, 'f', 3);
        if (hz >= 1000.0) return QString::fromUtf8("%1 кГц").arg(hz / 1000.0, 0, 'f', 3);
        return QString::fromUtf8("%1 Гц").arg(hz, 0, 'f', 1);
    };
    clock_ahb_frequency_label_->setText(formatFrequency(kBaseHz / (clock_ahb_divider_->value() + 1.0)));
    clock_apbm_frequency_label_->setText(formatFrequency(kBaseHz / (clock_apbm_divider_->value() + 1.0)));
    clock_apbp_frequency_label_->setText(formatFrequency(kBaseHz / (clock_apbp_divider_->value() + 1.0)));
}

void ProjectManagerScreen::writeClockSetting()
{
    if (syncing_ || document_ == nullptr)
    {
        return;
    }
    nlohmann::json clock;
    clock["oscillator_system"] = clock_oscillator_combo_->currentData().toString().toStdString();
    clock["ahb_divider"] = clock_ahb_divider_->value();
    clock["apbm_divider"] = clock_apbm_divider_->value();
    clock["apbp_divider"] = clock_apbp_divider_->value();
    document_->setProjectSetting("clock", clock);
    emit documentChanged();
}

void ProjectManagerScreen::updateToolchainDependentVisibility()
{
    const QString toolchain = toolchain_combo_->currentData().toString();
    const bool is_cmake = toolchain == QString::fromUtf8("cmake");
    const bool is_rust = toolchain == QString::fromUtf8("rust");
    // HAL-источник (Bundled/Network/Local path) актуален и для CMake
    // (hal_source_mode), и для Rust (rust_hal_source_mode) — тот же
    // трёхзначный выбор, что решён в /grill-me сессии для Rust-зависимостей
    // (docs/implementations/rust_codegen.md). compiler_path/per_type_files —
    // только CMake: у Cargo-проекта нет ни ручного пути к тулчейну (rustc
    // находится через PATH/.cargo/config.toml), ни разбиения init-файлов по
    // периферии (в Rust-ветке вообще нет отдельных init-файлов, всё в
    // main.rs).
    hal_source_section_->setVisible(is_cmake || is_rust);
    compiler_path_section_->setVisible(is_cmake);
    per_type_files_section_->setVisible(is_cmake);
}

void ProjectManagerScreen::updateHalLocalPathEnabled()
{
    const bool local_path_selected = hal_local_path_radio_->isChecked();
    hal_local_path_edit_->setEnabled(local_path_selected);
    hal_local_path_browse_->setEnabled(local_path_selected);
}
