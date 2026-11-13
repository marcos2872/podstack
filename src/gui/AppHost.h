// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "machines/AppSessionManager.h"

#include <QHash>
#include <QObject>
#include <QProcess>

namespace PodStack {

/// Hospeda e supervisiona os clientes locais (xpra/ssh) na sessão gráfica da GUI.
class AppHost : public QObject {
    Q_OBJECT

public:
    explicit AppHost(QObject *parent = nullptr);
    ~AppHost() override;

    bool start(const QString &sessionId, const LaunchSpec &spec);
    void stopLocal(const QString &sessionId);
    void shutdownAll();
    bool isRunning(const QString &sessionId) const;
    QStringList sessionIds() const;

signals:
    void clientExited(const QString &sessionId);
    void clientFailed(const QString &sessionId, const QString &message);

private:
    QHash<QString, QProcess *> m_processes;
};

} // namespace PodStack
