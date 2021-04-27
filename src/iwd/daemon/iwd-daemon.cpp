// Copyright (C) 2026 Shaper Tools
// SPDX-License-Identifier: LGPL-2.1-only

#include "iwd-daemon.h"
#include "iwd-ipc_p.h"

#include <QUuid>
#include <algorithm>

class IWDManagerAdaptor : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "com.shapertools.qdbussys.iwd1")
public:
    explicit IWDManagerAdaptor(IWDDaemon *daemon) : QDBusAbstractAdaptor(daemon), daemon(daemon) {}
public slots:
    QVariantMap GetSnapshot() const { return daemon->snapshot(); }
    void Request(const QString &client, const QString &id, const QString &device, const QString &action,
                 const QVariantMap &arguments, const QDBusMessage &message)
    {
        daemon->request(message, client, id, device, action, IWDIPC::unpack(arguments).toMap());
    }
    void Cancel(const QString &client, const QString &id, const QDBusMessage &message)
    {
        daemon->cancel(message.service(), client, id);
    }
    void ProvideCredentials(const QString &client, const QString &id, const QString &password, const QDBusMessage &message)
    {
        if (!daemon->provideCredentials(message.service(), client, id, password)) {
            message.setDelayedReply(true);
            QDBusConnection::systemBus().send(message.createErrorReply(QDBusError::AccessDenied, QStringLiteral("No matching credential request")));
        }
    }
    void ReleaseClient(const QString &client, const QDBusMessage &message)
    {
        daemon->releaseClient(message.service(), client);
    }
signals:
    void SnapshotDelta(const QVariantMap &delta);
    void OperationFinished(const QString &client, const QString &id, bool success, const QString &error, const QString &message);
    void CredentialsRequested(const QString &client, const QString &id, const QString &networkId);
private:
    IWDDaemon *daemon;
};

IWDDaemon::IWDDaemon(QObject *parent) : QObject(parent), epoch(QUuid::createUuid().toString())
{
    qDBusRegisterMetaType<ManagedObjectList>();
    qDBusRegisterMetaType<InterfacePropertyMap>();
    adaptor = new IWDManagerAdaptor(this);
    refreshTimer.setSingleShot(true);
    publishTimer.setSingleShot(true);
    publishTimer.setInterval(20);
    connect(&refreshTimer, &QTimer::timeout, this, &IWDDaemon::refresh);
    connect(&publishTimer, &QTimer::timeout, this, &IWDDaemon::publish);
    ownerWatcher = new QDBusServiceWatcher(QStringLiteral("net.connman.iwd"), bus,
                                          QDBusServiceWatcher::WatchForOwnerChange, this);
    connect(ownerWatcher, &QDBusServiceWatcher::serviceOwnerChanged, this, &IWDDaemon::ownerChanged);
    clientWatcher = new QDBusServiceWatcher(this);
    clientWatcher->setConnection(bus);
    clientWatcher->setWatchMode(QDBusServiceWatcher::WatchForUnregistration);
    connect(clientWatcher, &QDBusServiceWatcher::serviceUnregistered, this, [this](const QString &name) {
        clientWatcher->removeWatchedService(name);
        releaseClient(name, {});
    });
    bus.connect(QStringLiteral("net.connman.iwd"), QStringLiteral("/"), QStringLiteral("org.freedesktop.DBus.ObjectManager"),
                QStringLiteral("InterfacesAdded"), this, SLOT(interfacesAdded(QDBusObjectPath,InterfacePropertyMap)));
    bus.connect(QStringLiteral("net.connman.iwd"), QStringLiteral("/"), QStringLiteral("org.freedesktop.DBus.ObjectManager"),
                QStringLiteral("InterfacesRemoved"), this, SLOT(interfacesRemoved(QDBusObjectPath,QStringList)));
    // Publish a valid snapshot at once; the inventory is ready after the lookup.
    publish();
    auto message = QDBusMessage::createMethodCall(QStringLiteral("org.freedesktop.DBus"), QStringLiteral("/org/freedesktop/DBus"),
                                                  QStringLiteral("org.freedesktop.DBus"), QStringLiteral("GetNameOwner"));
    message << QStringLiteral("net.connman.iwd");
    auto *call = new QDBusPendingCallWatcher(bus.asyncCall(message), this);
    connect(call, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *finished) {
        QDBusPendingReply<QString> reply = *finished;
        finished->deleteLater();
        // The owner watcher may already have reported IWD.
        if (!owner.isEmpty()) return;
        if (reply.isError()) {
            inventoryReady = true;
            publish();
        } else {
            ownerChanged(QStringLiteral("net.connman.iwd"), {}, reply.value());
        }
    });
}

