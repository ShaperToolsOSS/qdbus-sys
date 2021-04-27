// Copyright (C) 2026 Shaper Tools
// SPDX-License-Identifier: LGPL-2.1-only

#pragma once

#include "dbus-interface-common.h"
#include <QtDBus>
#include <QUuid>
#include <QTimer>
#include <functional>

namespace {
const QString service = QStringLiteral("net.connman.iwd");
const QString adapterPath = QStringLiteral("/net/connman/iwd/0");
const QString stationPath = adapterPath + QStringLiteral("/2");
const QString networkPath = stationPath + QStringLiteral("/74657374_psk");
const QString knownPath = QStringLiteral("/net/connman/iwd/74657374_psk");
}

class MockAdapter : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "net.connman.iwd.Adapter")
    Q_PROPERTY(QString Model MEMBER model)
    Q_PROPERTY(QString Name MEMBER name)
    Q_PROPERTY(QString Vendor MEMBER vendor)
    Q_PROPERTY(QStringList SupportedModes MEMBER modes)
    Q_PROPERTY(bool Powered MEMBER powered)
public:
    QString model = QStringLiteral("Test adapter"), name = QStringLiteral("test0"), vendor = QStringLiteral("Test");
    QStringList modes{QStringLiteral("station")};
    bool powered = true;
};

class MockStation : public QObject, protected QDBusContext
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "net.connman.iwd.Station")
    Q_PROPERTY(QString State READ state)
    Q_PROPERTY(bool Scanning READ scanning)
    Q_PROPERTY(QDBusObjectPath ConnectedNetwork READ connectedNetwork)
public:
    bool includeNetwork = true;
    bool connected = true;
    QDBusObjectPath currentNetwork{networkPath};
    int scans = 0;
    QString registeredSignalPath;
    int registrations = 0, duplicates = 0;
    QString state() const { return connected ? QStringLiteral("connected") : QStringLiteral("disconnected"); }
    bool scanning() const { return false; }
    QDBusObjectPath connectedNetwork() const { return connected ? currentNetwork : QDBusObjectPath(QStringLiteral("/")); }
public slots:
    void Scan()
    {
        ++scans;
        const auto request = message();
        const auto bus = connection();
        setDelayedReply(true);
        QTimer::singleShot(100, this, [request, bus] { bus.send(request.createReply()); });
    }
    void Disconnect() { connected = false; }
    OrderedNetworksList GetOrderedNetworks()
    {
        return includeNetwork ? OrderedNetworksList{{currentNetwork, -5000}} : OrderedNetworksList{};
    }
    void RegisterSignalLevelAgent(const QDBusObjectPath &path, const LevelsList &)
    {
        if (!registeredSignalPath.isEmpty()) {
            ++duplicates;
            sendErrorReply(QStringLiteral("net.connman.iwd.AlreadyExists"), QStringLiteral("Agent already registered"));
            return;
        }
        registeredSignalPath = path.path();
        ++registrations;
    }
    void UnregisterSignalLevelAgent(const QDBusObjectPath &path)
    {
        if (path.path() == registeredSignalPath) registeredSignalPath.clear();
    }
};

class MockNetwork : public QObject, protected QDBusContext
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "net.connman.iwd.Network")
    Q_PROPERTY(QString Name MEMBER name)
    Q_PROPERTY(QString Type MEMBER type)
    Q_PROPERTY(bool Connected MEMBER connected)
    Q_PROPERTY(QDBusObjectPath KnownNetwork MEMBER known)
public:
    QString name = QStringLiteral("Test network"), type = QStringLiteral("psk");
    bool connected = true;
    QDBusObjectPath known{knownPath};
    QString objectPath = networkPath;
    std::function<QString(const QString &)> findAgent;
    std::function<void()> didConnect;
    QStringList credentials;
public slots:
    void Connect()
    {
        const auto trigger = message();
        const auto bus = connection();
        const QString path = findAgent ? findAgent(trigger.service()) : QString();
        if (path.isEmpty()) {
            sendErrorReply(QStringLiteral("net.connman.iwd.NoAgent"), QStringLiteral("Missing credential agent"));
            return;
        }
        setDelayedReply(true);
        auto request = QDBusMessage::createMethodCall(trigger.service(), path, QStringLiteral("net.connman.iwd.Agent"), QStringLiteral("RequestPassphrase"));
        request << QVariant::fromValue(QDBusObjectPath(objectPath));
        auto *call = new QDBusPendingCallWatcher(bus.asyncCall(request), this);
        connect(call, &QDBusPendingCallWatcher::finished, this, [this, trigger, bus](QDBusPendingCallWatcher *finished) {
            QDBusPendingReply<QString> reply = *finished;
            finished->deleteLater();
            if (reply.isError()) {
                bus.send(trigger.createErrorReply(reply.error().name(), reply.error().message()));
                return;
            }
            credentials << reply.value();
            connected = true;
            if (didConnect) didConnect();
            bus.send(trigger.createReply());
        });
    }
};

class MockKnownNetwork : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "net.connman.iwd.KnownNetwork")
    Q_PROPERTY(bool AutoConnect MEMBER autoconnect)
    Q_PROPERTY(bool Hidden MEMBER hidden)
    Q_PROPERTY(QString LastConnectedTime MEMBER lastConnected)
    Q_PROPERTY(QString Name MEMBER name)
    Q_PROPERTY(QString Type MEMBER type)
public:
    bool autoconnect = true, hidden = false;
    QString lastConnected = QStringLiteral("2026-10-29T00:00:00Z");
    QString name = QStringLiteral("Test network"), type = QStringLiteral("psk");
    int forgets = 0;
public slots:
    void Forget() { ++forgets; }
};

