// Copyright (C) 2026 Shaper Tools
// SPDX-License-Identifier: LGPL-2.1-only

#include "qsystemd-networkd.h"

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusServiceWatcher>
#include <QDBusVariant>
#include <QDebug>
#include <QHash>
#include <QSocketNotifier>
#include <QTimer>
#include <QVariantMap>

#include <cerrno>
#include <utility>
#include <cstring>
#include <net/if.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <sys/socket.h>
#include <unistd.h>

namespace QDBusSys {

namespace {

const auto network1Service = QStringLiteral("org.freedesktop.network1");
const auto network1Path = QStringLiteral("/org/freedesktop/network1");
const auto network1ManagerInterface = QStringLiteral("org.freedesktop.network1.Manager");

QString stringProperty(const QVariantMap &properties, const QString &propertyName)
{
    QVariant value = properties.value(propertyName);
    if (value.canConvert<QDBusVariant>()) {
        value = qvariant_cast<QDBusVariant>(value).variant();
    }

    return value.toString();
}

bool updateStringProperty(const QVariantMap &properties, const QString &propertyName, QString *target)
{
    if (!properties.contains(propertyName)) {
        return false;
    }

    const QString newValue = stringProperty(properties, propertyName);
    const bool changed = *target != newValue;
    *target = newValue;
    return changed;
}

bool invalidatesStateProperty(const QStringList &invalidatedProperties)
{
    return invalidatedProperties.contains(QStringLiteral("OperationalState")) ||
           invalidatedProperties.contains(QStringLiteral("CarrierState")) ||
           invalidatedProperties.contains(QStringLiteral("AddressState")) ||
           invalidatedProperties.contains(QStringLiteral("OnlineState"));
}

QString interfaceNameFromLinkMessage(nlmsghdr *header)
{
    if (header->nlmsg_len < NLMSG_LENGTH(sizeof(ifinfomsg))) {
        return QString();
    }

    auto *interfaceInfo = reinterpret_cast<ifinfomsg *>(NLMSG_DATA(header));
    int attributeLength = IFLA_PAYLOAD(header);
    for (rtattr *attribute = IFLA_RTA(interfaceInfo);
         RTA_OK(attribute, attributeLength);
         attribute = RTA_NEXT(attribute, attributeLength)) {
        if (attribute->rta_type == IFLA_IFNAME) {
            return QString::fromUtf8(static_cast<const char *>(RTA_DATA(attribute)));
        }
    }

    return QString();
}

SystemdNetworkd::OperationalState operationalStateFromString(const QString &value)
{
    using State = SystemdNetworkd::OperationalState;
    static const QHash<QString, State> states = {
        {QStringLiteral("missing"), State::Missing},
        {QStringLiteral("off"), State::Off},
        {QStringLiteral("failed"), State::Failed},
        {QStringLiteral("no-carrier"), State::NoCarrier},
        {QStringLiteral("dormant"), State::Dormant},
        {QStringLiteral("degraded-carrier"), State::DegradedCarrier},
        {QStringLiteral("carrier"), State::Carrier},
        {QStringLiteral("enslaved"), State::Enslaved},
        {QStringLiteral("degraded"), State::Degraded},
        {QStringLiteral("routable"), State::Routable},
    };
    return states.value(value, State::Unknown);
}

SystemdNetworkd::CarrierState carrierStateFromString(const QString &value)
{
    using State = SystemdNetworkd::CarrierState;
    static const QHash<QString, State> states = {
        {QStringLiteral("off"), State::Off},
        {QStringLiteral("no-carrier"), State::NoCarrier},
        {QStringLiteral("dormant"), State::Dormant},
        {QStringLiteral("degraded-carrier"), State::DegradedCarrier},
        {QStringLiteral("carrier"), State::Carrier},
        {QStringLiteral("enslaved"), State::Enslaved},
    };
    return states.value(value, State::Unknown);
}

SystemdNetworkd::AddressState addressStateFromString(const QString &value)
{
    using State = SystemdNetworkd::AddressState;
    static const QHash<QString, State> states = {
        {QStringLiteral("off"), State::Off},
        {QStringLiteral("degraded"), State::Degraded},
        {QStringLiteral("routable"), State::Routable},
    };
    return states.value(value, State::Unknown);
}

SystemdNetworkd::OnlineState onlineStateFromString(const QString &value)
{
    using State = SystemdNetworkd::OnlineState;
    static const QHash<QString, State> states = {
        {QStringLiteral("unknown"), State::Unknown},
        {QStringLiteral("offline"), State::Offline},
        {QStringLiteral("partial"), State::Partial},
        {QStringLiteral("online"), State::Online},
    };
    return states.value(value, State::Unknown);
}

} // namespace

class SystemdNetworkd::SystemdNetworkdPrivate : public QObject {
    Q_OBJECT

public:
    SystemdNetworkdPrivate(SystemdNetworkd *parent);
    ~SystemdNetworkdPrivate() override;

public slots:
    void onManagerPropertiesChanged(const QString &interface_name, const QVariantMap &changed_properties, const QStringList &invalidated_properties);
    void onNetlinkActivated(QSocketDescriptor socket, QSocketNotifier::Type activationEvent);
    void onServiceRegistered();
    void onServiceUnregistered();
    void refreshAll();
    // Reads a running networkd's state synchronously so it is valid as soon
    // as the object exists; later updates are asynchronous.
    void loadInitialState();

private:
    // networkd's Manager already aggregates its links according to
    // RequiredForOnline and related settings, so its values are published as-is.
    // All calls are asynchronous and never auto-start networkd.
    QDBusMessage networkdCall(const QString &interface, const QString &method) const;
    void fetchManagerState();
    void fetchLinks();
    void applyLinkList(const QDBusMessage &reply);
    void reconcileWithKernel();
    void handleCallError(const QDBusError &error, const char *what);
    void publishState();
    void setLinkInterfaces(const QStringList &interfaces);
    void clearState();
    bool setupLinkMonitor();
    void startPollingFallback();

public:
    static constexpr int linkRefreshIntervalMs = 2000;
    static constexpr int maximumReconcileAttempts = 10;