void IWDDaemon::ownerChanged(const QString &, const QString &, const QString &newOwner)
{
    if (newOwner == owner) return;
    ++generation;
    owner = newOwner;
    refreshing = false;
    refreshPending = false;
    refreshTimer.stop();
    for (const QString &device : controllers.keys()) removeDevice(device);
    adapters.clear();
    devices.clear();
    inventoryReady = owner.isEmpty();
    publish();
    if (!owner.isEmpty()) scheduleRefresh();
}

void IWDDaemon::interfacesAdded(const QDBusObjectPath &, const InterfacePropertyMap &interfaces)
{
    if (interfaces.contains(QStringLiteral("net.connman.iwd.Adapter"))
        || interfaces.contains(QStringLiteral("net.connman.iwd.Device"))
        || interfaces.contains(QStringLiteral("net.connman.iwd.Station"))) scheduleRefresh();
}

void IWDDaemon::interfacesRemoved(const QDBusObjectPath &, const QStringList &interfaces)
{
    if (interfaces.contains(QStringLiteral("net.connman.iwd.Adapter"))
        || interfaces.contains(QStringLiteral("net.connman.iwd.Device"))
        || interfaces.contains(QStringLiteral("net.connman.iwd.Station"))) scheduleRefresh();
}

void IWDDaemon::scheduleRefresh()
{
    if (owner.isEmpty()) return;
    refreshPending = true;
    if (!refreshing) refreshTimer.start(0);
}

void IWDDaemon::refresh()
{
    if (owner.isEmpty() || refreshing) return;
    refreshing = true;
    refreshPending = false;
    const auto requestGeneration = generation;
    auto message = QDBusMessage::createMethodCall(owner, QStringLiteral("/"),
        QStringLiteral("org.freedesktop.DBus.ObjectManager"), QStringLiteral("GetManagedObjects"));
    auto *call = new QDBusPendingCallWatcher(bus.asyncCall(message), this);
    connect(call, &QDBusPendingCallWatcher::finished, this, [this, requestGeneration](QDBusPendingCallWatcher *finished) {
        QDBusPendingReply<ManagedObjectList> reply = *finished;
        finished->deleteLater();
        if (requestGeneration != generation) return;
        refreshing = false;
        if (refreshPending) {
            scheduleRefresh();
            return;
        }
        if (reply.isError()) {
            qWarning() << "qiwd: IWD inventory failed:" << reply.error().name();
            inventoryReady = true;
            publish();
            return;
        }
        QStringList nextDevices, nextAdapters;
        const auto objects = reply.value();
        for (auto it = objects.cbegin(); it != objects.cend(); ++it) {
            if (it.value().contains(QStringLiteral("net.connman.iwd.Device"))) nextDevices << it.key().path();
            if (it.value().contains(QStringLiteral("net.connman.iwd.Adapter"))) nextAdapters << it.key().path();
        }
        nextDevices.sort();
        nextAdapters.sort();
        for (const QString &device : controllers.keys()) {
            if (!nextDevices.contains(device)) removeDevice(device);
        }
        devices = nextDevices;
        adapters = nextAdapters;
        for (const QString &device : devices) {
            if (controllers.contains(device)) continue;
            auto *controller = new IWDController(device, this);
            controllers.insert(device, controller);
            connect(controller, &IWDController::snapshotChanged, this, [this, device] {
                publishTimer.start();
                startNext(device);
            });
            connect(controller, &IWDController::operationCompleted, this,
                    [this, device](const QString &id, const QString &, bool success, const QString &error, const QString &message) {
                completed(device, id, success, error, message);
            });
            connect(controller, &IWDController::credentialsRequested, this,
                    [this, device](const QString &id, const QString &networkId) {
                auto &queue = queues[device];
                if (!queue.active || queue.active->id != id || queue.active->canceled) return;
                for (const Caller &caller : queue.active->callers) {
                    auto signal = QDBusMessage::createTargetedSignal(caller.sender, QString::fromLatin1(IWDIPC::path), QString::fromLatin1(IWDIPC::interface), QStringLiteral("CredentialsRequested"));
                    signal << caller.client << caller.id << networkId;
                    bus.send(signal);
                }
            });
        }
        inventoryReady = true;
        publishTimer.start();
    });
}

