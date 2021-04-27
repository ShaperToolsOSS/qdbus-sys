// Copyright (C) 2026 Shaper Tools
// SPDX-License-Identifier: LGPL-2.1-only

#pragma once

#include "qdbus-sys-global.h"
#include "qdbus-sys-version.h"

#include "iwd-model.h"

#include <QtQml/qqml.h>
#include <QObject>
#include <QStringList>
#include <QVariantMap>


namespace QDBusSys {

class QDBUS_SYS_EXPORT IWD : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool available READ available NOTIFY availableChanged)
    Q_PROPERTY(QString selectedDevice READ selectedDevice WRITE setSelectedDevice NOTIFY selectedDeviceChanged)
    Q_PROPERTY(int signalStrengthLevel READ signalStrengthLevel NOTIFY signalStrengthLevelChanged)
    Q_PROPERTY(IWDNetworkModel *networks READ networks CONSTANT)
    // IWD's saved networks, including those out of range: maps with networkId,
    // ssid, type, autoconnect, hidden and lastTimeConnected.
    Q_PROPERTY(QVariantList knownNetworks READ knownNetworks NOTIFY knownNetworksChanged)
    Q_PROPERTY(QStringList adapters READ adapters NOTIFY adaptersChanged)
    Q_PROPERTY(QStringList devices READ devices NOTIFY devicesChanged)
    Q_PROPERTY(QStringList supportedModes READ supportedModes NOTIFY supportedModesChanged)
    Q_PROPERTY(QString model READ model NOTIFY adaptersChanged)
    Q_PROPERTY(QString name READ name NOTIFY adaptersChanged)
    Q_PROPERTY(QString vendor READ vendor NOTIFY adaptersChanged)
    Q_PROPERTY(QStringList possibleStates READ possibleStates CONSTANT)
    Q_PROPERTY(bool connected READ connected NOTIFY connectedChanged)
    Q_PROPERTY(bool scanning READ scanning NOTIFY scanningChanged)
    Q_PROPERTY(bool powered READ powered NOTIFY poweredChanged)
    Q_PROPERTY(QString state READ state NOTIFY stateChanged)
    Q_PROPERTY(QVariantMap connectedNetworkData READ connectedNetworkData NOTIFY connectedChanged)

    QML_NAMED_ELEMENT(IWD)

public:
    enum Error {
        ScanFailed = Qt::UserRole,
        ConnectFailed,
        ForgetFailed,
        DisconnectFailed,
        RegisterAuthAgentFailed,
        SetAutoConnectFailed,
        OperationFailed
    };
    Q_ENUM(Error)

    IWD(QObject *parent = nullptr);
    ~IWD();

    Q_INVOKABLE QString scan();
    Q_INVOKABLE QString disconnectNetwork();
    Q_INVOKABLE QString connectNetwork(const QString &networkId);
    Q_INVOKABLE QString forgetNetwork(const QString &networkId);
    Q_INVOKABLE QString setAutoConnectNetwork(const QString &networkId, bool autoconnect);
    Q_INVOKABLE void preparePassword(const QString &networkId, const QString &password);

    Q_INVOKABLE void provideCredentials(const QString &operationId, const QString &password);
    Q_INVOKABLE void cancelOperation(const QString &operationId);
    bool available() const;
    QString selectedDevice() const;
    void setSelectedDevice(const QString &devicePath);

    IWDNetworkModel *networks() const;
    QVariantList knownNetworks() const;

    QStringList adapters() const;
    QStringList devices() const;
    QStringList supportedModes() const;
    QStringList possibleStates() const;

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
    void availableChanged();
    void selectedDeviceChanged();
    void snapshotChanged();
    void operationFinished(const QString &operationId, bool success, const QString &errorName, const QString &message);
    void credentialsRequested(const QString &operationId, const QString &networkId);
    void connectedChanged();
    void scanningChanged();
    void poweredChanged();
    void deviceStateChanged();
    void adaptersChanged();
    void devicesChanged();
    void knownNetworksChanged();
    void signalStrengthLevelChanged();
    void supportedModesChanged();
    void stateChanged();
    void error(Error);

private slots:
    void applyAdapterSnapshot(const QStringList &adapters,
                              const QStringList &devices,
                              const QStringList &supportedModes,
                              const QString &model,
                              const QString &name,
                              const QString &vendor,
                              bool powered);
    void applyDeviceSnapshot(const QString &state,
                             bool scanning,
                             bool connected,
                             const QVariantMap &connectedNetworkData,
                             int signalStrengthLevel);

private:
    class Q_DECL_HIDDEN IWDPrivate;
    IWDPrivate *d = nullptr;


};

} // namespace QDBusSys
