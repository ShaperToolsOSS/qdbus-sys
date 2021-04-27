// Copyright (C) 2026 Shaper Tools
// SPDX-License-Identifier: LGPL-2.1-only

#include <QDBusConnection>
#include <QDBusError>
#include <QDBusMessage>

#include <QNetworkAccessManager>
#include <QTimer>
#include <QNetworkReply>
#include <QHostInfo>
#include <QDebug>
#include <QSettings>
#include <QFile>
#include <QFileInfo>
#include <QDir>

#include "qdbus-sys-connectivityd-daemon.h"
#include "qsystemd-networkd.h"

using QDBusSys::SystemdNetworkd;
using QDBusSys::Connectivity;

namespace {
const auto connectivityObjectPath = QStringLiteral("/com/shapertools/qdbussys/connectivityd");
const auto connectivityInterface = QStringLiteral("com.shapertools.qdbussys.connectivityd1");
}

void ConnectivityAdaptor::publishProperties(const QVariantMap &changed)
{
    // PropertiesChanged carries the values, so clients need no follow-up Get.
    if (changed.isEmpty() || !qdbusSysConnectivityDaemon->publishedOnDBus) {
        return;
    }
    auto signal = QDBusMessage::createSignal(connectivityObjectPath, QStringLiteral("org.freedesktop.DBus.Properties"),
                                             QStringLiteral("PropertiesChanged"));
    signal << connectivityInterface << changed << QStringList();
    QDBusConnection::systemBus().send(signal);
}

int ConnectivityAdaptor::connectivityLevel() const
{
    return static_cast<int>(mConnectivityLevel);
}

void ConnectivityAdaptor::setCheckingConnectivity(bool checking)
{
    if (checkingConnectivity != checking) {
      if (qdbusSysConnectivityDaemon->verboseLogging) {
        qDebug() << "advertize checking connectivity" << checking;
      }

      checkingConnectivity = checking;
      publishProperties({{QStringLiteral("checkingConnectivity"), checking}});
      emit qdbusSysConnectivityDaemon->checkingConnectivityChanged();
    }
}

void ConnectivityAdaptor::checkConnectivity()
{
    if (qdbusSysConnectivityDaemon->verboseLogging) {
      qDebug() << "External call to check connectivity";
    }
    qdbusSysConnectivityDaemon->requestExternalCheck();
}

void ConnectivityAdaptor::setConnectivityLevel(ConnectivityLevel connectivityLevel) {
    const QString &connectedStateOSPath = qdbusSysConnectivityDaemon->connectedStateFilePath;
    const ConnectivityLevel existingLevel = mConnectivityLevel;
    const bool existingConnected = connected;

    mConnectivityLevel = connectivityLevel;
    connected = connectivityLevel == Connectivity::EndpointAccessible;

    QVariantMap changed;
    if (existingLevel != mConnectivityLevel) {
        changed.insert(QStringLiteral("connectivityLevel"), static_cast<int>(mConnectivityLevel));
    }
    if (existingConnected != connected) {
        changed.insert(QStringLiteral("connected"), connected);
    }
    publishProperties(changed);

    if (existingLevel != mConnectivityLevel) {
        emit qdbusSysConnectivityDaemon->connectivityLevelChanged();
    }

    if (existingConnected != connected) {
        emit qdbusSysConnectivityDaemon->connectedChanged();
    }

    // Reconcile the file even when the in-memory state has not changed: a
    // previous daemon instance may have left it behind after a crash.
    QFile connectedStateOSFile(connectedStateOSPath);
    if (connected) {
        QDir().mkpath(QFileInfo(connectedStateOSPath).absolutePath());
        if (connectedStateOSFile.open(QIODevice::WriteOnly)) {
            connectedStateOSFile.close();
        } else if (qdbusSysConnectivityDaemon->verboseLogging) {
            qWarning() << "Failed to create connected-state file" << connectedStateOSPath;
        }
    } else {
        connectedStateOSFile.remove();
    }

    if (qdbusSysConnectivityDaemon->verboseLogging) {
      switch (connectivityLevel) {
      case Connectivity::EndpointAccessible:
        qDebug() << "Connectivity checks passed.";
        break;
      case Connectivity::InternetRoutable:
        qDebug() << "We are connected to a network and DNS resolution works, "
                    "but we can't reach our health check endpoints";
        break;
      case Connectivity::InterfaceConnected:
        qDebug() << "We are connected to a network but DNS resolution is not "
                    "working";
        break;
      case Connectivity::InterfaceDisconnected:
        qDebug() << "We are not connected to a network";
        break;
      default:
        qDebug() << "Unknown connectivity level:"
                 << static_cast<int>(connectivityLevel);
      }
    }

    setCheckingConnectivity(false);
}

