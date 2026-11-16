#pragma once

#include "core/error/Error.h"
#include "podman/PodmanClient.h"

#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>

#include <mutex>

namespace PodStack {

struct ComposeServiceInfo {
    QString name;
    QStringList containerIds;
    QString status;

    QJsonObject toJson() const;
};

struct ComposeProjectInfo {
    QString name;
    QString workingDir;
    QStringList configFiles;
    QVector<ComposeServiceInfo> services;
    QString status;

    QJsonObject toJson() const;
};

/// Agrupa containers por labels do Compose e delega up/down ao provider instalado.
class ComposeManager {
public:
    explicit ComposeManager(PodmanClient *podman);

    QVector<ComposeProjectInfo> listProjects(Error *error = nullptr);
    QString provider(Error *error = nullptr);
    bool up(const QString &projectPath, bool detach, Error *error = nullptr);
    bool down(const QString &projectPath, bool volumes, Error *error = nullptr);
    bool restart(const QString &projectPath, const QString &service, Error *error = nullptr);
    bool stop(const QString &projectPath, const QString &service, Error *error = nullptr);
    bool start(const QString &projectPath, const QString &service, Error *error = nullptr);

    static QString findConfigFile(const QString &projectPath);

private:
    bool runCompose(const QString &projectPath, const QStringList &args, int timeoutMs,
                    QString *output, Error *error);
    /// Resolve e devolve o provider (vazio em falha). Protegido por
    /// `m_providerMutex`: handlers RPC chamam `resolveProvider` em threads
    /// concorrentes.
    QString resolveProvider(Error *error);

    PodmanClient *m_podman = nullptr;
    mutable std::mutex m_providerMutex;
    QString m_provider;
};

} // namespace PodStack
