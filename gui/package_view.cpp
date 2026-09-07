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
    // важны — вид масштабируется под окно, а затем колёсиком.
    constexpr double kBodySize = 560.0;   // сторона тела микросхемы
    constexpr double kPadLength = 34.0;   // длина вывода наружу от тела
    constexpr double kPadWidth = 12.0;    // ширина вывода
    constexpr int kPinsPerSide = 16;      // QFN-64: 64 / 4

    QColor colorFor(PinState state)
    {
        switch (state)
        {
            // Служебные — тёмно-серые и визуально «мертвые»: пользователь
            // должен с первого взгляда видеть, что там нечего настраивать.
            case PinState::Service:    return QColor(0x55, 0x5b, 0x62);
            case PinState::Free:       return QColor(0xd8, 0xb4, 0x4a);  // «золото» неподключённого вывода
            case PinState::Gpio:       return QColor(0x3d, 0x8b, 0xfd);
            case PinState::Peripheral: return QColor(0xf0, 0x8c, 0x28);
            case PinState::Conflict:   return QColor(0xe0, 0x3b, 0x3b);
        }
        return Qt::gray;
    }
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
    scene()->addEllipse(-half + 26.0, -half + 26.0, 42.0, 42.0,
                        QPen(Qt::NoPen), QBrush(QColor(0xe8, 0xe8, 0xe8)))->setZValue(2.0);

    QGraphicsSimpleTextItem* caption = scene()->addSimpleText(
        QString::fromStdString(package.package));
    caption->setBrush(QColor(0xa8, 0xb0, 0xba));
    QFont caption_font = caption->font();
    caption_font.setPointSizeF(22.0);
    caption->setFont(caption_font);
    caption->setPos(-caption->boundingRect().width() / 2.0, -12.0);
    caption->setZValue(2.0);

    const double step = kBodySize / kPinsPerSide;

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
            case 0:  // левая сторона, сверху вниз
                pad_rect = QRectF(-half - kPadLength, offset - kPadWidth / 2.0, kPadLength, kPadWidth);
                label_pos = QPointF(-half - kPadLength - 8.0, offset);
                break;
            case 1:  // нижняя сторона, слева направо
                pad_rect = QRectF(offset - kPadWidth / 2.0, half, kPadWidth, kPadLength);
                label_pos = QPointF(offset, half + kPadLength + 8.0);
                label_rotation = 90.0;
                break;
            case 2:  // правая сторона, снизу вверх
                pad_rect = QRectF(half, -offset - kPadWidth / 2.0, kPadLength, kPadWidth);
                label_pos = QPointF(half + kPadLength + 8.0, -offset);
                break;
            default:  // верхняя сторона, справа налево
                pad_rect = QRectF(-offset - kPadWidth / 2.0, -half - kPadLength, kPadWidth, kPadLength);
                label_pos = QPointF(-offset, -half - kPadLength - 8.0);
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

        PinItem item;
        item.pad = pad;
        item.multiplexed = pin.isMultiplexed();
        pin_items_.insert(pin.pin, item);

        QGraphicsSimpleTextItem* label = scene()->addSimpleText(
            QString("%1  %2").arg(pin.pin).arg(QString::fromStdString(pin.name)));
        QFont label_font = label->font();
        label_font.setPointSizeF(8.0);
        label->setFont(label_font);
        label->setBrush(pin.isMultiplexed() ? QColor(0xd0, 0xd6, 0xdd) : QColor(0x7d, 0x85, 0x8f));

        const QRectF text_rect = label->boundingRect();
        label->setTransformOriginPoint(0, 0);
        label->setRotation(label_rotation);
        if (side == 0)  // подпись слева — выравнивание по правому краю
        {
            label->setPos(label_pos.x() - text_rect.width(), label_pos.y() - text_rect.height() / 2.0);
        }
        else if (side == 2)
        {
            label->setPos(label_pos.x(), label_pos.y() - text_rect.height() / 2.0);
        }
        else if (side == 1)  // повёрнута на 90°: растёт вниз от точки привязки
        {
            label->setPos(label_pos.x() + text_rect.height() / 2.0, label_pos.y());
        }
        else
        {
            label->setPos(label_pos.x() + text_rect.height() / 2.0, label_pos.y() - text_rect.width());
        }
        label->setZValue(3.0);
    }

    scene()->setSceneRect(scene()->itemsBoundingRect().adjusted(-40, -40, 40, 40));
    fitInView(scene()->sceneRect(), Qt::KeepAspectRatio);
    scale_factor_ = 1.0;
}

void PackageView::setPinStates(const QHash<int, PinState>& states, const QHash<int, QString>& tooltips)
{
    for (auto it = pin_items_.begin(); it != pin_items_.end(); ++it)
    {
        const int pin_number = it.key();
        const PinItem& item = it.value();

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
