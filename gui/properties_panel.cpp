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
#include "freertos_settings_widget.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
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

    // schemas/timer16.json: "clock_source" — Тир 1 аудита (AUDIT_TZ_PERIPHERY_GAPS.md).
    const std::vector<std::pair<const char*, const char*>> kTimer16ClockSources = {
        {"Системная (sys_clk)",                 "internal_system"},
        {"Шина AHB (hclk)",                      "internal_ahb"},
        {"Внешний осциллятор OSC32M",            "internal_osc32m"},
        {"Встроенный осциллятор HSI32M",         "internal_hsi32m"},
        {"Внешний осциллятор OSC32K",            "internal_osc32k"},
        {"Встроенный осциллятор LSI32K",         "internal_lsi32k"},
    };

    const std::vector<std::pair<const char*, const char*>> kTimer16TriggerEdges = {
        {"Программный (неактивен)",  "software"},
        {"Нарастающий",              "rising"},
        {"Спадающий",                "falling"},
        {"Оба фронта",               "both"},
    };

    const std::vector<std::pair<const char*, const char*>> kTimer16Filters = {
        {"Выключен",   "none"},
        {"2 такта",    "2clock"},
        {"4 такта",    "4clock"},
        {"8 тактов",   "8clock"},
    };

    // parser/i2c/i2c_parser.cpp, kValidClockSpeeds — Standard/Fast/Fast+.
    const std::vector<std::pair<const char*, int>> kI2cSpeeds = {
        {"100 кГц (Standard)",  100000},
        {"400 кГц (Fast)",      400000},
        {"1 МГц (Fast+)",       1000000},
    };

    // mik32_hal_i2c.h, HAL_I2C_DigitalFilterTypeDef.
    const std::vector<std::pair<const char*, int>> kI2cDigitalFilters = {
        {"Выключен",      0},
        {"1 такт",        1},
        {"2 такта",       2},
        {"15 тактов",     15},
    };

    // mik32_hal_i2c.h, HAL_I2C_OwnAddress2MaskTypeDef.
    const std::vector<std::pair<const char*, const char*>> kI2cOwnAddress2Masks = {
        {"Точное совпадение",      "none"},
        {"Маска 1 бит (x)",        "111111x"},
        {"Маска 2 бита (xx)",      "11111xx"},
        {"Маска 3 бита (xxx)",     "1111xxx"},
        {"Маска 4 бита (xxxx)",    "111xxxx"},
        {"Маска 5 бит (xxxxx)",    "11xxxxx"},
        {"Маска 6 бит (xxxxxx)",   "1xxxxxx"},
        {"ACK все адреса",         "1111111"},
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

    // FreeRTOS — не элемент peripherals[] (type+instance), а верхнеуровневый
    // ключ документа "freertos" (как "clock" у ClockConfigScreen), поэтому
    // document_->isEnabled("freertos", 0) всегда вернул бы false и показал
    // бы заглушку "включить блок" с кнопкой, пишущей несуществующий блок
    // peripherals[] — обходим общий гейт isEnabled ДО него, не трогая
    // остальные типы. Включение/выключение самого FreeRTOS — чекбокс
    // "Включить FreeRTOS" внутри FreeRtosSettingsWidget, как и раньше внутри
    // ProjectManagerScreen.
    if (type_ == "freertos")
    {
        buildFreertosForm(layout);
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
    else if (type_ == "rtc")          buildRtcForm(layout);
    else if (type_ == "crc")          buildCrcForm(layout);
    else if (type_ == "eeprom")       buildEepromForm(layout);
    else if (type_ == "wdt")          buildWdtForm(layout);
    else if (type_ == "dac")          buildDacForm(layout, instance_);
    else if (type_ == "adc")          buildAdcForm(layout);
    else if (type_ == "crypto")       buildCryptoForm(layout);
    else if (type_ == "otp")          buildOtpForm(layout);
    else if (type_ == "tsens")        buildTsensForm(layout);
    else if (type_ == "pvd")          buildPvdForm(layout);
    else if (type_ == "bus_watchdog") buildBusWatchdogForm(layout);
    else if (type_ == "dma")          buildDmaForm(layout, instance_);
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

    QGroupBox* box = new QGroupBox(QString::fromUtf8("Параметры"));
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

    // DMA (ТЗ §3.1.3.3) — backend уже полностью реализован (parser/USART/
    // usart_parser.cpp валидирует массив "dma", codegen/project/
    // uart_contribution.cpp генерирует реальную привязку канала) с самой
    // первой сессии до сегодняшней ночи — здесь только GUI-обёртка поверх
    // уже существующих JSON-полей, без изобретения новых. Направление
    // (tx требует direction=tx/duplex и т.п.) парсер проверяет сам —
    // ошибка несовместимости появится в панели диагностики, GUI её не
    // дублирует.
    QGroupBox* dma_box = new QGroupBox(QString::fromUtf8("DMA"));
    QFormLayout* dma_form = new QFormLayout(dma_box);

    auto buildDmaDirectionRow = [this, block_index](const char* label, const std::string& direction, int default_channel) -> QWidget*
    {
        const nlohmann::json dma = document_->field(block_index, "dma");
        int existing_index = -1;
        if (dma.is_array())
        {
            for (size_t i = 0; i < dma.size(); ++i)
            {
                if (dma[i].value("direction", std::string()) == direction) { existing_index = static_cast<int>(i); break; }
            }
        }

        QWidget* row = new QWidget();
        QHBoxLayout* row_layout = new QHBoxLayout(row);
        row_layout->setContentsMargins(0, 0, 0, 0);

        QCheckBox* enable = new QCheckBox(QString::fromUtf8(label));
        enable->setChecked(existing_index >= 0);
        row_layout->addWidget(enable);

        QSpinBox* channel = new QSpinBox();
        channel->setRange(0, 3);  // dma_config.h, DMA_CHANNEL_COUNT=4
        channel->setPrefix(QString::fromUtf8("канал "));
        channel->setValue(existing_index >= 0 && dma[existing_index].contains("channel")
                               ? dma[existing_index]["channel"].get<int>() : default_channel);
        channel->setEnabled(existing_index >= 0);
        row_layout->addWidget(channel);
        row_layout->addStretch();

        QObject::connect(enable, &QCheckBox::toggled, this, [this, block_index, direction, channel](bool on)
        {
            nlohmann::json dma_array = document_->field(block_index, "dma");
            if (!dma_array.is_array())
            {
                dma_array = nlohmann::json::array();
            }
            for (auto it = dma_array.begin(); it != dma_array.end(); )
            {
                if (it->value("direction", std::string()) == direction) it = dma_array.erase(it);
                else ++it;
            }
            if (on)
            {
                // Значения buffer_type/buffer_size_bytes/element_size/increment —
                // безопасные дефолты (linear, 64 байта, byte, с инкрементом) по
                // образцу docs/examples/mik32-examples/HAL_USART_DMA — пользователь
                // может их не трогать, но правка требует прямого редактирования
                // JSON (не выведены отдельными виджетами в этой сессии).
                nlohmann::json entry;
                entry["channel"] = channel->value();
                entry["direction"] = direction;
                entry["buffer_type"] = "linear";
                entry["buffer_size_bytes"] = 64;
                entry["element_size"] = "byte";
                entry["increment"] = true;
                dma_array.push_back(entry);
            }
            document_->setField(block_index, "dma", dma_array);
            channel->setEnabled(on);
            emit documentChanged();
        });

        QObject::connect(channel, &QSpinBox::valueChanged, this, [this, block_index, direction](int value)
        {
            nlohmann::json dma_array = document_->field(block_index, "dma");
            if (!dma_array.is_array())
            {
                return;
            }
            for (auto& entry : dma_array)
            {
                if (entry.value("direction", std::string()) == direction)
                {
                    entry["channel"] = value;
                }
            }
            document_->setField(block_index, "dma", dma_array);
            emit documentChanged();
        });

        return row;
    };

    dma_form->addRow(buildDmaDirectionRow("Передача (TX)", "tx", 0));
    dma_form->addRow(buildDmaDirectionRow("Приём (RX)", "rx", 1));
    layout->addWidget(dma_box);

    // OVERNIGHT_FIELD_REFERENCE.md — новые необязательные поля этой сессии.
    // Все значения по умолчанию здесь (lsb_first/duplex/async/...) совпадают
    // с HAL-дефолтами — отсутствие поля в JSON ведёт к тому же поведению.
    QGroupBox* extra_box = new QGroupBox(QString::fromUtf8("Дополнительные параметры"));
    QFormLayout* extra_form = new QFormLayout(extra_box);

    const std::string bit_direction = document_->field(block_index, "bit_direction").is_string()
        ? document_->field(block_index, "bit_direction").get<std::string>() : "lsb_first";
    extra_form->addRow(QString::fromUtf8("Порядок бит:"), stringCombo(
        {{"LSB первым", "lsb_first"}, {"MSB первым", "msb_first"}}, bit_direction,
        [this, block_index](const std::string& value)
        {
            document_->setField(block_index, "bit_direction", value);
            emit documentChanged();
        }));

    const std::string channel_mode = document_->field(block_index, "channel_mode").is_string()
        ? document_->field(block_index, "channel_mode").get<std::string>() : "duplex";
    extra_form->addRow(QString::fromUtf8("Режим линии:"), stringCombo(
        {{"Дуплекс", "duplex"}, {"Однопроводной полудуплекс", "half_duplex"}}, channel_mode,
        [this, block_index](const std::string& value)
        {
            document_->setField(block_index, "channel_mode", value);
            emit documentChanged();
        }));

    QWidget* inversion_row = new QWidget();
    QHBoxLayout* inversion_layout = new QHBoxLayout(inversion_row);
    inversion_layout->setContentsMargins(0, 0, 0, 0);
    QCheckBox* tx_inv = new QCheckBox(QString::fromUtf8("Инверсия TX"));
    tx_inv->setChecked(document_->field(block_index, "tx_inversion").is_boolean()
                       && document_->field(block_index, "tx_inversion").get<bool>());
    connect(tx_inv, &QCheckBox::toggled, this, [this, block_index](bool on)
    {
        document_->setField(block_index, "tx_inversion", on);
        emit documentChanged();
    });
    QCheckBox* rx_inv = new QCheckBox(QString::fromUtf8("Инверсия RX"));
    rx_inv->setChecked(document_->field(block_index, "rx_inversion").is_boolean()
                       && document_->field(block_index, "rx_inversion").get<bool>());
    connect(rx_inv, &QCheckBox::toggled, this, [this, block_index](bool on)
    {
        document_->setField(block_index, "rx_inversion", on);
        emit documentChanged();
    });
    QCheckBox* swap = new QCheckBox(QString::fromUtf8("Swap TX/RX"));
    swap->setChecked(document_->field(block_index, "swap").is_boolean()
                     && document_->field(block_index, "swap").get<bool>());
    connect(swap, &QCheckBox::toggled, this, [this, block_index](bool on)
    {
        document_->setField(block_index, "swap", on);
        emit documentChanged();
    });
    inversion_layout->addWidget(tx_inv);
    inversion_layout->addWidget(rx_inv);
    inversion_layout->addWidget(swap);
    inversion_layout->addStretch();
    extra_form->addRow(QString::fromUtf8("Полярность/обмен линий:"), inversion_row);

    // Синхронный режим: ЗАПРЕЩЁН на instance=1 (Errata MIK32V2 v1.0.1 §10.1 —
    // паразитный сигнал CK на PORT2_6, тот же пин, где на 1T Rex Board
    // светодиод; parser/USART/usart_parser.cpp отклоняет эту комбинацию с
    // явной ошибкой). Если instance=1 — переключатель не показываем вовсе,
    // чтобы не провоцировать пользователя выбрать заведомо отклоняемую
    // парсером комбинацию.
    if (instance != 1)
    {
        const bool is_synchronous = document_->field(block_index, "mode").is_string()
            && document_->field(block_index, "mode").get<std::string>() == "synchronous";

        QCheckBox* sync_check = new QCheckBox(QString::fromUtf8("Синхронный режим (с тактовым выходом)"));
        sync_check->setChecked(is_synchronous);
        extra_form->addRow(QString::fromUtf8("Режим:"), sync_check);

        const std::string xck_mode = document_->field(block_index, "xck_mode").is_string()
            ? document_->field(block_index, "xck_mode").get<std::string>() : "mode0";
        QComboBox* xck_combo = stringCombo(
            {{"XCK mode 0", "mode0"}, {"XCK mode 1", "mode1"}, {"XCK mode 2", "mode2"}, {"XCK mode 3", "mode3"}},
            xck_mode,
            [this, block_index](const std::string& value)
            {
                document_->setField(block_index, "xck_mode", value);
                emit documentChanged();
            });
        xck_combo->setEnabled(is_synchronous);
        extra_form->addRow(QString::fromUtf8("Режим тактового сигнала:"), xck_combo);

        QCheckBox* last_byte = new QCheckBox(QString::fromUtf8("Тактовый импульс на последнем байте"));
        last_byte->setChecked(document_->field(block_index, "last_byte_clock").is_boolean()
                              && document_->field(block_index, "last_byte_clock").get<bool>());
        last_byte->setEnabled(is_synchronous);
        connect(last_byte, &QCheckBox::toggled, this, [this, block_index](bool on)
        {
            document_->setField(block_index, "last_byte_clock", on);
            emit documentChanged();
        });
        extra_form->addRow(QString::fromUtf8(""), last_byte);

        connect(sync_check, &QCheckBox::toggled, this, [this, block_index, xck_combo, last_byte](bool on)
        {
            document_->setField(block_index, "mode", on ? std::string("synchronous") : std::string("asynchronous"));
            xck_combo->setEnabled(on);
            last_byte->setEnabled(on);
            emit documentChanged();
        });
    }
    else
    {
        QLabel* sync_blocked = new QLabel(QString::fromUtf8(
            "Синхронный режим недоступен для USART_1: errata МИК32V2 — паразитный "
            "сигнал CK на PORT2_6 (там же светодиод на 1T Rex Board)."));
        sync_blocked->setWordWrap(true);
        sync_blocked->setEnabled(false);
        extra_form->addRow(sync_blocked);
    }

    const std::string rts_mode = document_->field(block_index, "rts_mode").is_string()
        ? document_->field(block_index, "rts_mode").get<std::string>() : "always_enabled";
    QComboBox* rts_mode_combo = stringCombo(
        {{"Всегда включён", "always_enabled"}, {"Модемный режим", "modem"}}, rts_mode,
        [this, block_index](const std::string& value)
        {
            document_->setField(block_index, "rts_mode", value);
            emit documentChanged();
        });
    // rts_mode имеет смысл только при включённом RTS/CTS (flow чекбокс выше).
    rts_mode_combo->setEnabled(flow->isChecked());
    connect(flow, &QCheckBox::toggled, rts_mode_combo, &QComboBox::setEnabled);
    extra_form->addRow(QString::fromUtf8("Режим RTS:"), rts_mode_combo);

    QGroupBox* modem_box = new QGroupBox(QString::fromUtf8("Модемные сигналы (RI/DSR/DTR/DDIS/DCD)"));
    QHBoxLayout* modem_layout = new QHBoxLayout(modem_box);
    const nlohmann::json modem_signals = document_->field(block_index, "modem_signals");
    const char* modem_fields[] = {"ri", "dsr", "dtr", "ddis", "dcd"};
    const char* modem_labels[] = {"RI", "DSR", "DTR", "DDIS", "DCD"};
    for (int i = 0; i < 5; ++i)
    {
        const char* field_name = modem_fields[i];
        QCheckBox* box_check = new QCheckBox(QString::fromUtf8(modem_labels[i]));
        box_check->setChecked(modem_signals.is_object() && modem_signals.contains(field_name)
                              && modem_signals[field_name].is_boolean() && modem_signals[field_name].get<bool>());
        connect(box_check, &QCheckBox::toggled, this, [this, block_index, field_name](bool on)
        {
            nlohmann::json modem_json = document_->field(block_index, "modem_signals");
            if (!modem_json.is_object())
            {
                modem_json = nlohmann::json::object();
            }
            modem_json[field_name] = on;
            document_->setField(block_index, "modem_signals", modem_json);
            emit documentChanged();
        });
        modem_layout->addWidget(box_check);
    }
    modem_layout->addStretch();
    extra_form->addRow(modem_box);

    layout->addWidget(extra_box);

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

    QGroupBox* box = new QGroupBox(QString::fromUtf8("Параметры"));
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

    // OVERNIGHT_FIELD_REFERENCE.md: два взаимоисключающих способа задать
    // период/скважность — либо prescaler+arr+cmp (регистры напрямую), либо
    // frequency_hz+duty_percent (парсер сам подбирает регистры). Переключатель
    // здесь определяет, какую пару полей показывать и писать в документ;
    // backend (parser/timer16/timer16_parser.cpp) требует, чтобы в JSON
    // присутствовал РОВНО один из двух наборов — eraseField убирает поля
    // прежнего режима целиком, а не обнуляет их.
    const bool has_frequency_duty = document_->field(block_index, "frequency_hz").is_number();

    QComboBox* input_mode = stringCombo(
        {{"Регистры (предделитель/ARR/CMP)", "registers"}, {"Частота и скважность (Гц/%)", "frequency"}},
        has_frequency_duty ? "frequency" : "registers",
        [this, block_index](const std::string& value)
        {
            if (value == "frequency")
            {
                document_->eraseField(block_index, "prescaler");
                document_->eraseField(block_index, "arr");
                document_->eraseField(block_index, "cmp");
                if (!document_->field(block_index, "frequency_hz").is_number())
                {
                    document_->setField(block_index, "frequency_hz", 1000);
                    document_->setField(block_index, "duty_percent", 50);
                }
            }
            else
            {
                document_->eraseField(block_index, "frequency_hz");
                document_->eraseField(block_index, "duty_percent");
                if (!document_->field(block_index, "prescaler").is_number_integer())
                {
                    document_->setField(block_index, "prescaler", 16);
                    document_->setField(block_index, "arr", 999);
                    document_->setField(block_index, "cmp", 499);
                }
            }
            emit documentChanged();
            refresh();
        });
    form->addRow(QString::fromUtf8("Способ задания:"), input_mode);

    if (has_frequency_duty)
    {
        QDoubleSpinBox* freq = new QDoubleSpinBox();
        freq->setRange(0.01, 32000000.0);
        freq->setSuffix(QString::fromUtf8(" Гц"));
        freq->setDecimals(2);
        const nlohmann::json freq_value = document_->field(block_index, "frequency_hz");
        freq->setValue(freq_value.is_number() ? freq_value.get<double>() : 1000.0);
        connect(freq, &QDoubleSpinBox::valueChanged, this, [this, block_index](double value)
        {
            document_->setField(block_index, "frequency_hz", value);
            emit documentChanged();
        });
        form->addRow(QString::fromUtf8("Частота:"), freq);

        QDoubleSpinBox* duty = new QDoubleSpinBox();
        duty->setRange(0.0, 100.0);
        duty->setSuffix(QString::fromUtf8(" %"));
        duty->setDecimals(1);
        const nlohmann::json duty_value = document_->field(block_index, "duty_percent");
        duty->setValue(duty_value.is_number() ? duty_value.get<double>() : 50.0);
        connect(duty, &QDoubleSpinBox::valueChanged, this, [this, block_index](double value)
        {
            document_->setField(block_index, "duty_percent", value);
            emit documentChanged();
        });
        form->addRow(QString::fromUtf8("Скважность:"), duty);

        QLabel* note = new QLabel(QString::fromUtf8(
            "Фактическая частота/скважность после округления делителя — в панели "
            "диагностики снизу (строка \"запрошено .../фактически ...\")."));
        note->setWordWrap(true);
        note->setEnabled(false);
        form->addRow(QString(), note);
    }
    else
    {
        const nlohmann::json prescaler = document_->field(block_index, "prescaler");
        QComboBox* prescaler_combo = intCombo(
            kPrescalers, QString(), prescaler.is_number_integer() ? prescaler.get<int>() : 16,
            [this, block_index](int value)
            {
                document_->setField(block_index, "prescaler", value);
                emit documentChanged();
            });
        form->addRow(QString::fromUtf8("Предделитель:"), prescaler_combo);

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

        form->addRow(QString::fromUtf8("Период ARR:"), arr);
        form->addRow(QString::fromUtf8("Сравнение CMP:"), cmp);

        // Поле обязательно во всех режимах, но в счётном режиме HAL его не
        // принимает (HAL_Timer16_Counter_Start без Compare) — честнее сказать это
        // прямо, чем оставить пользователя гадать, почему правка ничего не меняет.
        if (mode == "external_event_count")
        {
            cmp->setEnabled(false);
            QLabel* note = new QLabel(QString::fromUtf8(
                "В режиме счёта внешних событий CMP кодогенерацией не используется."));
            note->setWordWrap(true);
            note->setEnabled(false);
            form->addRow(QString(), note);
        }

        // Наблюдаемая частота — то, ради чего эти три числа и вводятся. Считать её
        // в уме (32 МГц / prescaler / (ARR+1)) пользователь не обязан.
        // ВАЖНО: это должно пересчитываться вживую при каждой правке, а не один
        // раз на момент построения формы — иначе подпись молча расходится с
        // реальными значениями, как только пользователь меняет хоть одно из
        // трёх полей (MainWindow::refreshEverything не перестраивает панель
        // свойств на documentChanged, только дерево/диагностику/схему корпуса).
        QLabel* summary = new QLabel();
        summary->setWordWrap(true);
        form->addRow(QString(), summary);

        auto updateSummary = [prescaler_combo, arr, cmp, summary]()
        {
            const int prescaler_now = prescaler_combo->currentData().toInt();
            const int arr_now = arr->value();
            if (prescaler_now <= 0)
            {
                summary->clear();
                return;
            }
            const double frequency = 32000000.0 / prescaler_now / (arr_now + 1.0);
            const double duty = (arr_now > 0) ? (cmp->value() * 100.0 / (arr_now + 1.0)) : 0.0;
            summary->setText(QString::fromUtf8("При HSI 32 МГц: частота ≈ %1 Гц, скважность ≈ %2 %")
                .arg(frequency, 0, 'f', 1).arg(duty, 0, 'f', 1));
        };
        updateSummary();

        connect(prescaler_combo, &QComboBox::currentIndexChanged, this, [updateSummary](int)
        {
            updateSummary();
        });
        connect(arr, &QSpinBox::valueChanged, this, [this, block_index, cmp, updateSummary](int value)
        {
            document_->setField(block_index, "arr", value);
            cmp->setMaximum(value);   // сузили ARR — CMP подтянется сам
            document_->setField(block_index, "cmp", cmp->value());
            updateSummary();
            emit documentChanged();
        });
        connect(cmp, &QSpinBox::valueChanged, this, [this, block_index, updateSummary](int value)
        {
            document_->setField(block_index, "cmp", value);
            updateSummary();
            emit documentChanged();
        });
    }
    layout->addWidget(box);

    // --- Тир 1 аудита: clock_source/trigger_edge/фильтры/полярность ------
    QGroupBox* advanced_box = new QGroupBox(QString::fromUtf8("Дополнительно"));
    QFormLayout* advanced_form = new QFormLayout(advanced_box);

    const bool is_external_event_count = (mode == "external_event_count");

    const std::string clock_source = document_->field(block_index, "clock_source").is_string()
        ? document_->field(block_index, "clock_source").get<std::string>() : "internal_system";
    QComboBox* clock_source_combo = stringCombo(
        kTimer16ClockSources, clock_source,
        [this, block_index](const std::string& value)
        {
            document_->setField(block_index, "clock_source", value);
            emit documentChanged();
        });
    clock_source_combo->setEnabled(!is_external_event_count);
    clock_source_combo->setToolTip(is_external_event_count
        ? QString::fromUtf8("В режиме счёта внешних событий источник всегда EXTERNAL_INPUT1 — поле не читается кодогеном.")
        : QString::fromUtf8("Частота ШИМ в комментарии вычисляется только для системной/AHB/HSI32M "
                            "(совпадают с sys_clk=32МГц по умолчанию) — для внешних/некалиброванных "
                            "осцилляторов частота не зафиксирована."));
    advanced_form->addRow(QString::fromUtf8("Источник тактирования:"), clock_source_combo);

    const std::string trigger_edge = document_->field(block_index, "trigger_edge").is_string()
        ? document_->field(block_index, "trigger_edge").get<std::string>() : "software";
    advanced_form->addRow(QString::fromUtf8("Фронт триггера:"), stringCombo(
        kTimer16TriggerEdges, trigger_edge,
        [this, block_index](const std::string& value)
        {
            document_->setField(block_index, "trigger_edge", value);
            emit documentChanged();
        }));

    const std::string external_clock_filter = document_->field(block_index, "external_clock_filter").is_string()
        ? document_->field(block_index, "external_clock_filter").get<std::string>() : "none";
    advanced_form->addRow(QString::fromUtf8("Фильтр внешнего такта:"), stringCombo(
        kTimer16Filters, external_clock_filter,
        [this, block_index](const std::string& value)
        {
            document_->setField(block_index, "external_clock_filter", value);
            emit documentChanged();
        }));

    const std::string trigger_filter = document_->field(block_index, "trigger_filter").is_string()
        ? document_->field(block_index, "trigger_filter").get<std::string>() : "none";
    advanced_form->addRow(QString::fromUtf8("Фильтр триггера:"), stringCombo(
        kTimer16Filters, trigger_filter,
        [this, block_index](const std::string& value)
        {
            document_->setField(block_index, "trigger_filter", value);
            emit documentChanged();
        }));

    QCheckBox* waveform_inverted = new QCheckBox(QString::fromUtf8("Инвертировать полярность ШИМ"));
    waveform_inverted->setChecked(document_->field(block_index, "waveform_inverted").is_boolean()
                                  && document_->field(block_index, "waveform_inverted").get<bool>());
    waveform_inverted->setEnabled(!is_external_event_count);
    if (is_external_event_count)
    {
        waveform_inverted->setToolTip(QString::fromUtf8(
            "В режиме счёта внешних событий волновая форма не генерируется (Waveform.Enable=DISABLE) "
            "— поле не влияет на сгенерированный код."));
    }
    connect(waveform_inverted, &QCheckBox::toggled, this, [this, block_index](bool on)
    {
        document_->setField(block_index, "waveform_inverted", on);
        emit documentChanged();
    });
    advanced_form->addRow(QString::fromUtf8(""), waveform_inverted);

    layout->addWidget(advanced_box);

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
            // Смена режима меняет обязательные поля (parser/i2c/i2c_parser.cpp)
            // — сразу приводим блок к валидному для нового режима виду, а не
            // оставляем его сломанным до тех пор, пока пользователь сам не
            // догадается дозаполнить поля.
            if (value == "slave")
            {
                document_->setField(block_index, "address_mode", "7bit");
                if (!document_->field(block_index, "own_address").is_number_integer())
                {
                    document_->setField(block_index, "own_address", 0x36);
                }
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

    // digital_filter/analog_filter — общие для master и slave
    // (HAL_I2C_{Digital,Analog}FilterInit вызываются из обеих веток
    // HAL_I2C_Init, см. schemas/i2c.json).
    const nlohmann::json digital_filter = document_->field(block_index, "digital_filter");
    form->addRow(QString::fromUtf8("Цифровой фильтр:"), labelledIntCombo(
        kI2cDigitalFilters, digital_filter.is_number_integer() ? digital_filter.get<int>() : 0,
        [this, block_index](int value)
        {
            document_->setField(block_index, "digital_filter", value);
            emit documentChanged();
        }));

    QCheckBox* analog_filter = new QCheckBox(QString::fromUtf8("Аналоговый фильтр"));
    analog_filter->setChecked(document_->field(block_index, "analog_filter").is_boolean()
                              && document_->field(block_index, "analog_filter").get<bool>());
    connect(analog_filter, &QCheckBox::toggled, this, [this, block_index](bool on)
    {
        document_->setField(block_index, "analog_filter", on);
        emit documentChanged();
    });
    form->addRow(QString::fromUtf8("Фильтры:"), analog_filter);

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
                // молча стал бы 7-битным (parser/i2c/i2c_parser.cpp). Второй
                // адрес (dual_address) требует 7-битный основной — выключаем
                // его при переходе на 10 бит, иначе блок стал бы невалидным.
                if (value == "10bit")
                {
                    const nlohmann::json current = document_->field(block_index, "own_address");
                    if (!current.is_number_integer() || current.get<int>() <= 127)
                    {
                        document_->setField(block_index, "own_address", 128);
                    }
                    document_->setField(block_index, "dual_address_enabled", false);
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

        QCheckBox* general_call = new QCheckBox(QString::fromUtf8("Общий вызов (General Call, адрес 0x00)"));
        general_call->setChecked(document_->field(block_index, "general_call").is_boolean()
                                 && document_->field(block_index, "general_call").get<bool>());
        connect(general_call, &QCheckBox::toggled, this, [this, block_index](bool on)
        {
            document_->setField(block_index, "general_call", on);
            emit documentChanged();
        });
        form->addRow(QString::fromUtf8("Ведомый:"), general_call);

        QCheckBox* no_stretch = new QCheckBox(QString::fromUtf8("Без растягивания SCL (NOSTRETCH)"));
        no_stretch->setToolTip(QString::fromUtf8(
            "Errata п. 4.1: без прерываний/DMA (этот кодоген генерирует только "
            "блокирующие вызовы) возможна недозагрузка TXDR."));
        no_stretch->setChecked(document_->field(block_index, "no_stretch_mode").is_boolean()
                               && document_->field(block_index, "no_stretch_mode").get<bool>());
        connect(no_stretch, &QCheckBox::toggled, this, [this, block_index](bool on)
        {
            document_->setField(block_index, "no_stretch_mode", on);
            emit documentChanged();
        });
        form->addRow(QString::fromUtf8(""), no_stretch);

        if (address_mode == "7bit")
        {
            QCheckBox* dual_address = new QCheckBox(QString::fromUtf8("Второй собственный адрес"));
            const bool dual_address_enabled = document_->field(block_index, "dual_address_enabled").is_boolean()
                                              && document_->field(block_index, "dual_address_enabled").get<bool>();
            dual_address->setChecked(dual_address_enabled);
            connect(dual_address, &QCheckBox::toggled, this, [this, block_index](bool on)
            {
                document_->setField(block_index, "dual_address_enabled", on);
                if (on && !document_->field(block_index, "own_address_2").is_number_integer())
                {
                    document_->setField(block_index, "own_address_2", 0x37);
                }
                emit documentChanged();
                refresh();
            });
            form->addRow(QString::fromUtf8(""), dual_address);

            if (dual_address_enabled)
            {
                QSpinBox* own2 = new QSpinBox();
                own2->setRange(0, 127);  // второй адрес всегда 7-битный
                const nlohmann::json own2_value = document_->field(block_index, "own_address_2");
                own2->setValue(own2_value.is_number_integer() ? own2_value.get<int>() : 0x37);
                own2->setDisplayIntegerBase(16);
                own2->setPrefix("0x");
                connect(own2, &QSpinBox::valueChanged, this, [this, block_index](int value)
                {
                    document_->setField(block_index, "own_address_2", value);
                    emit documentChanged();
                });
                form->addRow(QString::fromUtf8("Второй адрес:"), own2);
                document_->setField(block_index, "own_address_2", own2->value());

                const std::string mask = document_->field(block_index, "own_address_2_mask").is_string()
                    ? document_->field(block_index, "own_address_2_mask").get<std::string>() : "none";
                form->addRow(QString::fromUtf8("Маска второго адреса:"), stringCombo(
                    kI2cOwnAddress2Masks, mask,
                    [this, block_index](const std::string& value)
                    {
                        document_->setField(block_index, "own_address_2_mask", value);
                        emit documentChanged();
                    }));
            }
        }
    }

    layout->addWidget(box);

    auto fixed = knowledge_->i2c.instance_fixed_pins.find(instance);
    if (fixed != knowledge_->i2c.instance_fixed_pins.end() && !fixed->second.empty())
    {
        // SDA/SCL по отдельности НЕ названы намеренно — датащит и HAL
        // подтверждают только пару пинов, но не то, какой из двух физически
        // SDA, а какой SCL (knowledge_base/i2c.json, _comment_instance_fixed_pins).
        // Называть один из них "SDA" было бы невыдуманным по форме, но
        // выдуманным по содержанию значением — поэтому показываем пару как
        // пару, с явной пометкой, почему роли не различены.
        QString pins_text = QString::fromUtf8("Выводы I2C (SDA/SCL, фиксированы аппаратно): ");
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
        pins->setToolTip(QString::fromUtf8(
            "Какой из двух выводов физически SDA, а какой SCL — датащит МИК32 "
            "и HAL не называют по отдельности (есть только маска обоих пинов "
            "в одном вызове HAL_GPIO_Init). Уточнить можно по схеме/шелкографии "
            "конкретной платы."));
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

    QGroupBox* box = new QGroupBox(QString::fromUtf8("Параметры"));
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

    // OVERNIGHT_FIELD_REFERENCE.md: frequency_hz — альтернатива prescaler+top,
    // взаимоисключающая (тот же приём переключателя, что и у Timer16 выше).
    const bool has_frequency = document_->field(block_index, "frequency_hz").is_number();

    QComboBox* input_mode = stringCombo(
        {{"Регистры (PRESCALER/TOP)", "registers"}, {"Частота (Гц)", "frequency"}},
        has_frequency ? "frequency" : "registers",
        [this, block_index](const std::string& value)
        {
            if (value == "frequency")
            {
                document_->eraseField(block_index, "prescaler");
                document_->eraseField(block_index, "top");
                if (!document_->field(block_index, "frequency_hz").is_number())
                {
                    document_->setField(block_index, "frequency_hz", 1000);
                }
            }
            else
            {
                document_->eraseField(block_index, "frequency_hz");
                if (!document_->field(block_index, "prescaler").is_number_integer())
                {
                    document_->setField(block_index, "prescaler", 31);
                    document_->setField(block_index, "top", 999);
                }
            }
            emit documentChanged();
            refresh();
        });
    form->addRow(QString::fromUtf8("Способ задания:"), input_mode);

    if (has_frequency)
    {
        QDoubleSpinBox* freq = new QDoubleSpinBox();
        freq->setRange(0.01, 32000000.0);
        freq->setSuffix(QString::fromUtf8(" Гц"));
        freq->setDecimals(2);
        const nlohmann::json freq_value = document_->field(block_index, "frequency_hz");
        freq->setValue(freq_value.is_number() ? freq_value.get<double>() : 1000.0);
        connect(freq, &QDoubleSpinBox::valueChanged, this, [this, block_index](double value)
        {
            document_->setField(block_index, "frequency_hz", value);
            emit documentChanged();
        });
        form->addRow(QString::fromUtf8("Частота:"), freq);

        QLabel* note = new QLabel(QString::fromUtf8(
            "PRESCALER фиксируется в 0 (максимальное разрешение TOP). Фактическая "
            "частота после округления — в панели диагностики снизу."));
        note->setWordWrap(true);
        note->setEnabled(false);
        form->addRow(QString(), note);
        layout->addWidget(box);
    }
    else
    {
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
        form->addRow(QString::fromUtf8("Предделитель (PRESCALER):"), prescaler);
        document_->setField(block_index, "prescaler", prescaler->value());

        QSpinBox* top = new QSpinBox();
        // mik32_hal_timer32.h, TIMER32_HandleTypeDef.Top: 32-битный регистр TOP.
        top->setRange(0, kUint32SpinBoxMax);
        const nlohmann::json top_value = document_->field(block_index, "top");
        top->setValue(top_value.is_number_integer() ? top_value.get<int>() : 999);
        form->addRow(QString::fromUtf8("Верхняя граница счёта (TOP):"), top);
        document_->setField(block_index, "top", top->value());
        layout->addWidget(box);

        // Наблюдаемая частота счёта и частота ШИМ-периода — mik32_hal_timer32.h:
        // «Частота делится на Prescaler + 1», период ШИМ = TOP+1 тактов (та же
        // идея, что и у Timer16 выше). ВАЖНО: пересчитывается вживую при каждой
        // правке — MainWindow::refreshEverything не перестраивает эту панель на
        // documentChanged (см. тот же комментарий у Timer16 выше).
        const long sys_clk_hz = knowledge_->timer32.sys_clk_hz > 0
            ? knowledge_->timer32.sys_clk_hz : 32000000;
        QLabel* summary = new QLabel();
        summary->setWordWrap(true);
        layout->addWidget(summary);

        auto updateSummary = [sys_clk_hz, prescaler, top, summary]()
        {
            const double count_freq = static_cast<double>(sys_clk_hz) / (prescaler->value() + 1.0);
            const double period_freq = count_freq / (static_cast<double>(top->value()) + 1.0);
            summary->setText(QString::fromUtf8(
                "Частота счёта ≈ %1 Гц, частота периода ШИМ (с учётом TOP) ≈ %2 Гц")
                .arg(count_freq, 0, 'f', 1).arg(period_freq, 0, 'f', 3));
        };
        updateSummary();

        connect(prescaler, &QSpinBox::valueChanged, this, [this, block_index, updateSummary](int value)
        {
            document_->setField(block_index, "prescaler", value);
            updateSummary();
            emit documentChanged();
        });
        connect(top, &QSpinBox::valueChanged, this, [this, block_index, updateSummary](int value)
        {
            document_->setField(block_index, "top", value);
            updateSummary();
            emit documentChanged();
        });
    }

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
                "сбросом флага первой инструкцией тела."));
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

    if (mode == "compare")
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
        form->addRow(QString::fromUtf8("OCR (порог сравнения):"), ocr);

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
        // OVERNIGHT_FIELD_REFERENCE.md: duty_percent — альтернатива ocr,
        // взаимоисключающая, только для mode=pwm. Парсер (не GUI) вычисляет
        // ocr = round((top_родителя+1) * duty_percent/100) — здесь top
        // родительского Timer32Config неизвестен без повторного парсинга, и
        // вычислять его тут не нужно: достаточно записать "duty_percent" в
        // JSON, остальное сделает parser/timer32/timer32_parser.cpp при
        // следующей валидации/генерации.
        const bool has_duty = channel.contains("duty_percent") && channel["duty_percent"].is_number();

        QComboBox* duty_mode = stringCombo(
            {{"OCR (регистр)", "ocr"}, {"Скважность (%)", "duty_percent"}},
            has_duty ? "duty_percent" : "ocr",
            [this, block_index, channel_position](const std::string& value)
            {
                nlohmann::json channels = document_->field(block_index, "channels");
                if (!channels.is_array() || channel_position >= static_cast<int>(channels.size()))
                {
                    return;
                }
                if (value == "duty_percent")
                {
                    channels[channel_position].erase("ocr");
                    if (!channels[channel_position].contains("duty_percent"))
                    {
                        channels[channel_position]["duty_percent"] = 50;
                    }
                }
                else
                {
                    channels[channel_position].erase("duty_percent");
                    if (!channels[channel_position].contains("ocr"))
                    {
                        channels[channel_position]["ocr"] = 0;
                    }
                }
                document_->setField(block_index, "channels", channels);
                emit documentChanged();
                refresh();
            });
        form->addRow(QString::fromUtf8("Способ задания:"), duty_mode);

        if (has_duty)
        {
            QDoubleSpinBox* duty = new QDoubleSpinBox();
            duty->setRange(0.0, 100.0);
            duty->setSuffix(QString::fromUtf8(" %"));
            duty->setDecimals(1);
            const nlohmann::json duty_value = channel["duty_percent"];
            duty->setValue(duty_value.is_number() ? duty_value.get<double>() : 50.0);
            connect(duty, &QDoubleSpinBox::valueChanged, this, [this, block_index, channel_position](double value)
            {
                nlohmann::json channels = document_->field(block_index, "channels");
                if (channels.is_array() && channel_position < static_cast<int>(channels.size()))
                {
                    channels[channel_position]["duty_percent"] = value;
                    document_->setField(block_index, "channels", channels);
                    emit documentChanged();
                }
            });
            form->addRow(QString::fromUtf8("Скважность:"), duty);
        }
        else
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
            form->addRow(QString::fromUtf8("OCR (скважность):"), ocr);

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

// ============================================================================
// Тир 2/3 (AUDIT_TZ_PERIPHERY_GAPS.md) — RTC/CRC/EEPROM/WDT/DAC/ADC/CRYPTO/
// OTP/TSENS/PVD/BusWatchdog. Все, кроме DAC, единственные глобальные блоки:
// document_->blockIndices(type, 0) ищет их ровно так же, как остальные формы
// выше ищут "свой" block_index — instance всегда 0 (см. properties_panel.h).
// ============================================================================

void PropertiesPanel::buildRtcForm(QVBoxLayout* layout)
{
    const std::vector<int> indices = document_->blockIndices("rtc", 0);
    if (indices.empty())
    {
        return;
    }
    const int block_index = indices.front();

    QLabel* title = new QLabel(QString::fromUtf8("<b>RTC</b>"));
    layout->addWidget(title);

    // schemas/rtc.json: time.dow — RTC_WEEKDAY_MONDAY(1)..SUNDAY(7), не 0-6.
    const std::vector<std::pair<const char*, int>> kRtcWeekdays = {
        {"Понедельник", 1}, {"Вторник", 2}, {"Среда", 3}, {"Четверг", 4},
        {"Пятница", 5}, {"Суббота", 6}, {"Воскресенье", 7},
    };

    auto buildTimeDateFields = [this](QFormLayout* form, const nlohmann::json& time_value,
        const nlohmann::json& date_value, const std::function<void(const nlohmann::json&)>& on_time_change,
        const std::function<void(const nlohmann::json&)>& on_date_change)
    {
        nlohmann::json time = time_value.is_object() ? time_value
            : nlohmann::json{{"dow", 1}, {"hours", 0}, {"minutes", 0}, {"seconds", 0}};
        nlohmann::json date = date_value.is_object() ? date_value
            : nlohmann::json{{"century", 20}, {"day", 1}, {"month", 1}, {"year", 0}};

        form->addRow(QString::fromUtf8("День недели:"), labelledIntCombo(
            {{"Понедельник", 1}, {"Вторник", 2}, {"Среда", 3}, {"Четверг", 4},
             {"Пятница", 5}, {"Суббота", 6}, {"Воскресенье", 7}},
            time.value("dow", 1),
            [time, on_time_change](int value) mutable { time["dow"] = value; on_time_change(time); }));

        QSpinBox* hours = new QSpinBox(); hours->setRange(0, 23);
        hours->setValue(time.value("hours", 0));
        connect(hours, &QSpinBox::valueChanged, this, [time, on_time_change](int value) mutable
        { time["hours"] = value; on_time_change(time); });
        form->addRow(QString::fromUtf8("Часы:"), hours);

        QSpinBox* minutes = new QSpinBox(); minutes->setRange(0, 59);
        minutes->setValue(time.value("minutes", 0));
        connect(minutes, &QSpinBox::valueChanged, this, [time, on_time_change](int value) mutable
        { time["minutes"] = value; on_time_change(time); });
        form->addRow(QString::fromUtf8("Минуты:"), minutes);

        QSpinBox* seconds = new QSpinBox(); seconds->setRange(0, 59);
        seconds->setValue(time.value("seconds", 0));
        connect(seconds, &QSpinBox::valueChanged, this, [time, on_time_change](int value) mutable
        { time["seconds"] = value; on_time_change(time); });
        form->addRow(QString::fromUtf8("Секунды:"), seconds);

        QSpinBox* century = new QSpinBox(); century->setRange(0, 99);
        century->setValue(date.value("century", 20));
        connect(century, &QSpinBox::valueChanged, this, [date, on_date_change](int value) mutable
        { date["century"] = value; on_date_change(date); });
        form->addRow(QString::fromUtf8("Век (20=19xx/20xx):"), century);

        QSpinBox* year = new QSpinBox(); year->setRange(0, 99);
        year->setValue(date.value("year", 0));
        connect(year, &QSpinBox::valueChanged, this, [date, on_date_change](int value) mutable
        { date["year"] = value; on_date_change(date); });
        form->addRow(QString::fromUtf8("Год (2 цифры):"), year);

        QSpinBox* month = new QSpinBox(); month->setRange(1, 12);
        month->setValue(date.value("month", 1));
        connect(month, &QSpinBox::valueChanged, this, [date, on_date_change](int value) mutable
        { date["month"] = value; on_date_change(date); });
        form->addRow(QString::fromUtf8("Месяц:"), month);

        QSpinBox* day = new QSpinBox(); day->setRange(1, 31);
        day->setValue(date.value("day", 1));
        connect(day, &QSpinBox::valueChanged, this, [date, on_date_change](int value) mutable
        { date["day"] = value; on_date_change(date); });
        form->addRow(QString::fromUtf8("День:"), day);
    };

    QGroupBox* time_box = new QGroupBox(QString::fromUtf8("Текущее время/дата"));
    QFormLayout* time_form = new QFormLayout(time_box);
    buildTimeDateFields(time_form, document_->field(block_index, "time"), document_->field(block_index, "date"),
        [this, block_index](const nlohmann::json& v) { document_->setField(block_index, "time", v); emit documentChanged(); },
        [this, block_index](const nlohmann::json& v) { document_->setField(block_index, "date", v); emit documentChanged(); });
    layout->addWidget(time_box);

    const bool has_alarm = document_->field(block_index, "alarm").is_object();
    QCheckBox* alarm_enable = new QCheckBox(QString::fromUtf8("Будильник"));
    alarm_enable->setChecked(has_alarm);
    connect(alarm_enable, &QCheckBox::toggled, this, [this, block_index](bool on)
    {
        if (on)
        {
            document_->setField(block_index, "alarm", nlohmann::json{
                {"time", {{"dow", 1}, {"hours", 0}, {"minutes", 0}, {"seconds", 0}}},
                {"date", {{"century", 20}, {"day", 1}, {"month", 1}, {"year", 0}}}});
        }
        else
        {
            document_->eraseField(block_index, "alarm");
        }
        emit documentChanged();
        refresh();
    });
    layout->addWidget(alarm_enable);

    if (has_alarm)
    {
        const nlohmann::json alarm = document_->field(block_index, "alarm");
        QGroupBox* alarm_box = new QGroupBox(QString::fromUtf8("Время/дата будильника"));
        QFormLayout* alarm_form = new QFormLayout(alarm_box);
        buildTimeDateFields(alarm_form, alarm.value("time", nlohmann::json()), alarm.value("date", nlohmann::json()),
            [this, block_index](const nlohmann::json& v)
            { nlohmann::json a = document_->field(block_index, "alarm"); a["time"] = v; document_->setField(block_index, "alarm", a); emit documentChanged(); },
            [this, block_index](const nlohmann::json& v)
            { nlohmann::json a = document_->field(block_index, "alarm"); a["date"] = v; document_->setField(block_index, "alarm", a); emit documentChanged(); });

        QCheckBox* interrupt = new QCheckBox(QString::fromUtf8("Прерывание по будильнику"));
        interrupt->setChecked(alarm.value("interrupt_enabled", false));
        interrupt->setToolTip(QString::fromUtf8(
            "EPIC-маскирование (HAL_EPIC_MaskLevelSet(HAL_EPIC_RTC_MASK)) и подключение "
            "RTC_IRQHandler_Template() к raw_trap_handler остаются за вами."));
        connect(interrupt, &QCheckBox::toggled, this, [this, block_index](bool on)
        {
            nlohmann::json a = document_->field(block_index, "alarm");
            a["interrupt_enabled"] = on;
            document_->setField(block_index, "alarm", a);
            emit documentChanged();
        });
        alarm_form->addRow(QString(), interrupt);
        layout->addWidget(alarm_box);

        QLabel* compare_note = new QLabel(QString::fromUtf8(
            "compare_fields не редактируется здесь — по умолчанию сравниваются все 8 "
            "полей (точное совпадение). Для частичного сравнения (например, \"каждый день "
            "в это время\") отредактируйте JSON-файл проекта вручную."));
        compare_note->setWordWrap(true);
        compare_note->setEnabled(false);
        layout->addWidget(compare_note);
    }

    QPushButton* disable = new QPushButton(QString::fromUtf8("Отключить блок"));
    connect(disable, &QPushButton::clicked, this, [this]()
    {
        document_->removeBlocks("rtc", 0);
        emit documentChanged();
        refresh();
    });
    layout->addWidget(disable);
}

void PropertiesPanel::buildCrcForm(QVBoxLayout* layout)
{
    const std::vector<int> indices = document_->blockIndices("crc", 0);
    if (indices.empty())
    {
        return;
    }
    const int block_index = indices.front();

    QLabel* title = new QLabel(QString::fromUtf8("<b>CRC</b>"));
    layout->addWidget(title);

    QGroupBox* box = new QGroupBox(QString::fromUtf8("Параметры"));
    QFormLayout* form = new QFormLayout(box);

    const bool use_preset = document_->field(block_index, "preset").is_string();
    QComboBox* source_mode = stringCombo(
        {{"Готовый пресет", "preset"}, {"Свои параметры (poly/init/...)", "raw"}},
        use_preset ? "preset" : "raw",
        [this, block_index](const std::string& value)
        {
            if (value == "preset")
            {
                document_->eraseField(block_index, "poly");
                document_->eraseField(block_index, "init");
                document_->eraseField(block_index, "input_reverse");
                document_->eraseField(block_index, "output_reverse");
                document_->eraseField(block_index, "output_inversion");
                document_->setField(block_index, "preset", "crc32");
            }
            else
            {
                document_->eraseField(block_index, "preset");
                document_->setField(block_index, "poly", 0x04C11DB7);
                document_->setField(block_index, "init", 0xFFFFFFFF);
                document_->setField(block_index, "input_reverse", true);
                document_->setField(block_index, "output_reverse", true);
                document_->setField(block_index, "output_inversion", true);
            }
            emit documentChanged();
            refresh();
        });
    form->addRow(QString::fromUtf8("Источник параметров:"), source_mode);

    if (use_preset)
    {
        const std::string preset = document_->field(block_index, "preset").get<std::string>();
        form->addRow(QString::fromUtf8("Пресет:"), stringCombo(
            {{"CRC-32", "crc32"}, {"CRC-32/BZIP2", "crc32_bzip2"}, {"CRC-32C", "crc32c"},
             {"CRC-32D", "crc32d"}, {"CRC-32/JAMCRC", "crc32_jamcrc"}, {"CRC-32/MPEG-2", "crc32_mpeg2"},
             {"CRC-32/POSIX", "crc32_posix"}, {"CRC-32Q", "crc32q"}, {"CRC-32/XFER", "crc32_xfer"}},
            preset,
            [this, block_index](const std::string& value)
            {
                document_->setField(block_index, "preset", value);
                emit documentChanged();
            }));
    }
    else
    {
        QSpinBox* poly = new QSpinBox(); poly->setRange(0, kUint32SpinBoxMax);
        poly->setDisplayIntegerBase(16); poly->setPrefix("0x");
        const nlohmann::json poly_value = document_->field(block_index, "poly");
        poly->setValue(poly_value.is_number_integer() ? poly_value.get<int>() : 0x04C11DB7);
        connect(poly, &QSpinBox::valueChanged, this, [this, block_index](int value)
        { document_->setField(block_index, "poly", static_cast<unsigned int>(value)); emit documentChanged(); });
        form->addRow(QString::fromUtf8("Poly:"), poly);

        QSpinBox* init = new QSpinBox(); init->setRange(0, kUint32SpinBoxMax);
        init->setDisplayIntegerBase(16); init->setPrefix("0x");
        const nlohmann::json init_value = document_->field(block_index, "init");
        init->setValue(init_value.is_number_integer() ? init_value.get<int>() : -1);
        connect(init, &QSpinBox::valueChanged, this, [this, block_index](int value)
        { document_->setField(block_index, "init", static_cast<unsigned int>(value)); emit documentChanged(); });
        form->addRow(QString::fromUtf8("Init:"), init);

        QCheckBox* input_reverse = new QCheckBox(QString::fromUtf8("Input reverse"));
        input_reverse->setChecked(document_->field(block_index, "input_reverse").is_boolean()
            && document_->field(block_index, "input_reverse").get<bool>());
        connect(input_reverse, &QCheckBox::toggled, this, [this, block_index](bool on)
        { document_->setField(block_index, "input_reverse", on); emit documentChanged(); });
        form->addRow(QString(), input_reverse);

        QCheckBox* output_reverse = new QCheckBox(QString::fromUtf8("Output reverse"));
        output_reverse->setChecked(document_->field(block_index, "output_reverse").is_boolean()
            && document_->field(block_index, "output_reverse").get<bool>());
        connect(output_reverse, &QCheckBox::toggled, this, [this, block_index](bool on)
        { document_->setField(block_index, "output_reverse", on); emit documentChanged(); });
        form->addRow(QString(), output_reverse);

        QCheckBox* output_inversion = new QCheckBox(QString::fromUtf8("Output inversion (XorOut)"));
        output_inversion->setChecked(document_->field(block_index, "output_inversion").is_boolean()
            && document_->field(block_index, "output_inversion").get<bool>());
        connect(output_inversion, &QCheckBox::toggled, this, [this, block_index](bool on)
        { document_->setField(block_index, "output_inversion", on); emit documentChanged(); });
        form->addRow(QString(), output_inversion);
    }

    layout->addWidget(box);

    QPushButton* disable = new QPushButton(QString::fromUtf8("Отключить блок"));
    connect(disable, &QPushButton::clicked, this, [this]()
    {
        document_->removeBlocks("crc", 0);
        emit documentChanged();
        refresh();
    });
    layout->addWidget(disable);
}

void PropertiesPanel::buildEepromForm(QVBoxLayout* layout)
{
    const std::vector<int> indices = document_->blockIndices("eeprom", 0);
    if (indices.empty())
    {
        return;
    }
    const int block_index = indices.front();

    QLabel* title = new QLabel(QString::fromUtf8("<b>EEPROM</b>"));
    layout->addWidget(title);

    QGroupBox* box = new QGroupBox(QString::fromUtf8("Параметры"));
    QFormLayout* form = new QFormLayout(box);

    const std::string mode = document_->field(block_index, "mode").is_string()
        ? document_->field(block_index, "mode").get<std::string>() : "two_stage";
    form->addRow(QString::fromUtf8("Режим:"), stringCombo(
        {{"2 этапа", "two_stage"}, {"3 этапа (с опросом BSY)", "three_stage"}}, mode,
        [this, block_index](const std::string& value)
        { document_->setField(block_index, "mode", value); emit documentChanged(); }));

    QCheckBox* ecc = new QCheckBox(QString::fromUtf8("Коррекция ошибок (ECC)"));
    const nlohmann::json ecc_value = document_->field(block_index, "ecc_enabled");
    ecc->setChecked(!ecc_value.is_boolean() || ecc_value.get<bool>());
    connect(ecc, &QCheckBox::toggled, this, [this, block_index](bool on)
    { document_->setField(block_index, "ecc_enabled", on); emit documentChanged(); });
    form->addRow(QString(), ecc);

    QCheckBox* serr = new QCheckBox(QString::fromUtf8("Прерывание по одиночной ошибке ECC"));
    serr->setChecked(document_->field(block_index, "single_error_interrupt_enabled").is_boolean()
        && document_->field(block_index, "single_error_interrupt_enabled").get<bool>());
    connect(serr, &QCheckBox::toggled, this, [this, block_index](bool on)
    { document_->setField(block_index, "single_error_interrupt_enabled", on); emit documentChanged(); });
    form->addRow(QString(), serr);

    layout->addWidget(box);

    QPushButton* disable = new QPushButton(QString::fromUtf8("Отключить блок"));
    connect(disable, &QPushButton::clicked, this, [this]()
    {
        document_->removeBlocks("eeprom", 0);
        emit documentChanged();
        refresh();
    });
    layout->addWidget(disable);
}

void PropertiesPanel::buildWdtForm(QVBoxLayout* layout)
{
    const std::vector<int> indices = document_->blockIndices("wdt", 0);
    if (indices.empty())
    {
        return;
    }
    const int block_index = indices.front();

    QLabel* title = new QLabel(QString::fromUtf8("<b>WDT</b>"));
    layout->addWidget(title);

    QLabel* safety_note = new QLabel(QString::fromUtf8(
        "Кодоген генерирует только HAL_WDT_Init — HAL_WDT_Start/Refresh никогда не "
        "вызываются автоматически (автозапуск без гарантии регулярного Refresh в вашем "
        "коде привёл бы к безусловному циклическому сбросу платы)."));
    safety_note->setWordWrap(true);
    safety_note->setEnabled(false);
    layout->addWidget(safety_note);

    QGroupBox* box = new QGroupBox(QString::fromUtf8("Параметры"));
    QFormLayout* form = new QFormLayout(box);

    const std::string clock_source = document_->field(block_index, "clock_source").is_string()
        ? document_->field(block_index, "clock_source").get<std::string>() : "osc32k";
    form->addRow(QString::fromUtf8("Источник тактирования:"), stringCombo(
        {{"OSC32M", "osc32m"}, {"HSI32M", "hsi32m"}, {"OSC32K", "osc32k"}, {"LSI32K", "lsi32k"}},
        clock_source,
        [this, block_index](const std::string& value)
        { document_->setField(block_index, "clock_source", value); emit documentChanged(); }));

    const bool is_32m = (clock_source == "osc32m" || clock_source == "hsi32m");
    QSpinBox* reload_ms = new QSpinBox();
    // mik32_hal_wdt.h: 524 мс максимум для источников 32 МГц, 511875 мс для 32 кГц —
    // межполевая проверка, парсер валидирует точнее (parser/wdt/wdt_parser.cpp).
    reload_ms->setRange(1, is_32m ? 524 : 511875);
    const nlohmann::json reload_value = document_->field(block_index, "reload_ms");
    reload_ms->setValue(reload_value.is_number_integer() ? reload_value.get<int>() : 1000);
    reload_ms->setSuffix(QString::fromUtf8(" мс"));
    connect(reload_ms, &QSpinBox::valueChanged, this, [this, block_index](int value)
    { document_->setField(block_index, "reload_ms", value); emit documentChanged(); });
    form->addRow(QString::fromUtf8("Время до сброса:"), reload_ms);

    layout->addWidget(box);

    QPushButton* disable = new QPushButton(QString::fromUtf8("Отключить блок"));
    connect(disable, &QPushButton::clicked, this, [this]()
    {
        document_->removeBlocks("wdt", 0);
        emit documentChanged();
        refresh();
    });
    layout->addWidget(disable);
}

void PropertiesPanel::buildDacForm(QVBoxLayout* layout, int instance)
{
    const std::vector<int> indices = document_->blockIndices("dac", instance);
    if (indices.empty())
    {
        return;
    }
    const int block_index = indices.front();

    QLabel* title = new QLabel(QString("<b>DAC_%1</b>").arg(instance));
    layout->addWidget(title);

    QGroupBox* box = new QGroupBox(QString::fromUtf8("Параметры"));
    QFormLayout* form = new QFormLayout(box);

    QSpinBox* div = new QSpinBox(); div->setRange(0, 255);
    const nlohmann::json div_value = document_->field(block_index, "div");
    div->setValue(div_value.is_number_integer() ? div_value.get<int>() : 0);
    connect(div, &QSpinBox::valueChanged, this, [this, block_index](int value)
    { document_->setField(block_index, "div", value); emit documentChanged(); });
    form->addRow(QString::fromUtf8("Делитель (DIV):"), div);

    QCheckBox* ext_ref = new QCheckBox(QString::fromUtf8("Внешний ИОН (ext_ref)"));
    ext_ref->setChecked(document_->field(block_index, "ext_ref").is_boolean()
        && document_->field(block_index, "ext_ref").get<bool>());
    connect(ext_ref, &QCheckBox::toggled, this, [this, block_index](bool on)
    { document_->setField(block_index, "ext_ref", on); emit documentChanged(); });
    form->addRow(QString(), ext_ref);

    QCheckBox* ext_clb = new QCheckBox(QString::fromUtf8("Внешний вывод DAC_REF (ext_clb)"));
    ext_clb->setToolTip(QString::fromUtf8(
        "Вывод DAC_REF=GPIO_1/PIN_11 занимается только когда ext_ref И ext_clb оба включены."));
    ext_clb->setChecked(document_->field(block_index, "ext_clb").is_boolean()
        && document_->field(block_index, "ext_clb").get<bool>());
    connect(ext_clb, &QCheckBox::toggled, this, [this, block_index](bool on)
    { document_->setField(block_index, "ext_clb", on); emit documentChanged(); });
    form->addRow(QString(), ext_clb);

    layout->addWidget(box);

    QPushButton* disable = new QPushButton(QString::fromUtf8("Отключить блок"));
    connect(disable, &QPushButton::clicked, this, [this, instance]()
    {
        document_->removeBlocks("dac", instance);
        emit documentChanged();
        refresh();
    });
    layout->addWidget(disable);
}

void PropertiesPanel::buildAdcForm(QVBoxLayout* layout)
{
    const std::vector<int> indices = document_->blockIndices("adc", 0);
    if (indices.empty())
    {
        return;
    }
    const int block_index = indices.front();

    QLabel* title = new QLabel(QString::fromUtf8("<b>ADC</b>"));
    layout->addWidget(title);

    QGroupBox* box = new QGroupBox(QString::fromUtf8("Параметры"));
    QFormLayout* form = new QFormLayout(box);

    const nlohmann::json channel_value = document_->field(block_index, "channel");
    form->addRow(QString::fromUtf8("Начальный канал:"), labelledIntCombo(
        {{"Канал 0 (PORT_1_5)", 0}, {"Канал 1 (PORT_1_7)", 1}, {"Канал 2 (PORT_0_2)", 2},
         {"Канал 3 (PORT_0_4)", 3}, {"Канал 4 (PORT_0_7)", 4}, {"Канал 5 (PORT_0_9)", 5},
         {"Канал 6 (PORT_0_11)", 6}, {"Канал 7 (PORT_0_13)", 7}},
        channel_value.is_number_integer() ? channel_value.get<int>() : 0,
        [this, block_index](int value)
        { document_->setField(block_index, "channel", value); emit documentChanged(); }));

    QCheckBox* ext_ref = new QCheckBox(QString::fromUtf8("Внешний ИОН (ext_ref)"));
    ext_ref->setChecked(document_->field(block_index, "ext_ref").is_boolean()
        && document_->field(block_index, "ext_ref").get<bool>());
    connect(ext_ref, &QCheckBox::toggled, this, [this, block_index](bool on)
    { document_->setField(block_index, "ext_ref", on); emit documentChanged(); });
    form->addRow(QString(), ext_ref);

    QCheckBox* ext_clb = new QCheckBox(QString::fromUtf8("Внешний вывод ADC_REF (ext_clb)"));
    ext_clb->setChecked(document_->field(block_index, "ext_clb").is_boolean()
        && document_->field(block_index, "ext_clb").get<bool>());
    connect(ext_clb, &QCheckBox::toggled, this, [this, block_index](bool on)
    { document_->setField(block_index, "ext_clb", on); emit documentChanged(); });
    form->addRow(QString(), ext_clb);

    layout->addWidget(box);

    QLabel* note = new QLabel(QString::fromUtf8(
        "Переключение на другие каналы в рантайме остаётся за вами — здесь задаётся "
        "только начальный канал при инициализации."));
    note->setWordWrap(true);
    note->setEnabled(false);
    layout->addWidget(note);

    QPushButton* disable = new QPushButton(QString::fromUtf8("Отключить блок"));
    connect(disable, &QPushButton::clicked, this, [this]()
    {
        document_->removeBlocks("adc", 0);
        emit documentChanged();
        refresh();
    });
    layout->addWidget(disable);
}

void PropertiesPanel::buildCryptoForm(QVBoxLayout* layout)
{
    const std::vector<int> indices = document_->blockIndices("crypto", 0);
    if (indices.empty())
    {
        return;
    }
    const int block_index = indices.front();

    QLabel* title = new QLabel(QString::fromUtf8("<b>CRYPTO</b>"));
    layout->addWidget(title);

    QLabel* security_note = new QLabel(QString::fromUtf8(
        "Ключ и вектор инициализации (IV) сознательно не настраиваются здесь — это "
        "секретные данные времени выполнения, их нельзя хранить в JSON-конфиге или "
        "генерировать в исходном коде. Задайте их сами через HAL_Crypto_SetKey/SetIV."));
    security_note->setWordWrap(true);
    security_note->setEnabled(false);
    layout->addWidget(security_note);

    QGroupBox* box = new QGroupBox(QString::fromUtf8("Параметры"));
    QFormLayout* form = new QFormLayout(box);

    const std::string algorithm = document_->field(block_index, "algorithm").is_string()
        ? document_->field(block_index, "algorithm").get<std::string>() : "aes";
    form->addRow(QString::fromUtf8("Алгоритм:"), stringCombo(
        {{"Кузнечик", "kuznechik"}, {"Магма", "magma"}, {"AES", "aes"}}, algorithm,
        [this, block_index](const std::string& value)
        { document_->setField(block_index, "algorithm", value); emit documentChanged(); }));

    const std::string cipher_mode = document_->field(block_index, "cipher_mode").is_string()
        ? document_->field(block_index, "cipher_mode").get<std::string>() : "cbc";
    form->addRow(QString::fromUtf8("Режим шифрования:"), stringCombo(
        {{"ECB", "ecb"}, {"CBC", "cbc"}, {"CTR", "ctr"}}, cipher_mode,
        [this, block_index](const std::string& value)
        { document_->setField(block_index, "cipher_mode", value); emit documentChanged(); }));

    const std::string swap_mode = document_->field(block_index, "swap_mode").is_string()
        ? document_->field(block_index, "swap_mode").get<std::string>() : "none";
    form->addRow(QString::fromUtf8("Перестановка байт:"), stringCombo(
        {{"Нет", "none"}, {"Полуслово", "halfword"}, {"Байт", "byte"}, {"Бит", "bit"}}, swap_mode,
        [this, block_index](const std::string& value)
        { document_->setField(block_index, "swap_mode", value); emit documentChanged(); }));

    const std::string order_mode = document_->field(block_index, "order_mode").is_string()
        ? document_->field(block_index, "order_mode").get<std::string>() : "lsw";
    form->addRow(QString::fromUtf8("Порядок слов:"), stringCombo(
        {{"LSW (младшее слово первым)", "lsw"}, {"MSW (старшее слово первым)", "msw"}}, order_mode,
        [this, block_index](const std::string& value)
        { document_->setField(block_index, "order_mode", value); emit documentChanged(); }));

    layout->addWidget(box);

    QPushButton* disable = new QPushButton(QString::fromUtf8("Отключить блок"));
    connect(disable, &QPushButton::clicked, this, [this]()
    {
        document_->removeBlocks("crypto", 0);
        emit documentChanged();
        refresh();
    });
    layout->addWidget(disable);
}

void PropertiesPanel::buildOtpForm(QVBoxLayout* layout)
{
    const std::vector<int> indices = document_->blockIndices("otp", 0);
    if (indices.empty())
    {
        return;
    }
    const int block_index = indices.front();

    QLabel* title = new QLabel(QString::fromUtf8("<b>OTP</b>"));
    layout->addWidget(title);

    QLabel* safety_note = new QLabel(QString::fromUtf8(
        "Кодоген генерирует только HAL_OTP_Init (режим чтения) — запись в OTP никогда "
        "не генерируется автоматически: запись физически необратима (прожигает биты)."));
    safety_note->setWordWrap(true);
    safety_note->setEnabled(false);
    layout->addWidget(safety_note);

    QGroupBox* box = new QGroupBox(QString::fromUtf8("Параметры"));
    QFormLayout* form = new QFormLayout(box);

    const std::string read_mode = document_->field(block_index, "read_mode").is_string()
        ? document_->field(block_index, "read_mode").get<std::string>() : "two_stages";
    form->addRow(QString::fromUtf8("Режим чтения:"), stringCombo(
        {{"2 этапа (такты ожидания APB)", "two_stages"}, {"3 этапа (опрос флага BSY)", "three_stages"}},
        read_mode,
        [this, block_index](const std::string& value)
        { document_->setField(block_index, "read_mode", value); emit documentChanged(); }));

    layout->addWidget(box);

    QPushButton* disable = new QPushButton(QString::fromUtf8("Отключить блок"));
    connect(disable, &QPushButton::clicked, this, [this]()
    {
        document_->removeBlocks("otp", 0);
        emit documentChanged();
        refresh();
    });
    layout->addWidget(disable);
}

void PropertiesPanel::buildTsensForm(QVBoxLayout* layout)
{
    const std::vector<int> indices = document_->blockIndices("tsens", 0);
    if (indices.empty())
    {
        return;
    }
    const int block_index = indices.front();

    QLabel* title = new QLabel(QString::fromUtf8("<b>TSENS</b>"));
    layout->addWidget(title);

    QGroupBox* box = new QGroupBox(QString::fromUtf8("Параметры"));
    QFormLayout* form = new QFormLayout(box);

    const std::string clock_source = document_->field(block_index, "clock_source").is_string()
        ? document_->field(block_index, "clock_source").get<std::string>() : "sys_clk";
    form->addRow(QString::fromUtf8("Источник тактирования:"), stringCombo(
        {{"Системная (sys_clk)", "sys_clk"}, {"Шина AHB (hclk)", "hclk"}, {"OSC32M", "osc32m"},
         {"HSI32M", "hsi32m"}, {"OSC32K", "osc32k"}, {"LSI32K", "lsi32k"}}, clock_source,
        [this, block_index](const std::string& value)
        { document_->setField(block_index, "clock_source", value); emit documentChanged(); }));

    QSpinBox* frequency = new QSpinBox(); frequency->setRange(1, 100000);
    frequency->setSuffix(QString::fromUtf8(" Гц"));
    const nlohmann::json frequency_value = document_->field(block_index, "frequency_hz");
    // TSENS_OPTIMAL_FREQUENCY = 40000 (mik32_hal_tsens.h).
    frequency->setValue(frequency_value.is_number_integer() ? frequency_value.get<int>() : 40000);
    connect(frequency, &QSpinBox::valueChanged, this, [this, block_index](int value)
    { document_->setField(block_index, "frequency_hz", value); emit documentChanged(); });
    form->addRow(QString::fromUtf8("Частота:"), frequency);

    const bool has_low = document_->field(block_index, "low_threshold_celsius").is_number_integer();
    QCheckBox* low_enable = new QCheckBox(QString::fromUtf8("Нижний порог"));
    low_enable->setChecked(has_low);
    connect(low_enable, &QCheckBox::toggled, this, [this, block_index](bool on)
    {
        if (on) document_->setField(block_index, "low_threshold_celsius", -10);
        else document_->eraseField(block_index, "low_threshold_celsius");
        emit documentChanged();
        refresh();
    });
    form->addRow(QString(), low_enable);
    if (has_low)
    {
        QSpinBox* low = new QSpinBox(); low->setRange(-40, 125);
        low->setSuffix(QString::fromUtf8(" °C"));
        low->setValue(document_->field(block_index, "low_threshold_celsius").get<int>());
        connect(low, &QSpinBox::valueChanged, this, [this, block_index](int value)
        { document_->setField(block_index, "low_threshold_celsius", value); emit documentChanged(); });
        form->addRow(QString::fromUtf8("Значение нижнего порога:"), low);
    }

    const bool has_high = document_->field(block_index, "high_threshold_celsius").is_number_integer();
    QCheckBox* high_enable = new QCheckBox(QString::fromUtf8("Верхний порог"));
    high_enable->setChecked(has_high);
    connect(high_enable, &QCheckBox::toggled, this, [this, block_index](bool on)
    {
        if (on) document_->setField(block_index, "high_threshold_celsius", 85);
        else document_->eraseField(block_index, "high_threshold_celsius");
        emit documentChanged();
        refresh();
    });
    form->addRow(QString(), high_enable);
    if (has_high)
    {
        QSpinBox* high = new QSpinBox(); high->setRange(-40, 125);
        high->setSuffix(QString::fromUtf8(" °C"));
        high->setValue(document_->field(block_index, "high_threshold_celsius").get<int>());
        connect(high, &QSpinBox::valueChanged, this, [this, block_index](int value)
        { document_->setField(block_index, "high_threshold_celsius", value); emit documentChanged(); });
        form->addRow(QString::fromUtf8("Значение верхнего порога:"), high);
    }

    const nlohmann::json interrupts = document_->field(block_index, "interrupts");
    std::set<std::string> enabled_interrupts;
    if (interrupts.is_array())
    {
        for (const auto& item : interrupts)
        {
            if (item.is_string()) enabled_interrupts.insert(item.get<std::string>());
        }
    }
    auto makeInterruptCheckbox = [this, block_index](const char* label, const char* key, bool checked)
    {
        QCheckBox* box = new QCheckBox(QString::fromUtf8(label));
        box->setChecked(checked);
        connect(box, &QCheckBox::toggled, this, [this, block_index, key](bool on)
        {
            nlohmann::json interrupts = document_->field(block_index, "interrupts");
            if (!interrupts.is_array()) interrupts = nlohmann::json::array();
            std::set<std::string> values;
            for (const auto& item : interrupts) if (item.is_string()) values.insert(item.get<std::string>());
            if (on) values.insert(key); else values.erase(key);
            nlohmann::json updated = nlohmann::json::array();
            for (const auto& value : values) updated.push_back(value);
            document_->setField(block_index, "interrupts", updated);
            emit documentChanged();
        });
        return box;
    };
    form->addRow(QString::fromUtf8("Прерывания:"),
        makeInterruptCheckbox("Низкая температура", "low", enabled_interrupts.count("low") > 0));
    form->addRow(QString(), makeInterruptCheckbox("Высокая температура", "high", enabled_interrupts.count("high") > 0));
    form->addRow(QString(), makeInterruptCheckbox("Конец преобразования (EOC)", "eoc", enabled_interrupts.count("eoc") > 0));

    layout->addWidget(box);

    QLabel* note = new QLabel(QString::fromUtf8(
        "Запуск измерения (HAL_TSENS_SingleStart/ContinuousStart) остаётся за вами — "
        "здесь настраивается только тактирование/пороги/прерывания."));
    note->setWordWrap(true);
    note->setEnabled(false);
    layout->addWidget(note);

    QPushButton* disable = new QPushButton(QString::fromUtf8("Отключить блок"));
    connect(disable, &QPushButton::clicked, this, [this]()
    {
        document_->removeBlocks("tsens", 0);
        emit documentChanged();
        refresh();
    });
    layout->addWidget(disable);
}

void PropertiesPanel::buildPvdForm(QVBoxLayout* layout)
{
    const std::vector<int> indices = document_->blockIndices("pvd", 0);
    if (indices.empty())
    {
        return;
    }
    const int block_index = indices.front();

    QLabel* title = new QLabel(QString::fromUtf8("<b>PVD</b>"));
    layout->addWidget(title);

    QGroupBox* box = new QGroupBox(QString::fromUtf8("Параметры"));
    QFormLayout* form = new QFormLayout(box);

    QSpinBox* under = new QSpinBox(); under->setRange(0, 15);
    const nlohmann::json under_value = document_->field(block_index, "under_threshold");
    under->setValue(under_value.is_number_integer() ? under_value.get<int>() : 0);
    connect(under, &QSpinBox::valueChanged, this, [this, block_index](int value)
    { document_->setField(block_index, "under_threshold", value); emit documentChanged(); });
    form->addRow(QString::fromUtf8("Порог \"ниже\" (сырой код 0-15):"), under);

    QSpinBox* over = new QSpinBox(); over->setRange(0, 15);
    const nlohmann::json over_value = document_->field(block_index, "over_threshold");
    over->setValue(over_value.is_number_integer() ? over_value.get<int>() : 15);
    connect(over, &QSpinBox::valueChanged, this, [this, block_index](int value)
    { document_->setField(block_index, "over_threshold", value); emit documentChanged(); });
    form->addRow(QString::fromUtf8("Порог \"выше\" (сырой код 0-15):"), over);

    QSpinBox* filter = new QSpinBox();
    // Схема допускает 0-65535 (ширина регистра), но Errata MIK32V2 §12.2:
    // значения выше 255 отключают детектирование напряжения вообще — в GUI
    // верхняя граница зажата строже схемы (см. codegen/project/pvd_contribution.cpp).
    filter->setRange(0, 255);
    filter->setToolTip(QString::fromUtf8(
        "Errata §12.2: значения выше 255 отключают детектирование напряжения — "
        "GUI ограничивает диапазон строже, чем сама схема/регистр."));
    const nlohmann::json filter_value = document_->field(block_index, "digital_filter_value");
    filter->setValue(filter_value.is_number_integer()
        ? std::min(filter_value.get<int>(), 255) : 0);
    connect(filter, &QSpinBox::valueChanged, this, [this, block_index](int value)
    { document_->setField(block_index, "digital_filter_value", value); emit documentChanged(); });
    form->addRow(QString::fromUtf8("Цифровой фильтр:"), filter);

    QCheckBox* calibration = new QCheckBox(QString::fromUtf8("Калибровка опорного напряжения"));
    calibration->setChecked(document_->field(block_index, "enable_reference_calibration").is_boolean()
        && document_->field(block_index, "enable_reference_calibration").get<bool>());
    connect(calibration, &QCheckBox::toggled, this, [this, block_index](bool on)
    { document_->setField(block_index, "enable_reference_calibration", on); emit documentChanged(); });
    form->addRow(QString(), calibration);

    layout->addWidget(box);

    QLabel* note = new QLabel(QString::fromUtf8(
        "Сброс платы по порогу (NRESETU/NRESETO) здесь не настраивается — полярность "
        "этих битов не подтверждена ни в одном доступном материале, кодоген их не трогает."));
    note->setWordWrap(true);
    note->setEnabled(false);
    layout->addWidget(note);

    QPushButton* disable = new QPushButton(QString::fromUtf8("Отключить блок"));
    connect(disable, &QPushButton::clicked, this, [this]()
    {
        document_->removeBlocks("pvd", 0);
        emit documentChanged();
        refresh();
    });
    layout->addWidget(disable);
}

void PropertiesPanel::buildBusWatchdogForm(QVBoxLayout* layout)
{
    const std::vector<int> indices = document_->blockIndices("bus_watchdog", 0);
    if (indices.empty())
    {
        return;
    }
    const int block_index = indices.front();

    QLabel* title = new QLabel(QString::fromUtf8("<b>Bus Watchdog</b>"));
    layout->addWidget(title);

    QGroupBox* box = new QGroupBox(QString::fromUtf8("Линии"));
    QFormLayout* form = new QFormLayout(box);

    QCheckBox* eeprom = new QCheckBox(QString::fromUtf8("EEPROM"));
    eeprom->setChecked(document_->field(block_index, "eeprom_enabled").is_boolean()
        && document_->field(block_index, "eeprom_enabled").get<bool>());
    connect(eeprom, &QCheckBox::toggled, this, [this, block_index](bool on)
    { document_->setField(block_index, "eeprom_enabled", on); emit documentChanged(); });
    form->addRow(QString(), eeprom);

    QCheckBox* spifi = new QCheckBox(QString::fromUtf8("SPIFI"));
    spifi->setChecked(document_->field(block_index, "spifi_enabled").is_boolean()
        && document_->field(block_index, "spifi_enabled").get<bool>());
    connect(spifi, &QCheckBox::toggled, this, [this, block_index](bool on)
    { document_->setField(block_index, "spifi_enabled", on); emit documentChanged(); });
    form->addRow(QString(), spifi);

    QCheckBox* dom3 = new QCheckBox(QString::fromUtf8("DOM3 (периферия APB)"));
    dom3->setChecked(document_->field(block_index, "dom3_enabled").is_boolean()
        && document_->field(block_index, "dom3_enabled").get<bool>());
    connect(dom3, &QCheckBox::toggled, this, [this, block_index](bool on)
    { document_->setField(block_index, "dom3_enabled", on); emit documentChanged(); });
    form->addRow(QString(), dom3);

    QSpinBox* timeout = new QSpinBox(); timeout->setRange(0, kUint32SpinBoxMax);
    const nlohmann::json timeout_value = document_->field(block_index, "timeout");
    timeout->setValue(timeout_value.is_number_integer() ? timeout_value.get<int>() : 0);
    connect(timeout, &QSpinBox::valueChanged, this, [this, block_index](int value)
    { document_->setField(block_index, "timeout", value); emit documentChanged(); });
    form->addRow(QString::fromUtf8("Таймаут (общий на все линии):"), timeout);

    layout->addWidget(box);

    QLabel* note = new QLabel(QString::fromUtf8(
        "Хотя бы одна линия включена, а таймаут равен 0 — сторожевой таймер шины "
        "сработает немедленно. Задайте ненулевой таймаут перед включением линии."));
    note->setWordWrap(true);
    note->setEnabled(false);
    layout->addWidget(note);

    QPushButton* disable = new QPushButton(QString::fromUtf8("Отключить блок"));
    connect(disable, &QPushButton::clicked, this, [this]()
    {
        document_->removeBlocks("bus_watchdog", 0);
        emit documentChanged();
        refresh();
    });
    layout->addWidget(disable);
}

// schemas/dma.json, _comment_usart_excluded: USART0/1 настраиваются
// встроенным полем "dma" у type="uart", здесь не повторяются.
void PropertiesPanel::buildDmaForm(QVBoxLayout* layout, int instance)
{
    const std::vector<int> indices = document_->blockIndices("dma", instance);
    if (indices.empty())
    {
        return;
    }
    const int block_index = indices.front();

    QLabel* title = new QLabel(QString::fromUtf8("<b>DMA — канал %1</b>").arg(instance));
    layout->addWidget(title);

    QGroupBox* box = new QGroupBox(QString::fromUtf8("Параметры канала"));
    QFormLayout* form = new QFormLayout(box);

    static const std::vector<std::pair<const char*, const char*>> kDmaDirections = {
        {"Периферия -> память (RX)", "peripheral_to_memory"},
        {"Память -> периферия (TX)", "memory_to_peripheral"},
        {"Память -> память", "memory_to_memory"},
    };
    QComboBox* direction = stringCombo(kDmaDirections,
        document_->field(block_index, "direction").is_string()
            ? document_->field(block_index, "direction").get<std::string>() : "peripheral_to_memory",
        [this, block_index](const std::string& value)
        {
            document_->setField(block_index, "direction", value);
            if (value == "memory_to_memory")
            {
                // "request" не должен остаться в документе — парсер
                // (parser/dma/dma_parser.cpp) отклоняет сам факт его
                // присутствия при direction=memory_to_memory, даже если
                // значение null.
                document_->eraseField(block_index, "request");
            }
            else if (!document_->field(block_index, "request").is_string())
            {
                // Обратный переход — "request" обязателен, а поле могло
                // быть стёрто предыдущим переключением на memory_to_memory;
                // без этого документ был бы временно невалиден до того,
                // как пользователь тронет комбобокс request сам.
                document_->setField(block_index, "request", std::string("crypto"));
            }
            emit documentChanged();
            refresh();
        });
    form->addRow(QString::fromUtf8("Направление:"), direction);

    static const std::vector<std::pair<const char*, const char*>> kDmaRequests = {
        {"CRYPTO", "crypto"}, {"SPI_0", "spi_0"}, {"SPI_1", "spi_1"},
        {"I2C_0", "i2c_0"}, {"I2C_1", "i2c_1"}, {"SPIFI", "spifi"},
        {"Timer32_0", "timer32_0"}, {"Timer32_1", "timer32_1"}, {"Timer32_2", "timer32_2"},
        {"DAC_0", "dac_0"}, {"DAC_1", "dac_1"},
    };
    const std::string current_direction = document_->field(block_index, "direction").is_string()
        ? document_->field(block_index, "direction").get<std::string>() : "peripheral_to_memory";
    if (current_direction != "memory_to_memory")
    {
        QComboBox* request = stringCombo(kDmaRequests,
            document_->field(block_index, "request").is_string()
                ? document_->field(block_index, "request").get<std::string>() : "crypto",
            [this, block_index](const std::string& value)
            { document_->setField(block_index, "request", value); emit documentChanged(); });
        form->addRow(QString::fromUtf8("Периферийная линия (request):"), request);
    }
    else
    {
        QLabel* no_request = new QLabel(QString::fromUtf8("(не используется — обе стороны канала — память)"));
        no_request->setEnabled(false);
        form->addRow(QString::fromUtf8("Периферийная линия (request):"), no_request);
    }

    static const std::vector<std::pair<const char*, const char*>> kDmaElementSizes = {
        {"Байт", "byte"}, {"Полуслово", "halfword"}, {"Слово", "word"},
    };
    QComboBox* element_size = stringCombo(kDmaElementSizes,
        document_->field(block_index, "element_size").is_string()
            ? document_->field(block_index, "element_size").get<std::string>() : "byte",
        [this, block_index](const std::string& value)
        { document_->setField(block_index, "element_size", value); emit documentChanged(); });
    form->addRow(QString::fromUtf8("Размер элемента:"), element_size);

    QSpinBox* buffer_size = new QSpinBox(); buffer_size->setRange(1, kUint32SpinBoxMax);
    const nlohmann::json buffer_size_value = document_->field(block_index, "buffer_size_bytes");
    buffer_size->setValue(buffer_size_value.is_number_integer() ? buffer_size_value.get<int>() : 16);
    connect(buffer_size, &QSpinBox::valueChanged, this, [this, block_index](int value)
    { document_->setField(block_index, "buffer_size_bytes", value); emit documentChanged(); });
    form->addRow(QString::fromUtf8("Размер буфера (байт):"), buffer_size);

    static const std::vector<std::pair<const char*, const char*>> kDmaPriorities = {
        {"Низкий", "low"}, {"Средний", "medium"}, {"Высокий", "high"}, {"Наивысший", "very_high"},
    };
    QComboBox* priority = stringCombo(kDmaPriorities,
        document_->field(block_index, "priority").is_string()
            ? document_->field(block_index, "priority").get<std::string>() : "very_high",
        [this, block_index](const std::string& value)
        { document_->setField(block_index, "priority", value); emit documentChanged(); });
    form->addRow(QString::fromUtf8("Приоритет:"), priority);

    QCheckBox* irq_enable = new QCheckBox(QString::fromUtf8("Прерывание по завершении канала"));
    irq_enable->setChecked(document_->field(block_index, "irq_enable").is_boolean()
        && document_->field(block_index, "irq_enable").get<bool>());
    connect(irq_enable, &QCheckBox::toggled, this, [this, block_index](bool on)
    { document_->setField(block_index, "irq_enable", on); emit documentChanged(); });
    form->addRow(QString(), irq_enable);

    layout->addWidget(box);

    QLabel* note = new QLabel(QString::fromUtf8(
        "Кодоген настраивает только канал (ChannelInit) — сам запуск передачи "
        "(HAL_DMA_Start) остаётся за вами: конкретный адрес регистра данных нужной "
        "периферии не выводится автоматически (см. docs/implementations/dma.md). "
        "USART0/1 настраиваются отдельно — встроенным полем DMA в самом блоке UART."));
    note->setWordWrap(true);
    note->setEnabled(false);
    layout->addWidget(note);

    QPushButton* disable = new QPushButton(QString::fromUtf8("Отключить блок"));
    connect(disable, &QPushButton::clicked, this, [this, instance]()
    {
        document_->removeBlocks("dma", instance);
        emit documentChanged();
        refresh();
    });
    layout->addWidget(disable);
}

// FreeRTOS — перенесено сюда из ProjectManagerScreen (gui/project_manager_screen.cpp)
// по требованию пользователя: в STM32CubeMX middleware (FreeRTOS и т.п.)
// выбирается кликом по дереву слева, а не вкладкой внутри Project Manager.
// FreeRtosSettingsWidget уже самодостаточен (setValue/value()/changed()) —
// тот же приём "собрать весь nlohmann::json объект одним value()", что и
// раньше; здесь только меняется МЕСТО встраивания, не контракт виджета.
void PropertiesPanel::buildFreertosForm(QVBoxLayout* layout)
{
    QLabel* title = new QLabel(QString::fromUtf8("<b>FreeRTOS</b>"));
    layout->addWidget(title);

    FreeRtosSettingsWidget* widget = new FreeRtosSettingsWidget();
    widget->setValue(document_->projectSetting("freertos"));
    connect(widget, &FreeRtosSettingsWidget::changed, this, [this, widget]()
    {
        document_->setProjectSetting("freertos", widget->value());
        emit documentChanged();
    });
    layout->addWidget(widget);
}
