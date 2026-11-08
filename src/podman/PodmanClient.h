#pragma once

#include "core/Error.h"
#include "podman/UnixHttpClient.h"

#include <QJsonObject>
#include <QMap>
#include <QString>
#include <QStringList>
#include <QVector>

#include <atomic>
#include <functional>
#include <mutex>

namespace PodStack {

struct ContainerInfo {
    QString id;
    QString name;
    QString image;
    QString state;
    QString status;
    QString created;
    QStringList ports;
    QMap<QString, QString> labels;

    QJsonObject toJson() const;
};

struct ExecResult {
    int exitCode = -1;
    QByteArray output;
};

/// Cliente da API Libpod do Podman sobre o socket Unix.
/// Thread-safe para chamadas concorrentes (cada chamada usa um CURL handle próprio).
class PodmanClient {
public:
    explicit PodmanClient(QString socketPath = {});

    bool ping(Error *error = nullptr);
    QString apiVersion() const;

    QVector<ContainerInfo> listContainers(bool all, Error *error = nullptr);

    bool startContainer(const QString &id, Error *error = nullptr);
    bool stopContainer(const QString &id, int timeoutSeconds = 10, Error *error = nullptr);
    bool restartContainer(const QString &id, int timeoutSeconds = 10, Error *error = nullptr);
    bool removeContainer(const QString &id, bool force, bool volumes, Error *error = nullptr);

    bool execOnce(const QString &id, const QStringList &cmd, ExecResult *result,
                  Error *error = nullptr);

    bool streamLogs(const QString &id, int tail, bool follow,
                    const std::function<bool(const QByteArray &)> &onData,
                    std::atomic_bool *cancel, Error *error = nullptr);

    bool streamEvents(const std::function<bool(const QByteArray &)> &onData,
                      std::atomic_bool *cancel, Error *error = nullptr);

    QString socketPath() const { return m_http.socketPath(); }

private:
    bool ensureVersion(Error *error);
    QString path(const QString &suffix) const;
    static Error errorFromResponse(const HttpResponse &response, const QString &fallback);

    UnixHttpClient m_http;
    QString m_apiVersion;
    mutable std::recursive_mutex m_versionMutex;
};

} // namespace PodStack
