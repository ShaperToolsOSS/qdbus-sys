// Copyright (C) 2026 Shaper Tools
// SPDX-License-Identifier: LGPL-2.1-only

#include "iwd.h"
#include "iwd-ipc_p.h"

#include <QTimer>
#include <algorithm>
#include <utility>
#include <QUuid>

namespace QDBusSys {

class IWD::IWDPrivate : public QObject
{
    Q_OBJECT
public:
    explicit IWDPrivate(IWD *state) : QObject(state), state(state), networkModel(new IWDNetworkModel(state))
    {
        deadline.setSingleShot(true);
        deadline.setInterval(10000);
        connect(&deadline, &QTimer::timeout, this, [this] {
            const auto requests = pending;
            for (const auto &request : requests) complete(request.id, false, QStringLiteral("com.shapertools.qdbussys.iwd.NotAvailable"), QStringLiteral("Daemon startup timed out"));
        });
        if (!bus.isConnected()) return;
        watcher = new QDBusServiceWatcher(QString::fromLatin1(IWDIPC::service), bus,
                                          QDBusServiceWatcher::WatchForOwnerChange, this);
        connect(watcher, &QDBusServiceWatcher::serviceOwnerChanged, this,
                [this](const QString &, const QString &, const QString &newOwner) { setOwner(newOwner); });
        bus.connect(QString::fromLatin1(IWDIPC::service), QString::fromLatin1(IWDIPC::path), QString::fromLatin1(IWDIPC::interface),
                    QStringLiteral("SnapshotDelta"), this, SLOT(onSnapshotDelta(QVariantMap)));
        bus.connect(QString::fromLatin1(IWDIPC::service), QString::fromLatin1(IWDIPC::path), QString::fromLatin1(IWDIPC::interface),
                    QStringLiteral("OperationFinished"), this, SLOT(onOperationFinished(QString,QString,bool,QString,QString)));
        bus.connect(QString::fromLatin1(IWDIPC::service), QString::fromLatin1(IWDIPC::path), QString::fromLatin1(IWDIPC::interface),
                    QStringLiteral("CredentialsRequested"), this, SLOT(onCredentialsRequested(QString,QString,QString)));
    }

    ~IWDPrivate() override
    {
        if (!owner.isEmpty()) send(QStringLiteral("ReleaseClient"), {clientId});
    }

    struct Pending {
        QString id, action, device;
        QVariantMap arguments;
        bool sent = false;
    };
    QString request(const QString &action, const QVariantMap &arguments = {})
    {
        const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        pending.append({id, action, state->selectedDevice(), arguments});
        if (owner.isEmpty() || !haveSnapshot || !latest.value(QStringLiteral("inventoryReady")).toBool()) {
            if (!deadline.isActive()) deadline.start();
            ensureStarted();
        } else flush();
        return id;
    }

    void complete(const QString &id, bool success, const QString &name, const QString &message)
    {
        auto it = std::find_if(pending.begin(), pending.end(), [&](const Pending &entry) { return entry.id == id; });
        if (it == pending.end()) return;
        const QString action = it->action;
        pending.erase(it);
        if (!success) {
            IWD::Error code = IWD::OperationFailed;
            if (name == QLatin1String(IWDIPC::agentUnavailableError)) code = IWD::RegisterAuthAgentFailed;
            else if (action == QLatin1String("scan")) code = IWD::ScanFailed;
            else if (action == QLatin1String("connect")) code = IWD::ConnectFailed;
            else if (action == QLatin1String("autoconnect")) code = IWD::SetAutoConnectFailed;
            else if (action == QLatin1String("disconnect")) code = IWD::DisconnectFailed;
            else if (action == QLatin1String("forget")) code = IWD::ForgetFailed;
            emit state->error(code);
        }
        emit state->operationFinished(id, success, name, message);
    }

    QDBusPendingCall send(const QString &method, const QVariantList &arguments)
    {
        auto message = QDBusMessage::createMethodCall(QString::fromLatin1(IWDIPC::service), QString::fromLatin1(IWDIPC::path),
                                                     QString::fromLatin1(IWDIPC::interface), method);
        message.setAutoStartService(false);
        message.setArguments(arguments);
        return bus.asyncCall(message);
    }

