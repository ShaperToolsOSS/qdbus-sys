// Copyright (C) 2026 Shaper Tools
// SPDX-License-Identifier: LGPL-2.1-only

#pragma once

#include "qdbus-sys-global.h"
#include "qdbus-sys-version.h"

#include <QtQml/qqml.h>
#include <QAbstractListModel>
#include <QVariantList>


namespace QDBusSys {

class IWD;

class QDBUS_SYS_EXPORT IWDNetworkModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int length READ rowCount NOTIFY networkDataChanged)
    QML_NAMED_ELEMENT(IWDNetworkModel)
    QML_UNCREATABLE("Access the network model through IWD.networks")

public:
    enum Roles {
        SSIDRole = Qt::UserRole,
        SignalStrengthRole,
        SignalInDBMRole,
        APTypeRole,
        NetworkIdRole,
        ConnectedRole,
        KnownNetworkRole,
        AutoConnectRole,
        LastTimeConnectedRole,
        HiddenRole,
    };
    Q_ENUM(Roles)

    explicit IWDNetworkModel(QObject *parent = nullptr);
    ~IWDNetworkModel() override;
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

signals:
    void networkDataChanged();

private slots:
    void applyNetworkSnapshot(const QVariantList &networkRows);

private:
    friend class IWD;
    class Q_DECL_HIDDEN IWDNetworkModelPrivate;
    IWDNetworkModelPrivate *d;
};

} // namespace QDBusSys
