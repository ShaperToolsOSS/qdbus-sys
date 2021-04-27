// Copyright (C) 2026 Shaper Tools
// SPDX-License-Identifier: LGPL-2.1-only

#include "iwd-controller.h"
#include "agents.h"
#include "dbus-interface-common.h"
#include "iwd-ipc_p.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusServiceWatcher>
#include <QDBusVariant>
#include <QDebug>
#include <QPointer>
#include <QScopeGuard>
#include <QThread>
#include <QTimer>
#include <QUuid>

#include <algorithm>
#include <cmath>
#include <utility>

// Enable backend diagnostics with QT_LOGGING_RULES="qdbus.iwd.*.debug=true".

namespace {
const QString iwdService = QStringLiteral("net.connman.iwd");
const QString agentManagerPath = QStringLiteral("/net/connman/iwd");
const QString adapterInterface = QStringLiteral("net.connman.iwd.Adapter");
const QString deviceInterface = QStringLiteral("net.connman.iwd.Device");
const QString stationInterface = QStringLiteral("net.connman.iwd.Station");
const QString networkInterface = QStringLiteral("net.connman.iwd.Network");
const QString knownNetworkInterface = QStringLiteral("net.connman.iwd.KnownNetwork");
const QString agentManagerInterface = QStringLiteral("net.connman.iwd.AgentManager");
const QString objectManagerInterface = QStringLiteral("org.freedesktop.DBus.ObjectManager");
const QString propertiesInterface = QStringLiteral("org.freedesktop.DBus.Properties");

const QString busyError = QStringLiteral("com.shapertools.qdbussys.iwd.Busy");
const QString notAvailableError = QStringLiteral("com.shapertools.qdbussys.iwd.NotAvailable");
const QString notFoundError = QStringLiteral("com.shapertools.qdbussys.iwd.NotFound");
const QString canceledError = QStringLiteral("com.shapertools.qdbussys.iwd.Canceled");
const QString invalidArgumentsError = QStringLiteral("com.shapertools.qdbussys.iwd.InvalidArguments");

// Network and KnownNetwork objects share their final path component, e.g.
// .../0/4/74657374_psk and /net/connman/iwd/74657374_psk.
QString lastPathSegment(const QString &path)
{
    if (path.isEmpty() || path == QLatin1String("/")) {
        return {};
    }
    const int lastSlashIndex = path.lastIndexOf(QLatin1Char('/'));
    return lastSlashIndex < 0 ? path : path.mid(lastSlashIndex + 1);
}

QVariant unwrap(const QVariant &value)
{
    return value.metaType() == QMetaType::fromType<QDBusVariant>() ? qvariant_cast<QDBusVariant>(value).variant() : value;
}

QString objectPathValue(const QVariant &value)
{
    const QVariant plain = unwrap(value);
    if (plain.metaType() == QMetaType::fromType<QDBusObjectPath>()) {
        const QString path = qvariant_cast<QDBusObjectPath>(plain).path();
        return path == QLatin1String("/") ? QString() : path;
    }
    return plain.toString();
}

QStringList stringListValue(const QVariant &value)
{
    const QVariant plain = unwrap(value);
    if (plain.metaType() == QMetaType::fromType<QDBusArgument>()) {
        return qdbus_cast<QStringList>(qvariant_cast<QDBusArgument>(plain));
    }
    return plain.toStringList();
}

QVariantMap plainProperties(QVariantMap properties)
{
    for (auto it = properties.begin(); it != properties.end(); ++it) {
        it.value() = unwrap(it.value());
    }
    return properties;
}

int normalizeSignalStrength(int signalInCentiDBM)
{
    /* https://www.metageek.com/training/resources/wifi-signal-strength-basics.html
        -30dB <-> -100dB (x100 in communicated units)
        (10000 + signalInCentiDBM)/70
    */
    return qBound(0, (10000 + signalInCentiDBM) / 70, 100);
}
}

class AuthenticationAgentAdapter;
class SignalLevelAgentAdapter;

class IWDController::IWDControllerPrivate : public QObject {
    Q_OBJECT

signals:
    void error(IWDController::Error);
    void initializationChanged(bool ready);
    void operationCompleted(const QString &id, const QString &action, bool success, const QString &errorName, const QString &message);
    void credentialsRequested(const QString &id, const QString &networkId);
    void stateChanged(const QVariantMap &state);
    void networksChanged(const QVariantList &rows, const QVariantList &knownNetworks);

public:
    explicit IWDControllerPrivate(const QString &requestedDevicePath);
    ~IWDControllerPrivate() override;

    Q_INVOKABLE void setup();
    Q_INVOKABLE void shutdown();
    Q_INVOKABLE void executeOperation(const QString &id, const QString &action, const QVariantMap &arguments);
    Q_INVOKABLE void cancelOperation(const QString &id);
    Q_INVOKABLE void provideCredentials(const QString &id, const QString &password);

    void finishOperation(QString id, const QString &action, bool success, const QString &name = {}, const QString &message = {});
    void updateSignalLevel(uint8_t level);
    void releaseSignalLevelAgent(SignalLevelAgentAdapter *agent);
    bool isIwd(const QDBusMessage &message) const { return !serviceOwner.isEmpty() && message.service() == serviceOwner; }

public slots:
    void onServiceOwnerChanged(const QString &service, const QString &oldOwner, const QString &newOwner);
    void onInterfacesAdded(const QDBusObjectPath &objectPath, const InterfacePropertyMap &interfaces, const QDBusMessage &message);
    void onInterfacesRemoved(const QDBusObjectPath &objectPath, const QStringList &interfaces, const QDBusMessage &message);
    void onPropertiesChanged(const QString &interface, const QVariantMap &changed, const QStringList &invalidated, const QDBusMessage &message);

private:
    QDBusPendingCallWatcher *callIwd(const QString &path, const QString &interface, const QString &method,
                                     const QVariantList &arguments = {});
    QVariant cached(const QString &path, const QString &interface, const QString &name) const;
    bool hasInterface(const QString &path, const QString &interface) const;
    void refreshObject(const QString &path, const QString &interface);

    void scheduleDiscovery();
    void discover();
    void onGetManagedObjects(QDBusPendingCallWatcher *call);
    void applyTopology();
    void setupDevice(const QString &adapterPath, const QString &devicePath);
    void resetDevice();

    void cleanupAgents(bool unregisterWithIwd);
    void trackAgentUnregistration(QDBusPendingCallWatcher *call);
    bool registerAuthenticationAgent();
    void registerSignalLevelAgent();
    void preparePassword(const QString &networkId, const QString &password);

    void getOrderedNetworks();
    void onGetOrderedNetworksFinished(QDBusPendingCallWatcher *call);
    void watchOperation(QDBusPendingCallWatcher *call, const QString &action, IWDController::Error failure);
    void scan();
    void disconnectNetwork();
    void connectNetwork(const QString &networkId);
    void forgetNetwork(const QString &networkId);
    void setAutoConnectNetwork(const QString &networkId, bool enabled);
    QString knownNetworkPathForId(const QString &networkId) const;

