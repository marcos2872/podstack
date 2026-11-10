// SPDX-License-Identifier: GPL-3.0-or-later

#include "daemon/HttpProxy.h"

#include "core/Logging.h"

#include <QAbstractSocket>
#include <QDateTime>
#include <QHash>
#include <QHostAddress>
#include <QRegularExpression>
#include <QSslSocket>
#include <QStringList>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>

#include <algorithm>
#include <memory>

namespace PodStack {

struct PortCacheEntry {
    QString digest;
    QString host;
    quint16 port = 0;
    bool tls = false;
    qint64 updatedAt = 0;
};

struct PortCache {
    QHash<QString, PortCacheEntry> entries;
};

namespace {

constexpr int kMaxHeaderBytes = 32 * 1024;

struct RequestHead {
    QByteArray method;
    QByteArray target;
    QByteArray version;
    QByteArray host;
    QList<QPair<QByteArray, QByteArray>> headers;
};

constexpr int kProbeTimeoutMs = 300;
constexpr int kPortCacheLimit = 64;
constexpr qint64 kPortCacheTtlMs = 10LL * 60 * 1000;

/// Portas de container que costumam servir HTTP; usadas como preferência no
/// probe concorrente e documentadas na spec §11.4.
const QVector<int> &commonHttpPorts()
{
    static const QVector<int> ports{80, 8080, 3000, 5000, 8000, 8081, 8888, 443, 8443};
    return ports;
}

int portPriority(const PortMapping &mapping)
{
    const QVector<int> &common = commonHttpPorts();
    for (int index = 0; index < common.size(); ++index) {
        if (mapping.containerPort == common.at(index) || mapping.hostPort == common.at(index)) {
            return index;
        }
    }
    return common.size();
}

QVector<PortMapping> orderedCandidates(const ContainerInfo &container)
{
    QVector<PortMapping> candidates = HttpProxy::publishedTcpPorts(container);
    std::stable_sort(candidates.begin(), candidates.end(),
                     [](const PortMapping &a, const PortMapping &b) {
                         const int priorityA = portPriority(a);
                         const int priorityB = portPriority(b);
                         if (priorityA != priorityB) {
                             return priorityA < priorityB;
                         }
                         return a.containerPort < b.containerPort;
                     });
    return candidates;
}

constexpr auto kHttpPortLabel = "dev.podstack.http-port";
constexpr auto kHttpsPortLabel = "dev.podstack.https-port";
constexpr auto kDomainsLabel = "dev.podstack.domains";

/// Domínios extras declarados no label `dev.podstack.domains`.
QStringList customDomains(const ContainerInfo &container)
{
    static const QRegularExpression validDomain(QStringLiteral("^[a-z0-9]([a-z0-9.-]*[a-z0-9])?$"));
    QStringList domains;
    const QString raw = container.labels.value(QString::fromLatin1(kDomainsLabel));
    for (QString domain : raw.split(QLatin1Char(','), Qt::SkipEmptyParts)) {
        domain = domain.trimmed().toLower();
        if (domain.isEmpty() || domain.size() > 253 || domain.contains(QStringLiteral("..")) ||
            !validDomain.match(domain).hasMatch()) {
            continue;
        }
        domains.append(domain);
    }
    domains.removeDuplicates();
    return domains;
}

/// Porta forçada por label: aceita tanto a porta do container quanto a porta
/// publicada no host. Retorna `false` quando o label não casa com nenhuma porta.
bool labeledTarget(const ContainerInfo &container, const QString &label, QString *host,
                   quint16 *port)
{
    const QString value = container.labels.value(label).trimmed();
    bool ok = false;
    const int wanted = value.toInt(&ok);
    if (!ok || wanted < 1 || wanted > 65535) {
        return false;
    }
    for (const PortMapping &mapping : HttpProxy::publishedTcpPorts(container)) {
        if (mapping.containerPort != wanted && mapping.hostPort != wanted) {
            continue;
        }
        if (host) {
            *host = mapping.hostIp;
        }
        if (port) {
            *port = static_cast<quint16>(mapping.hostPort);
        }
        return true;
    }
    return false;
}

/// Digest da configuração que influencia a detecção; o id do container é a
/// chave do cache, então recriar o container troca a chave naturalmente.
QString containerDigest(const ContainerInfo &container)
{
    QStringList parts{container.image, container.ports.join(QLatin1Char('\n'))};
    parts.append(QString::fromLatin1(kHttpPortLabel) + QLatin1Char('=') +
                 container.labels.value(QString::fromLatin1(kHttpPortLabel)));
    parts.append(QString::fromLatin1(kHttpsPortLabel) + QLatin1Char('=') +
                 container.labels.value(QString::fromLatin1(kHttpsPortLabel)));
    return parts.join(QLatin1Char('\n'));
}

QByteArray probeRequest(const QString &name)
{
    return "HEAD / HTTP/1.1\r\nHost: " + name.toUtf8() +
           ".localhost\r\nUser-Agent: PodStack-Server-Detection\r\nAccept: */*\r\n"
           "Connection: close\r\n\r\n";
}

struct ProbeState {
    QTcpSocket *socket = nullptr;
    QTimer *timer = nullptr;
    QByteArray response;
    PortMapping candidate;
    int priority = 0;
    bool finished = false;
};

QByteArray reasonPhrase(int status)
{
    switch (status) {
    case 400:
        return QByteArrayLiteral("Bad Request");
    case 404:
        return QByteArrayLiteral("Not Found");
    case 405:
        return QByteArrayLiteral("Method Not Allowed");
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

QByteArray htmlDocument(const QString &title, const QString &content)
{
    const QString html =
        QStringLiteral("<!doctype html><html lang=\"pt-BR\"><head><meta charset=\"utf-8\">"
                       "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">"
                       "<title>%1 — PodStack</title><style>"
                       "body{font-family:system-ui,sans-serif;margin:2rem auto;max-width:48rem;"
                       "padding:0 1rem;color:#1f2937;background:#fafafa}"
                       "h1{font-size:1.5rem;margin-bottom:.25rem}"
                       "p{color:#4b5563}"
                       "table{width:100%;border-collapse:collapse;margin-top:1rem}"
                       "th,td{text-align:left;padding:.5rem;border-bottom:1px solid #e5e7eb}"
                       "th{font-size:.8rem;text-transform:uppercase;letter-spacing:.04em;"
                       "color:#6b7280}"
                       "code{background:#eef2ff;padding:.15rem .35rem;border-radius:.25rem}"
                       "footer{margin-top:2rem;font-size:.8rem;color:#9ca3af}"
                       "</style></head><body>%2"
                       "<footer>PodStack · proxy local</footer></body></html>")
            .arg(title.toHtmlEscaped(), content);
    return html.toUtf8();
}

QByteArray errorBody(const QString &title, const QString &detail)
{
    const QString content =
        QStringLiteral("<h1>%1</h1><p>%2</p>").arg(title.toHtmlEscaped(), detail.toHtmlEscaped());
    return htmlDocument(title, content);
}

bool isValidLabel(const QString &label)
{
    static const QRegularExpression validLabel(
        QStringLiteral("^[a-z0-9]([a-z0-9-]{0,61}[a-z0-9])?$"));
    return validLabel.match(label).hasMatch();
}

/// Página de índice de serviços: lista os containers com porta TCP publicada e
/// o endereço `http://<nome>.localhost:<porta do proxy>/` de cada um.
QByteArray indexBody(const QVector<ContainerInfo> &containers, quint16 proxyPort,
                     const QString &title, const QString &detail)
{
    QVector<const ContainerInfo *> entries;
    for (const ContainerInfo &container : containers) {
        QString host;
        quint16 port = 0;
        if (HttpProxy::publishedPort(container, &host, &port)) {
            entries.append(&container);
        }
    }
    std::sort(entries.begin(), entries.end(), [](const ContainerInfo *a, const ContainerInfo *b) {
        return a->name.compare(b->name, Qt::CaseInsensitive) < 0;
    });

    QString content = QStringLiteral("<h1>%1</h1>").arg(title.toHtmlEscaped());
    if (!detail.isEmpty()) {
        content += QStringLiteral("<p>%1</p>").arg(detail.toHtmlEscaped());
    }

    if (entries.isEmpty()) {
        content += QStringLiteral("<p>Nenhum container com porta TCP publicada.</p>");
        return htmlDocument(title, content);
    }

    content += QStringLiteral(
        "<table><thead><tr><th>Serviço</th><th>Estado</th><th>Porta</th><th>URL</th></tr>"
        "</thead><tbody>");
    for (const ContainerInfo *entry : entries) {
        QString host;
        quint16 port = 0;
        HttpProxy::publishedPort(*entry, &host, &port);

        const QString state =
            entry->state.isEmpty() ? QStringLiteral("—") : entry->state.toHtmlEscaped();
        const QString lowerName = entry->name.toLower();
        QStringList urls;
        if (entry->name == lowerName && isValidLabel(lowerName)) {
            urls.append(QStringLiteral("http://%1.localhost:%2/").arg(lowerName).arg(proxyPort));
        }
        for (const QString &domain : customDomains(*entry)) {
            urls.append(QStringLiteral("http://%1:%2/").arg(domain).arg(proxyPort));
        }
        QString url;
        if (urls.isEmpty()) {
            url = QStringLiteral("<span>nome incompatível com Host</span>");
        } else {
            QStringList links;
            for (const QString &text : urls) {
                links.append(
                    QStringLiteral("<a href=\"%1\"><code>%1</code></a>").arg(text.toHtmlEscaped()));
            }
            url = links.join(QStringLiteral("<br>"));
        }
        content += QStringLiteral("<tr><td>%1</td><td>%2</td><td>%3</td><td>%4</td></tr>")
                       .arg(entry->name.toHtmlEscaped(), state, QString::number(port), url);
    }
    content += QStringLiteral("</tbody></table>");
    content += QStringLiteral("<p>Também é possível acessar cada serviço pela porta publicada: "
                              "<code>http://localhost:&lt;porta&gt;/</code>.</p>");
    return htmlDocument(title, content);
}

/// Remove a porta opcional de `Host` e valida o valor (sem IPv6 literal).
bool splitHost(const QByteArray &hostHeader, QString *host)
{
    QString value = QString::fromLatin1(hostHeader).trimmed().toLower();
    if (value.isEmpty() || value.startsWith(QLatin1Char('['))) {
        return false;
    }

    const int colon = value.lastIndexOf(QLatin1Char(':'));
    if (colon >= 0) {
        const QString port = value.mid(colon + 1);
        bool ok = false;
        const uint number = port.toUInt(&ok);
        if (!ok || number < 1 || number > 65535) {
            return false;
        }
        value.truncate(colon);
    }

    if (host) {
        *host = value;
    }
    return true;
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
    ProxyConnection(QTcpSocket *client, HttpProxy::ContainerProvider provider,
                    std::shared_ptr<PortCache> portCache, quint16 proxyPort, int connectTimeoutMs,
                    int idleTimeoutMs, QObject *parent)
        : QObject(parent), m_client(client), m_provider(std::move(provider)),
          m_portCache(std::move(portCache)), m_proxyPort(proxyPort),
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
        if (HttpProxy::isIndexHost(m_request.host)) {
            if (m_request.method != "GET" && m_request.method != "HEAD") {
                fail(405, QStringLiteral("Método não permitido"),
                     QStringLiteral("A página de índice aceita apenas GET e HEAD."),
                     QByteArrayLiteral("Allow: GET, HEAD\r\n"));
                return;
            }
            serveIndex();
            return;
        }

        QString name;
        const bool zoneHost = HttpProxy::hostName(m_request.host, &name);

        QVector<ContainerInfo> containers;
        if (!fetchContainers(&containers)) {
            return;
        }

        const ContainerInfo *container = nullptr;
        if (zoneHost) {
            for (const ContainerInfo &candidate : containers) {
                if (candidate.name == name) {
                    container = &candidate;
                    break;
                }
            }
            if (!container) {
                sendIndex(
                    404, QStringLiteral("Container não encontrado"),
                    QStringLiteral("Nenhum container chamado \"%1\" está em execução.").arg(name),
                    containers);
                return;
            }
        } else {
            QString customHost;
            if (splitHost(m_request.host, &customHost)) {
                for (const ContainerInfo &candidate : containers) {
                    if (customDomains(candidate).contains(customHost)) {
                        container = &candidate;
                        break;
                    }
                }
            }
            if (!container) {
                fail(404, QStringLiteral("Domínio não encontrado"),
                     QStringLiteral("Use http://<nome>.localhost[:porta]/ ou um domínio "
                                    "declarado em %1; recebido Host: %2.")
                         .arg(QString::fromLatin1(kDomainsLabel),
                              QString::fromLatin1(m_request.host)));
                return;
            }
            name = container->name;
        }

        if (HttpProxy::publishedTcpPorts(*container).isEmpty()) {
            sendIndex(
                404, QStringLiteral("Sem porta publicada"),
                QStringLiteral("O container \"%1\" não publica portas TCP.").arg(container->name),
                containers);
            return;
        }

        QString labeledHost;
        quint16 labeledPort = 0;
        if (labeledTarget(*container, QString::fromLatin1(kHttpPortLabel), &labeledHost,
                          &labeledPort)) {
            rememberTarget(container->id.isEmpty() ? name : container->id,
                           containerDigest(*container), labeledHost, labeledPort, false);
            connectBackend(labeledHost, labeledPort, false);
            return;
        }
        if (labeledTarget(*container, QString::fromLatin1(kHttpsPortLabel), &labeledHost,
                          &labeledPort)) {
            rememberTarget(container->id.isEmpty() ? name : container->id,
                           containerDigest(*container), labeledHost, labeledPort, true);
            connectBackend(labeledHost, labeledPort, true);
            return;
        }
        resolveContainerPort(*container, name);
    }

    void resolveContainerPort(const ContainerInfo &container, const QString &name)
    {
        const QString key = container.id.isEmpty() ? name : container.id;
        const QString digest = containerDigest(container);

        if (m_portCache) {
            const auto cached = m_portCache->entries.constFind(key);
            if (cached != m_portCache->entries.constEnd() && cached->digest == digest &&
                cached->port != 0) {
                connectBackend(cached->host, cached->port, cached->tls);
                return;
            }
        }

        const QVector<PortMapping> candidates = orderedCandidates(container);
        if (candidates.isEmpty()) {
            fail(502, QStringLiteral("Nenhuma porta disponível"),
                 QStringLiteral("O container não tem porta TCP publicada."));
            return;
        }
        if (candidates.size() == 1) {
            const PortMapping &only = candidates.first();
            rememberTarget(key, digest, only.hostIp, quint16(only.hostPort), false);
            connectBackend(only.hostIp, quint16(only.hostPort), false);
            return;
        }
        probePorts(key, digest, name, candidates);
    }

    void probePorts(const QString &key, const QString &digest, const QString &name,
                    const QVector<PortMapping> &candidates)
    {
        m_probeKey = key;
        m_probeDigest = digest;
        m_probeName = name;
        m_candidates = candidates;
        m_pendingProbes = candidates.size();
        m_bestHost.clear();
        m_bestPort = 0;
        m_bestPriority = candidates.size();

        for (int index = 0; index < candidates.size(); ++index) {
            auto state = std::make_shared<ProbeState>();
            state->candidate = candidates.at(index);
            state->priority = index;
            state->socket = new QTcpSocket(this);
            state->timer = new QTimer(state->socket);
            state->timer->setSingleShot(true);

            QTcpSocket *socket = state->socket;
            connect(socket, &QTcpSocket::connected, this, [this, state] {
                if (state->finished) {
                    return;
                }
                state->socket->write(probeRequest(m_probeName));
            });
            connect(socket, &QTcpSocket::readyRead, this, [this, state] {
                if (state->finished) {
                    return;
                }
                state->response.append(state->socket->readAll());
                if (state->response.startsWith("HTTP/")) {
                    finishProbe(state, true);
                }
            });
            connect(socket, &QTcpSocket::errorOccurred, this,
                    [this, state](QAbstractSocket::SocketError) { finishProbe(state, false); });
            connect(state->timer, &QTimer::timeout, this,
                    [this, state] { finishProbe(state, false); });
            state->timer->start(kProbeTimeoutMs);
            socket->connectToHost(state->candidate.hostIp, quint16(state->candidate.hostPort));
            m_probes.append(state);
        }
    }

    void finishProbe(const std::shared_ptr<ProbeState> &state, bool success)
    {
        if (state->finished) {
            return;
        }
        state->finished = true;
        state->timer->stop();
        state->socket->abort();
        state->socket->deleteLater();

        if (success && state->priority < m_bestPriority) {
            m_bestPriority = state->priority;
            m_bestHost = state->candidate.hostIp;
            m_bestPort = quint16(state->candidate.hostPort);
        }
        if (--m_pendingProbes > 0) {
            return;
        }

        if (m_bestPort == 0 && !m_candidates.isEmpty()) {
            const PortMapping &fallback = m_candidates.first();
            m_bestHost = fallback.hostIp;
            m_bestPort = quint16(fallback.hostPort);
        }
        if (m_bestPort == 0) {
            fail(502, QStringLiteral("Nenhuma porta disponível"),
                 QStringLiteral("O container não tem porta TCP publicada."));
            return;
        }
        rememberTarget(m_probeKey, m_probeDigest, m_bestHost, m_bestPort, false);
        connectBackend(m_bestHost, m_bestPort, false);
    }

    void rememberTarget(const QString &key, const QString &digest, const QString &host,
                        quint16 port, bool tls)
    {
        if (!m_portCache) {
            return;
        }
        PortCache &cache = *m_portCache;
        if (cache.entries.size() > kPortCacheLimit) {
            const qint64 cutoff = QDateTime::currentMSecsSinceEpoch() - kPortCacheTtlMs;
            for (auto it = cache.entries.begin(); it != cache.entries.end();) {
                if (it->updatedAt < cutoff) {
                    it = cache.entries.erase(it);
                } else {
                    ++it;
                }
            }
            if (cache.entries.size() > kPortCacheLimit) {
                cache.entries.clear();
            }
        }
        cache.entries.insert(
            key, PortCacheEntry{digest, host, port, tls, QDateTime::currentMSecsSinceEpoch()});
    }

    bool fetchContainers(QVector<ContainerInfo> *containers)
    {
        Error error;
        if (!m_provider || !m_provider(containers, &error)) {
            fail(502, QStringLiteral("Podman indisponível"),
                 error.message.isEmpty() ? QStringLiteral("Não foi possível listar containers.")
                                         : error.message);
            return false;
        }
        return true;
    }

    void serveIndex()
    {
        QVector<ContainerInfo> containers;
        if (!fetchContainers(&containers)) {
            return;
        }
        sendIndex(200, QStringLiteral("Serviços locais"), QString(), containers);
    }

    void sendIndex(int status, const QString &title, const QString &detail,
                   const QVector<ContainerInfo> &containers)
    {
        respond(status, indexBody(containers, m_proxyPort, title, detail));
    }

    void connectBackend(const QString &host, quint16 port, bool tls)
    {
        m_backend = tls ? new QSslSocket(this) : new QTcpSocket(this);
        const auto onConnected = [this] {
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
        };
        const auto onError = [this](QAbstractSocket::SocketError) {
            if (!m_connected) {
                fail(502, QStringLiteral("Container indisponível"),
                     QStringLiteral("Não foi possível conectar à porta publicada."));
            } else {
                m_client->disconnectFromHost();
            }
        };

        m_connectTimer->start(m_connectTimeoutMs);
        m_idleTimer->start(m_idleTimeoutMs);
        if (tls) {
            auto *ssl = static_cast<QSslSocket *>(m_backend);
            ssl->setPeerVerifyMode(QSslSocket::VerifyNone);
            connect(ssl, &QSslSocket::encrypted, this, onConnected);
            connect(ssl, &QAbstractSocket::errorOccurred, this, onError);
            ssl->connectToHostEncrypted(host, port);
        } else {
            connect(m_backend, &QTcpSocket::connected, this, onConnected);
            connect(m_backend, &QAbstractSocket::errorOccurred, this, onError);
            m_backend->connectToHost(host, port);
        }
    }

    void fail(int status, const QString &title, const QString &detail,
              const QByteArray &extraHeaders = {})
    {
        respond(status, errorBody(title, detail), extraHeaders);
    }

    void respond(int status, const QByteArray &body, const QByteArray &extraHeaders = {})
    {
        if (m_responseSent) {
            return;
        }
        m_responseSent = true;
        m_connectTimer->stop();
        m_idleTimer->stop();

        QByteArray response = "HTTP/1.1 " + QByteArray::number(status) + ' ' +
                              reasonPhrase(status) +
                              "\r\nContent-Type: text/html; charset=utf-8\r\n"
                              "Content-Length: " +
                              QByteArray::number(body.size()) + "\r\nCache-Control: no-store\r\n" +
                              extraHeaders + "Connection: close\r\n\r\n";
        if (!m_headRequest) {
            response += body;
        }
        m_client->write(response);
        m_client->disconnectFromHost();
    }

    QTcpSocket *m_client = nullptr;
    QTcpSocket *m_backend = nullptr;
    HttpProxy::ContainerProvider m_provider;
    std::shared_ptr<PortCache> m_portCache;
    quint16 m_proxyPort = 0;
    int m_connectTimeoutMs = 10000;
    int m_idleTimeoutMs = 60000;
    QTimer *m_connectTimer = nullptr;
    QTimer *m_idleTimer = nullptr;
    QByteArray m_buffer;
    QByteArray m_pending;
    RequestHead m_request;
    QString m_probeKey;
    QString m_probeDigest;
    QString m_probeName;
    QString m_bestHost;
    QVector<PortMapping> m_candidates;
    QVector<std::shared_ptr<ProbeState>> m_probes;
    quint16 m_bestPort = 0;
    int m_bestPriority = 0;
    int m_pendingProbes = 0;
    bool m_headParsed = false;
    bool m_headRequest = false;
    bool m_connected = false;
    bool m_responseSent = false;
};

} // namespace

HttpProxy::HttpProxy(ContainerProvider provider, QObject *parent)
    : QObject(parent), m_provider(std::move(provider)), m_portCache(std::make_shared<PortCache>())
{
}

bool HttpProxy::listen(quint16 port, Error *error)
{
    if (error) {
        *error = {};
    }
    close();

    const QHostAddress bindV4 =
        m_exposeLan ? QHostAddress(QHostAddress::Any) : QHostAddress(QHostAddress::LocalHost);
    const QHostAddress bindV6 = m_exposeLan ? QHostAddress(QHostAddress::AnyIPv6)
                                            : QHostAddress(QHostAddress::LocalHostIPv6);
    auto *v4 = new QTcpServer(this);
    if (!v4->listen(bindV4, port)) {
        const QString detail = v4->errorString();
        delete v4;
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_IO_ERROR"),
                                 QStringLiteral("Não foi possível escutar em %1:%2.")
                                     .arg(bindV4.toString(), QString::number(port)),
                                 detail);
        }
        return false;
    }
    m_v4 = v4;
    connect(m_v4, &QTcpServer::newConnection, this, [this] { accept(m_v4); });

