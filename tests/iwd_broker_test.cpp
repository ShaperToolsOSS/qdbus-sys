// Copyright (C) 2026 Shaper Tools
// SPDX-License-Identifier: LGPL-2.1-only

#include "iwd.h"
#include "iwd-ipc_p.h"
#include "mock_iwd.h"

#include <QJsonDocument>
#include <QProcess>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>
#include <memory>

using QDBusSys::IWD;

class DeltaRecorder : public QObject
{
    Q_OBJECT
public:
    QList<QVariantMap> deltas;
public slots:
    void record(const QVariantMap &delta) { deltas << IWDIPC::unpack(delta).toMap(); }
};

class IWDBrokerTest : public QObject
{
    Q_OBJECT
    QProcess broker;
    QTemporaryDir logs;

    void startBroker()
    {
        broker.setProcessChannelMode(QProcess::MergedChannels);
        broker.setStandardOutputFile(logs.filePath(QStringLiteral("broker.log")), QIODevice::Append);
        broker.start(QStringLiteral(QIWD_EXECUTABLE_PATH));
        QVERIFY(broker.waitForStarted());
        QTRY_VERIFY(QDBusConnection::systemBus().interface()->isServiceRegistered(QString::fromLatin1(IWDIPC::service)).value());
    }
    void stopBroker()
    {
        broker.terminate();
        if (!broker.waitForFinished(3000)) {
            broker.kill();
            broker.waitForFinished();
        }
        QTRY_VERIFY(!QDBusConnection::systemBus().interface()->isServiceRegistered(QString::fromLatin1(IWDIPC::service)).value());
    }
    static bool succeeded(const QSignalSpy &spy, const QString &id)
    {
        for (const auto &event : spy) {
            if (event.at(0).toString() == id) return event.at(1).toBool();
        }
        return false;
    }

private slots:
    void initTestCase()
    {
        QVERIFY(!qgetenv("DBUS_SESSION_BUS_ADDRESS").isEmpty());
        QCOMPARE(qgetenv("DBUS_SYSTEM_BUS_ADDRESS"), qgetenv("DBUS_SESSION_BUS_ADDRESS"));
        qDBusRegisterMetaType<NetworkEntry>();
        qDBusRegisterMetaType<ManagedObjectList>();
        qDBusRegisterMetaType<InterfacePropertyMap>();
        qDBusRegisterMetaType<OrderedNetworksList>();
        qDBusRegisterMetaType<LevelsList>();
        QVERIFY(logs.isValid());
    }

    void clientsShareOneBackendAndReceiveUpdates()
    {
        FakeIwd iwd;
        QVERIFY(iwd.start());
        startBroker();
        IWD first, second;
        QTRY_COMPARE(first.networks()->rowCount(), 1);
        QTRY_COMPARE(second.networks()->rowCount(), 1);
        QVERIFY(first.available() && second.available());
        QCOMPARE(iwd.agents.paths.size(), 1);
        QCOMPARE(iwd.station.registrations, 1);
        QCOMPARE(iwd.station.duplicates, 0);
        const QString backendOwner = iwd.agents.paths.constBegin().key();
        auto forged = QDBusMessage::createMethodCall(backendOwner, iwd.agents.paths.value(backendOwner),
            QStringLiteral("net.connman.iwd.Agent"), QStringLiteral("RequestPassphrase"));
        forged << QVariant::fromValue(QDBusObjectPath(networkPath));
        const auto denied = QDBusConnection::systemBus().call(forged);
        QCOMPARE(denied.type(), QDBusMessage::ErrorMessage);
        QCOMPARE(denied.errorName(), QStringLiteral("org.freedesktop.DBus.Error.AccessDenied"));
        iwd.network.name = QStringLiteral("Renamed network");
        iwd.interfacesAdded(networkPath, {QStringLiteral("net.connman.iwd.Network")});
        QTRY_COMPARE(first.connectedNetworkData().value("ssid").toString(), QStringLiteral("Renamed network"));
        QTRY_COMPARE(second.connectedNetworkData(), first.connectedNetworkData());
    }

    void simultaneousScansAreCoalescedAndResultsArePrivate()
    {
        FakeIwd iwd;
        QVERIFY(iwd.start());
        startBroker();
        IWD first, second;
        QTRY_COMPARE(first.networks()->rowCount(), 1);
        QTRY_COMPARE(second.networks()->rowCount(), 1);
        QSignalSpy firstResults(&first, &IWD::operationFinished);
        QSignalSpy secondResults(&second, &IWD::operationFinished);
        const QString a = first.scan(), b = second.scan();
        QTRY_VERIFY(succeeded(firstResults, a));
        QTRY_VERIFY(succeeded(secondResults, b));
        QCOMPARE(firstResults.count(), 1);
        QCOMPARE(secondResults.count(), 1);
        QCOMPARE(iwd.station.scans, 1);
    }

