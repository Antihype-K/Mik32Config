#include "peripheral_tree.h"
#include <QVBoxLayout>
#include <QTreeWidget>
#include <QLineEdit>
#include <QMenu>
#include <QSet>
#include <QHash>

namespace
{
    // Роли данных, в которых узел хранит свой адрес в конфигурации.
    constexpr int kRoleType = Qt::UserRole + 1;
    constexpr int kRoleInstance = Qt::UserRole + 2;
    constexpr int kRoleSupported = Qt::UserRole + 3;
    constexpr int kRoleTitle = Qt::UserRole + 4;  // исходное имя без пользовательского
}

QString PeripheralTree::keyFor(const QString& type, int instance)
{
    return type + ":" + QString::number(instance);
}

PeripheralTree::PeripheralTree(QWidget* parent)
    : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    filter_ = new QLineEdit(this);
    filter_->setPlaceholderText("Поиск по дереву…");  // ТЗ §3.1.9 п.3 — текстовый поиск
    filter_->setClearButtonEnabled(true);
    layout->addWidget(filter_);

    tree_ = new QTreeWidget(this);
    tree_->setHeaderHidden(true);
    tree_->setContextMenuPolicy(Qt::CustomContextMenu);
    layout->addWidget(tree_, 1);

    auto* system_core = new QTreeWidgetItem(tree_, QStringList("System Core"));
    addLeaf(system_core, "Clock", QString(), -1, false);
    auto* gpio_group = new QTreeWidgetItem(system_core, QStringList("GPIO"));
    // GPIO_2 имеет только 8 разрядов против 16 у GPIO_0/1 — факт из
    // knowledge_base/gpio.json (pins_per_instance), подтверждённый на плате.
    // В дереве это не отражается: разряды выбираются в панели свойств.
    for (int instance = 0; instance <= 2; ++instance)
    {
        addLeaf(gpio_group, QString("GPIO_%1").arg(instance), "gpio", instance, true);
    }

    auto* analog = new QTreeWidgetItem(tree_, QStringList("Analog"));
    addLeaf(analog, "(зарезервировано)", QString(), -1, false);

    auto* timers = new QTreeWidgetItem(tree_, QStringList("Timers"));
    for (int instance = 0; instance <= 2; ++instance)
    {
        addLeaf(timers, QString("Timer16_%1").arg(instance), "timer16", instance, true);
    }
    for (int instance = 0; instance <= 2; ++instance)
    {
        // Timer32 требуется ТЗ §3.1.1, но парсера и кодогенерации для него ещё нет.
        addLeaf(timers, QString("Timer32_%1").arg(instance), QString(), -1, false);
    }

    auto* connectivity = new QTreeWidgetItem(tree_, QStringList("Connectivity"));
    for (int instance = 0; instance <= 1; ++instance)
    {
        addLeaf(connectivity, QString("USART_%1").arg(instance), "uart", instance, true);
    }
    for (int instance = 0; instance <= 1; ++instance)
    {
        addLeaf(connectivity, QString("I2C_%1").arg(instance), "i2c", instance, true);
    }
    for (int instance = 0; instance <= 1; ++instance)
    {
        addLeaf(connectivity, QString("SPI_%1").arg(instance), "spi", instance, true);
    }

    tree_->expandAll();

    connect(filter_, &QLineEdit::textChanged, this, &PeripheralTree::onFilterChanged);
    connect(tree_, &QTreeWidget::customContextMenuRequested, this, &PeripheralTree::onContextMenu);
    connect(tree_, &QTreeWidget::currentItemChanged, this, &PeripheralTree::onCurrentItemChanged);
}

QTreeWidgetItem* PeripheralTree::addLeaf(QTreeWidgetItem* parent, const QString& title,
                                         const QString& type, int instance, bool supported)
{
    auto* item = new QTreeWidgetItem(parent, QStringList(title));
    item->setData(0, kRoleType, type);
    item->setData(0, kRoleInstance, instance);
    item->setData(0, kRoleSupported, supported);
    item->setData(0, kRoleTitle, title);
    if (!supported)
    {
        item->setDisabled(true);
        item->setToolTip(0, "Конфигуратор пока не поддерживает этот блок");
    }
    return item;
}