class MockAgentManager : public QObject, protected QDBusContext
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "net.connman.iwd.AgentManager")
public:
    QHash<QString, QString> paths;
public slots:
    void RegisterAgent(const QDBusObjectPath &path) { paths[message().service()] = path.path(); }
    void UnregisterAgent(const QDBusObjectPath &) { paths.remove(message().service()); }
};

class MockObjectManager : public QObject, protected QDBusContext
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.DBus.ObjectManager")
public:
    // Interfaces without a mock object of their own, such as Device.
    ManagedObjectList objects;
    // Like IWD, report the properties of every exported object.
    std::function<ManagedObjectList()> provider;
    int delayMs = 0, requests = 0, activeRequests = 0, maxActiveRequests = 0;
public slots:
    ManagedObjectList GetManagedObjects()
    {
        ++requests;
        maxActiveRequests = qMax(maxActiveRequests, ++activeRequests);
        const auto snapshot = provider ? provider() : objects;
        const auto request = message();
        const auto bus = connection();
        setDelayedReply(true);
        QTimer::singleShot(delayMs, this, [this, snapshot, request, bus] {
            --activeRequests;
            bus.send(request.createReply(QVariantList{QVariant::fromValue(snapshot)}));
        });
        return {};
    }
signals:
    void InterfacesAdded(const QDBusObjectPath &path, const InterfacePropertyMap &interfaces);
    void InterfacesRemoved(const QDBusObjectPath &path, const QStringList &interfaces);
};

class FakeIwd
{
public:
    QString connectionName = QUuid::createUuid().toString();
    QDBusConnection bus = QDBusConnection::connectToBus(QString::fromLocal8Bit(qgetenv("DBUS_SYSTEM_BUS_ADDRESS")), connectionName);
    MockObjectManager root;
    MockAdapter adapter;
    MockStation station;
    MockNetwork network;
    MockKnownNetwork known;
    MockAgentManager agents;
    QStringList exportedPaths;

    // The interface properties IWD would report for every live object.
    ManagedObjectList managedObjects() const
    {
        ManagedObjectList result = root.objects;
        for (const QString &path : exportedPaths) {
            QObject *object = bus.objectRegisteredAt(path);
            if (!object || object == &root || object == &agents) continue;
            const QMetaObject *meta = object->metaObject();
            const QString interface = QString::fromLatin1(meta->classInfo(meta->indexOfClassInfo("D-Bus Interface")).value());
            auto &properties = result[QDBusObjectPath(path)][interface];
            for (int i = QObject::staticMetaObject.propertyCount(); i < meta->propertyCount(); ++i) {
                const QMetaProperty property = meta->property(i);
                if (!properties.contains(QString::fromLatin1(property.name()))) {
                    properties.insert(QString::fromLatin1(property.name()), property.read(object));
                }
            }
        }
        return result;
    }

    bool exportObject(const QString &path, QObject *object,
                      QDBusConnection::RegisterOptions flags = QDBusConnection::ExportAllSlots | QDBusConnection::ExportAllProperties)
    {
        if (!exportedPaths.contains(path)) exportedPaths << path;
        return bus.registerObject(path, object, flags);
    }

    void interfacesAdded(const QString &path, const QStringList &interfaces)
    {
        const auto all = managedObjects().value(QDBusObjectPath(path));
        InterfacePropertyMap added;
        for (const QString &interface : interfaces) added.insert(interface, all.value(interface));
        emit root.InterfacesAdded(QDBusObjectPath(path), added);
    }

    void propertiesChanged(const QString &path, const QString &interface, const QVariantMap &changed)
    {
        auto signal = QDBusMessage::createSignal(path, QStringLiteral("org.freedesktop.DBus.Properties"), QStringLiteral("PropertiesChanged"));
        signal << interface << changed << QStringList();
        bus.send(signal);
    }

    ~FakeIwd()
    {
        bus.unregisterService(service);
        QDBusConnection::disconnectFromBus(connectionName);
    }

    bool start()
    {
        network.findAgent = [this](const QString &owner) { return agents.paths.value(owner); };
        network.didConnect = [this] {
            station.connected = true;
            propertiesChanged(networkPath, QStringLiteral("net.connman.iwd.Network"), {{"Connected", true}});
            propertiesChanged(stationPath, QStringLiteral("net.connman.iwd.Station"),
                              {{"State", station.state()}, {"ConnectedNetwork", QVariant::fromValue(station.connectedNetwork())}});
        };
        root.provider = [this] { return managedObjects(); };
        root.objects[QDBusObjectPath(stationPath)] = {
            {QStringLiteral("net.connman.iwd.Device"), {{QStringLiteral("Adapter"), QVariant::fromValue(QDBusObjectPath(adapterPath))}}},
        };
        const auto flags = QDBusConnection::ExportAllSlots | QDBusConnection::ExportAllProperties | QDBusConnection::ExportAllSignals;
        return exportObject(QStringLiteral("/"), &root, flags)
            && exportObject(adapterPath, &adapter, flags)
            && exportObject(stationPath, &station, flags)
            && exportObject(networkPath, &network, flags)
            && exportObject(knownPath, &known, flags)
            && exportObject(QStringLiteral("/net/connman/iwd"), &agents, flags)
            && bus.registerService(service);
    }

    void removeStation()
    {
        adapter.powered = false;
        bus.unregisterObject(stationPath);
        station.registeredSignalPath.clear();
        emit root.InterfacesRemoved(QDBusObjectPath(stationPath), {QStringLiteral("net.connman.iwd.Station")});
    }

    bool restoreStation()
    {
        adapter.powered = true;
        if (!exportObject(stationPath, &station)) return false;
        interfacesAdded(stationPath, {QStringLiteral("net.connman.iwd.Station")});
        return true;
    }
};