void IWDDaemon::publish()
{
    QVariantMap states;
    for (auto it = controllers.cbegin(); it != controllers.cend(); ++it) states[it.key()] = it.value()->snapshot();
    QVariantMap next{{"version", IWDIPC::version}, {"epoch", epoch}, {"available", !owner.isEmpty()},
                     {"inventoryReady", inventoryReady}, {"adapters", adapters}, {"devices", devices}, {"deviceStates", states}};
    auto previous = published;
    previous.remove(QStringLiteral("revision"));
    if (next == previous) return;
    const quint64 baseRevision = revision;
    next[QStringLiteral("revision")] = QVariant::fromValue(++revision);
    const QVariantMap changes = delta(published, next, baseRevision);
    published = next;
    emit adaptor->SnapshotDelta(changes);
}

// Only changed top-level keys and changed per-device keys are sent. A device
// that is new, or lost keys, is replaced whole; clients apply a delta only on
// top of baseRevision and otherwise fetch the full state.
QVariantMap IWDDaemon::delta(const QVariantMap &previous, const QVariantMap &next, quint64 baseRevision) const
{
    static const QStringList envelope{"version", "epoch", "revision", "deviceStates"};
    QVariantMap changes;
    for (auto it = next.cbegin(); it != next.cend(); ++it) {
        if (!envelope.contains(it.key()) && previous.value(it.key()) != it.value()) changes.insert(it.key(), it.value());
    }

    const QVariantMap previousStates = previous.value(QStringLiteral("deviceStates")).toMap();
    const QVariantMap nextStates = next.value(QStringLiteral("deviceStates")).toMap();
    QVariantMap deviceChanges, replacedDevices;
    QStringList removedDevices;
    for (auto it = previousStates.cbegin(); it != previousStates.cend(); ++it) {
        if (!nextStates.contains(it.key())) removedDevices << it.key();
    }
    for (auto it = nextStates.cbegin(); it != nextStates.cend(); ++it) {
        const QVariantMap state = it.value().toMap();
        if (!previousStates.contains(it.key())) {
            replacedDevices.insert(it.key(), state);
            continue;
        }
        const QVariantMap old = previousStates.value(it.key()).toMap();
        if (std::any_of(old.keyBegin(), old.keyEnd(), [&](const QString &key) { return !state.contains(key); })) {
            replacedDevices.insert(it.key(), state);
            continue;
        }
        QVariantMap changed;
        for (auto value = state.cbegin(); value != state.cend(); ++value) {
            if (old.value(value.key()) != value.value()) changed.insert(value.key(), value.value());
        }
        if (!changed.isEmpty()) deviceChanges.insert(it.key(), changed);
    }

    return {{"version", IWDIPC::version}, {"epoch", epoch}, {"revision", next.value(QStringLiteral("revision"))},
            {"baseRevision", QVariant::fromValue(baseRevision)}, {"changes", changes},
            {"deviceChanges", deviceChanges}, {"replacedDevices", replacedDevices}, {"removedDevices", removedDevices}};
}

