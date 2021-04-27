// Copyright (C) 2026 Shaper Tools
// SPDX-License-Identifier: LGPL-2.1-only

#include "qdbus-sys-connectivityd.h"

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusServiceWatcher>
#include <QDBusVariant>
#include <QDebug>

namespace QDBusSys {

namespace
{

QDBusConnection qdbusSysConnectivityDBus()
{
    return QDBusConnection::systemBus();
}

QVariant unwrap(const QVariant &value)
{
    return value.metaType() == QMetaType::fromType<QDBusVariant>() ? qvariant_cast<QDBusVariant>(value).variant() : value;
}

}

class Connectivity::ConnectivityPrivate : public QObject {
    Q_OBJECT

public:
    ConnectivityPrivate(Connectivity *parent);

public slots:
    void onServiceRegistered(const QString &serviceName);
    void onServiceUnregistered(const QString &serviceName);
    void onPropertiesChanged(const QString &interface, const QVariantMap &changed, const QStringList &invalidated);
    void refreshState();
    void checkConnectivity();

private:
    // Reads a running daemon's state synchronously so it is valid as soon as
    // the object exists; returns false when the daemon is not on the bus.
    bool loadInitialState();
    // A GetAll with auto-start doubles as D-Bus activation and never blocks.
    void fetchState(bool activate);
    void applyReply(const QDBusMessage &reply);
    void applyProperties(const QVariantMap &properties);
    void sendPendingCheck();
    void resetState();

public:
    const char *serviceName = "com.shapertools.qdbussys.connectivityd";
    const char *objectPath = "/com/shapertools/qdbussys/connectivityd";
    const char *interfaceName = "com.shapertools.qdbussys.connectivityd1";
    static constexpr int initialStateTimeoutMs = 2000;

    QDBusConnection bus;
    QDBusServiceWatcher *serviceWatcher = nullptr;
    bool verboseLogging = false;
    bool daemonReady = false;
    bool fetchPending = false;
    bool refetchPending = false;
    bool checkPending = false;
    quint64 generation = 0;

    bool connected = false;
    bool checkingConnectivity = false;
    Connectivity::Level connectivityLevel = Connectivity::InterfaceDisconnected;

    Connectivity *qdbusSysConnectivityd;
};

Connectivity::ConnectivityPrivate::ConnectivityPrivate(Connectivity *parent)
    : QObject(parent),
      bus(qdbusSysConnectivityDBus()),
      qdbusSysConnectivityd(parent)
{
    if (!bus.isConnected()) {
        qWarning() << "Connectivity: Cannot connect to the D-Bus system bus:"
                   << bus.lastError().message();
        return;
    }

    const QString service = QString::fromLatin1(serviceName);
    const QString path = QString::fromLatin1(objectPath);
    serviceWatcher = new QDBusServiceWatcher(service, bus,
                                             QDBusServiceWatcher::WatchForRegistration
                                                 | QDBusServiceWatcher::WatchForUnregistration,
                                             this);
    connect(serviceWatcher, &QDBusServiceWatcher::serviceRegistered,
            this, &ConnectivityPrivate::onServiceRegistered);
    connect(serviceWatcher, &QDBusServiceWatcher::serviceUnregistered,
            this, &ConnectivityPrivate::onServiceUnregistered);

    bus.connect(service, path, QStringLiteral("org.freedesktop.DBus.Properties"), QStringLiteral("PropertiesChanged"),
                this, SLOT(onPropertiesChanged(QString,QVariantMap,QStringList)));

    if (!loadInitialState()) {
        fetchState(true);
    }
}

bool Connectivity::ConnectivityPrivate::loadInitialState()
{
    auto message = QDBusMessage::createMethodCall(QString::fromLatin1(serviceName), QString::fromLatin1(objectPath),
                                                  QStringLiteral("org.freedesktop.DBus.Properties"), QStringLiteral("GetAll"));
    message << QString::fromLatin1(interfaceName);
    message.setAutoStartService(false);
    const QDBusMessage reply = bus.call(message, QDBus::Block, initialStateTimeoutMs);
    if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty()) {
        if (verboseLogging) {
            qDebug() << "Connectivity: Daemon not available at startup:" << reply.errorMessage();
        }
        return false;
    }
    applyReply(reply);
    return true;
}

void Connectivity::ConnectivityPrivate::applyReply(const QDBusMessage &reply)
{
    daemonReady = true;
    applyProperties(qdbus_cast<QVariantMap>(reply.arguments().constFirst()));
    if (verboseLogging) {
        qDebug() << "Connectivity: Connected to daemon over D-Bus";
    }
}

