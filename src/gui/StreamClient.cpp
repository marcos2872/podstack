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

void StreamClient::unsubscribe()
{
    stop(true);
}

void StreamClient::stop(bool sendUnsubscribe)
{
    if (sendUnsubscribe && !m_subscriptionId.isEmpty()
        && m_socket.state() == QLocalSocket::ConnectedState) {
        QJsonObject message;
        message.insert(QStringLiteral("jsonrpc"), QStringLiteral("2.0"));
        message.insert(QStringLiteral("id"), 2);
        message.insert(QStringLiteral("method"),
                       m_unsubscribeMethod.isEmpty()
                           ? QStringLiteral("containers.") + m_kind
                                 + QStringLiteral(".unsubscribe")
                           : m_unsubscribeMethod);
        message.insert(QStringLiteral("params"),
                       QJsonObject{{QStringLiteral("subscriptionId"), m_subscriptionId}});
        send(message);
    }

    m_subscriptionId.clear();
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
            m_subscriptionId = message.value(QStringLiteral("result"))
                                   .toObject()
                                   .value(QStringLiteral("subscriptionId"))
                                   .toString();
            continue;
        }

        const QString method = message.value(QStringLiteral("method")).toString();
        const QJsonObject params = message.value(QStringLiteral("params")).toObject();

        if (method == QStringLiteral("containers.logs.delta")) {
            emit logLine(params.value(QStringLiteral("line")).toString());
        } else if (method == QStringLiteral("containers.stats.delta")) {
            emit stats(params);
        } else if (method == QStringLiteral("images.pull.delta")) {
            emit pullEvent(params);
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
                   || method == QStringLiteral("containers.stats.error")) {
            emit error(params.value(QStringLiteral("error"))
                           .toObject()
                           .value(QStringLiteral("message"))
                           .toString());
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
}

} // namespace PodStack