void QDBusSysConnectivityDaemon::setupDBus()
{
    // qdbus-sys-connectivityd is a daemon and always publishes on the system bus.
    QDBusConnection dbusBus = QDBusConnection::systemBus();

    if (!dbusBus.isConnected()) {
        qWarning() << "Cannot connect to the D-Bus system bus:" << dbusBus.lastError().message();
        return;
    }

    publishedOnDBus = true;
    if(dbusBus.registerObject(connectivityObjectPath, this)) {
        qDebug() << "Successfully registered D-Bus object";
    } else {
        qDebug() << "D-Bus object registration failed: " << dbusBus.lastError().message().toStdString();
    }

    if(dbusBus.registerService("com.shapertools.qdbussys.connectivityd")) {
        qDebug() << "Sucessfully registered D-Bus service";
    } else {
        qDebug() << "D-Bus service registration failed: " << dbusBus.lastError().message().toStdString();
    }
}

void QDBusSysConnectivityDaemon::setConnectivityLevel(ConnectivityLevel status)
{
    if (!qdbusSysConnectivitydAdaptor) {
        if (verboseLogging) {
            qWarning() << "QDBusSysConnectivityDaemon: D-Bus adaptor unavailable, keeping internal connectivity level only:"
                       << static_cast<int>(status);
        }
        return;
    }

    qdbusSysConnectivitydAdaptor->setConnectivityLevel(status);
    if (recheckIntervalMs > 0) {
        recheckTimeout->start(recheckIntervalMs);
    }
}

QDBusSysConnectivityDaemon::QDBusSysConnectivityDaemon()
    : QDBusSysConnectivityDaemon(new SystemdNetworkd, new QNetworkAccessManager,
                               QStringLiteral("/run/shaper-state/connectivity/connected"), true)
{
}

