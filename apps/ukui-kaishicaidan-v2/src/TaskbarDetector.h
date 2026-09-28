#pragma once

#include <QRect>
#include <QPoint>

struct TaskbarInfo {
    bool detected = false;
    unsigned long nativeWindow = 0; // Real panel; zero for a fallback estimate.
    int edge = 0;               // 0=left, 1=right, 2=top, 3=bottom
    QRect geometry;             // taskbar rectangle in screen coordinates
    QPoint startButtonCenter;   // estimated center of the start button
    QRect screenGeometry;       // the screen this taskbar belongs to
    // For centered taskbars: the start button is at the visual start of the
    // centered group, not the panel's left edge. We expose both:
    bool panelIsCentered = false;       // widget group centered on panel?
    QPoint panelCenter;                 // horizontal center of the panel
};

class TaskbarDetector {
public:
    static TaskbarInfo detect();

private:
    static TaskbarInfo detectPanelWindow();
    static TaskbarInfo detectFromStrut();
    static TaskbarInfo fallback();
    static bool queryPanelDbus(TaskbarInfo &info);
};
