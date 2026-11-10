// SPDX-License-Identifier: GPL-3.0-or-later

#include "daemon/HttpProxy.h"

#include "core/Logging.h"

#include <QAbstractSocket>
#include <QHostAddress>
#include <QRegularExpression>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>

namespace PodStack {
namespace {

constexpr int kMaxHeaderBytes = 32 * 1024;

struct RequestHead {
    QByteArray method;
    QByteArray target;
    QByteArray version;
    QByteArray host;
    QList<QPair<QByteArray, QByteArray>> headers;
};

QByteArray reasonPhrase(int status)
{
    switch (status) {
    case 400:
        return QByteArrayLiteral("Bad Request");
    case 404:
        return QByteArrayLiteral("Not Found");
    case 431:
        return QByteArrayLiteral("Request Header Fields Too Large");
    case 502:
        return QByteArrayLiteral("Bad Gateway");
    case 504:
        return QByteArrayLiteral("Gateway Timeout");
    default:
        return QByteArrayLiteral("Error");
    }
}

QByteArray errorBody(const QString &title, const QString &detail)
{
    const QString html =
        QStringLiteral("<!doctype html><html lang=\"pt-BR\"><head><meta charset=\"utf-8\">"
                       "<title>%1 — PodStack</title></head><body><h1>%1</h1><p>%2</p>"
                       "<p>PodStack · proxy local</p></body></html>")
            .arg(title.toHtmlEscaped(), detail.toHtmlEscaped());
    return html.toUtf8();
}

bool parseRequest(const QByteArray &head, RequestHead *request)
{
    const QList<QByteArray> lines = head.split('\n');
    if (lines.isEmpty()) {
        return false;
    }

    QByteArray first = lines.first();
    if (first.endsWith('\r')) {
        first.chop(1);
    }
    const QList<QByteArray> parts = first.split(' ');
    if (parts.size() != 3 || parts.at(0).isEmpty() || parts.at(1).isEmpty() ||
        !parts.at(2).startsWith("HTTP/")) {
        return false;
    }

    static const QRegularExpression methodPattern(QStringLiteral("^[A-Z]+$"));
    if (!methodPattern.match(QString::fromLatin1(parts.at(0))).hasMatch() ||
        !parts.at(1).startsWith('/')) {
        return false;
    }

    request->method = parts.at(0);
    request->target = parts.at(1);
    request->version = parts.at(2);

    for (int index = 1; index < lines.size(); ++index) {
        QByteArray line = lines.at(index);
        if (line.endsWith('\r')) {
            line.chop(1);
        }
        if (line.isEmpty()) {
            continue;
        }
        const int colon = line.indexOf(':');
        if (colon <= 0) {
            return false;
        }
        const QByteArray name = line.left(colon).trimmed();
        const QByteArray value = line.mid(colon + 1).trimmed();
        if (name.isEmpty()) {
            return false;
        }
        request->headers.append({name, value});
        if (name.compare("host", Qt::CaseInsensitive) == 0) {
            request->host = value;
        }
    }

    return !request->host.isEmpty();
}

QByteArray buildRequest(const RequestHead &request, const QString &clientAddress)
{
    QByteArray output = request.method + ' ' + request.target + ' ' + request.version + "\r\n";
    for (const auto &header : request.headers) {
        const QByteArray name = header.first.toLower();
        if (name == "connection" || name == "keep-alive" || name == "proxy-connection" ||
            name == "upgrade") {
            continue;
        }
        output += header.first + ": " + header.second + "\r\n";
    }
    output += "X-Forwarded-For: " + clientAddress.toLatin1() + "\r\n";
    output += "X-Forwarded-Proto: http\r\n";
    output += "X-Forwarded-Host: " + request.host + "\r\n";
    output += "Connection: close\r\n\r\n";
    return output;
}

class ProxyConnection : public QObject {
public:
    ProxyConnection(QTcpSocket *client, HttpProxy::ContainerProvider provider, int connectTimeoutMs,
                    int idleTimeoutMs, QObject *parent)
        : QObject(parent), m_client(client), m_provider(std::move(provider)),
          m_connectTimeoutMs(connectTimeoutMs), m_idleTimeoutMs(idleTimeoutMs)
    {
        m_client->setParent(this);
        m_connectTimer = new QTimer(this);
        m_connectTimer->setSingleShot(true);
        m_idleTimer = new QTimer(this);
        m_idleTimer->setSingleShot(true);

        connect(m_client, &QTcpSocket::readyRead, this, &ProxyConnection::onClientData);
        connect(m_client, &QTcpSocket::disconnected, this, [this] {
            if (m_backend) {
                m_backend->abort();
            }
            deleteLater();
        });
        connect(m_connectTimer, &QTimer::timeout, this, [this] {
            if (!m_connected) {
                fail(504, QStringLiteral("Tempo esgotado"),
                     QStringLiteral("O container não respondeu a tempo."));
            }
        });
        connect(m_idleTimer, &QTimer::timeout, this, [this] {
            if (m_backend) {
                m_backend->abort();
            }
            m_client->abort();
        });
    }

private:
    void onClientData()
    {
        if (m_responseSent) {
            return;
        }
        const QByteArray data = m_client->readAll();
        m_idleTimer->start(m_idleTimeoutMs);

        if (m_headParsed) {
            if (m_connected && m_backend) {
                m_backend->write(data);
            } else {
                m_pending.append(data);
            }
            return;
        }

        m_buffer.append(data);
        const int headerEnd = m_buffer.indexOf("\r\n\r\n");
        if (headerEnd < 0) {
            if (m_buffer.size() > kMaxHeaderBytes) {
                fail(431, QStringLiteral("Cabeçalho muito grande"),
                     QStringLiteral("A requisição excede o limite de 32 KiB."));
            }
            return;
        }
        if (headerEnd > kMaxHeaderBytes) {
            fail(431, QStringLiteral("Cabeçalho muito grande"),
                 QStringLiteral("A requisição excede o limite de 32 KiB."));
            return;
        }
        if (!parseRequest(m_buffer.left(headerEnd), &m_request)) {
            fail(400, QStringLiteral("Requisição inválida"),
                 QStringLiteral("Cabeçalhos HTTP inválidos."));
            return;
        }

        m_headParsed = true;
        m_headRequest = m_request.method == "HEAD";
        m_pending = m_buffer.mid(headerEnd + 4);
        m_buffer.clear();
        resolveAndConnect();
    }

