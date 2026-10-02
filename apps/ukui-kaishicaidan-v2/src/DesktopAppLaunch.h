#pragma once
#include <QString>

// Runs in a short-lived helper process, before constructing the launcher UI.
int launchDesktopFile(const QString &path);
