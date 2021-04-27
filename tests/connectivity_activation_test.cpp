// Copyright (C) 2026 Shaper Tools
// SPDX-License-Identifier: LGPL-2.1-only

#include "qdbus-sys-connectivityd.h"

#include <QFile>
#include <QProcess>
#include <QTemporaryDir>
#include <QtDBus>
#include <QtTest>

using QDBusSys::Connectivity;

class ConnectivityActivationTest : public QObject
{
    Q_OBJECT
    QTemporaryDir directory;
    QProcess busProcess;
    bool busReady = false;
    const QString serviceName = QStringLiteral("com.shapertools.qdbussys.connectivityd");
    const QString interfaceName = QStringLiteral("com.shapertools.qdbussys.connectivityd1");

    QString eventsPath() const { return directory.filePath(QStringLiteral("events")); }
    QString manifestPath() const { return directory.filePath(serviceName + QStringLiteral(".service")); }
    QByteArray events() const
    {
        QFile file(eventsPath());
        return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
    }
    bool serviceRunning() const
    {
        return busReady && QDBusConnection::systemBus().interface()->isServiceRegistered(serviceName).value();
    }
    static QString quoted(QString value)
    {
        value.replace('\\', QStringLiteral("\\\\"));
        value.replace('"', QStringLiteral("\\\""));
        return '"' + value + '"';
    }

private slots:
    void initTestCase()
    {
        QVERIFY(directory.isValid());
        QFile config(directory.filePath(QStringLiteral("bus.conf")));
        QVERIFY(config.open(QIODevice::WriteOnly));
        const QString xml = QStringLiteral(
            "<busconfig><type>session</type><listen>unix:tmpdir=%1</listen>"
            "<auth>EXTERNAL</auth><policy context=\"default\">"
            "<allow user=\"*\"/><allow own=\"*\"/><allow send_destination=\"*\"/>"
            "<allow receive_sender=\"*\"/></policy><servicedir>%1</servicedir></busconfig>")
            .arg(directory.path().toHtmlEscaped());
        config.write(xml.toUtf8());
        config.close();
        busProcess.start(QStringLiteral(DBUS_DAEMON_PATH),
                         {QStringLiteral("--nofork"), QStringLiteral("--print-address=1"),
                          QStringLiteral("--config-file=") + config.fileName()});
        QVERIFY(busProcess.waitForStarted());
        QVERIFY(busProcess.waitForReadyRead(3000));
        const QByteArray address = busProcess.readLine().trimmed();
        QVERIFY2(address.startsWith("unix:"), busProcess.readAllStandardError().constData());
        // Set before the client's first systemBus() call. No host services are used.
        qputenv("DBUS_SYSTEM_BUS_ADDRESS", address);
        QVERIFY(QDBusConnection::systemBus().isConnected());
        busReady = true;
    }

    void init()
    {
        QVERIFY(!serviceRunning());
        QFile::remove(eventsPath());
        QFile source(QStringLiteral(ACTIVATION_MANIFEST_PATH));
        QVERIFY(source.open(QIODevice::ReadOnly));
        QString manifest = QString::fromUtf8(source.readAll());
        const QString helper = QCoreApplication::applicationDirPath() + QStringLiteral("/connectivity_activation_service");
        manifest.replace(QRegularExpression(QStringLiteral("(?m)^Exec=.*$")),
                         QStringLiteral("Exec=") + quoted(helper) + ' ' + quoted(eventsPath()));
        QFile file(manifestPath());
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(manifest.toUtf8());
        file.close();
        const auto reply = QDBusConnection::systemBus().interface()->call(QStringLiteral("ReloadConfig"));
        QVERIFY(reply.type() != QDBusMessage::ErrorMessage);
    }

    void clientConstructionActivatesService()
    {
        Connectivity client;
        QVERIFY(!client.connected());
        QTRY_VERIFY_WITH_TIMEOUT(client.connected(), 5000);
        QCOMPARE(client.connectivityLevel(), Connectivity::EndpointAccessible);
        QCOMPARE(events().count("start\n"), 1);
        QCOMPARE(events().count("check\n"), 0);
    }

    void runningServiceStateIsAvailableAtConstruction()
    {
        {
            Connectivity activator;
            QTRY_VERIFY_WITH_TIMEOUT(activator.connected(), 5000);
        }
        // Applications such as command-line tools read state without waiting
        // for a change notification.
        Connectivity client;
        QVERIFY(client.connected());
        QCOMPARE(client.connectivityLevel(), Connectivity::EndpointAccessible);
        QCOMPARE(events().count("start\n"), 1);
    }

    void checksDuringStartupAreSentOnce()
    {
        Connectivity client;
        for (int i = 0; i < 5; ++i) client.checkConnectivity();
        QTRY_VERIFY_WITH_TIMEOUT(client.connected(), 5000);
        QTRY_COMPARE(events().count("check\n"), 1);
        QCOMPARE(events().count("start\n"), 1);
        QTest::qWait(50);
        QCOMPARE(events().count("check\n"), 1);
    }

    void failedActivationKeepsSafeDefaults()
    {
        QVERIFY(QFile::remove(manifestPath()));
        QDBusConnection::systemBus().interface()->call(QStringLiteral("ReloadConfig"));
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("Connectivity: Could not activate daemon:.*")));
        Connectivity client;
        QTRY_VERIFY(client.findChildren<QDBusPendingCallWatcher *>().isEmpty());
        QVERIFY(!client.connected());
        QVERIFY(!client.checkingConnectivity());
        QCOMPARE(client.connectivityLevel(), Connectivity::InterfaceDisconnected);
        QVERIFY(!serviceRunning());
        QVERIFY(events().isEmpty());
    }

    void cleanup()
    {
        if (!serviceRunning()) return;
        const auto message = QDBusMessage::createMethodCall(serviceName,
            QStringLiteral("/com/shapertools/qdbussys/connectivityd"), interfaceName, QStringLiteral("quit"));
        QDBusConnection::systemBus().asyncCall(message);
        QTRY_VERIFY(!serviceRunning());
    }

    void cleanupTestCase()
    {
        if (busReady) QDBusConnection::disconnectFromBus(QDBusConnection::systemBus().name());
        busProcess.terminate();
        if (!busProcess.waitForFinished(3000)) {
            busProcess.kill();
            busProcess.waitForFinished();
        }
    }
};

QTEST_GUILESS_MAIN(ConnectivityActivationTest)
#include "connectivity_activation_test.moc"
