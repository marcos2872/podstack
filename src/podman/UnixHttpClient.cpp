#include "podman/UnixHttpClient.h"

#include <QByteArray>
#include <QHash>
#include <QString>

#include <curl/curl.h>

#include <QElapsedTimer>
#include <QThread>

#include <mutex>
#include <utility>

namespace PodStack {
namespace {

void ensureCurlInit()
{
    static std::once_flag flag;
    std::call_once(flag, [] { curl_global_init(CURL_GLOBAL_DEFAULT); });
}

struct WriteContext {
    QByteArray *body = nullptr;
    const std::function<bool(const QByteArray &)> *stream = nullptr;
};

size_t writeCallback(char *ptr, size_t size, size_t nmemb, void *userdata)
{
    const size_t bytes = size * nmemb;
    auto *context = static_cast<WriteContext *>(userdata);
    if (context->stream) {
        if (!(*context->stream)(QByteArray(ptr, static_cast<int>(bytes)))) {
            return 0;
        }
    } else if (context->body) {
        context->body->append(ptr, static_cast<int>(bytes));
    }
    return bytes;
}

size_t headerCallback(char *buffer, size_t size, size_t nitems, void *userdata)
{
    const size_t bytes = size * nitems;
    auto *headers = static_cast<QHash<QString, QString> *>(userdata);
    const QByteArray line(buffer, static_cast<int>(bytes));
    const int colon = line.indexOf(':');
    if (colon > 0) {
        const QString name = QString::fromLatin1(line.left(colon)).trimmed().toLower();
        const QString value = QString::fromLatin1(line.mid(colon + 1)).trimmed();
        headers->insert(name, value);
    }
    return bytes;
}

int progressCallback(void *clientp, curl_off_t, curl_off_t, curl_off_t, curl_off_t)
{
    auto *cancel = static_cast<std::atomic_bool *>(clientp);
    return (cancel && cancel->load()) ? 1 : 0;
}

} // namespace

UnixHttpClient::UnixHttpClient(QString socketPath)
    : m_socketPath(std::move(socketPath))
{
    ensureCurlInit();
}

HttpResponse UnixHttpClient::request(const QByteArray &method, const QString &path,
                                    const QByteArray &body, const QString &contentType,
                                    int timeoutMs)
{
    HttpResponse response;
    CURL *curl = curl_easy_init();
    if (!curl) {
        response.error = QStringLiteral("Falha ao inicializar o libcurl.");
        return response;
    }

    const QByteArray socketPath = m_socketPath.toUtf8();
    const QByteArray url = QByteArrayLiteral("http://localhost") + path.toUtf8();

    WriteContext context;
    context.body = &response.body;

    struct curl_slist *headers = nullptr;
    if (!contentType.isEmpty()) {
        headers = curl_slist_append(headers, ("Content-Type: " + contentType).toUtf8().constData());
    }

    curl_easy_setopt(curl, CURLOPT_UNIX_SOCKET_PATH, socketPath.constData());
    curl_easy_setopt(curl, CURLOPT_URL, url.constData());
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, method.constData());
    curl_easy_setopt(curl, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_1_1);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, static_cast<long>(timeoutMs));
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &context);
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, headerCallback);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, &response.headers);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 0L);

    if (!body.isEmpty()) {
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.constData());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(body.size()));
    }

    const CURLcode result = curl_easy_perform(curl);
    if (result != CURLE_OK) {
        response.error = QString::fromUtf8(curl_easy_strerror(result));
    } else {
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response.status);
    }

    if (headers) {
        curl_slist_free_all(headers);
    }
    curl_easy_cleanup(curl);
    return response;
}

bool UnixHttpClient::stream(const QString &path,
                            const std::function<bool(const QByteArray &)> &onData,
                            std::atomic_bool *cancel, QString *error, int timeoutMs)
{
    return streamRequest(QByteArrayLiteral("GET"), path, {}, {}, onData, cancel, error, timeoutMs);
}

bool UnixHttpClient::streamRequest(const QByteArray &method, const QString &path,
                                   const QByteArray &body, const QString &contentType,
                                   const std::function<bool(const QByteArray &)> &onData,
                                   std::atomic_bool *cancel, QString *error, int timeoutMs)
{
    if (error) {
        error->clear();
    }

    CURL *curl = curl_easy_init();
    if (!curl) {
        if (error) {
            *error = QStringLiteral("Falha ao inicializar o libcurl.");
        }
        return false;
    }

    const QByteArray socketPath = m_socketPath.toUtf8();
    const QByteArray url = QByteArrayLiteral("http://localhost") + path.toUtf8();

    WriteContext context;
    context.stream = &onData;

    struct curl_slist *headers = nullptr;
    if (!contentType.isEmpty()) {
        headers = curl_slist_append(headers, ("Content-Type: " + contentType).toUtf8().constData());
    }

    curl_easy_setopt(curl, CURLOPT_UNIX_SOCKET_PATH, socketPath.constData());
    curl_easy_setopt(curl, CURLOPT_URL, url.constData());
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, method.constData());
    curl_easy_setopt(curl, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_1_1);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &context);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, progressCallback);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, cancel);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 0L);
    if (timeoutMs > 0) {
        curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, static_cast<long>(timeoutMs));
    }
    if (!body.isEmpty()) {
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.constData());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(body.size()));
    }

    const CURLcode result = curl_easy_perform(curl);
    const bool ok = result == CURLE_OK || result == CURLE_WRITE_ERROR;
    if (!ok && error) {
        *error = QString::fromUtf8(curl_easy_strerror(result));
    }

    if (headers) {
        curl_slist_free_all(headers);
    }
    curl_easy_cleanup(curl);
    return ok;
}

