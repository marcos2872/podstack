#pragma once

#include <QByteArray>
#include <QHash>
#include <QString>

#include <atomic>
#include <functional>

namespace PodStack {

struct HttpResponse {
    long status = 0;
    QByteArray body;
    QHash<QString, QString> headers;
    QString error;
};

/// HTTP/1.1 sobre Unix domain socket, usando libcurl.
class UnixHttpClient {
public:
    explicit UnixHttpClient(QString socketPath);

    HttpResponse request(const QByteArray &method, const QString &path,
                         const QByteArray &body = {}, const QString &contentType = {},
                         int timeoutMs = 30000);

    /// Stream contínuo (logs/events). `onData` retorna false para encerrar.
    bool stream(const QString &path, const std::function<bool(const QByteArray &)> &onData,
                std::atomic_bool *cancel, QString *error, int timeoutMs = 0);

    /// Exec/attach com HTTP hijack (`Upgrade: tcp`), retornando o stream cru.
    bool hijack(const QString &path, const QByteArray &body,
                const std::function<bool(const QByteArray &)> &onData, std::atomic_bool *cancel,
                QString *error, int timeoutMs = 30000);

    QString socketPath() const { return m_socketPath; }

private:
    QString m_socketPath;
};

} // namespace PodStack