    void resolveAndConnect()
    {
        QString name;
        if (!HttpProxy::hostName(m_request.host, &name)) {
            fail(404, QStringLiteral("Domínio não encontrado"),
                 QStringLiteral("Use http://<nome>.localhost:<porta>/; recebido Host: %1.")
                     .arg(QString::fromLatin1(m_request.host)));
            return;
        }

        QVector<ContainerInfo> containers;
        Error error;
        if (!m_provider || !m_provider(&containers, &error)) {
            fail(502, QStringLiteral("Podman indisponível"),
                 error.message.isEmpty() ? QStringLiteral("Não foi possível listar containers.")
                                         : error.message);
            return;
        }

        const ContainerInfo *container = nullptr;
        for (const ContainerInfo &candidate : containers) {
            if (candidate.name == name) {
                container = &candidate;
                break;
            }
        }
        if (!container) {
            fail(404, QStringLiteral("Container não encontrado"),
                 QStringLiteral("Nenhum container chamado \"%1\" está em execução.").arg(name));
            return;
        }

        QString host;
        quint16 port = 0;
        if (!HttpProxy::publishedPort(*container, &host, &port)) {
            fail(404, QStringLiteral("Sem porta publicada"),
                 QStringLiteral("O container \"%1\" não publica portas TCP.").arg(name));
            return;
        }
        connectBackend(host, port);
    }

    void connectBackend(const QString &host, quint16 port)
    {
        m_backend = new QTcpSocket(this);
        connect(m_backend, &QTcpSocket::connected, this, [this] {
            if (m_responseSent) {
                m_backend->abort();
                return;
            }
            m_connected = true;
            m_connectTimer->stop();
            m_backend->write(buildRequest(m_request, m_client->peerAddress().toString()));
            if (!m_pending.isEmpty()) {
                m_backend->write(m_pending);
                m_pending.clear();
            }
            connect(m_backend, &QTcpSocket::readyRead, this, [this] {
                m_idleTimer->start(m_idleTimeoutMs);
                m_client->write(m_backend->readAll());
            });
            connect(m_backend, &QTcpSocket::disconnected, this, [this] {
                m_idleTimer->stop();
                m_client->disconnectFromHost();
            });
        });
        connect(m_backend, &QTcpSocket::errorOccurred, this, [this](QAbstractSocket::SocketError) {
            if (!m_connected) {
                fail(502, QStringLiteral("Container indisponível"),
                     QStringLiteral("Não foi possível conectar à porta publicada."));
            } else {
                m_client->disconnectFromHost();
            }
        });
        m_connectTimer->start(m_connectTimeoutMs);
        m_idleTimer->start(m_idleTimeoutMs);
        m_backend->connectToHost(host, port);
    }

    void fail(int status, const QString &title, const QString &detail)
    {
        if (m_responseSent) {
            return;
        }
        m_responseSent = true;
        m_connectTimer->stop();
        m_idleTimer->stop();

        const QByteArray body = errorBody(title, detail);
        QByteArray response = "HTTP/1.1 " + QByteArray::number(status) + ' ' +
                              reasonPhrase(status) +
                              "\r\nContent-Type: text/html; charset=utf-8\r\n"
                              "Content-Length: " +
                              QByteArray::number(body.size()) +
                              "\r\nCache-Control: no-store\r\nConnection: close\r\n\r\n";
        if (!m_headRequest) {
            response += body;
        }
        m_client->write(response);
        m_client->disconnectFromHost();
    }

