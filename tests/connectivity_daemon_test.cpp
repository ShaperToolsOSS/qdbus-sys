// Copyright (C) 2026 Shaper Tools
// SPDX-License-Identifier: LGPL-2.1-only

#include "qdbus-sys-connectivityd-daemon.h"
#include "qsystemd-networkd.h"

#include <QFile>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>
#include <cstring>

using QDBusSys::SystemdNetworkd;
using QDBusSys::Connectivity;

class FakeReply : public QNetworkReply
{
    QByteArray responseBody;

public:
    FakeReply(const QNetworkRequest &request, QObject *parent) : QNetworkReply(parent)
    {
        setRequest(request);
        setUrl(request.url());
        open(QIODevice::ReadOnly);
    }

    void finish(NetworkError error = NoError)
    {
        if (isFinished()) {
            return;
        }
        if (error != NoError) {
            setError(error, QStringLiteral("Simulated failure"));
        }
        setFinished(true);
        emit finished();
    }

    void abort() override { finish(OperationCanceledError); }

    void setResponse(int status, const QByteArray &body)
    {
        if (status != 0) {
            setAttribute(QNetworkRequest::HttpStatusCodeAttribute, status);
        }
        responseBody = body;
    }

protected:
    qint64 readData(char *data, qint64 maxSize) override
    {
        const qint64 size = qMin(maxSize, qint64(responseBody.size()));
        if (size == 0) {
            return -1;
        }
        memcpy(data, responseBody.constData(), size);
        responseBody.remove(0, size);
        return size;
    }
};

class FakeNetworkManager : public QNetworkAccessManager
{
public:
    int requestCount = 0;
    QPointer<FakeReply> latestReply;

protected:
    QNetworkReply *createRequest(Operation, const QNetworkRequest &request, QIODevice *) override
    {
        ++requestCount;
        latestReply = new FakeReply(request, this);
        return latestReply;
    }
};

// Publishes a networkd Manager AddressState as if it arrived over D-Bus.
static bool setAddressState(SystemdNetworkd *networkd, const QString &state)
{
    for (QObject *child : networkd->children()) {
        if (child->metaObject()->indexOfSlot("onManagerPropertiesChanged(QString,QVariantMap,QStringList)") >= 0) {
            return QMetaObject::invokeMethod(child, "onManagerPropertiesChanged", Qt::DirectConnection,
                Q_ARG(QString, QStringLiteral("org.freedesktop.network1.Manager")),
                Q_ARG(QVariantMap, (QVariantMap{{QStringLiteral("AddressState"), state}})), Q_ARG(QStringList, QStringList()));
        }
    }
    return false;
}

class QDBusSysConnectivityDaemonTest : public QObject
{
    Q_OBJECT
    QTemporaryDir settingsDirectory;
    QString settingsFilePath;

private slots:
    void initTestCase()
    {
        QVERIFY(settingsDirectory.isValid());
        QCoreApplication::setOrganizationName(QStringLiteral("qdbus-sys-tests"));
        QCoreApplication::setApplicationName(QStringLiteral("connectivity-daemon"));
        QSettings::setDefaultFormat(QSettings::IniFormat);
        settingsFilePath = settingsDirectory.filePath(QStringLiteral("daemon.conf"));
        QSettings settings(settingsFilePath, QSettings::IniFormat);
        settings.setValue("Connectivity/endpointUrl", "https://health.example.test/check");
        settings.setValue("Connectivity/dnsTestDomains", QStringList{"dns.example.test", "other.example.test"});
        settings.sync();
        QCOMPARE(settings.status(), QSettings::NoError);
    }

    void restartRemovesStaleMarker()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath(QStringLiteral("connected"));
        QFile marker(path);
        QVERIFY(marker.open(QIODevice::WriteOnly));
        marker.close();

