// SPDX-License-Identifier: GPL-3.0-or-later
#include "daemon/rpc/handlers/Common.h"

using namespace PodStack::DaemonInternal;

namespace PodStack {

QJsonObject Daemon::volumeClone(const QJsonObject &params, Error *error)
{
    const QString source = params.value(QStringLiteral("source")).toString();
    const QString destination = params.value(QStringLiteral("destination")).toString();
    if (!m_podman.cloneVolume(source, destination, error)) {
        return {};
    }

    QJsonObject result;
    result.insert(QStringLiteral("source"), source);
    result.insert(QStringLiteral("destination"), destination);
    result.insert(QStringLiteral("ok"), true);
    return result;
}

QJsonObject Daemon::volumeCreate(const QJsonObject &params, Error *error)
{
    QMap<QString, QString> options;
    const QJsonObject optionsObject = params.value(QStringLiteral("options")).toObject();
    for (auto it = optionsObject.constBegin(); it != optionsObject.constEnd(); ++it) {
        options.insert(it.key(), it.value().toString());
    }

    const QJsonObject json =
        m_podman.createVolume(params.value(QStringLiteral("name")).toString(),
                              params.value(QStringLiteral("driver")).toString(),
                              labelsFromParams(params), options, error);
    if (error && !error->isEmpty()) {
        return {};
    }
    return json;
}

QJsonObject Daemon::volumeExport(const QJsonObject &params, Error *error)
{
    const QString name = params.value(QStringLiteral("name")).toString();
    const QString path = params.value(QStringLiteral("path")).toString();
    if (!m_podman.exportVolume(name, path, error)) {
        return {};
    }

    QJsonObject result;
    result.insert(QStringLiteral("name"), name);
    result.insert(QStringLiteral("path"), path);
    return result;
}

QJsonObject Daemon::volumeImport(const QJsonObject &params, Error *error)
{
    const QString name = params.value(QStringLiteral("name")).toString();
    const QString path = params.value(QStringLiteral("path")).toString();
    const bool force = params.value(QStringLiteral("force")).toBool(false);
    if (!m_podman.importVolume(name, path, force, error)) {
        return {};
    }

    QJsonObject result;
    result.insert(QStringLiteral("name"), name);
    result.insert(QStringLiteral("path"), path);
    result.insert(QStringLiteral("ok"), true);
    return result;
}

QJsonObject Daemon::volumeImportHostPath(const QJsonObject &params, Error *error)
{
    const QString name = params.value(QStringLiteral("name")).toString();
    const QString sourcePath = params.value(QStringLiteral("sourcePath")).toString();
    if (!m_podman.importHostPath(name, sourcePath, error)) {
        return {};
    }

    QJsonObject result;
    result.insert(QStringLiteral("name"), name);
    result.insert(QStringLiteral("sourcePath"), sourcePath);
    result.insert(QStringLiteral("ok"), true);
    return result;
}

QJsonObject Daemon::volumePrune(Error *error)
{
    qint64 reclaimed = 0;
    if (!m_podman.pruneVolumes(&reclaimed, error)) {
        return {};
    }

    QJsonObject result;
    result.insert(QStringLiteral("reclaimedBytes"), static_cast<double>(reclaimed));
    return result;
}

QJsonObject Daemon::volumeRemove(const QJsonObject &params, Error *error)
{
    const QString name = params.value(QStringLiteral("name")).toString();
    if (name.isEmpty()) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_INVALID_PARAMS"),
                                 QStringLiteral("Informe o nome do volume."));
        }
        return {};
    }

    const bool force = params.value(QStringLiteral("force")).toBool(false);
    if (!m_podman.removeVolume(name, force, error)) {
        return {};
    }

    QJsonObject result;
    result.insert(QStringLiteral("name"), name);
    result.insert(QStringLiteral("removed"), true);
    return result;
}

QJsonObject Daemon::volumesList(Error *error)
{
    QJsonArray items;
    for (const VolumeInfo &volume : m_podman.listVolumes(error)) {
        items.append(volume.toJson());
    }
    if (error && !error->isEmpty()) {
        return {};
    }

    QJsonObject result;
    result.insert(QStringLiteral("items"), items);
    result.insert(QStringLiteral("total"), items.size());
    return result;
}

} // namespace PodStack
