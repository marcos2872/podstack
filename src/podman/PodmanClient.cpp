// SPDX-License-Identifier: GPL-3.0-or-later
#include "podman/client/Common.h"

using namespace PodStack::PodmanInternal;

namespace PodStack {

QString PodmanClient::path(const QString &suffix) const
{
    std::lock_guard<std::recursive_mutex> lock(m_versionMutex);
    if (m_apiVersion.isEmpty()) {
        return QStringLiteral("/libpod") + suffix;
    }
    return QStringLiteral("/") + m_apiVersion + QStringLiteral("/libpod") + suffix;
}

QString PodmanClient::apiVersion() const
{
    std::lock_guard<std::recursive_mutex> lock(m_versionMutex);
    return m_apiVersion;
}

Error PodmanClient::errorFromResponse(const HttpResponse &response, const QString &fallback)
{
    QString message = fallback;
    QString detail = response.error;

    const QJsonDocument document = QJsonDocument::fromJson(response.body);
    if (document.isObject()) {
        const QJsonObject object = document.object();
        const QString engineMessage = object.value(QStringLiteral("message")).toString();
        if (!engineMessage.isEmpty()) {
            message = engineMessage;
        }
        const QString cause = object.value(QStringLiteral("cause")).toString();
        if (!cause.isEmpty()) {
            detail = cause;
        } else if (detail.isEmpty()) {
            detail = QString::fromUtf8(response.body.left(512));
        }
    } else if (detail.isEmpty()) {
        detail = QString::fromUtf8(response.body.left(512));
    }

    QString code = QStringLiteral("PODSTACK_ENGINE_ERROR");
    if (response.status == 404) {
        code = QStringLiteral("PODSTACK_NOT_FOUND");
    } else if (response.status == 409) {
        code = QStringLiteral("PODSTACK_CONFLICT");
    } else if (response.status == 403) {
        code = QStringLiteral("PODSTACK_PERMISSION_DENIED");
    }

    return Error::make(code, message, detail);
}

bool PodmanClient::ping(Error *error)
{
    std::lock_guard<std::recursive_mutex> lock(m_versionMutex);
    if (error) {
        *error = {};
    }
    const HttpResponse response = m_http.request(QByteArrayLiteral("GET"), QStringLiteral("/_ping"));
    if (response.status != 200) {
        if (error) {
            *error = errorFromResponse(response,
                                       QStringLiteral("O Podman não respondeu ao ping."));
        }
        return false;
    }

    QString version = response.headers.value(QStringLiteral("libpod-api-version"));
    if (version.isEmpty()) {
        version = response.headers.value(QStringLiteral("api-version"));
    }
    if (!version.isEmpty() && !version.startsWith(QLatin1Char('v'))) {
        version.prepend(QLatin1Char('v'));
    }
    m_apiVersion = version;
    return true;
}

bool PodmanClient::ensureVersion(Error *error)
{
    std::lock_guard<std::recursive_mutex> lock(m_versionMutex);
    if (!m_apiVersion.isEmpty()) {
        return true;
    }
    return ping(error);
}

} // namespace PodStack
