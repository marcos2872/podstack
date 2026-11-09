// SPDX-License-Identifier: GPL-3.0-or-later

#include "helper/Polkit.h"

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QFile>
#include <QMap>
#include <QVariantMap>

namespace PodStack {
namespace {

constexpr auto kPolkitService = "org.freedesktop.PolicyKit1";
constexpr auto kPolkitPath = "/org/freedesktop/PolicyKit1/Authority";
constexpr auto kPolkitInterface = "org.freedesktop.PolicyKit1.Authority";

using StringMap = QMap<QString, QString>;

} // namespace

qulonglong processStartTime(uint pid)
{
    QFile stat(QStringLiteral("/proc/%1/stat").arg(pid));
    if (!stat.open(QIODevice::ReadOnly)) {
        return 0;
    }

    const QByteArray contents = stat.readAll();
    const int close = contents.lastIndexOf(')');
    if (close < 0) {
        return 0;
    }

    const QList<QByteArray> fields = contents.mid(close + 1).trimmed().split(' ');
    // Após o comm (campo 2), fields[0] é o campo 3 (state); starttime é o campo 22.
    const int index = 22 - 3;
    if (fields.size() <= index) {
        return 0;
    }
    bool ok = false;
    const qulonglong value = fields.at(index).toULongLong(&ok);
    return ok ? value : 0;
}

QDBusMessage checkAuthorizationRequest(const QString &actionId, uint pid)
{
    static const QMetaType registered = qDBusRegisterMetaType<StringMap>();
    Q_UNUSED(registered)

    QDBusArgument subject;
    subject.beginStructure();
    subject << QStringLiteral("unix-process");

    QVariantMap subjectDetails;
    subjectDetails.insert(QStringLiteral("pid"), QVariant::fromValue<uint>(pid));
    const qulonglong startTime = processStartTime(pid);
    if (startTime > 0) {
        subjectDetails.insert(QStringLiteral("start-time"),
                              QVariant::fromValue<qulonglong>(startTime));
    }
    subject << subjectDetails;
    subject.endStructure();

    // O polkit exige a{ss} em `details` (não a{sv}).
    StringMap details;
    details.insert(QStringLiteral("action"), actionId);

    QDBusMessage request = QDBusMessage::createMethodCall(
        QString::fromLatin1(kPolkitService), QString::fromLatin1(kPolkitPath),
        QString::fromLatin1(kPolkitInterface), QStringLiteral("CheckAuthorization"));
    request << QVariant::fromValue(subject) << actionId << QVariant::fromValue(details)
            << uint(1) << QString();
    return request;
}

bool checkAuthorization(const QString &actionId, uint pid, QString *error)
{
    const QDBusMessage request = checkAuthorizationRequest(actionId, pid);
    const QDBusMessage reply =
        QDBusConnection::systemBus().call(request, QDBus::Block, 30000);
    if (reply.type() == QDBusMessage::ErrorMessage) {
        if (error) {
            *error = QStringLiteral("Polkit indisponível: %1").arg(reply.errorMessage());
        }
        return false;
    }

    if (reply.arguments().isEmpty()) {
        if (error) {
            *error = QStringLiteral("Resposta inválida do polkit.");
        }
        return false;
    }

    const QDBusArgument result = reply.arguments().constFirst().value<QDBusArgument>();
    bool authorized = false;
    bool challenge = false;
    StringMap resultDetails;
    result.beginStructure();
    result >> authorized >> challenge >> resultDetails;
    result.endStructure();

    if (!authorized) {
        if (error) {
            *error = challenge ? QStringLiteral("Autorização negada ou cancelada.")
                               : QStringLiteral("Autorização negada pelo polkit.");
        }
        return false;
    }
    return true;
}

} // namespace PodStack
