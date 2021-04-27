// Copyright (C) 2026 Shaper Tools
// SPDX-License-Identifier: LGPL-2.1-only

#include "iwd-daemon.h"
#include "iwd-ipc_p.h"
#include <QCoreApplication>

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("qdbus-sys-iwd"));
    auto bus = QDBusConnection::systemBus();
    if (!bus.isConnected()) return 1;
    IWDDaemon daemon;
    if (!bus.registerObject(QString::fromLatin1(IWDIPC::path), &daemon)
        || !bus.registerService(QString::fromLatin1(IWDIPC::service))) {
        qCritical() << "qdbus-sys-iwd: Cannot publish D-Bus service:" << bus.lastError().message();
        return 1;
    }
    return app.exec();
}