    void invalidNetworkCompletesAndUnblocksQueue()
    {
        FakeIwd iwd;
        QVERIFY(iwd.start());
        startBroker();
        IWD client;
        QTRY_COMPARE(client.networks()->rowCount(), 1);
        QSignalSpy results(&client, &IWD::operationFinished);
        const QString invalid = client.connectNetwork(QStringLiteral("does-not-exist"));
        const QString scan = client.scan();
        QTRY_COMPARE(results.count(), 2);
        QCOMPARE(results.at(0).at(0).toString(), invalid);
        QVERIFY(!results.at(0).at(1).toBool());
        QVERIFY(succeeded(results, scan));
    }

    void deviceSelectionIsLocalToEachClient()
    {
        FakeIwd iwd;
        QVERIFY(iwd.start());
        MockStation otherStation;
        MockNetwork otherNetwork;
        const QString otherPath = adapterPath + QStringLiteral("/4");
        const QString otherNetworkPath = otherPath + QStringLiteral("/7465737432_psk");
        otherStation.currentNetwork = QDBusObjectPath(otherNetworkPath);
        otherNetwork.name = QStringLiteral("Other network");
        otherNetwork.objectPath = otherNetworkPath;
        iwd.root.objects[QDBusObjectPath(otherPath)] = {
            {QStringLiteral("net.connman.iwd.Device"), {{QStringLiteral("Adapter"), QVariant::fromValue(QDBusObjectPath(adapterPath))}}},
        };
        QVERIFY(iwd.exportObject(otherPath, &otherStation));
        QVERIFY(iwd.exportObject(otherNetworkPath, &otherNetwork));
        startBroker();
        IWD first, second;
        QTRY_COMPARE(first.devices().size(), 2);
        QTRY_COMPARE(first.networks()->rowCount(), 1);
        second.setSelectedDevice(otherPath);
        QTRY_COMPARE(second.connectedNetworkData().value("ssid").toString(), QStringLiteral("Other network"));
        QCOMPARE(first.selectedDevice(), stationPath);
        QCOMPARE(first.connectedNetworkData().value("ssid").toString(), QStringLiteral("Test network"));
        QCOMPARE(iwd.agents.paths.size(), 2);
        const auto otherInterfaces = iwd.root.objects.take(QDBusObjectPath(otherPath));
        iwd.bus.unregisterObject(otherPath);
        emit iwd.root.InterfacesRemoved(QDBusObjectPath(otherPath),
                                        {QStringLiteral("net.connman.iwd.Device"), QStringLiteral("net.connman.iwd.Station")});
        QTRY_COMPARE(first.devices().size(), 1);
        QTRY_COMPARE(second.networks()->rowCount(), 0);
        QVERIFY(!second.connected());
        QCOMPARE(first.networks()->rowCount(), 1);
        iwd.root.objects[QDBusObjectPath(otherPath)] = otherInterfaces;
        QVERIFY(iwd.exportObject(otherPath, &otherStation));
        iwd.interfacesAdded(otherPath, {QStringLiteral("net.connman.iwd.Device"), QStringLiteral("net.connman.iwd.Station")});
        QTRY_COMPARE(first.devices().size(), 2);
        QTRY_COMPARE(second.connectedNetworkData().value("ssid").toString(), QStringLiteral("Other network"));
    }

    void preparedCredentialsAreScopedToConnectionRequests()
    {
        FakeIwd iwd;
        iwd.station.connected = iwd.network.connected = false;
        QVERIFY(iwd.start());
        startBroker();
        IWD first, second;
        QTRY_COMPARE(first.networks()->rowCount(), 1);
        QTRY_COMPARE(second.networks()->rowCount(), 1);
        QSignalSpy a(&first, &IWD::operationFinished), b(&second, &IWD::operationFinished);
        first.preparePassword(QStringLiteral("74657374_psk"), QStringLiteral("first-test-secret"));
        second.preparePassword(QStringLiteral("74657374_psk"), QStringLiteral("second-test-secret"));
        const QString firstId = first.connectNetwork(QStringLiteral("74657374_psk"));
        const QString secondId = second.connectNetwork(QStringLiteral("74657374_psk"));
        QTRY_VERIFY(succeeded(a, firstId));
        QTRY_VERIFY(succeeded(b, secondId));
        QCOMPARE(iwd.network.credentials, (QStringList{"first-test-secret", "second-test-secret"}));
        QCOMPARE(a.count(), 1);
        QCOMPARE(b.count(), 1);
        const auto request = QDBusMessage::createMethodCall(QString::fromLatin1(IWDIPC::service), QString::fromLatin1(IWDIPC::path),
                                                          QString::fromLatin1(IWDIPC::interface), QStringLiteral("GetSnapshot"));
        const QDBusReply<QVariantMap> reply = QDBusConnection::systemBus().call(request);
        QVERIFY(reply.isValid());
        const auto json = QJsonDocument::fromVariant(IWDIPC::unpack(reply.value())).toJson();
        QVERIFY(!json.contains("first-test-secret") && !json.contains("second-test-secret"));
    }

