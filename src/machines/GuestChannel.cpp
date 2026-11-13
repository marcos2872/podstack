// SPDX-License-Identifier: GPL-3.0-or-later

#include "machines/GuestChannel.h"

#include "machines/GuestAgentClient.h"
#include "machines/QmpClient.h"

#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>

#include <utility>

namespace PodStack {
namespace {

QString sshBinary()
{
    const QString override = qEnvironmentVariable("PODSTACK_SSH_BINARY").trimmed();
    if (!override.isEmpty()) {
        return override;
    }
    return QStandardPaths::findExecutable(QStringLiteral("ssh"));
}

} // namespace

GuestChannel::GuestChannel(QString agentSocketPath, QString qmpSocketPath,
                           bool reconnectOnFailure)
    : m_agentSocketPath(std::move(agentSocketPath))
    , m_qmpSocketPath(std::move(qmpSocketPath))
    , m_reconnectOnFailure(reconnectOnFailure)
{
}

bool GuestChannel::probeSsh(const MachineMeta &machine, int timeoutMs, Error *error)
{
    if (error) {
        *error = {};
    }
    if (machine.sshPort <= 0 || machine.sshUser.isEmpty() || machine.sshKeyPath.isEmpty()) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_UNSUPPORTED"),
                                 QStringLiteral("A machine não tem destino SSH válido."));
        }
        return false;
    }
    const QString binary = sshBinary();
    if (binary.isEmpty()) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_DEPENDENCY_MISSING"),
                                 QStringLiteral("O cliente SSH não foi encontrado."),
                                 QStringLiteral("ssh"));
        }
        return false;
    }

    const QString identity = machine.sshKeyPath;
    const QStringList arguments{
        QStringLiteral("-i"),
        identity,
        QStringLiteral("-p"),
        QString::number(machine.sshPort),
        QStringLiteral("-o"),
        QStringLiteral("IdentitiesOnly=yes"),
        QStringLiteral("-o"),
        QStringLiteral("StrictHostKeyChecking=accept-new"),
        QStringLiteral("-o"),
        QStringLiteral("UserKnownHostsFile=%1/known_hosts").arg(
            QFileInfo(identity).absolutePath()),
        QStringLiteral("-o"),
        QStringLiteral("BatchMode=yes"),
        QStringLiteral("-o"),
        QStringLiteral("ConnectTimeout=5"),
        QStringLiteral("%1@127.0.0.1").arg(machine.sshUser),
        QStringLiteral("true"),
    };

    QProcess process;
    process.setProcessChannelMode(QProcess::SeparateChannels);
    process.start(binary, arguments);
    if (!process.waitForStarted(timeoutMs)) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_IO_ERROR"),
                                 QStringLiteral("Não foi possível executar o SSH."),
                                 process.errorString());
        }
        return false;
    }
    if (!process.waitForFinished(timeoutMs)) {
        process.kill();
        process.waitForFinished(1000);
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_TIMEOUT"),
                                 QStringLiteral("O SSH não respondeu no tempo limite."));
        }
        return false;
    }
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        if (error) {
            *error = Error::make(
                QStringLiteral("PODSTACK_IO_ERROR"),
                QStringLiteral("O SSH recusou a autenticação."),
                QString::fromUtf8(process.readAllStandardError()));
        }
        return false;
    }
    return true;
}

bool GuestChannel::ping(Error *error)
{
    GuestAgentClient client(m_agentSocketPath);
    client.setConnectTimeout(1500);
    client.setCommandTimeout(1500);
    Error pingError;
    if (!client.connect(&pingError) || !client.ping(&pingError)) {
        if (!m_reconnectOnFailure || m_qmpSocketPath.isEmpty()
            || !QFileInfo::exists(m_agentSocketPath)) {
            if (error) {
                *error = pingError;
            }
            return false;
        }
        Error reconnectError;
        if (!reconnect(&reconnectError)) {
            if (error) {
                *error = reconnectError;
            }
            return false;
        }
        client.disconnect();
        if (!client.connect(&pingError) || !client.ping(&pingError)) {
            if (error) {
                *error = pingError;
            }
            return false;
        }
    }
    return true;
}

