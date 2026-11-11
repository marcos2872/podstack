// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QJsonObject>
#include <QString>
#include <QStringList>

namespace PodStack {

/// Retrato read-only do que existe hoje no Docker, usado antes de uma
/// remoção opt-in. Nenhuma operação de escrita é feita no socket.
struct DockerImpact {
    bool available = false;
    QString socketPath;
    int containers = 0;
    int running = 0;
    int images = 0;
    int networks = 0;
    int volumes = 0;
    qint64 estimatedSizeBytes = 0;
    QString error;

    QJsonObject toJson() const;
};

class DockerInventory {
public:
    explicit DockerInventory(QString socketPath = {});

    /// Procura o socket em PODSTACK_DOCKER_SOCKET, DOCKER_HOST,
    /// $XDG_RUNTIME_DIR/docker.sock e /var/run/docker.sock.
    static QStringList socketCandidates();

    DockerImpact inspect(int timeoutMs = 1500) const;

private:
    QString m_socketPath;
};

} // namespace PodStack
