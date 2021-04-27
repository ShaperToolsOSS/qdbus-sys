// Copyright (C) 2026 Shaper Tools
// SPDX-License-Identifier: LGPL-2.1-only

#pragma once

#include "iwd-controller.h"
#include "dbus-interface-common.h"

#include <QtDBus>
#include <QQueue>
#include <QTimer>
#include <optional>

class IWDManagerAdaptor;

class IWDDaemon : public QObject
{
    Q_OBJECT
public:
    explicit IWDDaemon(QObject *parent = nullptr);
    QVariantMap snapshot() const { return published; }
    void request(const QDBusMessage &message, const QString &client, const QString &id,
                 const QString &device, const QString &action, const QVariantMap &arguments);
    void cancel(const QString &sender, const QString &client, const QString &id);
    bool provideCredentials(const QString &sender, const QString &client, const QString &id, const QString &password);
    void releaseClient(const QString &sender, const QString &client);

private slots:
    void ownerChanged(const QString &service, const QString &oldOwner, const QString &newOwner);
    void interfacesAdded(const QDBusObjectPath &, const InterfacePropertyMap &);
    void interfacesRemoved(const QDBusObjectPath &, const QStringList &);
    void refresh();
    void publish();

private:
    struct Caller {
        QString sender, client, id;
        bool reported = false;
    };
    struct Operation {
        QString id, action;
        QVariantMap arguments;
        QList<Caller> callers;
        bool canceled = false;
    };
    struct DeviceQueue {
        std::optional<Operation> active;
        QQueue<Operation> waiting;
    };
    void startNext(const QString &device);
    void completed(const QString &device, const QString &id, bool success, const QString &error, const QString &message);
    void report(Caller &caller, bool success, const QString &error, const QString &message);
    void removeDevice(const QString &device);
    void scheduleRefresh();
    void watchClient(const QString &sender);
    QVariantMap delta(const QVariantMap &previous, const QVariantMap &next, quint64 baseRevision) const;
    QDBusConnection bus = QDBusConnection::systemBus();
    IWDManagerAdaptor *adaptor = nullptr;
    QDBusServiceWatcher *ownerWatcher = nullptr;
    // Watches only connections with operations, not every name on the bus.
    QDBusServiceWatcher *clientWatcher = nullptr;
    QTimer refreshTimer, publishTimer;
    QString owner, epoch;
    QStringList adapters, devices;
    QMap<QString, IWDController *> controllers;
    QMap<QString, DeviceQueue> queues;
    QVariantMap published;
    quint64 generation = 0, revision = 0;
    bool refreshing = false, refreshPending = false, inventoryReady = false;
};
