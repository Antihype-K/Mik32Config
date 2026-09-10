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
#include <limits>
#include <set>

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

    // schemas/spi.json, fields.clk_phase / fields.clk_polarity.
    const std::vector<std::pair<const char*, const char*>> kSpiClkPhases = {
        {"Активна вне слова (off)",    "off"},
        {"Неактивна вне слова (on)",   "on"},
    };
    const std::vector<std::pair<const char*, const char*>> kSpiClkPolarities = {
        {"Низкий уровень (low)",  "low"},
        {"Высокий уровень (high)", "high"},
    };

    // schemas/spi.json, fields.baud_rate_div — семь ступеней деления spi_ref_clk.
    const std::vector<int> kSpiBaudRateDivs = {4, 8, 16, 32, 64, 128, 256};

    // schemas/spi.json, fields.chip_select.
    const std::vector<std::pair<const char*, const char*>> kSpiChipSelects = {
        {"CS0", "cs0"}, {"CS1", "cs1"}, {"CS2", "cs2"}, {"CS3", "cs3"},
    };

    // schemas/timer32.json, fields.count_mode; ТЗ §3.1.5 п.2.
    const std::vector<std::pair<const char*, const char*>> kTimer32CountModes = {
        {"Прямой (forward)",            "forward"},
        {"Обратный (reverse)",          "reverse"},
        {"Двунаправленный (bidirectional)", "bidirectional"},
    };

    // schemas/timer32.json, item_fields.mode (внутри channels).
    const std::vector<std::pair<const char*, const char*>> kTimer32ChannelModes = {
        {"ШИМ (pwm)",         "pwm"},
        {"Захват (capture)",  "capture"},
        {"Сравнение (compare)", "compare"},
    };

    // schemas/timer32.json, item_fields.capture_edge.
    const std::vector<std::pair<const char*, const char*>> kTimer32CaptureEdges = {
        {"Нарастающий фронт (rising)",  "rising"},
        {"Спадающий фронт (falling)",   "falling"},
    };

    // schemas/timer32.json: prescaler/top/ocr — прямые значения 32-битных
    // регистров (uint32_t, 0..4294967295), а не HAL-перечисления. QSpinBox
    // хранит значение в int (Qt API), поэтому верхняя граница здесь —
    // INT_MAX, а не полный диапазон uint32_t: значения регистра выше 2^31-1
    // на практике означают предделитель/период, растягивающий счёт на часы,
    // так что это ограничение виджета не мешает реальному использованию;
    // расхождение задокументировано в docs/implementations/timer32.md.
    constexpr int kUint32SpinBoxMax = std::numeric_limits<int>::max();

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
    else if (type_ == "spi")      buildSpiForm(layout, instance_);
    else if (type_ == "timer32")  buildTimer32Form(layout, instance_);
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

