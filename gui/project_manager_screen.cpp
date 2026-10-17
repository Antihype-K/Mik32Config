#include "project_manager_screen.h"
#include "project_document.h"

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
#include <QTabWidget>
#include <QTableWidget>
#include <QHeaderView>
#include <QAbstractItemView>
#include <QVBoxLayout>
#include <map>
#include <set>

ProjectManagerScreen::ProjectManagerScreen(QWidget* parent)
    : QWidget(parent)
{
    // Вкладка «Проект» — весь прежний экран без изменений. FreeRTOS раньше
    // жил здесь отдельной вкладкой; перенесён в дерево периферии слева
    // (gui/peripheral_tree.cpp, группа "Middleware and Software Packs") —
    // в STM32CubeMX middleware выбирается кликом по дереву, а не вкладкой
    // внутри Project Manager (см. gui/properties_panel.cpp::buildFreertosForm).
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
        "«по файлу на периферию» доступно только для тулчейна CMake."));
    note->setWordWrap(true);
    note->setStyleSheet("color: gray;");
    root_project->addSpacing(18);
    root_project->addWidget(note);
    root_project->addStretch(1);

    tabs->addTab(project_tab, QString::fromUtf8("Проект"));

    // --- Вкладка «Диагностика» -----------------------------------------
    // Read-only сводка в духе Project Manager -> Advanced Settings / NVIC
    // Settings из STM32CubeMX, но честная относительно того, что реально
    // есть у MIK32: ОДИН драйвер (HAL) на все модули — у vendor SDK
    // (~/.platformio/packages/framework-mik32v2-sdk) нет LL-слоя, который
    // CubeMX даёт выбирать для STM32, поэтому здесь не переключатель,
    // а констатация факта с пояснением, почему он не настраивается. Таблица
    // EPIC-прерываний — тоже read-only: редактируемый edge/level-редактор
    // сознательно не сделан в этой же сессии как отдельный Тир-3 модуль
    // (AUDIT_TZ_PERIPHERY_GAPS.md) — для части источников (PVD AVCC/VCC,
    // BOR, battery, DMA global error, freq monitor) нет подтверждённой
    // классификации edge/level ни в одном доступном материале, а неверная
    // догадка воспроизводит уже дважды пойманный в этой сессии класс бага
    // (незачищенный уровневый флаг -> бесконечный повторный вход в
    // прерывание, см. RTC ALRM). Эта вкладка не редактирует ничего — только
    // показывает, что уже решено кодогеном для ТЕКУЩЕЙ конфигурации.
    QWidget* diagnostics_tab = new QWidget();
    QVBoxLayout* root_diagnostics = new QVBoxLayout(diagnostics_tab);
    root_diagnostics->setContentsMargins(40, 26, 40, 26);

    QLabel* driver_caption = new QLabel(QString::fromUtf8(
        "<b>Драйвер на модуль</b><br>"
        "MIK32 SDK не содержит LL-варианта драйверов (в отличие от STM32) — "
        "только HAL. Переключателя нет не потому, что он не реализован, "
        "а потому, что выбирать не из чего."));
    driver_caption->setWordWrap(true);
    root_diagnostics->addWidget(driver_caption);

    driver_table_ = new QTableWidget();
    driver_table_->setColumnCount(2);
    driver_table_->setHorizontalHeaderLabels(
        {QString::fromUtf8("Периферия"), QString::fromUtf8("Драйвер")});
    driver_table_->horizontalHeader()->setStretchLastSection(true);
    driver_table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    driver_table_->setSelectionMode(QAbstractItemView::NoSelection);
    driver_table_->setMaximumHeight(220);
    root_diagnostics->addWidget(driver_table_);

    root_diagnostics->addSpacing(18);

    QLabel* irq_caption = new QLabel(QString::fromUtf8(
        "<b>Прерывания (EPIC)</b><br>"
        "Read-only: кто из включённых блоков генерирует обработчик "
        "прерывания в текущей конфигурации. Приоритет/edge-level здесь не "
        "редактируются — см. AUDIT_TZ_PERIPHERY_GAPS.md, Тир 3 (EPIC)."));
    irq_caption->setWordWrap(true);
    root_diagnostics->addWidget(irq_caption);

    irq_table_ = new QTableWidget();
    irq_table_->setColumnCount(2);
    irq_table_->setHorizontalHeaderLabels(
        {QString::fromUtf8("Периферия"), QString::fromUtf8("Прерывание")});
    irq_table_->horizontalHeader()->setStretchLastSection(true);
    irq_table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    irq_table_->setSelectionMode(QAbstractItemView::NoSelection);
    root_diagnostics->addWidget(irq_table_, 1);

    tabs->addTab(diagnostics_tab, QString::fromUtf8("Расширенные настройки"));

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
    refreshDiagnosticsTab();
}

