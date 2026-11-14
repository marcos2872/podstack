// SPDX-License-Identifier: GPL-3.0-or-later
#include "machines/manager/Common.h"

using namespace PodStack::MachineInternal;

namespace PodStack {

bool MachineManager::sshInfo(const QString &idOrName, SshInfo *info, Error *error)
{
    if (error) {
        *error = {};
    }
    if (!info) {
        return fails(error, invalidParams(QStringLiteral("Destino SSH inválido.")));
    }
    *info = {};

    const MachineMeta meta = load(idOrName, error);
    if (meta.id.isEmpty()) {
        return false;
    }

    const QFileInfo keyInfo(meta.sshKeyPath);
    if (meta.sshKeyPath.isEmpty() || keyInfo.isSymLink() || !keyInfo.isFile()) {
        return fails(error,
                     Error::make(QStringLiteral("PODSTACK_IO_ERROR"),
                                 QStringLiteral("A chave SSH da machine não existe."),
                                 meta.id,
                                 QStringLiteral("Recrie a machine para gerar uma nova chave.")));
    }
    if (!QFile::setPermissions(meta.sshKeyPath, QFileDevice::ReadOwner
                                                   | QFileDevice::WriteOwner)) {
        return fails(error,
                     Error::make(QStringLiteral("PODSTACK_PERMISSION_DENIED"),
                                 QStringLiteral("Não foi possível restringir a chave SSH "
                                                "a 0600."),
                                 meta.id));
    }

    Error syncError;
    if (!m_sshConfig.sync(meta, meta.forwardSshAgent, &syncError)) {
        if (error) {
            *error = syncError;
        }
        return false;
    }

    info->host = QStringLiteral("127.0.0.1");
    info->port = meta.sshPort;
    info->user = meta.sshUser;
    info->identityFile = meta.sshKeyPath;
    info->forwardAgent = meta.forwardSshAgent;
    return true;
}

QString MachineManager::virtiofsSocketPath(const QString &machineId, const QString &tag) const
{
    static const QRegularExpression tagPattern(QStringLiteral("^[a-z0-9-]{1,32}$"));
    if (!tagPattern.match(tag).hasMatch()) {
        return {};
    }
    const QString directory = machineDirFor(machineId);
    if (directory.isEmpty()) {
        return {};
    }
    return directory + QStringLiteral("/virtiofs-") + tag + QStringLiteral(".sock");
}

bool MachineManager::startVirtiofs(const MachineMeta &meta, Error *error)
{
    if (error) {
        *error = {};
    }
    std::lock_guard<std::recursive_mutex> lock(m_virtiofsMutex);
    stopVirtiofs(meta.id);
    if (meta.mounts.isEmpty()) {
        return true;
    }

    for (const MachineMount &mount : meta.mounts) {
        if (m_virtiofs[meta.id].find(mount.tag) != m_virtiofs[meta.id].end()) {
            stopVirtiofs(meta.id);
            return fails(error,
                         invalidParams(QStringLiteral("Tag de mount duplicada."), mount.tag));
        }
        const QString socketPath = virtiofsSocketPath(meta.id, mount.tag);
        if (socketPath.isEmpty()) {
            stopVirtiofs(meta.id);
            return fails(error,
                         invalidParams(QStringLiteral("Tag de mount inválida."), mount.tag));
        }

        auto process = std::make_unique<VirtiofsdProcess>();
        Error startError;
        if (!process->start(m_options.virtiofsdBinary, mount.source, socketPath, mount.tag,
                            mount.readOnly, &startError)) {
            qCWarning(lcCore) << "Falha ao iniciar o virtiofsd do mount" << mount.source
                              << "da machine" << meta.name << ":" << startError.code
                              << startError.detail;
            stopVirtiofs(meta.id);
            if (error) {
                *error = startError;
                if (error->detail.isEmpty()) {
                    error->detail = QStringLiteral("mount %1 (%2)").arg(mount.tag, mount.source);
                }
            }
            return false;
        }
        m_virtiofs[meta.id].emplace(mount.tag, std::move(process));
    }
    return true;
}

bool MachineManager::stopVirtiofs(const QString &machineId, Error *error)
{
    if (error) {
        *error = {};
    }
    std::lock_guard<std::recursive_mutex> lock(m_virtiofsMutex);
    const auto it = m_virtiofs.find(machineId);
    if (it == m_virtiofs.end()) {
        return true;
    }

    bool ok = true;
    for (auto processIt = it->second.begin(); processIt != it->second.end(); ++processIt) {
        Error stopError;
        if (!processIt->second->stop(&stopError)) {
            ok = false;
            qCWarning(lcCore) << "Falha ao encerrar o virtiofsd da machine" << machineId
                              << "tag" << processIt->first << ":" << stopError.code;
            if (error && error->isEmpty()) {
                *error = stopError;
            }
        }
    }
    m_virtiofs.erase(it);
    return ok;
}

} // namespace PodStack
