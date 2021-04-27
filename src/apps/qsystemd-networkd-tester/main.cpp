// Copyright (C) 2026 Shaper Tools
// SPDX-License-Identifier: LGPL-2.1-only

#include "qsystemd-networkd.h"

#include <QCoreApplication>
#include <QDebug>
#include <QMetaEnum>
#include <QStringList>

using QDBusSys::SystemdNetworkd;

namespace
{

template<typename State>
QString stateName(State state)
{
    return QString::fromLatin1(QMetaEnum::fromType<State>().valueToKey(static_cast<int>(state)));
}

struct NetworkState
{
    QString operationalState;
    QString carrierState;
    QString addressState;
    QString onlineState;
    QStringList linkInterfaces;
};

NetworkState snapshot(const SystemdNetworkd &manager)
{
    return {
        .operationalState = stateName(manager.operationalState()),
        .carrierState = stateName(manager.carrierState()),
        .addressState = stateName(manager.addressState()),
        .onlineState = stateName(manager.onlineState()),
        .linkInterfaces = manager.linkInterfaces(),
    };
}

QString formatState(const NetworkState &state)
{
    return QString("operationalState=%1 carrierState=%2 addressState=%3 onlineState=%4 linkInterfaces=[%5]")
        .arg(state.operationalState, state.carrierState, state.addressState, state.onlineState,
             state.linkInterfaces.join(","));
}

QStringList stateDeltas(const NetworkState &oldState, const NetworkState &newState)
{
    QStringList deltas;

    if (oldState.operationalState != newState.operationalState) {
        deltas << QString("operationalState: %1 -> %2")
                      .arg(oldState.operationalState, newState.operationalState);
    }

    if (oldState.carrierState != newState.carrierState) {
        deltas << QString("carrierState: %1 -> %2")
                      .arg(oldState.carrierState, newState.carrierState);
    }

    if (oldState.addressState != newState.addressState) {
        deltas << QString("addressState: %1 -> %2")
                      .arg(oldState.addressState, newState.addressState);
    }

    if (oldState.onlineState != newState.onlineState) {
        deltas << QString("onlineState: %1 -> %2")
                      .arg(oldState.onlineState, newState.onlineState);
    }

    if (oldState.linkInterfaces != newState.linkInterfaces) {
        deltas << QString("linkInterfaces: [%1] -> [%2]")
                      .arg(oldState.linkInterfaces.join(","), newState.linkInterfaces.join(","));
    }

    return deltas;
}

void logSignal(const QString &signalName, NetworkState &lastState, const SystemdNetworkd &manager)
{
    const NetworkState currentState = snapshot(manager);
    QStringList deltas = stateDeltas(lastState, currentState);

    if (deltas.isEmpty()) {
        deltas << "no state delta";
    }

    qInfo().noquote() << signalName << "-" << deltas.join(", ") << "|" << formatState(currentState);
    lastState = currentState;
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication::setOrganizationName("shapertools");
    QCoreApplication::setOrganizationDomain("shapertools.com");
    QCoreApplication::setApplicationName("qsystemd-networkd-tester");

    QCoreApplication app(argc, argv);
    SystemdNetworkd manager;

    NetworkState lastState = snapshot(manager);
    qInfo().noquote() << "qsystemd-networkd-tester starting";
    qInfo().noquote() << "initial state -" << formatState(lastState);
    qInfo().noquote() << "listening for SystemdNetworkd signals";

    QObject::connect(&manager, &SystemdNetworkd::operationalStateChanged, &app, [&]() {
        logSignal("operationalStateChanged", lastState, manager);
    });

    QObject::connect(&manager, &SystemdNetworkd::carrierStateChanged, &app, [&]() {
        logSignal("carrierStateChanged", lastState, manager);
    });

    QObject::connect(&manager, &SystemdNetworkd::addressStateChanged, &app, [&]() {
        logSignal("addressStateChanged", lastState, manager);
    });

    QObject::connect(&manager, &SystemdNetworkd::onlineStateChanged, &app, [&]() {
        logSignal("onlineStateChanged", lastState, manager);
    });

    QObject::connect(&manager, &SystemdNetworkd::linkInterfacesChanged, &app, [&]() {
        logSignal("linkInterfacesChanged", lastState, manager);
    });

    return app.exec();
}
