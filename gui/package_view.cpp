#include "package_view.h"
#include <QGraphicsScene>
#include <QGraphicsRectItem>
#include <QGraphicsSimpleTextItem>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QFont>

namespace
{
    // Геометрия корпуса в условных единицах сцены. Абсолютные размеры не
    // важны — вид масштабируется под окно, а затем колёсиком. Чип сделан
    // МЕНЬШЕ, а выводы — КРУПНЕЕ относительно него (по прямому замечанию
    // пользователя: "чип слишком большой, а ножки маленькие"): kBodySize
    // уменьшен (560 -> 300), kPadLength увеличен (34 -> 85) — внутри вывода
    // теперь есть место под имя из даташита.
    constexpr double kBodySize = 300.0;   // сторона тела микросхемы
    constexpr double kPadLength = 85.0;   // длина вывода наружу от тела
    constexpr int kPinsPerSide = 16;      // QFN-64: 64 / 4
    // Плотная раскладка, как в STM32CubeMX (выводы почти соприкасаются,
    // только тонкий зазор между соседними). kPadGapFraction — доля шага,
    // остающаяся зазором; остальное — ширина самой площадки.
    constexpr double kPadGapFraction = 0.08;

    QColor colorFor(PinState state)
    {
        switch (state)
        {
            // Служебные (VCC/VDD/VSS/VBAT/NRST/осцилляторы) — бледно-жёлтые,
            // как в STM32CubeMX: пользователь должен с первого взгляда видеть
            // "это питание/сброс, трогать нечего" — назначить их нельзя в
            // принципе (PinState::Service), цвет лишь подтверждает это визуально.
            case PinState::Service:    return QColor(0xe8, 0xe0, 0x9a);
            // Свободный мультиплексируемый вывод — нейтральный серый
            // (STM32CubeMX: неиспользуемые выводы PBx/PAx серые по умолчанию).
            case PinState::Free:       return QColor(0xa8, 0xac, 0xb2);
            // Реально назначенный вывод (GPIO пользователем или фиксированная
            // периферия) — зелёный, как в STM32CubeMX при выборе режима пина.
            case PinState::Gpio:       return QColor(0x3d, 0xd9, 0x5c);
            case PinState::Peripheral: return QColor(0x3d, 0xd9, 0x5c);
            case PinState::Conflict:   return QColor(0xe0, 0x3b, 0x3b);
        }
        return Qt::gray;
    }

    // Текст ВНУТРИ вывода всегда тёмный и жирный — светлые заливки (жёлтый/
    // серый/зелёный) с тёмным текстом дают контраст (прежде серые подписи на
    // тёмном фоне были еле видны — прямое замечание пользователя "контрасты
    // не очень").
    const QColor kNameLabelColor(0x12, 0x14, 0x16);
}

PackageView::PackageView(QWidget* parent)
    : QGraphicsView(parent)
{
    setScene(new QGraphicsScene(this));
    setRenderHint(QPainter::Antialiasing, true);
    setDragMode(QGraphicsView::ScrollHandDrag);
    // Масштабирование должно происходить вокруг курсора, а не вокруг центра
    // вида — иначе при приближении интересующий вывод уезжает за край.
    setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    setBackgroundBrush(QColor(0x2b, 0x2e, 0x33));
}