    QVariantMap networkRow(const QString &path, int signalInCentiDBM) const;
    QVariantList knownNetworkRows() const;
    void publishState();
    void scheduleNetworkSnapshot();
    void publishNetworkSnapshot();

public:
    QDBusConnection bus;
    QString requestedDevicePath;
    QString serviceOwner;
    QString activeOperationId;

    // IWD's object tree: path -> interface -> properties.
    QHash<QString, InterfacePropertyMap> objects;
    QStringList adapters;
    QStringList devices;
    QHash<QString, QString> deviceAdapterPaths;
    QString selectedAdapterPath;
    QString selectedDevicePath;
    bool stationReady = false;

    QStringList possibleStates{"connected", "disconnected", "connecting", "disconnecting", "roaming"};
    LevelsList signalStrengthLevelThresholds{-30, -40, -50, -60, -70, -80, -90};

    OrderedNetworksList orderedNetworkList;
    // Networks in IWD's ranked order, as (path, RSSI in 100 * dBm).
    QList<QPair<QString, int>> rankedNetworks;
    QPointer<QDBusPendingCallWatcher> orderedNetworksCall;
    bool orderedNetworksPending = false;

    QObject *authenticationAgentObject = nullptr;
    AuthenticationAgentAdapter *authenticationAgent = nullptr;
    bool authenticationAgentRegistered = false;
    QString deferredConnectNetwork;
    QObject *signalLevelAgentObject = nullptr;
    SignalLevelAgentAdapter *signalLevelAgent = nullptr;
    int pendingAgentUnregistrations = 0;
    // Latest level from our own agent; -1 until it reports for this connection.
    int agentSignalLevel = -1;

    bool shuttingDown = false;
    bool discoveryInFlight = false;
    bool discoveryPending = false;
    quint64 serviceGeneration = 0;
    quint64 deviceGeneration = 0;
    quint64 ownerEvents = 0;
    QDBusServiceWatcher *serviceWatcher = nullptr;
    QTimer *discoveryTimer = nullptr;
    QTimer *networkSnapshotTimer = nullptr;

#ifdef LOAD_TESTING
#warning load testing is enabled so your unit will scan ad infinitum
    bool loadTesting = true;
#else
    bool loadTesting = false;
#endif
};

class SignalLevelAgentAdapter : public AbstractSignalLevelAgentAdapter {
public:
    SignalLevelAgentAdapter(IWDController::IWDControllerPrivate *owner, QObject *parent)
        : AbstractSignalLevelAgentAdapter(parent), owner(owner) {}

private:
    bool isTrustedCaller(const QDBusMessage &message) const override { return owner->isIwd(message); }
    void onRelease(const QString &) override { owner->releaseSignalLevelAgent(this); }
    void onChanged(const QString &, uint8_t level) override { owner->updateSignalLevel(level); }

    IWDController::IWDControllerPrivate *owner;
};

class AuthenticationAgentAdapter : public AbstractAuthenticationAgentAdapter {
public:
    AuthenticationAgentAdapter(IWDController::IWDControllerPrivate *owner, QObject *parent);
    void preparePassword(const QString &networkId, const QString &password);
    void clear();
    void provide(const QString &password);

private:
    bool isTrustedCaller(const QDBusMessage &message) const override { return owner->isIwd(message); }
    QString onRequestPassphrase(const QString &networkPath, const QDBusMessage &message) override;
    void onCancel(const QString &) override { clear(); }
    void onRelease() override { clear(); }

    IWDController::IWDControllerPrivate *owner;
    QString password, passwordNetworkId;
    QDBusMessage pending;
    QTimer *deadline;
};

AuthenticationAgentAdapter::AuthenticationAgentAdapter(IWDController::IWDControllerPrivate *owner, QObject *parent)
    : AbstractAuthenticationAgentAdapter(parent, owner->bus), owner(owner), deadline(new QTimer(this))
{
    deadline->setSingleShot(true);
    deadline->setInterval(60000);
    connect(deadline, &QTimer::timeout, this, [this] { clear(); });
}

void AuthenticationAgentAdapter::preparePassword(const QString &networkId, const QString &value)
{
    passwordNetworkId = networkId;
    password = value;
}

void AuthenticationAgentAdapter::clear()
{
    deadline->stop();
    password.clear();
    passwordNetworkId.clear();
    if (pending.type() != QDBusMessage::InvalidMessage) {
        replyCanceled(pending, QStringLiteral("Credential request canceled"));
        pending = QDBusMessage();
    }
}

void AuthenticationAgentAdapter::provide(const QString &value)
{
    if (pending.type() == QDBusMessage::InvalidMessage) return;
    deadline->stop();
    bus.send(pending.createReply(QVariantList{value}));
    pending = QDBusMessage();
}

QString AuthenticationAgentAdapter::onRequestPassphrase(const QString &networkPath, const QDBusMessage &message)
{
    const QString networkId = lastPathSegment(networkPath);
    if (networkId == passwordNetworkId && !password.isNull()) return password;
    if (owner->activeOperationId.isEmpty()) {
        replyCanceled(message, QStringLiteral("No requesting client"));
        return {};
    }
    clear();
    message.setDelayedReply(true);
    pending = message;
    deadline->start();
    emit owner->credentialsRequested(owner->activeOperationId, networkId);
    return {};
}

IWDController::IWDControllerPrivate::IWDControllerPrivate(const QString &requestedDevicePath)
    : QObject(),
      bus(QDBusConnection::connectToBus(QDBusConnection::SystemBus, QStringLiteral("qiwd-device-") + QUuid::createUuid().toString())),
      requestedDevicePath(requestedDevicePath)
{
    Q_ASSERT(signalStrengthLevelThresholds.size() == IWDIPC::weakestSignalLevel);

    networkSnapshotTimer = new QTimer(this);
    networkSnapshotTimer->setSingleShot(true);
    // Let bursts of per-network changes settle into one published snapshot.
    networkSnapshotTimer->setInterval(50);
    connect(networkSnapshotTimer, &QTimer::timeout, this, &IWDControllerPrivate::publishNetworkSnapshot);

    discoveryTimer = new QTimer(this);
    discoveryTimer->setSingleShot(true);
    connect(discoveryTimer, &QTimer::timeout, this, &IWDControllerPrivate::discover);

    qDBusRegisterMetaType<ManagedObjectList>();
    qDBusRegisterMetaType<InterfacePropertyMap>();
    qDBusRegisterMetaType<NetworkEntry>();
    qDBusRegisterMetaType<OrderedNetworksList>();
    qDBusRegisterMetaType<HiddenNetworkEntry>();
    qDBusRegisterMetaType<HiddenNetworksList>();
    qDBusRegisterMetaType<LevelsList>();
}

IWDController::IWDControllerPrivate::~IWDControllerPrivate()
{
    shutdown();
    QDBusConnection::disconnectFromBus(bus.name());
}

