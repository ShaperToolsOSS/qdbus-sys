// Copyright (C) 2026 Shaper Tools
// SPDX-License-Identifier: LGPL-2.1-only

#pragma once

#include <QLoggingCategory>
Q_DECLARE_LOGGING_CATEGORY(IWDBackendLog)
Q_DECLARE_LOGGING_CATEGORY(IWDBackendPropertiesLog)

#include <QObject>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

class QThread;

// Observes one IWD station device on a worker thread and publishes its state as
// plain snapshots. All IWD state comes from ObjectManager and PropertiesChanged
// data; the controller never issues blocking D-Bus calls.
class IWDController : public QObject
{
    Q_OBJECT

public:
    enum Error {
        ScanFailed = Qt::UserRole,
        ConnectFailed,
        ForgetFailed,
        DisconnectFailed,
        GetManagedObjectsFailed,
        RegisterAuthAgentFailed,
        RegisterSignalLevelAgentFailed,
        SetAutoConnectFailed
    };
    Q_ENUM(Error)

    explicit IWDController(const QString &devicePath = {}, QObject *parent = nullptr);
    ~IWDController();

    QString devicePath() const { return configuredDevicePath; }
    QVariantList networkRows() const { return cachedNetworkRows; }
    QVariantList knownNetworks() const { return cachedKnownNetworks; }
    QVariantMap snapshot() const;
    void executeOperation(const QString &id, const QString &action, const QVariantMap &arguments);
    void cancelOperation(const QString &id);
    void provideCredentials(const QString &id, const QString &password);

    QStringList adapters() const;
    QStringList devices() const;
    QStringList supportedModes() const;
    int signalStrengthLevel() const;
    bool connected() const;
    bool scanning() const;
    bool powered() const;
    QString state() const;
    QString model() const;
    QString name() const;
    QString vendor() const;
    QVariantMap connectedNetworkData() const;

signals:
    void snapshotChanged();
    void operationCompleted(const QString &id, const QString &action, bool success, const QString &errorName, const QString &message);
    void credentialsRequested(const QString &id, const QString &networkId);
    void error(IWDController::Error);

private:
    class IWDControllerPrivate;
    friend class AuthenticationAgentAdapter;
    friend class SignalLevelAgentAdapter;

    QString configuredDevicePath;
    bool cachedInitialized = false;
    QVariantMap cachedState;
    QVariantList cachedNetworkRows;
    QVariantList cachedKnownNetworks;
    QThread *workerThread = nullptr;
    IWDControllerPrivate *d = nullptr;
};
