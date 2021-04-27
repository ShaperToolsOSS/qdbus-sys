// Copyright (C) 2026 Shaper Tools
// SPDX-License-Identifier: LGPL-2.1-only

#include "qdbus-sys-connectivityd.h"

#include <QDBusAbstractAdaptor>
#include <QByteArray>
#include <QElapsedTimer>
#include <QObject>
#include <QPointer>
#include <QTimer>

class QNetworkReply;
class QNetworkAccessManager;
namespace QDBusSys { class SystemdNetworkd; }
using QDBusSys::SystemdNetworkd;
using QDBusSys::Connectivity;

class QNetworkReplyTimeout : public QObject {
  Q_OBJECT
public:
  QNetworkReplyTimeout(QNetworkReply* reply, int timeoutMs, bool verboseLogging);

private slots:
  void handleTimeout();
private:
  int timeoutDurationMs = 0;
  bool verboseLogging = false;
};

class ConnectivityAdaptor;
using ConnectivityLevel = Connectivity::Level;

class QDBusSysConnectivityDaemon : public QObject
{
    Q_OBJECT
public:
    QDBusSysConnectivityDaemon();
    void handleConnectivityWiggle();
signals:
    // In-process notifications; the bus sees PropertiesChanged.
    void connectedChanged();
    void checkingConnectivityChanged();
    void connectivityLevelChanged();
private slots:
    void handleWiggleTimeout();
    void handleRecheckTimeout();
    void handleEndpointConnectivityCheck();
    void checkEndpointConnectivity();
private:
    friend class QDBusSysConnectivityDaemonTest;
    // The daemon owns these dependencies. Tests can use a temporary state file
    // and leave the system bus untouched.
    QDBusSysConnectivityDaemon(SystemdNetworkd *networkd, QNetworkAccessManager *networkManager,
                             const QString &stateFilePath, bool publishOnDBus,
                             const QString &settingsFilePath = QStringLiteral("/etc/qdbus-sys/connectivityd.conf"));
    QTimer *wiggleTimeout;
    QTimer *retryTimeout;
    QTimer *recheckTimeout;
    QTimer *externalCheckTimeout;
    QElapsedTimer lastExternalCheck;
    QString connectedStateFilePath;

    int deferredActionDurationMs() const {
        return connectivityCheckTimeoutMs*shaperConnectionAttempt;
    }
    void setupDBus();
    void startCheck(bool announce);
    void requestExternalCheck();
    void setConnectivityLevel(ConnectivityLevel status);
    bool deferredEndpointCheckNeeded(int staggeringTimeInMs = 0);
    void checkDnsConnectivity(bool endpointPassed);

    QString endpointUrl;
    int expectedStatusCode = 0;
    bool checkExpectedResponse = false;
    QByteArray expectedResponse;
    QStringList dnsTestDomains;

    bool verboseLogging = false;
    bool publishedOnDBus = false;
    // Settled results are revalidated quietly so a silent upstream outage
    // (or recovery) is noticed without a local address change.
    int recheckIntervalMs = 60000;
    // Unprivileged callers may request checks; bound the probe rate they cause.
    const int minimumExternalCheckIntervalMs = 2000;

    friend class ConnectivityAdaptor;

    int shaperConnectionAttempt = 1;
    const int shaperConnectionAttemptLimit = 8;
    const int connectivityCheckTimeoutMs = 500;

    ConnectivityAdaptor *qdbusSysConnectivitydAdaptor = nullptr;
    SystemdNetworkd *systemdNetworkd = nullptr;
    QNetworkAccessManager *networkAccessManager = nullptr;
    quint64 connectivityCheckSerial = 0;
};

class ConnectivityAdaptor: public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "com.shapertools.qdbussys.connectivityd1")

    // Read-only on the bus: state is owned by the daemon, not its callers.
    // Changes are announced with org.freedesktop.DBus.Properties.PropertiesChanged.
    Q_PROPERTY(bool connected READ isConnected)
    Q_PROPERTY(bool checkingConnectivity READ isCheckingConnectivity)
    Q_PROPERTY(int connectivityLevel READ connectivityLevel)

public:
    ConnectivityAdaptor(QDBusSysConnectivityDaemon *managerd)
        : QDBusAbstractAdaptor(managerd),
        qdbusSysConnectivityDaemon(managerd)
    {
    }

    bool isConnected() const { return connected; }
    bool isCheckingConnectivity() const { return checkingConnectivity; }
    int connectivityLevel() const;
    void setConnectivityLevel(ConnectivityLevel connectivityLevel);
    void setCheckingConnectivity(bool checkingConnectivity);

public slots:
    void checkConnectivity();

private:
    void publishProperties(const QVariantMap &changed);

    bool connected = false;
    bool checkingConnectivity = false;
    ConnectivityLevel mConnectivityLevel = Connectivity::InterfaceDisconnected;

    QDBusSysConnectivityDaemon *qdbusSysConnectivityDaemon;
};