namespace
{
    // Человекочитаемое имя блока "type:instance" — тот же приём, что
    // PeripheralTree::keyFor, но для отображения, не для ключа QSet.
    QString peripheralLabel(const std::string& type, const nlohmann::json& block)
    {
        static const std::map<std::string, QString> kDisplayNames = {
            {"gpio", "GPIO"}, {"uart", "USART"}, {"timer16", "Timer16"}, {"i2c", "I2C"},
            {"spi", "SPI"}, {"timer32", "Timer32"}, {"rtc", "RTC"}, {"crc", "CRC"},
            {"eeprom", "EEPROM"}, {"wdt", "WDT"}, {"dac", "DAC"}, {"adc", "ADC"},
            {"crypto", "CRYPTO"}, {"otp", "OTP"}, {"tsens", "TSENS"}, {"pvd", "PVD"},
            {"bus_watchdog", "Bus Watchdog"},
        };
        const auto it = kDisplayNames.find(type);
        const QString name = it != kDisplayNames.end() ? it->second : QString::fromStdString(type);
        if (block.contains("instance") && block["instance"].is_number_integer())
        {
            return QString("%1_%2").arg(name).arg(block["instance"].get<int>());
        }
        return name;
    }
}

void ProjectManagerScreen::refreshDiagnosticsTab()
{
    if (driver_table_ == nullptr || irq_table_ == nullptr)
    {
        return;
    }
    if (document_ == nullptr || !document_->document().contains("peripherals")
        || !document_->document()["peripherals"].is_array())
    {
        driver_table_->setRowCount(0);
        irq_table_->setRowCount(0);
        return;
    }

    const nlohmann::json& peripherals = document_->document()["peripherals"];

    // --- Таблица драйверов: один раз на type:instance, HAL всегда -------
    driver_table_->setRowCount(0);
    for (const nlohmann::json& block : peripherals)
    {
        if (!block.contains("type") || !block["type"].is_string())
        {
            continue;
        }
        const std::string type = block["type"].get<std::string>();
        const int row = driver_table_->rowCount();
        driver_table_->insertRow(row);
        driver_table_->setItem(row, 0, new QTableWidgetItem(peripheralLabel(type, block)));
        driver_table_->setItem(row, 1, new QTableWidgetItem(QString::fromUtf8("HAL")));
    }

    // --- Таблица прерываний: пересчитана из тех же условий, что реальный --
    // кодоген использует для contribution.irq_handler_function_name
    // (codegen/project/rtc_contribution.cpp, spi_contribution.cpp,
    // gpio_contribution.cpp) и для EPIC-масок без именованного шаблона
    // обработчика (timer32_contribution.cpp, tsens_contribution.cpp) —
    // пересчёт здесь, а не вызов реального пайплайна, т.к. ProjectManagerScreen
    // не имеет доступа к ProjectKnowledge (нужен generateProjectFirmware),
    // а тянуть его сюда ради одной read-only таблицы — больше связности,
    // чем эта вкладка стоит. Если условия в контрибьюторах изменятся,
    // эта таблица может разойтись с реальным кодоgenom — риск принят
    // осознанно ради простоты (см. директиву задачи #24).
    irq_table_->setRowCount(0);
    auto addIrqRow = [this](const QString& label, const QString& detail)
    {
        const int row = irq_table_->rowCount();
        irq_table_->insertRow(row);
        irq_table_->setItem(row, 0, new QTableWidgetItem(label));
        irq_table_->setItem(row, 1, new QTableWidgetItem(detail));
    };

    bool any_gpio_irq = false;
    for (const nlohmann::json& block : peripherals)
    {
        if (!block.contains("type") || !block["type"].is_string())
        {
            continue;
        }
        const std::string type = block["type"].get<std::string>();

        if (type == "gpio" && block.contains("interrupts") && block["interrupts"].is_array()
            && !block["interrupts"].empty())
        {
            any_gpio_irq = true;
        }
        else if (type == "spi" && block.contains("interrupts") && block["interrupts"].is_array()
            && !block["interrupts"].empty())
        {
            addIrqRow(peripheralLabel(type, block),
                QString::fromUtf8("готовый SPI_x_IRQHandler_Template()"));
        }
        else if (type == "rtc" && block.contains("alarm") && block["alarm"].is_object()
            && block["alarm"].value("interrupt_enabled", false))
        {
            addIrqRow(peripheralLabel(type, block),
                QString::fromUtf8("готовый RTC_IRQHandler_Template()"));
        }
        else if (type == "timer32"
            && (block.value("overflow_interrupt", false) || block.value("underflow_interrupt", false)
                || (block.contains("channels") && block["channels"].is_array() && !block["channels"].empty())))
        {
            addIrqRow(peripheralLabel(type, block),
                QString::fromUtf8("маска EPIC включена, обработчик пишет пользователь (raw_trap_handler)"));
        }
        else if (type == "tsens" && block.contains("interrupts") && block["interrupts"].is_array()
            && !block["interrupts"].empty())
        {
            addIrqRow(peripheralLabel(type, block),
                QString::fromUtf8("макрос __HAL_TSENS_IRQ_ENABLE_*, обработчик пишет пользователь"));
        }
    }
    if (any_gpio_irq)
    {
        addIrqRow(QString::fromUtf8("GPIO"),
            QString::fromUtf8("готовый GPIO_IRQHandler_Template() (общий на все линии GPIO IRQ)"));
    }

    if (irq_table_->rowCount() == 0)
    {
        addIrqRow(QString::fromUtf8("—"), QString::fromUtf8("ни один блок не запрашивает прерывание"));
    }
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