QDBusPendingCallWatcher *IWDController::IWDControllerPrivate::callIwd(const QString &path, const QString &interface,
                                                                     const QString &method, const QVariantList &arguments)
{
    auto message = QDBusMessage::createMethodCall(iwdService, path, interface, method);
    message.setArguments(arguments);
    message.setAutoStartService(false);
    return new QDBusPendingCallWatcher(bus.asyncCall(message), this);
}

QVariant IWDController::IWDControllerPrivate::cached(const QString &path, const QString &interface, const QString &name) const
{
    return objects.value(path).value(interface).value(name);
}

bool IWDController::IWDControllerPrivate::hasInterface(const QString &path, const QString &interface) const
{
    const auto it = objects.constFind(path);
    return it != objects.cend() && it->contains(interface);
}

void IWDController::IWDControllerPrivate::refreshObject(const QString &path, const QString &interface)
{
    // Used only for invalidated properties and to confirm our own writes.
    const auto generation = serviceGeneration;
    auto *call = callIwd(path, propertiesInterface, QStringLiteral("GetAll"), {interface});
    connect(call, &QDBusPendingCallWatcher::finished, this, [this, path, interface, generation](QDBusPendingCallWatcher *finished) {
        QDBusPendingReply<QVariantMap> reply = *finished;
        finished->deleteLater();
        if (shuttingDown || generation != serviceGeneration || reply.isError() || !hasInterface(path, interface)) {
            return;
        }
        auto &properties = objects[path][interface];
        const QVariantMap values = plainProperties(reply.value());
        for (auto it = values.cbegin(); it != values.cend(); ++it) properties.insert(it.key(), it.value());
        publishState();
        scheduleNetworkSnapshot();
    });
}

void IWDController::IWDControllerPrivate::setup()
{
    if (serviceWatcher || shuttingDown) {
        scheduleDiscovery();
        return;
    }
    serviceWatcher = new QDBusServiceWatcher(iwdService, bus, QDBusServiceWatcher::WatchForOwnerChange, this);
    connect(serviceWatcher, &QDBusServiceWatcher::serviceOwnerChanged, this, [this](const QString &service, const QString &oldOwner, const QString &newOwner) {
        ++ownerEvents;
        onServiceOwnerChanged(service, oldOwner, newOwner);
    });
    bus.connect(iwdService, QStringLiteral("/"), objectManagerInterface, QStringLiteral("InterfacesAdded"),
                this, SLOT(onInterfacesAdded(QDBusObjectPath,InterfacePropertyMap,QDBusMessage)));
    bus.connect(iwdService, QStringLiteral("/"), objectManagerInterface, QStringLiteral("InterfacesRemoved"),
                this, SLOT(onInterfacesRemoved(QDBusObjectPath,QStringList,QDBusMessage)));
    // One match rule covers every IWD object instead of one per object.
    bus.connect(iwdService, QString(), propertiesInterface, QStringLiteral("PropertiesChanged"),
                this, SLOT(onPropertiesChanged(QString,QVariantMap,QStringList,QDBusMessage)));

    const auto events = ownerEvents;
    auto message = QDBusMessage::createMethodCall(QStringLiteral("org.freedesktop.DBus"), QStringLiteral("/org/freedesktop/DBus"),
                                                  QStringLiteral("org.freedesktop.DBus"), QStringLiteral("GetNameOwner"));
    message << iwdService;
    auto *call = new QDBusPendingCallWatcher(bus.asyncCall(message), this);
    connect(call, &QDBusPendingCallWatcher::finished, this, [this, events](QDBusPendingCallWatcher *finished) {
        QDBusPendingReply<QString> reply = *finished;
        finished->deleteLater();
        // The watcher already reported a newer owner.
        if (events != ownerEvents || reply.isError()) return;
        onServiceOwnerChanged(iwdService, QString(), reply.value());
    });
}

void IWDController::IWDControllerPrivate::onServiceOwnerChanged(const QString &, const QString &, const QString &newOwner)
{
    if (shuttingDown || serviceOwner == newOwner) {
        return;
    }
    ++serviceGeneration;
    emit initializationChanged(false);
    serviceOwner = newOwner;
    discoveryTimer->stop();
    discoveryInFlight = false;
    discoveryPending = false;
    // The old owner has gone. Never send its cleanup calls to the replacement.
    cleanupAgents(false);
    resetDevice();
    objects.clear();
    adapters.clear();
    devices.clear();
    deviceAdapterPaths.clear();
    publishState();
    publishNetworkSnapshot();
    if (!newOwner.isEmpty()) {
        scheduleDiscovery();
    }
}

void IWDController::IWDControllerPrivate::scheduleDiscovery()
{
    if (shuttingDown || serviceOwner.isEmpty()) {
        return;
    }
    discoveryPending = true;
    if (!discoveryInFlight) {
        discoveryTimer->start(0);
    }
}

void IWDController::IWDControllerPrivate::discover()
{
    if (shuttingDown || serviceOwner.isEmpty() || discoveryInFlight) {
        return;
    }
    discoveryPending = false;
    discoveryInFlight = true;
    auto *call = callIwd(QStringLiteral("/"), objectManagerInterface, QStringLiteral("GetManagedObjects"));
    call->setProperty("serviceGeneration", QVariant::fromValue(serviceGeneration));
    connect(call, &QDBusPendingCallWatcher::finished, this, &IWDControllerPrivate::onGetManagedObjects);
}

void IWDController::IWDControllerPrivate::onGetManagedObjects(QDBusPendingCallWatcher *call)
{
    call->deleteLater();
    if (shuttingDown || call->property("serviceGeneration").toULongLong() != serviceGeneration) {
        return;
    }
    auto discoveryFinished = qScopeGuard([this] {
        discoveryInFlight = false;
        if (discoveryPending) {
            scheduleDiscovery();
        }
    });
    // A topology event arrived after this snapshot was requested. Get a fresh
    // snapshot before selecting a device or registering agents.
    if (discoveryPending) {
        return;
    }

    QDBusPendingReply<ManagedObjectList> reply = *call;
    if (reply.isError()) {
        qDebug() << "GetManagedObjects call failed:" << reply.error();
        emit error(IWDController::GetManagedObjectsFailed);
        emit initializationChanged(true);
        publishState();
        publishNetworkSnapshot();
        return;
    }

    // Signals that arrived before this reply describe older state, so the
    // reply replaces the cache wholesale.
    objects.clear();
    const auto managedObjects = reply.value();
    for (auto it = managedObjects.cbegin(); it != managedObjects.cend(); ++it) {
        InterfacePropertyMap interfaces;
        for (auto interface = it.value().cbegin(); interface != it.value().cend(); ++interface) {
            interfaces.insert(interface.key(), plainProperties(interface.value()));
        }
        objects.insert(it.key().path(), interfaces);
    }
    applyTopology();
}

