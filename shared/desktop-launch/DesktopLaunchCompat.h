#pragma once

#include <QString>
#include <QStringList>

// Launch a .desktop file through the GIO runtime API available on the host.
// This works on the Kylin GIO build used by the X86 image, where the `gio
// launch` command is not provided.
bool launchDesktopFileCompat(const QString &path, QString *errorMessage = nullptr,
                             const QStringList &files = {});
