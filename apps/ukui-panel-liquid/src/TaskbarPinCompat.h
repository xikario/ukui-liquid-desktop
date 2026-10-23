#pragma once
#include <QObject>
#include <QStringList>
#include <QMap>
class QMenu;
class QWidget;

namespace TaskbarPin {
struct WindowIdentity {
    QString instance, windowClass, executable, workingDirectory;
    QStringList arguments;
};
// Kept separate from OEM widgets so launcher discovery and serialization can
// be tested with real GIO launches, including paths/arguments needing escaping.
QMap<QString,QString> desktopEntry(const QString &path);
bool validDesktop(const QString &path);
QString findDesktop(const WindowIdentity &,const QStringList &applicationDirectories);
QString writeLauncher(const WindowIdentity &,const QString &applicationsDirectory,QString *error);
WindowIdentity windowIdentity(qulonglong window);
}

// Adds the operation to every native application task menu, independent of
// NoDisplay. Uses native slots/settings and exported getters, never field offsets.
class TaskbarPinCompat final : public QObject {
public:
    explicit TaskbarPinCompat(QObject *parent);
    ~TaskbarPinCompat() override;
protected:
    bool eventFilter(QObject *,QEvent *) override;
private:
    void prepare(QMenu *);
    void toggle(QWidget *button);
    void routePinnedLaunch(QWidget *button);
};
