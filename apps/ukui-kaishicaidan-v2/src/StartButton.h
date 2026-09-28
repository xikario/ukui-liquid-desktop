#pragma once

#include <QWidget>
#include <QTimer>
#include <QMenu>

class StartMenu;
class QSocketNotifier;

class StartButton : public QWidget {
    Q_OBJECT
public:
    explicit StartButton(StartMenu *menu);
    ~StartButton() override;

    void positionOnTaskbar();
    void applyX11Immunity();

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void changeEvent(QEvent *) override;
    void enterEvent(QEvent *) override;
    void leaveEvent(QEvent *) override;
    void contextMenuEvent(QContextMenuEvent *) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void watchTaskbar();
    void readTaskbarEvents();
    void watchPanelWindow(unsigned long window);
    void showContextMenu(const QPoint &pos);
    void openTerminalAsAdmin();
    bool isPointInMenuOrSubmenus(const QPoint &globalPos) const;

    void *m_watchDisplay = nullptr;
    unsigned long m_panelWindow = 0, m_panelFrame = 0, m_clientListAtom = 0;
    QSocketNotifier *m_panelNotifier = nullptr;
    QTimer m_repositionTimer;
    StartMenu *m_menu = nullptr;
    bool m_hovered = false;
    QTimer *m_immunityTimer = nullptr;
    QMenu *m_contextMenu = nullptr;
};
