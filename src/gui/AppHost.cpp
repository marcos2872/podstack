// SPDX-License-Identifier: GPL-3.0-or-later

#include "gui/AppHost.h"

#include <QProcessEnvironment>

namespace PodStack {

AppHost::AppHost(QObject *parent)
    : QObject(parent)
{
}

AppHost::~AppHost()
{
    shutdownAll();
}

bool AppHost::start(const QString &sessionId, const LaunchSpec &spec)
{
    if (sessionId.isEmpty() || spec.program.isEmpty() || m_processes.contains(sessionId)) {
        return false;
    }
    auto *process = new QProcess(this);
    process->setProcessChannelMode(QProcess::SeparateChannels);
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    for (auto it = spec.env.constBegin(); it != spec.env.constEnd(); ++it) {
        environment.insert(it.key(), it.value());
    }
    process->setProcessEnvironment(environment);
    connect(process, &QProcess::finished, this, [this, process, sessionId](int, QProcess::ExitStatus) {
        m_processes.remove(sessionId);
        process->deleteLater();
        emit clientExited(sessionId);
    });
    connect(process, &QProcess::errorOccurred, this,
            [this, process, sessionId](QProcess::ProcessError error) {
                if (error != QProcess::FailedToStart) {
                    return;
                }
                process->setProperty("podstackFailed", true);
                m_processes.remove(sessionId);
                const QString message = process->errorString();
                process->deleteLater();
                emit clientFailed(sessionId, message);
            });
    m_processes.insert(sessionId, process);
    process->start(spec.program, spec.args);
    if (!process->waitForStarted(5000)) {
        m_processes.remove(sessionId);
        const QString message = process->errorString();
        const bool alreadyFailed = process->property("podstackFailed").toBool();
        process->deleteLater();
        if (!alreadyFailed) {
            emit clientFailed(sessionId, message);
        }
        return false;
    }
    return true;
}

void AppHost::stopLocal(const QString &sessionId)
{
    QProcess *process = m_processes.take(sessionId);
    if (!process) {
        return;
    }
    process->terminate();
    if (!process->waitForFinished(2000)) {
        process->kill();
        process->waitForFinished(1000);
    }
    process->deleteLater();
}

void AppHost::shutdownAll()
{
    const QStringList ids = m_processes.keys();
    for (const QString &sessionId : ids) {
        stopLocal(sessionId);
    }
}

bool AppHost::isRunning(const QString &sessionId) const
{
    return m_processes.contains(sessionId);
}

QStringList AppHost::sessionIds() const
{
    return m_processes.keys();
}

} // namespace PodStack
