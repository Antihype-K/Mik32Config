#include "diagnostics_panel.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTableWidget>
#include <QHeaderView>
#include <QLabel>

namespace
{
    struct LevelStyle
    {
        const char* title;
        QColor color;
    };

    LevelStyle styleFor(DiagnosticSeverity severity)
    {
        switch (severity)
        {
            case DiagnosticSeverity::Error:   return {"Ошибка",       QColor(0xe0, 0x3b, 0x3b)};
            case DiagnosticSeverity::Warning: return {"Предупреждение", QColor(0xf0, 0x8c, 0x28)};
            case DiagnosticSeverity::Info:    return {"Информация",   QColor(0x3d, 0x8b, 0xfd)};
        }
        return {"", Qt::gray};
    }

    QTableWidget* makeTable(QWidget* parent)
    {
        auto* table = new QTableWidget(parent);
        table->setColumnCount(2);
        table->setHorizontalHeaderLabels({"Уровень", "Сообщение"});
        table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
        table->verticalHeader()->setVisible(false);
        table->setWordWrap(true);
        QObject::connect(table->horizontalHeader(), &QHeaderView::geometriesChanged,
            table, &QTableWidget::resizeRowsToContents);
        table->setSelectionBehavior(QAbstractItemView::SelectRows);
        table->setEditTriggers(QAbstractItemView::NoEditTriggers);
        return table;
    }
}

DiagnosticsPanel::DiagnosticsPanel(QWidget* parent)
    : QWidget(parent)
{
    // Две отдельные таблицы вместо одной с чекбоксами-фильтрами: ошибки
    // (блокируют кодоген) и предупреждения+информация (не блокируют) нужно
    // видеть раздельно, не включая/выключая видимость строк вручную.
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);

    auto* errors_box = new QVBoxLayout();
    errors_summary_ = new QLabel(this);
    errors_box->addWidget(errors_summary_);
    errors_table_ = makeTable(this);
    errors_box->addWidget(errors_table_, 1);
    layout->addLayout(errors_box, 1);

    auto* warnings_box = new QVBoxLayout();
    warnings_summary_ = new QLabel(this);
    warnings_box->addWidget(warnings_summary_);
    warnings_table_ = makeTable(this);
    warnings_box->addWidget(warnings_table_, 1);
    layout->addLayout(warnings_box, 1);

    auto jump_handler = [this](QTableWidget* table, int row)
    {
        const QVariant block_index = table->item(row, 0)->data(Qt::UserRole);
        if (block_index.isValid() && block_index.toInt() >= 0)
        {
            const QVariant field_name = table->item(row, 1)->data(Qt::UserRole);
            emit jumpToBlock(block_index.toInt(), field_name.toString());
        }
    };
    connect(errors_table_, &QTableWidget::cellClicked, this,
        [this, jump_handler](int row, int) { jump_handler(errors_table_, row); });
    connect(warnings_table_, &QTableWidget::cellClicked, this,
        [this, jump_handler](int row, int) { jump_handler(warnings_table_, row); });
}

void DiagnosticsPanel::setDiagnostics(const std::vector<Diagnostic>& diagnostics)
{
    diagnostics_ = diagnostics;

    error_count_ = 0;
    warning_count_ = 0;
    for (const Diagnostic& diagnostic : diagnostics_)
    {
        if (diagnostic.severity == DiagnosticSeverity::Error)   ++error_count_;
        if (diagnostic.severity == DiagnosticSeverity::Warning) ++warning_count_;
    }
    errors_summary_->setText(QString::fromUtf8("Ошибки (%1)").arg(error_count_));
    warnings_summary_->setText(QString::fromUtf8("Предупреждения (%1)").arg(warning_count_));

    fillTable(errors_table_, [](DiagnosticSeverity severity)
        { return severity == DiagnosticSeverity::Error; });
    fillTable(warnings_table_, [](DiagnosticSeverity severity)
        { return severity == DiagnosticSeverity::Warning || severity == DiagnosticSeverity::Info; });
}

void DiagnosticsPanel::fillTable(QTableWidget* table, bool (*isWanted)(DiagnosticSeverity))
{
    table->setRowCount(0);

    for (const Diagnostic& diagnostic : diagnostics_)
    {
        if (!isWanted(diagnostic.severity))
        {
            continue;
        }

        const LevelStyle style = styleFor(diagnostic.severity);
        const int row = table->rowCount();
        table->insertRow(row);

        auto* level_item = new QTableWidgetItem(QString::fromUtf8(style.title));
        level_item->setForeground(QBrush(style.color));
        level_item->setData(Qt::UserRole, diagnostic.block_index);
        table->setItem(row, 0, level_item);

        auto* message_item = new QTableWidgetItem(QString::fromStdString(diagnostic.message));
        message_item->setData(Qt::UserRole, QString::fromStdString(diagnostic.field_name));
        if (diagnostic.block_index >= 0)
        {
            message_item->setToolTip(QString::fromUtf8("Щёлкните, чтобы перейти к блоку"));
        }
        table->setItem(row, 1, message_item);
    }

    table->resizeColumnToContents(0);
    table->resizeRowsToContents();
}