    auto *v6 = new QTcpServer(this);
    if (v6->listen(bindV6, m_v4->serverPort())) {
        m_v6 = v6;
        connect(m_v6, &QTcpServer::newConnection, this, [this] { accept(m_v6); });
    } else {
        qCDebug(lcRpc) << "Proxy HTTP sem listener IPv6:" << v6->errorString();
        delete v6;
    }

    qCInfo(lcRpc) << "Proxy HTTP de *.localhost em" << (m_exposeLan ? "0.0.0.0" : "127.0.0.1")
                  << ":" << m_v4->serverPort();
    if (m_exposeLan) {
        qCWarning(lcRpc) << "Proxy exposto na LAN; garanta que o firewall permita as portas.";
    }
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

void HttpProxy::setExposeLan(bool exposeLan)
{
    m_exposeLan = exposeLan;
}

bool HttpProxy::exposeLan() const
{
    return m_exposeLan;
}

void HttpProxy::accept(QTcpServer *server)
{
    while (server->hasPendingConnections()) {
        QTcpSocket *socket = server->nextPendingConnection();
        if (!socket) {
            break;
        }
        new ProxyConnection(socket, m_provider, m_portCache, server->serverPort(),
                            m_connectTimeoutMs, m_idleTimeoutMs, this);
    }
}

bool HttpProxy::hostName(const QByteArray &hostHeader, QString *name)
{
    QString host;
    if (!splitHost(hostHeader, &host)) {
        return false;
    }

    static const QStringList suffixes{QStringLiteral(".localhost"),
                                      QStringLiteral(".podstack.local")};
    QString suffix;
    for (const QString &candidate : suffixes) {
        if (host.endsWith(candidate)) {
            suffix = candidate;
            break;
        }
    }
    if (suffix.isEmpty()) {
        return false;
    }
    const QString label = host.left(host.size() - suffix.size());
    if (label.isEmpty() || label.contains(QLatin1Char('.')) || !isValidLabel(label)) {
        return false;
    }
    if (name) {
        *name = label;
    }
    return true;
}

bool HttpProxy::isIndexHost(const QByteArray &hostHeader)
{
    QString host;
    return splitHost(hostHeader, &host) &&
           (host == QStringLiteral("localhost") || host == QStringLiteral("podstack.local"));
}

QVector<PortMapping> HttpProxy::publishedTcpPorts(const ContainerInfo &container)
{
    static const QRegularExpression entry(
        QStringLiteral("^(?:(\\[[^\\]]+\\]|[^:]+):)?(\\d{1,5})->(\\d{1,5})/([A-Za-z]+)$"));
    QVector<PortMapping> mappings;
    for (const QString &text : container.ports) {
        const QRegularExpressionMatch match = entry.match(text.trimmed());
        if (!match.hasMatch() ||
            match.captured(4).compare(QStringLiteral("tcp"), Qt::CaseInsensitive) != 0) {
            continue;
        }
        bool hostOk = false;
        bool containerOk = false;
        const int hostPort = match.captured(2).toInt(&hostOk);
        const int containerPort = match.captured(3).toInt(&containerOk);
        if (!hostOk || !containerOk || hostPort < 1 || hostPort > 65535 || containerPort < 1 ||
            containerPort > 65535) {
            continue;
        }

        QString address = match.captured(1);
        if (address.isEmpty() || address == QStringLiteral("0.0.0.0") ||
            address == QStringLiteral("::")) {
            address = QStringLiteral("127.0.0.1");
        } else if (address.startsWith(QLatin1Char('[')) && address.endsWith(QLatin1Char(']'))) {
            address = address.mid(1, address.size() - 2);
        }

        PortMapping mapping;
        mapping.hostPort = hostPort;
        mapping.containerPort = containerPort;
        mapping.protocol = QStringLiteral("tcp");
        mapping.hostIp = address;
        mappings.append(mapping);
    }
    return mappings;
}

bool HttpProxy::publishedPort(const ContainerInfo &container, QString *host, quint16 *port)
{
    const QVector<PortMapping> mappings = publishedTcpPorts(container);
    if (mappings.isEmpty()) {
        return false;
    }
    if (host) {
        *host = mappings.first().hostIp;
    }
    if (port) {
        *port = static_cast<quint16>(mappings.first().hostPort);
    }
    return true;
}

} // namespace PodStack