void Connectivity::ConnectivityPrivate::fetchState(bool activate)
{
    if (!bus.isConnected()) {
        return;
    }
    if (fetchPending) {
        refetchPending = true;
        return;
    }
    fetchPending = true;
    refetchPending = false;
    auto message = QDBusMessage::createMethodCall(QString::fromLatin1(serviceName), QString::fromLatin1(objectPath),
                                                  QStringLiteral("org.freedesktop.DBus.Properties"), QStringLiteral("GetAll"));
    message << QString::fromLatin1(interfaceName);
    message.setAutoStartService(activate);
    const quint64 requestGeneration = generation;
    auto *watcher = new QDBusPendingCallWatcher(bus.asyncCall(message), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, activate, requestGeneration](QDBusPendingCallWatcher *call) {
        QDBusPendingReply<QVariantMap> reply = *call;
        call->deleteLater();
        if (requestGeneration != generation) {
            return;
        }
        fetchPending = false;
        if (reply.isError()) {
            if (activate) {
                qWarning() << "Connectivity: Could not activate daemon:" << reply.error().message();
            } else if (verboseLogging) {
                qWarning() << "Connectivity: Could not read daemon state:" << reply.error().message();
            }
            daemonReady = false;
            checkPending = false;
            resetState();
            return;
        }
        applyReply(reply.reply());
        sendPendingCheck();
        if (refetchPending) {
            fetchState(false);
        }
    });
}

void Connectivity::ConnectivityPrivate::applyProperties(const QVariantMap &properties)
{
    if (properties.contains(QStringLiteral("connected"))) {
        const bool value = unwrap(properties.value(QStringLiteral("connected"))).toBool();
        if (connected != value) {
            connected = value;
            emit qdbusSysConnectivityd->connectedChanged();
        }
    }
    if (properties.contains(QStringLiteral("checkingConnectivity"))) {
        const bool value = unwrap(properties.value(QStringLiteral("checkingConnectivity"))).toBool();
        if (checkingConnectivity != value) {
            checkingConnectivity = value;
            emit qdbusSysConnectivityd->checkingConnectivityChanged();
        }
    }
    if (properties.contains(QStringLiteral("connectivityLevel"))) {
        const auto value = static_cast<Connectivity::Level>(unwrap(properties.value(QStringLiteral("connectivityLevel"))).toInt());
        if (connectivityLevel != value) {
            connectivityLevel = value;
            emit qdbusSysConnectivityd->connectivityLevelChanged();
        }
    }
}

void Connectivity::ConnectivityPrivate::sendPendingCheck()
{
    if (!checkPending || !daemonReady) {
        return;
    }
    checkPending = false;
    bus.asyncCall(QDBusMessage::createMethodCall(QString::fromLatin1(serviceName), QString::fromLatin1(objectPath),
                                                 QString::fromLatin1(interfaceName), QStringLiteral("checkConnectivity")));
}

void Connectivity::ConnectivityPrivate::onServiceRegistered(const QString &registeredServiceName)
{
    if (registeredServiceName != QLatin1String(serviceName)) {
        return;
    }

    if (verboseLogging) {
        qDebug() << "Connectivity: D-Bus service registered" << registeredServiceName;
    }

    fetchState(false);
}

void Connectivity::ConnectivityPrivate::onServiceUnregistered(const QString &unregisteredServiceName)
{
    if (unregisteredServiceName != QLatin1String(serviceName)) {
        return;
    }

    if (verboseLogging) {
        qDebug() << "Connectivity: D-Bus service unregistered" << unregisteredServiceName;
    }

    ++generation;
    daemonReady = false;
    fetchPending = false;
    refetchPending = false;
    resetState();
}

void Connectivity::ConnectivityPrivate::onPropertiesChanged(const QString &interface, const QVariantMap &changed,
                                                            const QStringList &invalidated)
{
    if (interface != QLatin1String(interfaceName)) {
        return;
    }
    applyProperties(changed);
    if (!invalidated.isEmpty()) {
        fetchState(false);
    }
}

void Connectivity::ConnectivityPrivate::resetState()
{
    applyProperties({{QStringLiteral("connected"), false},
                     {QStringLiteral("checkingConnectivity"), false},
                     {QStringLiteral("connectivityLevel"), int(Connectivity::InterfaceDisconnected)}});
}

void Connectivity::ConnectivityPrivate::refreshState()
{
    fetchState(false);
}

void Connectivity::ConnectivityPrivate::checkConnectivity()
{
    checkPending = true;
    if (daemonReady) {
        sendPendingCheck();
    } else {
        // Coalesced with any activation already in flight.
        fetchState(true);
    }
}

Connectivity::Connectivity(QObject *parent)
    : QObject(parent),
      d(new ConnectivityPrivate(this)) { /**/ }

Connectivity::~Connectivity() = default;

void Connectivity::checkConnectivity()
{
    d->checkConnectivity();
}

bool Connectivity::connected() const { return d->connected; }
bool Connectivity::checkingConnectivity() const { return d->checkingConnectivity; }
Connectivity::Level Connectivity::connectivityLevel() const { return d->connectivityLevel; }

} // namespace QDBusSys

#include "qdbus-sys-connectivityd.moc"
