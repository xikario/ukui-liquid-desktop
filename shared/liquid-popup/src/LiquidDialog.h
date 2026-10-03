#pragma once
#include "LiquidPopup.h"
#include <QDialog>
#include <QMessageBox>
#include <QFileDialog>
#include <QColorDialog>
#include <QLineEdit>

namespace LiquidDialog {
struct Options {
    bool material = true;
    bool colors = true;
    bool chrome = true;
    LiquidPopup::BackdropProvider backdrop;
};
// Opt in only owned windows. Native file/color controls and modal results stay
// in Qt; OEM windows are never restyled through a global dialog filter.
void install(QWidget *window, const Options &options = {});
// Existing frameless hosts retain their own header unless explicitly opted in.
void installMotion(QWidget *window, bool chrome = false);
void reopen(QWidget *window);
class Dialog : public QDialog {
public:
    explicit Dialog(QWidget *parent=nullptr,Qt::WindowFlags flags={});
    void done(int result) override;
};
class FileDialog : public QFileDialog {
public:
    using QFileDialog::QFileDialog;
    void done(int result) override;
    void accept() override;
};
Dialog *createMessage(QWidget *parent,const QString &title,const QString &text,
    QMessageBox::Icon icon,QMessageBox::StandardButtons buttons=QMessageBox::Ok,
    QMessageBox::StandardButton defaultButton=QMessageBox::NoButton);
QMessageBox::StandardButton question(QWidget *,const QString &,const QString &,
    QMessageBox::StandardButtons=QMessageBox::Yes|QMessageBox::No,
    QMessageBox::StandardButton=QMessageBox::NoButton);
QMessageBox::StandardButton information(QWidget *,const QString &,const QString &,
    QMessageBox::StandardButtons=QMessageBox::Ok,QMessageBox::StandardButton=QMessageBox::NoButton);
QMessageBox::StandardButton warning(QWidget *,const QString &,const QString &,
    QMessageBox::StandardButtons=QMessageBox::Ok,QMessageBox::StandardButton=QMessageBox::NoButton);
QMessageBox::StandardButton critical(QWidget *,const QString &,const QString &,
    QMessageBox::StandardButtons=QMessageBox::Ok,QMessageBox::StandardButton=QMessageBox::NoButton);
QString getText(QWidget *,const QString &,const QString &,QLineEdit::EchoMode=QLineEdit::Normal,
                const QString &text={},bool *ok=nullptr);
QString getOpenFileName(QWidget *parent=nullptr,const QString &caption={},const QString &dir={},
    const QString &filter={},QString *selectedFilter=nullptr,QFileDialog::Options options={});
// Opt-in image browsing: visible items get bounded asynchronous thumbnails.
// Ordinary file/save/directory pickers keep their existing Qt views.
void installImageThumbnails(QFileDialog *picker);
QString getOpenImageName(QWidget *parent=nullptr,const QString &caption={},const QString &dir={},
    const QString &filter={},QString *selectedFilter=nullptr,QFileDialog::Options options={});
QString getSaveFileName(QWidget *parent=nullptr,const QString &caption={},const QString &dir={},
    const QString &filter={},QString *selectedFilter=nullptr,QFileDialog::Options options={});
QString getExistingDirectory(QWidget *parent=nullptr,const QString &caption={},const QString &dir={},
    QFileDialog::Options options=QFileDialog::ShowDirsOnly);
QColor getColor(const QColor &initial=Qt::white,QWidget *parent=nullptr,const QString &title={},
               QColorDialog::ColorDialogOptions options={});
}