void IWDController::IWDControllerPrivate::applyTopology()
{
    adapters.clear();
    devices.clear();
    deviceAdapterPaths.clear();
    QStringList stationDevices;
    for (auto it = objects.cbegin(); it != objects.cend(); ++it) {
        if (it->contains(adapterInterface)) {
            adapters << it.key();
        }
        if (it->contains(deviceInterface)) {
            devices << it.key();
            const QString adapterPath = objectPathValue(it->value(deviceInterface).value(QStringLiteral("Adapter")));
            if (!adapterPath.isEmpty()) {
                deviceAdapterPaths.insert(it.key(), adapterPath);
            }
        }
        if (it->contains(stationInterface)) {
            stationDevices << it.key();
        }
    }
    adapters.sort();
    devices.sort();
    qCDebug(IWDBackendLog) << "adapters:" << adapters << "devices:" << devices;

    QString adapterPath;
    QString devicePath;
    // Hotplug on another device must not tear down a working selection.
    if (stationDevices.contains(selectedDevicePath) && devices.contains(selectedDevicePath)
        && adapters.contains(selectedAdapterPath)) {
        adapterPath = selectedAdapterPath;
        devicePath = selectedDevicePath;
    }
    for (const QString &candidateDevicePath : std::as_const(devices)) {
        if (!devicePath.isEmpty()) {
            break;
        }
        if ((!requestedDevicePath.isEmpty() && requestedDevicePath != candidateDevicePath)
            || !stationDevices.contains(candidateDevicePath)) {
            continue;
        }
        const QString candidateAdapterPath = deviceAdapterPaths.value(candidateDevicePath);
        if (!candidateAdapterPath.isEmpty() && adapters.contains(candidateAdapterPath)) {
            adapterPath = candidateAdapterPath;
            devicePath = candidateDevicePath;
        }
    }
    if (devicePath.isEmpty()) {
        for (const QString &candidateAdapterPath : std::as_const(adapters)) {
            for (const QString &candidateDevicePath : std::as_const(devices)) {
                if ((requestedDevicePath.isEmpty() || requestedDevicePath == candidateDevicePath)
                    && stationDevices.contains(candidateDevicePath)
                    && candidateDevicePath.startsWith(candidateAdapterPath + QLatin1Char('/'))) {
                    adapterPath = candidateAdapterPath;
                    devicePath = candidateDevicePath;
                    break;
                }
            }
            if (!devicePath.isEmpty()) {
                break;
            }
        }
    }

    if (devicePath == selectedDevicePath && adapterPath == selectedAdapterPath && stationReady) {
        publishState();
        registerAuthenticationAgent();
        registerSignalLevelAgent();
        getOrderedNetworks();
    } else if (!adapterPath.isEmpty() && !devicePath.isEmpty()) {
        resetDevice();
        setupDevice(adapterPath, devicePath);
    } else {
        resetDevice();
        publishState();
        publishNetworkSnapshot();
        emit initializationChanged(true);
    }
}

void IWDController::IWDControllerPrivate::setupDevice(const QString &adapterPath, const QString &devicePath)
{
    selectedAdapterPath = adapterPath;
    selectedDevicePath = devicePath;
    stationReady = true;
    publishState();

    registerAuthenticationAgent();
    registerSignalLevelAgent();
    getOrderedNetworks();

    if (loadTesting) {
        // back to back scanning
        QMetaObject::invokeMethod(this, &IWDControllerPrivate::scan, Qt::QueuedConnection);
    }
}

void IWDController::IWDControllerPrivate::resetDevice()
{
    ++deviceGeneration;
    orderedNetworksCall = nullptr;
    orderedNetworksPending = false;
    cleanupAgents(true);
    stationReady = false;
    orderedNetworkList.clear();
    rankedNetworks.clear();
    agentSignalLevel = -1;
    selectedAdapterPath.clear();
    selectedDevicePath.clear();
}

void IWDController::IWDControllerPrivate::shutdown()
{
    shuttingDown = true;
    ++serviceGeneration;
    ++deviceGeneration;
    discoveryTimer->stop();
    networkSnapshotTimer->stop();
    cleanupAgents(true);
    stationReady = false;
}

void IWDController::IWDControllerPrivate::onInterfacesAdded(const QDBusObjectPath &objectPath, const InterfacePropertyMap &interfaces,
                                                            const QDBusMessage &message)
{
    if (shuttingDown || !isIwd(message)) {
        return;
    }
    auto &object = objects[objectPath.path()];
    for (auto it = interfaces.cbegin(); it != interfaces.cend(); ++it) {
        object.insert(it.key(), plainProperties(it.value()));
    }
    const QStringList names = interfaces.keys();
    if (names.contains(adapterInterface) || names.contains(deviceInterface) || names.contains(stationInterface)) {
        scheduleDiscovery();
    } else if (names.contains(networkInterface) || names.contains(knownNetworkInterface)) {
        getOrderedNetworks();
        scheduleNetworkSnapshot();
    }
}

void IWDController::IWDControllerPrivate::onInterfacesRemoved(const QDBusObjectPath &objectPath, const QStringList &interfaces,
                                                              const QDBusMessage &message)
{
    if (shuttingDown || !isIwd(message)) {
        return;
    }
    const QString path = objectPath.path();
    auto it = objects.find(path);
    if (it != objects.end()) {
        for (const QString &interface : interfaces) it->remove(interface);
        if (it->isEmpty()) objects.erase(it);
    }
    if (interfaces.contains(adapterInterface) || interfaces.contains(deviceInterface) || interfaces.contains(stationInterface)) {
        // Invalidate device replies immediately when its station disappears.
        if (path == selectedDevicePath || path == selectedAdapterPath) {
            resetDevice();
            publishState();
            publishNetworkSnapshot();
        }
        scheduleDiscovery();
    } else if (interfaces.contains(networkInterface) || interfaces.contains(knownNetworkInterface)) {
        getOrderedNetworks();
        scheduleNetworkSnapshot();
    }
}

void IWDController::IWDControllerPrivate::onPropertiesChanged(const QString &interface, const QVariantMap &changed,
                                                              const QStringList &invalidated, const QDBusMessage &message)
{
    if (shuttingDown || !isIwd(message)) {
        return;
    }
    const QString path = message.path();
    qCDebug(IWDBackendPropertiesLog) << "Property change:" << path << interface << changed << invalidated;

    auto &properties = objects[path][interface];
    const QVariantMap values = plainProperties(changed);
    for (auto it = values.cbegin(); it != values.cend(); ++it) properties.insert(it.key(), it.value());
    for (const QString &name : invalidated) properties.remove(name);
    if (!invalidated.isEmpty()) {
        refreshObject(path, interface);
    }

    if (interface == stationInterface && path == selectedDevicePath) {
        if (values.contains(QStringLiteral("ConnectedNetwork"))) {
            // A new connection needs a new measurement.
            agentSignalLevel = -1;
            scheduleNetworkSnapshot();
        }
        if (values.contains(QStringLiteral("State"))) {
            const QString state = values.value(QStringLiteral("State")).toString();
            if (!possibleStates.contains(state)) qDebug() << "Critical: unknown iwd state encountered:" << state;
        }
        publishState();
        if (values.contains(QStringLiteral("Scanning")) && !values.value(QStringLiteral("Scanning")).toBool()) {
            getOrderedNetworks();
            if (loadTesting) {
                scan();
            }
        }
    } else if (interface == adapterInterface && adapters.contains(path)) {
        publishState();
        if (values.contains(QStringLiteral("Powered"))) {
            // A powered adapter gains a station; an unpowered one loses it.
            scheduleDiscovery();
        }
    } else if (interface == networkInterface || interface == knownNetworkInterface) {
        scheduleNetworkSnapshot();
    }
}

