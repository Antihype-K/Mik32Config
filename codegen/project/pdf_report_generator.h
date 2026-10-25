//
// Текстовое содержимое PDF-отчёта о конфигурации — опциональная фича
// (docs/open_tasks.md: "PDF-отчёт о конфигурации", отложено из
// docs/legacy/MAKET_DESIGN_TZ.md §6). Включается пользователем явно
// (кнопка в диалоге "Проект сгенерирован"), не навязывается при каждой
// генерации.
//
// Чистая функция над исходным JSON документа (document()["peripherals"]),
// без привязки к Qt — поэтому тестируется GoogleTest без запуска GUI.
// Рендер в сам PDF (QTextDocument -> QPdfWriter) делает gui/pdf_report_writer.*,
// т.к. QPdfWriter требует Qt::Gui.
//
#ifndef MIK32_CONFIGURATOR_PDF_REPORT_GENERATOR_H
#define MIK32_CONFIGURATOR_PDF_REPORT_GENERATOR_H

#include <string>
#include "json.hpp"

// Список блоков "peripherals" из конфигурации в виде простого текста:
// тип блока + все его поля "ключ: значение". Никакой интерпретации полей —
// отчёт отражает ровно то, что лежит в конфиге, без изобретения разделов.
std::string buildConfigReportText(const nlohmann::json& projectDocument);

#endif