bool UnixHttpClient::hijack(const QString &path, const QByteArray &body,
                            const std::function<bool(const QByteArray &)> &onData,
                            std::atomic_bool *cancel, QString *error, int timeoutMs)
{
    if (error) {
        error->clear();
    }

    CURL *curl = curl_easy_init();
    if (!curl) {
        if (error) {
            *error = QStringLiteral("Falha ao inicializar o libcurl.");
        }
        return false;
    }

    const QByteArray socketPath = m_socketPath.toUtf8();
    const QByteArray url = QByteArrayLiteral("http://localhost") + path.toUtf8();

    curl_easy_setopt(curl, CURLOPT_UNIX_SOCKET_PATH, socketPath.constData());
    curl_easy_setopt(curl, CURLOPT_URL, url.constData());
    curl_easy_setopt(curl, CURLOPT_CONNECT_ONLY, 1L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);

    CURLcode code = curl_easy_perform(curl);
    if (code != CURLE_OK) {
        if (error) {
            *error = QString::fromUtf8(curl_easy_strerror(code));
        }
        curl_easy_cleanup(curl);
        return false;
    }

    QByteArray request;
    request += "POST " + path.toUtf8() + " HTTP/1.1\r\n";
    request += "Host: localhost\r\n";
    request += "Content-Type: application/json\r\n";
    request += "Content-Length: " + QByteArray::number(body.size()) + "\r\n";
    request += "Connection: Upgrade\r\n";
    request += "Upgrade: tcp\r\n\r\n";
    request += body;

    QElapsedTimer timer;
    timer.start();

    size_t sent = 0;
    while (sent < static_cast<size_t>(request.size())) {
        if (cancel && cancel->load()) {
            curl_easy_cleanup(curl);
            return false;
        }

        size_t written = 0;
        code = curl_easy_send(curl, request.constData() + sent,
                              static_cast<size_t>(request.size()) - sent, &written);
        if (code == CURLE_OK) {
            sent += written;
        } else if (code == CURLE_AGAIN) {
            if (timer.elapsed() > timeoutMs) {
                if (error) {
                    *error = QStringLiteral("Timeout ao enviar a requisição de upgrade.");
                }
                curl_easy_cleanup(curl);
                return false;
            }
            QThread::msleep(5);
        } else {
            if (error) {
                *error = QString::fromUtf8(curl_easy_strerror(code));
            }
            curl_easy_cleanup(curl);
            return false;
        }
    }

    QByteArray response;
    int headerEnd = -1;
    while (headerEnd < 0) {
        if (cancel && cancel->load()) {
            curl_easy_cleanup(curl);
            return false;
        }

        char buffer[4096];
        size_t received = 0;
        code = curl_easy_recv(curl, buffer, sizeof(buffer), &received);
        if (code == CURLE_OK) {
            if (received == 0) {
                if (error) {
                    *error = QStringLiteral("Conexão encerrada antes do upgrade.");
                }
                curl_easy_cleanup(curl);
                return false;
            }
            response.append(buffer, static_cast<int>(received));
            headerEnd = response.indexOf("\r\n\r\n");
        } else if (code == CURLE_AGAIN) {
            if (timer.elapsed() > timeoutMs) {
                if (error) {
                    *error = QStringLiteral("Timeout aguardando o upgrade.");
                }
                curl_easy_cleanup(curl);
                return false;
            }
            QThread::msleep(5);
        } else {
            if (error) {
                *error = QString::fromUtf8(curl_easy_strerror(code));
            }
            curl_easy_cleanup(curl);
            return false;
        }
    }

    const QByteArray headers = response.left(headerEnd);
    if (!headers.startsWith("HTTP/1.1 101") && !headers.startsWith("HTTP/1.1 200")) {
        if (error) {
            *error = QStringLiteral("Upgrade recusado: %1")
                         .arg(QString::fromUtf8(headers.left(160)));
        }
        curl_easy_cleanup(curl);
        return false;
    }

    const QByteArray initialBody = response.mid(headerEnd + 4);
    if (!initialBody.isEmpty() && !onData(initialBody)) {
        curl_easy_cleanup(curl);
        return true;
    }

    while (true) {
        if (cancel && cancel->load()) {
            break;
        }

        char buffer[8192];
        size_t received = 0;
        code = curl_easy_recv(curl, buffer, sizeof(buffer), &received);
        if (code == CURLE_OK) {
            if (received == 0) {
                break;
            }
            if (!onData(QByteArray(buffer, static_cast<int>(received)))) {
                break;
            }
        } else if (code == CURLE_AGAIN) {
            if (timer.elapsed() > timeoutMs) {
                if (error) {
                    *error = QStringLiteral("Timeout lendo o stream do exec.");
                }
                break;
            }
            QThread::msleep(5);
        } else {
            if (error) {
                *error = QString::fromUtf8(curl_easy_strerror(code));
            }
            break;
        }
    }

    curl_easy_cleanup(curl);
    return true;
}

} // namespace PodStack
