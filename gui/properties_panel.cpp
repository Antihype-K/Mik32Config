//
// Реализация панели свойств. См. properties_panel.h — там зафиксировано
// главное решение: форма пересобирается целиком на каждую смену выбора и на
// каждое изменение поля, от которого зависит СОСТАВ формы (mode у I2C,
// instance у GPIO). Ниже это выражено в том, что почти каждый обработчик
// заканчивается либо emit documentChanged(), либо парой
// setField + documentChanged + refresh.
//
// Источник допустимых значений для каждого выпадающего списка указан
// комментарием перед списком. Правило одно: набор в GUI обязан совпадать с
// набором, по которому валидирует парсер (parser/*), иначе пользователь смог
// бы ввести значение, которое тут же отвергнет валидатор.
//
#include "properties_panel.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QVBoxLayout>

#include <functional>

namespace
{
    // --- Наборы допустимых значений ----------------------------------------
    // Каждый набор продублирован из соответствующего парсера. Дублирование
    // осознанное: парсеры хранят их как std::set<std::string> во внутренних
    // анонимных namespace'ах (недоступны снаружи), а тянуть ради GUI ещё один
    // слой абстракции поверх схем — работа, которую эта панель не окупает.
    // Цена дублирования — обязанность держать списки синхронными; поэтому
    // рядом с каждым стоит имя константы-первоисточника.

    // parser/gpio/gpio_parser.cpp, kValidModes; schemas/gpio.json, fields.mode
    const std::vector<std::pair<const char*, const char*>> kGpioModes = {
        {"Вход (input)",                    "input"},
        {"Выход (output)",                  "output"},
        {"Последовательный (serial)",       "serial"},
        {"Таймер/serial (timer_serial)",    "timer_serial"},
        {"Аналоговый (analog)",             "analog"},
    };

    // parser/gpio/gpio_parser.cpp, kValidPulls; schemas/gpio.json, fields.pull
    const std::vector<std::pair<const char*, const char*>> kGpioPulls = {
        {"Нет подтяжки",   "none"},
        {"К питанию",      "pull_up"},
        {"К земле",        "pull_down"},
    };

    // parser/gpio/gpio_parser.cpp, kValidDriveStrengths
    const std::vector<std::pair<const char*, const char*>> kGpioDriveStrengths = {
        {"2 мА", "2mA"},
        {"4 мА", "4mA"},
        {"8 мА", "8mA"},
    };

    // parser/USART/usart_parser.cpp, kValidBaudRates; ТЗ §3.1.3.2 — скорость
    // выбирается из фиксированного ряда, а не вводится произвольным числом.
    const std::vector<int> kBaudRates = {9600, 19200, 38400, 57600, 115200, 230400, 460800, 921600};

    // parser/USART/usart_parser.cpp, kValidDataBits — 7/8/9 (биты M1:M0
    // регистра CONTROL1, даташит Таблица 18).
    const std::vector<int> kDataBits = {7, 8, 9};

    // parser/USART/usart_parser.cpp, kValidStopBits. ТЗ просит 0.5/1/1.5/2, но
    // в CONTROL1 под стоп-биты отведён ОДИН разряд STOP_1 — значений может быть
    // только два. Расхождение разобрано в docs/implementations/uart.md.
    const std::vector<int> kStopBits = {1, 2};

    const std::vector<std::pair<const char*, const char*>> kParities = {
        {"Нет",        "none"},
        {"Чётность",   "even"},
        {"Нечётность", "odd"},
    };

    const std::vector<std::pair<const char*, const char*>> kDirections = {
        {"Дуплекс",         "duplex"},
        {"Только передача", "tx"},
        {"Только приём",    "rx"},
    };

    // parser/timer16/timer16_parser.cpp; ТЗ §3.1.4 п.2 — «период / одиночный
    // импульс / счёт внешних событий».
    const std::vector<std::pair<const char*, const char*>> kTimer16Modes = {
        {"Периодический ШИМ (period)",          "period"},
        {"Одиночный импульс (one_shot)",        "one_shot"},
        {"Счёт внешних событий",                "external_event_count"},
    };

    // mik32_hal_timer16.h, HAL_Timer16_PrescalerTypeDef — восемь степеней двойки.
    const std::vector<int> kPrescalers = {1, 2, 4, 8, 16, 32, 64, 128};

