// Copyright (C) 2026 Shaper Tools
// SPDX-License-Identifier: LGPL-2.1-only

#include <QCoreApplication>
#include <QFile>
#include <QTimer>
#include <QtDBus>

class ActivationService : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "com.shapertools.qdbussys.connectivityd1")
    Q_PROPERTY(bool connected READ connected)
    Q_PROPERTY(bool checkingConnectivity READ checkingConnectivity)
    Q_PROPERTY(int connectivityLevel READ connectivityLevel)
public:
    explicit ActivationService(QString eventsPath) : eventsPath(std::move(eventsPath)) {}
    bool connected() const { return true; }
    bool checkingConnectivity() const { return false; }
    int connectivityLevel() const { return 0; }

    void record(const QByteArray &event)
    {
        QFile events(eventsPath);
        if (events.open(QIODevice::Append)) {
            events.write(event + '\n');
        }
    }

public slots:
    void checkConnectivity() { record("check"); }
    void quit() { QTimer::singleShot(0, qApp, &QCoreApplication::quit); }

private:
    QString eventsPath;
};

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    const QByteArray address = qgetenv("DBUS_STARTER_ADDRESS");
    if (address.isEmpty() || argc != 2) {
        return 1;
    }
    auto bus = QDBusConnection::connectToBus(QString::fromLocal8Bit(address), QStringLiteral("activation-service"));
    ActivationService service(QString::fromLocal8Bit(argv[1]));
    service.record("start");
    // Deliberately delay readiness to exercise requests queued by clients.
    QTimer::singleShot(250, &app, [&] {
        if (!bus.registerObject(QStringLiteral("/com/shapertools/qdbussys/connectivityd"), &service,
                                QDBusConnection::ExportAllSlots | QDBusConnection::ExportAllProperties)
            || !bus.registerService(QStringLiteral("com.shapertools.qdbussys.connectivityd"))) {
            app.exit(1);
        }
    });
    QTimer::singleShot(15000, &app, &QCoreApplication::quit);
    return app.exec();
}

#include "connectivity_activation_service.moc"
