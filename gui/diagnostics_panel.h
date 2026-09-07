//
// Нижняя панель — диагностические сообщения (ТЗ §3.1.9 п.5):
// фильтрация по уровню (Ошибки / Предупреждения / Информация) и переход к
// источнику проблемы по клику.
//
// «Переход к источнику» реализуется через Diagnostic::block_index — индекс
// блока в массиве "peripherals". Панель сама никуда не переходит: она лишь
// сообщает наверх, какой блок выбрал пользователь, а выделяет его дерево.
// Сообщения без привязки к блоку (конфликты выводов затрагивают два блока
// сразу, и указывать один из них было бы произволом) кликом не реагируют.
//
#ifndef MIK32_CONFIGURATOR_DIAGNOSTICS_PANEL_H
#define MIK32_CONFIGURATOR_DIAGNOSTICS_PANEL_H

#include <QWidget>
#include <vector>
#include "project_document.h"

class QTableWidget;
class QCheckBox;
class QLabel;

class DiagnosticsPanel : public QWidget
{
    Q_OBJECT

public:
    explicit DiagnosticsPanel(QWidget* parent = nullptr);

    void setDiagnostics(const std::vector<Diagnostic>& diagnostics);

    int errorCount() const { return error_count_; }
    int warningCount() const { return warning_count_; }

signals:
    void jumpToBlock(int block_index);

private slots:
    void rebuildTable();

private:
    std::vector<Diagnostic> diagnostics_;
    QTableWidget* table_ = nullptr;
    QCheckBox* show_errors_ = nullptr;
    QCheckBox* show_warnings_ = nullptr;
    QCheckBox* show_info_ = nullptr;
    QLabel* summary_ = nullptr;
    int error_count_ = 0;
    int warning_count_ = 0;
};

#endif //MIK32_CONFIGURATOR_DIAGNOSTICS_PANEL_H
