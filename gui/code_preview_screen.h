//
// Вкладка «Просмотр кода» — есть в макете Claude Design, отсутствует в
// userflow.drawio как отдельный шаг (схема сразу переходит от генерации к
// файлам на диске + отчёту). Оставлена как ДОПОЛНИТЕЛЬНЫЙ экран поверх
// официальной схемы (решение пользователя, MAKET_DESIGN_TZ.md) — удобно
// свериться с результатом, не открывая файлы отдельно.
//
// Экран строго read-only и ничего не знает о кодогене: содержимое
// подставляет MainWindow::onGenerate, читая уже ЗАПИСАННЫЕ на диск файлы
// (mik32_init.h/.c, main.c, platformio.ini) — а не второй раз запуская
// generateProjectFirmware. Один источник истины — то, что реально легло на
// диск, а не то, что кодоген вернул бы при повторном вызове.
//
#ifndef MIK32_CONFIGURATOR_CODE_PREVIEW_SCREEN_H
#define MIK32_CONFIGURATOR_CODE_PREVIEW_SCREEN_H

#include <QWidget>
#include <QString>

class QPlainTextEdit;
class QTabWidget;

class CodePreviewScreen : public QWidget
{
    Q_OBJECT

public:
    explicit CodePreviewScreen(QWidget* parent = nullptr);

    void setContents(const QString& main_c, const QString& init_c,
                      const QString& init_h, const QString& platformio_ini);

    // Rust-ветка не производит mik32_init.c/.h/platformio.ini (см.
    // docs/implementations/rust_codegen.md) — переиспользует те же четыре
    // вкладки (не заводит отдельный виджет ради двух файлов), но
    // перезаголовливает первые две под main.rs/Cargo.toml и прячет
    // оставшиеся две, вместо показа C-заголовков с чужим содержимым.
    void setContentsRust(const QString& main_rs, const QString& cargo_toml);
    void setContentsArduino(const QString& main_cpp, const QString& platformio_ini);

private:
    QTabWidget* tabs_ = nullptr;
    QPlainTextEdit* main_c_view_ = nullptr;
    QPlainTextEdit* init_c_view_ = nullptr;
    QPlainTextEdit* init_h_view_ = nullptr;
    QPlainTextEdit* ini_view_ = nullptr;
};

#endif //MIK32_CONFIGURATOR_CODE_PREVIEW_SCREEN_H
