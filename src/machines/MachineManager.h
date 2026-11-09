// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "core/Error.h"
#include "machines/CloudImageCache.h"
#include "machines/DistroRegistry.h"
#include "machines/MachineMeta.h"
#include "machines/MachineStore.h"
#include "machines/QemuRuntime.h"

#include <QString>
#include <QVector>

#include <functional>
#include <memory>

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
};

/// Orquestra provisionamento e ciclo de vida das machines QEMU/KVM.
class MachineManager {
public:
    struct Options {
        QString storePath;      // vazio = podstack.db
        QString machinesRoot;   // vazio = Paths::machinesDir()
        QString imageCacheDir;  // vazio = Paths::cloudImagesDir()
        QString qemuImgBinary;  // vazio = QStandardPaths
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
    bool validate(const CreateMachineRequest &request, Error *error = nullptr) const;
    MachineMeta create(const CreateMachineRequest &request,
                       const std::function<void(const QString &stage, int progress)> &progress,
                       const std::function<bool()> &cancelled, Error *error = nullptr);
    MachineMeta start(const QString &idOrName, Error *error = nullptr);
    MachineMeta stop(const QString &idOrName, bool force, Error *error = nullptr);
    MachineMeta restart(const QString &idOrName, Error *error = nullptr);
    bool remove(const QString &idOrName, bool force, Error *error = nullptr);

private:
    MachineMeta load(const QString &idOrName, Error *error);
    QVector<MachineMeta> existingMachines(Error *error);
    bool nameExists(const QVector<MachineMeta> &existing, const QString &name) const;
    int findFreePort(const QVector<MachineMeta> &existing, Error *error);
    QString machineDirFor(const QString &id) const;
    QemuSpec specFor(const MachineMeta &meta, bool forStart, Error *error);
    bool resolveFirmware(const QString &arch, const QString &machineDir, QString *codePath,
                         QString *varsPath, Error *error);
    bool createDisk(const QString &diskPath, const QString &backingPath, qint64 diskBytes,
                    Error *error);
    bool persist(MachineMeta &meta, Error *error);
    static QString normalizeArch(const QString &arch);
    static QString normalizedUser(const QString &user);
    static QString nowIso();
    static bool kvmAvailable();

    Options m_options;
    std::unique_ptr<QemuRuntime> m_runtime;
    MachineStore m_store;
    CloudImageCache m_cache;
};

} // namespace PodStack
