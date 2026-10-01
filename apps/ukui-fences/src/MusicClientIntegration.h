#pragma once
#include "MprisPlayer.h"

namespace MusicClientIntegration {
struct ProcessInfo {
    quint64 started=0;
    MusicClientProfile launch;
};
ProcessInfo inspectProcess(uint pid, const QString &service,
                           const QString &procRoot=QStringLiteral("/proc"),
                           const QStringList &applicationDirs={});
// Uses the live process identity, never a title/class-only guess.
bool activateProcessWindow(uint pid, quint64 started);
bool launch(const MusicClientProfile &profile);
}
