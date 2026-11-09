// SPDX-License-Identifier: GPL-3.0-or-later

#include "machines/QemuCommand.h"

#include <QDir>

namespace PodStack {

namespace {

constexpr qint64 kMinimumMemoryBytes = 1024LL * 1024LL * 1024LL;
constexpr qint64 kBytesPerMiB = 1024LL * 1024LL;

bool invalid(Error *error, const QString &message, const QString &detail = {})
{
    if (error) {
        *error = Error::make(QStringLiteral("PODSTACK_INVALID_PARAMS"), message, detail);
    }
    return false;
}

bool absolutePath(const QString &path)
{
    return !path.isEmpty() && QDir::isAbsolutePath(path);
}

} // namespace

QString QemuCommand::binary(const QString &arch, Error *error)
{
    if (error) {
        *error = {};
    }

    if (arch == QStringLiteral("amd64")) {
        return QStringLiteral("qemu-system-x86_64");
    }
    if (arch == QStringLiteral("arm64")) {
        return QStringLiteral("qemu-system-aarch64");
    }

    invalid(error, QStringLiteral("Arquitetura inválida para a máquina."),
            QStringLiteral("esperado amd64 ou arm64, recebido '%1'").arg(arch));
    return {};
}

QStringList QemuCommand::arguments(const QemuSpec &spec, Error *error)
{
    if (error) {
        *error = {};
    }

    if (spec.machineId.isEmpty()) {
        invalid(error, QStringLiteral("O identificador da máquina é obrigatório."));
        return {};
    }
    if (spec.name.isEmpty()) {
        invalid(error, QStringLiteral("O nome da máquina é obrigatório."));
        return {};
    }
    if (spec.arch != QStringLiteral("amd64") && spec.arch != QStringLiteral("arm64")) {
        invalid(error, QStringLiteral("Arquitetura inválida para a máquina."),
                QStringLiteral("esperado amd64 ou arm64, recebido '%1'").arg(spec.arch));
        return {};
    }
    if (spec.cpus < 1 || spec.cpus > 256) {
        invalid(error, QStringLiteral("Número de CPUs fora do intervalo permitido (1..256)."),
                QString::number(spec.cpus));
        return {};
    }
    if (spec.memoryBytes < kMinimumMemoryBytes) {
        invalid(error, QStringLiteral("Memória insuficiente para a máquina (mínimo 1 GiB)."),
                QString::number(spec.memoryBytes));
        return {};
    }
    if (!absolutePath(spec.diskPath)) {
        invalid(error, QStringLiteral("O caminho do disco deve ser absoluto."), spec.diskPath);
        return {};
    }
    if (!absolutePath(spec.seedPath)) {
        invalid(error, QStringLiteral("O caminho do seed deve ser absoluto."), spec.seedPath);
        return {};
    }
    if (!absolutePath(spec.qmpSocketPath)) {
        invalid(error, QStringLiteral("O caminho do socket QMP deve ser absoluto."),
                spec.qmpSocketPath);
        return {};
    }
    if (!absolutePath(spec.consoleLogPath)) {
        invalid(error, QStringLiteral("O caminho do console deve ser absoluto."),
                spec.consoleLogPath);
        return {};
    }
    if (!absolutePath(spec.pidFile)) {
        invalid(error, QStringLiteral("O caminho do pidfile deve ser absoluto."), spec.pidFile);
        return {};
    }
    if (spec.sshPort == 0) {
        invalid(error, QStringLiteral("A porta SSH deve ser maior que zero."));
        return {};
    }

    const bool hasOvmfCode = !spec.ovmfCodePath.isEmpty();
    const bool hasOvmfVars = !spec.ovmfVarsPath.isEmpty();
    if (hasOvmfCode != hasOvmfVars) {
        invalid(error, QStringLiteral("O firmware UEFI exige OVMF code e vars juntos."));
        return {};
    }
    if (hasOvmfCode) {
        if (!absolutePath(spec.ovmfCodePath)) {
            invalid(error, QStringLiteral("O caminho do OVMF code deve ser absoluto."),
                    spec.ovmfCodePath);
            return {};
        }
        if (!absolutePath(spec.ovmfVarsPath)) {
            invalid(error, QStringLiteral("O caminho do OVMF vars deve ser absoluto."),
                    spec.ovmfVarsPath);
            return {};
        }
    }

    QStringList args;
    args << QStringLiteral("-M")
         << (spec.arch == QStringLiteral("amd64") ? QStringLiteral("q35")
                                                  : QStringLiteral("virt"));
    args << QStringLiteral("-accel")
         << (spec.kvm ? QStringLiteral("kvm") : QStringLiteral("tcg"));
    args << QStringLiteral("-cpu") << (spec.kvm ? QStringLiteral("host") : QStringLiteral("max"));
    args << QStringLiteral("-smp") << QString::number(spec.cpus);
    args << QStringLiteral("-m") << QString::number(spec.memoryBytes / kBytesPerMiB);
    args << QStringLiteral("-name") << QStringLiteral("podstack:%1").arg(spec.machineId);
    args << QStringLiteral("-uuid") << spec.machineId;

    args << QStringLiteral("-drive")
         << QStringLiteral("file=%1,if=virtio,format=qcow2,cache=writeback,discard=unmap")
                .arg(spec.diskPath);
    args << QStringLiteral("-drive")
         << QStringLiteral("file=%1,format=raw,if=virtio,readonly=on").arg(spec.seedPath);

    QString netdev =
        QStringLiteral("user,id=net0,hostfwd=tcp:127.0.0.1:%1-:22").arg(spec.sshPort);
    if (spec.isolateNetwork) {
        netdev += QStringLiteral(",restrict=on");
    }
    args << QStringLiteral("-netdev") << netdev;
    args << QStringLiteral("-device") << QStringLiteral("virtio-net-pci,netdev=net0");

    args << QStringLiteral("-chardev")
         << QStringLiteral("socket,id=qmp,path=%1,server=on,wait=off").arg(spec.qmpSocketPath);
    args << QStringLiteral("-mon") << QStringLiteral("chardev=qmp,mode=control");
    args << QStringLiteral("-monitor") << QStringLiteral("none");
    args << QStringLiteral("-display") << QStringLiteral("none");
    args << QStringLiteral("-serial") << QStringLiteral("file:%1").arg(spec.consoleLogPath);
    args << QStringLiteral("-daemonize");
    args << QStringLiteral("-pidfile") << spec.pidFile;

    if (hasOvmfCode) {
        args << QStringLiteral("-drive")
             << QStringLiteral("if=pflash,format=raw,readonly=on,file=%1")
                    .arg(spec.ovmfCodePath);
        args << QStringLiteral("-drive")
             << QStringLiteral("if=pflash,format=raw,file=%1").arg(spec.ovmfVarsPath);
    }

    return args;
}

} // namespace PodStack