bool GuestChannel::getOsInfo(QJsonObject *info, Error *error)
{
    GuestAgentClient client(m_agentSocketPath);
    return client.connect(error) && client.getOsInfo(info, error);
}

bool GuestChannel::getInfo(QJsonObject *info, Error *error)
{
    GuestAgentClient client(m_agentSocketPath);
    return client.connect(error) && client.getInfo(info, error);
}

bool GuestChannel::networkInterfaces(QJsonArray *interfaces, Error *error)
{
    GuestAgentClient client(m_agentSocketPath);
    return client.connect(error) && client.networkInterfaces(interfaces, error);
}

bool GuestChannel::exec(const QString &path, const QStringList &arguments, int timeoutMs,
                        int *exitCode, QByteArray *stdoutData, QByteArray *stderrData,
                        Error *error)
{
    GuestAgentClient client(m_agentSocketPath);
    return client.connect(error)
           && client.exec(path, arguments, timeoutMs, exitCode, stdoutData, stderrData, error);
}

bool GuestChannel::readFile(const QString &path, qint64 maxBytes, QByteArray *data, Error *error)
{
    GuestAgentClient client(m_agentSocketPath);
    return client.connect(error) && client.readFile(path, maxBytes, data, error);
}

bool GuestChannel::writeFile(const QString &path, const QByteArray &data, Error *error)
{
    GuestAgentClient client(m_agentSocketPath);
    return client.connect(error) && client.writeFile(path, data, error);
}

bool GuestChannel::reconnect(Error *error)
{
    if (error) {
        *error = {};
    }
    if (m_qmpSocketPath.isEmpty() || m_agentSocketPath.isEmpty()) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_UNSUPPORTED"),
                                 QStringLiteral("A machine não tem QMP/agente para reconectar."));
        }
        return false;
    }

    QmpClient qmp(m_qmpSocketPath);
    if (!qmp.connect(error)) {
        return false;
    }

    Error ignored;
    qmp.execute(QStringLiteral("device_del"),
                QJsonObject{{QStringLiteral("id"), QStringLiteral("qga0")}}, nullptr, &ignored);
    ignored = {};
    qmp.execute(QStringLiteral("chardev-remove"),
                QJsonObject{{QStringLiteral("id"), QStringLiteral("qga")}}, nullptr, &ignored);

    const QJsonObject chardevArguments{
        {QStringLiteral("id"), QStringLiteral("qga")},
        {QStringLiteral("backend"),
         QJsonObject{
             {QStringLiteral("type"), QStringLiteral("socket")},
             {QStringLiteral("data"),
              QJsonObject{
                  {QStringLiteral("addr"),
                   QJsonObject{
                       {QStringLiteral("type"), QStringLiteral("unix")},
                       {QStringLiteral("data"),
                        QJsonObject{{QStringLiteral("path"), m_agentSocketPath}}},
                   }},
                  {QStringLiteral("server"), true},
                  {QStringLiteral("wait"), false},
              }},
         }},
    };
    if (!qmp.execute(QStringLiteral("chardev-add"), chardevArguments, nullptr, error)) {
        return false;
    }

    const QJsonObject deviceArguments{
        {QStringLiteral("driver"), QStringLiteral("virtserialport")},
        {QStringLiteral("id"), QStringLiteral("qga0")},
        {QStringLiteral("chardev"), QStringLiteral("qga")},
        {QStringLiteral("name"), QStringLiteral("org.qemu.guest_agent.0")},
        {QStringLiteral("bus"), QStringLiteral("virtserial0.0")},
    };
    return qmp.execute(QStringLiteral("device_add"), deviceArguments, nullptr, error);
}

} // namespace PodStack
