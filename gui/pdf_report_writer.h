//
// Рендер отчёта (см. codegen/project/pdf_report_generator.h) в файл PDF.
// Используется только Qt::Gui (QTextDocument печатает в QPdfWriter) — ни
// одна сторонняя PDF-библиотека не добавлена в проект, Qt это уже умеет.
//
#ifndef MIK32_CONFIGURATOR_GUI_PDF_REPORT_WRITER_H
#define MIK32_CONFIGURATOR_GUI_PDF_REPORT_WRITER_H

#include <QString>

// true при успехе. reportText — результат buildConfigReportText().
bool writeConfigReportPdf(const QString& reportText, const QString& outputPath);

#endif
