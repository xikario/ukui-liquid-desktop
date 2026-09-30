#pragma once
#include "MenuStyle.h"
#include "MenuTextRecorder.h"

static void checkShortcutPainting(QMenu &menu, QAction &action, const QString &label)
{
    const QRect row = menu.actionGeometry(&action);
    MenuTextRecorder painting(menu);
    const QRectF text = painting.boundsFor(label, row);
    const QRectF shortcut = painting.boundsFor(action.shortcut().toString(QKeySequence::NativeText), row);
    qInfo() << "actual menu text/shortcut bounds" << label << text << shortcut;
    check(!text.isEmpty() && !shortcut.isEmpty(), "label and shortcut are actually painted");
    check(row.contains(text.toAlignedRect()) && row.contains(shortcut.toAlignedRect()),
          "both text columns fit inside the native action row");
    check(shortcut.left()-text.right() >= 8, "label and shortcut have a readable gap without overlap");
}

static int runMenuShortcutTest()
{
    qApp->setStyleSheet(MenuStyle::venturaContextMenuStyleSheet());
    LiquidPopup::theme().reducedMotion = true;
    LiquidPopup::setBackdropProvider([](const QRect &rect, qreal dpr) {
        QImage image(rect.size()*dpr, QImage::Format_RGB32);
        image.setDevicePixelRatio(dpr); image.fill(QColor(40,55,70)); return image;
    });
    const QFont original = QApplication::font();
    QDir().mkpath("artifacts/menu-shortcut");
    for (int points : {10, 18}) {
        QFont font = original; font.setPointSize(points); QApplication::setFont(font);
        FenceWidget fence("回归测试分区", QRect(30,30,460,360)); fence.show(); settle();
        int undoCount = 0;
        QObject::connect(&fence, &FenceWidget::undoRequested, [&]{ ++undoCount; });
        for (bool title : {false, true}) {
            bool inspected = false;
            QTimer inspector;
            inspector.setSingleShot(true);
            QObject::connect(&inspector, &QTimer::timeout, [&] {
                auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget());
                check(menu != nullptr, "actual Fences context menu opens");
                if (!menu) return;
                for (QAction *action : menu->actions()) if (action->text()=="撤回") {
                    inspected = true;
                    check(action->shortcut()==QKeySequence::Undo, "Fences keeps its Undo shortcut");
                    checkShortcutPainting(*menu, *action, "撤回");
                    menu->grab().save(QString("artifacts/menu-shortcut/fence-%1-%2-%3.png")
                        .arg(title?"title":"body").arg(points).arg(menu->devicePixelRatioF()));
                    menu->setActiveAction(action);
                    QKeyEvent confirm(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
                    QApplication::sendEvent(menu, &confirm);
                    break;
                }
                menu->close();
            });
            inspector.start(30);
            const QPoint pos(100, title?15:100);
            QContextMenuEvent event(QContextMenuEvent::Mouse, pos, fence.mapToGlobal(pos));
            QApplication::sendEvent(&fence, &event);
            check(inspected, "actual title/body menu contains and paints Undo");
        }
        check(undoCount==2, "native keyboard activation still delivers Undo exactly once per menu");
    }
    QApplication::setFont(original);
    QSettings settings;
    settings.setValue("smartSpace/autoStart", false);
    settings.setValue("systemMonitor/autoStart", false);
    settings.sync();
    DesktopCanvas canvas; canvas.show(); settle();
    bool inspected = false;
    QTimer inspector; inspector.setSingleShot(true);
    QObject::connect(&inspector, &QTimer::timeout, [&] {
        if (auto *menu=qobject_cast<QMenu *>(QApplication::activePopupWidget())) {
            for (QAction *action : menu->actions()) if (action->text()=="撤销上一步") {
                checkShortcutPainting(*menu, *action, "撤销上一步"); inspected = true;
            }
            menu->close();
        }
    });
    inspector.start(30);
    QContextMenuEvent event(QContextMenuEvent::Mouse, QPoint(900,650), canvas.mapToGlobal(QPoint(900,650)));
    QApplication::sendEvent(&canvas, &event);
    check(inspected, "actual desktop menu shortcut also paints with separate columns");
    LiquidPopup::setBackdropProvider({});
    return failures ? 1 : 0;
}
