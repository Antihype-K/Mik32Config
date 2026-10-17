//
// Переиспользуемая визуальная "рамка" формы периферии в стиле STM32CubeMX:
// сверху секция "Mode" (чекбоксы/комбобоксы, включающие крупные подрежимы),
// кнопка "Reset Configuration", ниже — вкладки "Configuration": Parameter
// Settings (всегда), и опционально NVIC Settings / DMA Settings / GPIO
// Settings — в зависимости от того, что умеет конкретная периферия.
//
// Эта панель ничего не знает о конкретном типе периферии (RTC/SPI/...) —
// она только раскладывает виджеты, которые ей передаёт вызывающий код
// (будущий properties_panel.cpp::buildXForm). Сама не хранит и не пишет
// конфигурацию, как и остальные формообразующие классы этого GUI.
//
// Parameter Settings оформлен как двухколоночный "категория -> поля": слева
// жирный заголовок категории с меньшим отступом, под ним — подписи полей с
// большим отступом, справа — виджет-контрол на той же строке. Это калька
// внешнего вида CubeMX (Calendar Time / General / Calendar Date и т.п.), не
// полноценное сворачиваемое дерево — сворачивания категорий в этой версии
// нет (не было времени, можно добавить позже без слома API).
//
#ifndef MIK32_CONFIGURATOR_PERIPHERAL_FORM_TABS_H
#define MIK32_CONFIGURATOR_PERIPHERAL_FORM_TABS_H

#include <QWidget>
#include <QString>

class QVBoxLayout;
class QTabWidget;
class QGridLayout;
class QPushButton;
class QLabel;

class PeripheralFormTabs : public QWidget
{
    Q_OBJECT

public:
    explicit PeripheralFormTabs(QWidget* parent = nullptr);

    // Секция "Mode" — вызывающий код добавляет сюда свои чекбоксы/комбобоксы
    // напрямую (через layout), либо целиком передаёт готовый виджет.
    QVBoxLayout* modeLayout();
    void setModeWidget(QWidget* widget);

    // Вкладка "Parameter Settings" — всегда присутствует первой. Добавление
    // категории — это просто жирный заголовок на всю ширину; addParameterField
    // кладёт пару label+control в ту же сетку с увеличенным левым отступом,
    // визуально "вложенным" под последний добавленный заголовок.
    void addParameterCategory(const QString& category_name);
    void addParameterField(const QString& label, QWidget* control);

    // Опциональные вкладки — вызываются только если периферия это умеет
    // (прерывание / DMA / собственные выводы). Возвращают layout, который
    // вызывающий код заполняет сам, тем же приёмом, что и modeLayout().
    // Повторный вызов для уже добавленной вкладки возвращает тот же layout
    // (идемпотентно), а не создаёт вторую одноимённую вкладку.
    QVBoxLayout* addNvicSettingsTab();
    QVBoxLayout* addDmaSettingsTab();
    QVBoxLayout* addGpioSettingsTab();

    // Убрать все добавленные категории/поля/опциональные вкладки и Mode-виджет,
    // не пересоздавая саму рамку — нужно между showPeripheral() на разные
    // блоки, когда properties_panel.cpp перестраивает форму с нуля.
    void resetContents();

signals:
    // "Reset Configuration" — что именно это означает (сброс полей к
    // дефолту или отключение блока целиком) решает вызывающий код.
    void resetRequested();

private:
    QVBoxLayout* mode_layout_ = nullptr;
    QWidget* mode_widget_ = nullptr;

    QPushButton* reset_button_ = nullptr;
    QTabWidget* tab_widget_ = nullptr;

    QWidget* parameter_settings_page_ = nullptr;
    QGridLayout* parameter_grid_ = nullptr;
    int parameter_row_ = 0;

    QWidget* nvic_page_ = nullptr;
    QVBoxLayout* nvic_layout_ = nullptr;

    QWidget* dma_page_ = nullptr;
    QVBoxLayout* dma_layout_ = nullptr;

    QWidget* gpio_page_ = nullptr;
    QVBoxLayout* gpio_layout_ = nullptr;
};

#endif //MIK32_CONFIGURATOR_PERIPHERAL_FORM_TABS_H