QDBusSysConnectivityDaemon::QDBusSysConnectivityDaemon(SystemdNetworkd *networkd,
                                                     QNetworkAccessManager *networkManager,
                                                     const QString &stateFilePath,
                                                     bool publishOnDBus,
                                                     const QString &settingsFilePath)
    : QObject(),
      wiggleTimeout(new QTimer(this)),
      retryTimeout(new QTimer(this)),
      recheckTimeout(new QTimer(this)),
      externalCheckTimeout(new QTimer(this)),
      connectedStateFilePath(stateFilePath),
      qdbusSysConnectivitydAdaptor(new ConnectivityAdaptor(this)),
      systemdNetworkd(networkd),
      networkAccessManager(networkManager)
{
    systemdNetworkd->setParent(this);
    networkAccessManager->setParent(this);
    setConnectivityLevel(Connectivity::InterfaceDisconnected);
    if (publishOnDBus) {
        setupDBus();
    }

    // this is literally settling time from network fluctuation, to deciding we are going nowhere and hence dead
    wiggleTimeout->setSingleShot(true);
    connect(wiggleTimeout, &QTimer::timeout, this, &QDBusSysConnectivityDaemon::handleWiggleTimeout);
    retryTimeout->setSingleShot(true);
    connect(retryTimeout, &QTimer::timeout, this, &QDBusSysConnectivityDaemon::checkEndpointConnectivity);
    recheckTimeout->setSingleShot(true);
    connect(recheckTimeout, &QTimer::timeout, this, &QDBusSysConnectivityDaemon::handleRecheckTimeout);
    externalCheckTimeout->setSingleShot(true);
    connect(externalCheckTimeout, &QTimer::timeout, this, [this] {
        lastExternalCheck.start();
        handleWiggleTimeout();
    });

    QFile settingsFile(settingsFilePath);
    if (!settingsFile.open(QIODevice::ReadOnly)) {
        qWarning() << "Cannot read connectivity configuration file" << settingsFilePath
                   << ":" << settingsFile.errorString()
                   << "- unconfigured probes will assume success";
    } else {
        QSettings settings(settingsFilePath, QSettings::IniFormat);
        verboseLogging = settings.value("verboseDebugging", false).toBool();
        endpointUrl = settings.value("Connectivity/endpointUrl").toString().trimmed();
        if (settings.contains("Connectivity/expectedStatusCode")) {
            bool validStatus = false;
            const int status = settings.value("Connectivity/expectedStatusCode").toInt(&validStatus);
            if (validStatus && status >= 100 && status <= 599) {
                expectedStatusCode = status;
            } else {
                qWarning() << "Ignoring invalid Connectivity/expectedStatusCode in" << settingsFilePath
                           << "- expected an integer from 100 to 599";
            }
        }
        checkExpectedResponse = settings.contains("Connectivity/expectedResponse");
        expectedResponse = settings.value("Connectivity/expectedResponse").toString().toUtf8();
        if (settings.contains("Connectivity/recheckIntervalSec")) {
            bool validInterval = false;
            const int seconds = settings.value("Connectivity/recheckIntervalSec").toInt(&validInterval);
            if (validInterval && seconds >= 0 && seconds <= 86400) {
                recheckIntervalMs = seconds * 1000;
            } else {
                qWarning() << "Ignoring invalid Connectivity/recheckIntervalSec in" << settingsFilePath
                           << "- expected an integer from 0 to 86400";
            }
        }
        const QStringList domains = settings.value("Connectivity/dnsTestDomains").toStringList();
        for (const QString &domain : domains) {
            const QString trimmedDomain = domain.trimmed();
            if (!trimmedDomain.isEmpty()) {
                dnsTestDomains.append(trimmedDomain);
            }
        }

        if (settings.status() != QSettings::NoError) {
            qWarning() << "Cannot parse or read connectivity configuration file" << settingsFilePath
                       << "- QSettings status:" << settings.status()
                       << "- unconfigured probes will assume success";
            endpointUrl.clear();
            expectedStatusCode = 0;
            checkExpectedResponse = false;
            expectedResponse.clear();
            dnsTestDomains.clear();
        }
    }

    const QUrl endpoint(endpointUrl);
    if (!endpointUrl.isEmpty()
        && (!endpoint.isValid() || endpoint.host().isEmpty()
            || (endpoint.scheme() != QStringLiteral("https") && endpoint.scheme() != QStringLiteral("http")))) {
        qWarning() << "Invalid Connectivity/endpointUrl in" << settingsFilePath
                   << "- endpoint probe will assume success";
        endpointUrl.clear();
    }

    if (verboseLogging) {
        qDebug() << "QDBusSysConnectivityDaemon verbose debugging enabled";
    }

    //connect(systemdNetworkd, &SystemdNetworkd::operationalStateChanged, this, [this]() {
    //    handleConnectivityWiggle();
    //});

    //connect(systemdNetworkd, &SystemdNetworkd::carrierStateChanged, this, [this]() {
    //    handleConnectivityWiggle();
    //});

    // onlineState in systemd-networkmanager
    // refers to the state where all "RequiredForOnline" networks are connected (routable)
    //connect(systemdNetworkd, &SystemdNetworkd::onlineStateChanged, this, [this]() {
    //    handleConnectivityWiggle();
    //});

    connect(systemdNetworkd, &SystemdNetworkd::addressStateChanged, this, [this]() {
        handleConnectivityWiggle();
    });

    connect(systemdNetworkd, &SystemdNetworkd::linkInterfacesChanged, this, [this]() {
        handleConnectivityWiggle();
    });

    // Connectivity levels range from 3 (interface disconnected) to 0
    // (configured endpoint accessible). A successful HTTP check skips DNS;
    // after HTTP retries fail, DNS distinguishes internet reachability from
    // interface-only connectivity. Unconfigured probes assume success.

    handleConnectivityWiggle();
}