void IWDController::IWDControllerPrivate::cleanupAgents(bool unregisterWithIwd)
{
    authenticationAgentRegistered = false;
    deferredConnectNetwork.clear();
    if (signalLevelAgent) {
        const QDBusObjectPath path = signalLevelAgent->objectPath();
        if (unregisterWithIwd && stationReady && !serviceOwner.isEmpty()) {
            trackAgentUnregistration(callIwd(selectedDevicePath, stationInterface, QStringLiteral("UnregisterSignalLevelAgent"),
                                             {QVariant::fromValue(path)}));
        }
        bus.unregisterObject(path.path());
        delete signalLevelAgentObject;
        signalLevelAgentObject = nullptr;
        signalLevelAgent = nullptr;
    }

    if (authenticationAgent) {
        const QDBusObjectPath path = authenticationAgent->objectPath();
        if (unregisterWithIwd && !serviceOwner.isEmpty()) {
            trackAgentUnregistration(callIwd(agentManagerPath, agentManagerInterface, QStringLiteral("UnregisterAgent"),
                                             {QVariant::fromValue(path)}));
        }
        bus.unregisterObject(path.path());
        delete authenticationAgentObject;
        authenticationAgentObject = nullptr;
        authenticationAgent = nullptr;
    }
}

void IWDController::IWDControllerPrivate::trackAgentUnregistration(QDBusPendingCallWatcher *call)
{
    ++pendingAgentUnregistrations;
    connect(call, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *finished) {
        finished->deleteLater();
        --pendingAgentUnregistrations;
        if (!shuttingDown && !serviceOwner.isEmpty() && pendingAgentUnregistrations == 0) {
            registerAuthenticationAgent();
            registerSignalLevelAgent();
        }
    });
}

void IWDController::IWDControllerPrivate::updateSignalLevel(uint8_t level)
{
    agentSignalLevel = level;
    publishState();
}

void IWDController::IWDControllerPrivate::releaseSignalLevelAgent(SignalLevelAgentAdapter *agent)
{
    if (signalLevelAgent != agent) {
        return;
    }
    bus.unregisterObject(agent->objectPath().path());
    signalLevelAgentObject->deleteLater();
    signalLevelAgentObject = nullptr;
    signalLevelAgent = nullptr;
    agentSignalLevel = -1;
    publishState();
    scheduleDiscovery();
}

void IWDController::IWDControllerPrivate::registerSignalLevelAgent()
{
    if (shuttingDown || !stationReady || pendingAgentUnregistrations > 0
        || !cached(selectedAdapterPath, adapterInterface, QStringLiteral("Powered")).toBool()) {
        return;
    }
    if (signalLevelAgent) {
        return;
    }

    signalLevelAgentObject = new QObject(this);
    signalLevelAgent = new SignalLevelAgentAdapter(this, signalLevelAgentObject);

    const QDBusObjectPath path = signalLevelAgent->objectPath();
    if (!bus.registerObject(path.path(), signalLevelAgentObject)) {
        qDebug() << "Failed to register signal level agent object";
        delete signalLevelAgentObject;
        signalLevelAgentObject = nullptr;
        signalLevelAgent = nullptr;
        emit error(IWDController::RegisterSignalLevelAgentFailed);
        return;
    }

    auto *call = callIwd(selectedDevicePath, stationInterface, QStringLiteral("RegisterSignalLevelAgent"),
                         {QVariant::fromValue(path), QVariant::fromValue(signalStrengthLevelThresholds)});
    QPointer<QObject> registeredObject = signalLevelAgentObject;
    QPointer<SignalLevelAgentAdapter> registeredAgent = signalLevelAgent;
    connect(call, &QDBusPendingCallWatcher::finished, this, [this, path, registeredObject, registeredAgent](QDBusPendingCallWatcher *finished) {
        QDBusPendingReply<> reply = *finished;
        finished->deleteLater();
        if (!reply.isError()) {
            return;
        }
        if (!registeredAgent || signalLevelAgent != registeredAgent.data()) {
            qCDebug(IWDBackendLog) << "Ignoring stale signal level agent registration failure:" << reply.error();
            return;
        }
        qDebug() << "Registering signal level agent failed with:" << reply.error();
        bus.unregisterObject(path.path());
        delete registeredObject.data();
        signalLevelAgentObject = nullptr;
        signalLevelAgent = nullptr;
        const QString name = reply.error().name();
        if (name == QLatin1String("net.connman.iwd.AlreadyExists")) {
            // IWD permits only one signal agent per station, across all
            // clients. Keep using scanned RSSI while another owns it.
            publishState();
        } else if (name == QLatin1String("org.freedesktop.DBus.Error.NotFound")
                   || name == QLatin1String("org.freedesktop.DBus.Error.UnknownObject")
                   || name == QLatin1String("org.freedesktop.DBus.Error.UnknownMethod")) {
            scheduleDiscovery();
        } else {
            emit error(IWDController::RegisterSignalLevelAgentFailed);
        }
    });
}

bool IWDController::IWDControllerPrivate::registerAuthenticationAgent()
{
    if (shuttingDown || pendingAgentUnregistrations > 0 || serviceOwner.isEmpty()) {
        return false;
    }
    if (authenticationAgent) {
        return true;
    }

    authenticationAgentObject = new QObject(this);
    authenticationAgent = new AuthenticationAgentAdapter(this, authenticationAgentObject);

    const QDBusObjectPath path = authenticationAgent->objectPath();
    if (!bus.registerObject(path.path(), authenticationAgentObject)) {
        qDebug() << "Failed to register auth agent object";
        delete authenticationAgentObject;
        authenticationAgentObject = nullptr;
        authenticationAgent = nullptr;
        emit error(IWDController::RegisterAuthAgentFailed);
        return false;
    }

    auto *call = callIwd(agentManagerPath, agentManagerInterface, QStringLiteral("RegisterAgent"), {QVariant::fromValue(path)});
    QPointer<QObject> registeredObject = authenticationAgentObject;
    QPointer<AuthenticationAgentAdapter> registeredAgent = authenticationAgent;
    connect(call, &QDBusPendingCallWatcher::finished, this, [this, path, registeredObject, registeredAgent](QDBusPendingCallWatcher *finished) {
        QDBusPendingReply<> reply = *finished;
        finished->deleteLater();
        const bool current = registeredAgent && authenticationAgent == registeredAgent.data();
        if (!current) {
            if (reply.isError()) qCDebug(IWDBackendLog) << "Ignoring stale auth agent registration failure:" << reply.error();
            return;
        }
        if (!reply.isError()) {
            authenticationAgentRegistered = true;
            if (!deferredConnectNetwork.isEmpty()) {
                connectNetwork(std::exchange(deferredConnectNetwork, QString()));
            }
            return;
        }
        qDebug() << "Registering auth agent failed with:" << reply.error();
        bus.unregisterObject(path.path());
        delete registeredObject.data();
        authenticationAgentObject = nullptr;
        authenticationAgent = nullptr;
        emit error(IWDController::RegisterAuthAgentFailed);
        if (!deferredConnectNetwork.isEmpty()) {
            deferredConnectNetwork.clear();
            finishOperation(activeOperationId, QStringLiteral("connect"), false,
                            QString::fromLatin1(IWDIPC::agentUnavailableError),
                            QStringLiteral("Could not register credential agent: ") + reply.error().message());
        }
    });

    return true;
}