    QDBusConnection bus = QDBusConnection::systemBus();
    QDBusServiceWatcher *serviceWatcher = nullptr;
    QSocketNotifier *linkMonitorNotifier = nullptr;
    QTimer *linkRefreshTimer = nullptr;
    QTimer *linkReconcileTimer = nullptr;
    int linkReconcileIntervalMs = 50;
    static constexpr int initialStateTimeoutMs = 2000;
    int reconcileAttempts = 0;
    int linkMonitorSocket = -1;
    quint64 generation = 0;
    bool managerFetchPending = false;
    bool managerRefetchPending = false;
    bool linksFetchPending = false;
    bool linksRefetchPending = false;

    bool verboseLogging = false;
    SystemdNetworkd::OperationalState operationalState = SystemdNetworkd::OperationalState::Unknown;
    SystemdNetworkd::CarrierState carrierState = SystemdNetworkd::CarrierState::Unknown;
    SystemdNetworkd::AddressState addressState = SystemdNetworkd::AddressState::Unknown;
    SystemdNetworkd::OnlineState onlineState = SystemdNetworkd::OnlineState::Unknown;
    QStringList linkInterfaces;

    QString managerOperationalState;
    QString managerCarrierState;
    QString managerAddressState;
    QString managerOnlineState;

    SystemdNetworkd *systemdNetworkd;
};

SystemdNetworkd::SystemdNetworkdPrivate::SystemdNetworkdPrivate(SystemdNetworkd *parent)
    : QObject(parent),
      systemdNetworkd(parent)
{
    linkReconcileTimer = new QTimer(this);
    linkReconcileTimer->setSingleShot(true);
    connect(linkReconcileTimer, &QTimer::timeout, this, &SystemdNetworkdPrivate::fetchLinks);

    if (!bus.isConnected()) {
        qWarning() << "SystemdNetworkd: Cannot connect to the D-Bus system bus:" << bus.lastError().message();
        return;
    }

    serviceWatcher = new QDBusServiceWatcher(network1Service, bus,
                                             QDBusServiceWatcher::WatchForRegistration
                                                 | QDBusServiceWatcher::WatchForUnregistration,
                                             this);
    connect(serviceWatcher, &QDBusServiceWatcher::serviceRegistered, this, &SystemdNetworkdPrivate::onServiceRegistered);
    connect(serviceWatcher, &QDBusServiceWatcher::serviceUnregistered, this, &SystemdNetworkdPrivate::onServiceUnregistered);
    bus.connect(network1Service, network1Path, QStringLiteral("org.freedesktop.DBus.Properties"),
                QStringLiteral("PropertiesChanged"), this,
                SLOT(onManagerPropertiesChanged(QString,QVariantMap,QStringList)));

    if (!setupLinkMonitor()) {
        startPollingFallback();
    }

    loadInitialState();
}

void SystemdNetworkd::SystemdNetworkdPrivate::loadInitialState()
{
    auto managerMessage = networkdCall(QStringLiteral("org.freedesktop.DBus.Properties"), QStringLiteral("GetAll"));
    managerMessage << network1ManagerInterface;
    const QDBusMessage managerReply = bus.call(managerMessage, QDBus::Block, initialStateTimeoutMs);
    if (managerReply.type() != QDBusMessage::ReplyMessage || managerReply.arguments().isEmpty()) {
        if (verboseLogging) {
            qDebug() << "SystemdNetworkd: networkd not available at startup:" << managerReply.errorMessage();
        }
        // The asynchronous path reports errors and retries a slow networkd.
        refreshAll();
        return;
    }
    onManagerPropertiesChanged(network1ManagerInterface, qdbus_cast<QVariantMap>(managerReply.arguments().constFirst()), {});

    const QDBusMessage linksReply = bus.call(networkdCall(network1ManagerInterface, QStringLiteral("ListLinks")),
                                             QDBus::Block, initialStateTimeoutMs);
    if (linksReply.type() == QDBusMessage::ReplyMessage) {
        applyLinkList(linksReply);
    } else {
        fetchLinks();
    }
}

SystemdNetworkd::SystemdNetworkdPrivate::~SystemdNetworkdPrivate()
{
    if (linkMonitorSocket >= 0) {
        ::close(linkMonitorSocket);
    }
}

bool SystemdNetworkd::SystemdNetworkdPrivate::setupLinkMonitor()
{
    linkMonitorSocket = ::socket(AF_NETLINK, SOCK_RAW | SOCK_CLOEXEC | SOCK_NONBLOCK, NETLINK_ROUTE);
    if (linkMonitorSocket < 0) {
        qWarning() << "SystemdNetworkd: Failed to create netlink route socket:" << std::strerror(errno);
        return false;
    }

    sockaddr_nl address = {};
    address.nl_family = AF_NETLINK;
    address.nl_groups = RTMGRP_LINK;

    if (::bind(linkMonitorSocket, reinterpret_cast<sockaddr *>(&address), sizeof(address)) < 0) {
        qWarning() << "SystemdNetworkd: Failed to bind netlink route socket:" << std::strerror(errno);
        ::close(linkMonitorSocket);
        linkMonitorSocket = -1;
        return false;
    }

    linkMonitorNotifier = new QSocketNotifier(linkMonitorSocket, QSocketNotifier::Read, this);
    connect(linkMonitorNotifier, &QSocketNotifier::activated,
            this, &SystemdNetworkdPrivate::onNetlinkActivated);

    if (verboseLogging) {
        qDebug() << "SystemdNetworkd: Watching link changes via rtnetlink";
    }

    return true;
}

void SystemdNetworkd::SystemdNetworkdPrivate::startPollingFallback()
{
    if (linkRefreshTimer) {
        return;
    }

    linkRefreshTimer = new QTimer(this);
    linkRefreshTimer->setInterval(linkRefreshIntervalMs);
    connect(linkRefreshTimer, &QTimer::timeout, this, &SystemdNetworkdPrivate::fetchLinks);
    linkRefreshTimer->start();

    qWarning() << "SystemdNetworkd: Falling back to polling for link discovery";
}

QDBusMessage SystemdNetworkd::SystemdNetworkdPrivate::networkdCall(const QString &interface, const QString &method) const
{
    auto message = QDBusMessage::createMethodCall(network1Service, network1Path, interface, method);
    // Observing networkd must never be the reason it starts.
    message.setAutoStartService(false);
    return message;
}

void SystemdNetworkd::SystemdNetworkdPrivate::refreshAll()
{
    fetchManagerState();
    fetchLinks();
}

void SystemdNetworkd::SystemdNetworkdPrivate::handleCallError(const QDBusError &error, const char *what)
{
    if (error.type() == QDBusError::ServiceUnknown || error.name() == QLatin1String("org.freedesktop.DBus.Error.NameHasNoOwner")) {
        // networkd is not running; the service watcher reports its return.
        clearState();
        return;
    }
    qWarning() << "SystemdNetworkd:" << what << "failed:" << error.message();
}

void SystemdNetworkd::SystemdNetworkdPrivate::fetchManagerState()
{
    if (!bus.isConnected()) {
        return;
    }
    if (managerFetchPending) {
        managerRefetchPending = true;
        return;
    }
    managerFetchPending = true;
    managerRefetchPending = false;
    auto message = networkdCall(QStringLiteral("org.freedesktop.DBus.Properties"), QStringLiteral("GetAll"));
    message << network1ManagerInterface;
    const quint64 requestGeneration = generation;
    auto *watcher = new QDBusPendingCallWatcher(bus.asyncCall(message), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, requestGeneration](QDBusPendingCallWatcher *call) {
        QDBusPendingReply<QVariantMap> reply = *call;
        call->deleteLater();
        if (requestGeneration != generation) {
            return;
        }
        managerFetchPending = false;
        if (reply.isError()) {
            handleCallError(reply.error(), "Manager GetAll");
        } else {
            onManagerPropertiesChanged(network1ManagerInterface, reply.value(), {});
        }
        if (std::exchange(managerRefetchPending, false)) {
            fetchManagerState();
        }
    });
}

