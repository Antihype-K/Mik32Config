#include "pdf_report_writer.h"

#include <QFile>
#include <QPdfWriter>
#include <QTextDocument>

bool writeConfigReportPdf(const QString& reportText, const QString& outputPath)
{
    // QPdfWriter не сообщает об ошибке открытия файла через свой API —
    // проверяем возможность записи заранее, как это обычно делают с QFile.
    QFile probe(outputPath);
    if (!probe.open(QIODevice::WriteOnly))
    {
        return false;
    }
    probe.close();

    QTextDocument doc;
    doc.setPlainText(reportText);

    QPdfWriter writer(outputPath);
    doc.print(&writer);
    return true;
}