void PropertiesPanel::buildSpiForm(QVBoxLayout* layout, int instance)
{
    const std::vector<int> indices = document_->blockIndices("spi", instance);
    if (indices.empty())
    {
        return;
    }
    const int block_index = indices.front();

    QLabel* title = new QLabel(QString("<b>SPI_%1</b>").arg(instance));
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
            // master-поля (baud_rate_div/manual_cs/decoder/chip_select)
            // реальный HAL для slave не читает вовсе (mik32_hal_spi.c,
            // HAL_SPI_Init:232-241) — при переходе на master подставляем те
            // же значения, что и в defaultBlock (project_document.cpp), а не
            // оставляем блок без обязательных полей до перезаполнения руками.
            if (value == "master" && !document_->field(block_index, "chip_select").is_string())
            {
                document_->setField(block_index, "baud_rate_div", 64);
                document_->setField(block_index, "manual_cs", false);
                document_->setField(block_index, "decoder", false);
                document_->setField(block_index, "chip_select", "cs0");
            }
            emit documentChanged();
            refresh();
        }));

    const std::string clk_phase = document_->field(block_index, "clk_phase").is_string()
        ? document_->field(block_index, "clk_phase").get<std::string>() : "on";
    form->addRow(QString::fromUtf8("Фаза такта (CPHA):"), stringCombo(
        kSpiClkPhases, clk_phase,
        [this, block_index](const std::string& value)
        {
            document_->setField(block_index, "clk_phase", value);
            emit documentChanged();
        }));

    const std::string clk_polarity = document_->field(block_index, "clk_polarity").is_string()
        ? document_->field(block_index, "clk_polarity").get<std::string>() : "high";
    form->addRow(QString::fromUtf8("Полярность такта (CPOL):"), stringCombo(
        kSpiClkPolarities, clk_polarity,
        [this, block_index](const std::string& value)
        {
            document_->setField(block_index, "clk_polarity", value);
            emit documentChanged();
        }));

    QSpinBox* threshold = new QSpinBox();
    // 0-8: mik32_hal_spi.c, HAL_SPI_Init — единственная явная граница
    // (ThresholdTX > 8 -> HAL_ERROR), см. parser/spi/spi_parser.cpp.
    threshold->setRange(0, 8);
    const nlohmann::json threshold_value = document_->field(block_index, "threshold_tx");
    threshold->setValue(threshold_value.is_number_integer() ? threshold_value.get<int>() : 4);
    connect(threshold, &QSpinBox::valueChanged, this, [this, block_index](int value)
    {
        document_->setField(block_index, "threshold_tx", value);
        emit documentChanged();
    });
    form->addRow(QString::fromUtf8("Порог TX FIFO:"), threshold);
    document_->setField(block_index, "threshold_tx", threshold->value());

    if (mode == "master")
    {
        const nlohmann::json div = document_->field(block_index, "baud_rate_div");
        form->addRow(QString::fromUtf8("Делитель частоты:"), intCombo(
            kSpiBaudRateDivs, "", div.is_number_integer() ? div.get<int>() : 64,
            [this, block_index](int value)
            {
                document_->setField(block_index, "baud_rate_div", value);
                emit documentChanged();
            }));

        QCheckBox* manual_cs = new QCheckBox(QString::fromUtf8("Ручное управление CS"));
        manual_cs->setChecked(document_->field(block_index, "manual_cs").is_boolean()
            && document_->field(block_index, "manual_cs").get<bool>());
        connect(manual_cs, &QCheckBox::toggled, this, [this, block_index](bool checked)
        {
            document_->setField(block_index, "manual_cs", checked);
            emit documentChanged();
            refresh();  // может появиться/исчезнуть предупреждение об Errata п.7.1
        });
        form->addRow(QString(), manual_cs);

        QCheckBox* decoder = new QCheckBox(QString::fromUtf8("Внешний дешифратор CS"));
        decoder->setChecked(document_->field(block_index, "decoder").is_boolean()
            && document_->field(block_index, "decoder").get<bool>());
        connect(decoder, &QCheckBox::toggled, this, [this, block_index](bool checked)
        {
            document_->setField(block_index, "decoder", checked);
            emit documentChanged();
        });
        form->addRow(QString(), decoder);

        const std::string chip_select = document_->field(block_index, "chip_select").is_string()
            ? document_->field(block_index, "chip_select").get<std::string>() : "cs0";
        form->addRow(QString::fromUtf8("Выбор ведомого (CS):"), stringCombo(
            kSpiChipSelects, chip_select,
            [this, block_index](const std::string& value)
            {
                document_->setField(block_index, "chip_select", value);
                emit documentChanged();
                refresh();  // может появиться/исчезнуть предупреждение об Errata п.7.1
            }));

        // Errata MIK32V2 v1.0.1 §7.1 — тот же порог, что в project_document.cpp
        // (validate()) и codegen/project/spi_contribution.cpp (errata_7_1_applies).
        if (instance == 0 && !manual_cs->isChecked() && chip_select == "cs3")
        {
            QLabel* errata = new QLabel(QString::fromUtf8(
                "⚠ Errata п. 7.1: на SPI_0 в автоматическом режиме CS3 дублирует CS2. "
                "Включите «Ручное управление CS» для реальной работы с 4-м ведомым."));
            errata->setWordWrap(true);
            layout->addWidget(errata);
        }
    }

    layout->addWidget(box);

    auto fixed = knowledge_->spi.instance_fixed_pins.find(instance);
    if (fixed != knowledge_->spi.instance_fixed_pins.end() && !fixed->second.empty())
    {
        QString pins_text = QString::fromUtf8("Выводы SCK/MOSI/MISO/SS_IN (фиксированы аппаратно): ");
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
        document_->removeBlocks("spi", instance);
        emit documentChanged();
        refresh();
    });
    layout->addWidget(disable);
}

