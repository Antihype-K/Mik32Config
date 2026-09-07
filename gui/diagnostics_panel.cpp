#include "diagnostics_panel.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTableWidget>
#include <QHeaderView>
#include <QCheckBox>
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
}

DiagnosticsPanel::DiagnosticsPanel(QWidget* parent)
    : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);

    auto* controls = new QHBoxLayout();
    show_errors_ = new QCheckBox("Ошибки", this);
    show_warnings_ = new QCheckBox("Предупреждения", this);
    show_info_ = new QCheckBox("Информация", this);
    show_errors_->setChecked(true);
    show_warnings_->setChecked(true);
    show_info_->setChecked(true);
    controls->addWidget(show_errors_);
    controls->addWidget(show_warnings_);
    controls->addWidget(show_info_);
    controls->addStretch(1);
    summary_ = new QLabel(this);
    controls->addWidget(summary_);
    layout->addLayout(controls);

    table_ = new QTableWidget(this);
    table_->setColumnCount(2);
    table_->setHorizontalHeaderLabels({"Уровень", "Сообщение"});
    table_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    table_->verticalHeader()->setVisible(false);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    layout->addWidget(table_, 1);

    connect(show_errors_, &QCheckBox::toggled, this, &DiagnosticsPanel::rebuildTable);
    connect(show_warnings_, &QCheckBox::toggled, this, &DiagnosticsPanel::rebuildTable);
    connect(show_info_, &QCheckBox::toggled, this, &DiagnosticsPanel::rebuildTable);

    connect(table_, &QTableWidget::cellClicked, this, [this](int row, int)
    {
        // Индекс блока хранится в самой ячейке: после фильтрации номер строки
        // в таблице уже не совпадает с позицией в diagnostics_.
        const QVariant block_index = table_->item(row, 0)->data(Qt::UserRole);
        if (block_index.isValid() && block_index.toInt() >= 0)
        {
            emit jumpToBlock(block_index.toInt());
        }
    });
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
    summary_->setText(QString("Ошибок: %1    Предупреждений: %2").arg(error_count_).arg(warning_count_));

    rebuildTable();
}

void DiagnosticsPanel::rebuildTable()
{
    table_->setRowCount(0);

    for (const Diagnostic& diagnostic : diagnostics_)
    {
        const bool visible =
            (diagnostic.severity == DiagnosticSeverity::Error   && show_errors_->isChecked()) ||
            (diagnostic.severity == DiagnosticSeverity::Warning && show_warnings_->isChecked()) ||
            (diagnostic.severity == DiagnosticSeverity::Info    && show_info_->isChecked());
        if (!visible)
        {
            continue;
        }

        const LevelStyle style = styleFor(diagnostic.severity);
        const int row = table_->rowCount();
        table_->insertRow(row);

        auto* level_item = new QTableWidgetItem(QString::fromUtf8(style.title));
        level_item->setForeground(QBrush(style.color));
        level_item->setData(Qt::UserRole, diagnostic.block_index);
        table_->setItem(row, 0, level_item);

        auto* message_item = new QTableWidgetItem(QString::fromStdString(diagnostic.message));
        if (diagnostic.block_index >= 0)
        {
            message_item->setToolTip("Щёлкните, чтобы перейти к блоку");
        }
        table_->setItem(row, 1, message_item);
    }

    table_->resizeColumnToContents(0);
}