    void credentialPromptsAndRepliesBelongToTheirRequestingClient()
    {
        FakeIwd iwd;
        iwd.station.connected = iwd.network.connected = false;
        QVERIFY(iwd.start());
        startBroker();
        IWD first, second;
        QTRY_COMPARE(first.networks()->rowCount(), 1);
        QTRY_COMPARE(second.networks()->rowCount(), 1);
        QSignalSpy firstPrompt(&first, &IWD::credentialsRequested), secondPrompt(&second, &IWD::credentialsRequested);
        QSignalSpy result(&first, &IWD::operationFinished);
        const QString id = first.connectNetwork(QStringLiteral("74657374_psk"));
        QTRY_COMPARE(firstPrompt.count(), 1);
        QCOMPARE(secondPrompt.count(), 0);
        second.provideCredentials(id, QStringLiteral("wrong-client"));
        QTest::qWait(50);
        QCOMPARE(result.count(), 0);
        first.provideCredentials(id, QStringLiteral("right-client"));
        QTRY_VERIFY(succeeded(result, id));
        QCOMPARE(iwd.network.credentials, (QStringList{"right-client"}));
    }

    void disappearingClientCancelsItsPromptAndUnblocksNextRequest()
    {
        FakeIwd iwd;
        iwd.station.connected = iwd.network.connected = false;
        QVERIFY(iwd.start());
        startBroker();
        auto first = std::make_unique<IWD>();
        IWD second;
        QTRY_COMPARE(first->networks()->rowCount(), 1);
        QTRY_COMPARE(second.networks()->rowCount(), 1);
        QSignalSpy prompt(first.get(), &IWD::credentialsRequested);
        first->connectNetwork(QStringLiteral("74657374_psk"));
        QTRY_COMPARE(prompt.count(), 1);
        QSignalSpy result(&second, &IWD::operationFinished);
        second.preparePassword(QStringLiteral("74657374_psk"), QStringLiteral("next-client"));
        const QString id = second.connectNetwork(QStringLiteral("74657374_psk"));
        first.reset();
        QTRY_VERIFY(succeeded(result, id));
        QCOMPARE(iwd.network.credentials, (QStringList{"next-client"}));
    }

    void daemonRestartClearsAndRestoresEveryClient()
    {
        FakeIwd iwd;
        QVERIFY(iwd.start());
        startBroker();
        IWD first, second;
        QTRY_COMPARE(first.networks()->rowCount(), 1);
        QTRY_COMPARE(second.networks()->rowCount(), 1);
        stopBroker();
        QTRY_VERIFY(!first.available() && !second.available());
        QCOMPARE(first.networks()->rowCount(), 0);
        QCOMPARE(second.networks()->rowCount(), 0);
        QVERIFY(!first.connected() && !second.connected());
        iwd.station.registeredSignalPath.clear();
        startBroker();
        QTRY_VERIFY(first.available() && second.available());
        QTRY_COMPARE(first.networks()->rowCount(), 1);
        QTRY_COMPARE(second.networks()->rowCount(), 1);
    }

    void knownNetworksSignalLevelAndErrorsReachClients()
    {
        FakeIwd iwd;
        iwd.station.connected = iwd.network.connected = false;
        QVERIFY(iwd.start());
        startBroker();
        IWD client;
        // Level 0 is IWD's strongest; nothing connected must not look like that.
        QCOMPARE(client.signalStrengthLevel(), 7);
        QTRY_COMPARE(client.networks()->rowCount(), 1);
        QTRY_COMPARE(client.knownNetworks().size(), 1);
        QCOMPARE(client.knownNetworks().at(0).toMap().value("networkId").toString(), QStringLiteral("74657374_psk"));
        QCOMPARE(client.signalStrengthLevel(), 7);
        QSignalSpy errors(&client, &IWD::error);
        QSignalSpy results(&client, &IWD::operationFinished);
        client.setAutoConnectNetwork(QStringLiteral("does-not-exist"), false);
        QTRY_COMPARE(results.count(), 1);
        QCOMPARE(errors.count(), 1);
        QCOMPARE(errors.at(0).at(0).value<IWD::Error>(), IWD::SetAutoConnectFailed);
    }

