// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/Redaction.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QRegularExpression>

namespace PodStack::Redaction {
namespace {

constexpr auto kMask = "[redacted]";

QString redactCredentials(const QString &input)
{
    static const QRegularExpression pattern(
        QStringLiteral("(\\b[a-z][a-z0-9+.-]*://)([^/\\s:@]+):([^/\\s@]+)@"),
        QRegularExpression::CaseInsensitiveOption);
    QString output = input;
    output.replace(pattern, QStringLiteral("\\1%1@").arg(QString::fromLatin1(kMask)));
    return output;
}

QString redactAuthorization(const QString &input)
{
    static const QRegularExpression pattern(
        QStringLiteral("(?i)(authorization\\s*:\\s*)([^\\r\\n]+)"));
    QString output = input;
    output.replace(pattern, QStringLiteral("\\1%1").arg(QString::fromLatin1(kMask)));
    return output;
}

QString redactAssignments(const QString &input)
{
    static const QRegularExpression pattern(QStringLiteral(
        "(?i)\\b(password|passwd|secret|token|api[-_]?key)\\s*[=:]\\s*([^\\s&,;]+)"));
    QString output = input;
    output.replace(pattern, QStringLiteral("\\1=%1").arg(QString::fromLatin1(kMask)));
    return output;
}

} // namespace

bool isSensitiveKey(const QString &key)
{
    const QString normalized = key.toLower();
    static const QStringList fragments{
        QStringLiteral("password"), QStringLiteral("passwd"),        QStringLiteral("secret"),
        QStringLiteral("token"),    QStringLiteral("authorization"), QStringLiteral("apikey"),
        QStringLiteral("api_key"),  QStringLiteral("credential")};
    for (const QString &fragment : fragments) {
        if (normalized.contains(fragment)) {
            return true;
        }
    }
    return false;
}

QString text(const QString &input)
{
    return redactAssignments(redactAuthorization(redactCredentials(input)));
}

QJsonValue json(const QJsonValue &value)
{
    if (value.isObject()) {
        QJsonObject output = value.toObject();
        for (auto it = output.begin(); it != output.end(); ++it) {
            if (isSensitiveKey(it.key())) {
                it.value() = QString::fromLatin1(kMask);
            } else {
                it.value() = json(it.value());
            }
        }
        return output;
    }
    if (value.isArray()) {
        QJsonArray output;
        for (const QJsonValue &entry : value.toArray()) {
            output.append(json(entry));
        }
        return output;
    }
    if (value.isString()) {
        return text(value.toString());
    }
    return value;
}

} // namespace PodStack::Redaction
