#include <QMessageBox>
#include <QLabel>
#include <QAbstractButton>
#include <QVariantAnimation>

static int runSmartInteractionTest(const QString &root)
{
    QSettings settings;
    settings.setValue("smartSpace/themeMode", 3);
    settings.setValue("smartSpace/defaultHidden", false);
    settings.setValue("smartSpace/alwaysOnTop", false);
    settings.setValue("smartSpace/autoStart", false);
    settings.setValue("systemMonitor/autoStart", false);
    settings.sync();
    DesktopCanvas canvas;
    canvas.show(); canvas.showSmartSpaceWidget();
    canvas.moveSmartSpace(100,100); canvas.resizeSmartSpace(800,500);
    settle(600);
    auto *smart=canvas.findChild<SmartSpaceWidget *>();
    check(smart!=nullptr,"Smart Space exists");
    if (!smart) return 1;
    for (bool pinned : {false,true}) {
        canvas.setSmartSpaceAlwaysOnTop(pinned); settle(100);
        const QRect before=smart->geometry();
        for (int i=0;i<3;++i) {
            smart->hideToNearestEdge();
            check(smart->edgeHidden(),"collapse remains available during rapid toggles");
            auto *retract=canvas.findChild<QWidget *>("smartSpaceRetractFrame");
            check(retract && retract->isVisible(),"collapse animates a cached frame");
            check(retract && retract->geometry()==before,"retraction stays within expanded footprint");
            check(smart->isVisible() && smart->size()==QSize(52,52),"edge entry remains clickable during retraction");
            if (retract) {
                auto *animation=retract->findChild<QVariantAnimation *>();
                check(animation && animation->startValue().toReal()==1.0
                    && animation->endValue().toReal()==0.0 && animation->loopCount()==1
                    && animation->duration()<=170,"retraction is a short one-shot reverse transition");
            }
            settle(30);
            smart->revealFromEdge();
            check(!canvas.findChild<QWidget *>("smartSpaceRetractFrame"),"reopen cancels retraction immediately");
            auto *frame=canvas.findChild<QWidget *>("smartSpaceRevealFrame");
            check(frame && frame->isVisible(),"reveal animates a visible cached frame");
            check(smart->geometry()==before,"animation does not resize/reflow live content");
            settle(30);
        }
        settle(250);
        check(smart->isVisible() && !smart->edgeHidden(),"live content restored after reveal");
        check(!canvas.findChild<QWidget *>("smartSpaceRevealFrame"),"transition frame released after completion");
        check(smart->geometry()==before,"rapid toggles retain expanded geometry");
        smart->hideToNearestEdge();
        const QRect collapsed=smart->geometry();
        settle(250);
        check(smart->isVisible() && smart->edgeHidden() && smart->geometry()==collapsed,
            "completed retraction leaves the edge entry at its final location");
        check(!canvas.findChild<QWidget *>("smartSpaceRetractFrame"),
            "retraction releases its frame and animation when idle");
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