void IWDDaemon::report(Caller &caller, bool success, const QString &error, const QString &message)
{
    if (caller.reported) return;
    caller.reported = true;
    auto signal = QDBusMessage::createTargetedSignal(caller.sender, QString::fromLatin1(IWDIPC::path), QString::fromLatin1(IWDIPC::interface), QStringLiteral("OperationFinished"));
    signal << caller.client << caller.id << success << error << message;
    bus.send(signal);
}

void IWDDaemon::watchClient(const QString &sender)
{
    if (sender.isEmpty() || clientWatcher->watchedServices().contains(sender)) {
        return;
    }
    clientWatcher->addWatchedService(sender);
    // The client may have exited before the watch was installed.
    auto message = QDBusMessage::createMethodCall(QStringLiteral("org.freedesktop.DBus"), QStringLiteral("/org/freedesktop/DBus"),
                                                  QStringLiteral("org.freedesktop.DBus"), QStringLiteral("GetNameOwner"));
    message << sender;
    auto *call = new QDBusPendingCallWatcher(bus.asyncCall(message), this);
    connect(call, &QDBusPendingCallWatcher::finished, this, [this, sender](QDBusPendingCallWatcher *finished) {
        finished->deleteLater();
        if (finished->isError()) {
            clientWatcher->removeWatchedService(sender);
            releaseClient(sender, {});
        }
    });
}

void IWDDaemon::request(const QDBusMessage &message, const QString &client, const QString &id,
                        const QString &device, const QString &action, const QVariantMap &arguments)
{
    Caller caller{message.service(), client, id};
    const QStringList actions{"scan", "connect", "disconnect", "forget", "autoconnect"};
    if (client.isEmpty() || client.size() > 128 || id.isEmpty() || id.size() > 128 || !actions.contains(action)) {
        report(caller, false, QStringLiteral("com.shapertools.qdbussys.iwd.InvalidArguments"), QStringLiteral("Invalid operation"));
        return;
    }
    if (!controllers.contains(device)) {
        report(caller, false, QStringLiteral("com.shapertools.qdbussys.iwd.NotAvailable"), QStringLiteral("Device is not available"));
        return;
    }
    watchClient(caller.sender);
    auto &queue = queues[device];
    auto same = [&](const Caller &entry) { return entry.sender == caller.sender && entry.client == client && entry.id == id; };
    if (queue.active && std::any_of(queue.active->callers.cbegin(), queue.active->callers.cend(), same)) return;
    for (const auto &operation : queue.waiting) {
        if (std::any_of(operation.callers.cbegin(), operation.callers.cend(), same)) return;
    }
    if (action == QLatin1String("scan")) {
        if (queue.active && queue.active->action == action && queue.active->callers.size() < 32) {
            queue.active->callers << caller;
            return;
        }
        for (auto &operation : queue.waiting) {
            if (operation.action == action && operation.callers.size() < 32) {
                operation.callers << caller;
                return;
            }
        }
    }
    if (queue.waiting.size() >= 32) {
        report(caller, false, QStringLiteral("com.shapertools.qdbussys.iwd.LimitsExceeded"), QStringLiteral("Device operation queue is full"));
        return;
    }
    queue.waiting.enqueue(Operation{id, action, arguments, {caller}});
    startNext(device);
}

void IWDDaemon::startNext(const QString &device)
{
    auto *controller = controllers.value(device);
    if (!controller || !controller->snapshot().value(QStringLiteral("initialized")).toBool()) return;
    auto &queue = queues[device];
    if (queue.active || queue.waiting.isEmpty()) return;
    queue.active = queue.waiting.dequeue();
    const auto id = queue.active->id;
    const auto action = queue.active->action;
    controller->executeOperation(id, action, queue.active->arguments);
    queue.active->arguments.clear();
}