void PeripheralTree::refreshState(const QSet<QString>& enabled, const QHash<QString, QString>& names)
{
    QTreeWidgetItemIterator iterator(tree_);
    while (*iterator)
    {
        QTreeWidgetItem* item = *iterator;
        ++iterator;

        const QString type = item->data(0, kRoleType).toString();
        const int instance = item->data(0, kRoleInstance).toInt();
        if (type.isEmpty() || instance < 0)
        {
            continue;
        }

        const QString key = keyFor(type, instance);
        const QString title = item->data(0, kRoleTitle).toString();
        const bool is_enabled = enabled.contains(key);

        // ТЗ §3.1.9 п.6: пользовательское имя показывается рядом с исходным,
        // а не вместо него — иначе теряется связь с обозначением из даташита,
        // по которому пользователь ищет вывод в документации.
        const QString custom_name = names.value(key);
        item->setText(0, custom_name.isEmpty() ? title : QString("%1 (%2)").arg(title, custom_name));

        QFont item_font = item->font(0);
        item_font.setBold(is_enabled);
        item->setFont(0, item_font);
        item->setForeground(0, is_enabled ? QBrush(QColor(0x3d, 0x8b, 0xfd)) : QBrush());
    }
}

void PeripheralTree::selectPeripheral(const QString& type, int instance)
{
    QTreeWidgetItemIterator iterator(tree_);
    while (*iterator)
    {
        QTreeWidgetItem* item = *iterator;
        ++iterator;
        if (item->data(0, kRoleType).toString() == type
            && item->data(0, kRoleInstance).toInt() == instance)
        {
            tree_->setCurrentItem(item);
            return;
        }
    }
}

PeripheralRef PeripheralTree::currentRef() const
{
    PeripheralRef ref;
    QTreeWidgetItem* item = tree_->currentItem();
    if (item == nullptr)
    {
        return ref;
    }
    ref.type = item->data(0, kRoleType).toString();
    ref.instance = item->data(0, kRoleInstance).toInt();
    ref.supported = item->data(0, kRoleSupported).toBool();
    return ref;
}

void PeripheralTree::onFilterChanged(const QString& text)
{
    QTreeWidgetItemIterator iterator(tree_);
    while (*iterator)
    {
        QTreeWidgetItem* item = *iterator;
        ++iterator;
        if (item->childCount() > 0)
        {
            continue;  // групповые узлы скрываются ниже, по числу видимых детей
        }
        item->setHidden(!text.isEmpty() && !item->text(0).contains(text, Qt::CaseInsensitive));
    }

    // Группа скрывается, если в ней не осталось ни одного видимого потомка —
    // иначе поиск оставляет на экране пустые заголовки разделов.
    for (int i = 0; i < tree_->topLevelItemCount(); ++i)
    {
        QTreeWidgetItem* group = tree_->topLevelItem(i);
        bool any_visible = false;
        std::function<void(QTreeWidgetItem*)> walk = [&](QTreeWidgetItem* node)
        {
            for (int child_index = 0; child_index < node->childCount(); ++child_index)
            {
                QTreeWidgetItem* child = node->child(child_index);
                if (child->childCount() > 0)
                {
                    walk(child);
                    child->setHidden(false);
                }
                any_visible = any_visible || !child->isHidden();
            }
        };
        walk(group);
        group->setHidden(!any_visible);
    }
}

void PeripheralTree::onCurrentItemChanged()
{
    emit selectionChanged(currentRef());
}

void PeripheralTree::onContextMenu(const QPoint& position)
{
    QTreeWidgetItem* item = tree_->itemAt(position);
    if (item == nullptr)
    {
        return;
    }
    tree_->setCurrentItem(item);

    const PeripheralRef ref = currentRef();
    if (!ref.isValid() || !ref.supported)
    {
        return;
    }

    // Состав меню задан ТЗ §3.1.9 п.3 дословно.
    QMenu menu(this);
    QAction* enable = menu.addAction("Включить");
    QAction* disable = menu.addAction("Отключить");
    menu.addSeparator();
    QAction* rename = menu.addAction("Переименовать…");
    QAction* reset = menu.addAction("Сбросить");
    QAction* copy = menu.addAction("Копировать настройки");

    QAction* chosen = menu.exec(tree_->viewport()->mapToGlobal(position));
    if (chosen == enable)       emit enableRequested(ref);
    else if (chosen == disable) emit disableRequested(ref);
    else if (chosen == rename)  emit renameRequested(ref);
    else if (chosen == reset)   emit resetRequested(ref);
    else if (chosen == copy)    emit copySettingsRequested(ref);
}
