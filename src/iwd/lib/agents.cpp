// Copyright (C) 2026 Shaper Tools
// SPDX-License-Identifier: LGPL-2.1-only

#include "agents.h"
#include <QDebug>

namespace {
const auto canceledError = QStringLiteral("net.connman.iwd.Agent.Error.Canceled");
const auto unsupportedMessage = QStringLiteral("Only passphrase authentication is supported");
}

AbstractAuthenticationAgentAdapter::AbstractAuthenticationAgentAdapter(QObject *parent, const QDBusConnection &bus)
    : QDBusAbstractAdaptor(parent), bus(bus)
{
    path.setPath("/com/shapertools/authagent/" + QString::number(QRandomGenerator::global()->generate64()));
}

void AbstractAuthenticationAgentAdapter::replyCanceled(const QDBusMessage &message, const QString &reason)
{
    message.setDelayedReply(true);
    bus.send(message.createErrorReply(canceledError, reason));
}

void AbstractAuthenticationAgentAdapter::replyAccessDenied(const QDBusMessage &message)
{
    message.setDelayedReply(true);
    bus.send(message.createErrorReply(QDBusError::AccessDenied, QStringLiteral("Only IWD may use this agent")));
}

void AbstractAuthenticationAgentAdapter::Release(const QDBusMessage &message)
{
    if (isTrustedCaller(message)) onRelease();
}

void AbstractAuthenticationAgentAdapter::Cancel(const QString &reason, const QDBusMessage &message)
{
    if (isTrustedCaller(message)) onCancel(reason);
}

QString AbstractAuthenticationAgentAdapter::RequestPassphrase(const QDBusObjectPath &network, const QDBusMessage &message)
{
    if (!isTrustedCaller(message)) {
        replyAccessDenied(message);
        return {};
    }
    return onRequestPassphrase(network.path(), message);
}

QString AbstractAuthenticationAgentAdapter::RequestPrivateKeyPassphrase(const QDBusObjectPath &, const QDBusMessage &message)
{
    if (isTrustedCaller(message)) replyCanceled(message, unsupportedMessage);
    else replyAccessDenied(message);
    return {};
}

QString AbstractAuthenticationAgentAdapter::RequestUserNameAndPassword(const QDBusObjectPath &, const QDBusMessage &message, QString &)
{
    if (isTrustedCaller(message)) replyCanceled(message, unsupportedMessage);
    else replyAccessDenied(message);
    return {};
}

QString AbstractAuthenticationAgentAdapter::RequestUserPassword(const QDBusObjectPath &, const QString &, const QDBusMessage &message)
{
    if (isTrustedCaller(message)) replyCanceled(message, unsupportedMessage);
    else replyAccessDenied(message);
    return {};
}

void AbstractAuthenticationAgentAdapter::onCancel(const QString &) {}

void AbstractAuthenticationAgentAdapter::onRelease() {}

AbstractSignalLevelAgentAdapter::AbstractSignalLevelAgentAdapter(QObject *parent)
    : QDBusAbstractAdaptor(parent)
{
    path.setPath("/com/shapertools/signalagent/" + QString::number(QRandomGenerator::global()->generate64()));
}