void PropertiesPanel::buildTimer32Form(QVBoxLayout* layout, int instance)
{
    const std::vector<int> indices = document_->blockIndices("timer32", instance);
    if (indices.empty())
    {
        return;
    }
    const int block_index = indices.front();

    QLabel* title = new QLabel(QString("<b>Timer32_%1</b>").arg(instance));
    layout->addWidget(title);

    QGroupBox* box = new QGroupBox(QString::fromUtf8("Параметры (ТЗ §3.1.5)"));
    QFormLayout* form = new QFormLayout(box);

    // Блок, только что включённый через buildDisabledPlaceholder, приходит
    // без ЭТИХ полей: gui/project_document.cpp, defaultBlock() не содержит
    // ветки type=="timer32" (расхождение зафиксировано в финальном отчёте
    // сессии). Поэтому ниже — тот же приём, что уже используется в этом
    // файле для threshold_tx/own_address/target_address: подставить
    // разумное значение по умолчанию в виджет и сразу же записать его
    // обратно в документ через setField, а не просто в конце ждать, что
    // пользователь тронет каждое поле руками.
    const std::string count_mode = document_->field(block_index, "count_mode").is_string()
        ? document_->field(block_index, "count_mode").get<std::string>() : "forward";
    form->addRow(QString::fromUtf8("Режим счёта:"), stringCombo(
        kTimer32CountModes, count_mode,
        [this, block_index](const std::string& value)
        {
            document_->setField(block_index, "count_mode", value);
            emit documentChanged();
        }));
    document_->setField(block_index, "count_mode", count_mode);

    QSpinBox* prescaler = new QSpinBox();
    // mik32_hal_timer32.h, TIMER32_ClockConfigTypeDef.Prescaler: прямое
    // значение регистра (uint32_t), не HAL-перечисление. Верхняя граница
    // виджета — kUint32SpinBoxMax (см. комментарий у константы выше).
    prescaler->setRange(0, kUint32SpinBoxMax);
    const nlohmann::json prescaler_value = document_->field(block_index, "prescaler");
    // 31 — предделитель из ручного прогона, зафиксированного в
    // docs/implementations/timer32.md, «Собрано и прогнано» (не произвольное
    // число).
    prescaler->setValue(prescaler_value.is_number_integer() ? prescaler_value.get<int>() : 31);
    connect(prescaler, &QSpinBox::valueChanged, this, [this, block_index](int value)
    {
        document_->setField(block_index, "prescaler", value);
        emit documentChanged();
    });
    form->addRow(QString::fromUtf8("Предделитель (PRESCALER):"), prescaler);
    document_->setField(block_index, "prescaler", prescaler->value());

    QSpinBox* top = new QSpinBox();
    // mik32_hal_timer32.h, TIMER32_HandleTypeDef.Top: 32-битный регистр TOP.
    top->setRange(0, kUint32SpinBoxMax);
    const nlohmann::json top_value = document_->field(block_index, "top");
    top->setValue(top_value.is_number_integer() ? top_value.get<int>() : 999);
    connect(top, &QSpinBox::valueChanged, this, [this, block_index](int value)
    {
        document_->setField(block_index, "top", value);
        emit documentChanged();
    });
    form->addRow(QString::fromUtf8("Верхняя граница счёта (TOP):"), top);
    document_->setField(block_index, "top", top->value());
    layout->addWidget(box);

    // Наблюдаемая частота счёта — mik32_hal_timer32.h: «Частота делится на
    // Prescaler + 1» (ТЗ §3.1.8: HSI 32 МГц, knowledge_->timer32.sys_clk_hz).
    const long sys_clk_hz = knowledge_->timer32.sys_clk_hz > 0
        ? knowledge_->timer32.sys_clk_hz : 32000000;
    const double frequency = static_cast<double>(sys_clk_hz) / (prescaler->value() + 1.0);
    QLabel* summary = new QLabel(QString::fromUtf8("Частота счёта ≈ %1 Гц")
        .arg(frequency, 0, 'f', 1));
    summary->setWordWrap(true);
    layout->addWidget(summary);

    if (instance == 0)
    {
        // ТЗ §3.1.5: «для Timer32_0 – только настройку счётчика» — без
        // каналов ШИМ/захвата/сравнения.
        QLabel* note = new QLabel(QString::fromUtf8(
            "Timer32_0 — базовый счётчик без каналов ШИМ/захвата/сравнения "
            "(ТЗ §3.1.5). Каналы доступны только у Timer32_1 и Timer32_2."));
        note->setWordWrap(true);
        note->setEnabled(false);
        layout->addWidget(note);
    }
    else
    {
        QGroupBox* channels_box = new QGroupBox(QString::fromUtf8(
            "Каналы (0-4, ШИМ / захват / сравнение)"));
        QVBoxLayout* channels_layout = new QVBoxLayout(channels_box);

        nlohmann::json channels = document_->field(block_index, "channels");
        if (!channels.is_array())
        {
            channels = nlohmann::json::array();
        }

        for (size_t i = 0; i < channels.size(); ++i)
        {
            buildTimer32ChannelRow(channels_layout, block_index, static_cast<int>(i), channels[i], instance);
        }

        QPushButton* add_channel = new QPushButton(QString::fromUtf8("Добавить канал"));
        // schemas/timer32.json, channels.max_length=4 — у Timer32_1/2 ровно
        // 4 канала (index 0-3), пятый добавлять некуда.
        add_channel->setEnabled(channels.size() < 4);
        connect(add_channel, &QPushButton::clicked, this, [this, block_index]()
        {
            nlohmann::json current = document_->field(block_index, "channels");
            if (!current.is_array())
            {
                current = nlohmann::json::array();
            }
            std::set<int> used;
            for (const auto& existing_channel : current)
            {
                if (existing_channel.contains("index") && existing_channel["index"].is_number_integer())
                {
                    used.insert(existing_channel["index"].get<int>());
                }
            }
            int next_index = -1;
            for (int candidate = 0; candidate <= 3; ++candidate)
            {
                if (!used.count(candidate))
                {
                    next_index = candidate;
                    break;
                }
            }
            if (next_index < 0)
            {
                return;  // кнопка задизейблена при 4 каналах — сюда попасть нельзя
            }
            nlohmann::json new_channel;
            new_channel["index"] = next_index;
            new_channel["mode"] = "pwm";
            new_channel["ocr"] = 0;
            new_channel["pwm_invert"] = false;
            current.push_back(new_channel);
            document_->setField(block_index, "channels", current);
            emit documentChanged();
            refresh();
        });
        channels_layout->addWidget(add_channel);
        layout->addWidget(channels_box);

        if (!channels.empty())
        {
            // Errata MIK32V2 v1.0.1 §9.1 уже покрыта кодогеном
            // (codegen/project/timer32_contribution.cpp генерирует
            // TIMER32_<N>_IRQHandler_Template со сбросом флага первой
            // инструкцией) — здесь только справка, в том же
            // информационном формате, что и предупреждение Errata §7.1 у
            // SPI выше в этом файле.
            QLabel* errata = new QLabel(QString::fromUtf8(
                "ⓘ Errata п. 9.1: включение канала запускает его через "
                "_IT-вариант HAL (другого пути нет), поэтому кодоген "
                "обязательно генерирует шаблон обработчика прерывания со "
                "сбросом флага первой инструкцией тела (см. "
                "docs/implementations/timer32.md)."));
            errata->setWordWrap(true);
            layout->addWidget(errata);
        }
    }

    QPushButton* disable = new QPushButton(QString::fromUtf8("Отключить блок"));
    connect(disable, &QPushButton::clicked, this, [this, instance]()
    {
        document_->removeBlocks("timer32", instance);
        emit documentChanged();
        refresh();
    });
    layout->addWidget(disable);
}