void IWDController::IWDControllerPrivate::preparePassword(const QString &networkId, const QString &password)
{
    if (!rankedNetworks.isEmpty() && std::none_of(rankedNetworks.cbegin(), rankedNetworks.cend(),
            [&](const auto &entry) { return lastPathSegment(entry.first) == networkId; })) {
        qDebug() << "Ignoring password for unknown network id" << networkId;
        return;
    }
    if (!authenticationAgent && !registerAuthenticationAgent()) {
        return;
    }
    authenticationAgent->preparePassword(networkId, password);
}

void IWDController::IWDControllerPrivate::finishOperation(QString id, const QString &action, bool success,
                                                          const QString &name, const QString &message)
{
    if (activeOperationId == id) {
        deferredConnectNetwork.clear();
        activeOperationId.clear();
        if (authenticationAgent) authenticationAgent->clear();
    }
    emit operationCompleted(id, action, success, name, message);
}

void IWDController::IWDControllerPrivate::executeOperation(const QString &id, const QString &action, const QVariantMap &arguments)
{
    if (!activeOperationId.isEmpty()) {
        emit operationCompleted(id, action, false, busyError, QStringLiteral("Device has an active operation"));
        return;
    }
    activeOperationId = id;
    if (!stationReady || shuttingDown) {
        finishOperation(id, action, false, notAvailableError, QStringLiteral("Device is not available"));
        return;
    }
    const QString networkId = arguments.value(QStringLiteral("networkId")).toString();
    if (action == QLatin1String("scan")) scan();
    else if (action == QLatin1String("disconnect")) disconnectNetwork();
    else if (action == QLatin1String("connect")) {
        if (!authenticationAgent && !registerAuthenticationAgent()) {
            finishOperation(id, action, false, QString::fromLatin1(IWDIPC::agentUnavailableError),
                            QStringLiteral("Credential agent is unavailable"));
            return;
        }
        if (authenticationAgent) authenticationAgent->clear();
        if (arguments.contains(QStringLiteral("passphrase"))) preparePassword(networkId, arguments.value(QStringLiteral("passphrase")).toString());
        if (authenticationAgentRegistered || networkId.isEmpty()) connectNetwork(networkId);
        else deferredConnectNetwork = networkId;
    } else if (action == QLatin1String("forget")) forgetNetwork(networkId);
    else if (action == QLatin1String("autoconnect")) setAutoConnectNetwork(networkId, arguments.value(QStringLiteral("enabled")).toBool());
    else finishOperation(id, action, false, invalidArgumentsError, QStringLiteral("Unknown operation"));
}

void IWDController::IWDControllerPrivate::cancelOperation(const QString &id)
{
    if (id != activeOperationId) return;
    if (!deferredConnectNetwork.isEmpty()) {
        finishOperation(id, QStringLiteral("connect"), false, canceledError, QStringLiteral("Operation canceled"));
        return;
    }
    if (authenticationAgent) authenticationAgent->clear();
    if (stationReady) {
        // IWD aborts the pending Connect, whose reply then completes the operation.
        callIwd(selectedDevicePath, stationInterface, QStringLiteral("Disconnect"))->deleteLater();
    }
}

void IWDController::IWDControllerPrivate::provideCredentials(const QString &id, const QString &password)
{
    if (id == activeOperationId && authenticationAgent) authenticationAgent->provide(password);
}

void IWDController::IWDControllerPrivate::watchOperation(QDBusPendingCallWatcher *call, const QString &action,
                                                         IWDController::Error failure)
{
    const QString id = activeOperationId;
    connect(call, &QDBusPendingCallWatcher::finished, this, [this, id, action, failure](QDBusPendingCallWatcher *finished) {
        finished->deleteLater();
        const QDBusMessage reply = finished->reply();
        const bool success = reply.type() != QDBusMessage::ErrorMessage;
        if (!success) {
            qDebug() << action << "failed:" << reply.errorName() << reply.errorMessage();
            emit error(failure);
        }
        finishOperation(id, action, success, success ? QString() : reply.errorName(), success ? QString() : reply.errorMessage());
    });
}

void IWDController::IWDControllerPrivate::scan()
{
    // An IWD scan in progress serves this request too.
    if (cached(selectedDevicePath, stationInterface, QStringLiteral("Scanning")).toBool()) {
        finishOperation(activeOperationId, QStringLiteral("scan"), true);
        return;
    }
    watchOperation(callIwd(selectedDevicePath, stationInterface, QStringLiteral("Scan")), QStringLiteral("scan"), IWDController::ScanFailed);
}

void IWDController::IWDControllerPrivate::disconnectNetwork()
{
    watchOperation(callIwd(selectedDevicePath, stationInterface, QStringLiteral("Disconnect")), QStringLiteral("disconnect"),
                   IWDController::DisconnectFailed);
}

void IWDController::IWDControllerPrivate::connectNetwork(const QString &networkId)
{
    const auto entry = std::find_if(rankedNetworks.cbegin(), rankedNetworks.cend(),
                                    [&](const auto &candidate) { return lastPathSegment(candidate.first) == networkId; });
    if (networkId.isEmpty() || entry == rankedNetworks.cend()) {
        finishOperation(activeOperationId, QStringLiteral("connect"), false, notFoundError, QStringLiteral("Unknown network"));
        return;
    }
    watchOperation(callIwd(entry->first, networkInterface, QStringLiteral("Connect")), QStringLiteral("connect"), IWDController::ConnectFailed);
}

QString IWDController::IWDControllerPrivate::knownNetworkPathForId(const QString &networkId) const
{
    if (networkId.isEmpty()) {
        return {};
    }
    // Known networks need not be in range, so search IWD's KnownNetwork objects.
    for (auto it = objects.cbegin(); it != objects.cend(); ++it) {
        if (it->contains(knownNetworkInterface) && lastPathSegment(it.key()) == networkId) {
            return it.key();
        }
    }
    return {};
}