void SystemdNetworkd::SystemdNetworkdPrivate::fetchLinks()
{
    if (!bus.isConnected()) {
        return;
    }
    if (linksFetchPending) {
        linksRefetchPending = true;
        return;
    }
    linksFetchPending = true;
    linksRefetchPending = false;
    const quint64 requestGeneration = generation;
    auto *watcher = new QDBusPendingCallWatcher(bus.asyncCall(networkdCall(network1ManagerInterface, QStringLiteral("ListLinks"))), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, requestGeneration](QDBusPendingCallWatcher *call) {
        const QDBusMessage reply = call->reply();
        call->deleteLater();
        if (requestGeneration != generation) {
            return;
        }
        linksFetchPending = false;
        if (reply.type() == QDBusMessage::ErrorMessage) {
            handleCallError(QDBusError(reply), "ListLinks");
        } else {
            applyLinkList(reply);
        }
        if (std::exchange(linksRefetchPending, false)) {
            fetchLinks();
        }
    });
}

void SystemdNetworkd::SystemdNetworkdPrivate::applyLinkList(const QDBusMessage &reply)
{
    if (reply.arguments().isEmpty() || !reply.arguments().at(0).canConvert<QDBusArgument>()) {
        qWarning() << "SystemdNetworkd: ListLinks returned an unexpected reply";
        return;
    }

    QStringList discovered;
    const QDBusArgument linkListArgument = qvariant_cast<QDBusArgument>(reply.arguments().at(0));
    linkListArgument.beginArray();
    while (!linkListArgument.atEnd()) {
        int linkIndex = -1;
        QString linkName;
        QDBusObjectPath linkPath;

        linkListArgument.beginStructure();
        linkListArgument >> linkIndex >> linkName >> linkPath;
        linkListArgument.endStructure();

        // rtnetlink can arrive before networkd updates its own object list.
        // Do not retain a deleted link or an older link with a reused name.
        if (!linkPath.path().isEmpty()
            && ::if_nametoindex(linkName.toUtf8().constData()) == static_cast<unsigned int>(linkIndex)) {
            discovered << linkName;
        }
    }
    linkListArgument.endArray();

    discovered.sort();
    setLinkInterfaces(discovered);
    reconcileWithKernel();
}