void PropertiesPanel::buildTimer32ChannelRow(QVBoxLayout* channels_layout, int block_index,
                                             int channel_position, const nlohmann::json& channel,
                                             int instance)
{
    const int channel_index = (channel.contains("index") && channel["index"].is_number_integer())
        ? channel["index"].get<int>() : channel_position;
    const std::string mode = (channel.contains("mode") && channel["mode"].is_string())
        ? channel["mode"].get<std::string>() : "pwm";

    QGroupBox* row_box = new QGroupBox(QString::fromUtf8("Канал (индекс %1)").arg(channel_index));
    QVBoxLayout* row_layout = new QVBoxLayout(row_box);

    QHBoxLayout* header_row = new QHBoxLayout();
    header_row->addWidget(new QLabel(QString::fromUtf8("Аппаратный индекс:")));

    // schemas/timer32.json, item_fields.index: 0-3, уникален внутри массива
    // "channels" (проверяется парсером, см. tests/fixtures/
    // timer32_duplicate_channel_index.json). GUI намеренно не запрещает
    // дубликат заранее — как и у GPIO (buildGpioForm выше), диагностика
    // ядра сообщит об ошибке после ввода, а не дублирует правило парсера
    // своей копией.
    const std::vector<int> kChannelIndices = {0, 1, 2, 3};
    header_row->addWidget(intCombo(kChannelIndices, QString(), channel_index,
        [this, block_index, channel_position](int value)
        {
            nlohmann::json channels = document_->field(block_index, "channels");
            if (channels.is_array() && channel_position < static_cast<int>(channels.size()))
            {
                channels[channel_position]["index"] = value;
                document_->setField(block_index, "channels", channels);
                emit documentChanged();
                refresh();  // фиксированный вывод (knowledge_->timer32) зависит от индекса
            }
        }));

    QPushButton* remove = new QPushButton(QString::fromUtf8("Удалить канал"));
    connect(remove, &QPushButton::clicked, this, [this, block_index, channel_position]()
    {
        nlohmann::json channels = document_->field(block_index, "channels");
        if (channels.is_array() && channel_position < static_cast<int>(channels.size()))
        {
            channels.erase(channels.begin() + channel_position);
            document_->setField(block_index, "channels", channels);
            emit documentChanged();
            refresh();
        }
    });
    header_row->addWidget(remove);
    row_layout->addLayout(header_row);

    QFormLayout* form = new QFormLayout();
    form->addRow(QString::fromUtf8("Режим канала:"), stringCombo(
        kTimer32ChannelModes, mode,
        [this, block_index, channel_position](const std::string& value)
        {
            nlohmann::json channels = document_->field(block_index, "channels");
            if (!channels.is_array() || channel_position >= static_cast<int>(channels.size()))
            {
                return;
            }
            channels[channel_position]["mode"] = value;
            // Подставляем минимально обязательные поля нового режима сразу
            // при смене — тот же приём, что у buildI2cForm при смене mode
            // (см. выше): иначе блок валиден на экране, но не в схеме, до
            // первой ручной правки нового поля.
            if (value == "pwm" || value == "compare")
            {
                if (!channels[channel_position].contains("ocr")
                    || !channels[channel_position]["ocr"].is_number_integer())
                {
                    channels[channel_position]["ocr"] = 0;
                }
                if (value == "pwm" && !channels[channel_position].contains("pwm_invert"))
                {
                    channels[channel_position]["pwm_invert"] = false;
                }
            }
            else if (value == "capture")
            {
                if (!channels[channel_position].contains("capture_edge")
                    || !channels[channel_position]["capture_edge"].is_string())
                {
                    channels[channel_position]["capture_edge"] = "rising";
                }
                if (!channels[channel_position].contains("noise_filter"))
                {
                    channels[channel_position]["noise_filter"] = false;
                }
            }
            document_->setField(block_index, "channels", channels);
            emit documentChanged();
            refresh();  // набор полей ниже зависит от режима именно этого канала
        }));

    if (mode == "pwm" || mode == "compare")
    {
        QSpinBox* ocr = new QSpinBox();
        ocr->setRange(0, kUint32SpinBoxMax);
        const nlohmann::json ocr_field = channel.contains("ocr") ? channel["ocr"] : nlohmann::json();
        ocr->setValue(ocr_field.is_number_integer() ? ocr_field.get<int>() : 0);
        connect(ocr, &QSpinBox::valueChanged, this, [this, block_index, channel_position](int value)
        {
            nlohmann::json channels = document_->field(block_index, "channels");
            if (channels.is_array() && channel_position < static_cast<int>(channels.size()))
            {
                channels[channel_position]["ocr"] = value;
                document_->setField(block_index, "channels", channels);
                emit documentChanged();
            }
        });
        form->addRow(QString::fromUtf8("OCR (сравнение/скважность):"), ocr);

        // Бэкфилл, как и у top-level полей: свежедобавленный канал не
        // должен оставаться без обязательного поля до первой ручной правки.
        if (!ocr_field.is_number_integer())
        {
            nlohmann::json channels = document_->field(block_index, "channels");
            if (channels.is_array() && channel_position < static_cast<int>(channels.size()))
            {
                channels[channel_position]["ocr"] = ocr->value();
                document_->setField(block_index, "channels", channels);
            }
        }
    }

    if (mode == "pwm")
    {
        QCheckBox* invert = new QCheckBox(QString::fromUtf8("Инвертировать ШИМ"));
        invert->setChecked(channel.contains("pwm_invert") && channel["pwm_invert"].is_boolean()
            && channel["pwm_invert"].get<bool>());
        connect(invert, &QCheckBox::toggled, this, [this, block_index, channel_position](bool checked)
        {
            nlohmann::json channels = document_->field(block_index, "channels");
            if (channels.is_array() && channel_position < static_cast<int>(channels.size()))
            {
                channels[channel_position]["pwm_invert"] = checked;
                document_->setField(block_index, "channels", channels);
                emit documentChanged();
            }
        });
        form->addRow(QString(), invert);
    }

    if (mode == "capture")
    {
        const std::string capture_edge = (channel.contains("capture_edge") && channel["capture_edge"].is_string())
            ? channel["capture_edge"].get<std::string>() : "rising";
        form->addRow(QString::fromUtf8("Фронт захвата:"), stringCombo(
            kTimer32CaptureEdges, capture_edge,
            [this, block_index, channel_position](const std::string& value)
            {
                nlohmann::json channels = document_->field(block_index, "channels");
                if (channels.is_array() && channel_position < static_cast<int>(channels.size()))
                {
                    channels[channel_position]["capture_edge"] = value;
                    document_->setField(block_index, "channels", channels);
                    emit documentChanged();
                }
            }));

        QCheckBox* noise = new QCheckBox(QString::fromUtf8("Фильтр помех"));
        noise->setChecked(channel.contains("noise_filter") && channel["noise_filter"].is_boolean()
            && channel["noise_filter"].get<bool>());
        connect(noise, &QCheckBox::toggled, this, [this, block_index, channel_position](bool checked)
        {
            nlohmann::json channels = document_->field(block_index, "channels");
            if (channels.is_array() && channel_position < static_cast<int>(channels.size()))
            {
                channels[channel_position]["noise_filter"] = checked;
                document_->setField(block_index, "channels", channels);
                emit documentChanged();
            }
        });
        form->addRow(QString(), noise);
    }
    row_layout->addLayout(form);

    // Вывод фиксирован датащитом на весь канал независимо от режима —
    // knowledge_base/timer32.json, instance_channel_fixed_pin (см.
    // docs/implementations/timer32.md, «Расхождения между буквой ТЗ и
    // реальным железом», п.1).
    auto per_instance = knowledge_->timer32.instance_channel_fixed_pin.find(instance);
    if (per_instance != knowledge_->timer32.instance_channel_fixed_pin.end())
    {
        auto per_channel = per_instance->second.find(channel_index);
        if (per_channel != per_instance->second.end())
        {
            QLabel* pin = new QLabel(QString::fromUtf8("Вывод (фиксирован аппаратно): GPIO_%1.%2")
                .arg(per_channel->second.gpio_instance).arg(per_channel->second.pin));
            pin->setWordWrap(true);
            pin->setEnabled(false);
            row_layout->addWidget(pin);
        }
    }

    channels_layout->addWidget(row_box);
}
