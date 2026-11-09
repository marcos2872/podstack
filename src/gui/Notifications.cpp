// SPDX-License-Identifier: GPL-3.0-or-later
#include "gui/Notifications.h"

#include "core/Config.h"

#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusReply>
#include <QDateTime>
#include <QJsonValue>
#include <QSystemTrayIcon>

namespace PodStack {
namespace {

constexpr qint64 kCooldownMs = 30000;

bool notificationsEnabled()
{
    const QJsonValue value = Config::load().get(QStringLiteral("app.notifications"));
    return value.isBool() ? value.toBool() : true;
}

} // namespace

NotificationService::NotificationService(QObject *parent)
    : QObject(parent)
{
    QDBusConnection::sessionBus().connect(
        QStringLiteral("org.freedesktop.Notifications"),
        QStringLiteral("/org/freedesktop/Notifications"),
        QStringLiteral("org.freedesktop.Notifications"), QStringLiteral("ActionInvoked"),
        this, SLOT(onActionInvoked(uint,QString)));
    QDBusConnection::sessionBus().connect(
        QStringLiteral("org.freedesktop.Notifications"),
        QStringLiteral("/org/freedesktop/Notifications"),
        QStringLiteral("org.freedesktop.Notifications"), QStringLiteral("NotificationClosed"),
        this, SLOT(onNotificationClosed(uint,uint)));
}

void NotificationService::setTrayIcon(QSystemTrayIcon *icon)
{
    m_trayIcon = icon;
}

void NotificationService::setSenderForTesting(Sender sender)
{
    m_sender = std::move(sender);
}

QString NotificationService::summaryFor(const QVariantMap &event)
{
    const QString action = event.value(QStringLiteral("action")).toString();
    if (action == QStringLiteral("oom")) {
        return QObject::tr("Container sem memória");
    }
    if (action == QStringLiteral("health_status")) {
        return event.value(QStringLiteral("health")).toString() == QStringLiteral("unhealthy")
            ? QObject::tr("Container unhealthy")
            : QString();
    }
    if (action == QStringLiteral("die")) {
        const QString code = event.value(QStringLiteral("exitCode")).toString();
        if (!code.isEmpty() && code != QStringLiteral("0")) {
            return QObject::tr("Container falhou");
        }
    }
    return {};
}

QString NotificationService::bodyFor(const QVariantMap &event)
{
    QString name = event.value(QStringLiteral("name")).toString();
    if (name.isEmpty()) {
        name = event.value(QStringLiteral("id")).toString().left(12);
    }

    const QString action = event.value(QStringLiteral("action")).toString();
    if (action == QStringLiteral("oom")) {
        return QObject::tr("%1 foi encerrado por falta de memória.").arg(name);
    }
    if (action == QStringLiteral("health_status")) {
        return QObject::tr("%1 está unhealthy.").arg(name);
    }
    if (action == QStringLiteral("die")) {
        return QObject::tr("%1 saiu com código %2.")
            .arg(name, event.value(QStringLiteral("exitCode")).toString());
    }
    return {};
}

bool NotificationService::shouldNotify(const QVariantMap &event, qint64 nowMs)
{
    const QString key = event.value(QStringLiteral("id")).toString() + QLatin1Char('|')
        + event.value(QStringLiteral("action")).toString() + QLatin1Char('|')
        + event.value(QStringLiteral("health")).toString() + QLatin1Char('|')
        + event.value(QStringLiteral("exitCode")).toString();

    const qint64 lastSent = m_lastSent.value(key, 0);
    if (lastSent > 0 && nowMs - lastSent < kCooldownMs) {
        return false;
    }
    m_lastSent.insert(key, nowMs);
    if (m_lastSent.size() > 256) {
        m_lastSent.clear();
        m_lastSent.insert(key, nowMs);
    }
    return true;
}

void NotificationService::notifyContainerEvent(const QVariantMap &event)
{
    if (!notificationsEnabled()) {
        return;
    }

    const QString summary = summaryFor(event);
    if (summary.isEmpty()) {
        return;
    }
    if (!shouldNotify(event, QDateTime::currentMSecsSinceEpoch())) {
        return;
    }

    send(summary, bodyFor(event), event.value(QStringLiteral("id")).toString());
}

void NotificationService::send(const QString &summary, const QString &body,
                               const QString &containerId)
{
    if (m_sender) {
        m_sender(summary, body, containerId);
        return;
    }

    QDBusInterface interface(QStringLiteral("org.freedesktop.Notifications"),
                             QStringLiteral("/org/freedesktop/Notifications"),
                             QStringLiteral("org.freedesktop.Notifications"),
                             QDBusConnection::sessionBus());
    if (interface.isValid()) {
        const QVariantList arguments{
            QStringLiteral("PodStack"),
            QVariant::fromValue<uint>(0),
            QStringLiteral("dev.podstack.PodStack"),
            summary,
            body,
            QStringList{QStringLiteral("default"), tr("Abrir")},
            QVariantMap{},
            6000,
        };
        const QDBusReply<uint> reply =
            interface.callWithArgumentList(QDBus::AutoDetect, QStringLiteral("Notify"),
                                           arguments);
        if (reply.isValid()) {
            m_ids.insert(reply.value(), containerId);
            return;
        }
    }

    if (m_trayIcon) {
        m_trayIcon->showMessage(summary, body, QSystemTrayIcon::Information, 6000);
    }
}

void NotificationService::onActionInvoked(uint id, const QString &key)
{
    if (!m_ids.contains(id)) {
        return;
    }
    const QString containerId = m_ids.take(id);
    if (key == QStringLiteral("default")) {
        emit containerActivated(containerId);
    }
}

void NotificationService::onNotificationClosed(uint id, uint reason)
{
    Q_UNUSED(reason)
    m_ids.remove(id);
}

} // namespace PodStack
