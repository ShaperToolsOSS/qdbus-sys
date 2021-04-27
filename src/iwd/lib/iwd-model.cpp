// Copyright (C) 2026 Shaper Tools
// SPDX-License-Identifier: LGPL-2.1-only

#include "iwd-model.h"

namespace QDBusSys {

class IWDNetworkModel::IWDNetworkModelPrivate
{
public:
    QVariantList cachedNetworkRows;
};

namespace {
QStringList networkIdsForRows(const QVariantList &rows)
{
    QStringList networkIds;
    networkIds.reserve(rows.size());

    for (const QVariant &row : rows) {
        networkIds << row.toMap().value(QStringLiteral("networkId")).toString();
    }

    return networkIds;
}

int rowForNetworkId(const QVariantList &rows, const QString &networkId, int startRow = 0)
{
    for (int row = startRow; row < rows.size(); ++row) {
        if (rows.at(row).toMap().value(QStringLiteral("networkId")).toString() == networkId) {
            return row;
        }
    }

    return -1;
}

bool containsDuplicateNetworkIds(const QStringList &networkIds)
{
    QStringList seenNetworkIds;
    seenNetworkIds.reserve(networkIds.size());

    for (const QString &networkId : networkIds) {
        if (seenNetworkIds.contains(networkId)) {
            return true;
        }

        seenNetworkIds << networkId;
    }

    return false;
}
}

IWDNetworkModel::IWDNetworkModel(QObject *parent)
    : QAbstractListModel(parent), d(new IWDNetworkModelPrivate)
{
}

IWDNetworkModel::~IWDNetworkModel()
{
    delete d;
}

int IWDNetworkModel::rowCount(const QModelIndex &parent) const {
    return parent.isValid() ? 0 : d->cachedNetworkRows.length();
}

namespace {
const QHash<int, QByteArray> &networkRoleNames()
{
    static const QHash<int, QByteArray> names = {
        { IWDNetworkModel::SSIDRole,                 QByteArrayLiteral("ssid") },
        { IWDNetworkModel::SignalStrengthRole,       QByteArrayLiteral("signalStrength") },
        { IWDNetworkModel::SignalInDBMRole,          QByteArrayLiteral("signalInDBM") },
        { IWDNetworkModel::APTypeRole,               QByteArrayLiteral("type") },
        { IWDNetworkModel::NetworkIdRole,            QByteArrayLiteral("networkId") },
        { IWDNetworkModel::ConnectedRole,            QByteArrayLiteral("connected") },
        { IWDNetworkModel::KnownNetworkRole,         QByteArrayLiteral("known") },
        { IWDNetworkModel::AutoConnectRole,          QByteArrayLiteral("autoconnect") },
        { IWDNetworkModel::LastTimeConnectedRole,    QByteArrayLiteral("lastTimeConnected") },
        { IWDNetworkModel::HiddenRole,               QByteArrayLiteral("hidden") }
    };
    return names;
}

// Row maps are keyed by role name; build the keys once.
const QHash<int, QString> &networkRoleKeys()
{
    static const QHash<int, QString> keys = [] {
        QHash<int, QString> result;
        for (auto it = networkRoleNames().cbegin(); it != networkRoleNames().cend(); ++it) {
            result.insert(it.key(), QString::fromLatin1(it.value()));
        }
        return result;
    }();
    return keys;
}
}

QVariant IWDNetworkModel::data(const QModelIndex &index, int role) const {
    if (index.row() < 0 || index.row() >= d->cachedNetworkRows.length())
        return QVariant();

    const auto key = networkRoleKeys().constFind(role);
    if (key == networkRoleKeys().cend())
        return QVariant();
    return d->cachedNetworkRows.at(index.row()).toMap().value(*key);
}

QHash<int, QByteArray> IWDNetworkModel::roleNames() const {
    return networkRoleNames();
}

void IWDNetworkModel::applyNetworkSnapshot(const QVariantList &networkRows)
{
    if (d->cachedNetworkRows == networkRows) {
        return;
    }

    const QStringList oldNetworkIds = networkIdsForRows(d->cachedNetworkRows);
    const QStringList newNetworkIds = networkIdsForRows(networkRows);
    const QList<int> roles = roleNames().keys();

    if (oldNetworkIds == newNetworkIds) {
        const QVariantList oldNetworkRows = d->cachedNetworkRows;
        d->cachedNetworkRows = networkRows;

        int firstChangedRow = -1;
        int lastChangedRow = -1;
        for (int row = 0; row < oldNetworkRows.size(); ++row) {
            const bool rowChanged = oldNetworkRows.at(row) != networkRows.at(row);
            if (rowChanged && firstChangedRow < 0) {
                firstChangedRow = row;
            }
            if (!rowChanged && firstChangedRow >= 0) {
                emit dataChanged(index(firstChangedRow, 0), index(lastChangedRow, 0), roles);
                firstChangedRow = -1;
            }
            if (rowChanged) {
                lastChangedRow = row;
            }
        }

        if (firstChangedRow >= 0) {
            emit dataChanged(index(firstChangedRow, 0), index(lastChangedRow, 0), roles);
        }

        emit networkDataChanged();
        return;
    }

    if (containsDuplicateNetworkIds(oldNetworkIds) || containsDuplicateNetworkIds(newNetworkIds)) {
        beginResetModel();
        d->cachedNetworkRows = networkRows;
        endResetModel();
        emit networkDataChanged();
        return;
    }

    for (int row = d->cachedNetworkRows.size() - 1; row >= 0; --row) {
        const QString networkId = d->cachedNetworkRows.at(row).toMap().value(QStringLiteral("networkId")).toString();
        if (!newNetworkIds.contains(networkId)) {
            beginRemoveRows(QModelIndex(), row, row);
            d->cachedNetworkRows.removeAt(row);
            endRemoveRows();
        }
    }

    for (int targetRow = 0; targetRow < networkRows.size(); ++targetRow) {
        const QVariant &newRow = networkRows.at(targetRow);
        const QString networkId = newRow.toMap().value(QStringLiteral("networkId")).toString();
        const int currentRow = rowForNetworkId(d->cachedNetworkRows, networkId, targetRow);

        if (currentRow < 0) {
            beginInsertRows(QModelIndex(), targetRow, targetRow);
            d->cachedNetworkRows.insert(targetRow, newRow);
            endInsertRows();
            continue;
        }

        if (currentRow != targetRow) {
            beginMoveRows(QModelIndex(), currentRow, currentRow, QModelIndex(), targetRow);
            const QVariant movedRow = d->cachedNetworkRows.takeAt(currentRow);
            d->cachedNetworkRows.insert(targetRow, movedRow);
            endMoveRows();
        }

        if (d->cachedNetworkRows.at(targetRow) != newRow) {
            d->cachedNetworkRows[targetRow] = newRow;
            emit dataChanged(index(targetRow, 0), index(targetRow, 0), roles);
        }
    }

    emit networkDataChanged();
}


} // namespace QDBusSys