    QDBusMessage busCall(const QString &method, const QVariantList &arguments)
    {
        auto message = QDBusMessage::createMethodCall(QStringLiteral("org.freedesktop.DBus"), QStringLiteral("/org/freedesktop/DBus"),
                                                     QStringLiteral("org.freedesktop.DBus"), method);
        message.setArguments(arguments);
        return message;
    }

    // Asynchronous throughout: look up the broker, activating it only when the
    // name is unowned (activation fails for running services without a
    // .service file).
    void ensureStarted()
    {
        if (!bus.isConnected()) return;
        if (!owner.isEmpty()) {
            if (!haveSnapshot) fetch();
            return;
        }
        if (activating) return;
        activating = true;
        lookUpOwner(true);
    }

    void lookUpOwner(bool activateIfMissing)
    {
        auto *lookup = new QDBusPendingCallWatcher(bus.asyncCall(busCall(QStringLiteral("GetNameOwner"),
                                                                         {QString::fromLatin1(IWDIPC::service)})), this);
        connect(lookup, &QDBusPendingCallWatcher::finished, this, [this, activateIfMissing](QDBusPendingCallWatcher *found) {
            QDBusPendingReply<QString> current = *found;
            found->deleteLater();
            if (current.isError() && activateIfMissing) {
                activate();
                return;
            }
            activating = false;
            if (current.isError()) return;
            if (owner != current.value()) setOwner(current.value());
            else fetch();
        });
    }

    void activate()
    {
        auto *call = new QDBusPendingCallWatcher(bus.asyncCall(busCall(QStringLiteral("StartServiceByName"),
                                                                       {QString::fromLatin1(IWDIPC::service), quint32(0)})), this);
        connect(call, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *finished) {
            QDBusPendingReply<quint32> reply = *finished;
            finished->deleteLater();
            if (reply.isError()) {
                activating = false;
                const auto requests = pending;
                for (const auto &request : requests) complete(request.id, false, reply.error().name(), reply.error().message());
                return;
            }
            lookUpOwner(false);
        });
    }

    void setOwner(const QString &newOwner)
    {
        if (owner == newOwner) return;
        const bool replacing = !owner.isEmpty();
        owner = newOwner;
        ++generation;
        haveSnapshot = false;
        fetching = refetch = false;
        epoch.clear();
        revision = 0;
        latest.clear();
        passwords.clear();
        apply();
        if (replacing) {
            const auto requests = pending;
            for (const auto &request : requests) {
                if (request.sent) complete(request.id, false, QStringLiteral("com.shapertools.qdbussys.iwd.NotAvailable"), QStringLiteral("Daemon disappeared; operation outcome is unknown"));
            }
        }
        if (!owner.isEmpty()) fetch();
    }

    // Deltas received while a snapshot is outstanding were emitted before it
    // (the broker's messages arrive in order), so they are already included.
    void fetch()
    {
        if (owner.isEmpty()) return;
        if (fetching) {
            refetch = true;
            return;
        }
        fetching = true;
        refetch = false;
        const auto requestGeneration = generation;
        auto *call = new QDBusPendingCallWatcher(send(QStringLiteral("GetSnapshot"), {}), this);
        connect(call, &QDBusPendingCallWatcher::finished, this, [this, requestGeneration](QDBusPendingCallWatcher *finished) {
            QDBusPendingReply<QVariantMap> reply = *finished;
            finished->deleteLater();
            if (requestGeneration != generation) return;
            fetching = false;
            if (!reply.isError()) onSnapshot(reply.value());
            if (std::exchange(refetch, false)) fetch();
        });
    }

