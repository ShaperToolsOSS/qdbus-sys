// Copyright (C) 2026 Shaper Tools
// SPDX-License-Identifier: LGPL-2.1-only

#pragma once

#include "qdbus-sys-global.h"
#include "qdbus-sys-version.h"

#include <QtQml/qqml.h>

#include <QObject>

namespace QDBusSys {

class QDBUS_SYS_EXPORT Connectivity : public QObject
{
    Q_OBJECT
    // State mirrors the daemon and is read-only.
    Q_PROPERTY(bool connected READ connected NOTIFY connectedChanged)
    Q_PROPERTY(bool checkingConnectivity READ checkingConnectivity NOTIFY checkingConnectivityChanged)
    Q_PROPERTY(Connectivity::Level connectivityLevel READ connectivityLevel NOTIFY connectivityLevelChanged)

    QML_NAMED_ELEMENT(Connectivity)

public:
    enum Level {
        EndpointAccessible,
        InternetRoutable,
        InterfaceConnected,
        InterfaceDisconnected
    };
    Q_ENUM(Level)

    Connectivity(QObject *parent = nullptr);
    ~Connectivity() override;

    bool connected() const;
    bool checkingConnectivity() const;
    Level connectivityLevel() const;

    Q_INVOKABLE void checkConnectivity();

signals:
    void connectedChanged();
    void checkingConnectivityChanged();
    void connectivityLevelChanged();

private:
    class Q_DECL_HIDDEN ConnectivityPrivate;
    ConnectivityPrivate *d;
};

} // namespace QDBusSys

Q_DECLARE_METATYPE(QDBusSys::Connectivity::Level)
