#include "LiquidDialog.h"
#include "LiquidButtons.h"
#include <QApplication>
#include <QDialogButtonBox>
#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QScreen>
#include <QVBoxLayout>
#include <QStyle>
#include <QWindow>
#include <QFontComboBox>
#include <QFileInfo>
#include <QMouseEvent>
#include <QProgressDialog>
#include <QSizeGrip>
#include <QToolButton>
#include <QtMath>

namespace LiquidDialog {
namespace {
constexpr int headerInset = 44;
class TitleLabel final : public QLabel {
public:
    explicit TitleLabel(QWidget *parent):QLabel(parent) {
        setTextFormat(Qt::PlainText);
        setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Preferred);
    }
    void setTitle(const QString &value) {
        title=value;setAccessibleName(value);setToolTip(value);updateText();
    }
protected:
    void resizeEvent(QResizeEvent *event) override {QLabel::resizeEvent(event);updateText();}
private:
    void updateText(){setText(fontMetrics().elidedText(title,Qt::ElideRight,width()));}
    QString title;
};
class TitleButton final : public QToolButton {
public:
    enum Kind {Close,Minimize};
    TitleButton(Kind kind,QWidget *parent):QToolButton(parent),kind(kind) {
        setFixedSize(30,30);setAutoRaise(true);setFocusPolicy(Qt::TabFocus);
        setStyleSheet("background:transparent;border:0;padding:0;");
        const QString name=kind==Close?QStringLiteral("关闭窗口"):QStringLiteral("最小化");
        setAccessibleName(name);setToolTip(name);
    }
protected:
    void paintEvent(QPaintEvent *) override {
        QPainter painter(this);painter.setRenderHint(QPainter::Antialiasing);
        if(underMouse() || isDown() || hasFocus()) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(kind==Close?QColor(224,91,105,isDown()?150:95)
                :QColor(119,215,209,isDown()?90:45));
            painter.drawRoundedRect(QRectF(rect()).adjusted(1,1,-1,-1),7,7);
        }
        // A host may update its contrast palette after this button is created;
        // OEM tool-button styles can retain a stale local foreground.
        QColor color=window()->palette().color(QPalette::WindowText);if(!isEnabled())color.setAlphaF(.4);
        painter.setPen(QPen(color,1.5,Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin));
        const QPointF c=QRectF(rect()).center();
        if(kind==Close) {
            painter.drawLine(c+QPointF(-4,-4),c+QPointF(4,4));
            painter.drawLine(c+QPointF(4,-4),c+QPointF(-4,4));
        } else painter.drawLine(c+QPointF(-5,3),c+QPointF(5,3));
    }
private:
    Kind kind;
};
class Surface final : public QObject {
public:
    Surface(QWidget *window,const Options &options):QObject(window),window(window),options(options) {
        setObjectName("liquidDialogController");
        window->setProperty("liquidDialogInstalled",true);
        if(options.material) {
            window->setAttribute(Qt::WA_TranslucentBackground);
            window->setAutoFillBackground(false);
        }
        if(options.colors) {
            QPalette palette=window->palette();
            for(auto role:{QPalette::Window,QPalette::Button})palette.setColor(role,QColor("#243749"));
            for(auto role:{QPalette::Base,QPalette::AlternateBase})palette.setColor(role,QColor("#172b40"));
            for(auto role:{QPalette::WindowText,QPalette::Text,QPalette::ButtonText})palette.setColor(role,QColor("#edf5ff"));
            palette.setColor(QPalette::Highlight,QColor("#367f86"));
            palette.setColor(QPalette::HighlightedText,Qt::white);
            palette.setColor(QPalette::PlaceholderText,QColor("#9bb0c6"));
            window->setPalette(palette);
            window->setProperty("liquidDialogSurface",true);
            window->setStyleSheet(window->styleSheet()+QStringLiteral(
                "\nQWidget[liquidDialogSurface=true] {background:transparent;color:#edf5ff;}"
                "QWidget[liquidDialogSurface=true] QWidget {color:#edf5ff;}"
                "QWidget[liquidDialogSurface=true] QWidget:disabled {color:#758598;}"
                "QWidget[liquidDialogSurface=true] QLabel {background:transparent;}"
                "QWidget[liquidDialogSurface=true] QDialogButtonBox {background:transparent;border:0;}"
                "QWidget[liquidDialogSurface=true] QPushButton {background:rgba(218,238,255,22);"
                "border:1px solid rgba(225,245,255,65);border-radius:8px;padding:6px 12px;}"
                "QWidget[liquidDialogSurface=true] QPushButton:hover {background:rgba(119,215,209,65);}"
                "QWidget[liquidDialogSurface=true] QPushButton:focus {border-color:#63cfc6;}"
                "QWidget[liquidDialogSurface=true] QLineEdit,"
                "QWidget[liquidDialogSurface=true] QSpinBox,"
                "QWidget[liquidDialogSurface=true] QComboBox,"
                "QWidget[liquidDialogSurface=true] QListView,"
                "QWidget[liquidDialogSurface=true] QTreeView,"
                "QWidget[liquidDialogSurface=true] QTableView,"
                "QWidget[liquidDialogSurface=true] QPlainTextEdit {background:rgba(12,22,38,180);"
                "border:1px solid rgba(225,245,255,45);border-radius:6px;padding:4px;}"
                "QWidget[liquidDialogSurface=true] QHeaderView {border:0;padding:0;background:transparent;}"
                "QWidget[liquidDialogSurface=true] QHeaderView::section {background:#243749;color:#edf5ff;border:0;padding:4px;}"
                "QWidget[liquidDialogSurface=true] QScrollBar:vertical {background:rgba(18,31,45,80);width:9px;margin:0;}"
                "QWidget[liquidDialogSurface=true] QScrollBar:horizontal {background:rgba(18,31,45,80);height:9px;margin:0;}"
                "QWidget[liquidDialogSurface=true] QScrollBar::handle {background:rgba(182,215,233,100);border-radius:4px;min-height:24px;min-width:24px;}"
                "QWidget[liquidDialogSurface=true] QScrollBar::add-line,QWidget[liquidDialogSurface=true] QScrollBar::sub-line {width:0;height:0;}"
                "QWidget[liquidDialogSurface=true] QScrollBar::add-page,QWidget[liquidDialogSurface=true] QScrollBar::sub-page {background:transparent;}"
                "QWidget[liquidDialogSurface=true] QGroupBox {border:1px solid rgba(225,245,255,45);"
                "border-radius:9px;margin-top:14px;padding-top:12px;}"
                "QWidget[liquidDialogSurface=true] QGroupBox::title {subcontrol-origin:margin;left:10px;}"
            ));
        }
        if(options.chrome)createChrome();
        // Install before handling Show. Adding a root event filter inside its
        // current Show dispatch can visit this controller twice and recapture
        // the backdrop; child polish/show adoption already handles late pages.
        LiquidButtons::install(window);
        window->installEventFilter(this);
        connect(&motion,&QVariantAnimation::valueChanged,this,[this](const QVariant &value){
            if(this->window)this->window->setWindowOpacity(value.toReal());
        });
        connect(&motion,&QVariantAnimation::finished,this,[this]{completeClosing();});
    }
    bool finish(std::function<void()> completion) {
        if(closing)return true;
        if(!window || !window->isVisible() || !animate())return false;
        ++closeRevision;closing=true;window->setProperty("liquidDialogClosing",true);
        inputEnabled=window->isEnabled();restoreInput=true;window->setEnabled(false);
        done=std::move(completion);motion.stop();
        motion.setStartValue(window->windowOpacity());motion.setEndValue(0.);
        motion.setDuration(LiquidPopup::theme().closeMs);
        motion.setEasingCurve(QEasingCurve::InCubic);motion.start();
        return true;
    }
    void cancelClosing() {
        ++closeRevision;motion.stop();done={};closing=false;restoreControls();
        if(window){window->setWindowOpacity(1);window->setProperty("liquidDialogClosing",false);}
    }
protected:
    bool eventFilter(QObject *watched,QEvent *event) override {
        if(!window)return false;
        if(watched==header || watched==title) {
            if(event->type()==QEvent::MouseButtonPress) {
                auto *mouse=static_cast<QMouseEvent *>(event);
                if(mouse->button()==Qt::LeftButton && !window->isMaximized()) {
                    dragging=true;dragOffset=mouse->globalPos()-window->frameGeometry().topLeft();
                    return true;
                }
            } else if(event->type()==QEvent::MouseMove && dragging) {
                auto *mouse=static_cast<QMouseEvent *>(event);
                if(mouse->buttons().testFlag(Qt::LeftButton))window->move(mouse->globalPos()-dragOffset);
                else dragging=false;
                return true;
            } else if(event->type()==QEvent::MouseButtonRelease) {
                dragging=false;
            }
            return false;
        }
        if(event->type()==QEvent::Show) {
            // Mapping changes are not an explicit request to reopen a dialog.
            // Preserve an accepted/rejected result waiting to finish closing.
            if(event->spontaneous() && closing)return false;
            cancelClosing();
            arrangeChrome();
            syncIcon();
            // Native restore/show-desktop events arrive after the window is
            // mapped. Keep its opening material: recapturing here includes
            // the dialog itself, and activation would override the WM's focus.
            if(event->spontaneous())return false;
            if(options.material && LiquidPopup::isEnabled()) {
                const QRect area(window->mapToGlobal(QPoint()),window->size());
                const qreal dpr=window->devicePixelRatioF();
                const auto background=options.backdrop?options.backdrop(area,dpr):LiquidPopup::captureBackdrop(area,dpr);
                const bool light=window->palette().color(QPalette::WindowText).lightness()<128;
                material=LiquidPopup::renderMenuMaterial(background,window->size(),dpr,light);
                window->setProperty("liquidDialogMaterialBuilds",window->property("liquidDialogMaterialBuilds").toInt()+1);
                window->setProperty("liquidDialogMaterialBytes",material.sizeInBytes());
            }
            LiquidPopup::installComboPopups(window);
            // OEM styles paint an opaque plate behind standard button boxes.
            // Keep the container transparent, including motion-only surfaces.
            for(auto *box:window->findChildren<QDialogButtonBox *>()) {
                box->setAutoFillBackground(false);
                if(!box->property("liquidDialogButtonBox").toBool()) {
                    box->setStyleSheet(box->styleSheet()+"\nQDialogButtonBox {background:transparent;border:0;}");
                    box->setProperty("liquidDialogButtonBox",true);
                }
            }
            if(options.chrome) {
                window->raise();
                window->activateWindow();
            }
            if(animate()) {
                motion.setStartValue(.15);motion.setEndValue(1.);
                motion.setDuration(LiquidPopup::theme().openMs);
                motion.setEasingCurve(QEasingCurve::OutCubic);motion.start();
            }
        } else if(event->type()==QEvent::Paint && options.material) {
            QPainter painter(window);
            painter.setCompositionMode(QPainter::CompositionMode_Source);
            painter.fillRect(window->rect(),Qt::transparent);
            painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
            if(LiquidPopup::isEnabled() && !material.isNull())painter.drawImage(window->rect(),material);
            else painter.fillRect(window->rect(),window->palette().color(QPalette::Window));
        } else if(event->type()==QEvent::Hide) {
            dragging=false;
            if(closing) {
                motion.stop();
                const auto revision=closeRevision;
                // Finish outside the native visibility handler: done() can
                // exit a modal loop or delete the dialog via its host signals.
                QTimer::singleShot(0,this,[this,revision]{
                    if(closing && closeRevision==revision)completeClosing();
                });
            } else cancelClosing();
            if(!event->spontaneous()) {
                material={};window->setProperty("liquidDialogMaterialBytes",0);
            }
        } else if(event->type()==QEvent::Resize) {
            positionChrome();
        } else if(event->type()==QEvent::WindowTitleChange && title) {
            title->setTitle(window->windowTitle());
        } else if(event->type()==QEvent::WindowIconChange && icon) {
            updateIcon();
        }
        return false;
    }
private:
    void completeClosing() {
        auto completion=std::move(done);
        closing=false;restoreControls();
        if(window)window->setProperty("liquidDialogClosing",false);
        if(completion)completion();
    }
    void createChrome() {
        // A Dialog/Tool window can be filtered from the OEM task list. Owned
        // popups remain QDialogs (including their modal/results behavior), but
        // expose a normal task-managed X11 window so minimization has a return
        // path. The title contains only minimize and close.
        auto flags=window->windowFlags();
        flags &= ~(Qt::WindowType_Mask|Qt::WindowMaximizeButtonHint|Qt::WindowContextHelpButtonHint);
        // Qt 5.12 removes Frameless when CustomizeWindowHint is combined with
        // title buttons. Explicit buttons work with Frameless without it.
        flags &= ~Qt::CustomizeWindowHint;
        flags |= Qt::Window|Qt::FramelessWindowHint
            |Qt::WindowMinimizeButtonHint|Qt::WindowCloseButtonHint;
        // Normal task-managed children must remain above their owning settings
        // or launcher window, including a parent reached through page widgets.
        if(auto *parent=window->parentWidget())
            if(parent->window()->windowFlags().testFlag(Qt::WindowStaysOnTopHint))
                flags |= Qt::WindowStaysOnTopHint;
        window->setWindowFlags(flags);
        window->setProperty("liquidDialogChrome",true);
        header=new QWidget(window);header->setObjectName("liquidDialogTitlebar");
        header->setAttribute(Qt::WA_NoSystemBackground);
        auto *row=new QHBoxLayout(header);row->setContentsMargins(4,0,0,0);row->setSpacing(4);
        icon=new QLabel(header);icon->setObjectName("liquidDialogIcon");
        icon->setFixedSize(20,20);icon->setScaledContents(true);row->addWidget(icon);
        title=new TitleLabel(header);title->setObjectName("liquidDialogTitle");
        QFont font=title->font();font.setBold(true);title->setFont(font);
        title->setTitle(window->windowTitle());row->addWidget(title,1);
        if(window->windowFlags().testFlag(Qt::WindowMinimizeButtonHint)) {
            auto *button=new TitleButton(TitleButton::Minimize,header);
            button->setObjectName("liquidDialogMinimize");row->addWidget(button);
            connect(button,&QToolButton::clicked,window,[this]{window->showMinimized();});
        }
        auto *close=new TitleButton(TitleButton::Close,header);
        close->setObjectName("liquidDialogClose");row->addWidget(close);
        // closeEvent/reject remains owned by Qt/the host: safe Cancel/No,
        // progress cancellation, and unsaved-draft prompts must still run.
        connect(close,&QToolButton::clicked,window,[this]{window->close();});
        header->installEventFilter(this);title->installEventFilter(this);
        if(auto *dialog=qobject_cast<QDialog *>(window.data()))
            if(!qobject_cast<QProgressDialog *>(dialog))dialog->setSizeGripEnabled(true);
        positionChrome();
    }
    void arrangeChrome() {
        if(!header)return;
        if(!reserved) {
            if(auto *layout=window->layout()) {
                const auto margins=layout->contentsMargins();
                layout->setContentsMargins(margins.left(),margins.top()+headerInset,
                    margins.right(),margins.bottom());
                layout->activate();
                window->resize(window->size().expandedTo(window->minimumSizeHint()));
                reserved=true;
            } else if(auto *progress=qobject_cast<QProgressDialog *>(window.data())) {
                // Qt 5's progress dialog lays children out itself, ignoring
                // window contents margins. Its label's size hint participates.
                auto *label=progress->findChild<QLabel *>(QString(),Qt::FindDirectChildrenOnly);
                if(label) {
                    const auto margins=label->contentsMargins();
                    label->setContentsMargins(margins.left(),margins.top()+headerInset,
                        margins.right(),margins.bottom());
                    const QSize hint=progress->sizeHint();
                    progress->setMinimumSize(progress->minimumSize().expandedTo(hint));
                    progress->resize(progress->size().expandedTo(hint));
                }
                reserved=true;
            }
        }
        positionChrome();header->show();header->raise();
    }
    void positionChrome() {
        if(!header)return;
        header->setGeometry(12,8,qMax(0,window->width()-24),32);
        header->raise();
    }
    void syncIcon() {
        // Make the owning task window's icon explicit for native task lists
        // and the custom header; null-icon hosts still get a readable fallback.
        if(!window->testAttribute(Qt::WA_SetWindowIcon)) {
            QIcon selected;
            if(auto *parent=window->parentWidget())selected=parent->window()->windowIcon();
            if(selected.isNull())selected=QApplication::windowIcon();
            if(selected.isNull())selected=QIcon::fromTheme("preferences-desktop",
                window->style()->standardIcon(QStyle::SP_DesktopIcon));
            window->setWindowIcon(selected);
        }
        updateIcon();
    }
    void updateIcon() {
        if(!icon)return;
        const qreal dpr=window->devicePixelRatioF();
        QPixmap pixmap=window->windowIcon().pixmap(qCeil(20*dpr),qCeil(20*dpr));
        pixmap.setDevicePixelRatio(dpr);icon->setPixmap(pixmap);
    }
    void restoreControls() {
        if(restoreInput && window)window->setEnabled(inputEnabled);
        restoreInput=false;
    }
    bool animate() const {
        return LiquidPopup::isEnabled() && !LiquidPopup::theme().reducedMotion
            && QGuiApplication::platformName()=="xcb";
    }
    QPointer<QWidget> window;
    QPointer<QWidget> header;
    QPointer<QLabel> icon;
    QPointer<TitleLabel> title;
    Options options;
    QImage material;
    QVariantAnimation motion;
    std::function<void()> done;
    quint64 closeRevision=0;
    bool closing=false,inputEnabled=true,restoreInput=false;
    bool reserved=false,dragging=false;
    QPoint dragOffset;
};
Surface *surface(QWidget *window) {
    return window?dynamic_cast<Surface *>(window->findChild<QObject *>("liquidDialogController")):nullptr;
}
bool finish(QWidget *window,std::function<void()> completion) {
    auto *controller=surface(window);
    return controller && controller->finish(std::move(completion));
}
template<class Base> class AnimatedDialog final : public Base {
public:
    using Base::Base;
    void done(int result) override {
        if(!finish(this,[this,result]{Base::done(result);}))Base::done(result);
    }
};
class Message final : public Dialog {
public:
    using Dialog::Dialog;
    int escape=QMessageBox::Cancel;
    void reject() override {done(escape);}
};
QString caption(QMessageBox::StandardButton button) {
    switch(button) {
    case QMessageBox::Yes:return QStringLiteral("确定");
    case QMessageBox::No:return QStringLiteral("取消");
    case QMessageBox::Cancel:return QStringLiteral("取消");
    case QMessageBox::Ok:return QStringLiteral("确定");
    case QMessageBox::Retry:return QStringLiteral("重试");
    case QMessageBox::Ignore:return QStringLiteral("忽略");
    case QMessageBox::Abort:return QStringLiteral("终止");
    case QMessageBox::Save:return QStringLiteral("保存");
    case QMessageBox::Discard:return QStringLiteral("放弃");
    case QMessageBox::Close:return QStringLiteral("关闭");
    default:return QStringLiteral("确定");
    }
}
QMessageBox::StandardButton execute(QWidget *parent,const QString &title,const QString &text,
    QMessageBox::Icon icon,QMessageBox::StandardButtons buttons,QMessageBox::StandardButton defaultButton) {
    QPointer<Dialog> dialog=createMessage(parent,title,text,icon,buttons,defaultButton);
    const int result=dialog->exec();
    if(dialog)delete dialog.data();
    if(!result)return buttons.testFlag(QMessageBox::Cancel)?QMessageBox::Cancel
        :buttons.testFlag(QMessageBox::No)?QMessageBox::No:QMessageBox::NoButton;
    return static_cast<QMessageBox::StandardButton>(result);
}
QString chooseFile(QWidget *parent,const QString &title,const QString &dir,const QString &filter,
    QString *selectedFilter,QFileDialog::Options options,bool save,bool directory,bool images=false) {
    QPointer<FileDialog> picker=new FileDialog(parent,title,dir,filter);
    picker->setOptions(options|QFileDialog::DontUseNativeDialog);
    picker->setAcceptMode(save?QFileDialog::AcceptSave:QFileDialog::AcceptOpen);
    picker->setFileMode(directory?QFileDialog::Directory:save?QFileDialog::AnyFile:QFileDialog::ExistingFile);
    if(selectedFilter && !selectedFilter->isEmpty())picker->selectNameFilter(*selectedFilter);
    install(picker);
    if(images)installImageThumbnails(picker);
    const int result=picker->exec();
    QString path;
    if(picker) {
        if(result==QDialog::Accepted && !picker->selectedFiles().isEmpty())path=picker->selectedFiles().first();
        if(selectedFilter)*selectedFilter=picker->selectedNameFilter();
        delete picker.data();
    }
    return path;
}
}
void install(QWidget *window,const Options &options) {
    if(!window || window->property("liquidDialogInstalled").toBool()
        || qEnvironmentVariable("UKUI_LIQUID_POPUP")=="0")return;
    new Surface(window,options);
}
void installMotion(QWidget *window,bool chrome) {
    Options options;options.material=options.colors=false;options.chrome=chrome;install(window,options);
}
void reopen(QWidget *window) {
    if(!window)return;
    if(auto *controller=surface(window))controller->cancelClosing();
    window->showNormal();window->raise();window->activateWindow();
}
Dialog::Dialog(QWidget *parent,Qt::WindowFlags flags):QDialog(parent,flags){install(this);}
void Dialog::done(int result) {
    if(!finish(this,[this,result]{QDialog::done(result);}))QDialog::done(result);
}
void FileDialog::done(int result) {
    if(!finish(this,[this,result]{QFileDialog::done(result);}))QFileDialog::done(result);
}
void FileDialog::accept() {
    // Keep Qt's validation and selection logic. Route only its overwrite
    // question through the owned surface, avoiding an OEM color mismatch.
    const bool confirm=acceptMode()==AcceptSave && !testOption(DontConfirmOverwrite);
    const auto files=selectedFiles();
    if(confirm && !files.isEmpty() && QFileInfo(files.first()).isFile()) {
        const QPointer<FileDialog> guard(this);
        const auto answer=question(this,QStringLiteral("确认覆盖"),
            QStringLiteral("文件已存在，是否替换？\n%1").arg(files.first()),QMessageBox::Yes|QMessageBox::No,QMessageBox::No);
        if(!guard || answer!=QMessageBox::Yes)return;
        setOption(DontConfirmOverwrite,true);
        QFileDialog::accept();
        if(guard)setOption(DontConfirmOverwrite,false);
    } else QFileDialog::accept();
}
Dialog *createMessage(QWidget *parent,const QString &title,const QString &text,QMessageBox::Icon icon,
    QMessageBox::StandardButtons buttons,QMessageBox::StandardButton defaultButton) {
    auto *dialog=new Message(parent);dialog->setWindowTitle(title);
    dialog->setObjectName("liquidMessageDialog");
    auto *layout=new QVBoxLayout(dialog);layout->setContentsMargins(22,20,22,18);layout->setSpacing(16);
    auto *row=new QHBoxLayout;row->setSpacing(16);
    if(icon!=QMessageBox::NoIcon) {
        auto *symbol=new QLabel(dialog);
        const auto standard=icon==QMessageBox::Question?QStyle::SP_MessageBoxQuestion
            :icon==QMessageBox::Warning?QStyle::SP_MessageBoxWarning
            :icon==QMessageBox::Critical?QStyle::SP_MessageBoxCritical:QStyle::SP_MessageBoxInformation;
        symbol->setPixmap(dialog->style()->standardIcon(standard).pixmap(32,32));
        row->addWidget(symbol,0,Qt::AlignTop);
    }
    auto *label=new QLabel(text,dialog);label->setObjectName("liquidMessageText");
    label->setTextFormat(Qt::PlainText);label->setWordWrap(true);
    label->setMinimumWidth(300);label->setMaximumWidth(620);
    label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    row->addWidget(label,1);layout->addLayout(row);
    auto *box=new QDialogButtonBox(dialog);layout->addWidget(box);
    if(buttons.testFlag(QMessageBox::Cancel))dialog->escape=QMessageBox::Cancel;
    else if(buttons.testFlag(QMessageBox::No))dialog->escape=QMessageBox::No;
    else if(buttons.testFlag(QMessageBox::Close))dialog->escape=QMessageBox::Close;
    else if(buttons==QMessageBox::Ok)dialog->escape=QMessageBox::Ok;
    if(defaultButton==QMessageBox::NoButton)defaultButton=static_cast<QMessageBox::StandardButton>(dialog->escape);
    for(auto choice:{QMessageBox::Yes,QMessageBox::No,QMessageBox::Ok,QMessageBox::Cancel,
        QMessageBox::Save,QMessageBox::Discard,QMessageBox::Retry,QMessageBox::Ignore,
        QMessageBox::Abort,QMessageBox::Close}) {
        if(!buttons.testFlag(choice))continue;
        const bool reject=choice==QMessageBox::No || choice==QMessageBox::Cancel || choice==QMessageBox::Close;
        auto *button=box->addButton(caption(choice),reject?QDialogButtonBox::RejectRole:QDialogButtonBox::AcceptRole);
        button->setProperty("messageChoice",int(choice));
        button->setAutoDefault(false);button->setDefault(choice==defaultButton);
        if(choice==defaultButton)button->setFocus();
        QObject::connect(button,&QPushButton::clicked,dialog,[dialog,choice]{dialog->done(choice);});
    }
    return dialog;
}
QMessageBox::StandardButton question(QWidget *p,const QString &t,const QString &s,QMessageBox::StandardButtons b,QMessageBox::StandardButton d){return execute(p,t,s,QMessageBox::Question,b,d);}
QMessageBox::StandardButton information(QWidget *p,const QString &t,const QString &s,QMessageBox::StandardButtons b,QMessageBox::StandardButton d){return execute(p,t,s,QMessageBox::Information,b,d);}
QMessageBox::StandardButton warning(QWidget *p,const QString &t,const QString &s,QMessageBox::StandardButtons b,QMessageBox::StandardButton d){return execute(p,t,s,QMessageBox::Warning,b,d);}
QMessageBox::StandardButton critical(QWidget *p,const QString &t,const QString &s,QMessageBox::StandardButtons b,QMessageBox::StandardButton d){return execute(p,t,s,QMessageBox::Critical,b,d);}
QString getText(QWidget *parent,const QString &title,const QString &label,QLineEdit::EchoMode mode,const QString &text,bool *ok) {
    QPointer<Dialog> dialog=new Dialog(parent);dialog->setWindowTitle(title);dialog->setObjectName("liquidInputDialog");
    auto *layout=new QVBoxLayout(dialog);layout->setContentsMargins(20,18,20,18);
    layout->addWidget(new QLabel(label,dialog));
    auto *edit=new QLineEdit(text,dialog);edit->setEchoMode(mode);edit->setMinimumWidth(320);
    edit->setObjectName("liquidInputText");layout->addWidget(edit);edit->selectAll();edit->setFocus();
    auto *box=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel,dialog);layout->addWidget(box);
    box->button(QDialogButtonBox::Ok)->setText("确定");box->button(QDialogButtonBox::Cancel)->setText("取消");
    QObject::connect(box,&QDialogButtonBox::accepted,dialog,&QDialog::accept);
    QObject::connect(box,&QDialogButtonBox::rejected,dialog,&QDialog::reject);
    const int result=dialog->exec();
    const bool accepted=dialog && result==QDialog::Accepted;
    const QString value=accepted?edit->text():QString();
    if(ok)*ok=accepted;
    if(dialog)delete dialog.data();
    return value;
}
QString getOpenFileName(QWidget *p,const QString &c,const QString &d,const QString &f,QString *selected,QFileDialog::Options o){return chooseFile(p,c,d,f,selected,o,false,false);}
QString getOpenImageName(QWidget *p,const QString &c,const QString &d,const QString &f,QString *selected,QFileDialog::Options o){return chooseFile(p,c,d,f,selected,o,false,false,true);}
QString getSaveFileName(QWidget *p,const QString &c,const QString &d,const QString &f,QString *selected,QFileDialog::Options o){return chooseFile(p,c,d,f,selected,o,true,false);}
QString getExistingDirectory(QWidget *p,const QString &c,const QString &d,QFileDialog::Options o){return chooseFile(p,c,d,{},nullptr,o,false,true);}
QColor getColor(const QColor &initial,QWidget *parent,const QString &title,QColorDialog::ColorDialogOptions options) {
    QPointer<AnimatedDialog<QColorDialog>> picker=new AnimatedDialog<QColorDialog>(initial,parent);
    picker->setWindowTitle(title);picker->setOptions(options|QColorDialog::DontUseNativeDialog);install(picker);
    const int result=picker->exec();QColor color;
    if(picker){if(result==QDialog::Accepted)color=picker->selectedColor();delete picker.data();}
    return color;
}
}
