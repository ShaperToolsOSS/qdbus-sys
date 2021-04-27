// Copyright (C) 2026 Shaper Tools
// SPDX-License-Identifier: LGPL-2.1-only

// Frozen application source for the first release. Keep this compiling and
// linking during 1.x; do not rewrite it to hide a breaking API change. Add a new
// fixture for a new major.
#include <qdbus-sys-version.h>
#include <iwd.h>
#include <iwd-model.h>
#include <qsystemd-networkd.h>
#include <qdbus-sys-connectivityd.h>
#include <QCoreApplication>
#include <QMetaEnum>
#include <type_traits>

#if QDBUS_SYS_VERSION < QDBUS_SYS_VERSION_CHECK(1, 0, 0)
#error "This application requires qdbus-sys 1.0 or later"
#endif

using QDBusSys::IWD;
using QDBusSys::IWDNetworkModel;
using QDBusSys::SystemdNetworkd;
using QDBusSys::Connectivity;

static_assert(std::is_same_v<decltype(&IWD::devices), QStringList (IWD::*)() const>);
static_assert(std::is_same_v<decltype(&IWD::adapters), QStringList (IWD::*)() const>);
static_assert(std::is_same_v<decltype(&IWD::scan), QString (IWD::*)()>);
static_assert(std::is_same_v<decltype(&IWD::connectNetwork), QString (IWD::*)(const QString &)>);
static_assert(std::is_same_v<decltype(&IWD::networks), IWDNetworkModel *(IWD::*)() const>);
static_assert(std::is_same_v<decltype(&IWD::knownNetworks), QVariantList (IWD::*)() const>);
static_assert(std::is_same_v<decltype(&Connectivity::checkConnectivity), void (Connectivity::*)()>);
static_assert(std::is_same_v<decltype(&Connectivity::connected), bool (Connectivity::*)() const>);
static_assert(std::is_same_v<decltype(&SystemdNetworkd::onlineState), SystemdNetworkd::OnlineState (SystemdNetworkd::*)() const>);
static_assert(Connectivity::EndpointAccessible == 0);
static_assert(Connectivity::InternetRoutable == 1);
static_assert(Connectivity::InterfaceConnected == 2);
static_assert(Connectivity::InterfaceDisconnected == 3);
static_assert(int(IWDNetworkModel::SSIDRole) == int(Qt::UserRole));

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    IWD iwd;
    IWDNetworkModel model;
    SystemdNetworkd networkd;
    Connectivity connectivity;
    QObject::connect(&iwd, &IWD::adaptersChanged, &app, [] {});
    QObject::connect(&iwd, &IWD::devicesChanged, &app, [] {});
    QObject::connect(&iwd, &IWD::operationFinished, &app,
                     [](const QString &, bool, const QString &, const QString &) {});
    QObject::connect(&networkd, &SystemdNetworkd::addressStateChanged, &app, [] {});
    QObject::connect(&connectivity, &Connectivity::connectivityLevelChanged, &app, [] {});
    networkd.setVerboseLogging(false);
    if (iwd.available() || !iwd.selectedDevice().isEmpty() || !iwd.adapters().isEmpty()
        || !iwd.devices().isEmpty() || !iwd.networks() || model.rowCount() != 0
        || !iwd.knownNetworks().isEmpty() || iwd.signalStrengthLevel() != 7
        || connectivity.connected() || connectivity.checkingConnectivity()
        || connectivity.connectivityLevel() != Connectivity::InterfaceDisconnected
        || networkd.addressState() != SystemdNetworkd::AddressState::Unknown
        || networkd.onlineState() != SystemdNetworkd::OnlineState::Unknown) {
        return 1;
    }
    return QMetaEnum::fromType<Connectivity::Level>().valueToKey(0) ? 0 : 1;
}
