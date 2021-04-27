// Copyright (C) 2026 Shaper Tools
// SPDX-License-Identifier: LGPL-2.1-only

#pragma once

#include <QDBusAbstractAdaptor>
#include <QDBusConnection>
#include <QDBusObjectPath>
#include <QRandomGenerator>
#include <QDebug>
#include <QDBusMessage>

/*
 * git://git.kernel.org/pub/scm/network/wireless/iwd.git
 * doc/agent-api.txt
 *
 *        void Release()
 *
 *             This method gets called when the service daemon
 *             unregisters the agent. An agent can use it to do
 *             cleanup tasks. There is no need to unregister the
 *             agent, because when this method gets called it has
 *             already been unregistered.
 *
 *         string RequestPassphrase(object network)
 *
 *             This method gets called when trying to connect to
 *             a network and passphrase is required.
 *
 *             Possible Errors: net.connman.iwd.Agent.Error.Canceled
 *
 *         string RequestPrivateKeyPassphrase(object network)
 *
 *             This method gets called when connecting to
 *             a network that requires authentication using a
 *             locally-stored encrypted private key file, to
 *             obtain that private key's encryption passphrase.
 *
 *             Possible Errors: net.connman.iwd.Agent.Error.Canceled
 *
 *         (string, string) RequestUserNameAndPassword(object network)
 *
 *             This method gets called when connecting to
 *             a network that requires authentication using a
 *             user name and password.
 *
 *             Possible Errors: net.connman.iwd.Agent.Error.Canceled
 *
 *         string RequestUserPassword(object network, string user)
 *
 *             This method gets called when connecting to
 *             a network that requires authentication with a
 *             user password.  The user name is optionally passed
 *             in the parameter.
 *
 *             Possible Errors: net.connman.iwd.Agent.Error.Canceled
 *
 *         void Cancel(string reason)
 *
 *             This method gets called to indicate that the agent
 *             request failed before a reply was returned.  The
 *             argument will indicate why the request is being
 *             cancelled and may be "out-of-range", "user-canceled",
 *             "timed-out" or "shutdown".
 */

// Requests are honoured only from the IWD connection that owns the agent
// registration; anything else receives AccessDenied. Authentication methods
// other than a plain passphrase are unsupported and are answered with
// net.connman.iwd.Agent.Error.Canceled rather than with empty credentials.
class AbstractAuthenticationAgentAdapter : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "net.connman.iwd.Agent")

public:
    AbstractAuthenticationAgentAdapter(QObject *parent, const QDBusConnection &bus);
    QDBusObjectPath objectPath() const { return path; }

public slots:
    Q_NOREPLY void Release(const QDBusMessage &message);
    QString RequestPassphrase(const QDBusObjectPath &network, const QDBusMessage &message);
    QString RequestPrivateKeyPassphrase(const QDBusObjectPath &network, const QDBusMessage &message);
    QString RequestUserNameAndPassword(const QDBusObjectPath &network, const QDBusMessage &message, QString &password);
    QString RequestUserPassword(const QDBusObjectPath &network, const QString &user, const QDBusMessage &message);
    Q_NOREPLY void Cancel(const QString &reason, const QDBusMessage &message);

protected:
    virtual bool isTrustedCaller(const QDBusMessage &message) const = 0;
    // Both set a delayed reply, so the slot's return value is ignored.
    void replyCanceled(const QDBusMessage &message, const QString &reason);
    void replyAccessDenied(const QDBusMessage &message);

    QDBusConnection bus;

private:
    virtual void onCancel(const QString &reason);
    virtual void onRelease();
    virtual QString onRequestPassphrase(const QString &networkPath, const QDBusMessage &message) = 0;

    QDBusObjectPath path;
};

/*
 * git://git.kernel.org/pub/scm/network/wireless/iwd.git
 * doc/station-api.txt
 *
 *      void Release(object device)
 *
 *           This method gets called when the service daemon
 *           unregisters the agent. An agent can use it to do
 *           cleanup tasks. There is no need to unregister the
 *           agent, because when this method gets called it has
 *           already been unregistered.
 *
 *      void Changed(object device, uint8 level)
 *
 *           This method gets called when the signal strength
 *           measurement for the device's connected network
 *           changes enough to go from one level to another out
 *           of the N ranges defined by the array of (N-1)
 *           threshold values passed to RegisterSignalLevelAgent().
 *           The level parameter is in the range from 0 to N,
 *           0 being the strongest signal or above the first
 *           threshold value in the array, and N being the
 *           weakest and below the last threshold value.  For
 *           example if RegisterSignalLevelAgent was called with
 *           the array [-40, -50, -60], the 'level' parameter of
 *           0 would mean signal is received at -40 or more dBm
 *           and 3 would mean below -60 dBm and might correspond
 *           to 1 out of 4 bars on a UI signal meter.
 */

class AbstractSignalLevelAgentAdapter : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "net.connman.iwd.SignalLevelAgent")

public:
    AbstractSignalLevelAgentAdapter(QObject *parent);
    QDBusObjectPath objectPath() const { return path; }

public slots:
    Q_NOREPLY void Release(const QDBusObjectPath &device, const QDBusMessage &message) {
        if (isTrustedCaller(message)) onRelease(device.path());
    }

    Q_NOREPLY void Changed(const QDBusObjectPath &device, uchar level, const QDBusMessage &message) {
        if (isTrustedCaller(message)) onChanged(device.path(), level);
    }

private:
    virtual bool isTrustedCaller(const QDBusMessage &message) const = 0;
    virtual void onRelease(const QString &devicePath) = 0;
    virtual void onChanged(const QString &devicePath, uint8_t level) = 0;

    QDBusObjectPath path;
};