    void apply()
    {
        const QStringList devices = latest.value(QStringLiteral("devices")).toStringList();
        QString selected = selection;
        if (selected.isEmpty()) selected = devices.contains(cachedSelectedDevice) ? cachedSelectedDevice : devices.value(0);
        const bool selectionChanged = selected != cachedSelectedDevice;
        cachedSelectedDevice = selected;
        const bool available = !owner.isEmpty() && latest.value(QStringLiteral("available")).toBool();
        const bool availableChanged = cachedAvailable != available;
        cachedAvailable = available;
        const auto device = latest.value(QStringLiteral("deviceStates")).toMap().value(selected).toMap();
        state->applyAdapterSnapshot(latest.value(QStringLiteral("adapters")).toStringList(), devices,
            device.value(QStringLiteral("supportedModes")).toStringList(), device.value(QStringLiteral("model")).toString(),
            device.value(QStringLiteral("name")).toString(), device.value(QStringLiteral("vendor")).toString(), device.value(QStringLiteral("powered")).toBool());
        state->applyDeviceSnapshot(device.value(QStringLiteral("state")).toString(), device.value(QStringLiteral("scanning")).toBool(),
            device.value(QStringLiteral("connected")).toBool(), device.value(QStringLiteral("connectedNetworkData")).toMap(),
            device.value(QStringLiteral("signalStrengthLevel"), IWDIPC::weakestSignalLevel).toInt());
        networkModel->applyNetworkSnapshot(device.value(QStringLiteral("networks")).toList());
        const QVariantList known = device.value(QStringLiteral("knownNetworks")).toList();
        if (known != cachedKnownNetworks) {
            cachedKnownNetworks = known;
            emit state->knownNetworksChanged();
        }
        if (selectionChanged) emit state->selectedDeviceChanged();
        if (availableChanged) emit state->availableChanged();
        emit state->snapshotChanged();
    }

    void flush()
    {
        if (owner.isEmpty() || !haveSnapshot || !latest.value(QStringLiteral("inventoryReady")).toBool()) return;
        deadline.stop();
        for (auto &request : pending) {
            if (request.sent) continue;
            request.sent = true;
            if (request.device.isEmpty()) request.device = state->selectedDevice();
            auto *call = new QDBusPendingCallWatcher(send(QStringLiteral("Request"),
                {clientId, request.id, request.device, request.action, request.arguments}), this);
            request.arguments.clear();
            const QString id = request.id;
            connect(call, &QDBusPendingCallWatcher::finished, this, [this, id](QDBusPendingCallWatcher *finished) {
                QDBusPendingReply<> reply = *finished;
                finished->deleteLater();
                if (reply.isError()) complete(id, false, reply.error().name(), reply.error().message());
            });
        }
    }

public slots:
    void onSnapshot(const QVariantMap &wire)
    {
        if (owner.isEmpty()) return;
        const auto snapshot = IWDIPC::unpack(wire).toMap();
        if (snapshot.value(QStringLiteral("version")).toInt() != IWDIPC::version) return;
        const QString nextEpoch = snapshot.value(QStringLiteral("epoch")).toString();
        const auto nextRevision = snapshot.value(QStringLiteral("revision")).toULongLong();
        if (nextEpoch == epoch && haveSnapshot && nextRevision <= revision) return;
        epoch = nextEpoch;
        revision = nextRevision;
        haveSnapshot = true;
        latest = snapshot;
        apply();
        flush();
    }
    void onSnapshotDelta(const QVariantMap &wire)
    {
        if (owner.isEmpty() || fetching) return;
        const auto delta = IWDIPC::unpack(wire).toMap();
        if (delta.value(QStringLiteral("version")).toInt() != IWDIPC::version) return;
        const auto nextRevision = delta.value(QStringLiteral("revision")).toULongLong();
        if (haveSnapshot && delta.value(QStringLiteral("epoch")).toString() == epoch && nextRevision <= revision) return;
        if (!haveSnapshot || delta.value(QStringLiteral("epoch")).toString() != epoch
            || delta.value(QStringLiteral("baseRevision")).toULongLong() != revision) {
            // A new broker instance or a missed update: resynchronise.
            fetch();
            return;
        }
        const QVariantMap changes = delta.value(QStringLiteral("changes")).toMap();
        for (auto it = changes.cbegin(); it != changes.cend(); ++it) latest.insert(it.key(), it.value());
        QVariantMap states = latest.value(QStringLiteral("deviceStates")).toMap();
        for (const QString &device : delta.value(QStringLiteral("removedDevices")).toStringList()) states.remove(device);
        const QVariantMap replaced = delta.value(QStringLiteral("replacedDevices")).toMap();
        for (auto it = replaced.cbegin(); it != replaced.cend(); ++it) states.insert(it.key(), it.value());
        const QVariantMap deviceChanges = delta.value(QStringLiteral("deviceChanges")).toMap();
        for (auto it = deviceChanges.cbegin(); it != deviceChanges.cend(); ++it) {
            QVariantMap device = states.value(it.key()).toMap();
            const QVariantMap changed = it.value().toMap();
            for (auto value = changed.cbegin(); value != changed.cend(); ++value) device.insert(value.key(), value.value());
            states.insert(it.key(), device);
        }
        latest.insert(QStringLiteral("deviceStates"), states);
        latest.insert(QStringLiteral("revision"), QVariant::fromValue(nextRevision));
        revision = nextRevision;
        apply();
        flush();
    }
    void onOperationFinished(const QString &client, const QString &id, bool success, const QString &name, const QString &message)
    {
        if (client == clientId) complete(id, success, name, message);
    }
    void onCredentialsRequested(const QString &client, const QString &id, const QString &networkId)
    {
        if (client == clientId) emit state->credentialsRequested(id, networkId);
    }

public:
    IWD *state;
    IWDNetworkModel *networkModel;
    bool cachedAvailable = false;
    QString cachedSelectedDevice;
    QStringList cachedAdapters;
    QStringList cachedDevices;
    QStringList cachedSupportedModes;
    QStringList cachedPossibleStates;
    QString cachedState;
    QString cachedModel;
    QString cachedName;
    QString cachedVendor;
    QVariantMap cachedConnectedNetworkData;
    bool cachedConnected = false;
    bool cachedScanning = false;
    bool cachedPowered = false;
    int cachedSignalStrengthLevel = IWDIPC::weakestSignalLevel;
    QVariantList cachedKnownNetworks;