void SystemdNetworkd::SystemdNetworkdPrivate::reconcileWithKernel()
{
    if (linkMonitorSocket < 0) {
        return;
    }
    auto *interfaces = ::if_nameindex();
    if (!interfaces) {
        return;
    }
    QStringList kernelInterfaces;
    for (auto *entry = interfaces; entry->if_index != 0; ++entry) {
        kernelInterfaces << QString::fromUtf8(entry->if_name);
    }
    ::if_freenameindex(interfaces);
    kernelInterfaces.sort();

    if (kernelInterfaces == linkInterfaces) {
        linkReconcileTimer->stop();
        linkReconcileIntervalMs = 50;
        reconcileAttempts = 0;
        return;
    }
    // Retry briefly while networkd catches up with the kernel. Some links may
    // legitimately never appear, so give up until the next netlink event.
    if (reconcileAttempts >= maximumReconcileAttempts) {
        return;
    }
    ++reconcileAttempts;
    linkReconcileTimer->start(linkReconcileIntervalMs);
    linkReconcileIntervalMs = qMin(linkReconcileIntervalMs * 2, linkRefreshIntervalMs);
}

void SystemdNetworkd::SystemdNetworkdPrivate::onNetlinkActivated(QSocketDescriptor, QSocketNotifier::Type)
{
    bool sawDiscoveryChange = false;
    bool sawOverflow = false;
    char buffer[8192];

    for (;;) {
        const ssize_t bytesRead = ::recv(linkMonitorSocket, buffer, sizeof(buffer), MSG_DONTWAIT);
        if (bytesRead < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;
            }

            if (errno == ENOBUFS) {
                sawOverflow = true;
                break;
            }

            qWarning() << "SystemdNetworkd: Failed reading rtnetlink events:" << std::strerror(errno);
            return;
        }

        if (bytesRead == 0) {
            break;
        }

        int remaining = static_cast<int>(bytesRead);
        for (nlmsghdr *header = reinterpret_cast<nlmsghdr *>(buffer);
             NLMSG_OK(header, remaining);
             header = NLMSG_NEXT(header, remaining)) {
            switch (header->nlmsg_type) {
            case RTM_NEWLINK: {
                // State changes of known links arrive through networkd itself.
                const QString interfaceName = interfaceNameFromLinkMessage(header);
                if (interfaceName.isEmpty() || !linkInterfaces.contains(interfaceName)) {
                    sawDiscoveryChange = true;
                }
                break;
            }
            case RTM_DELLINK:
                sawDiscoveryChange = true;
                break;
            case NLMSG_ERROR:
                sawOverflow = true;
                break;
            default:
                break;
            }
        }
    }

    if (!sawDiscoveryChange && !sawOverflow) {
        return;
    }

    if (verboseLogging) {
        qDebug() << "SystemdNetworkd: Netlink reported link discovery changes";
    }

    linkReconcileIntervalMs = 50;
    reconcileAttempts = 0;
    fetchLinks();
}