    QTcpSocket *m_client = nullptr;
    QTcpSocket *m_backend = nullptr;
    HttpProxy::ContainerProvider m_provider;
    int m_connectTimeoutMs = 10000;
    int m_idleTimeoutMs = 60000;
    QTimer *m_connectTimer = nullptr;
    QTimer *m_idleTimer = nullptr;
    QByteArray m_buffer;
    QByteArray m_pending;
    RequestHead m_request;
    bool m_headParsed = false;
    bool m_headRequest = false;
    bool m_connected = false;
    bool m_responseSent = false;
};

} // namespace

HttpProxy::HttpProxy(ContainerProvider provider, QObject *parent)
    : QObject(parent), m_provider(std::move(provider))
{
}

bool HttpProxy::listen(quint16 port, Error *error)
{
    if (error) {
        *error = {};
    }
    close();

    auto *v4 = new QTcpServer(this);
    if (!v4->listen(QHostAddress::LocalHost, port)) {
        const QString detail = v4->errorString();
        delete v4;
        if (error) {
            *error = Error::make(
                QStringLiteral("PODSTACK_IO_ERROR"),
                QStringLiteral("Não foi possível escutar em 127.0.0.1:%1.").arg(port), detail);
        }
        return false;
    }
    m_v4 = v4;
    connect(m_v4, &QTcpServer::newConnection, this, [this] { accept(m_v4); });

    auto *v6 = new QTcpServer(this);
    if (v6->listen(QHostAddress::LocalHostIPv6, m_v4->serverPort())) {
        m_v6 = v6;
        connect(m_v6, &QTcpServer::newConnection, this, [this] { accept(m_v6); });
    } else {
        qCDebug(lcRpc) << "Proxy HTTP sem listener IPv6:" << v6->errorString();
        delete v6;
    }

    qCInfo(lcRpc) << "Proxy HTTP de *.localhost em 127.0.0.1:" << m_v4->serverPort();
    return true;
}

void HttpProxy::close()
{
    const QList<QTcpServer *> servers{m_v4, m_v6};
    for (QTcpServer *server : servers) {
        if (!server) {
            continue;
        }
        server->close();
        server->deleteLater();
    }
    m_v4 = nullptr;
    m_v6 = nullptr;
}

quint16 HttpProxy::port() const
{
    return m_v4 ? m_v4->serverPort() : 0;
}

void HttpProxy::setTimeouts(int connectTimeoutMs, int idleTimeoutMs)
{
    m_connectTimeoutMs = connectTimeoutMs;
    m_idleTimeoutMs = idleTimeoutMs;
}

void HttpProxy::accept(QTcpServer *server)
{
    while (server->hasPendingConnections()) {
        QTcpSocket *socket = server->nextPendingConnection();
        if (!socket) {
            break;
        }
        new ProxyConnection(socket, m_provider, m_connectTimeoutMs, m_idleTimeoutMs, this);
    }
}

bool HttpProxy::hostName(const QByteArray &hostHeader, QString *name)
{
    QString host = QString::fromLatin1(hostHeader).trimmed().toLower();
    if (host.isEmpty() || host.startsWith(QLatin1Char('['))) {
        return false;
    }

    const int colon = host.lastIndexOf(QLatin1Char(':'));
    if (colon >= 0) {
        const QString port = host.mid(colon + 1);
        bool ok = false;
        const uint value = port.toUInt(&ok);
        if (!ok || value < 1 || value > 65535) {
            return false;
        }
        host.truncate(colon);
    }

    static const QString suffix = QStringLiteral(".localhost");
    if (!host.endsWith(suffix)) {
        return false;
    }
    const QString label = host.left(host.size() - suffix.size());
    if (label.isEmpty() || label.contains(QLatin1Char('.'))) {
        return false;
    }

    static const QRegularExpression validLabel(
        QStringLiteral("^[a-z0-9]([a-z0-9-]{0,61}[a-z0-9])?$"));
    if (!validLabel.match(label).hasMatch()) {
        return false;
    }
    if (name) {
        *name = label;
    }
    return true;
}

bool HttpProxy::publishedPort(const ContainerInfo &container, QString *host, quint16 *port)
{
    static const QRegularExpression entry(
        QStringLiteral("^(?:(\\[[^\\]]+\\]|[^:]+):)?(\\d{1,5})->(\\d{1,5})/([A-Za-z]+)$"));
    for (const QString &text : container.ports) {
        const QRegularExpressionMatch match = entry.match(text.trimmed());
        if (!match.hasMatch() ||
            match.captured(4).compare(QStringLiteral("tcp"), Qt::CaseInsensitive) != 0) {
            continue;
        }
        bool ok = false;
        const int hostPort = match.captured(2).toInt(&ok);
        if (!ok || hostPort < 1 || hostPort > 65535) {
            continue;
        }

        QString address = match.captured(1);
        if (address.isEmpty() || address == QStringLiteral("0.0.0.0") ||
            address == QStringLiteral("::")) {
            address = QStringLiteral("127.0.0.1");
        } else if (address.startsWith(QLatin1Char('[')) && address.endsWith(QLatin1Char(']'))) {
            address = address.mid(1, address.size() - 2);
        }

        if (host) {
            *host = address;
        }
        if (port) {
            *port = static_cast<quint16>(hostPort);
        }
        return true;
    }
    return false;
}

} // namespace PodStack