void QDBusSysConnectivityDaemon::handleEndpointConnectivityCheck() {
    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    if (reply) {
        const quint64 replySerial = reply->property("connectivityCheckSerial").toULongLong();
        reply->deleteLater();

        if (replySerial != connectivityCheckSerial) {
            if (verboseLogging) {
                qDebug() << "Ignoring stale connectivity check result";
            }
            return;
        }

        const bool statusMatches = expectedStatusCode == 0
            || reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == expectedStatusCode;
        const bool responseMatches = !checkExpectedResponse
            || (reply->error() == QNetworkReply::NoError && reply->readAll() == expectedResponse);
        if (reply->error() == QNetworkReply::NoError && statusMatches && responseMatches) {
            if (verboseLogging) qDebug() << "Successfully reached" << reply->url();
            setConnectivityLevel(Connectivity::EndpointAccessible);
        } else {
            if (verboseLogging && (!statusMatches || !responseMatches)) {
                qDebug() << "Connectivity endpoint response did not match configured expectations"
                         << reply->url() << "status matches:" << statusMatches
                         << "body matches:" << responseMatches;
            }
            if (deferredEndpointCheckNeeded()) {
                return;
            }
            checkDnsConnectivity(false);
        }
    } else {
        if (verboseLogging) qDebug() << "reply is no more";
        if (!deferredEndpointCheckNeeded()) {
            checkDnsConnectivity(false);
        }
    }
}

void QDBusSysConnectivityDaemon::checkDnsConnectivity(bool endpointPassed) {
    const auto successLevel = endpointPassed ? Connectivity::EndpointAccessible
                                             : Connectivity::InternetRoutable;
    if (dnsTestDomains.isEmpty()) {
        setConnectivityLevel(successLevel);
        return;
    }
    const quint64 replySerial = connectivityCheckSerial;
    auto resolved = std::make_shared<bool>(false);
    // QHostInfo caches answers for about a minute; a cached answer would mask
    // a resolver that has just stopped working.
    QHostInfo::clearCache();
    auto pendingLookups = std::make_shared<QSet<int>>();

    for (const QString &domain : dnsTestDomains) {
        const int lookupId = QHostInfo::lookupHost(domain, this,
            [this, resolved, pendingLookups, replySerial, successLevel](const QHostInfo &host) {
                if (replySerial != connectivityCheckSerial) {
                    return;
                }

                pendingLookups->remove(host.lookupId());

                if (*resolved)
                    return;

                if (host.error() != QHostInfo::NoError) {
                    pendingLookups->remove(host.lookupId());
                    if (verboseLogging) qDebug() << QString("Failed to resolve %1: %2").arg(host.hostName()).arg(host.errorString());

                    if (pendingLookups->isEmpty()) {
                        setConnectivityLevel(Connectivity::InterfaceConnected);
                    }

                    return;
                }

                *resolved = true;
                if (verboseLogging) qDebug() << "Managed to successfully resolve" << host.hostName();
                setConnectivityLevel(successLevel);

                for (int id : *pendingLookups)
                    QHostInfo::abortHostLookup(id);
                pendingLookups->clear();
            });

        pendingLookups->insert(lookupId);
    }
}

bool QDBusSysConnectivityDaemon::deferredEndpointCheckNeeded(int staggeringTimeInMs) {
    shaperConnectionAttempt++;
    if (shaperConnectionAttempt < shaperConnectionAttemptLimit) {
      if (staggeringTimeInMs == 0) {
        staggeringTimeInMs = deferredActionDurationMs();
      }
      // it is to heuristic up this number; if the tool cant reach the internet, the replies will be dying of old age, in which case we really don't need further pause
      retryTimeout->start(staggeringTimeInMs);
      if (verboseLogging) qDebug() << QString("deferred endpoint connectivity check: %1/%2").arg(shaperConnectionAttempt).arg(shaperConnectionAttemptLimit - 1);
      return true;
    }

    return false;
}

