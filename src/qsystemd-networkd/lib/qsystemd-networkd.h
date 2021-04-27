// Copyright (C) 2026 Shaper Tools
// SPDX-License-Identifier: LGPL-2.1-only

#pragma once

#include "qdbus-sys-global.h"
#include "qdbus-sys-version.h"

#include <QtQml/qqml.h>

#include <QObject>
#include <QStringList>

namespace QDBusSys {

class QDBUS_SYS_EXPORT SystemdNetworkd : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("RegisterEnumClassesUnscoped", "false")
    Q_PROPERTY(bool verboseLogging READ verboseLogging WRITE setVerboseLogging NOTIFY verboseLoggingChanged)
    // State mirrors org.freedesktop.network1.Manager and is read-only.
    Q_PROPERTY(OperationalState operationalState READ operationalState NOTIFY operationalStateChanged)
    Q_PROPERTY(CarrierState carrierState READ carrierState NOTIFY carrierStateChanged)
    Q_PROPERTY(AddressState addressState READ addressState NOTIFY addressStateChanged)
    Q_PROPERTY(OnlineState onlineState READ onlineState NOTIFY onlineStateChanged)
    Q_PROPERTY(QStringList linkInterfaces READ linkInterfaces NOTIFY linkInterfacesChanged)

    QML_NAMED_ELEMENT(SystemdNetworkd)

public:
    // Unknown also covers unavailable or unrecognized networkd state strings.
    enum class OperationalState {
        Unknown,
        Missing,
        Off,
        Failed,
        NoCarrier,
        Dormant,
        DegradedCarrier,
        Carrier,
        Enslaved,
        Degraded,
        Routable,
    };
    Q_ENUM(OperationalState)

    enum class CarrierState {
        Unknown,
        Off,
        NoCarrier,
        Dormant,
        DegradedCarrier,
        Carrier,
        Enslaved,
    };
    Q_ENUM(CarrierState)

    enum class AddressState {
        Unknown,
        Off,
        Degraded,
        Routable,
    };
    Q_ENUM(AddressState)

    enum class OnlineState {
        Unknown,
        Offline,
        Partial,
        Online,
    };
    Q_ENUM(OnlineState)

    SystemdNetworkd(QObject *parent = nullptr);
    ~SystemdNetworkd() override;

    bool verboseLogging() const;
    void setVerboseLogging(bool enabled);

    OperationalState operationalState() const;
    CarrierState carrierState() const;
    AddressState addressState() const;
    OnlineState onlineState() const;
    QStringList linkInterfaces() const;

signals:
    void verboseLoggingChanged();
    void operationalStateChanged();
    void carrierStateChanged();
    void addressStateChanged();
    void onlineStateChanged();
    void linkInterfacesChanged();

private:
    class Q_DECL_HIDDEN SystemdNetworkdPrivate;
    SystemdNetworkdPrivate *d;
};

} // namespace QDBusSys
