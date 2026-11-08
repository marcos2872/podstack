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
    QString health;
    QStringList ports;
    QMap<QString, QString> labels;

    QJsonObject toJson() const;
};

struct ExecResult {
    int exitCode = -1;
    QByteArray output;
};

struct PortMapping {
    int hostPort = 0;
    int containerPort = 0;
    QString protocol = QStringLiteral("tcp");
    QString hostIp = QStringLiteral("0.0.0.0");
};

struct MountMapping {
    QString source;
    QString destination;
    bool readOnly = false;
};

struct ContainerCreateSpec {
    QString image;
    QString name;
    QString command;
    QMap<QString, QString> env;
    QVector<PortMapping> ports;
    QVector<MountMapping> mounts;
    QMap<QString, QString> labels;
    QString workDir;
    QString user;
    bool remove = false;
    bool init = false;
    bool privileged = false;

    QJsonObject toJson() const;
};

struct FileEntry {
    QString name;
    qint64 size = 0;
    bool directory = false;
    QString mode;
    QString modified;

    QJsonObject toJson() const;
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
    bool pauseContainer(const QString &id, Error *error = nullptr);
    bool unpauseContainer(const QString &id, Error *error = nullptr);
    bool killContainer(const QString &id, const QString &signal, Error *error = nullptr);
    bool renameContainer(const QString &id, const QString &name, Error *error = nullptr);
    QJsonObject inspectContainer(const QString &id, Error *error = nullptr);
    QString createContainer(const ContainerCreateSpec &spec, Error *error = nullptr);
    QVector<FileEntry> listFiles(const QString &id, const QString &path, Error *error = nullptr);
    bool downloadFile(const QString &id, const QString &path, const QString &destination,
                      Error *error = nullptr);

    bool execOnce(const QString &id, const QStringList &cmd, ExecResult *result,
                  Error *error = nullptr);

    bool streamLogs(const QString &id, int tail, bool follow,
                    const std::function<bool(const QByteArray &)> &onData,
                    std::atomic_bool *cancel, Error *error = nullptr);

    bool streamStats(const QString &id, int intervalSeconds,
                     const std::function<bool(const QByteArray &)> &onLine,
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
