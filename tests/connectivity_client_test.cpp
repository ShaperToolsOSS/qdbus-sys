// Copyright (C) 2026 Shaper Tools
// SPDX-License-Identifier: LGPL-2.1-only

#include "qdbus-sys-connectivityd.h"

#include <QSignalSpy>
#include <QQmlComponent>
#include <QQmlEngine>
#include <memory>
#include <QtTest>

using QDBusSys::Connectivity;

class ConnectivityClientTest : public QObject
{
    Q_OBJECT

private slots:
    void qmlCanCreateClient()
    {
        QQmlEngine engine;
        QQmlComponent component(&engine);
        component.setData("import DBusSysConnectivity\nConnectivity {\n"
                          "    property int endpointAccessibleLevel: Connectivity.EndpointAccessible\n}",
                          QUrl(QStringLiteral("inmemory:/connectivity-test.qml")));
        QTRY_VERIFY(component.status() != QQmlComponent::Loading);
        std::unique_ptr<QObject> object(component.create());
        QVERIFY2(object, qPrintable(component.errorString()));
        QVERIFY(qobject_cast<Connectivity *>(object.get()));
        QCOMPARE(object->property("endpointAccessibleLevel").toInt(), int(Connectivity::EndpointAccessible));
    }

    void absentDaemonHasSafeDefaults()
    {
        Connectivity client;
        QCOMPARE(client.connected(), false);
        QCOMPARE(client.checkingConnectivity(), false);
        QCOMPARE(client.connectivityLevel(), Connectivity::InterfaceDisconnected);
    }

    void stateIsReadOnlyFromQml()
    {
        QQmlEngine engine;
        QQmlComponent component(&engine);
        component.setData("import DBusSysConnectivity\nConnectivity { connected: true }",
                          QUrl(QStringLiteral("inmemory:/connectivity-write.qml")));
        QTRY_VERIFY(component.status() != QQmlComponent::Loading);
        QVERIFY(component.isError());
        QVERIFY2(component.errorString().contains(QStringLiteral("read-only")), qPrintable(component.errorString()));
        const QMetaObject *meta = &Connectivity::staticMetaObject;
        for (const char *name : {"connected", "checkingConnectivity", "connectivityLevel"}) {
            QVERIFY2(!meta->property(meta->indexOfProperty(name)).isWritable(), name);
        }
    }

    void propertiesChangedUpdatesStateWithoutFurtherCalls()
    {
        Connectivity client;
        QSignalSpy connected(&client, &Connectivity::connectedChanged);
        QSignalSpy level(&client, &Connectivity::connectivityLevelChanged);
        QObject *implementation = client.findChild<QObject *>(QString(), Qt::FindDirectChildrenOnly);
        QVERIFY(implementation);
        auto changed = [&](const QVariantMap &properties) {
            return QMetaObject::invokeMethod(implementation, "onPropertiesChanged", Qt::DirectConnection,
                Q_ARG(QString, QStringLiteral("com.shapertools.qdbussys.connectivityd1")),
                Q_ARG(QVariantMap, properties), Q_ARG(QStringList, QStringList()));
        };
        QVERIFY(changed({{"connected", true}, {"connectivityLevel", 0}}));
        QVERIFY(client.connected());
        QCOMPARE(client.connectivityLevel(), Connectivity::EndpointAccessible);
        QVERIFY(changed({{"connected", true}}));
        QCOMPARE(connected.count(), 1);
        QCOMPARE(level.count(), 1);
    }

    void losingDaemonResetsState()
    {
        Connectivity client;
        QObject *implementation = client.findChild<QObject *>(QString(), Qt::FindDirectChildrenOnly);
        QVERIFY(implementation);
        QVERIFY(QMetaObject::invokeMethod(implementation, "onPropertiesChanged", Qt::DirectConnection,
            Q_ARG(QString, QStringLiteral("com.shapertools.qdbussys.connectivityd1")),
            Q_ARG(QVariantMap, (QVariantMap{{"connected", true}, {"checkingConnectivity", true}, {"connectivityLevel", 0}})),
            Q_ARG(QStringList, QStringList())));
        QSignalSpy connected(&client, &Connectivity::connectedChanged);
        QSignalSpy checking(&client, &Connectivity::checkingConnectivityChanged);
        QSignalSpy level(&client, &Connectivity::connectivityLevelChanged);
        auto unregisterService = [&](const QString &service) {
            return QMetaObject::invokeMethod(implementation, "onServiceUnregistered",
                                              Qt::DirectConnection, Q_ARG(QString, service));
        };
        QVERIFY(unregisterService(QStringLiteral("unrelated.service")));
        QVERIFY(client.connected());
        QCOMPARE(connected.count(), 0);

        QVERIFY(unregisterService(QStringLiteral("com.shapertools.qdbussys.connectivityd")));
        QVERIFY(!client.connected());
        QVERIFY(!client.checkingConnectivity());
        QCOMPARE(client.connectivityLevel(), Connectivity::InterfaceDisconnected);
        QCOMPARE(connected.count(), 1);
        QCOMPARE(checking.count(), 1);
        QCOMPARE(level.count(), 1);

        QVERIFY(unregisterService(QStringLiteral("com.shapertools.qdbussys.connectivityd")));
        QCOMPARE(connected.count(), 1);
        QCOMPARE(checking.count(), 1);
        QCOMPARE(level.count(), 1);
    }
};

QTEST_GUILESS_MAIN(ConnectivityClientTest)
#include "connectivity_client_test.moc"