void SystemdNetworkd::SystemdNetworkdPrivate::onServiceRegistered()
{
    if (verboseLogging) {
        qDebug() << "SystemdNetworkd: networkd appeared on the bus";
    }
    reconcileAttempts = 0;
    linkReconcileIntervalMs = 50;
    refreshAll();
}

void SystemdNetworkd::SystemdNetworkdPrivate::onServiceUnregistered()
{
    if (verboseLogging) {
        qDebug() << "SystemdNetworkd: networkd left the bus";
    }
    clearState();
}

void SystemdNetworkd::SystemdNetworkdPrivate::clearState()
{
    // Replies requested from a vanished networkd are stale.
    ++generation;
    managerFetchPending = managerRefetchPending = false;
    linksFetchPending = linksRefetchPending = false;
    linkReconcileTimer->stop();
    managerOperationalState.clear();
    managerCarrierState.clear();
    managerAddressState.clear();
    managerOnlineState.clear();
    publishState();
    setLinkInterfaces({});
}

void SystemdNetworkd::SystemdNetworkdPrivate::setLinkInterfaces(const QStringList &interfaces)
{
    if (linkInterfaces != interfaces) {
        linkInterfaces = interfaces;
        emit systemdNetworkd->linkInterfacesChanged();
    }
}

void SystemdNetworkd::SystemdNetworkdPrivate::publishState()
{
    const auto newOperationalState = operationalStateFromString(managerOperationalState);
    const auto newCarrierState = carrierStateFromString(managerCarrierState);
    const auto newAddressState = addressStateFromString(managerAddressState);
    const auto newOnlineState = onlineStateFromString(managerOnlineState);

    if (operationalState != newOperationalState) {
        operationalState = newOperationalState;
        emit systemdNetworkd->operationalStateChanged();
    }

    if (carrierState != newCarrierState) {
        carrierState = newCarrierState;
        emit systemdNetworkd->carrierStateChanged();
    }

    if (addressState != newAddressState) {
        addressState = newAddressState;
        emit systemdNetworkd->addressStateChanged();
    }

    if (onlineState != newOnlineState) {
        onlineState = newOnlineState;
        emit systemdNetworkd->onlineStateChanged();
    }
}