void PackageView::setPackage(const PackageKnowledge& package)
{
    scene()->clear();
    pin_items_.clear();

    const double half = kBodySize / 2.0;

    // Тело микросхемы.
    QGraphicsRectItem* body = scene()->addRect(
        -half, -half, kBodySize, kBodySize,
        QPen(QColor(0x11, 0x12, 0x14), 2.0), QBrush(QColor(0x33, 0x37, 0x3d)));
    body->setZValue(1.0);

    // Ключ первого вывода — точка в левом верхнем углу. Именно от неё
    // отсчитывается нумерация, и именно она есть на реальном корпусе, поэтому
    // рисуется явно: без неё схема неотличима от повёрнутой на 90 градусов.
    scene()->addEllipse(-half + 14.0, -half + 14.0, 24.0, 24.0,
                        QPen(Qt::NoPen), QBrush(QColor(0xe8, 0xe8, 0xe8)))->setZValue(2.0);

    QGraphicsSimpleTextItem* caption = scene()->addSimpleText(
        QString::fromStdString(package.package));
    caption->setBrush(QColor(0xa8, 0xb0, 0xba));
    QFont caption_font = caption->font();
    caption_font.setPointSizeF(14.0);
    caption->setFont(caption_font);
    caption->setPos(-caption->boundingRect().width() / 2.0, -9.0);
    caption->setZValue(2.0);

    const double step = kBodySize / kPinsPerSide;
    // Ширина площадки считается от шага, а не задаётся отдельной константой:
    // так плотность упаковки не расходится при изменении kPinsPerSide.
    const double pad_width = step * (1.0 - kPadGapFraction);

    for (const PackagePin& pin : package.pins)
    {
        // Раскладка QFN: вывод 1 — верхний на ЛЕВОЙ стороне, дальше против
        // часовой стрелки: вниз по левой (1..16), вправо по нижней (17..32),
        // вверх по правой (33..48), влево по верхней (49..64).
        const int zero_based = pin.pin - 1;
        const int side = zero_based / kPinsPerSide;
        const int position = zero_based % kPinsPerSide;
        // Центр ячейки: +0.5 шага, иначе выводы прижались бы к углам.
        const double offset = -half + (position + 0.5) * step;

        QRectF pad_rect;
        QPointF label_pos;
        double label_rotation = 0.0;

        switch (side)
        {
            case 0:  // левая сторона, сверху вниз — подпись интерфейса СНАРУЖИ, левее вывода
                pad_rect = QRectF(-half - kPadLength, offset - pad_width / 2.0, kPadLength, pad_width);
                label_pos = QPointF(-half - kPadLength - 10.0, offset);
                break;
            case 1:  // нижняя сторона, слева направо — подпись СНАРУЖИ, ниже вывода
                pad_rect = QRectF(offset - pad_width / 2.0, half, pad_width, kPadLength);
                label_pos = QPointF(offset, half + kPadLength + 10.0);
                label_rotation = 90.0;
                break;
            case 2:  // правая сторона, снизу вверх (нумерация зеркальная — "-offset")
                pad_rect = QRectF(half, -offset - pad_width / 2.0, kPadLength, pad_width);
                label_pos = QPointF(half + kPadLength + 10.0, -offset);
                break;
            default:  // верхняя сторона, справа налево — подпись СНАРУЖИ, выше вывода
                pad_rect = QRectF(-offset - pad_width / 2.0, -half - kPadLength, pad_width, kPadLength);
                label_pos = QPointF(-offset, -half - kPadLength - 10.0);
                label_rotation = 90.0;
                break;
        }

        QGraphicsRectItem* pad = scene()->addRect(pad_rect, QPen(QColor(0x1a, 0x1c, 0x1f), 1.0),
                                                  QBrush(colorFor(pin.isMultiplexed() ? PinState::Free
                                                                                      : PinState::Service)));
        pad->setZValue(3.0);
        // Номер вывода хранится в самом элементе — по нему mousePressEvent
        // находит вывод, не пересчитывая геометрию обратно.
        pad->setData(0, pin.pin);
        pad->setToolTip(QString("%1 — %2").arg(pin.pin).arg(QString::fromStdString(pin.name)));

        // ВНУТРИ вывода — имя из даташита, всегда одна строка, тёмный жирный
        // текст на светлой площадке (контрастно, видно сразу).
        QGraphicsSimpleTextItem* name_label = scene()->addSimpleText(QString::fromStdString(pin.name));
        QFont name_font = name_label->font();
        name_font.setPointSizeF(7.0);
        name_font.setBold(true);
        name_label->setFont(name_font);
        name_label->setBrush(kNameLabelColor);
        name_label->setZValue(4.0);

        // СНАРУЖИ вывода — назначенный интерфейс (labels из setPinStates),
        // изначально пусто/невидимо (вывод свободен) — появляется только
        // когда вывод реально чем-то занят.
        QGraphicsSimpleTextItem* mode_label = scene()->addSimpleText(QString());
        QFont mode_font = mode_label->font();
        mode_font.setPointSizeF(8.5);
        mode_font.setBold(true);
        mode_label->setFont(mode_font);
        mode_label->setBrush(QColor(0xf0, 0xf2, 0xf5));
        mode_label->setZValue(3.0);
        mode_label->setVisible(false);

        PinItem item;
        item.pad = pad;
        item.name_label = name_label;
        item.mode_label = mode_label;
        item.multiplexed = pin.isMultiplexed();
        item.pad_center = pad_rect.center();
        item.label_anchor = label_pos;
        item.side = side;
        item.label_rotation = label_rotation;
        pin_items_.insert(pin.pin, item);

        layoutNameLabel(name_label, item);
    }

    scene()->setSceneRect(scene()->itemsBoundingRect().adjusted(-30, -30, 30, 30));
    fitInView(scene()->sceneRect(), Qt::KeepAspectRatio);
    scale_factor_ = 1.0;
}