        QDBusSysConnectivityDaemon daemon(new SystemdNetworkd, new FakeNetworkManager, path, false, settingsFilePath);
        QVERIFY(!QFile::exists(path));
        daemon.qdbusSysConnectivitydAdaptor->setConnectivityLevel(Connectivity::EndpointAccessible);
        QVERIFY(QFile::exists(path));
        daemon.qdbusSysConnectivitydAdaptor->setConnectivityLevel(Connectivity::InterfaceDisconnected);
        QVERIFY(!QFile::exists(path));
    }

    void readsShippedConfiguration()
    {
        const QString path = QFINDTESTDATA("../src/qdbus-sys-connectivityd/qdbus-sys-connectivityd/resources/qdbus-sys-connectivityd.conf");
        QVERIFY(!path.isEmpty());
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        auto *network = new FakeNetworkManager;
        QDBusSysConnectivityDaemon daemon(new SystemdNetworkd, network,
                                         directory.filePath(QStringLiteral("connected")), false, path);
        QVERIFY(!daemon.endpointUrl.isEmpty());
        QVERIFY(!daemon.verboseLogging);
        QCOMPARE(daemon.dnsTestDomains, (QStringList{"google.com", "baidu.com", "cloudflare.com", "shapertools.com"}));
        QVERIFY(setAddressState(daemon.systemdNetworkd, QStringLiteral("routable")));
        daemon.qdbusSysConnectivitydAdaptor->checkConnectivity();
        QCOMPARE(network->requestCount, 1);
        QCOMPARE(network->latestReply->url(), QUrl(QStringLiteral("https://connectivitycheck.gstatic.com/generate_204")));
    }

    void readsConfiguredTargetsWithoutRewritingFile()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QFile config(settingsFilePath);
        QVERIFY(config.open(QIODevice::ReadOnly));
        const QByteArray originalContents = config.readAll();
        config.close();
        auto *network = new FakeNetworkManager;
        QDBusSysConnectivityDaemon daemon(new SystemdNetworkd, network,
                                         directory.filePath(QStringLiteral("connected")), false, settingsFilePath);
        QVERIFY(setAddressState(daemon.systemdNetworkd, QStringLiteral("routable")));
        daemon.qdbusSysConnectivitydAdaptor->checkConnectivity();
        QCOMPARE(network->requestCount, 1);
        QCOMPARE(network->latestReply->url(), QUrl(QStringLiteral("https://health.example.test/check")));
        QCOMPARE(daemon.dnsTestDomains, (QStringList{"dns.example.test", "other.example.test"}));
        QVERIFY(config.open(QIODevice::ReadOnly));
        QCOMPARE(config.readAll(), originalContents);
    }

    void validatesEndpointResponse_data()
    {
        QTest::addColumn<QVariant>("expectedStatus");
        QTest::addColumn<QVariant>("expectedBody");
        QTest::addColumn<int>("status");
        QTest::addColumn<QByteArray>("body");
        QTest::addColumn<bool>("networkError");
        QTest::addColumn<bool>("success");
        QTest::newRow("no expectations") << QVariant() << QVariant() << 200 << QByteArray("anything") << false << true;
        QTest::newRow("matching status") << QVariant(204) << QVariant() << 204 << QByteArray() << false << true;
        QTest::newRow("wrong status") << QVariant(204) << QVariant() << 200 << QByteArray("portal") << false << false;
        QTest::newRow("missing status") << QVariant(204) << QVariant() << 0 << QByteArray() << false << false;
        QTest::newRow("matching body") << QVariant() << QVariant("OK") << 200 << QByteArray("OK") << false << true;
        QTest::newRow("wrong body") << QVariant() << QVariant("OK") << 200 << QByteArray("portal") << false << false;
        QTest::newRow("case sensitive") << QVariant() << QVariant("OK") << 200 << QByteArray("ok") << false << false;
        QTest::newRow("whitespace sensitive") << QVariant() << QVariant("OK") << 200 << QByteArray("OK\n") << false << false;
        QTest::newRow("utf8 body") << QVariant() << QVariant(QString::fromUtf8("prêt")) << 200 << QByteArray("prêt") << false << true;
        QTest::newRow("explicit empty body") << QVariant(204) << QVariant("") << 204 << QByteArray() << false << true;
        QTest::newRow("unexpected body") << QVariant(204) << QVariant("") << 204 << QByteArray("portal") << false << false;
        QTest::newRow("both match") << QVariant(200) << QVariant("OK") << 200 << QByteArray("OK") << false << true;
        QTest::newRow("body alone mismatches") << QVariant(200) << QVariant("OK") << 200 << QByteArray("portal") << false << false;
        QTest::newRow("status alone mismatches") << QVariant(204) << QVariant("OK") << 200 << QByteArray("OK") << false << false;
        QTest::newRow("network error") << QVariant(204) << QVariant("") << 204 << QByteArray() << true << false;
        QTest::newRow("invalid status setting") << QVariant("not a status") << QVariant() << 200 << QByteArray() << false << true;
    }

    void validatesEndpointResponse()
    {
        QFETCH(QVariant, expectedStatus);
        QFETCH(QVariant, expectedBody);
        QFETCH(int, status);
        QFETCH(QByteArray, body);
        QFETCH(bool, networkError);
        QFETCH(bool, success);
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath(QStringLiteral("daemon.conf"));
        {
            QSettings settings(path, QSettings::IniFormat);
            settings.setValue("Connectivity/endpointUrl", "https://example.test/check");
            if (expectedStatus.isValid()) {
                settings.setValue("Connectivity/expectedStatusCode", expectedStatus);
            }
            if (expectedBody.isValid()) {
                settings.setValue("Connectivity/expectedResponse", expectedBody);
            }
            settings.sync();
            QCOMPARE(settings.status(), QSettings::NoError);
        }
        if (expectedStatus == QVariant("not a status")) {
            QTest::ignoreMessage(QtWarningMsg, QRegularExpression("Ignoring invalid Connectivity/expectedStatusCode.*"));
        }
        auto *network = new FakeNetworkManager;
        const QString marker = directory.filePath(QStringLiteral("connected"));
        QDBusSysConnectivityDaemon daemon(new SystemdNetworkd, network, marker, false, path);
        QVERIFY(setAddressState(daemon.systemdNetworkd, QStringLiteral("routable")));
        daemon.qdbusSysConnectivitydAdaptor->checkConnectivity();
        QCOMPARE(network->requestCount, 1);
        // Exercise the final attempt so mismatches immediately reach DNS fallback.
        daemon.shaperConnectionAttempt = daemon.shaperConnectionAttemptLimit;
        network->latestReply->setResponse(status, body);
        network->latestReply->finish(networkError ? QNetworkReply::ConnectionRefusedError : QNetworkReply::NoError);
        QCOMPARE(daemon.qdbusSysConnectivitydAdaptor->connectivityLevel(),
                 int(success ? Connectivity::EndpointAccessible : Connectivity::InternetRoutable));
        QCOMPARE(QFile::exists(marker), success);
        QVERIFY(!daemon.qdbusSysConnectivitydAdaptor->property("checkingConnectivity").toBool());
    }

    void mismatchedResponseRetries()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        auto *network = new FakeNetworkManager;
        QDBusSysConnectivityDaemon daemon(new SystemdNetworkd, network,
                                         directory.filePath(QStringLiteral("connected")), false, settingsFilePath);
        daemon.expectedStatusCode = 204;
        QVERIFY(setAddressState(daemon.systemdNetworkd, QStringLiteral("routable")));
        daemon.qdbusSysConnectivitydAdaptor->checkConnectivity();
        network->latestReply->setResponse(200, "portal");
        network->latestReply->finish();
        QVERIFY(daemon.retryTimeout->isActive());
        QVERIFY(daemon.qdbusSysConnectivitydAdaptor->property("checkingConnectivity").toBool());
        // Trigger the scheduled retry without waiting for its backoff interval.
        daemon.retryTimeout->stop();
        daemon.checkEndpointConnectivity();
        QCOMPARE(network->requestCount, 2);
        network->latestReply->setResponse(204, {});
        network->latestReply->finish();
        QCOMPARE(daemon.qdbusSysConnectivitydAdaptor->connectivityLevel(), int(Connectivity::EndpointAccessible));
    }

    void unconfiguredProbesAssumeSuccess_data()
    {
        QTest::addColumn<QString>("endpoint");
        QTest::addColumn<QStringList>("domains");
        QTest::addColumn<bool>("expectRequest");
        QTest::newRow("empty settings") << QString() << QStringList() << false;
        QTest::newRow("missing endpoint") << QString() << QStringList{"localhost"} << false;
        QTest::newRow("invalid endpoint") << QStringLiteral("not a URL") << QStringList{"localhost"} << false;
        QTest::newRow("unsupported scheme") << QStringLiteral("ftp://example.test/check") << QStringList{"localhost"} << false;
        QTest::newRow("empty domains") << QStringLiteral("https://example.test/check") << QStringList{" ", ""} << true;
    }

    void unconfiguredProbesAssumeSuccess()
    {
        QFETCH(QString, endpoint);
        QFETCH(QStringList, domains);
        QFETCH(bool, expectRequest);
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath(QStringLiteral("daemon.conf"));
        {
            QSettings settings(path, QSettings::IniFormat);
            settings.setValue("Connectivity/endpointUrl", endpoint);
            settings.setValue("Connectivity/dnsTestDomains", domains);
            settings.sync();
            QCOMPARE(settings.status(), QSettings::NoError);
        }
        auto *network = new FakeNetworkManager;
        const QString marker = directory.filePath(QStringLiteral("connected"));
        QDBusSysConnectivityDaemon daemon(new SystemdNetworkd, network, marker, false, path);
        QVERIFY(setAddressState(daemon.systemdNetworkd, QStringLiteral("routable")));
        daemon.qdbusSysConnectivitydAdaptor->checkConnectivity();
        QCOMPARE(network->requestCount, int(expectRequest));
        if (expectRequest) {
            network->latestReply->finish();
        }
        QTRY_COMPARE(daemon.qdbusSysConnectivitydAdaptor->connectivityLevel(), int(Connectivity::EndpointAccessible));
        QVERIFY(!daemon.qdbusSysConnectivitydAdaptor->property("checkingConnectivity").toBool());
        QVERIFY(QFile::exists(marker));
    }

    void configurationErrorsWarnAndAssumeSuccess_data()
    {
        QTest::addColumn<bool>("malformed");
        QTest::newRow("missing file") << false;
        QTest::newRow("malformed file") << true;
    }

    void configurationErrorsWarnAndAssumeSuccess()
    {
        QFETCH(bool, malformed);
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath(QStringLiteral("daemon.conf"));
        if (malformed) {
            QFile config(path);
            QVERIFY(config.open(QIODevice::WriteOnly));
            config.write("[Connectivity\nendpointUrl=https://example.test/check\n");
        }
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(
            malformed ? "Cannot parse or read connectivity configuration file .*daemon\\.conf.*"
                      : "Cannot read connectivity configuration file .*daemon\\.conf.*"));
        auto *network = new FakeNetworkManager;
        const QString marker = directory.filePath(QStringLiteral("connected"));
        QDBusSysConnectivityDaemon daemon(new SystemdNetworkd, network, marker, false, path);
        QVERIFY(setAddressState(daemon.systemdNetworkd, QStringLiteral("routable")));
        daemon.qdbusSysConnectivitydAdaptor->checkConnectivity();
        QCOMPARE(network->requestCount, 0);
        QCOMPARE(daemon.qdbusSysConnectivitydAdaptor->connectivityLevel(), int(Connectivity::EndpointAccessible));
        QVERIFY(QFile::exists(marker));
        QCOMPARE(QFile::exists(path), malformed);

        QVERIFY(setAddressState(daemon.systemdNetworkd, QStringLiteral("off")));
        daemon.shaperConnectionAttempt = daemon.shaperConnectionAttemptLimit;
        daemon.checkEndpointConnectivity();
        QCOMPARE(daemon.qdbusSysConnectivitydAdaptor->connectivityLevel(), int(Connectivity::InterfaceDisconnected));
        QVERIFY(!QFile::exists(marker));
    }

    void missingDnsDoesNotOverrideEndpointFailure()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        auto *network = new FakeNetworkManager;
        QDBusSysConnectivityDaemon daemon(new SystemdNetworkd, network,
                                         directory.filePath(QStringLiteral("connected")), false, settingsFilePath);
        daemon.dnsTestDomains.clear();
        QVERIFY(setAddressState(daemon.systemdNetworkd, QStringLiteral("routable")));
        daemon.qdbusSysConnectivitydAdaptor->checkConnectivity();
        daemon.shaperConnectionAttempt = daemon.shaperConnectionAttemptLimit;
        network->latestReply->finish(QNetworkReply::HostNotFoundError);
        QCOMPARE(daemon.qdbusSysConnectivitydAdaptor->connectivityLevel(), int(Connectivity::InternetRoutable));
        QVERIFY(!daemon.qdbusSysConnectivitydAdaptor->property("checkingConnectivity").toBool());
    }

    void manualCheckPublishesRunningState()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        auto *network = new FakeNetworkManager;
        QDBusSysConnectivityDaemon daemon(new SystemdNetworkd, network,
                                         directory.filePath(QStringLiteral("connected")), false, settingsFilePath);
        daemon.wiggleTimeout->stop();
        QVERIFY(setAddressState(daemon.systemdNetworkd, QStringLiteral("routable")));
        auto *adaptor = daemon.qdbusSysConnectivitydAdaptor;
        adaptor->setCheckingConnectivity(false);
        QSignalSpy checking(&daemon, &QDBusSysConnectivityDaemon::checkingConnectivityChanged);

        adaptor->checkConnectivity();
        QCOMPARE(network->requestCount, 1);
        QVERIFY(adaptor->property("checkingConnectivity").toBool());
        QCOMPARE(checking.count(), 1);
        network->latestReply->finish();
        QVERIFY(!adaptor->property("checkingConnectivity").toBool());
        QCOMPARE(checking.count(), 2);
        QCOMPARE(adaptor->connectivityLevel(), int(Connectivity::EndpointAccessible));
    }

    void newCheckCancelsOldRetry()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        auto *network = new FakeNetworkManager;
        QDBusSysConnectivityDaemon daemon(new SystemdNetworkd, network,
                                         directory.filePath(QStringLiteral("connected")), false, settingsFilePath);
        daemon.wiggleTimeout->stop();
        QVERIFY(setAddressState(daemon.systemdNetworkd, QStringLiteral("routable")));
        QVERIFY(daemon.deferredEndpointCheckNeeded(30));
        daemon.qdbusSysConnectivitydAdaptor->checkConnectivity();
        const quint64 serial = daemon.connectivityCheckSerial;
        QTest::qWait(80);
        QCOMPARE(network->requestCount, 1);
        QCOMPARE(daemon.connectivityCheckSerial, serial);

        network->latestReply->finish();
        QCOMPARE(daemon.qdbusSysConnectivitydAdaptor->connectivityLevel(), int(Connectivity::EndpointAccessible));
    }

    void networkChangeCancelsRetryAndRejectsOldResult()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        auto *network = new FakeNetworkManager;
        QDBusSysConnectivityDaemon daemon(new SystemdNetworkd, network,
                                         directory.filePath(QStringLiteral("connected")), false, settingsFilePath);
        daemon.wiggleTimeout->stop();
        QVERIFY(setAddressState(daemon.systemdNetworkd, QStringLiteral("routable")));
        daemon.qdbusSysConnectivitydAdaptor->checkConnectivity();
        auto oldReply = network->latestReply;
        QVERIFY(daemon.deferredEndpointCheckNeeded(30));
        daemon.handleConnectivityWiggle();
        oldReply->finish();
        QTest::qWait(80);
        QCOMPARE(network->requestCount, 1);
        QCOMPARE(daemon.qdbusSysConnectivitydAdaptor->connectivityLevel(), int(Connectivity::InterfaceDisconnected));
        QVERIFY(daemon.qdbusSysConnectivitydAdaptor->property("checkingConnectivity").toBool());
    }

    void retryStillRunsForCurrentCheck()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        auto *network = new FakeNetworkManager;
        QDBusSysConnectivityDaemon daemon(new SystemdNetworkd, network,
                                         directory.filePath(QStringLiteral("connected")), false, settingsFilePath);
        daemon.wiggleTimeout->stop();
        QVERIFY(setAddressState(daemon.systemdNetworkd, QStringLiteral("routable")));
        QVERIFY(daemon.deferredEndpointCheckNeeded(1));
        QTRY_COMPARE_WITH_TIMEOUT(network->requestCount, 1, 200);
    }

    void busPropertiesAreReadOnly()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QDBusSysConnectivityDaemon daemon(new SystemdNetworkd, new FakeNetworkManager,
                                         directory.filePath(QStringLiteral("connected")), false, settingsFilePath);
        const QMetaObject *meta = daemon.qdbusSysConnectivitydAdaptor->metaObject();
        for (const char *name : {"connected", "checkingConnectivity", "connectivityLevel"}) {
            const int index = meta->indexOfProperty(name);
            QVERIFY(index >= 0);
            QVERIFY2(!meta->property(index).isWritable(), name);
        }
    }

    void settledResultIsRevalidatedQuietly()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        auto *network = new FakeNetworkManager;
        QDBusSysConnectivityDaemon daemon(new SystemdNetworkd, network,
                                         directory.filePath(QStringLiteral("connected")), false, settingsFilePath);
        QCOMPARE(daemon.recheckIntervalMs, 60000);
        daemon.wiggleTimeout->stop();
        QVERIFY(setAddressState(daemon.systemdNetworkd, QStringLiteral("routable")));
        auto *adaptor = daemon.qdbusSysConnectivitydAdaptor;
        adaptor->checkConnectivity();
        network->latestReply->finish();
        QCOMPARE(adaptor->connectivityLevel(), int(Connectivity::EndpointAccessible));
        QVERIFY(daemon.recheckTimeout->isActive());

        QSignalSpy checking(&daemon, &QDBusSysConnectivityDaemon::checkingConnectivityChanged);
        QSignalSpy level(&daemon, &QDBusSysConnectivityDaemon::connectivityLevelChanged);
        daemon.recheckTimeout->stop();
        daemon.handleRecheckTimeout();
        QCOMPARE(network->requestCount, 2);
        // The endpoint has stopped answering; retries are exhausted.
        daemon.shaperConnectionAttempt = daemon.shaperConnectionAttemptLimit;
        daemon.dnsTestDomains.clear();
        network->latestReply->finish(QNetworkReply::HostNotFoundError);
        QCOMPARE(adaptor->connectivityLevel(), int(Connectivity::InternetRoutable));
        QCOMPARE(level.count(), 1);
        QCOMPARE(checking.count(), 0);
        QVERIFY(daemon.recheckTimeout->isActive());
    }

    void recheckIntervalIsConfigurable_data()
    {
        QTest::addColumn<QVariant>("seconds");
        QTest::addColumn<int>("expectedMs");
        QTest::newRow("disabled") << QVariant(0) << 0;
        QTest::newRow("custom") << QVariant(5) << 5000;
        QTest::newRow("invalid") << QVariant("soon") << 60000;
    }

    void recheckIntervalIsConfigurable()
    {
        QFETCH(QVariant, seconds);
        QFETCH(int, expectedMs);
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath(QStringLiteral("daemon.conf"));
        {
            QSettings settings(path, QSettings::IniFormat);
            settings.setValue("Connectivity/recheckIntervalSec", seconds);
        }
        if (expectedMs == 60000) {
            QTest::ignoreMessage(QtWarningMsg, QRegularExpression("Ignoring invalid Connectivity/recheckIntervalSec.*"));
        }
        QDBusSysConnectivityDaemon daemon(new SystemdNetworkd, new FakeNetworkManager,
                                         directory.filePath(QStringLiteral("connected")), false, path);
        QCOMPARE(daemon.recheckIntervalMs, expectedMs);
        daemon.wiggleTimeout->stop();
        daemon.qdbusSysConnectivitydAdaptor->checkConnectivity();
        // Unconfigured probes settle immediately without an address either.
        daemon.shaperConnectionAttempt = daemon.shaperConnectionAttemptLimit;
        daemon.checkEndpointConnectivity();
        QCOMPARE(daemon.recheckTimeout->isActive(), expectedMs > 0);
    }

    void externalChecksAreRateLimited()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        auto *network = new FakeNetworkManager;
        QDBusSysConnectivityDaemon daemon(new SystemdNetworkd, network,
                                         directory.filePath(QStringLiteral("connected")), false, settingsFilePath);
        daemon.wiggleTimeout->stop();
        QVERIFY(setAddressState(daemon.systemdNetworkd, QStringLiteral("routable")));
        for (int i = 0; i < 20; ++i) daemon.qdbusSysConnectivitydAdaptor->checkConnectivity();
        QCOMPARE(network->requestCount, 1);
        // The burst is coalesced into one deferred check, not dropped.
        QVERIFY(daemon.externalCheckTimeout->isActive());
        QVERIFY(daemon.externalCheckTimeout->interval() <= daemon.minimumExternalCheckIntervalMs);
    }
};

QTEST_GUILESS_MAIN(QDBusSysConnectivityDaemonTest)
#include "connectivity_daemon_test.moc"