    QDBusConnection bus = QDBusConnection::systemBus();
    QDBusServiceWatcher *watcher = nullptr;
    QString clientId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QString owner, epoch, selection;
    quint64 generation = 0, revision = 0;
    bool activating = false, haveSnapshot = false, fetching = false, refetch = false;
    QTimer deadline;
    QVariantMap latest;
    QList<Pending> pending;
    QHash<QString, QString> passwords;
};

IWD::IWD(QObject *parent) : QObject(parent), d(new IWDPrivate(this))
{
    d->cachedPossibleStates << "connected" << "disconnected" << "connecting" << "disconnecting" << "roaming";
    d->ensureStarted();
}

bool IWD::available() const { return d->cachedAvailable; }
QString IWD::selectedDevice() const { return d->cachedSelectedDevice; }
IWDNetworkModel *IWD::networks() const { return d->networkModel; }
QVariantList IWD::knownNetworks() const { return d->cachedKnownNetworks; }

IWD::~IWD() = default;

void IWD::setSelectedDevice(const QString &path)
{
    if (d->selection == path) return;
    d->passwords.clear();
    d->selection = path;
    d->apply();
}

QString IWD::scan() { return d->request(QStringLiteral("scan")); }
QString IWD::disconnectNetwork() { return d->request(QStringLiteral("disconnect")); }
QString IWD::connectNetwork(const QString &id)
{
    QVariantMap arguments{{"networkId", id}};
    const QString key = selectedDevice() + '\n' + id;
    if (d->passwords.contains(key)) arguments[QStringLiteral("passphrase")] = d->passwords.take(key);
    return d->request(QStringLiteral("connect"), arguments);
}
QString IWD::forgetNetwork(const QString &id) { return d->request(QStringLiteral("forget"), {{"networkId", id}}); }
QString IWD::setAutoConnectNetwork(const QString &id, bool value) { return d->request(QStringLiteral("autoconnect"), {{"networkId", id}, {"enabled", value}}); }
void IWD::preparePassword(const QString &id, const QString &password) { d->passwords[selectedDevice() + '\n' + id] = password; }
void IWD::provideCredentials(const QString &id, const QString &password) { d->send(QStringLiteral("ProvideCredentials"), {d->clientId, id, password}); }
void IWD::cancelOperation(const QString &id)
{
    const auto it = std::find_if(d->pending.cbegin(), d->pending.cend(), [&](const auto &request) { return request.id == id; });
    if (it == d->pending.cend()) return;
    if (it->sent) d->send(QStringLiteral("Cancel"), {d->clientId, id});
    else d->complete(id, false, QStringLiteral("com.shapertools.qdbussys.iwd.Canceled"), QStringLiteral("Operation canceled"));
}