void PackageView::setPinStates(const QHash<int, PinState>& states, const QHash<int, QString>& tooltips,
                                const QHash<int, QString>& labels)
{
    for (auto it = pin_items_.begin(); it != pin_items_.end(); ++it)
    {
        const int pin_number = it.key();
        PinItem& item = it.value();

        const PinState state = states.contains(pin_number)
            ? states.value(pin_number)
            : (item.multiplexed ? PinState::Free : PinState::Service);

        item.pad->setBrush(QBrush(colorFor(state)));
        // Конфликтный вывод дополнительно обводится: полагаться на один лишь
        // цвет нельзя — красный и оранжевый неразличимы при дальтонизме.
        item.pad->setPen(state == PinState::Conflict
            ? QPen(QColor(0xff, 0xff, 0xff), 2.5)
            : QPen(QColor(0x1a, 0x1c, 0x1f), 1.0));

        if (tooltips.contains(pin_number))
        {
            item.pad->setToolTip(tooltips.value(pin_number));
        }

        // Имя внутри вывода (name_label) не меняется — оно статично (имя из
        // даташита). Меняется только mode_label снаружи: видим и содержит
        // назначенный интерфейс, когда вывод реально чем-то занят, иначе
        // скрыт целиком (а не просто пустая строка — так геометрия соседних
        // подписей не считается заново впустую).
        const QString new_mode_text = labels.value(pin_number);
        if (item.mode_label->text() != new_mode_text)
        {
            item.mode_label->setText(new_mode_text);
            item.mode_label->setVisible(!new_mode_text.isEmpty());
            if (!new_mode_text.isEmpty())
            {
                layoutModeLabel(item.mode_label, item);
            }
        }
    }
}

void PackageView::layoutNameLabel(QGraphicsSimpleTextItem* label, const PinItem& item)
{
    // Центрирование ВНУТРИ вывода. label_rotation=0 (лево/право) — обычный
    // центрированный блок; =90 (низ/верх) — тот же центр, но с поворотом
    // вокруг локального (0,0): точка (x,y) переходит в сцену pos + (-y, x).
    // Чтобы локальный центр текста (w/2, h/2) совпал с pad_center,
    // pos = pad_center - (-h/2, w/2) = (pad_center.x + h/2, pad_center.y - w/2).
    const QRectF text_rect = label->boundingRect();
    label->setTransformOriginPoint(0, 0);
    label->setRotation(item.label_rotation);

    if (item.label_rotation == 0.0)
    {
        label->setPos(item.pad_center.x() - text_rect.width() / 2.0,
                      item.pad_center.y() - text_rect.height() / 2.0);
    }
    else
    {
        label->setPos(item.pad_center.x() + text_rect.height() / 2.0,
                      item.pad_center.y() - text_rect.width() / 2.0);
    }
}

void PackageView::layoutModeLabel(QGraphicsSimpleTextItem* label, const PinItem& item)
{
    // Раскладка СНАРУЖИ вывода, от label_anchor наружу (растёт от корпуса,
    // не внутрь и не на соседние выводы) — тот же приём, что раньше был у
    // единственной внешней подписи, просто теперь это отдельный элемент,
    // не перекрывающийся с именем внутри вывода.
    const QRectF text_rect = label->boundingRect();
    label->setTransformOriginPoint(0, 0);
    label->setRotation(item.label_rotation);
    const QPointF& label_pos = item.label_anchor;
    if (item.side == 0)  // слева — выравнивание по правому краю (текст растёт влево, от корпуса)
    {
        label->setPos(label_pos.x() - text_rect.width(), label_pos.y() - text_rect.height() / 2.0);
    }
    else if (item.side == 2)  // справа — растёт вправо, от корпуса
    {
        label->setPos(label_pos.x(), label_pos.y() - text_rect.height() / 2.0);
    }
    else if (item.side == 1)  // низ, повёрнута на 90°: растёт вниз от точки привязки
    {
        label->setPos(label_pos.x() + text_rect.height() / 2.0, label_pos.y());
    }
    else  // верх, повёрнута на 90°: растёт вверх от точки привязки
    {
        label->setPos(label_pos.x() + text_rect.height() / 2.0, label_pos.y() - text_rect.width());
    }
}

void PackageView::wheelEvent(QWheelEvent* event)
{
    constexpr double kStep = 1.15;
    const double factor = event->angleDelta().y() > 0 ? kStep : 1.0 / kStep;

    // Ограничение масштаба: без него колесо уводит вид в состояние, из
    // которого пользователь не может вернуться (схема либо в пиксель, либо
    // за пределами разумного размера сцены).
    const double next = scale_factor_ * factor;
    if (next < 0.2 || next > 12.0)
    {
        return;
    }
    scale_factor_ = next;
    scale(factor, factor);
    event->accept();
}

void PackageView::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton)
    {
        if (QGraphicsItem* item = itemAt(event->pos()))
        {
            const QVariant pin_number = item->data(0);
            if (pin_number.isValid())
            {
                emit pinClicked(pin_number.toInt());
            }
        }
    }
    QGraphicsView::mousePressEvent(event);
}
