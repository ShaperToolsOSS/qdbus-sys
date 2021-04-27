// Copyright (C) 2026 Shaper Tools
// SPDX-License-Identifier: LGPL-2.1-only

#pragma once

#include <QtDBus>

namespace IWDIPC {
inline constexpr auto service = "com.shapertools.qdbussys.iwd";
inline constexpr auto path = "/com/shapertools/qdbussys/iwd";
inline constexpr auto interface = "com.shapertools.qdbussys.iwd1";
inline constexpr int version = 1;
// IWD reports signal levels 0 (strongest) to N (weakest) for N thresholds. The
// broker registers seven thresholds and reports the weakest level whenever no
// network is connected or no measurement is available yet.
inline constexpr int weakestSignalLevel = 7;
inline constexpr auto agentUnavailableError = "com.shapertools.qdbussys.iwd.AgentUnavailable";

// Nested QVariant containers arrive as QDBusArgument when read from a{sv}.
inline QVariant unpack(QVariant value)
{
    if (value.metaType() == QMetaType::fromType<QDBusVariant>()) {
        return unpack(qvariant_cast<QDBusVariant>(value).variant());
    }
    if (value.metaType() == QMetaType::fromType<QDBusArgument>()) {
        const auto argument = qvariant_cast<QDBusArgument>(value);
        if (argument.currentSignature() == QStringLiteral("a{sv}")) {
            value = qdbus_cast<QVariantMap>(argument);
        } else if (argument.currentSignature() == QStringLiteral("as")) {
            return qdbus_cast<QStringList>(argument);
        } else if (argument.currentSignature() == QStringLiteral("av")) {
            value = qdbus_cast<QVariantList>(argument);
        }
    }
    if (value.metaType() == QMetaType::fromType<QVariantMap>()) {
        auto map = value.toMap();
        for (auto it = map.begin(); it != map.end(); ++it) it.value() = unpack(it.value());
        return map;
    }
    if (value.metaType() == QMetaType::fromType<QVariantList>()) {
        auto list = value.toList();
        for (auto &entry : list) entry = unpack(entry);
        return list;
    }
    return value;
}
}
