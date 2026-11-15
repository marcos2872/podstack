// SPDX-License-Identifier: GPL-3.0-or-later
#include "podman/client/Common.h"

using namespace PodStack::PodmanInternal;

namespace PodStack {

QJsonObject PodmanClient::systemDf(Error *error)
{
    if (error) {
        *error = {};
    }
    if (!ensureVersion(error)) {
        return {};
    }

    const HttpResponse response =
        m_http.request(QByteArrayLiteral("GET"), path(QStringLiteral("/system/df")));
    if (response.status != 200) {
        if (error) {
            *error = errorFromResponse(response, QStringLiteral("Falha ao ler o uso de disco."));
        }
        return {};
    }
    return QJsonDocument::fromJson(response.body).object();
}

} // namespace PodStack
