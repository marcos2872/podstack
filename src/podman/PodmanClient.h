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

struct ImageInfo {
    QString id;
    QStringList repoTags;
    QString digest;
    qint64 size = 0;
    qint64 created = 0;
    int containers = 0;
    bool dangling = false;
    QMap<QString, QString> labels;

    QJsonObject toJson() const;
};

struct ImageHistoryEntry {
    QString id;
    qint64 created = 0;
    QString createdBy;
    QString comment;
    qint64 size = 0;

    QJsonObject toJson() const;
};

struct VolumeInfo {
    QString name;
    QString driver;
    QString mountpoint;
    QString created;
    qint64 size = 0;
    qint64 reclaimable = 0;
    int links = 0;
    QMap<QString, QString> labels;

    QJsonObject toJson() const;
};

struct NetworkInfo {
    QString name;
    QString id;
    QString driver;
    QString interfaceName;
    QString subnet;
    QString gateway;
    QString created;
    bool ipv6 = false;
    bool dnsEnabled = false;
    bool internal = false;
    int containers = 0;
    QMap<QString, QString> labels;

    QJsonObject toJson() const;
};

struct PodInfo {
    QString id;
    QString name;
    QString status;
    QString created;
    QString infraId;
    int containers = 0;
    QMap<QString, QString> labels;

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
    bool uploadFile(const QString &id, const QString &path, const QString &sourcePath,
                    Error *error = nullptr);
    bool buildImage(const QString &contextPath, const QString &dockerfile,
                    const QString &tag, const QMap<QString, QString> &buildArgs,
                    const QString &platform,
                    const std::function<bool(const QByteArray &)> &onLine,
                    std::atomic_bool *cancel, QString *imageId, Error *error = nullptr);

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

    // Imagens
    QVector<ImageInfo> listImages(bool all, Error *error = nullptr);
    QJsonObject inspectImage(const QString &id, Error *error = nullptr);
    QVector<ImageHistoryEntry> imageHistory(const QString &id, Error *error = nullptr);
    bool pullImage(const QString &reference, const QString &platform,
                   const std::function<bool(const QByteArray &)> &onLine,
                   std::atomic_bool *cancel, Error *error = nullptr);
    bool pushImage(const QString &id, const QString &destination,
                   const std::function<bool(const QByteArray &)> &onLine,
                   std::atomic_bool *cancel, Error *error = nullptr);
    bool tagImage(const QString &id, const QString &repo, const QString &tag,
                  Error *error = nullptr);
    bool untagImage(const QString &id, const QString &repo, const QString &tag,
                    Error *error = nullptr);
    bool removeImage(const QString &id, bool force, Error *error = nullptr);
    bool pruneImages(bool all, qint64 *reclaimedBytes, Error *error = nullptr);
    bool exportImage(const QString &id, const QString &destination, Error *error = nullptr);
    bool loadImage(const QString &source, Error *error = nullptr);

    // Volumes
    QVector<VolumeInfo> listVolumes(Error *error = nullptr);
    QJsonObject inspectVolume(const QString &name, Error *error = nullptr);
    QJsonObject createVolume(const QString &name, const QString &driver,
                             const QMap<QString, QString> &labels,
                             const QMap<QString, QString> &options, Error *error = nullptr);
    bool removeVolume(const QString &name, bool force, Error *error = nullptr);
    bool pruneVolumes(qint64 *reclaimedBytes, Error *error = nullptr);
    bool cloneVolume(const QString &source, const QString &destination, Error *error = nullptr);
    bool exportVolume(const QString &name, const QString &destination, Error *error = nullptr);
    bool importVolume(const QString &name, const QString &source, bool force,
                      Error *error = nullptr);
    bool importHostPath(const QString &name, const QString &sourcePath,
                        Error *error = nullptr);

    // Redes
    QVector<NetworkInfo> listNetworks(Error *error = nullptr);
    QJsonObject createNetwork(const QString &name, const QString &driver,
                              const QString &subnet, const QString &gateway, bool ipv6,
                              const QMap<QString, QString> &labels, Error *error = nullptr);
    bool removeNetwork(const QString &name, bool force, Error *error = nullptr);
    bool connectNetwork(const QString &name, const QString &containerId,
                        const QStringList &aliases, Error *error = nullptr);
    bool disconnectNetwork(const QString &name, const QString &containerId,
                           Error *error = nullptr);

    // Pods
    QVector<PodInfo> listPods(Error *error = nullptr);
    QJsonObject createPod(const QString &name, const QMap<QString, QString> &labels,
                          Error *error = nullptr);
    bool podAction(const QString &id, const QString &action, Error *error = nullptr);
    bool removePod(const QString &id, bool force, Error *error = nullptr);

    QJsonObject systemDf(Error *error = nullptr);

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
