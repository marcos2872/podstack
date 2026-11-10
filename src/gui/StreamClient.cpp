#include "gui/StreamClient.h"

#include "core/Paths.h"

#include <QJsonDocument>
#include <QJsonParseError>

namespace PodStack {

StreamClient::StreamClient(QObject *parent)
    : QObject(parent)
{
    connect(&m_socket, &QLocalSocket::connected, this, &StreamClient::onConnected);
    connect(&m_socket, &QLocalSocket::readyRead, this, &StreamClient::onReadyRead);
    connect(&m_socket, &QLocalSocket::errorOccurred, this, &StreamClient::onError);
}

void StreamClient::subscribeLogs(const QString &id, int tail, bool follow)
{
    stop(true);
    m_kind = QStringLiteral("logs");
    m_unsubscribeMethod = QStringLiteral("containers.logs.unsubscribe");
    m_pending = {
        {QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
        {QStringLiteral("id"), 1},
        {QStringLiteral("method"), QStringLiteral("containers.logs.subscribe")},
        {QStringLiteral("params"),
         QJsonObject{{QStringLiteral("id"), id},
                     {QStringLiteral("tail"), tail},
                     {QStringLiteral("follow"), follow}}},
    };
    m_pendingSend = true;
    connectSocket();
}

void StreamClient::subscribeStats(const QString &id, int intervalSeconds)
{
    stop(true);
    m_kind = QStringLiteral("stats");
    m_unsubscribeMethod = QStringLiteral("containers.stats.unsubscribe");
    m_pending = {
        {QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
        {QStringLiteral("id"), 1},
        {QStringLiteral("method"), QStringLiteral("containers.stats.subscribe")},
        {QStringLiteral("params"),
         QJsonObject{{QStringLiteral("id"), id},
                     {QStringLiteral("intervalSeconds"), intervalSeconds}}},
    };
    m_pendingSend = true;
    connectSocket();
}

void StreamClient::subscribeSystemStats(int intervalSeconds)
{
    stop(true);
    m_kind = QStringLiteral("system-stats");
    m_unsubscribeMethod = QStringLiteral("system.stats.unsubscribe");
    m_pending = {
        {QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
        {QStringLiteral("id"), 1},
        {QStringLiteral("method"), QStringLiteral("system.stats.subscribe")},
        {QStringLiteral("params"),
         QJsonObject{{QStringLiteral("intervalSeconds"), intervalSeconds}}},
    };
    m_pendingSend = true;
    connectSocket();
}

void StreamClient::subscribeMachineStats(const QString &id, int intervalSeconds)
{
    stop(true);
    m_kind = QStringLiteral("machine-stats");
    m_unsubscribeMethod = QStringLiteral("machines.stats.unsubscribe");
    m_pending = {
        {QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
        {QStringLiteral("id"), 1},
        {QStringLiteral("method"), QStringLiteral("machines.stats.subscribe")},
        {QStringLiteral("params"),
         QJsonObject{{QStringLiteral("id"), id},
                     {QStringLiteral("intervalSeconds"), intervalSeconds}}},
    };
    m_pendingSend = true;
    connectSocket();
}

void StreamClient::startMachineConsole(const QString &id, int tailBytes)
{
    stop(true);
    m_kind = QStringLiteral("machine-console");
    m_unsubscribeMethod = QStringLiteral("machines.console.unsubscribe");
    m_pending = {
        {QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
        {QStringLiteral("id"), 1},
        {QStringLiteral("method"), QStringLiteral("machines.console")},
        {QStringLiteral("params"),
         QJsonObject{{QStringLiteral("id"), id},
                     {QStringLiteral("tailBytes"), tailBytes}}},
    };
    m_pendingSend = true;
    connectSocket();
}

void StreamClient::stopMachineConsole()
{
    stop(true);
}

void StreamClient::pullImage(const QString &reference, const QString &platform)
{
    stop(true);
    m_kind = QStringLiteral("pull");
    m_unsubscribeMethod = QStringLiteral("images.pull.unsubscribe");
    QJsonObject params;
    params.insert(QStringLiteral("reference"), reference);
    if (!platform.isEmpty()) {
        params.insert(QStringLiteral("platform"), platform);
    }
    m_pending = {
        {QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
        {QStringLiteral("id"), 1},
        {QStringLiteral("method"), QStringLiteral("images.pull")},
        {QStringLiteral("params"), params},
    };
    m_pendingSend = true;
    connectSocket();
}

void StreamClient::subscribeComposeLogs(const QString &project, const QString &service)
{
    stop(true);
    m_kind = QStringLiteral("compose");
    m_unsubscribeMethod = QStringLiteral("compose.logs.unsubscribe");
    QJsonObject params;
    params.insert(QStringLiteral("project"), project);
    if (!service.isEmpty()) {
        params.insert(QStringLiteral("service"), service);
    }
    m_pending = {
        {QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
        {QStringLiteral("id"), 1},
        {QStringLiteral("method"), QStringLiteral("compose.logs.subscribe")},
        {QStringLiteral("params"), params},
    };
    m_pendingSend = true;
    connectSocket();
}

void StreamClient::startBuild(const QJsonObject &params)
{
    stop(true);
    m_kind = QStringLiteral("build");
    m_unsubscribeMethod = QStringLiteral("builds.unsubscribe");
    m_pending = {
        {QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
        {QStringLiteral("id"), 1},
        {QStringLiteral("method"), QStringLiteral("builds.start")},
        {QStringLiteral("params"), params},
    };
    m_pendingSend = true;
    connectSocket();
}

void StreamClient::startMachineCreate(const QJsonObject &params)
{
    startMachineJob(QStringLiteral("machines.create"), params);
}

void StreamClient::startMachineExport(const QJsonObject &params)
{
    startMachineJob(QStringLiteral("machines.export"), params);
}

void StreamClient::startMachineImport(const QJsonObject &params)
{
    startMachineJob(QStringLiteral("machines.import"), params);
}

void StreamClient::startMachineClone(const QJsonObject &params)
{
    startMachineJob(QStringLiteral("machines.clone"), params);
}

void StreamClient::startMachineJob(const QString &method, const QJsonObject &params)
{
    // Não há `machines.unsubscribe`: o daemon remove a subscription ao terminar.
    stop(false);
    m_kind = QStringLiteral("machine-job");
    m_unsubscribeMethod.clear();
    m_pending = {
        {QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
        {QStringLiteral("id"), 1},
        {QStringLiteral("method"), method},
        {QStringLiteral("params"), params},
    };
    m_pendingSend = true;
    connectSocket();
}

void StreamClient::startSetup(const QJsonObject &params)
{
    stop(true);
    m_kind = QStringLiteral("setup");
    m_unsubscribeMethod = QStringLiteral("setup.unsubscribe");
    m_pending = {
        {QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
        {QStringLiteral("id"), 1},
        {QStringLiteral("method"), QStringLiteral("setup.apply")},
        {QStringLiteral("params"), params},
    };
    m_pendingSend = true;
    connectSocket();
}

void StreamClient::startSetupRemoveDocker(const QJsonObject &params)
{
    stop(true);
    m_kind = QStringLiteral("setup");
    m_unsubscribeMethod = QStringLiteral("setup.unsubscribe");
    m_pending = {
        {QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
        {QStringLiteral("id"), 1},
        {QStringLiteral("method"), QStringLiteral("setup.removeDocker")},
        {QStringLiteral("params"), params},
    };
    m_pendingSend = true;
    connectSocket();
}

void StreamClient::subscribeSetup(const QString &jobId)
{
    stop(true);
    m_kind = QStringLiteral("setup");
    m_unsubscribeMethod = QStringLiteral("setup.unsubscribe");
    m_pending = {
        {QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
        {QStringLiteral("id"), 1},
        {QStringLiteral("method"), QStringLiteral("setup.subscribe")},
        {QStringLiteral("params"), QJsonObject{{QStringLiteral("jobId"), jobId}}},
    };
    m_pendingSend = true;
    connectSocket();
}

void StreamClient::subscribeEvents()
{
    stop(true);
    m_kind = QStringLiteral("events");
    m_unsubscribeMethod = QStringLiteral("events.unsubscribe");
    m_pending = {
        {QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
        {QStringLiteral("id"), 1},
        {QStringLiteral("method"), QStringLiteral("events.subscribe")},
        {QStringLiteral("params"), QJsonObject{}},
    };
    m_pendingSend = true;
    connectSocket();
}

void StreamClient::unsubscribe()
{
    stop(true);
}

void StreamClient::stop(bool sendUnsubscribe)
{
    if (sendUnsubscribe && !m_subscriptionId.isEmpty() && !m_unsubscribeMethod.isEmpty()
        && m_socket.state() == QLocalSocket::ConnectedState) {
        QJsonObject message;
        message.insert(QStringLiteral("jsonrpc"), QStringLiteral("2.0"));
        message.insert(QStringLiteral("id"), 2);
        message.insert(QStringLiteral("method"), m_unsubscribeMethod);
        message.insert(QStringLiteral("params"),
                       QJsonObject{{QStringLiteral("subscriptionId"), m_subscriptionId}});
        send(message);
    }

    m_subscriptionId.clear();
    m_machineConsoleSubscription.clear();
    m_kind.clear();
    m_pending = {};
    m_pendingSend = false;
    m_buffer.clear();
    m_socket.abort();
}

void StreamClient::connectSocket()
{
    m_buffer.clear();
    m_socket.abort();
    m_socket.connectToServer(Paths::controlSocket(), QIODevice::ReadWrite);
    if (m_socket.state() == QLocalSocket::ConnectedState) {
        onConnected();
    }
}

void StreamClient::send(const QJsonObject &message)
{
    if (m_socket.state() != QLocalSocket::ConnectedState) {
        return;
    }
    m_socket.write(QJsonDocument(message).toJson(QJsonDocument::Compact) + '\n');
    m_socket.flush();
}

void StreamClient::onConnected()
{
    if (m_pendingSend) {
        m_pendingSend = false;
        send(m_pending);
    }
}

void StreamClient::onReadyRead()
{
    m_buffer.append(m_socket.readAll());

    int newline = -1;
    while ((newline = m_buffer.indexOf('\n')) >= 0) {
        const QByteArray line = m_buffer.left(newline);
        m_buffer.remove(0, newline + 1);

        QJsonParseError parseError{};
        const QJsonDocument document = QJsonDocument::fromJson(line, &parseError);
        if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
            continue;
        }

        const QJsonObject message = document.object();
        if (message.value(QStringLiteral("id")).toInt() == 1) {
            if (message.contains(QStringLiteral("error"))) {
                const QJsonObject errorObject =
                    message.value(QStringLiteral("error")).toObject();
                QString text = errorObject.value(QStringLiteral("message")).toString();
                const QJsonObject data =
                    errorObject.value(QStringLiteral("data")).toObject();
                const QString remediation =
                    data.value(QStringLiteral("remediation")).toString();
                if (!remediation.isEmpty()) {
                    text += QStringLiteral(" ") + remediation;
                }
                emit error(text);
                m_pendingSend = false;
                continue;
            }

            m_lastResult = message.value(QStringLiteral("result")).toObject();
            m_subscriptionId =
                m_lastResult.value(QStringLiteral("subscriptionId")).toString();
            if (m_kind == QStringLiteral("machine-console")) {
                m_machineConsoleSubscription = m_subscriptionId;
            }
            emit subscriptionReady(m_lastResult);
            continue;
        }

        const QString method = message.value(QStringLiteral("method")).toString();
        const QJsonObject params = message.value(QStringLiteral("params")).toObject();

        if (method == QStringLiteral("containers.logs.delta")) {
            emit logLine(params.value(QStringLiteral("line")).toString());
        } else if (method == QStringLiteral("containers.stats.delta")) {
            emit stats(params);
        } else if (method == QStringLiteral("system.stats.delta")) {
            emit systemStats(params);
        } else if (method == QStringLiteral("machines.stats.delta")) {
            emit machineStats(params);
        } else if (method == QStringLiteral("machines.console.delta")) {
            emit machineConsoleEvent(params);
        } else if (method == QStringLiteral("images.pull.delta")) {
            emit pullEvent(params);
        } else if (method == QStringLiteral("compose.logs.delta")) {
            emit composeLog(params.value(QStringLiteral("service")).toString(),
                            params.value(QStringLiteral("line")).toString());
        } else if (method == QStringLiteral("builds.delta")) {
            emit buildEvent(params);
        } else if (method == QStringLiteral("builds.done")) {
            emit buildFinished(params);
            m_subscriptionId.clear();
            m_kind.clear();
            m_socket.abort();
        } else if (method == QStringLiteral("machines.delta")) {
            emit machineCreateEvent(params);
        } else if (method == QStringLiteral("machines.done")) {
            // O daemon remove a subscription quando o job termina; não há unsubscribe.
            emit machineCreateFinished(params);
            m_subscriptionId.clear();
            m_kind.clear();
            m_socket.abort();
        } else if (method == QStringLiteral("setup.delta")) {
            emit setupEvent(params);
        } else if (method == QStringLiteral("setup.done")) {
            emit setupFinished(params);
            m_subscriptionId.clear();
            m_kind.clear();
            m_socket.abort();
        } else if (method == QStringLiteral("event")) {
            emit eventReceived(params);
        } else if (method == QStringLiteral("images.pull.done")) {
            emit pullFinished(true, {});
            m_subscriptionId.clear();
            m_kind.clear();
            m_socket.abort();
        } else if (method == QStringLiteral("images.pull.error")) {
            emit pullFinished(false,
                              params.value(QStringLiteral("error"))
                                  .toObject()
                                  .value(QStringLiteral("message"))
                                  .toString());
            m_subscriptionId.clear();
            m_kind.clear();
            m_socket.abort();
        } else if (method == QStringLiteral("containers.logs.error")
                   || method == QStringLiteral("containers.stats.error")
                   || method == QStringLiteral("system.stats.error")) {
            emit error(params.value(QStringLiteral("error"))
                           .toObject()
                           .value(QStringLiteral("message"))
                           .toString());
        } else if (method == QStringLiteral("machines.stats.error")
                   || method == QStringLiteral("machines.console.error")) {
            const QJsonObject errorObject = params.value(QStringLiteral("error")).toObject();
            QString text = errorObject.value(QStringLiteral("message")).toString();
            const QString remediation = errorObject.value(QStringLiteral("remediation")).toString();
            if (!remediation.isEmpty()) {
                text += QStringLiteral(" ") + remediation;
            }
            emit error(text);
            m_subscriptionId.clear();
            m_machineConsoleSubscription.clear();
            m_kind.clear();
            m_socket.abort();
        }
    }
}

void StreamClient::onError()
{
    if (m_pendingSend || !m_subscriptionId.isEmpty()) {
        emit error(m_socket.errorString());
    }
    m_pendingSend = false;
    m_subscriptionId.clear();
    m_machineConsoleSubscription.clear();
}

} // namespace PodStack
