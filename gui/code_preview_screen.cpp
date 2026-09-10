#include "code_preview_screen.h"

#include <QFont>
#include <QPlainTextEdit>
#include <QTabWidget>
#include <QVBoxLayout>

namespace
{
    QPlainTextEdit* makeCodeView(QTabWidget* tabs, const QString& title)
    {
        QPlainTextEdit* view = new QPlainTextEdit();
        view->setReadOnly(true);
        QFont mono("monospace");
        mono.setStyleHint(QFont::Monospace);
        view->setFont(mono);
        view->setLineWrapMode(QPlainTextEdit::NoWrap);
        view->setPlaceholderText(QString::fromUtf8("Код появится здесь после генерации."));
        tabs->addTab(view, title);
        return view;
    }
}

CodePreviewScreen::CodePreviewScreen(QWidget* parent)
    : QWidget(parent)
{
    QVBoxLayout* root = new QVBoxLayout(this);
    root->setContentsMargins(20, 14, 20, 14);

    tabs_ = new QTabWidget();
    main_c_view_ = makeCodeView(tabs_, QString::fromUtf8("main.c"));
    init_c_view_ = makeCodeView(tabs_, QString::fromUtf8("mik32_init.c"));
    init_h_view_ = makeCodeView(tabs_, QString::fromUtf8("mik32_init.h"));
    ini_view_ = makeCodeView(tabs_, QString::fromUtf8("platformio.ini"));

    root->addWidget(tabs_);
}

void CodePreviewScreen::setContents(const QString& main_c, const QString& init_c,
                                     const QString& init_h, const QString& platformio_ini)
{
    // Восстанавливает C-заголовки/видимость — на случай, если до этого
    // показывали Rust-проект (setContentsRust ниже их меняет).
    tabs_->setTabText(0, QString::fromUtf8("main.c"));
    tabs_->setTabText(1, QString::fromUtf8("mik32_init.c"));
    tabs_->setTabVisible(2, true);
    tabs_->setTabVisible(3, true);

    main_c_view_->setPlainText(main_c);
    init_c_view_->setPlainText(init_c);
    init_h_view_->setPlainText(init_h);
    ini_view_->setPlainText(platformio_ini);
}

void CodePreviewScreen::setContentsRust(const QString& main_rs, const QString& cargo_toml)
{
    tabs_->setTabText(0, QString::fromUtf8("main.rs"));
    tabs_->setTabText(1, QString::fromUtf8("Cargo.toml"));
    tabs_->setTabVisible(2, false);
    tabs_->setTabVisible(3, false);

    main_c_view_->setPlainText(main_rs);
    init_c_view_->setPlainText(cargo_toml);
    init_h_view_->clear();
    ini_view_->clear();
}