QStringList IWD::adapters() const {
    return d->cachedAdapters;
}

QStringList IWD::devices() const {
    return d->cachedDevices;
}

QStringList IWD::supportedModes() const {
    return d->cachedSupportedModes;
}

QStringList IWD::possibleStates() const {
    return d->cachedPossibleStates;
}

int IWD::signalStrengthLevel() const
{
    return d->cachedSignalStrengthLevel;
}

bool IWD::connected() const {
    return d->cachedConnected;
}

bool IWD::scanning() const {
    return d->cachedScanning;
}

bool IWD::powered() const {
    return d->cachedPowered;
}

QString IWD::state() const {
    return d->cachedState;
}

QString IWD::model() const {
    return d->cachedModel;
}

QString IWD::name() const {
    return d->cachedName;
}

QString IWD::vendor() const {
    return d->cachedVendor;
}

QVariantMap IWD::connectedNetworkData() const {
    return d->cachedConnectedNetworkData;
}

void IWD::applyAdapterSnapshot(const QStringList &adapters,
                                    const QStringList &devices,
                                    const QStringList &supportedModes,
                                    const QString &model,
                                    const QString &name,
                                    const QString &vendor,
                                    bool powered)
{
    const bool adaptersChangedValue = d->cachedAdapters != adapters
        || d->cachedSupportedModes != supportedModes
        || d->cachedModel != model
        || d->cachedName != name
        || d->cachedVendor != vendor;
    const bool devicesChangedValue = d->cachedDevices != devices;
    const bool poweredChangedValue = d->cachedPowered != powered;

    d->cachedAdapters = adapters;
    d->cachedDevices = devices;
    d->cachedSupportedModes = supportedModes;
    d->cachedModel = model;
    d->cachedName = name;
    d->cachedVendor = vendor;
    d->cachedPowered = powered;

    if (adaptersChangedValue) {
        emit adaptersChanged();
        emit supportedModesChanged();
    }
    if (devicesChangedValue) {
        emit devicesChanged();
    }
    if (poweredChangedValue) {
        emit poweredChanged();
    }
}

void IWD::applyDeviceSnapshot(const QString &state,
                                   bool scanning,
                                   bool connected,
                                   const QVariantMap &connectedNetworkData,
                                   int signalStrengthLevel)
{
    const bool didStateChange = d->cachedState != state;
    const bool scanningChangedValue = d->cachedScanning != scanning;
    const bool connectedChangedValue = d->cachedConnected != connected
        || d->cachedConnectedNetworkData != connectedNetworkData;
    const bool signalStrengthLevelChangedValue = d->cachedSignalStrengthLevel != signalStrengthLevel;

    d->cachedState = state;
    d->cachedScanning = scanning;
    d->cachedConnected = connected;
    d->cachedConnectedNetworkData = connectedNetworkData;
    d->cachedSignalStrengthLevel = signalStrengthLevel;

    if (didStateChange) {
        emit stateChanged();
    }
    if (scanningChangedValue) {
        emit scanningChanged();
    }
    if (connectedChangedValue) {
        emit connectedChanged();
    }
    if (signalStrengthLevelChangedValue) {
        emit signalStrengthLevelChanged();
    }
}


} // namespace QDBusSys

#include "iwd.moc"