    // parser/i2c/i2c_parser.cpp, kValidClockSpeeds — Standard/Fast/Fast+.
    const std::vector<std::pair<const char*, int>> kI2cSpeeds = {
        {"100 кГц (Standard)",  100000},
        {"400 кГц (Fast)",      400000},
        {"1 МГц (Fast+)",       1000000},
    };

    // parser/i2c/i2c_parser.cpp, kValidMasterDemos / kValidSlaveDemos.
    // Набор зависит от mode — именно поэтому смена mode пересобирает форму.
    const std::vector<std::pair<const char*, const char*>> kI2cMasterDemos = {
        {"Сканер шины (scanner)",  "scanner"},
        {"Ping-pong",              "ping_pong"},
    };
    const std::vector<std::pair<const char*, const char*>> kI2cSlaveDemos = {
        {"Пассивный приём (receive)",       "receive"},
        {"Отвечать сканеру (scanner_target)", "scanner_target"},
        {"Ping-pong",                       "ping_pong"},
    };

    // --- Мелкие строители виджетов -----------------------------------------
    // Выпадающий список строковых значений. on_change получает уже само
    // значение схемы ("pull_up"), а не индекс: вызывающий не должен знать,
    // в каком порядке они лежат в таблице выше.
    QComboBox* stringCombo(const std::vector<std::pair<const char*, const char*>>& options,
                           const std::string& current,
                           const std::function<void(const std::string&)>& on_change)
    {
        QComboBox* combo = new QComboBox();
        for (const auto& option : options)
        {
            combo->addItem(QString::fromUtf8(option.first), QString::fromUtf8(option.second));
        }
        const int found = combo->findData(QString::fromStdString(current));
        combo->setCurrentIndex(found >= 0 ? found : 0);
        QObject::connect(combo, &QComboBox::currentIndexChanged, combo, [combo, on_change](int)
        {
            on_change(combo->currentData().toString().toStdString());
        });
        return combo;
    }

    // То же для целочисленных перечислений (baud_rate, prescaler, ...).
    // Отдельная функция, а не шаблон: значение кладётся в JSON, и типы там
    // разные — число и строка не взаимозаменяемы для валидатора.
    QComboBox* intCombo(const std::vector<int>& values,
                        const QString& suffix,
                        int current,
                        const std::function<void(int)>& on_change)
    {
        QComboBox* combo = new QComboBox();
        for (int value : values)
        {
            combo->addItem(QString::number(value) + suffix, value);
        }
        const int found = combo->findData(current);
        combo->setCurrentIndex(found >= 0 ? found : 0);
        QObject::connect(combo, &QComboBox::currentIndexChanged, combo, [combo, on_change](int)
        {
            on_change(combo->currentData().toInt());
        });
        return combo;
    }

    QComboBox* labelledIntCombo(const std::vector<std::pair<const char*, int>>& options,
                                int current,
                                const std::function<void(int)>& on_change)
    {
        QComboBox* combo = new QComboBox();
        for (const auto& option : options)
        {
            combo->addItem(QString::fromUtf8(option.first), option.second);
        }
        const int found = combo->findData(current);
        combo->setCurrentIndex(found >= 0 ? found : 0);
        QObject::connect(combo, &QComboBox::currentIndexChanged, combo, [combo, on_change](int)
        {
            on_change(combo->currentData().toInt());
        });
        return combo;
    }
}

PropertiesPanel::PropertiesPanel(QWidget* parent)
    : QWidget(parent)
{
    QVBoxLayout* outer = new QVBoxLayout(this);
    outer->setContentsMargins(8, 8, 8, 8);
    refresh();
}

void PropertiesPanel::setContext(ProjectDocument* document, const ProjectKnowledge* knowledge)
{
    document_ = document;
    knowledge_ = knowledge;
    refresh();
}

void PropertiesPanel::showPeripheral(const QString& type, int instance)
{
    type_ = type;
    instance_ = instance;
    // Позиция сбрасывается при КАЖДОЙ смене блока: у нового блока групп может
    // быть меньше, и сохранённый индекс указывал бы в пустоту.
    gpio_block_position_ = 0;
    refresh();
}

