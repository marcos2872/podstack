// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "core/Error.h"
#include "machines/CloudImageCache.h"
#include "machines/DistroRegistry.h"
#include "machines/MachineMeta.h"
#include "machines/MachineStore.h"
#include "machines/QemuRuntime.h"
#include "machines/SshConfig.h"
#include "machines/VirtiofsdProcess.h"

#include <QHash>
#include <QString>
#include <QVector>

#include <functional>
#include <map>
#include <memory>
#include <mutex>

namespace PodStack {

struct CreateMachineRequest {
    QString name;
    QString distro;
    QString version;
    QString arch;
    QString user;
    QString password;
    QString userData;
    int cpus = 2;
    qint64 memoryBytes = 4294967296LL;
    qint64 diskBytes = 21474836480LL;
    bool isolateNetwork = false;
    bool forwardSshAgent = false;
    QVector<MachineMount> mounts;
};

/// Destino de SSH de uma machine pronta para uso pelo CLI/GUI.
struct SshInfo {
    QString host;
    int port = 0;
    QString user;
    QString identityFile;
};

/// Orquestra provisionamento e ciclo de vida das machines QEMU/KVM.
class MachineManager {
public:
    struct Options {
        QString storePath;      // vazio = podstack.db
        QString machinesRoot;   // vazio = Paths::machinesDir()
        QString imageCacheDir;  // vazio = Paths::cloudImagesDir()
        QString qemuImgBinary;  // vazio = QStandardPaths
        QString sshKeygenBinary; // vazio = QStandardPaths
        QString virtiofsdBinary; // vazio = PODSTACK_VIRTIOFSD_BINARY/QStandardPaths
        QString sshConfigPath;   // vazio = PODSTACK_SSH_CONFIG ou ~/.ssh/config
        QString ovmfCodePath;   // vazio = autodetectar
        QString ovmfVarsPath;
        int sshPortStart = 32222;
        std::unique_ptr<QemuRuntime> runtime; // vazio = QemuProcessRuntime
        std::function<DistroImage(const QString &spec, const QString &arch, Error *)>
            resolveDistro;
    };

    MachineManager();
    explicit MachineManager(Options options);
    ~MachineManager();

    MachineManager(const MachineManager &) = delete;
    MachineManager &operator=(const MachineManager &) = delete;

    QVector<MachineMeta> list(Error *error = nullptr);
    MachineMeta get(const QString &idOrName, Error *error = nullptr);
    /// Reconcilia o store com os processos vivos (chamado no start do daemon).
    void reconcile(Error *error = nullptr);
    bool validate(const CreateMachineRequest &request, Error *error = nullptr) const;
    MachineMeta create(const CreateMachineRequest &request,
                       const std::function<void(const QString &stage, int progress)> &progress,
                       const std::function<bool()> &cancelled, Error *error = nullptr);
    MachineMeta start(const QString &idOrName, Error *error = nullptr);
    MachineMeta stop(const QString &idOrName, bool force, Error *error = nullptr);
    MachineMeta restart(const QString &idOrName, Error *error = nullptr);
    bool remove(const QString &idOrName, bool force, Error *error = nullptr);
    bool sshInfo(const QString &idOrName, SshInfo *info, Error *error = nullptr);
    /// Empacota a machine em `.tar.zst` (pausa e reinicia se estava rodando).
    MachineMeta exportMachine(const QString &idOrName, const QString &archivePath, bool force,
                              const std::function<void(const QString &stage, int progress)>
                                  &progress,
                              const std::function<bool()> &cancelled,
                              Error *error = nullptr);
    /// Restaura uma machine de um `.tar.zst` com novo ID, porta SSH e bloco SSH.
    MachineMeta importMachine(const QString &archivePath, const QString &name, bool force,
                              const std::function<void(const QString &stage, int progress)>
                                  &progress,
                              const std::function<bool()> &cancelled,
                              Error *error = nullptr);

private:
    MachineMeta load(const QString &idOrName, Error *error);
    QVector<MachineMeta> existingMachines(Error *error);
    bool nameExists(const QVector<MachineMeta> &existing, const QString &name) const;
    int findFreePort(const QVector<MachineMeta> &existing, Error *error);
    QString machineDirFor(const QString &id) const;
    QString virtiofsSocketPath(const QString &machineId, const QString &tag) const;
    bool normalizeMounts(const QVector<MachineMount> &mounts,
                         QVector<MachineMount> *normalized, Error *error) const;
    bool startVirtiofs(const MachineMeta &meta, Error *error);
    bool stopVirtiofs(const QString &machineId, Error *error = nullptr);
    QemuSpec specFor(const MachineMeta &meta, bool forStart, Error *error);
    bool resolveFirmware(const QString &arch, const QString &machineDir, QString *codePath,
                         QString *varsPath, Error *error);
    bool createDisk(const QString &diskPath, const QString &backingPath, qint64 diskBytes,
                    Error *error);
    bool flattenDisk(const QString &diskPath, const QString &targetPath,
                     const std::function<bool()> &cancelled, Error *error);
    QString qemuImgBinary(Error *error) const;
    bool ensureSshKey(const QString &directory, const QString &name, QString *privatePath,
                      QString *publicKey, Error *error);
    bool persist(MachineMeta &meta, Error *error);
    std::shared_ptr<std::recursive_mutex> machineLock(const QString &id);
    bool tryLockMachine(const QString &id,
                        std::shared_ptr<std::recursive_mutex> *holder,
                        std::unique_lock<std::recursive_mutex> *lock);
    static QString normalizeArch(const QString &arch);
    static QString normalizedUser(const QString &user);
    static QString nowIso();
    static bool kvmAvailable();

    Options m_options;
    std::unique_ptr<QemuRuntime> m_runtime;
    MachineStore m_store;
    CloudImageCache m_cache;
    SshConfig m_sshConfig;
    std::recursive_mutex m_virtiofsMutex;
    std::map<QString, std::map<QString, std::unique_ptr<VirtiofsdProcess>>> m_virtiofs;
    std::mutex m_machineLocksMutex;
    QHash<QString, std::shared_ptr<std::recursive_mutex>> m_machineLocks;
    std::mutex m_archiveMutex;
};

} // namespace PodStack
