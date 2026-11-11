// SPDX-License-Identifier: GPL-3.0-or-later

#include "daemon/DockerInventory.h"

#include "podman/UnixHttpClient.h"

#include <QDateTime>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcessEnvironment>

#include <utility>

namespace PodStack {
namespace {

QString envValue(const char *name)
{
    return QProcessEnvironment::systemEnvironment().value(QString::fromLatin1(name));
}

qint64 jsonSize(const QJsonObject &object, const QString &key)
{
    const QJsonValue value = object.value(key);
    return value.isDouble() ? static_cast<qint64>(value.toDouble()) : 0;
}

} // namespace

DockerInventory::DockerInventory(QString socketPath) : m_socketPath(std::move(socketPath)) {}

DockerImpact DockerInventory::inspect(int timeoutMs) const
{
    DockerImpact impact;

    QStringList candidates;
    if (!m_socketPath.isEmpty()) {
        candidates.append(m_socketPath);
    } else {
        candidates = socketCandidates();
    }

    for (const QString &candidate : candidates) {
        if (candidate.isEmpty() || !QFileInfo::exists(candidate)) {
            continue;
        }

        UnixHttpClient client(candidate);
        const HttpResponse df = client.request(QByteArrayLiteral("GET"),
                                               QStringLiteral("/system/df"), {}, {}, timeoutMs);
        if (df.status != 200) {
            impact.socketPath = candidate;
            impact.error = df.error.isEmpty()
                               ? QStringLiteral("Docker respondeu HTTP %1.").arg(df.status)
                               : df.error;
            continue;
        }

        QJsonParseError parseError{};
        const QJsonDocument document = QJsonDocument::fromJson(df.body, &parseError);
        if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
            impact.socketPath = candidate;
            impact.error =
                QStringLiteral("Resposta inválida do Docker: %1").arg(parseError.errorString());
            continue;
        }

        const QJsonObject root = document.object();
        const QJsonArray containers = root.value(QStringLiteral("Containers")).toArray();
        impact.containers = containers.size();
        for (const QJsonValue &value : containers) {
            const QJsonObject container = value.toObject();
            if (container.value(QStringLiteral("State")).toString() == QStringLiteral("running")) {
                ++impact.running;
            }
            impact.estimatedSizeBytes += jsonSize(container, QStringLiteral("SizeRw"));
        }

        const QJsonArray images = root.value(QStringLiteral("Images")).toArray();
        impact.images = images.size();
        impact.estimatedSizeBytes += jsonSize(root, QStringLiteral("LayersSize"));

        const QJsonArray volumes = root.value(QStringLiteral("Volumes")).toArray();
        impact.volumes = volumes.size();
        for (const QJsonValue &value : volumes) {
            const QJsonObject volume = value.toObject();
            impact.estimatedSizeBytes += jsonSize(
                volume.value(QStringLiteral("UsageData")).toObject(), QStringLiteral("Size"));
        }

        const HttpResponse networks = client.request(
            QByteArrayLiteral("GET"), QStringLiteral("/networks"), {}, {}, timeoutMs);
        if (networks.status == 200) {
            const QJsonDocument networkDocument = QJsonDocument::fromJson(networks.body);
            if (networkDocument.isArray()) {
                impact.networks = networkDocument.array().size();
            }
        } else {
            impact.error = networks.error.isEmpty()
                               ? QStringLiteral("Não foi possível listar as redes do Docker.")
                               : networks.error;
        }

        impact.available = true;
        impact.socketPath = candidate;
        return impact;
    }

    impact.error = QStringLiteral("Socket do Docker não encontrado.");
    return impact;
}

QStringList DockerInventory::socketCandidates()
{
    QStringList candidates;

    const QString overridePath = envValue("PODSTACK_DOCKER_SOCKET");
    if (!overridePath.isEmpty()) {
        candidates.append(overridePath);
    }

    const QString dockerHost = envValue("DOCKER_HOST");
    if (dockerHost.startsWith(QStringLiteral("unix://"))) {
        candidates.append(dockerHost.mid(7));
    }

    const QString runtimeDir = envValue("XDG_RUNTIME_DIR");
    if (!runtimeDir.isEmpty()) {
        candidates.append(runtimeDir + QStringLiteral("/docker.sock"));
    }
    candidates.append(QStringLiteral("/var/run/docker.sock"));

    QStringList unique;
    for (const QString &candidate : candidates) {
        if (!candidate.isEmpty() && !unique.contains(candidate)) {
            unique.append(candidate);
        }
    }
    return unique;
}

QJsonObject DockerImpact::toJson() const
{
    QJsonObject containersJson;
    containersJson.insert(QStringLiteral("total"), containers);
    containersJson.insert(QStringLiteral("running"), running);

    QJsonObject object;
    object.insert(QStringLiteral("available"), available);
    object.insert(QStringLiteral("socket"), socketPath);
    object.insert(QStringLiteral("checkedAt"),
                  QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    object.insert(QStringLiteral("containers"), containersJson);
    object.insert(QStringLiteral("images"), images);
    object.insert(QStringLiteral("networks"), networks);
    object.insert(QStringLiteral("volumes"), volumes);
    object.insert(QStringLiteral("estimatedSizeBytes"), static_cast<double>(estimatedSizeBytes));
    object.insert(QStringLiteral("error"), error.isEmpty() ? QJsonValue() : QJsonValue(error));
    return object;
}

} // namespace PodStack