void IWDController::IWDControllerPrivate::forgetNetwork(const QString &networkId)
{
    const QString knownPath = knownNetworkPathForId(networkId);
    if (knownPath.isEmpty()) {
        finishOperation(activeOperationId, QStringLiteral("forget"), false, notFoundError, QStringLiteral("Unknown saved network"));
        return;
    }
    watchOperation(callIwd(knownPath, knownNetworkInterface, QStringLiteral("Forget")), QStringLiteral("forget"), IWDController::ForgetFailed);
}

void IWDController::IWDControllerPrivate::setAutoConnectNetwork(const QString &networkId, bool enabled)
{
    const QString knownPath = knownNetworkPathForId(networkId);
    if (knownPath.isEmpty()) {
        finishOperation(activeOperationId, QStringLiteral("autoconnect"), false, notFoundError, QStringLiteral("Unknown saved network"));
        return;
    }
    auto *call = callIwd(knownPath, propertiesInterface, QStringLiteral("Set"),
                         {knownNetworkInterface, QStringLiteral("AutoConnect"), QVariant::fromValue(QDBusVariant(enabled))});
    connect(call, &QDBusPendingCallWatcher::finished, this, [this, knownPath](QDBusPendingCallWatcher *) {
        refreshObject(knownPath, knownNetworkInterface);
    });
    watchOperation(call, QStringLiteral("autoconnect"), IWDController::SetAutoConnectFailed);
}

void IWDController::IWDControllerPrivate::getOrderedNetworks()
{
    if (!stationReady) {
        return;
    }
    if (orderedNetworksCall) {
        orderedNetworksPending = true;
        return;
    }
    auto *call = callIwd(selectedDevicePath, stationInterface, QStringLiteral("GetOrderedNetworks"));
    orderedNetworksCall = call;
    call->setProperty("deviceGeneration", QVariant::fromValue(deviceGeneration));
    connect(call, &QDBusPendingCallWatcher::finished, this, &IWDControllerPrivate::onGetOrderedNetworksFinished);
}

void IWDController::IWDControllerPrivate::onGetOrderedNetworksFinished(QDBusPendingCallWatcher *call)
{
    call->deleteLater();
    if (shuttingDown || call != orderedNetworksCall
        || call->property("deviceGeneration").toULongLong() != deviceGeneration) {
        return;
    }
    orderedNetworksCall = nullptr;

    QDBusPendingReply<OrderedNetworksList> reply = *call;
    if (reply.isValid()) {
        orderedNetworkList = reply.value();
        publishNetworkSnapshot();
    } else {
        qDebug() << "GetOrderedNetworks failed:" << reply.error();
    }

    emit initializationChanged(true);
    if (std::exchange(orderedNetworksPending, false)) {
        getOrderedNetworks();
    }
}

QVariantMap IWDController::IWDControllerPrivate::networkRow(const QString &path, int signalInCentiDBM) const
{
    const QVariantMap network = objects.value(path).value(networkInterface);
    const QString knownPath = objectPathValue(network.value(QStringLiteral("KnownNetwork")));
    const QVariantMap known = objects.value(knownPath).value(knownNetworkInterface);
    QString ssid = network.value(QStringLiteral("Name")).toString();
    if (ssid.isEmpty()) {
        ssid = QStringLiteral("(hidden network)");
    }
    return {
        {QStringLiteral("ssid"), ssid},
        {QStringLiteral("signalStrength"), normalizeSignalStrength(signalInCentiDBM)},
        {QStringLiteral("signalInDBM"), int(std::round(signalInCentiDBM / 100.0))},
        {QStringLiteral("type"), network.value(QStringLiteral("Type")).toString().toLower()},
        {QStringLiteral("networkId"), lastPathSegment(path)},
        {QStringLiteral("connected"), network.value(QStringLiteral("Connected")).toBool()},
        {QStringLiteral("known"), !knownPath.isEmpty()},
        {QStringLiteral("autoconnect"), known.value(QStringLiteral("AutoConnect")).toBool()},
        {QStringLiteral("lastTimeConnected"), known.value(QStringLiteral("LastConnectedTime")).toString()},
        {QStringLiteral("hidden"), known.value(QStringLiteral("Hidden")).toBool()},
    };
}

QVariantList IWDController::IWDControllerPrivate::knownNetworkRows() const
{
    QList<QVariantMap> rows;
    for (auto it = objects.cbegin(); it != objects.cend(); ++it) {
        const auto known = it->constFind(knownNetworkInterface);
        if (known == it->cend()) {
            continue;
        }
        rows.append(QVariantMap{
            {QStringLiteral("networkId"), lastPathSegment(it.key())},
            {QStringLiteral("ssid"), known->value(QStringLiteral("Name")).toString()},
            {QStringLiteral("type"), known->value(QStringLiteral("Type")).toString().toLower()},
            {QStringLiteral("autoconnect"), known->value(QStringLiteral("AutoConnect")).toBool()},
            {QStringLiteral("hidden"), known->value(QStringLiteral("Hidden")).toBool()},
            {QStringLiteral("lastTimeConnected"), known->value(QStringLiteral("LastConnectedTime")).toString()},
        });
    }
    std::sort(rows.begin(), rows.end(), [](const QVariantMap &a, const QVariantMap &b) {
        return a.value(QStringLiteral("networkId")).toString() < b.value(QStringLiteral("networkId")).toString();
    });
    QVariantList result;
    for (const auto &row : std::as_const(rows)) result << row;
    return result;
}

void IWDController::IWDControllerPrivate::publishState()
{
    const QString adapterPath = !selectedAdapterPath.isEmpty()
        ? selectedAdapterPath : deviceAdapterPaths.value(requestedDevicePath, adapters.value(0));
    const QVariantMap adapter = objects.value(adapterPath).value(adapterInterface);
    const QVariantMap station = stationReady ? objects.value(selectedDevicePath).value(stationInterface) : QVariantMap();

    const QString connectedPath = objectPathValue(station.value(QStringLiteral("ConnectedNetwork")));
    const QString connectedId = lastPathSegment(connectedPath);
    QVariantMap connectedNetworkData;
    int signalLevel = IWDIPC::weakestSignalLevel;
    if (!connectedId.isEmpty()) {
        // The station can announce its connection before GetOrderedNetworks
        // contains that network. Keep its identity available until rows catch up.
        connectedNetworkData.insert(QStringLiteral("networkId"), connectedId);
        const auto entry = std::find_if(rankedNetworks.cbegin(), rankedNetworks.cend(),
                                        [&](const auto &candidate) { return lastPathSegment(candidate.first) == connectedId; });
        if (entry != rankedNetworks.cend()) {
            const QVariantMap row = networkRow(entry->first, entry->second);
            for (const char *key : {"signalStrength", "signalInDBM", "ssid", "type", "autoconnect", "hidden"}) {
                connectedNetworkData.insert(QString::fromLatin1(key), row.value(QString::fromLatin1(key)));
            }
            if (agentSignalLevel < 0) {
                // No live measurement (another client may own IWD's single
                // signal agent): derive the level from the scanned RSSI.
                const int dbm = row.value(QStringLiteral("signalInDBM")).toInt();
                signalLevel = int(std::count_if(signalStrengthLevelThresholds.cbegin(), signalStrengthLevelThresholds.cend(),
                                                [dbm](short threshold) { return dbm < threshold; }));
            }
        }
        if (agentSignalLevel >= 0) {
            signalLevel = agentSignalLevel;
        }
    }

    emit stateChanged({
        {QStringLiteral("adapters"), adapters},
        {QStringLiteral("devices"), devices},
        {QStringLiteral("supportedModes"), stringListValue(adapter.value(QStringLiteral("SupportedModes")))},
        {QStringLiteral("model"), adapter.value(QStringLiteral("Model")).toString()},
        {QStringLiteral("name"), adapter.value(QStringLiteral("Name")).toString()},
        {QStringLiteral("vendor"), adapter.value(QStringLiteral("Vendor")).toString()},
        {QStringLiteral("powered"), adapter.value(QStringLiteral("Powered")).toBool()},
        {QStringLiteral("state"), station.value(QStringLiteral("State")).toString()},
        {QStringLiteral("scanning"), station.value(QStringLiteral("Scanning")).toBool()},
        {QStringLiteral("connected"), !connectedId.isEmpty()},
        {QStringLiteral("connectedNetworkData"), connectedNetworkData},
        {QStringLiteral("signalStrengthLevel"), signalLevel},
    });
}