void PropertiesPanel::clearForm()
{
    if (content_ == nullptr)
    {
        return;
    }
    // Форма пересобирается ИЗ обработчика виджета, который в ней и лежит
    // (пользователь дёрнул комбобокс -> refresh). Удалять такой виджет
    // немедленно нельзя: управление вернётся в его же метод обработки события.
    // setParent(nullptr) убирает его с экрана сразу, deleteLater уничтожает,
    // когда цикл событий вернётся к себе.
    content_->setParent(nullptr);
    content_->deleteLater();
    content_ = nullptr;
}

void PropertiesPanel::refresh()
{
    clearForm();

    content_ = new QWidget(this);
    QVBoxLayout* layout = new QVBoxLayout(content_);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setAlignment(Qt::AlignTop);
    static_cast<QVBoxLayout*>(this->layout())->addWidget(content_);

    if (document_ == nullptr || knowledge_ == nullptr || type_.isEmpty() || instance_ < 0)
    {
        QLabel* hint = new QLabel(QString::fromUtf8(
            "Блок не выбран.\n\nВыберите периферийный блок в дереве слева."));
        hint->setWordWrap(true);
        hint->setEnabled(false);
        layout->addWidget(hint);
        return;
    }

    if (!document_->isEnabled(type_.toStdString(), instance_))
    {
        buildDisabledPlaceholder(layout, instance_);
        return;
    }

    if (type_ == "gpio")          buildGpioForm(layout, instance_);
    else if (type_ == "uart")     buildUartForm(layout, instance_);
    else if (type_ == "timer16")  buildTimer16Form(layout, instance_);
    else if (type_ == "i2c")      buildI2cForm(layout, instance_);
}

void PropertiesPanel::buildDisabledPlaceholder(QVBoxLayout* layout, int instance)
{
    // Отключённый блок — не пустой экран: пользователю нужно понимать, что
    // блок существует в МК, просто не участвует в конфигурации, и что его
    // можно включить прямо отсюда (то же действие есть в контекстном меню
    // дерева, ТЗ §3.1.9 п.3).
    QLabel* title = new QLabel(QString("<b>%1_%2</b>").arg(type_.toUpper()).arg(instance));
    layout->addWidget(title);

    QLabel* hint = new QLabel(QString::fromUtf8(
        "Блок отключён — в конфигурации его нет, код для него не генерируется."));
    hint->setWordWrap(true);
    layout->addWidget(hint);

    QPushButton* enable = new QPushButton(QString::fromUtf8("Включить"));
    layout->addWidget(enable);
    connect(enable, &QPushButton::clicked, this, [this]()
    {
        // Значения по умолчанию задаёт документ (ProjectDocument::defaultBlock),
        // а не панель: они должны быть одинаковыми и здесь, и при включении из
        // контекстного меню дерева.
        document_->addBlock(type_.toStdString(), instance_);
        emit documentChanged();
        refresh();
    });
}

