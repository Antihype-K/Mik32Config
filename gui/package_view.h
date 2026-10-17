//
// Центральная панель — схема корпуса МК с выводами (ТЗ §3.1.9 п.2),
// с масштабированием колёсиком мыши (п.4).
//
// Концепция. Данные о выводах приходят из knowledge_base/package.json
// (номер вывода, обозначение, разряд GPIO) — это факты из даташита.
// ГЕОМЕТРИЯ здесь не из даташита: раскладка «16 выводов на каждую из четырёх
// сторон, отсчёт против часовой стрелки от ключа в левом верхнем углу» — это
// стандартное соглашение для корпусов QFN/QFP, а не свойство МИК32. Поэтому
// она живёт в коде отрисовки, а не в базе знаний, и проверяется по
// шелкографии платы, а не по документу.
//
// Панель ничего не знает про конфигурацию: она принимает готовую раскраску
// выводов (setPinStates) и сообщает наверх о клике. Решение «этот вывод занят
// UART_0, а тот конфликтует» принимает MainWindow, у которого есть и документ,
// и база знаний.
//
#ifndef MIK32_CONFIGURATOR_PACKAGE_VIEW_H
#define MIK32_CONFIGURATOR_PACKAGE_VIEW_H

#include <QGraphicsView>
#include <QHash>
#include <QString>
#include <QPointF>
#include "package_knowledge.h"

class QGraphicsRectItem;
class QGraphicsSimpleTextItem;

// Состояние вывода определяет цвет. Смысл состояний — не «красиво», а
// «чем этот вывод сейчас занят и можно ли его трогать».
enum class PinState
{
    Service,      // питание/земля/сброс/осциллятор — назначить нельзя в принципе
    Free,         // мультиплексируемый и никем не занят
    Gpio,         // занят блоком gpio из конфигурации (пользователь выбрал явно)
    Peripheral,   // занят фиксированным выводом UART/I2C/Timer16 (выбора нет)
    Conflict      // на вывод претендуют двое — ТЗ §3.1.6.3, блокирует генерацию
};

class PackageView : public QGraphicsView
{
    Q_OBJECT

public:
    explicit PackageView(QWidget* parent = nullptr);

    // Строит схему один раз по карте выводов. Повторный вызов пересоздаёт сцену.
    void setPackage(const PackageKnowledge& package);

    // Перекрашивает уже построенную схему. Выводы, отсутствующие в states,
    // считаются Free (для мультиплексируемых) или Service.
    //
    // labels — необязательная карта «короткая подпись текущей функции»
    // (например, "I2C_0"), как в STM32CubeMX: имя вывода из даташита (pin.name)
    // всегда написано ВНУТРИ самого вывода (неизменно), а назначенный
    // интерфейс — СНАРУЖИ, над/сбоку от вывода, и виден только пока вывод
    // реально чем-то занят (пустая labels — снаружи ничего не пишется).
    void setPinStates(const QHash<int, PinState>& states, const QHash<int, QString>& tooltips,
                       const QHash<int, QString>& labels = {});

signals:
    void pinClicked(int pin_number);

protected:
    // ТЗ §3.1.9 п.4 — масштабирование колёсиком.
    void wheelEvent(QWheelEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;

private:
    struct PinItem
    {
        QGraphicsRectItem* pad = nullptr;
        QGraphicsSimpleTextItem* name_label = nullptr;   // ВНУТРИ вывода — имя из даташита, всегда видно
        QGraphicsSimpleTextItem* mode_label = nullptr;   // СНАРУЖИ вывода — назначенный интерфейс, видно только когда занят
        bool multiplexed = false;
        QPointF pad_center;      // центр вывода — для name_label (внутри)
        QPointF label_anchor;    // точка привязки СНАРУЖИ вывода — для mode_label
        int side = 0;            // 0=лево,1=низ,2=право,3=верх
        double label_rotation = 0.0;   // 0 (лево/право) или 90 (низ/верх)
    };

    // Центрирует name_label ВНУТРИ вывода (pad_center).
    void layoutNameLabel(QGraphicsSimpleTextItem* label, const PinItem& item);
    // Раскладывает mode_label СНАРУЖИ вывода, от label_anchor наружу (как в
    // STM32CubeMX — подпись интерфейса растёт от корпуса, а не наезжает
    // на соседние выводы).
    void layoutModeLabel(QGraphicsSimpleTextItem* label, const PinItem& item);

    QHash<int, PinItem> pin_items_;
    double scale_factor_ = 1.0;
};

#endif //MIK32_CONFIGURATOR_PACKAGE_VIEW_H