void IWDController::IWDControllerPrivate::scheduleNetworkSnapshot()
{
    if (!networkSnapshotTimer->isActive()) {
        networkSnapshotTimer->start();
    }
}

void IWDController::IWDControllerPrivate::publishNetworkSnapshot()
{
    networkSnapshotTimer->stop();
    rankedNetworks.clear();
    QStringList seen;
    QVariantList rows;
    for (const auto &entry : std::as_const(orderedNetworkList)) {
        const QString path = entry.first.path();
        const QString networkId = lastPathSegment(path);
        if (networkId.isEmpty() || seen.contains(networkId)) {
            qCDebug(IWDBackendLog) << "Skipping network with empty or duplicate id from path" << path;
            continue;
        }
        if (!hasInterface(path, networkInterface)) {
            // Its InterfacesAdded signal, which carries the properties, follows shortly.
            continue;
        }
        seen << networkId;
        rankedNetworks.append({path, entry.second});
        rows << networkRow(path, entry.second);
    }
    emit networksChanged(rows, knownNetworkRows());
    // Connected-network summary data is derived from the same rows.
    publishState();
}

IWDController::IWDController(const QString &devicePath, QObject *parent)
    : QObject(parent), configuredDevicePath(devicePath)
{
    d = new IWDControllerPrivate(devicePath);
    connect(d, &IWDControllerPrivate::error, this, &IWDController::error);
    connect(d, &IWDControllerPrivate::operationCompleted, this, &IWDController::operationCompleted);
    connect(d, &IWDControllerPrivate::credentialsRequested, this, &IWDController::credentialsRequested);
    connect(d, &IWDControllerPrivate::initializationChanged, this, [this](bool ready) {
        cachedInitialized = ready;
        emit snapshotChanged();
    });
    connect(d, &IWDControllerPrivate::stateChanged, this, [this](const QVariantMap &state) {
        if (cachedState == state) return;
        cachedState = state;
        emit snapshotChanged();
    });
    connect(d, &IWDControllerPrivate::networksChanged, this, [this](const QVariantList &rows, const QVariantList &known) {
        if (cachedNetworkRows == rows && cachedKnownNetworks == known) return;
        cachedNetworkRows = rows;
        cachedKnownNetworks = known;
        emit snapshotChanged();
    });
    workerThread = new QThread(this);
    workerThread->setObjectName("qiwd-device-thread");
    connect(workerThread, &QThread::finished, d, &QObject::deleteLater);
    d->moveToThread(workerThread);
    workerThread->start();
    QMetaObject::invokeMethod(d, &IWDControllerPrivate::setup, Qt::QueuedConnection);
}

IWDController::~IWDController()
{
    if (workerThread && workerThread->isRunning()) {
        QMetaObject::invokeMethod(d, &IWDControllerPrivate::shutdown, Qt::BlockingQueuedConnection);
        workerThread->quit();
        workerThread->wait();
    }
}

void IWDController::executeOperation(const QString &id, const QString &action, const QVariantMap &arguments)
{
    QMetaObject::invokeMethod(d, "executeOperation", Qt::QueuedConnection,
                              Q_ARG(QString, id), Q_ARG(QString, action), Q_ARG(QVariantMap, arguments));
}

void IWDController::cancelOperation(const QString &id)
{
    QMetaObject::invokeMethod(d, "cancelOperation", Qt::QueuedConnection, Q_ARG(QString, id));
}

void IWDController::provideCredentials(const QString &id, const QString &password)
{
    QMetaObject::invokeMethod(d, "provideCredentials", Qt::QueuedConnection, Q_ARG(QString, id), Q_ARG(QString, password));
}

QVariantMap IWDController::snapshot() const
{
    QVariantMap snapshot = cachedState;
    snapshot.insert(QStringLiteral("devicePath"), configuredDevicePath);
    snapshot.insert(QStringLiteral("initialized"), cachedInitialized);
    snapshot.insert(QStringLiteral("signalStrengthLevel"), signalStrengthLevel());
    snapshot.insert(QStringLiteral("networks"), cachedNetworkRows);
    snapshot.insert(QStringLiteral("knownNetworks"), cachedKnownNetworks);
    return snapshot;
}

QStringList IWDController::adapters() const { return cachedState.value(QStringLiteral("adapters")).toStringList(); }
QStringList IWDController::devices() const { return cachedState.value(QStringLiteral("devices")).toStringList(); }
QStringList IWDController::supportedModes() const { return cachedState.value(QStringLiteral("supportedModes")).toStringList(); }
int IWDController::signalStrengthLevel() const
{
    return cachedState.value(QStringLiteral("signalStrengthLevel"), IWDIPC::weakestSignalLevel).toInt();
}
bool IWDController::connected() const { return cachedState.value(QStringLiteral("connected")).toBool(); }
bool IWDController::scanning() const { return cachedState.value(QStringLiteral("scanning")).toBool(); }
bool IWDController::powered() const { return cachedState.value(QStringLiteral("powered")).toBool(); }
QString IWDController::state() const { return cachedState.value(QStringLiteral("state")).toString(); }
QString IWDController::model() const { return cachedState.value(QStringLiteral("model")).toString(); }
QString IWDController::name() const { return cachedState.value(QStringLiteral("name")).toString(); }
QString IWDController::vendor() const { return cachedState.value(QStringLiteral("vendor")).toString(); }
QVariantMap IWDController::connectedNetworkData() const { return cachedState.value(QStringLiteral("connectedNetworkData")).toMap(); }

Q_LOGGING_CATEGORY(IWDBackendLog, "qdbus.iwd.backend", QtInfoMsg)
Q_LOGGING_CATEGORY(IWDBackendPropertiesLog, "qdbus.iwd.properties", QtInfoMsg)

#include "iwd-controller.moc"
