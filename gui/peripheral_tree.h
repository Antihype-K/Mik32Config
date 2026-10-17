//
// Левая панель — дерево навигации по периферийным блокам
// (ТЗ §3.1.9 п.2 «левая панель», п.3 — структура, поиск, контекстное меню).
//
// Четыре исходных группы заданы ТЗ буквально:
//   System Core   (Clock, GPIO, + RTC/WDT/EEPROM/BusWatchdog, Тир 2/3)
//   Analog        (ADC/DAC_0-1/TSENS/PVD, Тир 2/3 — было "зарезервировано")
//   Timers        (Timer16_0–2, Timer32_0–2)
//   Connectivity  (USART_0–1, I2C_0–1, SPI_0–1)
// Пятая группа "Security & Integrity" (CRC/CRYPTO/OTP) добавлена сверх ТЗ —
// для этих трёх типов нет естественного места ни в одной из четырёх
// исходных групп (не тактирование, не аналог, не таймер, не шина связи).
//
// Важное решение: узлы, для которых в конфигураторе ЕЩЁ НЕТ парсера и
// кодогенерации (Clock), в дереве всё равно присутствуют, но помечены как
// недоступные. Убрать их было бы удобнее в реализации, но дерево — это карта
// возможностей МК, и пользователь должен видеть разницу между «у МИК32
// такого нет» и «конфигуратор пока не умеет». Второе честнее показать серым,
// чем спрятать.
//
#ifndef MIK32_CONFIGURATOR_PERIPHERAL_TREE_H
#define MIK32_CONFIGURATOR_PERIPHERAL_TREE_H

#include <QWidget>
#include <QString>

class QTreeWidget;
class QTreeWidgetItem;
class QLineEdit;

// Адрес блока в конфигурации. instance == -1 у групповых узлов.
struct PeripheralRef
{
    QString type;      // "gpio" / "uart" / "timer16" / "i2c"; пусто у групп и нереализованных
    int instance = -1;
    bool supported = false;

    bool isValid() const { return !type.isEmpty() && instance >= 0; }
};

class PeripheralTree : public QWidget
{
    Q_OBJECT

public:
    explicit PeripheralTree(QWidget* parent = nullptr);

    // Помечает включённые блоки (жирным + цветом) и подставляет
    // пользовательские имена. enabled/names ключуются строкой "type:instance".
    void refreshState(const QSet<QString>& enabled, const QHash<QString, QString>& names);

    void selectPeripheral(const QString& type, int instance);
    PeripheralRef currentRef() const;

    static QString keyFor(const QString& type, int instance);

signals:
    void selectionChanged(const PeripheralRef& ref);
    void enableRequested(const PeripheralRef& ref);
    void disableRequested(const PeripheralRef& ref);
    void renameRequested(const PeripheralRef& ref);
    void resetRequested(const PeripheralRef& ref);
    void copySettingsRequested(const PeripheralRef& ref);

private slots:
    void onFilterChanged(const QString& text);
    void onContextMenu(const QPoint& position);
    void onCurrentItemChanged();

private:
    QTreeWidgetItem* addLeaf(QTreeWidgetItem* parent, const QString& title,
                             const QString& type, int instance, bool supported);

    QTreeWidget* tree_ = nullptr;
    QLineEdit* filter_ = nullptr;
};

#endif //MIK32_CONFIGURATOR_PERIPHERAL_TREE_H
