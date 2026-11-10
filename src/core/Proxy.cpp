// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/Proxy.h"

#include <QAbstractSocket>
#include <QHostAddress>
#include <QSet>
#include <QUrl>

namespace PodStack::Proxy {
namespace {

constexpr int kMaxExcludeLength = 255;
constexpr int kMaxHostnameLength = 253;
constexpr int kMaxLabelLength = 63;
constexpr int kMaxPort = 65535;

bool hasControlCharacters(const QString &value)
{
    for (const QChar character : value) {
        if (character.unicode() < 0x20 || character.unicode() == 0x7f) {
            return true;
        }
    }
    return false;
}

bool isAsciiAlphaNumeric(QChar character)
{
    const ushort value = character.unicode();
    return (value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z')
           || (value >= '0' && value <= '9');
}

bool isProxyScheme(const QString &scheme)
{
    return scheme == QStringLiteral("http") || scheme == QStringLiteral("https")
           || scheme == QStringLiteral("socks5") || scheme == QStringLiteral("socks5h");
}

bool isValidProxyUrl(const QString &proxy)
{
    if (proxy.isEmpty() || proxy != proxy.trimmed() || hasControlCharacters(proxy)) {
        return false;
    }

    const QUrl url(proxy, QUrl::StrictMode);
    if (!url.isValid() || url.isRelative() || url.scheme().isEmpty() || url.host().isEmpty()
        || !isProxyScheme(url.scheme())) {
        return false;
    }
    if (url.port(-1) > kMaxPort) {
        return false;
    }
    return true;
}

bool isValidHostname(const QString &value)
{
    QString hostname = value;
    if (hostname.startsWith(QStringLiteral("*."))) {
        hostname.remove(0, 2);
    } else if (hostname.startsWith(QLatin1Char('.'))) {
        hostname.remove(0, 1);
    }
    if (hostname.isEmpty() || hostname.size() > kMaxHostnameLength) {
        return false;
    }

    const QStringList labels = hostname.split(QLatin1Char('.'));
    for (const QString &label : labels) {
        if (label.isEmpty() || label.size() > kMaxLabelLength) {
            return false;
        }
        if (label.startsWith(QLatin1Char('-')) || label.endsWith(QLatin1Char('-'))) {
            return false;
        }
        for (const QChar character : label) {
            if (!isAsciiAlphaNumeric(character) && character != QLatin1Char('-')
                && character != QLatin1Char('_')) {
                return false;
            }
        }
    }
    return true;
}

bool isValidCidr(const QString &value)
{
    if (value.count(QLatin1Char('/')) != 1) {
        return false;
    }

    const int slash = value.indexOf(QLatin1Char('/'));
    const QString addressText = value.left(slash);
    const QString prefixText = value.mid(slash + 1);
    if (addressText.isEmpty() || prefixText.isEmpty()) {
        return false;
    }

    bool prefixOk = false;
    const int prefix = prefixText.toInt(&prefixOk);
    if (!prefixOk || prefixText.startsWith(QLatin1Char('+'))) {
        return false;
    }

    QHostAddress address;
    if (!address.setAddress(addressText)) {
        return false;
    }
    const bool ipv4 = address.protocol() == QAbstractSocket::IPv4Protocol;
    const bool ipv6 = address.protocol() == QAbstractSocket::IPv6Protocol;
    if (!ipv4 && !ipv6) {
        return false;
    }
    return prefix >= 0 && prefix <= (ipv4 ? 32 : 128);
}

bool isValidExclude(const QString &value)
{
    if (value.isEmpty() || value != value.trimmed() || value.size() > kMaxExcludeLength
        || hasControlCharacters(value) || value.contains(QLatin1Char(','))
        || value.contains(QLatin1Char(' '))) {
        return false;
    }
    if (value.contains(QLatin1Char('/'))) {
        return isValidCidr(value);
    }

    QHostAddress address;
    if (address.setAddress(value)) {
        return address.protocol() == QAbstractSocket::IPv4Protocol
               || address.protocol() == QAbstractSocket::IPv6Protocol;
    }
    return isValidHostname(value);
}

Error validationError(const QString &message, const QString &detail = {})
{
    return Error::make(QStringLiteral("PODSTACK_VALIDATION"), message, detail);
}

const QStringList &proxyKeys()
{
    static const QStringList keys = {
        QStringLiteral("HTTP_PROXY"),  QStringLiteral("HTTPS_PROXY"),
        QStringLiteral("ALL_PROXY"),   QStringLiteral("NO_PROXY"),
        QStringLiteral("http_proxy"),  QStringLiteral("https_proxy"),
        QStringLiteral("all_proxy"),   QStringLiteral("no_proxy"),
    };
    return keys;
}

void appendExcludes(QStringList &target, QSet<QString> &seen, const QString &list)
{
    const QStringList entries = list.split(QLatin1Char(','), Qt::SkipEmptyParts);
    for (const QString &entry : entries) {
        const QString normalized = entry.trimmed();
        if (normalized.isEmpty()) {
            continue;
        }
        const QString key = normalized.toLower();
        if (seen.contains(key)) {
            continue;
        }
        seen.insert(key);
        target.append(normalized);
    }
}

} // namespace

bool validate(const QString &proxy, const QStringList &exclude, Error *error)
{
    if (error) {
        *error = {};
    }

    if (proxy != QStringLiteral("none") && proxy != QStringLiteral("auto")
        && !isValidProxyUrl(proxy)) {
        if (error) {
            *error = validationError(
                QStringLiteral("Proxy inválido: use none, auto ou uma URL http, https, "
                               "socks5 ou socks5h com host."));
        }
        return false;
    }

    for (const QString &entry : exclude) {
        if (!isValidExclude(entry)) {
            if (error) {
                *error = validationError(
                    QStringLiteral("Exclusão de proxy inválida: use host, domínio, CIDR ou "
                                   "*.domínio."),
                    entry.left(kMaxExcludeLength));
            }
            return false;
        }
    }
    return true;
}

QMap<QString, QString> environment(const QString &proxy, const QStringList &exclude)
{
    QMap<QString, QString> values;
    if (proxy == QStringLiteral("none")) {
        return values;
    }

    if (proxy == QStringLiteral("auto")) {
        for (const QString &key : proxyKeys()) {
            if (qEnvironmentVariableIsSet(key.toUtf8().constData())) {
                values.insert(key, qEnvironmentVariable(key.toUtf8().constData()));
            }
        }
        return values;
    }

    if (!isValidProxyUrl(proxy)) {
        return values;
    }

    const QStringList keys = {
        QStringLiteral("HTTP_PROXY"), QStringLiteral("HTTPS_PROXY"),
        QStringLiteral("ALL_PROXY"),  QStringLiteral("http_proxy"),
        QStringLiteral("https_proxy"), QStringLiteral("all_proxy"),
    };
    for (const QString &key : keys) {
        values.insert(key, proxy);
    }

    QStringList noProxy = defaultExcludes();
    QSet<QString> seen;
    for (const QString &entry : noProxy) {
        seen.insert(entry.toLower());
    }
    for (const QString &entry : exclude) {
        const QString normalized = entry.trimmed();
        if (normalized.isEmpty() || !isValidExclude(normalized)) {
            continue;
        }
        const QString key = normalized.toLower();
        if (seen.contains(key)) {
            continue;
        }
        seen.insert(key);
        noProxy.append(normalized);
    }

    for (const QString &key : {QStringLiteral("NO_PROXY"), QStringLiteral("no_proxy")}) {
        if (qEnvironmentVariableIsSet(key.toUtf8().constData())) {
            appendExcludes(noProxy, seen, qEnvironmentVariable(key.toUtf8().constData()));
        }
    }

    const QString merged = noProxy.join(QLatin1Char(','));
    values.insert(QStringLiteral("NO_PROXY"), merged);
    values.insert(QStringLiteral("no_proxy"), merged);
    return values;
}

QStringList defaultExcludes()
{
    return {QStringLiteral("localhost"), QStringLiteral("127.0.0.1"), QStringLiteral("::1"),
            QStringLiteral("*.localhost")};
}

} // namespace PodStack::Proxy