    void changesArriveAsDeltas()
    {
        FakeIwd iwd;
        QVERIFY(iwd.start());
        startBroker();
        IWD client;
        QTRY_COMPARE(client.networks()->rowCount(), 1);

        const auto introspect = QDBusMessage::createMethodCall(QString::fromLatin1(IWDIPC::service), QString::fromLatin1(IWDIPC::path),
            QStringLiteral("org.freedesktop.DBus.Introspectable"), QStringLiteral("Introspect"));
        const QDBusReply<QString> xml = QDBusConnection::systemBus().call(introspect);
        QVERIFY(xml.isValid());
        QVERIFY(xml.value().contains(QStringLiteral("SnapshotDelta")));
        QVERIFY(!xml.value().contains(QStringLiteral("SnapshotChanged")));

        DeltaRecorder recorder;
        QVERIFY(QDBusConnection::systemBus().connect(QString::fromLatin1(IWDIPC::service), QString::fromLatin1(IWDIPC::path),
            QString::fromLatin1(IWDIPC::interface), QStringLiteral("SnapshotDelta"), &recorder, SLOT(record(QVariantMap))));
        iwd.network.name = QStringLiteral("Renamed network");
        iwd.interfacesAdded(networkPath, {QStringLiteral("net.connman.iwd.Network")});
        QTRY_COMPARE(client.connectedNetworkData().value("ssid").toString(), QStringLiteral("Renamed network"));
        QVERIFY(!recorder.deltas.isEmpty());
        for (const auto &delta : std::as_const(recorder.deltas)) {
            QCOMPARE(delta.value("baseRevision").toULongLong() + 1, delta.value("revision").toULongLong());
            // Only the device's changed keys travel; inventory is unchanged.
            QVERIFY(delta.value("changes").toMap().isEmpty());
            QVERIFY(delta.value("replacedDevices").toMap().isEmpty());
            const auto device = delta.value("deviceChanges").toMap().value(stationPath).toMap();
            QVERIFY(!device.isEmpty());
            QVERIFY(!device.contains("adapters") && !device.contains("model") && !device.contains("knownNetworks"));
        }
    }

    void missedDeltaResynchronises()
    {
        FakeIwd iwd;
        QVERIFY(iwd.start());
        startBroker();
        IWD client;
        QTRY_COMPARE(client.networks()->rowCount(), 1);
        const QStringList devices = client.devices();
        QObject *implementation = nullptr;
        for (QObject *child : client.children()) {
            if (child->metaObject()->indexOfSlot("onSnapshotDelta(QVariantMap)") >= 0) implementation = child;
        }
        QVERIFY(implementation);
        const auto request = QDBusMessage::createMethodCall(QString::fromLatin1(IWDIPC::service), QString::fromLatin1(IWDIPC::path),
            QString::fromLatin1(IWDIPC::interface), QStringLiteral("GetSnapshot"));
        const QDBusReply<QVariantMap> snapshot = QDBusConnection::systemBus().call(request);
        QVERIFY(snapshot.isValid());
        const auto revision = snapshot.value().value("revision").toULongLong();
        // A delta that skips revisions must not be applied on top of stale state.
        const QVariantMap gap{{"version", IWDIPC::version}, {"epoch", snapshot.value().value("epoch")},
                              {"revision", QVariant::fromValue(revision + 5)}, {"baseRevision", QVariant::fromValue(revision + 4)},
                              {"changes", QVariantMap{{"devices", QStringList{"/bogus"}}}}};
        QSignalSpy devicesChanged(&client, &IWD::devicesChanged);
        QVERIFY(QMetaObject::invokeMethod(implementation, "onSnapshotDelta", Qt::DirectConnection, Q_ARG(QVariantMap, gap)));
        QTest::qWait(200);
        QCOMPARE(client.devices(), devices);
        QCOMPARE(devicesChanged.count(), 0);
        QCOMPARE(client.networks()->rowCount(), 1);
    }

    void cleanup() { if (broker.state() != QProcess::NotRunning) stopBroker(); }
};

QTEST_GUILESS_MAIN(IWDBrokerTest)
#include "iwd_broker_test.moc"