void PropertiesPanel::buildGpioForm(QVBoxLayout* layout, int instance)
{
    const std::vector<int> indices = document_->blockIndices("gpio", instance);
    if (indices.empty())
    {
        return;
    }
    if (gpio_block_position_ >= static_cast<int>(indices.size()))
    {
        gpio_block_position_ = static_cast<int>(indices.size()) - 1;
    }
    const int block_index = indices[gpio_block_position_];

    QLabel* title = new QLabel(QString("<b>GPIO_%1</b>").arg(instance));
    layout->addWidget(title);

    // Групп у одного порта может быть несколько: разным пинам одного GPIO
    // нужны разные mode/pull/drive_strength, а блок задаёт их сразу для всего
    // списка "pins". Это не костыль GUI, а штатная модель конфигурации —
    // см. codegen/project/gpio_contribution.h (несколько блоков одного
    // instance сливаются в одну init-функцию).
    QGroupBox* group_box = new QGroupBox(QString::fromUtf8("Группа выводов"));
    QHBoxLayout* group_row = new QHBoxLayout(group_box);

    QComboBox* group_selector = new QComboBox();
    for (size_t i = 0; i < indices.size(); ++i)
    {
        group_selector->addItem(QString::fromUtf8("Группа %1 из %2")
                                    .arg(i + 1).arg(indices.size()));
    }
    group_selector->setCurrentIndex(gpio_block_position_);
    connect(group_selector, &QComboBox::currentIndexChanged, this, [this](int index)
    {
        gpio_block_position_ = index;
        refresh();  // другая группа — другие значения полей
    });
    group_row->addWidget(group_selector, 1);

    QPushButton* add_group = new QPushButton("+");
    add_group->setToolTip(QString::fromUtf8("Добавить группу выводов с другими режимом/подтяжкой"));
    add_group->setFixedWidth(32);
    connect(add_group, &QPushButton::clicked, this, [this, instance]()
    {
        document_->addBlock("gpio", instance);
        gpio_block_position_ = static_cast<int>(document_->blockIndices("gpio", instance).size()) - 1;
        emit documentChanged();
        refresh();
    });
    group_row->addWidget(add_group);

    QPushButton* remove_group = new QPushButton("−");
    remove_group->setToolTip(QString::fromUtf8("Удалить эту группу выводов"));
    remove_group->setFixedWidth(32);
    // Последнюю группу удалять нечем: блок без групп — это отключённый блок,
    // и для отключения есть отдельная кнопка ниже. Иначе одно и то же
    // состояние достигалось бы двумя разными кнопками с разным смыслом.
    remove_group->setEnabled(indices.size() > 1);
    connect(remove_group, &QPushButton::clicked, this, [this, block_index]()
    {
        document_->removeBlockAt(block_index);
        gpio_block_position_ = 0;
        emit documentChanged();
        refresh();
    });
    group_row->addWidget(remove_group);
    layout->addWidget(group_box);

    QGroupBox* fields_box = new QGroupBox(QString::fromUtf8("Параметры группы"));
    QFormLayout* form = new QFormLayout(fields_box);

    form->addRow(QString::fromUtf8("Режим:"), stringCombo(
        kGpioModes, document_->field(block_index, "mode").is_string()
                        ? document_->field(block_index, "mode").get<std::string>() : "output",
        [this, block_index](const std::string& value)
        {
            document_->setField(block_index, "mode", value);
            emit documentChanged();
        }));

    form->addRow(QString::fromUtf8("Подтяжка:"), stringCombo(
        kGpioPulls, document_->field(block_index, "pull").is_string()
                        ? document_->field(block_index, "pull").get<std::string>() : "none",
        [this, block_index](const std::string& value)
        {
            document_->setField(block_index, "pull", value);
            emit documentChanged();
        }));

    form->addRow(QString::fromUtf8("Ток драйвера:"), stringCombo(
        kGpioDriveStrengths, document_->field(block_index, "drive_strength").is_string()
                        ? document_->field(block_index, "drive_strength").get<std::string>() : "2mA",
        [this, block_index](const std::string& value)
        {
            document_->setField(block_index, "drive_strength", value);
            emit documentChanged();
        }));
    layout->addWidget(fields_box);

    // Верхняя граница разрядов зависит от instance: у GPIO_2 в корпусе
    // разведены только разряды 0..7 (даташит v2.2.3, Таблица 193, с.320 —
    // то же ограничение зашито в schemas/gpio.json, item_max_by_instance).
    const int max_pin = (instance == 2) ? 7 : 15;

    QGroupBox* pins_box = new QGroupBox(QString::fromUtf8("Выводы"));
    QGridLayout* pins_grid = new QGridLayout(pins_box);

    const nlohmann::json pins_value = document_->field(block_index, "pins");
    for (int pin = 0; pin <= max_pin; ++pin)
    {
        bool checked = false;
        if (pins_value.is_array())
        {
            for (const auto& item : pins_value)
            {
                if (item.is_number_integer() && item.get<int>() == pin)
                {
                    checked = true;
                    break;
                }
            }
        }

        QCheckBox* box = new QCheckBox(QString::number(pin));
        box->setChecked(checked);
        connect(box, &QCheckBox::toggled, this, [this, block_index, pin](bool on)
        {
            // Список пересобирается по возрастанию, а не дописывается в конец:
            // schemas/gpio.json запрещает дубликаты, а стабильный порядок ещё
            // и делает diff файла проекта читаемым.
            nlohmann::json updated = nlohmann::json::array();
            const nlohmann::json existing = document_->field(block_index, "pins");
            for (int candidate = 0; candidate <= 15; ++candidate)
            {
                bool present = false;
                if (existing.is_array())
                {
                    for (const auto& item : existing)
                    {
                        if (item.is_number_integer() && item.get<int>() == candidate)
                        {
                            present = true;
                            break;
                        }
                    }
                }
                if (candidate == pin)
                {
                    present = on;
                }
                if (present)
                {
                    updated.push_back(candidate);
                }
            }
            document_->setField(block_index, "pins", updated);
            emit documentChanged();
        });
        pins_grid->addWidget(box, pin / 8, pin % 8);
    }
    layout->addWidget(pins_box);

    QLabel* note = new QLabel(QString::fromUtf8(
        "Снятие всех флажков сделает конфигурацию невалидной: схема требует "
        "хотя бы один вывод в группе."));
    note->setWordWrap(true);
    note->setEnabled(false);
    layout->addWidget(note);

    QPushButton* disable = new QPushButton(QString::fromUtf8("Отключить блок"));
    connect(disable, &QPushButton::clicked, this, [this, instance]()
    {
        document_->removeBlocks("gpio", instance);
        gpio_block_position_ = 0;
        emit documentChanged();
        refresh();
    });
    layout->addWidget(disable);
}