void IWDDaemon::completed(const QString &device, const QString &id, bool success, const QString &error, const QString &message)
{
    auto &queue = queues[device];
    if (!queue.active || queue.active->id != id) return;
    for (auto &caller : queue.active->callers) report(caller, success, error, message);
    queue.active.reset();
    startNext(device);
}

void IWDDaemon::cancel(const QString &sender, const QString &client, const QString &id)
{
    for (auto it = queues.begin(); it != queues.end(); ++it) {
        auto matches = [&](const Caller &caller) { return caller.sender == sender && caller.client == client && caller.id == id; };
        for (auto operation = it->waiting.begin(); operation != it->waiting.end();) {
            for (auto &caller : operation->callers) {
                if (matches(caller)) report(caller, false, QStringLiteral("com.shapertools.qdbussys.iwd.Canceled"), QStringLiteral("Operation canceled"));
            }
            if (std::all_of(operation->callers.cbegin(), operation->callers.cend(), [](const Caller &caller) { return caller.reported; })) operation = it->waiting.erase(operation);
            else ++operation;
        }
        if (!it->active) continue;
        for (auto &caller : it->active->callers) {
            if (matches(caller)) report(caller, false, QStringLiteral("com.shapertools.qdbussys.iwd.Canceled"), QStringLiteral("Operation canceled"));
        }
        if (it->active->action == QLatin1String("connect") && !it->active->canceled
            && std::all_of(it->active->callers.cbegin(), it->active->callers.cend(), [](const Caller &caller) { return caller.reported; })) {
            it->active->canceled = true;
            controllers.value(it.key())->cancelOperation(it->active->id);
        }
    }
}

bool IWDDaemon::provideCredentials(const QString &sender, const QString &client, const QString &id, const QString &password)
{
    for (auto it = queues.begin(); it != queues.end(); ++it) {
        if (!it->active || it->active->canceled || it->active->action != QLatin1String("connect")) continue;
        for (const auto &caller : it->active->callers) {
            if (caller.sender == sender && caller.client == client && caller.id == id && !caller.reported) {
                controllers.value(it.key())->provideCredentials(it->active->id, password);
                return true;
            }
        }
    }
    return false;
}

void IWDDaemon::releaseClient(const QString &sender, const QString &client)
{
    QStringList ids;
    for (const auto &queue : std::as_const(queues)) {
        auto collect = [&](const Operation &operation) {
            for (const auto &caller : operation.callers) {
                if (caller.sender == sender && (client.isEmpty() || caller.client == client)) ids << caller.id;
            }
        };
        if (queue.active) collect(*queue.active);
        for (const auto &operation : queue.waiting) collect(operation);
    }
    // Resolve the token for each id when an entire D-Bus connection vanished.
    for (const QString &id : ids) {
        QStringList tokens;
        for (const auto &queue : std::as_const(queues)) {
            auto collect = [&](const Operation &operation) {
                for (const auto &caller : operation.callers) {
                    if (caller.sender == sender && caller.id == id && (client.isEmpty() || caller.client == client)) tokens << caller.client;
                }
            };
            if (queue.active) collect(*queue.active);
            for (const auto &operation : queue.waiting) collect(operation);
        }
        for (const QString &token : tokens) cancel(sender, token, id);
    }
}

void IWDDaemon::removeDevice(const QString &device)
{
    const auto queue = queues.take(device);
    auto fail = [this](Operation operation) {
        for (auto &caller : operation.callers) report(caller, false, QStringLiteral("com.shapertools.qdbussys.iwd.NotAvailable"), QStringLiteral("Device disappeared"));
    };
    if (queue.active) fail(*queue.active);
    for (const auto &operation : queue.waiting) fail(operation);
    delete controllers.take(device);
}

#include "iwd-daemon.moc"
