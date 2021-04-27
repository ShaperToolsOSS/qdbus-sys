// Copyright (C) 2026 Shaper Tools
// SPDX-License-Identifier: LGPL-2.1-only

#pragma once

#include <QMap>
#include <QString>
#include <QVariantMap>
#include <QDBusObjectPath>
#include <QDBusArgument>
#include <QDBusConnection>

// One entry of Station.GetHiddenAccessPoints: a(sns) of address, RSSI in
// 100 * dBm, and security type.
struct HiddenNetworkEntry {
    QString address;
    qint16 signalStrength = 0;
    QString type;
};
Q_DECLARE_METATYPE(HiddenNetworkEntry)

inline QDBusArgument &operator<<(QDBusArgument &argument, const HiddenNetworkEntry &entry)
{
    argument.beginStructure();
    argument << entry.address << entry.signalStrength << entry.type;
    argument.endStructure();
    return argument;
}

inline const QDBusArgument &operator>>(const QDBusArgument &argument, HiddenNetworkEntry &entry)
{
    argument.beginStructure();
    argument >> entry.address >> entry.signalStrength >> entry.type;
    argument.endStructure();
    return argument;
}

/* Map of dbus types to Qt types*/
using NetworkEntry = QPair<QDBusObjectPath,short>;
using OrderedNetworksList = QList<NetworkEntry>;
using HiddenNetworksList = QList<HiddenNetworkEntry>;
using LevelsList = QList<short>;
using InterfacePropertyMap = QMap<QString, QVariantMap>;
using ManagedObjectList = QMap<QDBusObjectPath, QMap<QString, QVariantMap>>;