void PropertiesPanel::buildUartForm(QVBoxLayout* layout, int instance)
{
    const std::vector<int> indices = document_->blockIndices("uart", instance);
    if (indices.empty())
    {
        return;
    }
    const int block_index = indices.front();

    QLabel* title = new QLabel(QString("<b>USART_%1</b>").arg(instance));
    layout->addWidget(title);

    QGroupBox* box = new QGroupBox(QString::fromUtf8("Параметры (ТЗ §3.1.3.2)"));
    QFormLayout* form = new QFormLayout(box);

    const nlohmann::json baud = document_->field(block_index, "baud_rate");
    form->addRow(QString::fromUtf8("Скорость:"), intCombo(
        kBaudRates, QString::fromUtf8(" бод"),
        baud.is_number_integer() ? baud.get<int>() : 38400,
        [this, block_index](int value)
        {
            document_->setField(block_index, "baud_rate", value);
            // Фактическая скорость и погрешность считаются в
            // ProjectDocument::validate и попадают в панель диагностики —
            // поэтому здесь достаточно сообщить об изменении.
            emit documentChanged();
        }));

    const nlohmann::json data_bits = document_->field(block_index, "data_bits");
    form->addRow(QString::fromUtf8("Бит данных:"), intCombo(
        kDataBits, QString(), data_bits.is_number_integer() ? data_bits.get<int>() : 8,
        [this, block_index](int value)
        {
            document_->setField(block_index, "data_bits", value);
            emit documentChanged();
        }));

    const nlohmann::json stop_bits = document_->field(block_index, "stop_bits");
    form->addRow(QString::fromUtf8("Стоп-бит:"), intCombo(
        kStopBits, QString(), stop_bits.is_number_integer() ? stop_bits.get<int>() : 1,
        [this, block_index](int value)
        {
            document_->setField(block_index, "stop_bits", value);
            emit documentChanged();
        }));

    form->addRow(QString::fromUtf8("Чётность:"), stringCombo(
        kParities, document_->field(block_index, "parity").is_string()
                       ? document_->field(block_index, "parity").get<std::string>() : "none",
        [this, block_index](const std::string& value)
        {
            document_->setField(block_index, "parity", value);
            emit documentChanged();
        }));

    form->addRow(QString::fromUtf8("Направление:"), stringCombo(
        kDirections, document_->field(block_index, "direction").is_string()
                       ? document_->field(block_index, "direction").get<std::string>() : "duplex",
        [this, block_index](const std::string& value)
        {
            document_->setField(block_index, "direction", value);
            emit documentChanged();
        }));

    QCheckBox* flow = new QCheckBox(QString::fromUtf8("RTS/CTS"));
    flow->setToolTip(QString::fromUtf8("Аппаратное управление потоком"));
    flow->setChecked(document_->field(block_index, "flow_control").is_boolean()
                     && document_->field(block_index, "flow_control").get<bool>());
    connect(flow, &QCheckBox::toggled, this, [this, block_index](bool on)
    {
        document_->setField(block_index, "flow_control", on);
        // Флаг меняет НАБОР занятых выводов (добавляются RTS/CTS,
        // knowledge_base/uart.json instance_fixed_pins) — схема корпуса и
        // проверка конфликтов обязаны обновиться.
        emit documentChanged();
    });
    form->addRow(QString::fromUtf8("Управление потоком:"), flow);
    layout->addWidget(box);

    // Выводы UART не выбираются: они жёстко привязаны к instance. Показать их
    // всё равно нужно — иначе конфликт с GPIO выглядит для пользователя
    // необъяснимым.
    auto fixed = knowledge_->uart.instance_fixed_pins.find(instance);
    if (fixed != knowledge_->uart.instance_fixed_pins.end())
    {
        QLabel* pins = new QLabel(QString::fromUtf8(
            "Выводы (фиксированы аппаратно): TX = GPIO_%1.%2, RX = GPIO_%3.%4")
            .arg(fixed->second.tx.gpio_instance).arg(fixed->second.tx.pin)
            .arg(fixed->second.rx.gpio_instance).arg(fixed->second.rx.pin));
        pins->setWordWrap(true);
        pins->setEnabled(false);
        layout->addWidget(pins);
    }

    QPushButton* disable = new QPushButton(QString::fromUtf8("Отключить блок"));
    connect(disable, &QPushButton::clicked, this, [this, instance]()
    {
        document_->removeBlocks("uart", instance);
        emit documentChanged();
        refresh();
    });
    layout->addWidget(disable);
}

