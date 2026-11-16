// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QString>
#include <QThread>
#include <QThreadPool>
#include <QtGlobal>

namespace PodStack {

/// Pool dos handlers JSON-RPC. Fica separado do pool de tarefas longas para
/// que um stream infinito (events/logs/stats/console) não consuma a thread
/// que atenderia `system.hello`/`containers.list` — que era a causa de o
/// daemon parecer zumbi em máquinas com poucos núcleos.
inline QThreadPool *rpcPool()
{
    // Intencionalmente vazado: vive até o processo terminar e evita ordem de
    // destruição com tarefas ainda em execução.
    static QThreadPool *pool = [] {
        auto *created = new QThreadPool();
        const int cores = qMax(1, QThread::idealThreadCount());
        created->setMaxThreadCount(qBound(4, cores * 2, 16));
        created->setObjectName(QStringLiteral("podstack-rpc"));
        return created;
    }();
    return pool;
}

/// Pool de tarefas longas: streams (events/logs/stats/console), pull/push,
/// builds, jobs de machine/k8s e setup. Cada stream bloqueia uma thread em
/// curl; por isso o limite é bem maior que o número de núcleos.
/// `PODSTACK_BACKGROUND_POOL_SIZE` reduz o pool em testes de estagnação.
inline QThreadPool *backgroundPool()
{
    static QThreadPool *pool = [] {
        const int configured = qEnvironmentVariableIntValue("PODSTACK_BACKGROUND_POOL_SIZE");
        const int maxThreads = configured > 0 ? qBound(1, configured, 512)
                                              : qMax(32, qMax(2, QThread::idealThreadCount()) * 8);
        auto *created = new QThreadPool();
        created->setMaxThreadCount(maxThreads);
        created->setObjectName(QStringLiteral("podstack-background"));
        return created;
    }();
    return pool;
}

} // namespace PodStack
