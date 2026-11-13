// SPDX-License-Identifier: GPL-3.0-or-later

#include "machines/QemuCommand.h"

#include <QDir>
#include <QRegularExpression>
#include <QSet>

namespace PodStack {

namespace {

constexpr qint64 kMinimumMemoryBytes = 1024LL * 1024LL * 1024LL;
constexpr qint64 kBytesPerMiB = 1024LL * 1024LL;
constexpr int kMaximumMounts = 8;

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
    if (!spec.seedPath.isEmpty() && !absolutePath(spec.seedPath)) {
        invalid(error, QStringLiteral("O caminho do seed deve ser absoluto."), spec.seedPath);
        return {};
    }
    const QStringList isoPaths = {spec.installerIsoPath, spec.driverIsoPath, spec.unattendIsoPath};
    for (const QString &isoPath : isoPaths) {
        if (isoPath.isEmpty()) {
            continue;
        }
        if (!absolutePath(isoPath)) {
            invalid(error, QStringLiteral("O caminho da ISO deve ser absoluto."), isoPath);
            return {};
        }
    }
    if (!absolutePath(spec.qmpSocketPath)) {
        invalid(error, QStringLiteral("O caminho do socket QMP deve ser absoluto."),
                spec.qmpSocketPath);
        return {};
    }
    if (!spec.guestAgentSocketPath.isEmpty() && !absolutePath(spec.guestAgentSocketPath)) {
        invalid(error, QStringLiteral("O caminho do socket do agente deve ser absoluto."),
                spec.guestAgentSocketPath);
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
    if (spec.ports.isEmpty()) {
        invalid(error, QStringLiteral("A machine precisa de ao menos uma porta encaminhada."));
        return {};
    }

    QSet<QString> forwardedPorts;
    for (const QemuPort &port : spec.ports) {
        const QString protocol = port.protocol.trimmed().toLower();
        if (protocol != QStringLiteral("tcp") && protocol != QStringLiteral("udp")) {
            invalid(error, QStringLiteral("Protocolo de porta inválido."),
                    QStringLiteral("%1 (use tcp ou udp)").arg(port.protocol));
            return {};
        }
        if (port.hostPort < 1 || port.hostPort > 65535) {
            invalid(error, QStringLiteral("Porta do host fora do intervalo 1..65535."),
                    QString::number(port.hostPort));
            return {};
        }
        if (port.guestPort < 1 || port.guestPort > 65535) {
            invalid(error, QStringLiteral("Porta do guest fora do intervalo 1..65535."),
                    QString::number(port.guestPort));
            return {};
        }
        if (port.hostAddress != QStringLiteral("127.0.0.1")
            && port.hostAddress != QStringLiteral("0.0.0.0")) {
            invalid(error, QStringLiteral("Endereço de bind inválido para a porta."),
                    QStringLiteral("%1 (use 127.0.0.1 ou 0.0.0.0)").arg(port.hostAddress));
            return {};
        }
        const QString key = protocol + QLatin1Char(':') + QString::number(port.hostPort);
        if (forwardedPorts.contains(key)) {
            invalid(error, QStringLiteral("Porta do host duplicada no protocolo."),
                    QStringLiteral("%1/%2").arg(port.hostPort).arg(protocol));
            return {};
        }
        forwardedPorts.insert(key);
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
    if (spec.secureBoot) {
        if (spec.arch != QStringLiteral("amd64")) {
            invalid(error, QStringLiteral("Secure Boot é suportado apenas em machines amd64."),
                    spec.arch);
            return {};
        }
        if (!hasOvmfCode) {
            invalid(error, QStringLiteral("Secure Boot exige firmware OVMF."));
            return {};
        }
    }
    if (!spec.spiceSocketPath.isEmpty() && !absolutePath(spec.spiceSocketPath)) {
        invalid(error, QStringLiteral("O socket do SPICE deve ser absoluto."),
                spec.spiceSocketPath);
        return {};
    }
    if (!spec.tpmSocketPath.isEmpty() && !absolutePath(spec.tpmSocketPath)) {
        invalid(error, QStringLiteral("O socket do TPM deve ser absoluto."), spec.tpmSocketPath);
        return {};
    }

    if (spec.mounts.size() > kMaximumMounts) {
        invalid(error, QStringLiteral("Número de mounts acima do limite (8)."),
                QString::number(spec.mounts.size()));
        return {};
    }
    static const QRegularExpression mountTagPattern(QStringLiteral("^[a-z0-9-]{1,32}$"));
    QSet<QString> mountTags;
    for (const QemuMount &mount : spec.mounts) {
        if (!mountTagPattern.match(mount.tag).hasMatch()) {
            invalid(error, QStringLiteral("Tag de mount inválida."), mount.tag);
            return {};
        }
        if (mountTags.contains(mount.tag)) {
            invalid(error, QStringLiteral("Tag de mount duplicada."), mount.tag);
            return {};
        }
        mountTags.insert(mount.tag);
        if (!absolutePath(mount.socketPath)) {
            invalid(error, QStringLiteral("O socket do mount deve ser absoluto."),
                    mount.socketPath);
            return {};
        }
    }

    QStringList args;
    QString machine =
        spec.arch == QStringLiteral("amd64") ? QStringLiteral("q35") : QStringLiteral("virt");
    if (spec.secureBoot) {
        machine += QStringLiteral(",smm=on");
    }
    args << QStringLiteral("-M") << machine;
    if (spec.secureBoot) {
        args << QStringLiteral("-global")
             << QStringLiteral("driver=cfi.pflash01,property=secure,value=on");
    }
    args << QStringLiteral("-accel")
         << (spec.kvm ? QStringLiteral("kvm") : QStringLiteral("tcg"));
    QString cpu = spec.kvm ? QStringLiteral("host") : QStringLiteral("max");
    if (spec.kvm && spec.secureBoot) {
        cpu += QStringLiteral(",hv_relaxed,hv_vpindex,hv_time,hv_synic,hv_stimer,hv_reset,"
                              "hv_frequencies");
    }
    args << QStringLiteral("-cpu") << cpu;
    args << QStringLiteral("-smp") << QString::number(spec.cpus);
    args << QStringLiteral("-m") << QString::number(spec.memoryBytes / kBytesPerMiB);
    args << QStringLiteral("-name") << QStringLiteral("podstack:%1").arg(spec.machineId);
    args << QStringLiteral("-uuid") << spec.machineId;

    args << QStringLiteral("-drive")
         << QStringLiteral("file=%1,if=virtio,format=qcow2,cache=writeback,discard=unmap")
                .arg(spec.diskPath);
    if (!spec.seedPath.isEmpty()) {
        args << QStringLiteral("-drive")
             << QStringLiteral("file=%1,format=raw,if=virtio,readonly=on").arg(spec.seedPath);
    }
    if (!spec.installerIsoPath.isEmpty()) {
        // OVMF não inicializa o El Torito do ISO Windows pelo CD IDE/AHCI;
        // o mesmo ISO boota como CD USB (usb-storage) com bootindex.
        args << QStringLiteral("-device") << QStringLiteral("qemu-xhci,id=usbmedia");
        args << QStringLiteral("-drive")
             << QStringLiteral("if=none,id=installer,file=%1,media=cdrom,readonly=on")
                    .arg(spec.installerIsoPath);
        args << QStringLiteral("-device")
             << QStringLiteral("usb-storage,drive=installer,bus=usbmedia.0,bootindex=0");
    }
    for (const QString &isoPath : {spec.driverIsoPath, spec.unattendIsoPath}) {
        if (isoPath.isEmpty()) {
            continue;
        }
        args << QStringLiteral("-drive")
             << QStringLiteral("file=%1,if=ide,media=cdrom,readonly=on").arg(isoPath);
    }

    QString netdev = QStringLiteral("user,id=net0");
    for (const QemuPort &port : spec.ports) {
        netdev += QStringLiteral(",hostfwd=%1:%2:%3-:%4")
                      .arg(port.protocol.trimmed().toLower(), port.hostAddress,
                           QString::number(port.hostPort), QString::number(port.guestPort));
    }
    if (spec.isolateNetwork) {
        netdev += QStringLiteral(",restrict=on");
    }
    args << QStringLiteral("-netdev") << netdev;
    args << QStringLiteral("-device") << QStringLiteral("virtio-net-pci,netdev=net0");

    int mountIndex = 0;
    for (const QemuMount &mount : spec.mounts) {
        const QString id = QStringLiteral("vfs%1").arg(mountIndex++);
        args << QStringLiteral("-chardev")
             << QStringLiteral("socket,id=%1,path=%2").arg(id, mount.socketPath);
        args << QStringLiteral("-device")
             << QStringLiteral("vhost-user-fs-pci,chardev=%1,tag=%2").arg(id, mount.tag);
    }

    args << QStringLiteral("-chardev")
         << QStringLiteral("socket,id=qmp,path=%1,server=on,wait=off").arg(spec.qmpSocketPath);
    args << QStringLiteral("-mon") << QStringLiteral("chardev=qmp,mode=control");
    args << QStringLiteral("-monitor") << QStringLiteral("none");
    if (!spec.guestAgentSocketPath.isEmpty()) {
        args << QStringLiteral("-device") << QStringLiteral("virtio-serial-pci,id=virtserial0");
        args << QStringLiteral("-chardev")
             << QStringLiteral("socket,id=qga,path=%1,server=on,wait=off")
                    .arg(spec.guestAgentSocketPath);
        args << QStringLiteral("-device")
             << QStringLiteral("virtserialport,id=qga0,chardev=qga,"
                               "name=org.qemu.guest_agent.0");
    }
    args << QStringLiteral("-display") << QStringLiteral("none");
    if (!spec.spiceSocketPath.isEmpty()) {
        args << QStringLiteral("-spice")
             << QStringLiteral("unix=on,addr=%1,disable-ticketing=on").arg(spec.spiceSocketPath);
        args << QStringLiteral("-device") << QStringLiteral("virtio-vga");
        args << QStringLiteral("-device") << QStringLiteral("qemu-xhci,id=usbconsole");
        args << QStringLiteral("-device") << QStringLiteral("usb-tablet,bus=usbconsole.0");
    }
    args << QStringLiteral("-serial") << QStringLiteral("file:%1").arg(spec.consoleLogPath);
    for (const QString &serialPort : spec.serialPorts) {
        args << QStringLiteral("-serial") << serialPort;
    }
    if (!spec.usbDevices.isEmpty()) {
        args << QStringLiteral("-device") << QStringLiteral("qemu-xhci,id=usbhost");
        for (const QemuUsbDevice &device : spec.usbDevices) {
            args << QStringLiteral("-device")
                 << QStringLiteral("usb-host,hostbus=%1,hostaddr=%2")
                        .arg(QString::number(device.bus), QString::number(device.address));
        }
    }
    if (spec.audioEnabled) {
        args << QStringLiteral("-audiodev") << QStringLiteral("pipewire,id=audio0");
        args << QStringLiteral("-device") << QStringLiteral("intel-hda");
        args << QStringLiteral("-device") << QStringLiteral("hda-duplex,audiodev=audio0");
    }
    if (!spec.tpmSocketPath.isEmpty()) {
        args << QStringLiteral("-chardev")
             << QStringLiteral("socket,id=chrtpm,path=%1").arg(spec.tpmSocketPath);
        args << QStringLiteral("-tpmdev") << QStringLiteral("emulator,id=tpm0,chardev=chrtpm");
        args << QStringLiteral("-device") << QStringLiteral("tpm-tis,tpmdev=tpm0");
    }
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
