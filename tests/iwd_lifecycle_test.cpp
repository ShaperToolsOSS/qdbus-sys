// Copyright (C) 2026 Shaper Tools
// SPDX-License-Identifier: LGPL-2.1-only

#include "iwd-controller.h"
#include "dbus-interface-common.h"

#include <QtDBus>
#include <QtTest>
#include <QUuid>
#include <QDomDocument>

#include "mock_iwd.h"

class IWDLifecycleTest : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase()
    {
        // This executable must never claim IWD's name on the host system bus.
        QVERIFY(!qgetenv("DBUS_SESSION_BUS_ADDRESS").isEmpty());
        QCOMPARE(qgetenv("DBUS_SYSTEM_BUS_ADDRESS"), qgetenv("DBUS_SESSION_BUS_ADDRESS"));
        qDBusRegisterMetaType<ManagedObjectList>();
        qDBusRegisterMetaType<InterfacePropertyMap>();
        qDBusRegisterMetaType<OrderedNetworksList>();
        qDBusRegisterMetaType<LevelsList>();
    }

    void summaryRecoversWhenNetworkArrivesLate()
    {
        FakeIwd daemon;
        daemon.station.includeNetwork = false;
        QVERIFY(daemon.start());
        IWDController state;
        QTRY_VERIFY(state.connected());
        QCOMPARE(state.networkRows().size(), 0);
        QCOMPARE(state.connectedNetworkData().value("networkId").toString(), QStringLiteral("74657374_psk"));
        daemon.station.includeNetwork = true;
        daemon.interfacesAdded(networkPath, {QStringLiteral("net.connman.iwd.Network")});
        QTRY_COMPARE(state.networkRows().size(), 1);
        QTRY_COMPARE(state.connectedNetworkData().value("ssid").toString(), QStringLiteral("Test network"));
    }

    void ownerLossClearsStateAndReplacementRecovers()
    {
        FakeIwd daemon;
        QVERIFY(daemon.start());
        IWDController state;
        QTRY_COMPARE(state.networkRows().size(), 1);
        QTRY_VERIFY(state.connected() && state.powered());
        QVERIFY(daemon.bus.unregisterService(service));
        QTRY_VERIFY(!state.connected() && !state.powered());
        QTRY_COMPARE(state.networkRows().size(), 0);
        QVERIFY(state.adapters().isEmpty());
        QVERIFY(state.devices().isEmpty());
        QVERIFY(state.connectedNetworkData().isEmpty());
        daemon.station.registeredSignalPath.clear();
        QVERIFY(daemon.bus.registerService(service));
        QTRY_COMPARE(state.networkRows().size(), 1);
        QTRY_COMPARE(state.connectedNetworkData().value("ssid").toString(), QStringLiteral("Test network"));
        QTRY_VERIFY(!daemon.station.registeredSignalPath.isEmpty());
        QCOMPARE(daemon.station.duplicates, 0);
    }

    void hotplugCoalescesDiscoveryWithoutReplacingAgents()
    {
        FakeIwd daemon;
        QVERIFY(daemon.start());
        IWDController state;
        QTRY_COMPARE(state.networkRows().size(), 1);
        QTRY_COMPARE(daemon.station.registrations, 1);
        daemon.root.delayMs = 100;
        daemon.network.name = QStringLiteral("Updated network");
        daemon.interfacesAdded(stationPath, {QStringLiteral("net.connman.iwd.Device")});
        QTRY_COMPARE(daemon.root.activeRequests, 1);
        for (int i = 0; i < 30; ++i) {
            daemon.interfacesAdded(stationPath, {QStringLiteral("net.connman.iwd.Device")});
        }
        QTRY_VERIFY(daemon.root.requests >= 3 && daemon.root.activeRequests == 0);
        QTRY_COMPARE(state.connectedNetworkData().value("ssid").toString(), QStringLiteral("Updated network"));
        QCOMPARE(daemon.root.maxActiveRequests, 1);
        QCOMPARE(daemon.station.registrations, 1);
        QCOMPARE(daemon.station.duplicates, 0);
        QCOMPARE(state.networkRows().size(), 1);
    }

    void repeatedStationReplacementRestoresSummaryAndAgent()
    {
        FakeIwd daemon;
        QVERIFY(daemon.start());
        IWDController state;
        QSignalSpy errors(&state, &IWDController::error);
        QTRY_COMPARE(state.networkRows().size(), 1);
        QTRY_VERIFY(!daemon.station.registeredSignalPath.isEmpty());
        for (int i = 0; i < 3; ++i) {
            daemon.removeStation();
            QTRY_VERIFY(!state.connected() && !state.powered());
            QTRY_COMPARE(state.networkRows().size(), 0);
            QVERIFY(daemon.restoreStation());
            QTRY_COMPARE(state.connectedNetworkData().value("ssid").toString(), QStringLiteral("Test network"));
            QTRY_VERIFY(!daemon.station.registeredSignalPath.isEmpty());
        }
        QCOMPARE(daemon.station.duplicates, 0);
        QCOMPARE(errors.count(), 0);
    }

    void anotherClientsSignalAgentUsesScannedSignalStrength()
    {
        FakeIwd daemon;
        daemon.station.registeredSignalPath = QStringLiteral("/another/client/agent");
        QVERIFY(daemon.start());
        IWDController state;
        QSignalSpy errors(&state, &IWDController::error);
        QTRY_COMPARE(state.networkRows().size(), 1);
        QTRY_COMPARE(daemon.station.duplicates, 1);
        // -50 dBm crosses the -30 and -40 dBm thresholds.
        QTRY_COMPARE(state.signalStrengthLevel(), 2);
        QCOMPARE(errors.count(), 0);
        QCOMPARE(daemon.station.registeredSignalPath, QStringLiteral("/another/client/agent"));
    }

    void knownNetworkChangesUpdateRowsWithoutPolling()
    {
        FakeIwd daemon;
        QVERIFY(daemon.start());
        IWDController state;
        QTRY_COMPARE(state.networkRows().size(), 1);
        QVERIFY(state.networkRows().at(0).toMap().value("autoconnect").toBool());
        QCOMPARE(state.knownNetworks().size(), 1);
        daemon.known.autoconnect = false;
        daemon.propertiesChanged(knownPath, QStringLiteral("net.connman.iwd.KnownNetwork"), {{"AutoConnect", false}});
        QTRY_VERIFY(!state.networkRows().at(0).toMap().value("autoconnect").toBool());
        QTRY_VERIFY(!state.knownNetworks().at(0).toMap().value("autoconnect").toBool());
        QCOMPARE(state.knownNetworks().at(0).toMap().value("ssid").toString(), QStringLiteral("Test network"));
    }

    void outOfRangeKnownNetworkCanBeForgotten()
    {
        FakeIwd daemon;
        daemon.station.includeNetwork = false;
        daemon.station.connected = false;
        QVERIFY(daemon.start());
        IWDController state;
        QTRY_COMPARE(state.knownNetworks().size(), 1);
        QCOMPARE(state.networkRows().size(), 0);
        QCOMPARE(state.signalStrengthLevel(), 7);
        QSignalSpy completed(&state, &IWDController::operationCompleted);
        state.executeOperation(QStringLiteral("forget-1"), QStringLiteral("forget"), {{"networkId", "74657374_psk"}});
        QTRY_COMPARE(completed.count(), 1);
        QVERIFY(completed.at(0).at(2).toBool());
        QCOMPARE(daemon.known.forgets, 1);
    }

    void credentialAgentAnswersOnlyIwdWithCorrectSignatures()
    {
        FakeIwd daemon;
        QVERIFY(daemon.start());
        IWDController state;
        QTRY_COMPARE(daemon.agents.paths.size(), 1);
        const QString owner = daemon.agents.paths.constBegin().key();
        const QString path = daemon.agents.paths.value(owner);

        auto introspect = QDBusMessage::createMethodCall(owner, path, QStringLiteral("org.freedesktop.DBus.Introspectable"),
                                                         QStringLiteral("Introspect"));
        const QDBusReply<QString> xml = daemon.bus.call(introspect);
        QVERIFY(xml.isValid());
        QDomDocument document;
        QVERIFY(document.setContent(xml.value()));
        QHash<QString, QStringList> signatures;
        const auto methods = document.elementsByTagName(QStringLiteral("method"));
        for (int i = 0; i < methods.size(); ++i) {
            const auto method = methods.at(i).toElement();
            const auto arguments = method.elementsByTagName(QStringLiteral("arg"));
            // Qt lists the return value first; compare inputs, then outputs.
            QStringList inputs, outputs;
            for (int j = 0; j < arguments.size(); ++j) {
                const auto argument = arguments.at(j).toElement();
                const bool output = argument.attribute(QStringLiteral("direction")) == QLatin1String("out");
                (output ? outputs : inputs) << (output ? "out:" : "in:") + argument.attribute(QStringLiteral("type"));
            }
            const QStringList signature = inputs + outputs;
            signatures.insert(method.attribute(QStringLiteral("name")), signature);
        }
        QCOMPARE(signatures.value("RequestPassphrase"), (QStringList{"in:o", "out:s"}));
        QCOMPARE(signatures.value("RequestPrivateKeyPassphrase"), (QStringList{"in:o", "out:s"}));
        QCOMPARE(signatures.value("RequestUserNameAndPassword"), (QStringList{"in:o", "out:s", "out:s"}));
        QCOMPARE(signatures.value("RequestUserPassword"), (QStringList{"in:o", "in:s", "out:s"}));
        QCOMPARE(signatures.value("Cancel"), (QStringList{"in:s"}));

        auto request = QDBusMessage::createMethodCall(owner, path, QStringLiteral("net.connman.iwd.Agent"),
                                                      QStringLiteral("RequestUserNameAndPassword"));
        request << QVariant::fromValue(QDBusObjectPath(networkPath));
        const auto canceled = daemon.bus.call(request);
        QCOMPARE(canceled.errorName(), QStringLiteral("net.connman.iwd.Agent.Error.Canceled"));
        const auto denied = QDBusConnection::systemBus().call(request);
        QCOMPARE(denied.errorName(), QStringLiteral("org.freedesktop.DBus.Error.AccessDenied"));
    }
};

QTEST_GUILESS_MAIN(IWDLifecycleTest)
#include "iwd_lifecycle_test.moc"
