// Copyright (C) 2026 Shaper Tools
// SPDX-License-Identifier: LGPL-2.1-only

#include "qdbus-sys-connectivityd.h"

#include <QCoreApplication>
#include <QDebug>
#include <QStringList>
#include <QVariant>

using QDBusSys::Connectivity;

namespace
{

struct ConnectivityState
{
    bool connected;
    Connectivity::Level connectivityLevel;
};

QString connectivityLabel(Connectivity::Level connectivityLevel)
{
    switch (connectivityLevel) {
    case Connectivity::EndpointAccessible:
        return QStringLiteral("internet-reachable");
    case Connectivity::InternetRoutable:
        return QStringLiteral("dns-works-healthcheck-failed");
    case Connectivity::InterfaceConnected:
        return QStringLiteral("network-no-dns");
    case Connectivity::InterfaceDisconnected:
        return QStringLiteral("no-network");
    default:
        return QStringLiteral("unknown");
    }
}

ConnectivityState snapshot(const Connectivity &manager)
{
    return {
        .connected = manager.property("connected").toBool(),
        .connectivityLevel = static_cast<Connectivity::Level>(manager.property("connectivityLevel").toInt()),
    };
}

QString formatState(const ConnectivityState &state)
{
    return QStringLiteral("connected=%1 connectivityLevel=%2(%3)")
        .arg(state.connected ? QStringLiteral("true") : QStringLiteral("false"))
        .arg(static_cast<int>(state.connectivityLevel))
        .arg(connectivityLabel(state.connectivityLevel));
}

QStringList stateDeltas(const ConnectivityState &oldState, const ConnectivityState &newState)
{
    QStringList deltas;

    if (oldState.connected != newState.connected) {
        deltas << QStringLiteral("connected: %1 -> %2")
                      .arg(oldState.connected ? QStringLiteral("true") : QStringLiteral("false"))
                      .arg(newState.connected ? QStringLiteral("true") : QStringLiteral("false"));
    }

    if (oldState.connectivityLevel != newState.connectivityLevel) {
        deltas << QStringLiteral("connectivityLevel: %1(%2) -> %3(%4)")
                      .arg(static_cast<int>(oldState.connectivityLevel))
                      .arg(connectivityLabel(oldState.connectivityLevel))
                      .arg(static_cast<int>(newState.connectivityLevel))
                      .arg(connectivityLabel(newState.connectivityLevel));
    }

    return deltas;
}

void logSignal(const QString &signalName, ConnectivityState &lastState, const Connectivity &manager)
{
    const ConnectivityState currentState = snapshot(manager);
    QStringList deltas = stateDeltas(lastState, currentState);

    if (deltas.isEmpty()) {
        deltas << QStringLiteral("no state delta");
    }

    qInfo().noquote() << signalName << "-" << deltas.join(", ") << "|" << formatState(currentState);
    lastState = currentState;
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication::setOrganizationName("shapertools");
    QCoreApplication::setOrganizationDomain("shapertools.com");
    QCoreApplication::setApplicationName("qdbus-sys-connectivityd-tester");

    QCoreApplication app(argc, argv);
    Connectivity manager;

    ConnectivityState lastState = snapshot(manager);
    qInfo().noquote() << "qdbus-sys-connectivityd-tester starting";
    qInfo().noquote() << "initial state -" << formatState(lastState);
    qInfo().noquote() << "listening for Connectivity signals";

    QObject::connect(&manager, &Connectivity::connectedChanged, &app, [&]() {
        logSignal(QStringLiteral("connectedChanged"), lastState, manager);
    });

    QObject::connect(&manager, &Connectivity::connectivityLevelChanged, &app, [&]() {
        logSignal(QStringLiteral("connectivityLevelChanged"), lastState, manager);
    });

    return app.exec();
}
