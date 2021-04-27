// Copyright (C) 2026 Shaper Tools
// SPDX-License-Identifier: LGPL-2.1-only

#include "qsystemd-networkd.h"

#include <QDBusVariant>
#include <QMetaProperty>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QSignalSpy>
#include <QtTest>

using QDBusSys::SystemdNetworkd;

class NetworkdStateTest : public QObject
{
    Q_OBJECT

    static bool publish(SystemdNetworkd &networkd, const QVariantMap &properties)
    {
        for (QObject *child : networkd.children()) {
            if (child->metaObject()->indexOfSlot("onManagerPropertiesChanged(QString,QVariantMap,QStringList)") >= 0) {
                return QMetaObject::invokeMethod(child, "onManagerPropertiesChanged", Qt::DirectConnection,
                    Q_ARG(QString, QStringLiteral("org.freedesktop.network1.Manager")),
                    Q_ARG(QVariantMap, properties), Q_ARG(QStringList, QStringList()));
            }
        }
        return false;
    }

private slots:
    void stateMapping_data()
    {
        QTest::addColumn<QByteArray>("property");
        QTest::addColumn<QString>("wireValue");
        QTest::addColumn<QByteArray>("enumKey");
        QTest::newRow("operationalState-missing") << QByteArray("operationalState") << QStringLiteral("missing") << QByteArray("Missing");
        QTest::newRow("operationalState-off") << QByteArray("operationalState") << QStringLiteral("off") << QByteArray("Off");
        QTest::newRow("operationalState-failed") << QByteArray("operationalState") << QStringLiteral("failed") << QByteArray("Failed");
        QTest::newRow("operationalState-no-carrier") << QByteArray("operationalState") << QStringLiteral("no-carrier") << QByteArray("NoCarrier");
        QTest::newRow("operationalState-dormant") << QByteArray("operationalState") << QStringLiteral("dormant") << QByteArray("Dormant");
        QTest::newRow("operationalState-degraded-carrier") << QByteArray("operationalState") << QStringLiteral("degraded-carrier") << QByteArray("DegradedCarrier");
        QTest::newRow("operationalState-carrier") << QByteArray("operationalState") << QStringLiteral("carrier") << QByteArray("Carrier");
        QTest::newRow("operationalState-enslaved") << QByteArray("operationalState") << QStringLiteral("enslaved") << QByteArray("Enslaved");
        QTest::newRow("operationalState-degraded") << QByteArray("operationalState") << QStringLiteral("degraded") << QByteArray("Degraded");
        QTest::newRow("operationalState-routable") << QByteArray("operationalState") << QStringLiteral("routable") << QByteArray("Routable");
        QTest::newRow("operationalState-empty") << QByteArray("operationalState") << QStringLiteral("") << QByteArray("Unknown");
        QTest::newRow("operationalState-future") << QByteArray("operationalState") << QStringLiteral("future-state") << QByteArray("Unknown");
        QTest::newRow("carrierState-off") << QByteArray("carrierState") << QStringLiteral("off") << QByteArray("Off");
        QTest::newRow("carrierState-no-carrier") << QByteArray("carrierState") << QStringLiteral("no-carrier") << QByteArray("NoCarrier");
        QTest::newRow("carrierState-dormant") << QByteArray("carrierState") << QStringLiteral("dormant") << QByteArray("Dormant");
        QTest::newRow("carrierState-degraded-carrier") << QByteArray("carrierState") << QStringLiteral("degraded-carrier") << QByteArray("DegradedCarrier");
        QTest::newRow("carrierState-carrier") << QByteArray("carrierState") << QStringLiteral("carrier") << QByteArray("Carrier");
        QTest::newRow("carrierState-enslaved") << QByteArray("carrierState") << QStringLiteral("enslaved") << QByteArray("Enslaved");
        QTest::newRow("carrierState-empty") << QByteArray("carrierState") << QStringLiteral("") << QByteArray("Unknown");
        QTest::newRow("carrierState-future") << QByteArray("carrierState") << QStringLiteral("future-state") << QByteArray("Unknown");
        QTest::newRow("addressState-off") << QByteArray("addressState") << QStringLiteral("off") << QByteArray("Off");
        QTest::newRow("addressState-degraded") << QByteArray("addressState") << QStringLiteral("degraded") << QByteArray("Degraded");
        QTest::newRow("addressState-routable") << QByteArray("addressState") << QStringLiteral("routable") << QByteArray("Routable");
        QTest::newRow("addressState-empty") << QByteArray("addressState") << QStringLiteral("") << QByteArray("Unknown");
        QTest::newRow("addressState-future") << QByteArray("addressState") << QStringLiteral("future-state") << QByteArray("Unknown");
        QTest::newRow("onlineState-unknown") << QByteArray("onlineState") << QStringLiteral("unknown") << QByteArray("Unknown");
        QTest::newRow("onlineState-offline") << QByteArray("onlineState") << QStringLiteral("offline") << QByteArray("Offline");
        QTest::newRow("onlineState-partial") << QByteArray("onlineState") << QStringLiteral("partial") << QByteArray("Partial");
        QTest::newRow("onlineState-online") << QByteArray("onlineState") << QStringLiteral("online") << QByteArray("Online");
        QTest::newRow("onlineState-empty") << QByteArray("onlineState") << QStringLiteral("") << QByteArray("Unknown");
        QTest::newRow("onlineState-future") << QByteArray("onlineState") << QStringLiteral("future-state") << QByteArray("Unknown");
    }