void QDBusSysConnectivityDaemon::handleConnectivityWiggle() {
  qdbusSysConnectivitydAdaptor->setCheckingConnectivity(true);
  retryTimeout->stop();
  recheckTimeout->stop();

  // iwd establishes the connection
  // the AP can then boot us and influence state
  // we introduce a settling period for this reason
  wiggleTimeout->start(500);

  // any inflight checks are now stale, so we bump the serial to ignore them when they come back
  ++connectivityCheckSerial;

  if (verboseLogging) {
      qDebug() << "connectivity wiggle";
  }
  /*if (verboseLogging) {
      qDebug() << "operationalStateChanged" << systemdNetworkd->operationalState;
      qDebug() << "carrierStateChanged" << systemdNetworkd->carrierState;
      qDebug() << "onlineStateChanged" << systemdNetworkd->onlineState;
      qDebug() << "addressStateChanged" << systemdNetworkd->addressState;
      qDebug() << "linkInterfacesChanged" << systemdNetworkd->linkInterfaces;
  }*/
}

void QDBusSysConnectivityDaemon::requestExternalCheck()
{
    if (externalCheckTimeout->isActive()) {
        return;
    }
    if (lastExternalCheck.isValid() && lastExternalCheck.elapsed() < minimumExternalCheckIntervalMs) {
        // Coalesce bursts into one deferred check rather than dropping the request.
        externalCheckTimeout->start(int(minimumExternalCheckIntervalMs - lastExternalCheck.elapsed()));
        return;
    }
    lastExternalCheck.start();
    handleWiggleTimeout();
}

void QDBusSysConnectivityDaemon::handleWiggleTimeout() {
    startCheck(true);
}

void QDBusSysConnectivityDaemon::handleRecheckTimeout() {
    // A pending or running check will publish a fresher result anyway.
    if (wiggleTimeout->isActive() || retryTimeout->isActive()
        || qdbusSysConnectivitydAdaptor->isCheckingConnectivity()) {
        return;
    }
    startCheck(false);
}

void QDBusSysConnectivityDaemon::startCheck(bool announce) {
    if (verboseLogging) qDebug() << (announce ? "checking connectivity" : "revalidating connectivity");

    wiggleTimeout->stop();
    retryTimeout->stop();
    recheckTimeout->stop();
    // Background revalidation only publishes a changed result, not progress.
    if (announce) {
        qdbusSysConnectivitydAdaptor->setCheckingConnectivity(true);
    }

    // spare myself the increment on all backoff timers
    shaperConnectionAttempt = 1;

    networkAccessManager->clearConnectionCache();

    // enter connectivity check loop
    checkEndpointConnectivity();
}

void QDBusSysConnectivityDaemon::checkEndpointConnectivity() {
  ++connectivityCheckSerial;

  if(systemdNetworkd->addressState() != SystemdNetworkd::AddressState::Routable) {
      if (verboseLogging) qDebug() << "addressState is not routable";
      // if the network interface is not ready, defer consideration for a period
      if (!deferredEndpointCheckNeeded()) {
        setConnectivityLevel(Connectivity::InterfaceDisconnected);
      }
      return;
  }

  if (endpointUrl.isEmpty()) {
      checkDnsConnectivity(true);
      return;
  }

  QNetworkReply *reply = networkAccessManager->get(QNetworkRequest(QUrl(endpointUrl)));
  reply->setProperty("connectivityCheckSerial", QVariant::fromValue(connectivityCheckSerial));
  new QNetworkReplyTimeout(reply, deferredActionDurationMs(), verboseLogging);

  connect(reply, &QNetworkReply::finished, this, &QDBusSysConnectivityDaemon::handleEndpointConnectivityCheck);
  // it turns out QNetworkReply already covers this eventuality and always finishes, even via destruction
  //connect(reply, &QObject::destroyed, this, &QDBusSysConnectivityDaemon::handleEndpointConnectivityCheck);
}

void QNetworkReplyTimeout::handleTimeout() {
    QNetworkReply* reply = qobject_cast<QNetworkReply*>(parent());
    if (verboseLogging) {
        qDebug() << QString("Connectivity check request timed out after %1 ms").arg(timeoutDurationMs);
    }

    if (!reply) {
        return;
    }

    reply->abort();
    reply->deleteLater();
}

QNetworkReplyTimeout::QNetworkReplyTimeout(QNetworkReply *reply, int timeoutMs, bool verboseLogging):
    QObject(reply),
    timeoutDurationMs(timeoutMs),
    verboseLogging(verboseLogging)
{
    QTimer::singleShot(timeoutMs, this, &QNetworkReplyTimeout::handleTimeout);
}