void SystemdNetworkd::SystemdNetworkdPrivate::onManagerPropertiesChanged(const QString &interface_name, const QVariantMap &changed_properties, const QStringList &invalidated_properties)
{
    if (verboseLogging) {
        qDebug() << "SystemdNetworkd: Manager PropertiesChanged signal received:"
                 << interface_name << changed_properties << invalidated_properties;
    }

    if (interface_name != network1ManagerInterface) {
        return;
    }

    if (invalidatesStateProperty(invalidated_properties)) {
        fetchManagerState();
    }

    updateStringProperty(changed_properties, QStringLiteral("OperationalState"), &managerOperationalState);
    updateStringProperty(changed_properties, QStringLiteral("CarrierState"), &managerCarrierState);
    updateStringProperty(changed_properties, QStringLiteral("AddressState"), &managerAddressState);
    updateStringProperty(changed_properties, QStringLiteral("OnlineState"), &managerOnlineState);
    publishState();
}

SystemdNetworkd::SystemdNetworkd(QObject *parent)
    : QObject(parent),
      d(new SystemdNetworkdPrivate(this)) { /**/ }

SystemdNetworkd::~SystemdNetworkd() = default;

bool SystemdNetworkd::verboseLogging() const { return d->verboseLogging; }

void SystemdNetworkd::setVerboseLogging(bool enabled)
{
    if (d->verboseLogging == enabled) return;
    d->verboseLogging = enabled;
    emit verboseLoggingChanged();
}

SystemdNetworkd::OperationalState SystemdNetworkd::operationalState() const { return d->operationalState; }
SystemdNetworkd::CarrierState SystemdNetworkd::carrierState() const { return d->carrierState; }
SystemdNetworkd::AddressState SystemdNetworkd::addressState() const { return d->addressState; }
SystemdNetworkd::OnlineState SystemdNetworkd::onlineState() const { return d->onlineState; }
QStringList SystemdNetworkd::linkInterfaces() const { return d->linkInterfaces; }

} // namespace QDBusSys

#include "qsystemd-networkd.moc"
