#include <QMessageBox>
#include <QLabel>
#include <QAbstractButton>
#include <QVariantAnimation>
#include <QGraphicsOpacityEffect>

class SmartTransitionVisibilityProbe final : public QObject
{
public:
    bool recording = false;
    int visibleGeometryChanges = 0;
    int expandedShows = 0;
    int transientEntryShows = 0;
    SmartSpaceWidget *smart = nullptr;
    QToolButton *entry = nullptr;
protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (!recording) return false;
        if (watched == smart) {
            if ((event->type()==QEvent::Resize || event->type()==QEvent::Move)
                && smart->isVisible()) ++visibleGeometryChanges;
            if (event->type()==QEvent::Show && smart->width()>52)
                ++expandedShows;
        }
        if (watched==entry && event->type()==QEvent::Show && smart->width()>52)
            ++transientEntryShows;
        return false;
    }
};

static int runSmartInteractionTest(const QString &root)
{
    QSettings settings;
    settings.setValue("smartSpace/themeMode", 3);
    settings.setValue("smartSpace/defaultHidden", false);
    settings.setValue("smartSpace/alwaysOnTop", false);
    settings.setValue("smartSpace/autoStart", false);
    settings.setValue("systemMonitor/autoStart", false);
    settings.sync();
    // Exercise the populated card hierarchy, not just an empty search pane.
    QJsonArray items;
    for (int i=0; i<120; ++i) {
        const QString path = root + QString("/document-%1.txt").arg(i);
        QFile file(path); file.open(QIODevice::WriteOnly); file.write("fixture");
        items.append(QJsonObject{{"path", path}, {"root", root},
            {"category", "text"}, {"content", "animation fixture"}});
    }
    const QString indexDir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation)
        + "/smart-space";
    QDir().mkpath(indexDir);
    QFile index(indexDir + "/index.json"); index.open(QIODevice::WriteOnly);
    index.write(QJsonDocument(QJsonObject{{"items", items}}).toJson()); index.close();
    DesktopCanvas canvas;
    canvas.show(); canvas.showSmartSpaceWidget();
    canvas.moveSmartSpace(100,100); canvas.resizeSmartSpace(800,500);
    settle(600);
    auto *smart=canvas.findChild<SmartSpaceWidget *>();
    check(smart!=nullptr,"Smart Space exists");
    if (!smart) return 1;
    SmartTransitionVisibilityProbe visibility;
    visibility.smart = smart;
    visibility.entry = smart->findChild<QToolButton *>("smartEdgeReveal");
    smart->installEventFilter(&visibility);
    visibility.entry->installEventFilter(&visibility);
    check(smart->findChildren<QWidget *>("fileResultCard").size() >= 100,
          "transition regression includes a full page of file cards");
    for (bool pinned : {false,true}) {
        canvas.setSmartSpaceAlwaysOnTop(pinned); settle(100);
        const QRect before=smart->geometry();
        for (int i=0;i<3;++i) {
            visibility.recording = true;
            smart->hideToNearestEdge();
            visibility.recording = false;
            check(visibility.visibleGeometryChanges==0 && visibility.transientEntryShows==0,
                  "collapse preparation never exposes resized content or a transient entry window");
            check(smart->edgeHidden(),"collapse remains available during rapid toggles");
            auto *retract=canvas.findChild<QWidget *>("smartSpaceRetractFrame");
            check(retract && retract->isVisible(),"collapse animates a cached frame");
            check(retract && retract->geometry()==before,"retraction stays within expanded footprint");
            check(retract && (pinned || retract->testAttribute(Qt::WA_OpaquePaintEvent)),
                  "desktop transition does not repaint transparent ancestors every frame");
            check(smart->isVisible() && smart->size()==QSize(52,52),"edge entry remains clickable during retraction");
            if (retract) {
                auto *animation=retract->findChild<QVariantAnimation *>();
                check(animation && animation->startValue().toReal()>=0.0
                    && animation->startValue().toReal()<=1.0
                    && animation->endValue().toReal()==0.0 && animation->loopCount()==1
                    && animation->duration()<=200,"retraction is a short one-shot reverse transition");
            }
            settle(30);
            auto *closingAnimation = retract ? retract->findChild<QVariantAnimation *>() : nullptr;
            const qreal closingProgress = closingAnimation ? closingAnimation->currentValue().toReal() : -1;
            auto *edge = smart->findChild<QToolButton *>("smartEdgeReveal");
            auto *opacity = edge ? qobject_cast<QGraphicsOpacityEffect *>(edge->graphicsEffect()) : nullptr;
            check(opacity && qAbs(opacity->opacity() - (1-closingProgress)) < 0.001,
                  "edge entry fades in with the retracting content");
            visibility.recording = true;
            smart->revealFromEdge();
            visibility.recording = false;
            check(visibility.visibleGeometryChanges==0 && visibility.expandedShows==0,
                  "reveal preparation never maps the expanded content before its animation");
            check(!canvas.findChild<QWidget *>("smartSpaceRetractFrame"),"reopen cancels retraction immediately");
            auto *frame=canvas.findChild<QWidget *>("smartSpaceRevealFrame");
            check(frame && frame->isVisible(),"reveal animates a visible cached frame");
            auto *openingAnimation = frame ? frame->findChild<QVariantAnimation *>() : nullptr;
            check(frame == retract && openingAnimation
                      && qAbs(openingAnimation->startValue().toReal()-closingProgress)<0.001,
                  "reversal reuses the frame and continues at exactly the current progress");
            check(smart->geometry()==before,"animation does not resize/reflow live content");
            settle(30);
        }
        settle(250);
        check(smart->isVisible() && !smart->edgeHidden(),"live content restored after reveal");
        check(!canvas.findChild<QWidget *>("smartSpaceRevealFrame"),"transition frame released after completion");
        check(!smart->findChild<QToolButton *>("smartEdgeReveal")->graphicsEffect(),
              "entry effect is released when animation is idle");
        check(smart->geometry()==before,"rapid toggles retain expanded geometry");
        smart->hideToNearestEdge();
        const QRect collapsed=smart->geometry();
        settle(250);
        check(smart->isVisible() && smart->edgeHidden() && smart->geometry()==collapsed,
            "completed retraction leaves the edge entry at its final location");
        check(!canvas.findChild<QWidget *>("smartSpaceRetractFrame"),
            "retraction releases its frame and animation when idle");
        check(smart->property("edgePaintFrames").toInt() > 1,
              "completed transition records actual paint frames");
        smart->revealFromEdge(); settle(250);
        check(smart->geometry()==before,"reveal after completed retraction preserves original geometry");
    }
    canvas.setSmartSpaceAlwaysOnTop(false); settle(100);
    auto *toggle=smart->findChild<QToolButton *>("smartThemeToggle");
    for (int mode=0;mode<3;++mode) {
        bool inspected=false;
        QTimer::singleShot(150,smart,[&] {
            auto *box=qobject_cast<QDialog *>(QApplication::activeModalWidget());
            check(box!=nullptr,"fast full index shows real confirmation");
            if (!box) return;
            auto *label=box->findChild<QLabel *>("smartMessageText");
            check(label && !label->text().isEmpty(),"confirmation contains explanatory text");
            const QColor bg=box->palette().color(QPalette::Window);
            const QColor fg=label ? label->palette().color(QPalette::WindowText) : bg;
            auto luma=[](QColor c) { return .2126*c.red()+.7152*c.green()+.0722*c.blue(); };
            check(qAbs(luma(bg)-luma(fg))>120,"confirmation text has readable contrast in each skin");
            QPushButton *cancel=nullptr;
            for (auto *b:box->findChildren<QPushButton *>())
                if (b->property("messageChoice").toInt()==QMessageBox::No) cancel=b;
            check(cancel!=nullptr,"cancel button is present");
            box->grab().save(root+QString("/confirm-%1.png").arg(mode));
            inspected=true;
            if(cancel) cancel->click(); else box->reject();
        });
        smart->startFastFullIndex();
        check(inspected && !smart->indexBusy(),"canceling confirmation does not start an index");
        if (toggle) toggle->click();
        settle(60);
    }
    return failures ? 1 : 0;
}