    void stateMapping()
    {
        QFETCH(QByteArray, property);
        QFETCH(QString, wireValue);
        QFETCH(QByteArray, enumKey);
        SystemdNetworkd networkd;
        const QMetaProperty metaProperty = networkd.metaObject()->property(networkd.metaObject()->indexOfProperty(property));
        QVERIFY(metaProperty.isEnumType());
        QCOMPARE(metaProperty.read(&networkd).toInt(), metaProperty.enumerator().keyToValue("Unknown"));
        QSignalSpy spy(&networkd, metaProperty.notifySignal());
        const QString wireProperty = QString(QChar::fromLatin1(property.at(0))).toUpper() + QString::fromLatin1(property.mid(1));
        // Exercise both plain and wrapped D-Bus variants and real publication.
        QVERIFY(publish(networkd, {{wireProperty, QVariant::fromValue(QDBusVariant(wireValue))}}));
        QCOMPARE(metaProperty.read(&networkd).toInt(), metaProperty.enumerator().keyToValue(enumKey));
        const int expectedSignals = wireValue.isEmpty() || enumKey == "Unknown" ? 0 : 1;
        QCOMPARE(spy.count(), expectedSignals);
        QVERIFY(publish(networkd, {{wireProperty, wireValue}}));
        QCOMPARE(spy.count(), expectedSignals);
        // An unrelated partial update must retain this property's value.
        QVERIFY(publish(networkd, {{QStringLiteral("Unrelated"), QStringLiteral("off")}}));
        QCOMPARE(metaProperty.read(&networkd).toInt(), metaProperty.enumerator().keyToValue(enumKey));
        QVERIFY(publish(networkd, {{wireProperty, QStringLiteral("future-state")}}));
        QCOMPARE(metaProperty.read(&networkd).toInt(), metaProperty.enumerator().keyToValue("Unknown"));
        QCOMPARE(spy.count(), expectedSignals * 2);
    }

    void stateIsReadOnly()
    {
        SystemdNetworkd networkd;
        const QMetaObject *meta = networkd.metaObject();
        for (const char *name : {"operationalState", "carrierState", "addressState", "onlineState", "linkInterfaces"}) {
            QVERIFY2(!meta->property(meta->indexOfProperty(name)).isWritable(), name);
        }
    }

    void managerValuesArePublishedAsIs()
    {
        // networkd's Manager applies RequiredForOnline when aggregating links;
        // a "partial" manager state must not be promoted to "online".
        SystemdNetworkd networkd;
        QVERIFY(publish(networkd, {{QStringLiteral("OnlineState"), QStringLiteral("partial")},
                                   {QStringLiteral("AddressState"), QStringLiteral("degraded")}}));
        QCOMPARE(networkd.onlineState(), SystemdNetworkd::OnlineState::Partial);
        QCOMPARE(networkd.addressState(), SystemdNetworkd::AddressState::Degraded);
    }

    void qmlEnums()
    {
        QQmlEngine engine;
        QQmlComponent component(&engine);
        component.setData(R"(
            import DBusSysSystemdNetworkd
            SystemdNetworkd {
                property bool enumsWork:
                    operationalState === SystemdNetworkd.OperationalState.Unknown &&
                    carrierState === SystemdNetworkd.CarrierState.Unknown &&
                    addressState === SystemdNetworkd.AddressState.Unknown &&
                    onlineState === SystemdNetworkd.OnlineState.Unknown
                property string onlineName: Qt.enumValueToString(SystemdNetworkd.OnlineState, onlineState)
            }
        )", QUrl());
        QScopedPointer<QObject> object(component.create());
        QVERIFY2(object, qPrintable(component.errorString()));
        QVERIFY(object->property("enumsWork").toBool());
        QCOMPARE(object->property("onlineName").toString(), QStringLiteral("Unknown"));
        auto *networkd = qobject_cast<SystemdNetworkd *>(object.data());
        QVERIFY(networkd);
        QVERIFY(publish(*networkd, {{QStringLiteral("OnlineState"), QStringLiteral("online")}}));
        QVERIFY(!object->property("enumsWork").toBool());
        QCOMPARE(object->property("onlineName").toString(), QStringLiteral("Online"));
    }
};

QTEST_GUILESS_MAIN(NetworkdStateTest)
#include "networkd_state_test.moc"
