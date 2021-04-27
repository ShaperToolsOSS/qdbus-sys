// Copyright (C) 2026 Shaper Tools
// SPDX-License-Identifier: LGPL-2.1-only

#include "iwd-model.h"
#include "iwd.h"

#include <QAbstractItemModelTester>
#include <QSignalSpy>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QMetaProperty>
#include <memory>
#include <QtTest>

using QDBusSys::IWD;
using QDBusSys::IWDNetworkModel;

class IWDModelTest : public QObject
{
    Q_OBJECT

private slots:
    void listHasNoChildRows()
    {
        IWDNetworkModel model;
        QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::QtTest);
        const QVariantList rows = {QVariantMap{{"networkId", "one"}}, QVariantMap{{"networkId", "two"}}};
        QVERIFY(QMetaObject::invokeMethod(&model, "applyNetworkSnapshot", Qt::DirectConnection,
                                          Q_ARG(QVariantList, rows)));
        QCOMPARE(model.rowCount(), 2);
        QCOMPARE(model.rowCount(model.index(0, 0)), 0);
        QCOMPARE(model.rowCount(model.index(1, 0)), 0);
    }

    void stateOwnsNetworkModel()
    {
        QPointer<IWDNetworkModel> networks;
        {
            IWD state;
            networks = state.networks();
            QVERIFY(networks);
            QCOMPARE(networks->parent(), &state);
            QCOMPARE(networks->thread(), state.thread());
            const QMetaProperty property = state.metaObject()->property(state.metaObject()->indexOfProperty("networks"));
            QVERIFY(property.isConstant());
            QVERIFY(!property.isWritable());

            const QVariantList rows = {QVariantMap{{"networkId", "one"}, {"ssid", "Test network"}}};
            QVERIFY(QMetaObject::invokeMethod(networks, "applyNetworkSnapshot", Qt::DirectConnection,
                                              Q_ARG(QVariantList, rows)));
            QCOMPARE(state.networks(), networks.data());
            QCOMPARE(networks->data(networks->index(0, 0), IWDNetworkModel::SSIDRole).toString(), QStringLiteral("Test network"));
            QVERIFY(!state.connected());
        }
        QVERIFY(networks.isNull());
    }

    void networkUpdatesPreserveModelContract()
    {
        IWDNetworkModel model;
        QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::QtTest);
        QSignalSpy resets(&model, &QAbstractItemModel::modelReset);
        QSignalSpy moves(&model, &QAbstractItemModel::rowsMoved);
        QSignalSpy updates(&model, &QAbstractItemModel::dataChanged);
        const QVariantList first = {QVariantMap{{"networkId", "one"}, {"ssid", "One"}},
                                    QVariantMap{{"networkId", "two"}, {"ssid", "Two"}}};
        QVERIFY(QMetaObject::invokeMethod(&model, "applyNetworkSnapshot", Qt::DirectConnection,
                                          Q_ARG(QVariantList, first)));
        const QVariantList reordered = {first.at(1), QVariantMap{{"networkId", "one"}, {"ssid", "Updated"}}};
        QVERIFY(QMetaObject::invokeMethod(&model, "applyNetworkSnapshot", Qt::DirectConnection,
                                          Q_ARG(QVariantList, reordered)));
        QCOMPARE(moves.count(), 1);
        QCOMPARE(updates.count(), 1);
        QCOMPARE(model.data(model.index(1, 0), IWDNetworkModel::SSIDRole).toString(), QStringLiteral("Updated"));
        const QVariantList last = {reordered.at(0)};
        QVERIFY(QMetaObject::invokeMethod(&model, "applyNetworkSnapshot", Qt::DirectConnection,
                                          Q_ARG(QVariantList, last)));
        QCOMPARE(model.rowCount(), 1);
        QCOMPARE(resets.count(), 0);
    }

    void qmlCanAccessNestedModel()
    {
        QQmlEngine engine;
        QQmlComponent component(&engine);
        component.setData(R"(
            import DBusSysIWD
            IWD {
                property var networkList: networks
                property int networkCount: networks.length
                property int adapterCount: adapters.length
                property int deviceCount: devices.length
            }
        )", QUrl(QStringLiteral("inmemory:/iwd-test.qml")));
        QTRY_VERIFY(component.status() != QQmlComponent::Loading);
        std::unique_ptr<QObject> object(component.create());
        QVERIFY2(object, qPrintable(component.errorString()));
        auto *state = qobject_cast<IWD *>(object.get());
        QVERIFY(state);
        QCOMPARE(object->property("networkList").value<QObject *>(), state->networks());
        QCOMPARE(object->property("networkCount").toInt(), 0);
        QCOMPARE(object->property("adapterCount").toInt(), 0);
        QCOMPARE(object->property("deviceCount").toInt(), 0);

        QVERIFY(QMetaObject::invokeMethod(state, "applyAdapterSnapshot", Qt::DirectConnection,
            Q_ARG(QStringList, QStringList{"/adapter/one"}),
            Q_ARG(QStringList, QStringList{"/adapter/one/device/one"}),
            Q_ARG(QStringList, QStringList()), Q_ARG(QString, QString()),
            Q_ARG(QString, QString()), Q_ARG(QString, QString()), Q_ARG(bool, true)));
        QCOMPARE(object->property("adapterCount").toInt(), 1);
        QCOMPARE(object->property("deviceCount").toInt(), 1);

        const QVariantList rows = {QVariantMap{{"networkId", "one"}}};
        QVERIFY(QMetaObject::invokeMethod(state->networks(), "applyNetworkSnapshot", Qt::DirectConnection,
                                          Q_ARG(QVariantList, rows)));
        QCOMPARE(object->property("networkCount").toInt(), 1);
    }
};

QTEST_GUILESS_MAIN(IWDModelTest)
#include "iwd_model_test.moc"