void PropertiesPanel::buildTimer16Form(QVBoxLayout* layout, int instance)
{
    const std::vector<int> indices = document_->blockIndices("timer16", instance);
    if (indices.empty())
    {
        return;
    }
    const int block_index = indices.front();

    QLabel* title = new QLabel(QString("<b>Timer16_%1</b>").arg(instance));
    layout->addWidget(title);

    QGroupBox* box = new QGroupBox(QString::fromUtf8("Параметры (ТЗ §3.1.4)"));
    QFormLayout* form = new QFormLayout(box);

    const std::string mode = document_->field(block_index, "mode").is_string()
        ? document_->field(block_index, "mode").get<std::string>() : "period";

    form->addRow(QString::fromUtf8("Режим:"), stringCombo(
        kTimer16Modes, mode,
        [this, block_index](const std::string& value)
        {
            document_->setField(block_index, "mode", value);
            emit documentChanged();
            // Режим определяет, занимает ли блок вывод ШИМ, и меняет
            // осмысленность поля CMP — форма пересобирается.
            refresh();
        }));

    const nlohmann::json prescaler = document_->field(block_index, "prescaler");
    form->addRow(QString::fromUtf8("Предделитель:"), intCombo(
        kPrescalers, QString(), prescaler.is_number_integer() ? prescaler.get<int>() : 16,
        [this, block_index](int value)
        {
            document_->setField(block_index, "prescaler", value);
            emit documentChanged();
        }));

    // ARR и CMP — произвольные 16-битные числа, а не перечисления, поэтому
    // здесь spinbox, а не список. Ограничение CMP <= ARR (schemas/timer16.json,
    // max_field) выражено прямо в виджете: недопустимое значение просто нельзя
    // ввести, вместо того чтобы ловить его валидатором постфактум.
    QSpinBox* arr = new QSpinBox();
    arr->setRange(0, 65535);
    const nlohmann::json arr_value = document_->field(block_index, "arr");
    arr->setValue(arr_value.is_number_integer() ? arr_value.get<int>() : 999);

    QSpinBox* cmp = new QSpinBox();
    cmp->setRange(0, arr->value());
    const nlohmann::json cmp_value = document_->field(block_index, "cmp");
    cmp->setValue(cmp_value.is_number_integer() ? cmp_value.get<int>() : 499);

    connect(arr, &QSpinBox::valueChanged, this, [this, block_index, cmp](int value)
    {
        document_->setField(block_index, "arr", value);
        cmp->setMaximum(value);   // сузили ARR — CMP подтянется сам
        document_->setField(block_index, "cmp", cmp->value());
        emit documentChanged();
    });
    connect(cmp, &QSpinBox::valueChanged, this, [this, block_index](int value)
    {
        document_->setField(block_index, "cmp", value);
        emit documentChanged();
    });

    form->addRow(QString::fromUtf8("Период ARR:"), arr);
    form->addRow(QString::fromUtf8("Сравнение CMP:"), cmp);

    // Поле обязательно по ТЗ во всех режимах, но в счётном режиме HAL его не
    // принимает (HAL_Timer16_Counter_Start без Compare) — честнее сказать это
    // прямо, чем оставить пользователя гадать, почему правка ничего не меняет.
    if (mode == "external_event_count")
    {
        cmp->setEnabled(false);
        QLabel* note = new QLabel(QString::fromUtf8(
            "В режиме счёта внешних событий CMP кодогенерацией не используется "
            "(docs/implementations/timer16.md)."));
        note->setWordWrap(true);
        note->setEnabled(false);
        form->addRow(QString(), note);
    }
    layout->addWidget(box);

    // Наблюдаемая частота — то, ради чего эти три числа и вводятся. Считать её
    // в уме (32 МГц / prescaler / (ARR+1)) пользователь не обязан.
    const int prescaler_value = prescaler.is_number_integer() ? prescaler.get<int>() : 16;
    const int arr_now = arr->value();
    if (prescaler_value > 0 && arr_now >= 0)
    {
        const double frequency = 32000000.0 / prescaler_value / (arr_now + 1.0);
        const double duty = (arr_now > 0) ? (cmp->value() * 100.0 / (arr_now + 1.0)) : 0.0;
        QLabel* summary = new QLabel(QString::fromUtf8(
            "При HSI 32 МГц: частота ≈ %1 Гц, скважность ≈ %2 %")
            .arg(frequency, 0, 'f', 1).arg(duty, 0, 'f', 1));
        summary->setWordWrap(true);
        layout->addWidget(summary);
    }

    QPushButton* disable = new QPushButton(QString::fromUtf8("Отключить блок"));
    connect(disable, &QPushButton::clicked, this, [this, instance]()
    {
        document_->removeBlocks("timer16", instance);
        emit documentChanged();
        refresh();
    });
    layout->addWidget(disable);
}

