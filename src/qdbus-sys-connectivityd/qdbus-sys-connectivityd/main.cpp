// Copyright (C) 2026 Shaper Tools
// SPDX-License-Identifier: LGPL-2.1-only

#include <QCoreApplication>

#include "qdbus-sys-connectivityd-daemon.h"

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setOrganizationName("shapertools");
    QCoreApplication::setOrganizationDomain("shapertools.com");
    QCoreApplication::setApplicationName("qdbus-sys-connectivityd");

    QDBusSysConnectivityDaemon networkManager;
    return app.exec();
}
