// SPDX-License-Identifier: GPL-3.0-or-later

#include "daemon/HelperClient.h"

#include "daemon/DbusUtils.h"

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusInterface>
#include <QDBusMessage>
#include <QVariantMap>

namespace PodStack {
namespace {

constexpr auto kHelperService = "dev.podstack.Helper1";
constexpr auto kHelperPath = "/dev/podstack/Helper1";
constexpr auto kHelperInterface = "dev.podstack.Helper1";

} // namespace

bool HelperClient::available() const
{
    QDBusConnectionInterface *bus = QDBusConnection::systemBus().interface();
    if (!bus) {
        return false;
    }
    return bus->isServiceRegistered(QString::fromLatin1(kHelperService)).value();
}

bool HelperClient::call(const QString &method, const QVariantList &arguments, QString *error,
                        int timeoutMs) const
{
    if (error) {
        *error = {};
    }

    QDBusInterface interface(QString::fromLatin1(kHelperService),
                             QString::fromLatin1(kHelperPath),
                             QString::fromLatin1(kHelperInterface),
                             QDBusConnection::systemBus());
    if (!interface.isValid()) {
        if (error) {
            *error = QStringLiteral("helper indisponível: %1")
                         .arg(QDBusConnection::systemBus().lastError().message());
        }
        return false;
    }

    interface.setTimeout(timeoutMs);
    const QDBusMessage reply =
        interface.callWithArgumentList(QDBus::Block, method, arguments);
    if (reply.type() == QDBusMessage::ErrorMessage) {
        if (error) {
            *error = reply.errorMessage();
        }
        return false;
    }

    const QVariantMap values = DbusUtils::variantMapFromReply(reply.arguments().value(0));
    const bool ok = values.value(QStringLiteral("ok")).toBool();
    const QString message = values.value(QStringLiteral("message")).toString();
    if (!ok && error) {
        *error = message.isEmpty() ? QStringLiteral("O helper recusou a operação.") : message;
    }
    return ok;
}

} // namespace PodStack