void PropertiesPanel::buildI2cForm(QVBoxLayout* layout, int instance)
{
    const std::vector<int> indices = document_->blockIndices("i2c", instance);
    if (indices.empty())
    {
        return;
    }
    const int block_index = indices.front();

    QLabel* title = new QLabel(QString("<b>I2C_%1</b>").arg(instance));
    layout->addWidget(title);

    QGroupBox* box = new QGroupBox(QString::fromUtf8("Параметры"));
    QFormLayout* form = new QFormLayout(box);

    const std::string mode = document_->field(block_index, "mode").is_string()
        ? document_->field(block_index, "mode").get<std::string>() : "master";

    form->addRow(QString::fromUtf8("Режим:"), stringCombo(
        {{"Ведущий (master)", "master"}, {"Ведомый (slave)", "slave"}}, mode,
        [this, block_index](const std::string& value)
        {
            document_->setField(block_index, "mode", value);
            // Смена режима меняет и обязательные поля, и допустимый набор
            // сценариев demo (parser/i2c/i2c_parser.cpp, kValidMasterDemos /
            // kValidSlaveDemos). Поэтому сразу приводим блок к валидному для
            // нового режима виду, а не оставляем его сломанным до тех пор,
            // пока пользователь сам не догадается дозаполнить поля.
            if (value == "slave")
            {
                document_->setField(block_index, "demo", "receive");
                document_->setField(block_index, "address_mode", "7bit");
                if (!document_->field(block_index, "own_address").is_number_integer())
                {
                    document_->setField(block_index, "own_address", 0x36);
                }
            }
            else
            {
                document_->setField(block_index, "demo", "scanner");
            }
            emit documentChanged();
            refresh();
        }));

    const nlohmann::json speed = document_->field(block_index, "clock_speed");
    form->addRow(QString::fromUtf8("Частота шины:"), labelledIntCombo(
        kI2cSpeeds, speed.is_number_integer() ? speed.get<int>() : 100000,
        [this, block_index](int value)
        {
            document_->setField(block_index, "clock_speed", value);
            emit documentChanged();
        }));

    const std::string demo = document_->field(block_index, "demo").is_string()
        ? document_->field(block_index, "demo").get<std::string>()
        : (mode == "master" ? "scanner" : "receive");

    form->addRow(QString::fromUtf8("Демо-сценарий:"), stringCombo(
        mode == "master" ? kI2cMasterDemos : kI2cSlaveDemos, demo,
        [this, block_index](const std::string& value)
        {
            document_->setField(block_index, "demo", value);
            emit documentChanged();
            refresh();  // ping_pong у master добавляет поле target_address
        }));

    if (mode == "slave")
    {
        const std::string address_mode = document_->field(block_index, "address_mode").is_string()
            ? document_->field(block_index, "address_mode").get<std::string>() : "7bit";

        form->addRow(QString::fromUtf8("Адресация:"), stringCombo(
            {{"7 бит", "7bit"}, {"10 бит", "10bit"}}, address_mode,
            [this, block_index](const std::string& value)
            {
                document_->setField(block_index, "address_mode", value);
                // При переходе на 10 бит адрес <= 127 запрещён парсером:
                // HAL различает разрядность ПО ВЕЛИЧИНЕ адреса, и такой адрес
                // молча стал бы 7-битным (parser/i2c/i2c_parser.cpp).
                if (value == "10bit")
                {
                    const nlohmann::json current = document_->field(block_index, "own_address");
                    if (!current.is_number_integer() || current.get<int>() <= 127)
                    {
                        document_->setField(block_index, "own_address", 128);
                    }
                }
                emit documentChanged();
                refresh();
            }));

        QSpinBox* own = new QSpinBox();
        // Границы: I2C_ADDRESS_7BIT_MAX=0x7F, I2C_ADDRESS_10BIT_MAX=0x3FF
        // (mik32_hal_i2c.h). Нижняя для 10 бит — 128 по той же причине, что
        // и подстановка выше.
        own->setRange(address_mode == "10bit" ? 128 : 0, address_mode == "10bit" ? 1023 : 127);
        const nlohmann::json own_value = document_->field(block_index, "own_address");
        own->setValue(own_value.is_number_integer() ? own_value.get<int>()
                                                    : (address_mode == "10bit" ? 128 : 0x36));
        own->setDisplayIntegerBase(16);
        own->setPrefix("0x");
        connect(own, &QSpinBox::valueChanged, this, [this, block_index](int value)
        {
            document_->setField(block_index, "own_address", value);
            emit documentChanged();
        });
        form->addRow(QString::fromUtf8("Свой адрес:"), own);
        // Значение могло быть подтянуто в допустимый диапазон самим виджетом —
        // документ обязан узнать об этом, иначе на диск уйдёт старое число.
        document_->setField(block_index, "own_address", own->value());
    }

    if (mode == "master" && demo == "ping_pong")
    {
        QSpinBox* target = new QSpinBox();
        target->setRange(0, 127);  // parser/i2c: kMaxTargetAddress, только 7 бит
        const nlohmann::json target_value = document_->field(block_index, "target_address");
        target->setValue(target_value.is_number_integer() ? target_value.get<int>() : 0x36);
        target->setDisplayIntegerBase(16);
        target->setPrefix("0x");
        connect(target, &QSpinBox::valueChanged, this, [this, block_index](int value)
        {
            document_->setField(block_index, "target_address", value);
            emit documentChanged();
        });
        form->addRow(QString::fromUtf8("Адрес ведомого:"), target);
        document_->setField(block_index, "target_address", target->value());
    }
    layout->addWidget(box);

    if (mode == "master" && demo == "scanner")
    {
        QLabel* note = new QLabel(QString::fromUtf8(
            "Сценарий «сканер» печатает результаты в UART — в конфигурации "
            "должен быть включён хотя бы один блок USART."));
        note->setWordWrap(true);
        note->setEnabled(false);
        layout->addWidget(note);
    }

    auto fixed = knowledge_->i2c.instance_fixed_pins.find(instance);
    if (fixed != knowledge_->i2c.instance_fixed_pins.end() && !fixed->second.empty())
    {
        QString pins_text = QString::fromUtf8("Выводы (фиксированы аппаратно): ");
        for (size_t i = 0; i < fixed->second.size(); ++i)
        {
            if (i > 0)
            {
                pins_text += ", ";
            }
            pins_text += QString("GPIO_%1.%2")
                             .arg(fixed->second[i].gpio_instance).arg(fixed->second[i].pin);
        }
        QLabel* pins = new QLabel(pins_text);
        pins->setWordWrap(true);
        pins->setEnabled(false);
        layout->addWidget(pins);
    }

    QPushButton* disable = new QPushButton(QString::fromUtf8("Отключить блок"));
    connect(disable, &QPushButton::clicked, this, [this, instance]()
    {
        document_->removeBlocks("i2c", instance);
        emit documentChanged();
        refresh();
    });
    layout->addWidget(disable);
}
