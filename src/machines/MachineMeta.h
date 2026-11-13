// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "core/Error.h"

#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>

namespace PodStack {

struct MachinePort {
    QString id;
    int hostPort = 0;
    int guestPort = 0;
    QString protocol;
    bool exposeLan = false;
    QString label;
};

struct MachineMount {
    QString id;
    QString source;
    QString target;
    bool readOnly = false;
    QString tag;
};

/// Snapshot interno do qcow2 (não há tabela SQLite; metadados vêm do qemu-img).
struct MachineSnapshot {
    QString id;
    QString name;
    qint64 sizeBytes = 0; // vm-state-size; 0 em snapshot tirado com a machine parada
    QString date;         // RFC 3339 UTC com milissegundos

    QJsonObject toJson() const;
};

/// Porta serial do host anexada a uma machine.
struct MachineUsbDevice {
    int bus = 0;
    int address = 0;
    QString vendorId;
    QString productId;
    QString description;

    QJsonObject toJson() const;
    static MachineUsbDevice fromJson(const QJsonObject &object);
};

/// Metadados persistidos de uma machine. Timestamps em RFC 3339 UTC com milissegundos.
struct MachineMeta {
    QString id;
    QString name;
    QString distro;
    QString distroVersion;
    QString provisioner = QStringLiteral("linux"); // linux|windows
    QString arch;
    int cpus = 2;
    qint64 memoryBytes = 0;
    qint64 diskBytes = 0;
    QString diskPath;
    QString seedPath;
    QString sshKeyPath;
    int sshPort = 0;
    QString sshUser;
    QString state; // provisioning|starting|running|stopping|stopped|error|deleting|deleted
    bool isolated = false;
    bool isolateNetwork = false;
    bool forwardSshAgent = false;
    QString lastBootAt;
    QString createdAt;
    QString updatedAt;
    QString error;
    QVector<MachinePort> ports;
    QVector<MachineMount> mounts;
    QStringList serialPorts;
    QVector<MachineUsbDevice> usbDevices;
    bool audioEnabled = false;
    QString windowsEdition;      // ex.: Windows 11 Pro (provisioner windows)
    QString installerIso;        // ISO do Windows fornecida pelo usuário (instalação pendente)
    QString virtioWinIso;        // ISO do virtio-win fornecida pelo usuário (instalação pendente)
    bool windowsSsh = false;     // instala/habilita OpenSSH no answer file
    bool installPending = false; // anexa ISOs e -boot once=d somente enquanto true
    QJsonObject toJson() const;
    static MachineMeta fromJson(const QJsonObject &object);
};

/// Leitura/escrita atômica de `meta.json` (fonte de verdade recuperável da machine).
class MachineMetaFile {
public:
    static bool write(const QString &path, const MachineMeta &meta, Error *error);
    static MachineMeta read(const QString &path, Error *error);
};

} // namespace PodStack
