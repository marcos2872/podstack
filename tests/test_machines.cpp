// SPDX-License-Identifier: GPL-3.0-or-later

#include "MockHttpServer.h"
#include "MockQmpServer.h"

#include "core/Config.h"
#include "core/Ids.h"
#include "core/Paths.h"
#include "daemon/BuildStore.h"
#include "k8s/K8sManager.h"
#include "machines/CloudImageCache.h"
#include "machines/CloudInit.h"
#include "machines/DistroRegistry.h"
#include "machines/HttpDownloader.h"
#include "machines/MachineArchive.h"
#include "machines/MachineFiles.h"
#include "machines/MachineManager.h"
#include "machines/MachineMeta.h"
#include "machines/MachineStats.h"
#include "machines/MachineStore.h"
#include "machines/QemuCommand.h"
#include "machines/QemuRuntime.h"
#include "machines/QmpClient.h"
#include "machines/SerialPorts.h"
#include "machines/SshConfig.h"
#include "machines/UsbDevices.h"
#include "machines/VirtiofsdProcess.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalServer>
#include <QLocalSocket>
#include <QLockFile>
#include <QProcess>
#include <QRegularExpression>
#include <QScopeGuard>
#include <QSet>
#include <QStandardPaths>
#include <QTcpServer>
#include <QTemporaryDir>
#include <QTest>
#include <QTimeZone>

#include <archive.h>
#include <archive_entry.h>
#include <sqlite3.h>

#include <csignal>
#include <thread>
#include <unistd.h>

using PodStack::BuildRecord;
using PodStack::BuildStore;
using PodStack::CloudImageCache;
using PodStack::CloudInit;
using PodStack::CloudInitOptions;
using PodStack::CreateMachineRequest;
using PodStack::DistroImage;
using PodStack::DistroRegistry;
using PodStack::HttpDownloader;
using PodStack::K8sCluster;
using PodStack::K8sManager;
using PodStack::K8sStore;
using PodStack::MachineArchive;
using PodStack::MachineArchiveManifest;
using PodStack::MachineFiles;
using PodStack::MachineManager;
using PodStack::MachineMeta;
using PodStack::MachineMetaFile;
using PodStack::MachineMount;
using PodStack::MachinePort;
using PodStack::MachineSnapshot;
using PodStack::MachineStatsCollector;
using PodStack::MachineStore;
using PodStack::MachineUsbDevice;
using PodStack::QemuCommand;
using PodStack::QemuMount;
using PodStack::QemuPort;
using PodStack::QemuProcessRuntime;
using PodStack::QemuRuntime;
using PodStack::QemuSpec;
using PodStack::QemuUsbDevice;
using PodStack::QmpClient;
using PodStack::SshConfig;
using PodStack::SshInfo;
using PodStack::UpdateMachineRequest;
using PodStack::VirtiofsdProcess;

namespace {

QString hexHash(const QByteArray &data, QCryptographicHash::Algorithm algorithm)
{
    return QString::fromLatin1(QCryptographicHash::hash(data, algorithm).toHex());
}

CloudInitOptions seedOptions(const QString &machineId, const QString &hostname)
{
    CloudInitOptions options;
    options.machineId = machineId;
    options.hostname = hostname;
    options.sshAuthorizedKeys = {
        QStringLiteral("ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAIPODSTACK podstack")};
    return options;
}

QString normalizedIsoName(const char *pathname)
{
    QString name = QString::fromUtf8(pathname);
    while (name.startsWith(QStringLiteral("./"))) {
        name.remove(0, 2);
    }
    if (name.endsWith(QStringLiteral(";1"))) {
        name.chop(2);
    }
    return name;
}

bool readIsoEntry(const QString &isoPath, const QString &wanted, QByteArray *data)
{
    archive *reader = archive_read_new();
    if (!reader) {
        return false;
    }
    archive_read_support_format_iso9660(reader);
    archive_read_support_filter_all(reader);

    const QByteArray nativePath = QFile::encodeName(isoPath);
    if (archive_read_open_filename(reader, nativePath.constData(), 10240) != ARCHIVE_OK) {
        archive_read_free(reader);
        return false;
    }

    bool found = false;
    archive_entry *entry = nullptr;
    while (archive_read_next_header(reader, &entry) == ARCHIVE_OK) {
        if (normalizedIsoName(archive_entry_pathname(entry)) != wanted) {
            archive_read_data_skip(reader);
            continue;
        }

        QByteArray content;
        char buffer[4096];
        la_ssize_t count = 0;
        while ((count = archive_read_data(reader, buffer, sizeof(buffer))) > 0) {
            content.append(buffer, static_cast<int>(count));
        }
        if (count < 0) {
            break;
        }
        if (data) {
            *data = content;
        }
        found = true;
        break;
    }

    archive_read_free(reader);
    return found;
}

QStringList isoEntryNames(const QString &isoPath)
{
    QStringList names;
    archive *reader = archive_read_new();
    if (!reader) {
        return names;
    }
    archive_read_support_format_iso9660(reader);
    archive_read_support_filter_all(reader);

    const QByteArray nativePath = QFile::encodeName(isoPath);
    if (archive_read_open_filename(reader, nativePath.constData(), 10240) == ARCHIVE_OK) {
        archive_entry *entry = nullptr;
        while (archive_read_next_header(reader, &entry) == ARCHIVE_OK) {
            if (archive_entry_filetype(entry) == AE_IFREG) {
                names.append(normalizedIsoName(archive_entry_pathname(entry)));
            }
            archive_read_data_skip(reader);
        }
    }

    archive_read_free(reader);
    names.sort();
    return names;
}

QByteArray volumeIdentifier(const QString &isoPath)
{
    QFile file(isoPath);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    if (!file.seek(0x8000 + 40)) {
        return {};
    }
    return file.read(32);
}

bool ownerOnly(const QFileDevice::Permissions &permissions, bool directory)
{
    const QFileDevice::Permissions forbidden =
        QFileDevice::ReadGroup | QFileDevice::WriteGroup | QFileDevice::ExeGroup
        | QFileDevice::ReadOther | QFileDevice::WriteOther | QFileDevice::ExeOther;
    if (permissions.testFlag(QFileDevice::ReadOwner)
        && permissions.testFlag(QFileDevice::WriteOwner)
        && (permissions.testFlag(QFileDevice::ExeOwner) || !directory)) {
        return (permissions & forbidden) == 0;
    }
    return false;
}

QemuSpec qemuSpec(const QString &root)
{
    QemuSpec spec;
    spec.machineId = QStringLiteral("0190f0f0-0000-7000-8000-000000000001");
    spec.name = QStringLiteral("podstack-test");
    spec.arch = QStringLiteral("amd64");
    spec.cpus = 2;
    spec.memoryBytes = 4294967296LL;
    spec.diskPath = root + QStringLiteral("/qemu/disk.qcow2");
    spec.seedPath = root + QStringLiteral("/qemu/seed.iso");
    spec.qmpSocketPath = root + QStringLiteral("/qemu/qmp.sock");
    spec.consoleLogPath = root + QStringLiteral("/qemu/console.log");
    spec.pidFile = root + QStringLiteral("/qemu/qemu.pid");
    spec.sshPort = 32222;
    spec.ports = {QemuPort{QStringLiteral("tcp"), 32222, 22, QStringLiteral("127.0.0.1")}};
    spec.kvm = true;
    return spec;
}

QString valueAfter(const QStringList &args, const QString &flag)
{
    const int index = args.indexOf(flag);
    if (index < 0 || index + 1 >= args.size()) {
        return {};
    }
    return args.at(index + 1);
}

MachineMeta testMachine(const QString &id, const QString &name)
{
    MachineMeta meta;
    meta.id = id;
    meta.name = name;
    meta.distro = QStringLiteral("ubuntu");
    meta.distroVersion = QStringLiteral("24.04");
    meta.arch = QStringLiteral("amd64");
    meta.cpus = 2;
    meta.memoryBytes = 4294967296LL;
    meta.diskBytes = 21474836480LL;
    meta.diskPath = QStringLiteral("/tmp/podstack-test/disk.qcow2");
    meta.seedPath = QStringLiteral("/tmp/podstack-test/seed.iso");
    meta.sshKeyPath = QStringLiteral("/tmp/podstack-test/id_ed25519");
    meta.sshPort = 32222;
    meta.sshUser = QStringLiteral("marcos");
    meta.state = QStringLiteral("stopped");
    meta.createdAt = QStringLiteral("2026-11-08T12:00:00.000Z");
    meta.updatedAt = QStringLiteral("2000-01-01T00:00:00.000Z");
    return meta;
}

int machineRowCount(const QString &dbPath, const QString &table, const QString &machineId)
{
    sqlite3 *db = nullptr;
    if (sqlite3_open_v2(dbPath.toUtf8().constData(), &db, SQLITE_OPEN_READONLY, nullptr)
        != SQLITE_OK) {
        sqlite3_close(db);
        return -1;
    }

    const QByteArray sql =
        QStringLiteral("SELECT COUNT(*) FROM %1 WHERE machine_id = ?").arg(table).toUtf8();
    sqlite3_stmt *statement = nullptr;
    if (sqlite3_prepare_v2(db, sql.constData(), -1, &statement, nullptr) != SQLITE_OK) {
        sqlite3_close(db);
        return -1;
    }

    const QByteArray id = machineId.toUtf8();
    sqlite3_bind_text(statement, 1, id.constData(), id.size(), SQLITE_TRANSIENT);
    int count = -1;
    if (sqlite3_step(statement) == SQLITE_ROW) {
        count = sqlite3_column_int(statement, 0);
    }
    sqlite3_finalize(statement);
    sqlite3_close(db);
    return count;
}

bool writeTestFile(const QString &path, const QByteArray &content,
                   QFileDevice::Permissions permissions
                   = QFileDevice::ReadOwner | QFileDevice::WriteOwner)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    if (file.write(content) != content.size()) {
        return false;
    }
    file.close();
    return QFile::setPermissions(path, permissions);
}

bool createFirmwareFiles(const QString &root, QString *codePath, QString *varsPath)
{
    const QString directory = root + QStringLiteral("/firmware");
    *codePath = directory + QStringLiteral("/OVMF_CODE.fd");
    *varsPath = directory + QStringLiteral("/OVMF_VARS.fd");
    return writeTestFile(*codePath, QByteArrayLiteral("fake-ovmf-code"))
           && writeTestFile(*varsPath, QByteArrayLiteral("fake-ovmf-vars"));
}

QString createQemuImgScript(const QString &root)
{
    const QString path = root + QStringLiteral("/qemu-img-fake.sh");
    const QByteArray script = QByteArrayLiteral(
        "#!/bin/sh\n"
        "log=\"${PODSTACK_QEMU_IMG_LOG:-}\"\n"
        "if [ -n \"$log\" ]; then\n"
        "  if [ \"${PODSTACK_QEMU_IMG_LOG_APPEND:-}\" = '1' ]; then :; else : > \"$log\"; fi\n"
        "  for arg in \"$@\"; do printf '%s\\n' \"$arg\" >> \"$log\"; done\n"
        "fi\n"
        "if [ \"$1\" = 'info' ]; then\n"
        "  if [ -n \"${PODSTACK_QEMU_IMG_INFO_JSON:-}\" ]; then\n"
        "    printf '%s' \"$PODSTACK_QEMU_IMG_INFO_JSON\"\n"
        "  else\n"
        "    printf '%s' '{\"snapshots\":[]}'\n"
        "  fi\n"
        "  exit 0\n"
        "fi\n"
        "if [ \"$1\" = 'snapshot' ]; then\n"
        "  exit 0\n"
        "fi\n"
        "disk=''\n"
        "if [ \"$1\" = 'convert' ]; then eval \"disk=\\${$#}\"; else eval \"disk=\\${$(( $# - 1 ))}\"; fi\n"
        "if [ \"$1\" = 'convert' ] && [ -n \"${PODSTACK_QEMU_IMG_BLOCK_RELEASE:-}\" ]; then\n"
        "  : > \"${PODSTACK_QEMU_IMG_STARTED:-/dev/null}\"\n"
        "  while [ ! -f \"$PODSTACK_QEMU_IMG_BLOCK_RELEASE\" ]; do sleep 0.02; done\n"
        "fi\n"
        "printf 'fake-qcow2' > \"$disk\"\n");
    if (!writeTestFile(path, script,
                       QFileDevice::ReadOwner | QFileDevice::WriteOwner
                           | QFileDevice::ExeOwner)) {
        return {};
    }
    return path;
}

QString createVirtiofsdScript(const QString &root)
{
    const QString path = root + QStringLiteral("/virtiofsd-fake.sh");
    const QByteArray script = QByteArrayLiteral(
        "#!/bin/sh\n"
        "sock=''\n"
        "tag=''\n"
        "prev=''\n"
        "for arg in \"$@\"; do\n"
        "  if [ -n \"${PODSTACK_VIRTIOFSD_LOG:-}\" ]; then\n"
        "    printf '%s\\n' \"$arg\" >> \"$PODSTACK_VIRTIOFSD_LOG\"\n"
        "  fi\n"
        "  if [ \"$prev\" = '--socket-path' ]; then sock=\"$arg\"; fi\n"
        "  if [ \"$prev\" = '--tag' ]; then tag=\"$arg\"; fi\n"
        "  prev=\"$arg\"\n"
        "done\n"
        "if [ -z \"$sock\" ]; then echo 'socket-path ausente' >&2; exit 2; fi\n"
        "if [ -n \"${PODSTACK_VIRTIOFSD_FAIL_TAG:-}\" ] &&\n"
        "   [ \"$tag\" = \"$PODSTACK_VIRTIOFSD_FAIL_TAG\" ]; then\n"
        "  exit 1\n"
        "fi\n"
        "exec python3 -c 'import os,socket,sys,time; s=socket.socket(socket.AF_UNIX);"
        " s.bind(sys.argv[1]); s.listen(1); open(sys.argv[1] + \".pid\", \"w\")"
        ".write(str(os.getpid())); time.sleep(120)' \"$sock\"\n");
    if (!writeTestFile(path, script,
                       QFileDevice::ReadOwner | QFileDevice::WriteOwner
                           | QFileDevice::ExeOwner)) {
        return {};
    }
    return path;
}

qint64 readPidFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return 0;
    }
    bool ok = false;
    const qint64 pid = QString::fromUtf8(file.readAll()).trimmed().toLongLong(&ok);
    return ok ? pid : 0;
}

bool processAlive(qint64 pid)
{
    if (pid <= 0) {
        return false;
    }
    if (::kill(static_cast<pid_t>(pid), 0) != 0) {
        return false;
    }
    // No CI o PID 1 do container pode não recolher zumbis; um processo em
    // estado Z já terminou e não deve segurar o teste.
    QFile stat(QStringLiteral("/proc/%1/stat").arg(pid));
    if (!stat.open(QIODevice::ReadOnly)) {
        return true;
    }
    const QByteArray data = stat.readAll();
    const qsizetype close = data.lastIndexOf(')');
    if (close < 0 || close + 2 >= data.size()) {
        return true;
    }
    return data.at(close + 2) != 'Z';
}

bool waitForProcessGone(qint64 pid, int timeoutMs = 3000)
{
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeoutMs) {
        if (!processAlive(pid)) {
            return true;
        }
        QTest::qWait(20);
    }
    return !processAlive(pid);
}

QString createSshKeygenScript(const QString &root)
{    const QString path = root + QStringLiteral("/ssh-keygen-fake.sh");
    const QByteArray script = QByteArrayLiteral(
        "#!/bin/sh\n"
        "key=\"\"\n"
        "while [ $# -gt 0 ]; do\n"
        "  case \"$1\" in\n"
        "    -f) shift; key=\"$1\";;\n"
        "  esac\n"
        "  shift\n"
        "done\n"
        "if [ -z \"$key\" ]; then echo 'missing -f' >&2; exit 1; fi\n"
        "printf 'FAKE-PRIVATE-KEY' > \"$key\"\n"
        "printf 'ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAIFAKEPODSTACKKEY podstack:fake'"
        " > \"$key.pub\"\n"
        "chmod 600 \"$key\"\n"
        "chmod 644 \"$key.pub\"\n"
        "exit 0\n");
    if (!writeTestFile(path, script,
                       QFileDevice::ReadOwner | QFileDevice::WriteOwner
                           | QFileDevice::ExeOwner)) {
        return {};
    }
    return path;
}

bool configureSshOptions(MachineManager::Options *options, const QString &root)
{
    const QString keygen = createSshKeygenScript(root);
    if (keygen.isEmpty()) {
        return false;
    }
    options->sshKeygenBinary = keygen;
    options->sshConfigPath = root + QStringLiteral("/ssh/config");
    return true;
}

int freePortHint()
{
    QTcpServer probe;
    if (!probe.listen(QHostAddress::LocalHost, 0)) {
        return 32222;
    }
    const int port = probe.serverPort();
    probe.close();
    return port > 65515 ? 32222 : port;
}

class FakeQemuRuntime : public QemuRuntime {
public:
    bool start(const QemuSpec &spec, PodStack::Error *error) override
    {
        lastSpec = spec;
        ++startCount;
        if (failStart || failStartSilently) {
            running = false;
            if (error) {
                *error = failStart
                             ? PodStack::Error::make(QStringLiteral("PODSTACK_IO_ERROR"),
                                                     QStringLiteral("falha simulada de start"))
                             : PodStack::Error{};
            }
            return false;
        }
        running = true;
        return true;
    }

    bool stop(const QemuSpec &spec, bool force, PodStack::Error *error) override
    {
        Q_UNUSED(spec);
        ++stopCount;
        lastForce = force;
        if (failStop) {
            if (error) {
                *error = PodStack::Error::make(QStringLiteral("PODSTACK_IO_ERROR"),
                                               QStringLiteral("falha simulada de stop"));
            }
            return false;
        }
        running = false;
        return true;
    }

    bool isRunning(const QemuSpec &spec, PodStack::Error *error) override
    {
        Q_UNUSED(spec);
        if (error) {
            *error = {};
        }
        return running;
    }

    QString status(const QemuSpec &spec, PodStack::Error *error) override
    {
        Q_UNUSED(spec);
        if (error) {
            *error = {};
        }
        return running ? QStringLiteral("running") : QStringLiteral("stopped");
    }

    QString pid(const QemuSpec &spec) override
    {
        Q_UNUSED(spec);
        return running ? QStringLiteral("4242") : QString();
    }

    QHash<QString, qint64> runningMachines() override
    {
        QHash<QString, qint64> result;
        if (running && !lastSpec.machineId.isEmpty()) {
            result.insert(lastSpec.machineId, 4242);
        }
        return result;
    }

    qint64 findPid(const QString &machineId) override
    {
        return running && lastSpec.machineId == machineId ? 4242 : 0;
    }

    bool terminate(const QString &machineId, qint64 pid, PodStack::Error *error) override
    {
        Q_UNUSED(machineId);
        Q_UNUSED(pid);
        if (error) {
            *error = {};
        }
        running = false;
        return true;
    }

    bool running = false;
    bool failStart = false;
    bool failStartSilently = false;
    bool failStop = false;
    int startCount = 0;
    int stopCount = 0;
    bool lastForce = false;
    QemuSpec lastSpec;
};

QStringList k8sKubeconfigLines()
{
    return {
        QStringLiteral("apiVersion: v1"),
        QStringLiteral("clusters:"),
        QStringLiteral("- cluster:"),
        QStringLiteral("    certificate-authority-data: Y2E="),
        QStringLiteral("    server: https://127.0.0.1:6443"),
        QStringLiteral("  name: default"),
        QStringLiteral("contexts:"),
        QStringLiteral("- context:"),
        QStringLiteral("    cluster: default"),
        QStringLiteral("    user: default"),
        QStringLiteral("  name: default"),
        QStringLiteral("current-context: default"),
        QStringLiteral("kind: Config"),
        QStringLiteral("users:"),
        QStringLiteral("- name: default"),
        QStringLiteral("  user:"),
        QStringLiteral("    client-certificate-data: Y2VydA=="),
    };
}

QString k8sFakeSsh(const QString &root, const QByteArray &body)
{
    const QString path = root + QStringLiteral("/ssh-fake.sh");
    if (!writeTestFile(path, body,
                       QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner)) {
        return {};
    }
    return path;
}

MachineManager::Options k8sMachineOptions(const QString &root, const DistroImage &image,
                                          const QString &qemuImg, const QString &ovmfCode,
                                          const QString &ovmfVars,
                                          FakeQemuRuntime **runtimeOut = nullptr)
{
    auto runtime = std::make_unique<FakeQemuRuntime>();
    if (runtimeOut) {
        *runtimeOut = runtime.get();
    }
    MachineManager::Options options;
    if (!configureSshOptions(&options, root)) {
        return {};
    }
    options.storePath = root + QStringLiteral("/podstack.db");
    options.machinesRoot = root + QStringLiteral("/machines");
    options.imageCacheDir = root + QStringLiteral("/images");
    options.qemuImgBinary = qemuImg;
    options.ovmfCodePath = ovmfCode;
    options.ovmfVarsPath = ovmfVars;
    options.sshPortStart = freePortHint();
    options.runtime = std::move(runtime);
    options.resolveDistro = [image](const QString &, const QString &, PodStack::Error *) {
        return image;
    };
    return options;
}

/// Processo fake destacado cujo argv contém `podstack:<id>` (sem pidfile).
class FakeMachineProcess {
public:
    ~FakeMachineProcess() { stop(); }

    qint64 start(const QString &machineId, const QString &prefix = QString(),
                 const QString &extraArgument = QString())
    {
        const QString marker = (prefix.isEmpty() ? QStringLiteral("podstack:") : prefix)
                               + machineId;
        m_marker = marker;

        // `sleep 60 podstack:<id>` não serve: o sleep trata o marcador como
        // segundo intervalo e sai. Python mantém o marcador no próprio argv
        // sem criar filho; o shell é o fallback quando o python3 não existe.
        QString program = QStandardPaths::findExecutable(QStringLiteral("python3"));
        QStringList arguments;
        if (!program.isEmpty()) {
            arguments = {QStringLiteral("-c"),
                         QStringLiteral("import time; time.sleep(60)"), marker};
        } else {
            program = QStandardPaths::findExecutable(QStringLiteral("sh"));
            if (program.isEmpty()) {
                return 0;
            }
            arguments = {QStringLiteral("-c"),
                         QStringLiteral("while :; do sleep 0.05; done"), marker};
        }
        if (!extraArgument.isEmpty()) {
            arguments.append(extraArgument);
        }

        qint64 pid = 0;
        if (!QProcess::startDetached(program, arguments, QString(), &pid)) {
            return 0;
        }
        m_pid = pid;
        return m_pid;
    }

    qint64 pid() const { return m_pid; }

    void stop()
    {
        if (m_pid > 0 && markerPresent()) {
            ::kill(static_cast<pid_t>(m_pid), SIGKILL);
        }
        m_pid = 0;
    }

private:
    bool markerPresent() const
    {
        QFile cmdline(QStringLiteral("/proc/%1/cmdline").arg(m_pid));
        if (!cmdline.open(QIODevice::ReadOnly)) {
            return false;
        }
        const QByteArray payload = cmdline.read(64 * 1024);
        return payload.contains(m_marker.toUtf8());
    }

    qint64 m_pid = 0;
    QString m_marker;
};

bool waitForMachinePid(PodStack::QemuRuntime *runtime, const QString &machineId,
                       qint64 expected, int timeoutMs = 3000)
{
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeoutMs) {
        if (runtime->findPid(machineId) == expected) {
            return true;
        }
        QTest::qWait(20);
    }
    return runtime->findPid(machineId) == expected;
}

bool waitForMachineGone(PodStack::QemuRuntime *runtime, const QString &machineId,
                        int timeoutMs = 3000)
{
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeoutMs) {
        if (runtime->findPid(machineId) == 0) {
            return true;
        }
        QTest::qWait(20);
    }
    return runtime->findPid(machineId) == 0;
}

bool seedMachine(const QString &storePath, const QString &machinesRoot,
                 const MachineMeta &meta)
{
    const QString directory = machinesRoot + QLatin1Char('/') + meta.id;
    if (!QDir().mkpath(directory)
        || !MachineMetaFile::write(directory + QStringLiteral("/meta.json"), meta, nullptr)) {
        return false;
    }

    MachineStore store(storePath);
    PodStack::Error error;
    return store.open(&error) && error.isEmpty() && store.upsert(meta, &error)
           && error.isEmpty();
}

/// Reescreve uma entrada do `.tar.zst` mantendo o manifesto intacto. Usado para
/// simular corrupção de conteúdo e validar a checagem de checksum no import.
bool rewriteArchiveEntry(const QString &source, const QString &target,
                         const QString &entryName, const QByteArray &replacement)
{
    struct archive *reader = archive_read_new();
    if (!reader) {
        return false;
    }
    archive_read_support_filter_zstd(reader);
    archive_read_support_format_tar(reader);
    const QByteArray sourceBytes = QFile::encodeName(source);
    if (archive_read_open_filename(reader, sourceBytes.constData(), 10240) != ARCHIVE_OK) {
        archive_read_free(reader);
        return false;
    }

    struct archive *writer = archive_write_new();
    if (!writer) {
        archive_read_free(reader);
        return false;
    }
    archive_write_add_filter_zstd(writer);
    archive_write_set_format_pax_restricted(writer);
    const QByteArray targetBytes = QFile::encodeName(target);
    if (archive_write_open_filename(writer, targetBytes.constData()) != ARCHIVE_OK) {
        archive_read_free(reader);
        archive_write_free(writer);
        return false;
    }

    bool ok = true;
    QByteArray buffer(64 * 1024, '\0');
    while (true) {
        struct archive_entry *entry = nullptr;
        const int status = archive_read_next_header(reader, &entry);
        if (status == ARCHIVE_EOF) {
            break;
        }
        if (status != ARCHIVE_OK) {
            ok = false;
            break;
        }

        const QString name = QString::fromUtf8(archive_entry_pathname(entry));
        QByteArray payload;
        while (true) {
            const la_ssize_t read = archive_read_data(reader, buffer.data(), buffer.size());
            if (read < 0) {
                ok = false;
                break;
            }
            if (read == 0) {
                break;
            }
            payload.append(buffer.constData(), read);
        }
        if (!ok) {
            break;
        }

        const QByteArray data = name == entryName ? replacement : payload;
        struct archive_entry *output = archive_entry_new();
        const QByteArray nameBytes = name.toUtf8();
        archive_entry_set_pathname(output, nameBytes.constData());
        archive_entry_set_filetype(output, AE_IFREG);
        archive_entry_set_perm(output, 0600);
        archive_entry_set_size(output, data.size());
        if (archive_write_header(writer, output) != ARCHIVE_OK) {
            archive_entry_free(output);
            ok = false;
            break;
        }
        archive_entry_free(output);
        if (!data.isEmpty()) {
            archive_write_data(writer, data.constData(), static_cast<size_t>(data.size()));
        }
        if (archive_write_finish_entry(writer) != ARCHIVE_OK) {
            ok = false;
            break;
        }
    }

    archive_read_free(reader);
    archive_write_close(writer);
    archive_write_free(writer);
    return ok;
}

} // namespace

class MachinesTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void registryDefaults();
    void registryRejectsInvalid();
    void registryLists();
    void pathsCacheDir();
    void pathsMachinesDir();
    void machineMetaRoundTrip();
    void serialPortsListing();
    void serialPortsValidation();
    void usbDevicesListing();
    void ubuntuOvmfCandidates();
    void machineMetaInvalid();
    void machineMetaFileErrors();
    void machineStoreCrud();
    void machineStoreErrors();
    void machineStoreLegacySchema();
    void machineStoreSharesDb();
    void downloadAndReuse();
    void downloadHttpError();
    void downloadChecksumMismatch();
    void cacheSidecarMissing();
    void downloadCancelled();
    void cloudImageCacheErrors();
    void cloudImageCacheEdgeCases();
    void httpDownloaderSuccess();
    void httpDownloaderErrors();
    void seedIsoEntries();
    void seedYamlDefaults();
    void seedPasswordHashed();
    void seedVerbatimUserData();
    void seedEnvironment();
    void seedEscapesYaml();
    void seedValidationErrors();
    void qemuBinary();
    void qemuCommandKvm();
    void qemuCommandTcg();
    void qemuCommandArm64();
    void qemuCommandUefi();
    void qemuCommandIsolated();
    void qemuCommandMounts();
    void qemuCommandPorts();
    void qemuCommandSerialPorts();
    void qemuCommandUsbDevices();
    void qemuCommandAudio();
    void qemuCommandInvalid();
    void qmpHandshake();
    void qmpQueryStatus();
    void qmpPowerdown();
    void qmpErrorResponse();
    void qmpInterleavedEvent();
    void qmpTimeout();
    void qmpQuit();
    void qmpConnectMissing();
    void qmpInvalidStates();
    void qmpSocketCloses();
    void idsUuidV7();
    void machineManagerCreate();
    void k8sEnableProvisionsCluster();
    void k8sEnableReusesCluster();
    void k8sEnableInstallFailure();
    void k8sEnableCancelled();
    void k8sStatusReconcilesStoppedMachine();
    void k8sStoreLifecycle();
    void k8sStatusStaleAndMissingVm();
    void k8sEnableNameConflict();
    void k8sSshRunnerSuccess();
    void k8sSshRunnerFailure();
    void k8sSshRunnerTimeout();
    void k8sSshRunnerCancelled();
    void k8sSshRunnerStartFailure();
    void k8sSshReadyTimeout();
    void k8sNodeNotReady();
    void k8sInstallFailureWithoutError();
    void k8sSshReadyCancelledByRemote();
    void k8sClusterJsonRoundtrip();
    void k8sNullManager();
    void k8sEnableClearsMissingVmAndStartsStopped();
    void k8sNodeReadyWithoutError();
    void k8sBrokenStore();
    void k8sDefaultSshTimeout();
    void k8sCreateStartFailure();
    void k8sEnableWritesKubeconfig();
    void k8sKubeconfigFormatError();
    void k8sKubeconfigReadFailure();
    void k8sKubeconfigDirFailure();
    void k8sListsPodsAndServices();
    void k8sListRequiresReadyCluster();
    void k8sStreamLogs();
    void k8sDeleteAndPortForward();
    void k8sDisableIsIdempotent();
    void k8sUseContext();
    void machineManagerCreateErrors();
    void machineManagerPortsErrors();
    void machineManagerUpdateRemoveErrors();
    void machineManagerIsolated();
    void machineManagerMounts();
    void machineManagerValidation();
    void machineManagerErrors();
    void machineManagerQemuImgFailures();
    void machineManagerDiskErrors();
    void machineManagerSshKeyFailures();
    void machineManagerStateErrors();
    void machineManagerRuntimeErrors();
    void machineManagerLifecycle();
    void machineManagerUpdate();
    void machineManagerRename();
    void machineManagerClone();
    void machineDefaultConfig();
    void machineManagerExportImport();
    void machineArchiveManifestValidation();
    void machineArchiveExtractErrors();
    void machineArchiveWriteErrors();
    void machineManagerExportImportErrors();
    void machineManagerExportConcurrency();
    void machineManagerImportChecksum();
    void machineManagerCancelled();
    void machineManagerPort();
    void machineManagerPorts();
    void machineManagerSerialPorts();
    void machineManagerUsbDevices();
    void machineManagerAudio();
    void machineManagerSnapshots();
    void qemuRuntimeFallback();
    void qemuRuntimePidFile();
    void qemuRuntimeStartErrors();
    void qemuRuntimeFakeQemu();
    void machineManagerReconcile();
    void machineManagerStartFallback();
    void machineManagerRemoveOrphan();
    void sshConfigPreserves();
    void sshConfigPrefixNames();
    void sshConfigErrors();
    void machineManagerSsh();
    void machineStats();
    void machineStatsErrors();
    void machineFiles();
    void machineFilesErrors();
    void machineFilesIntegration();
    void qemuRuntimeIntegration();
    void virtiofsdValidation();
    void virtiofsdIntegration();

private:
    QString cacheDirFor(const QString &name) const;
    DistroImage testImage(const QString &path, const QByteArray &payload,
                          QCryptographicHash::Algorithm algorithm
                          = QCryptographicHash::Sha256) const;

    MockHttpServer m_server;
    QString m_root;
};

QString MachinesTest::cacheDirFor(const QString &name) const
{
    const QString directory = m_root + QStringLiteral("/caches/") + name;
    QDir(directory).removeRecursively();
    return directory;
}

DistroImage MachinesTest::testImage(const QString &path, const QByteArray &payload,
                                    QCryptographicHash::Algorithm algorithm) const
{
    DistroImage image;
    image.distro = QStringLiteral("test");
    image.version = QStringLiteral("1.0");
    image.arch = QStringLiteral("amd64");
    image.url = m_server.url(path);
    image.checksum = hexHash(payload, algorithm);
    image.algorithm = algorithm;
    image.size = payload.size();
    image.isDefault = true;
    return image;
}

void MachinesTest::initTestCase()
{
    m_root = QDir::tempPath()
             + QStringLiteral("/podstack-machines-%1").arg(QCoreApplication::applicationPid());
    QDir(m_root).removeRecursively();
    QVERIFY(QDir().mkpath(m_root));

    qputenv("XDG_CACHE_HOME", (m_root + QStringLiteral("/cache")).toUtf8());
    qputenv("XDG_CONFIG_HOME", (m_root + QStringLiteral("/config")).toUtf8());
    qputenv("XDG_DATA_HOME", (m_root + QStringLiteral("/data")).toUtf8());
    qputenv("XDG_STATE_HOME", (m_root + QStringLiteral("/state")).toUtf8());

    QVERIFY(m_server.isListening());
}

void MachinesTest::cleanupTestCase()
{
    QDir(m_root).removeRecursively();
    const QString shortRoot = QDir::tempPath()
                              + QStringLiteral("/psm-%1").arg(QCoreApplication::applicationPid());
    QDir(shortRoot).removeRecursively();
    QDir(shortRoot + QStringLiteral("-fail")).removeRecursively();
}

void MachinesTest::registryDefaults()
{
    PodStack::Error error;

    const DistroImage ubuntu = DistroRegistry::resolve(QStringLiteral("ubuntu"), QString(),
                                                       &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QCOMPARE(ubuntu.distro, QStringLiteral("ubuntu"));
    QCOMPARE(ubuntu.version, QStringLiteral("24.04"));
    QCOMPARE(ubuntu.arch, QStringLiteral("amd64"));
    QCOMPARE(ubuntu.algorithm, QCryptographicHash::Sha256);
    QCOMPARE(ubuntu.checksum.size(), 64);
    QVERIFY(ubuntu.isDefault);
    QVERIFY(ubuntu.url.startsWith(QStringLiteral("https://")));

    const DistroImage ubuntuExplicit =
        DistroRegistry::resolve(QStringLiteral("ubuntu:24.04"), QStringLiteral("amd64"),
                                &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QCOMPARE(ubuntuExplicit.checksum, ubuntu.checksum);

    const DistroImage debian =
        DistroRegistry::resolve(QStringLiteral("debian:12"), QStringLiteral("amd64"), &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QCOMPARE(debian.algorithm, QCryptographicHash::Sha512);
    QCOMPARE(debian.checksum.size(), 128);
    QVERIFY(debian.isDefault);

    const DistroImage fedora =
        DistroRegistry::resolve(QStringLiteral("fedora"), QStringLiteral("amd64"), &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QCOMPARE(fedora.version, QStringLiteral("44"));
    QCOMPARE(fedora.size, 583729152LL);
    QCOMPARE(fedora.algorithm, QCryptographicHash::Sha256);

    if (DistroRegistry::hostArch() == QStringLiteral("amd64")) {
        const DistroImage host = DistroRegistry::resolve(QStringLiteral("ubuntu"), {}, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error.message));
        QCOMPARE(host.arch, QStringLiteral("amd64"));
    }

    const DistroImage x86 =
        DistroRegistry::resolve(QStringLiteral("ubuntu"), QStringLiteral("x86_64"), &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QCOMPARE(x86.arch, QStringLiteral("amd64"));
}

void MachinesTest::registryRejectsInvalid()
{
    PodStack::Error error;

    QVERIFY(DistroRegistry::resolve(QString(), QStringLiteral("amd64"), &error).url.isEmpty());
    QCOMPARE(error.code, QStringLiteral("PODSTACK_INVALID_PARAMS"));

    QVERIFY(DistroRegistry::resolve(QStringLiteral("gentoo"), QStringLiteral("amd64"), &error)
                .url.isEmpty());
    QCOMPARE(error.code, QStringLiteral("PODSTACK_INVALID_PARAMS"));

    QVERIFY(DistroRegistry::resolve(QStringLiteral("ubuntu:24.04:extra"),
                                    QStringLiteral("amd64"), &error)
                .url.isEmpty());
    QCOMPARE(error.code, QStringLiteral("PODSTACK_INVALID_PARAMS"));

    QVERIFY(DistroRegistry::resolve(QStringLiteral("ubuntu:"), QStringLiteral("amd64"), &error)
                .url.isEmpty());
    QCOMPARE(error.code, QStringLiteral("PODSTACK_INVALID_PARAMS"));

    QVERIFY(DistroRegistry::resolve(QStringLiteral("ubuntu:99.04"), QStringLiteral("amd64"),
                                    &error)
                .url.isEmpty());
    QCOMPARE(error.code, QStringLiteral("PODSTACK_NOT_FOUND"));

    QVERIFY(DistroRegistry::resolve(QStringLiteral("ubuntu"), QStringLiteral("aarch64"), &error)
                .url.isEmpty());
    QCOMPARE(error.code, QStringLiteral("PODSTACK_UNSUPPORTED"));

    QVERIFY(DistroRegistry::resolve(QStringLiteral("ubuntu"), QStringLiteral("riscv64"), &error)
                .url.isEmpty());
    QCOMPARE(error.code, QStringLiteral("PODSTACK_UNSUPPORTED"));
}

void MachinesTest::registryLists()
{
    const QVector<DistroImage> images = DistroRegistry::images();
    QCOMPARE(images.size(), 3);
    QCOMPARE(DistroRegistry::distros(),
             QStringList({QStringLiteral("ubuntu"), QStringLiteral("debian"),
                          QStringLiteral("fedora")}));
}

void MachinesTest::pathsCacheDir()
{
    QCOMPARE(PodStack::Paths::cacheDir(), m_root + QStringLiteral("/cache/podstack"));
    QCOMPARE(PodStack::Paths::cloudImagesDir(),
             m_root + QStringLiteral("/cache/podstack/images"));

    QString error;
    QVERIFY2(PodStack::Paths::ensureDirs(&error), qPrintable(error));

    const QFileInfo info(PodStack::Paths::cloudImagesDir());
    QVERIFY(info.isDir());
    QVERIFY(ownerOnly(info.permissions(), true));
}

void MachinesTest::pathsMachinesDir()
{
    const QString id = QStringLiteral("0190f0f0-0000-7000-8000-000000000001");
    QCOMPARE(PodStack::Paths::machinesDir(), m_root + QStringLiteral("/data/podstack/machines"));
    QCOMPARE(PodStack::Paths::machineDir(id),
             PodStack::Paths::machinesDir() + QLatin1Char('/') + id);
    QCOMPARE(PodStack::Paths::machineDir(QStringLiteral("name:with.dots")),
             PodStack::Paths::machinesDir() + QStringLiteral("/name:with.dots"));
    QCOMPARE(PodStack::Paths::machineDir(QStringLiteral("name_with_underscore")),
             PodStack::Paths::machinesDir() + QStringLiteral("/name_with_underscore"));

    QVERIFY(PodStack::Paths::machineDir(QString()).isEmpty());
    QVERIFY(PodStack::Paths::machineDir(QStringLiteral(".")).isEmpty());
    QVERIFY(PodStack::Paths::machineDir(QStringLiteral("..")).isEmpty());
    QVERIFY(PodStack::Paths::machineDir(QStringLiteral("a..b")).isEmpty());
    QVERIFY(PodStack::Paths::machineDir(QStringLiteral("../escape")).isEmpty());
    QVERIFY(PodStack::Paths::machineDir(QStringLiteral("a/b")).isEmpty());
    QVERIFY(PodStack::Paths::machineDir(QStringLiteral("a b")).isEmpty());
    QVERIFY(PodStack::Paths::machineDir(QStringLiteral("ação")).isEmpty());

    QString error;
    QVERIFY2(PodStack::Paths::ensureDirs(&error), qPrintable(error));
    const QFileInfo info(PodStack::Paths::machinesDir());
    QVERIFY(info.isDir());
    QVERIFY(ownerOnly(info.permissions(), true));
}

void MachinesTest::machineMetaRoundTrip()
{
    const QString directory = m_root + QStringLiteral("/meta-roundtrip/machine");
    QDir(directory).removeRecursively();
    const QString path = directory + QStringLiteral("/meta.json");

    MachineMeta meta = testMachine(QStringLiteral("0190f0f0-0000-7000-8000-000000000010"),
                                   QStringLiteral("dev-ubuntu"));
    meta.cpus = 4;
    meta.memoryBytes = 8589934592LL;
    meta.diskBytes = 32212254720LL;
    meta.state = QStringLiteral("running");
    meta.isolated = true;
    meta.isolateNetwork = true;
    meta.forwardSshAgent = true;
    meta.lastBootAt = QStringLiteral("2026-11-09T10:00:00.123Z");
    meta.updatedAt = QStringLiteral("2026-11-09T10:00:00.123Z");
    meta.error = QStringLiteral("último erro simulado");
    meta.ports = {
        MachinePort{QStringLiteral("port-ssh"), 32222, 22, QStringLiteral("tcp"), false,
                    QStringLiteral("ssh")},
        MachinePort{QStringLiteral("port-http"), 8080, 80, QStringLiteral("tcp"), true,
                    QStringLiteral("http")},
    };
    meta.mounts = {
        MachineMount{QStringLiteral("mount-projetos"),
                     QStringLiteral("/home/marcos/Projetos"),
                     QStringLiteral("/mnt/projetos"), false, QStringLiteral("host")},
    };
    meta.serialPorts = {QStringLiteral("/dev/ttyUSB0"), QStringLiteral("/dev/ttyACM0")};
    meta.usbDevices = {MachineUsbDevice{1, 4, QStringLiteral("0781"), QStringLiteral("5567"),
                                        QStringLiteral("SanDisk Cruzer Blade")}};
    meta.audioEnabled = true;

    PodStack::Error error;
    QVERIFY2(MachineMetaFile::write(path, meta, &error), qPrintable(error.message));
    QVERIFY(error.isEmpty());

    const QFileInfo fileInfo(path);
    QVERIFY(fileInfo.isFile());
    QVERIFY(ownerOnly(fileInfo.permissions(), false));
    QVERIFY(ownerOnly(QFileInfo(directory).permissions(), true));
    QCOMPARE(QDir(directory).entryList(QDir::Files | QDir::Hidden),
             QStringList{QStringLiteral("meta.json")});
    QVERIFY(!QFileInfo::exists(path + QStringLiteral(".tmp")));

    QFile raw(path);
    QVERIFY(raw.open(QIODevice::ReadOnly));
    const QJsonObject object = QJsonDocument::fromJson(raw.readAll()).object();
    QCOMPARE(object.value(QStringLiteral("distroVersion")).toString(),
             QStringLiteral("24.04"));
    QCOMPARE(object.value(QStringLiteral("memoryBytes")).toInteger(), 8589934592LL);
    QCOMPARE(object.value(QStringLiteral("forwardSshAgent")).toBool(), true);
    QCOMPARE(object.value(QStringLiteral("ports")).toArray().size(), 2);
    QCOMPARE(object.value(QStringLiteral("mounts")).toArray().size(), 1);
    QCOMPARE(object.value(QStringLiteral("serialPorts")).toArray().size(), 2);
    QCOMPARE(object.value(QStringLiteral("usbDevices")).toArray().size(), 1);
    QCOMPARE(object.value(QStringLiteral("audioEnabled")).toBool(), true);
    QVERIFY(!object.contains(QStringLiteral("password")));

    const MachineMeta reloaded = MachineMetaFile::read(path, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QCOMPARE(QJsonDocument(reloaded.toJson()).toJson(QJsonDocument::Compact),
             QJsonDocument(meta.toJson()).toJson(QJsonDocument::Compact));
    QCOMPARE(reloaded.ports.size(), 2);
    QCOMPARE(reloaded.ports.at(1).hostPort, 8080);
    QCOMPARE(reloaded.ports.at(1).label, QStringLiteral("http"));
    QCOMPARE(reloaded.mounts.size(), 1);
    QCOMPARE(reloaded.mounts.at(0).tag, QStringLiteral("host"));
    QCOMPARE(reloaded.serialPorts, meta.serialPorts);
    QCOMPARE(reloaded.usbDevices.size(), 1);
    QCOMPARE(reloaded.usbDevices.at(0).description, QStringLiteral("SanDisk Cruzer Blade"));
    QCOMPARE(reloaded.audioEnabled, true);
}

void MachinesTest::serialPortsListing()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString dev = dir.filePath(QStringLiteral("dev"));
    const QString sys = dir.filePath(QStringLiteral("sys"));
    QVERIFY(QDir().mkpath(dev));
    QVERIFY(QDir().mkpath(sys));

    const auto touch = [](const QString &path, const QByteArray &content = {}) {
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly)) {
            return false;
        }
        file.write(content);
        return true;
    };

    QVERIFY(touch(dev + QStringLiteral("/ttyUSB0")));
    QVERIFY(touch(sys + QStringLiteral("/ttyUSB0/device/manufacturer"), "Acme\n"));
    QVERIFY(touch(sys + QStringLiteral("/ttyUSB0/device/product"), "SerialX\n"));
    QVERIFY(touch(dev + QStringLiteral("/ttyUSB1")));
    QVERIFY(touch(sys + QStringLiteral("/ttyUSB1/device/uevent"), "DEVTYPE=usb\n"));
    QVERIFY(touch(dev + QStringLiteral("/ttyACM0")));
    QVERIFY(touch(sys + QStringLiteral("/ttyACM0/device/manufacturer"), "Acme\n"));
    QVERIFY(touch(sys + QStringLiteral("/ttyACM0/device/product"), "CDC ACM\n"));
    QVERIFY(touch(dev + QStringLiteral("/ttyACM1")));
    QVERIFY(touch(sys + QStringLiteral("/ttyACM1/device/idVendor"), "1a86\n"));
    QVERIFY(touch(sys + QStringLiteral("/ttyACM1/device/idProduct"), "7523\n"));
    QVERIFY(touch(dev + QStringLiteral("/ttyUSB2")));
    QVERIFY(touch(sys + QStringLiteral("/ttyUSB2/device/uevent"), "DEVTYPE=usb\n"));
    QVERIFY(touch(sys + QStringLiteral("/ttyUSB2/manufacturer"), "Parent\n"));
    QVERIFY(touch(sys + QStringLiteral("/ttyUSB2/product"), "Device\n"));
    QVERIFY(touch(dev + QStringLiteral("/ttyS0")));
    QVERIFY(touch(sys + QStringLiteral("/ttyS0/type"), "4\n"));
    QVERIFY(touch(dev + QStringLiteral("/ttyS1")));
    QVERIFY(touch(sys + QStringLiteral("/ttyS1/type"), "0\n"));
    QVERIFY(touch(dev + QStringLiteral("/ttyS2")));
    QVERIFY(touch(dev + QStringLiteral("/ttyS3")));
    QVERIFY(touch(sys + QStringLiteral("/ttyS3/type"), "9\n"));
    const QList<int> uartTypes{1, 2, 3, 5, 6, 7, 8};
    for (int index = 0; index < uartTypes.size(); ++index) {
        const QString name = QStringLiteral("ttyS%1").arg(index + 4);
        QVERIFY(touch(dev + QStringLiteral("/") + name));
        QVERIFY(touch(sys + QStringLiteral("/") + name + QStringLiteral("/type"),
                      QByteArray::number(uartTypes.at(index)) + '\n'));
    }
    QVERIFY(touch(dev + QStringLiteral("/notaserial")));

    const QVector<PodStack::SerialPortInfo> ports = PodStack::listSerialPortsIn(dev, sys);
    QCOMPARE(ports.size(), 14);
    (void) PodStack::listHostSerialPorts();

    QHash<QString, QString> descriptions;
    for (const PodStack::SerialPortInfo &port : ports) {
        descriptions.insert(QFileInfo(port.path).fileName(), port.description);
        QVERIFY(!port.path.isEmpty());
        QVERIFY(QDir::isAbsolutePath(port.path));
        QVERIFY(QFileInfo::exists(port.path));
        QVERIFY(!port.description.isEmpty());
        QVERIFY(port.kind == QStringLiteral("usb") || port.kind == QStringLiteral("acm") ||
                port.kind == QStringLiteral("uart"));
    }
    QCOMPARE(descriptions.value(QStringLiteral("ttyUSB0")), QStringLiteral("Acme SerialX"));
    QCOMPARE(descriptions.value(QStringLiteral("ttyUSB1")), QStringLiteral("Adaptador USB serial"));
    QCOMPARE(descriptions.value(QStringLiteral("ttyACM0")), QStringLiteral("Acme CDC ACM"));
    QCOMPARE(descriptions.value(QStringLiteral("ttyACM1")), QStringLiteral("1a86:7523"));
    QCOMPARE(descriptions.value(QStringLiteral("ttyUSB2")), QStringLiteral("Parent Device"));
    QCOMPARE(descriptions.value(QStringLiteral("ttyS0")), QStringLiteral("UART 16550A"));
    QCOMPARE(descriptions.value(QStringLiteral("ttyS3")), QStringLiteral("UART"));
    QVERIFY(!descriptions.contains(QStringLiteral("ttyS1")));
    QVERIFY(!descriptions.contains(QStringLiteral("ttyS2")));
}

void MachinesTest::serialPortsValidation()
{
    QString error;
    QVERIFY(!PodStack::validateSerialPortPath(QString(), &error));
    QVERIFY(!error.isEmpty());
    QVERIFY(!PodStack::validateSerialPortPath(QStringLiteral("dev/ttyUSB0"), &error));
    QVERIFY(!PodStack::validateSerialPortPath(m_root + QStringLiteral("/missing-tty"), &error));

    const QString path = m_root + QStringLiteral("/fake-tty");
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("serial");
    file.close();
    QVERIFY(PodStack::validateSerialPortPath(path, &error));
    QVERIFY(error.isEmpty());
}

void MachinesTest::usbDevicesListing()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString root = dir.filePath(QStringLiteral("usb"));

    const auto touch = [](const QString &path, const QByteArray &content) {
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly)) {
            return false;
        }
        file.write(content);
        return true;
    };

    QVERIFY(touch(root + QStringLiteral("/1-4/busnum"), "1\n"));
    QVERIFY(touch(root + QStringLiteral("/1-4/devnum"), "4\n"));
    QVERIFY(touch(root + QStringLiteral("/1-4/idVendor"), "0781\n"));
    QVERIFY(touch(root + QStringLiteral("/1-4/idProduct"), "5567\n"));
    QVERIFY(touch(root + QStringLiteral("/1-4/manufacturer"), "SanDisk\n"));
    QVERIFY(touch(root + QStringLiteral("/1-4/product"), "Cruzer Blade\n"));

    QVERIFY(touch(root + QStringLiteral("/1-1/busnum"), "1\n"));
    QVERIFY(touch(root + QStringLiteral("/1-1/devnum"), "1\n"));
    QVERIFY(touch(root + QStringLiteral("/1-1/bDeviceClass"), "09\n"));
    QVERIFY(touch(root + QStringLiteral("/1-1/idVendor"), "1d6b\n"));
    QVERIFY(touch(root + QStringLiteral("/1-1/idProduct"), "0002\n"));

    QVERIFY(touch(root + QStringLiteral("/1-5/busnum"), "1\n"));
    QVERIFY(touch(root + QStringLiteral("/1-5/devnum"), "5\n"));
    QVERIFY(touch(root + QStringLiteral("/1-5/idVendor"), "abcd\n"));
    QVERIFY(touch(root + QStringLiteral("/1-5/idProduct"), "ef01\n"));

    QVERIFY(touch(root + QStringLiteral("/1-6/busnum"), "1\n"));
    QVERIFY(touch(root + QStringLiteral("/1-6/devnum"), "6\n"));

    const QVector<PodStack::UsbDeviceInfo> devices = PodStack::listUsbDevicesIn(root);
    QCOMPARE(devices.size(), 2);
    QCOMPARE(devices.at(0).bus, 1);
    QCOMPARE(devices.at(0).address, 4);
    QCOMPARE(devices.at(0).description, QStringLiteral("SanDisk Cruzer Blade"));
    QCOMPARE(devices.at(1).description, QStringLiteral("ABCD:EF01"));

    PodStack::UsbDeviceInfo found;
    qputenv("PODSTACK_SYSFS_USB", root.toUtf8());
    QVERIFY(PodStack::findUsbDevice(1, 4, &found));
    QCOMPARE(found.vendorId, QStringLiteral("0781"));
    QString error;
    QVERIFY(!PodStack::findUsbDevice(9, 9, &found, &error));
    QVERIFY(!error.isEmpty());

    QCOMPARE(PodStack::listHostUsbDevices().size(), 2);
    qunsetenv("PODSTACK_SYSFS_USB");
}

void MachinesTest::machineMetaInvalid()
{
    const QString root = m_root + QStringLiteral("/meta-invalid");
    QDir(root).removeRecursively();
    QVERIFY(QDir().mkpath(root));

    PodStack::Error error;
    const MachineMeta missing = MachineMetaFile::read(root + QStringLiteral("/missing/meta.json"),
                                                      &error);
    QVERIFY(missing.id.isEmpty());
    QCOMPARE(error.code, QStringLiteral("PODSTACK_NOT_FOUND"));

    const QString path = root + QStringLiteral("/meta.json");
    {
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        QVERIFY(file.write(QByteArrayLiteral("{ \"id\": ")) > 0);
    }
    QVERIFY(MachineMetaFile::read(path, &error).id.isEmpty());
    QCOMPARE(error.code, QStringLiteral("PODSTACK_IO_ERROR"));

    {
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        QVERIFY(file.write(QByteArrayLiteral("[]")) > 0);
    }
    QVERIFY(MachineMetaFile::read(path, &error).id.isEmpty());
    QCOMPARE(error.code, QStringLiteral("PODSTACK_IO_ERROR"));

    const MachineMeta meta = testMachine(QStringLiteral("0190f0f0-0000-7000-8000-000000000011"),
                                         QStringLiteral("invalid-write"));
    QVERIFY(!MachineMetaFile::write(root, meta, &error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_IO_ERROR"));
}

void MachinesTest::machineMetaFileErrors()
{
    const QString root = m_root + QStringLiteral("/meta-file-errors");
    QDir(root).removeRecursively();
    QVERIFY(QDir().mkpath(root));

    const MachineMeta meta = testMachine(QStringLiteral("0190f0f0-0000-7000-8000-000000000012"),
                                         QStringLiteral("meta-errors"));
    PodStack::Error error;
    QVERIFY(!MachineMetaFile::write(QString(), meta, &error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_IO_ERROR"));

    const QString parentFile = root + QStringLiteral("/not-a-dir");
    QVERIFY(writeTestFile(parentFile, QByteArrayLiteral("x")));
    error = {};
    QVERIFY(!MachineMetaFile::write(parentFile + QStringLiteral("/meta.json"), meta, &error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_IO_ERROR"));

    error = {};
    QVERIFY(MachineMetaFile::read(root, &error).id.isEmpty());
    QCOMPARE(error.code, QStringLiteral("PODSTACK_IO_ERROR"));

    const QString valid = root + QStringLiteral("/valid/meta.json");
    QVERIFY2(MachineMetaFile::write(valid, meta, &error), qPrintable(error.message));
    QVERIFY(ownerOnly(QFileInfo(valid).permissions(), false));

    // Campos opcionais ausentes usam defaults; entradas não-objeto são ignoradas.
    const QJsonObject minimal{
        {QStringLiteral("id"), QStringLiteral("minimal")},
        {QStringLiteral("name"), QStringLiteral("minimal")},
        {QStringLiteral("ports"), QJsonArray{QStringLiteral("bad")}},
        {QStringLiteral("mounts"), QJsonArray{42}},
    };
    const MachineMeta parsed = MachineMeta::fromJson(minimal);
    QCOMPARE(parsed.cpus, 2);
    QVERIFY(parsed.ports.isEmpty());
    QVERIFY(parsed.mounts.isEmpty());
    QCOMPARE(parsed.id, QStringLiteral("minimal"));

    MachineSnapshot snapshot;
    snapshot.id = QStringLiteral("snap");
    snapshot.name = QStringLiteral("snap");
    snapshot.sizeBytes = 1024;
    snapshot.date = QStringLiteral("2026-01-01T00:00:00.000Z");
    QCOMPARE(snapshot.toJson().value(QStringLiteral("sizeBytes")).toInteger(), 1024);
}

void MachinesTest::machineStoreCrud()
{
    const QString directory = m_root + QStringLiteral("/store-crud");
    const QString dbPath = directory + QStringLiteral("/podstack.db");
    QDir(directory).removeRecursively();

    MachineStore store(dbPath);
    QCOMPARE(store.path(), dbPath);
    PodStack::Error error;
    QVERIFY2(store.open(&error), qPrintable(error.message));
    QVERIFY(error.isEmpty());
    QVERIFY(QFileInfo(dbPath).isFile());
    QVERIFY(ownerOnly(QFileInfo(dbPath).permissions(), false));
    QVERIFY(QFileInfo::exists(dbPath + QStringLiteral("-wal")));
    QVERIFY(QFileInfo::exists(dbPath + QStringLiteral("-shm")));
    QVERIFY(ownerOnly(QFileInfo(dbPath + QStringLiteral("-wal")).permissions(), false));
    QVERIFY(ownerOnly(QFileInfo(dbPath + QStringLiteral("-shm")).permissions(), false));

    MachineMeta first = testMachine(QStringLiteral("0190f0f0-0000-7000-8000-000000000020"),
                                    QStringLiteral("dev-ubuntu"));
    first.ports = {
        MachinePort{QStringLiteral("port-http"), 8080, 80, QStringLiteral("tcp"), true,
                    QStringLiteral("http")},
        MachinePort{QStringLiteral("port-ssh"), 32222, 22, QStringLiteral("tcp"), false,
                    QStringLiteral("ssh")},
    };
    first.mounts = {
        MachineMount{QStringLiteral("mount-projetos"),
                     QStringLiteral("/home/marcos/Projetos"),
                     QStringLiteral("/mnt/projetos"), false, QStringLiteral("host")},
    };

    MachineMeta second = testMachine(QStringLiteral("0190f0f0-0000-7000-8000-000000000021"),
                                     QStringLiteral("dev-debian"));
    second.distro = QStringLiteral("debian");
    second.distroVersion = QStringLiteral("12");
    second.createdAt = QStringLiteral("2026-11-08T12:01:00.000Z");
    second.ports = {
        MachinePort{QStringLiteral("port-ssh-debian"), 32223, 22, QStringLiteral("tcp"), false,
                    QStringLiteral("ssh")},
    };

    QVERIFY2(store.upsert(first, &error), qPrintable(error.message));
    QVERIFY2(store.upsert(second, &error), qPrintable(error.message));
    QCOMPARE(store.list(&error).size(), 2);

    const MachineMeta loaded = store.get(first.id, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QCOMPARE(QJsonDocument(loaded.toJson()).toJson(QJsonDocument::Compact),
             QJsonDocument(first.toJson()).toJson(QJsonDocument::Compact));

    const MachineMeta byName = store.getByName(QStringLiteral("dev-ubuntu"), &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QCOMPARE(byName.id, first.id);
    QCOMPARE(byName.ports.size(), 2);
    QCOMPARE(byName.mounts.size(), 1);

    QVERIFY(store.get(QStringLiteral("missing"), &error).id.isEmpty());
    QCOMPARE(error.code, QStringLiteral("PODSTACK_NOT_FOUND"));
    QVERIFY(store.getByName(QStringLiteral("missing"), &error).id.isEmpty());
    QCOMPARE(error.code, QStringLiteral("PODSTACK_NOT_FOUND"));

    MachineMeta updated = first;
    updated.cpus = 8;
    updated.state = QStringLiteral("stopped");
    updated.ports = {
        MachinePort{QStringLiteral("port-ssh"), 32000, 22, QStringLiteral("tcp"), false,
                    QStringLiteral("ssh")},
    };
    updated.mounts.clear();
    QVERIFY2(store.upsert(updated, &error), qPrintable(error.message));

    const MachineMeta afterUpsert = store.get(first.id, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QCOMPARE(afterUpsert.cpus, 8);
    QCOMPARE(afterUpsert.ports.size(), 1);
    QCOMPARE(afterUpsert.ports.at(0).hostPort, 32000);
    QVERIFY(afterUpsert.mounts.isEmpty());

    QVERIFY2(store.updateState(first.id, QStringLiteral("running"), QString(), &error),
             qPrintable(error.message));
    const MachineMeta running = store.get(first.id, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QCOMPARE(running.state, QStringLiteral("running"));
    QVERIFY(running.error.isEmpty());
    QVERIFY(running.updatedAt > updated.updatedAt);

    QVERIFY2(store.updateState(first.id, QStringLiteral("error"),
                               QStringLiteral("falha simulada"), &error),
             qPrintable(error.message));
    const MachineMeta failed = store.get(first.id, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QCOMPARE(failed.state, QStringLiteral("error"));
    QCOMPARE(failed.error, QStringLiteral("falha simulada"));

    QVERIFY(!store.updateState(QStringLiteral("missing"), QStringLiteral("running"), {},
                               &error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_NOT_FOUND"));

    QVERIFY2(store.remove(first.id, &error), qPrintable(error.message));
    QVERIFY(store.get(first.id, &error).id.isEmpty());
    QCOMPARE(error.code, QStringLiteral("PODSTACK_NOT_FOUND"));
    QCOMPARE(store.list(&error).size(), 1);
    QCOMPARE(machineRowCount(dbPath, QStringLiteral("machine_ports"), first.id), 0);
    QCOMPARE(machineRowCount(dbPath, QStringLiteral("machine_mounts"), first.id), 0);
    QCOMPARE(machineRowCount(dbPath, QStringLiteral("machine_ports"), second.id), 1);

    QVERIFY(!store.remove(first.id, &error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_NOT_FOUND"));
    QVERIFY2(store.remove(second.id, &error), qPrintable(error.message));
    QVERIFY(store.list(&error).isEmpty());
}

void MachinesTest::machineStoreErrors()
{
    const QString root = m_root + QStringLiteral("/store-errors");
    QDir(root).removeRecursively();
    QVERIFY(QDir().mkpath(root));

    PodStack::Error error;
    MachineStore invalidParent(QStringLiteral("/dev/null/podstack.db"));
    QVERIFY(!invalidParent.open(&error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_DB_ERROR"));

    error = {};
    MachineStore directory(root);
    QVERIFY(!directory.open(&error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_DB_ERROR"));

    const QString corrupt = root + QStringLiteral("/corrupt.db");
    QVERIFY(writeTestFile(corrupt, QByteArrayLiteral("this is not a sqlite database")));
    error = {};
    MachineStore corrupted(corrupt);
    QVERIFY(!corrupted.open(&error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_DB_ERROR"));

    const QString dbPath = root + QStringLiteral("/valid/podstack.db");
    MachineStore store(dbPath);
    QVERIFY2(store.open(&error), qPrintable(error.message));
    QVERIFY(store.open(&error));
    QVERIFY(store.list(&error).isEmpty());

    MachineMeta noId = testMachine(QString(), QStringLiteral("no-id"));
    error = {};
    QVERIFY(!store.upsert(noId, &error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_INVALID_PARAMS"));

    MachineMeta first = testMachine(QStringLiteral("0190f0f0-0000-7000-8000-000000000030"),
                                    QStringLiteral("store-errors"));
    QVERIFY2(store.upsert(first, &error), qPrintable(error.message));

    MachineMeta duplicate = testMachine(QStringLiteral("0190f0f0-0000-7000-8000-000000000031"),
                                        QStringLiteral("store-errors"));
    error = {};
    QVERIFY(!store.upsert(duplicate, &error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_DB_ERROR"));

    error = {};
    QVERIFY(!store.remove(QStringLiteral("missing"), &error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_NOT_FOUND"));

    error = {};
    MachineStore reopened(dbPath);
    QVERIFY2(reopened.open(&error), qPrintable(error.message));
    QCOMPARE(reopened.list(&error).size(), 1);
}

void MachinesTest::machineStoreLegacySchema()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("legacy.db"));
    sqlite3 *db = nullptr;
    QCOMPARE(sqlite3_open(path.toUtf8().constData(), &db), SQLITE_OK);
    QCOMPARE(
        sqlite3_exec(db, "CREATE TABLE machines (id TEXT PRIMARY KEY);", nullptr, nullptr, nullptr),
        SQLITE_OK);
    sqlite3_close(db);

    PodStack::MachineStore store(path);
    PodStack::Error error;
    QVERIFY(!store.open(&error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_DB_ERROR"));
    error = {};
    QVERIFY(store.list(&error).isEmpty());
    QCOMPARE(error.code, QStringLiteral("PODSTACK_DB_ERROR"));

    error = {};
    QVERIFY(store.get(QStringLiteral("missing"), &error).id.isEmpty());
    QCOMPARE(error.code, QStringLiteral("PODSTACK_DB_ERROR"));

    error = {};
    QVERIFY(
        !store.upsert(testMachine(QStringLiteral("legacy-id"), QStringLiteral("legacy")), &error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_DB_ERROR"));
}

void MachinesTest::machineStoreSharesDb()
{
    const QString directory = m_root + QStringLiteral("/shared-db");
    const QString dbPath = directory + QStringLiteral("/podstack.db");
    QDir(directory).removeRecursively();

    PodStack::Error error;
    BuildStore builds(dbPath);
    QVERIFY2(builds.open(&error), qPrintable(error.message));

    BuildRecord build;
    build.id = QStringLiteral("0190f0f0-0000-7000-8000-000000000030");
    build.imageRef = QStringLiteral("localhost/podstack-test:latest");
    build.status = QStringLiteral("success");
    build.startedAt = QStringLiteral("2026-11-09T11:00:00.000Z");
    build.finishedAt = QStringLiteral("2026-11-09T11:00:05.000Z");
    build.durationMs = 5000;
    build.trigger = QStringLiteral("gui");
    build.contextPath = QStringLiteral("/tmp/podstack-test/context");
    build.dockerfile = QStringLiteral("Dockerfile");
    build.logPath = QStringLiteral("/tmp/podstack-test/build.log");
    QVERIFY2(builds.insert(build, &error), qPrintable(error.message));

    MachineStore machines(dbPath);
    QVERIFY2(machines.open(&error), qPrintable(error.message));
    const MachineMeta meta = testMachine(
        QStringLiteral("0190f0f0-0000-7000-8000-000000000031"), QStringLiteral("shared-db"));
    QVERIFY2(machines.upsert(meta, &error), qPrintable(error.message));

    QCOMPARE(builds.get(build.id, &error).imageRef, build.imageRef);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QCOMPARE(machines.get(meta.id, &error).name, meta.name);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));

    BuildStore buildsReader(dbPath);
    QVERIFY2(buildsReader.open(&error), qPrintable(error.message));
    QCOMPARE(buildsReader.get(build.id, &error).status, QStringLiteral("success"));

    MachineStore machinesReader(dbPath);
    QVERIFY2(machinesReader.open(&error), qPrintable(error.message));
    QCOMPARE(machinesReader.getByName(meta.name, &error).id, meta.id);
    QCOMPARE(machinesReader.list(&error).size(), 1);
}

void MachinesTest::downloadAndReuse()
{
    const QByteArray payload = QByteArrayLiteral("conteudo de imagem cloud falsa");
    m_server.setResponse(QStringLiteral("/download-ok.qcow2"), 200, payload);

    const DistroImage image = testImage(QStringLiteral("/download-ok.qcow2"), payload);
    CloudImageCache cache(cacheDirFor(QStringLiteral("download-ok")));

    int progressCalls = 0;
    qint64 lastTotal = 0;
    QString path;
    PodStack::Error error;
    const bool ok = cache.ensure(
        image,
        [&progressCalls, &lastTotal](qint64, qint64 total) {
            ++progressCalls;
            lastTotal = total;
        },
        {}, &path, &error);
    QVERIFY2(ok, qPrintable(error.message));
    QCOMPARE(path, cache.cachedPath(image));
    QCOMPARE(lastTotal, static_cast<qint64>(payload.size()));
    QVERIFY(progressCalls > 0);

    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(file.readAll(), payload);
    QVERIFY(!QFileInfo::exists(path + QStringLiteral(".part")));
    QVERIFY(cache.isCached(image));
    QCOMPARE(cache.sizeBytes(image), static_cast<qint64>(payload.size()));
    QVERIFY(cache.sizeBytes(image) > 0);
    QCOMPARE(m_server.requestCount(QStringLiteral("/download-ok.qcow2")), 1);

    QFile sidecar(cache.sidecarPath(image));
    QVERIFY(sidecar.open(QIODevice::ReadOnly));
    const QJsonObject sidecarObject = QJsonDocument::fromJson(sidecar.readAll()).object();
    QCOMPARE(sidecarObject.value(QStringLiteral("url")).toString(), image.url);
    QCOMPARE(sidecarObject.value(QStringLiteral("checksum")).toString(), image.checksum);
    QCOMPARE(sidecarObject.value(QStringLiteral("algorithm")).toString(),
             QStringLiteral("sha256"));
    QCOMPARE(sidecarObject.value(QStringLiteral("size")).toVariant().toLongLong(),
             static_cast<qint64>(payload.size()));
    QVERIFY(!sidecarObject.value(QStringLiteral("date")).toString().isEmpty());

    QVERIFY(ownerOnly(QFileInfo(cache.directory()).permissions(), true));
    QVERIFY(ownerOnly(QFileInfo(path).permissions(), false));
    QVERIFY(ownerOnly(QFileInfo(cache.sidecarPath(image)).permissions(), false));

    QVERIFY2(cache.ensure(image, {}, {}, &path, &error), qPrintable(error.message));
    QCOMPARE(m_server.requestCount(QStringLiteral("/download-ok.qcow2")), 1);
}

void MachinesTest::downloadHttpError()
{
    m_server.setResponse(QStringLiteral("/missing.qcow2"), 404, QByteArrayLiteral("nao existe"));

    const DistroImage image =
        testImage(QStringLiteral("/missing.qcow2"), QByteArrayLiteral("esperado"));
    CloudImageCache cache(cacheDirFor(QStringLiteral("http-error")));

    QString path;
    PodStack::Error error;
    QVERIFY(!cache.ensure(image, {}, {}, &path, &error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_DOWNLOAD_FAILED"));
    QVERIFY(!QFileInfo::exists(cache.cachedPath(image)));
    QVERIFY(!QFileInfo::exists(cache.cachedPath(image) + QStringLiteral(".part")));
    QVERIFY(!cache.isCached(image));
}

void MachinesTest::downloadChecksumMismatch()
{
    const QByteArray payload = QByteArrayLiteral("conteudo adulterado");
    m_server.setResponse(QStringLiteral("/corrupt.qcow2"), 200, payload);

    DistroImage image = testImage(QStringLiteral("/corrupt.qcow2"),
                                  QByteArrayLiteral("outro conteudo"));
    CloudImageCache cache(cacheDirFor(QStringLiteral("checksum")));

    QString path;
    PodStack::Error error;
    QVERIFY(!cache.ensure(image, {}, {}, &path, &error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_DOWNLOAD_FAILED"));
    QVERIFY(!QFileInfo::exists(cache.cachedPath(image)));
    QVERIFY(!QFileInfo::exists(cache.sidecarPath(image)));
    QVERIFY(!QFileInfo::exists(cache.cachedPath(image) + QStringLiteral(".part")));
}

void MachinesTest::cacheSidecarMissing()
{
    const QByteArray payload = QByteArrayLiteral("imagem para rehash");
    m_server.setResponse(QStringLiteral("/rehash.qcow2"), 200, payload);

    const DistroImage image = testImage(QStringLiteral("/rehash.qcow2"), payload);
    CloudImageCache cache(cacheDirFor(QStringLiteral("rehash")));

    QString path;
    PodStack::Error error;
    QVERIFY2(cache.ensure(image, {}, {}, &path, &error), qPrintable(error.message));
    QVERIFY(cache.isCached(image));

    QVERIFY(QFile::remove(cache.sidecarPath(image)));
    QVERIFY(!cache.isCached(image));

    QVERIFY2(cache.ensure(image, {}, {}, &path, &error), qPrintable(error.message));
    QVERIFY(cache.isCached(image));
    QVERIFY(QFileInfo::exists(cache.sidecarPath(image)));
    QCOMPARE(m_server.requestCount(QStringLiteral("/rehash.qcow2")), 1);
}

void MachinesTest::downloadCancelled()
{
    const QByteArray payload(1024 * 1024, 'x');
    m_server.setResponse(QStringLiteral("/cancel.qcow2"), 200, payload);

    const DistroImage image = testImage(QStringLiteral("/cancel.qcow2"), payload);
    CloudImageCache cache(cacheDirFor(QStringLiteral("cancel")));

    bool cancelled = false;
    QString path;
    PodStack::Error error;
    const bool ok = cache.ensure(
        image,
        [&cancelled](qint64 received, qint64) {
            if (received > 0) {
                cancelled = true;
            }
        },
        [&cancelled] { return cancelled; }, &path, &error);
    QVERIFY(!ok);
    QCOMPARE(error.code, QStringLiteral("PODSTACK_CANCELLED"));
    QVERIFY(!QFileInfo::exists(cache.cachedPath(image)));
    QVERIFY(!QFileInfo::exists(cache.cachedPath(image) + QStringLiteral(".part")));
}

void MachinesTest::httpDownloaderSuccess()
{
    const QString root = m_root + QStringLiteral("/http-downloader-success");
    QDir(root).removeRecursively();
    QVERIFY(QDir().mkpath(root));

    m_server.setResponse(QStringLiteral("/downloader-success.qcow2"), 200,
                         QByteArrayLiteral("payload"));
    const QString destination = root + QStringLiteral("/image.qcow2");
    int progressCalls = 0;
    qint64 lastReceived = 0;
    HttpDownloader downloader;
    PodStack::Error error;
    QVERIFY2(downloader.download(
                 m_server.url(QStringLiteral("/downloader-success.qcow2")), destination,
                 [&](qint64 received, qint64) {
                     ++progressCalls;
                     lastReceived = received;
                 },
                 {}, &error),
             qPrintable(error.message));
    QVERIFY(QFileInfo(destination).isFile());
    QCOMPARE(QFileInfo(destination).size(), 7);
    QVERIFY(progressCalls > 0);
    QCOMPARE(lastReceived, 7);
    QVERIFY(ownerOnly(QFileInfo(destination).permissions(), false));
}

void MachinesTest::httpDownloaderErrors()
{
    const QString root = m_root + QStringLiteral("/http-downloader-errors");
    QDir(root).removeRecursively();
    QVERIFY(QDir().mkpath(root));

    m_server.setResponse(QStringLiteral("/downloader-ok.qcow2"), 200, QByteArrayLiteral("payload"));
    const QString url = m_server.url(QStringLiteral("/downloader-ok.qcow2"));

    HttpDownloader downloader;
    PodStack::Error error;
    QVERIFY(!downloader.download(QString(), root + QStringLiteral("/a"), {}, {}, &error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_DOWNLOAD_FAILED"));
    error = {};
    QVERIFY(!downloader.download(url, QString(), {}, {}, &error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_DOWNLOAD_FAILED"));

    error = {};
    QVERIFY(!downloader.download(
        url, root + QStringLiteral("/cancelled"), {}, [] { return true; }, &error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_CANCELLED"));

    const QString parentFile = root + QStringLiteral("/not-a-dir");
    QVERIFY(writeTestFile(parentFile, QByteArrayLiteral("x")));
    error = {};
    QVERIFY(!downloader.download(url, parentFile + QStringLiteral("/file"), {}, {}, &error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_DOWNLOAD_FAILED"));

    const QString partDirectory = root + QStringLiteral("/part-dir.part");
    QVERIFY(QDir().mkpath(partDirectory));
    error = {};
    QVERIFY(!downloader.download(url, root + QStringLiteral("/part-dir"), {}, {}, &error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_DOWNLOAD_FAILED"));

    // Rename do .part para um destino que é diretório falha.
    error = {};
    QVERIFY(!downloader.download(url, root, {}, {}, &error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_DOWNLOAD_FAILED"));
    QVERIFY(!QFileInfo::exists(root + QStringLiteral(".part")));

    error = {};
    QVERIFY(!downloader.download(QStringLiteral("http://127.0.0.1:1/nope"),
                                 root + QStringLiteral("/offline"), {}, {}, &error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_DOWNLOAD_FAILED"));
}

void MachinesTest::cloudImageCacheErrors()
{
    const QString root = m_root + QStringLiteral("/cache-errors");
    QDir(root).removeRecursively();
    QVERIFY(QDir().mkpath(root));

    m_server.setResponse(QStringLiteral("/cache-errors.qcow2"), 200, QByteArrayLiteral("image"));
    const DistroImage image =
        testImage(QStringLiteral("/cache-errors.qcow2"), QByteArrayLiteral("image"));

    const QString cacheFile = root + QStringLiteral("/cache-file");
    QVERIFY(writeTestFile(cacheFile, QByteArrayLiteral("x")));
    CloudImageCache invalid(cacheFile);
    QString path;
    PodStack::Error error;
    QVERIFY(!invalid.ensure(image, {}, {}, &path, &error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_DOWNLOAD_FAILED"));

    const QString cacheDir = root + QStringLiteral("/cache");
    CloudImageCache cache(cacheDir);
    QVERIFY(!cache.isCached(image));
    QCOMPARE(cache.sizeBytes(image), 0);
    DistroImage special = image;
    special.distro = QStringLiteral("ubuntu/24.04");
    QVERIFY(cache.cachedPath(special).contains(QStringLiteral("ubuntu_24.04")));

    // Destino já ocupado por um diretório não pode ser publicado.
    QDir().mkpath(cacheDir);
    QDir().mkpath(cache.cachedPath(image));
    error = {};
    QVERIFY(!cache.ensure(image, {}, {}, &path, &error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_DOWNLOAD_FAILED"));
    QDir(cache.cachedPath(image)).removeRecursively();
}

void MachinesTest::cloudImageCacheEdgeCases()
{
    const QString root = m_root + QStringLiteral("/cache-edge");
    QDir(root).removeRecursively();
    QVERIFY(QDir().mkpath(root));
    m_server.setResponse(QStringLiteral("/cache-edge.qcow2"), 200, QByteArrayLiteral("image"));
    const DistroImage image =
        testImage(QStringLiteral("/cache-edge.qcow2"), QByteArrayLiteral("image"));

    CloudImageCache cache(root + QStringLiteral("/cache"));
    QVERIFY(QDir().mkpath(cache.directory()));
    QString path;
    PodStack::Error error;

    QVERIFY(writeTestFile(cache.cachedPath(image), QByteArrayLiteral("image")));
    QVERIFY(!cache.ensure(image, {}, [] { return true; }, &path, &error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_CANCELLED"));

    QVERIFY(QDir().mkpath(cache.sidecarPath(image)));
    error = {};
    QVERIFY(!cache.ensure(image, {}, {}, &path, &error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_DOWNLOAD_FAILED"));
    QDir(cache.sidecarPath(image)).removeRecursively();

    QVERIFY(writeTestFile(cache.sidecarPath(image), QByteArrayLiteral("not-json")));
    error = {};
    QVERIFY2(cache.ensure(image, {}, {}, &path, &error), qPrintable(error.message));
    QVERIFY(cache.isCached(image));

    QFile::remove(cache.cachedPath(image));
    QFile::remove(cache.sidecarPath(image));
    int progressCalls = 0;
    error = {};
    QVERIFY2(cache.ensure(
                 image, [&progressCalls](qint64, qint64) { ++progressCalls; }, {}, &path, &error),
             qPrintable(error.message));
    QVERIFY(progressCalls > 0);

    QJsonObject sidecar;
    sidecar.insert(QStringLiteral("checksum"), QStringLiteral("00"));
    sidecar.insert(QStringLiteral("algorithm"),
                   PodStack::DistroRegistry::algorithmName(image.algorithm));
    sidecar.insert(QStringLiteral("size"), 5);
    QVERIFY(writeTestFile(cache.sidecarPath(image),
                          QJsonDocument(sidecar).toJson(QJsonDocument::Compact)));
    QVERIFY(!cache.isCached(image));
    error = {};
    QVERIFY2(cache.ensure(image, {}, {}, &path, &error), qPrintable(error.message));
    QVERIFY(cache.isCached(image));

    sidecar.insert(QStringLiteral("algorithm"), QStringLiteral("sha1"));
    QVERIFY(writeTestFile(cache.sidecarPath(image),
                          QJsonDocument(sidecar).toJson(QJsonDocument::Compact)));
    QVERIFY(!cache.isCached(image));

    sidecar.insert(QStringLiteral("algorithm"),
                   PodStack::DistroRegistry::algorithmName(image.algorithm));
    sidecar.insert(QStringLiteral("size"), 999);
    QVERIFY(writeTestFile(cache.sidecarPath(image),
                          QJsonDocument(sidecar).toJson(QJsonDocument::Compact)));
    QVERIFY(!cache.isCached(image));
}

void MachinesTest::seedIsoEntries()
{
    const QString directory = m_root + QStringLiteral("/seed-entries");
    QDir(directory).removeRecursively();
    QVERIFY(QDir().mkpath(directory));
    const QString seedPath = directory + QStringLiteral("/seed.iso");

    const CloudInitOptions options =
        seedOptions(QStringLiteral("machine-iso"), QStringLiteral("podstack-iso"));
    PodStack::Error error;
    QVERIFY2(CloudInit::writeSeedIso(seedPath, options, &error), qPrintable(error.message));
    QVERIFY(error.isEmpty());
    QVERIFY(QFileInfo(seedPath).isFile());
    const QFileDevice::Permissions seedPermissions = QFileInfo(seedPath).permissions();
    QVERIFY(ownerOnly(seedPermissions, false));
    QVERIFY(!seedPermissions.testFlag(QFileDevice::ExeOwner));
    QVERIFY(!QFileInfo::exists(seedPath + QStringLiteral(".tmp")));

    QCOMPARE(isoEntryNames(seedPath),
             QStringList({QStringLiteral("meta-data"), QStringLiteral("network-config"),
                          QStringLiteral("user-data")}));

    QByteArray metadata;
    QVERIFY(readIsoEntry(seedPath, QStringLiteral("meta-data"), &metadata));
    QCOMPARE(QString::fromUtf8(metadata),
             QStringLiteral("instance-id: 'machine-iso'\n"
                            "local-hostname: 'podstack-iso'\n"));

    QByteArray userData;
    QVERIFY(readIsoEntry(seedPath, QStringLiteral("user-data"), &userData));
    QVERIFY(userData.contains("#cloud-config\n"));
    QVERIFY(userData.contains("name: 'podstack'\n"));

    QByteArray networkConfig;
    QVERIFY(readIsoEntry(seedPath, QStringLiteral("network-config"), &networkConfig));
    QVERIFY(networkConfig.contains("version: 2\n"));

    const QByteArray label = volumeIdentifier(seedPath);
    QVERIFY(!label.isEmpty());
    QCOMPARE(QString::fromLatin1(label.trimmed()).toUpper(), QStringLiteral("CIDATA"));
}

void MachinesTest::seedYamlDefaults()
{
    const QString directory = m_root + QStringLiteral("/seed-defaults");
    QDir(directory).removeRecursively();
    QVERIFY(QDir().mkpath(directory));
    const QString seedPath = directory + QStringLiteral("/seed.iso");

    const CloudInitOptions options =
        seedOptions(QStringLiteral("machine-default"), QStringLiteral("podstack-default"));
    PodStack::Error error;
    QVERIFY2(CloudInit::writeSeedIso(seedPath, options, &error), qPrintable(error.message));

    QByteArray metadata;
    QByteArray userData;
    QByteArray networkConfig;
    QVERIFY(readIsoEntry(seedPath, QStringLiteral("meta-data"), &metadata));
    QVERIFY(readIsoEntry(seedPath, QStringLiteral("user-data"), &userData));
    QVERIFY(readIsoEntry(seedPath, QStringLiteral("network-config"), &networkConfig));

    const QString yaml = QString::fromUtf8(userData);
    QVERIFY(yaml.startsWith(QStringLiteral("#cloud-config\n")));
    QVERIFY(yaml.contains(QStringLiteral("  - name: 'podstack'\n")));
    QVERIFY(yaml.contains(QStringLiteral("    shell: /bin/bash\n")));
    QVERIFY(yaml.contains(QStringLiteral("    sudo: 'ALL=(ALL) NOPASSWD:ALL'\n")));
    QVERIFY(yaml.contains(QStringLiteral("    lock_passwd: true\n")));
    QVERIFY(yaml.contains(QStringLiteral("    ssh_authorized_keys:\n")));
    QVERIFY(yaml.contains(QStringLiteral("      - 'ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAI"
                                         "PODSTACK podstack'\n")));
    QVERIFY(yaml.contains(QStringLiteral("ssh_pwauth: false\n")));
    QVERIFY(yaml.contains(QStringLiteral("package_update: true\n")));
    QVERIFY(yaml.contains(QStringLiteral("packages:\n  - xauth\n")));
    QVERIFY(yaml.contains(QStringLiteral("10-podstack-x11.conf\n")));
    QVERIFY(yaml.contains(QStringLiteral("      X11Forwarding yes\n")));
    QVERIFY(yaml.contains(
        QStringLiteral("apt-get install -y x11-apps xdg-utils pulseaudio alsa-utils")));
    QVERIFY(yaml.contains(QStringLiteral("if ! command -v xauth")));
    QVERIFY(yaml.contains(QStringLiteral("apt-get install -y xauth")));
    QVERIFY(yaml.contains(QStringLiteral("dnf install -y xauth xorg-x11-apps")));
    QVERIFY(yaml.contains(QStringLiteral("linux-image-$arch")));
    QVERIFY(yaml.contains(QStringLiteral("generic_ok=1")));
    QVERIFY(yaml.contains(QStringLiteral("update-initramfs -u -k all")));
    QVERIFY(yaml.contains(QStringLiteral("if [ \"$generic_ok\" = \"1\" ]")));
    QVERIFY(yaml.contains(QStringLiteral("usermod -aG audio")));
    QVERIFY(yaml.contains(QStringLiteral("loginctl enable-linger")));
    QVERIFY(yaml.contains(QStringLiteral("PermitUserEnvironment yes")));
    QVERIFY(yaml.contains(QStringLiteral(".ssh/environment")));
    QVERIFY(yaml.contains(QStringLiteral("GRUB_DEFAULT")));
    QVERIFY(!yaml.contains(QStringLiteral("apt-get purge -y $cloud_kernels")));
    QVERIFY(yaml.contains(QStringLiteral("dnf install -y kernel-modules kernel-modules-extra")));
    QVERIFY(!yaml.contains(QStringLiteral("kernel-modules-extra-$(uname -r)")));
    QVERIFY(yaml.contains(QStringLiteral("pulseaudio --start")));
    QVERIFY(yaml.contains(QStringLiteral("/usr/local/sbin/podstack-guest-setup")));
    QVERIFY(yaml.contains(QStringLiteral("need_reboot=1")));
    QVERIFY(!yaml.contains(QStringLiteral("    passwd:")));

    QCOMPARE(QString::fromUtf8(networkConfig),
             QStringLiteral("version: 2\n"
                            "ethernets:\n"
                            "  eth0:\n"
                            "    match:\n"
                            "      name: 'en*'\n"
                            "    dhcp4: true\n"));
}

void MachinesTest::seedPasswordHashed()
{
    const QString directory = m_root + QStringLiteral("/seed-password");
    QDir(directory).removeRecursively();
    QVERIFY(QDir().mkpath(directory));
    const QString seedPath = directory + QStringLiteral("/seed.iso");

    CloudInitOptions options =
        seedOptions(QStringLiteral("machine-password"), QStringLiteral("podstack-password"));
    options.password = QStringLiteral("Senha-Muito-Secreta-42");
    PodStack::Error error;
    QVERIFY2(CloudInit::writeSeedIso(seedPath, options, &error), qPrintable(error.message));

    QByteArray userData;
    QVERIFY(readIsoEntry(seedPath, QStringLiteral("user-data"), &userData));
    const QString yaml = QString::fromUtf8(userData);
    QVERIFY(yaml.contains(QStringLiteral("    lock_passwd: false\n")));
    QVERIFY(yaml.contains(QStringLiteral("    passwd: '$6$")));
    QVERIFY(!yaml.contains(options.password));

    const QStringList entries = {
        QStringLiteral("meta-data"),
        QStringLiteral("user-data"),
        QStringLiteral("network-config"),
    };
    for (const QString &entry : entries) {
        QByteArray content;
        QVERIFY(readIsoEntry(seedPath, entry, &content));
        QVERIFY2(!content.contains(options.password.toUtf8()),
                 qPrintable(QStringLiteral("senha em claro em %1").arg(entry)));
    }
}

void MachinesTest::seedVerbatimUserData()
{
    const QString directory = m_root + QStringLiteral("/seed-verbatim");
    QDir(directory).removeRecursively();
    QVERIFY(QDir().mkpath(directory));
    const QString seedPath = directory + QStringLiteral("/seed.iso");

    CloudInitOptions options =
        seedOptions(QStringLiteral("machine-verbatim"), QStringLiteral("podstack-verbatim"));
    options.userData =
        QStringLiteral("#cloud-config\nruncmd:\n  - echo podstack-verbatim\n");
    PodStack::Error error;
    QVERIFY2(CloudInit::writeSeedIso(seedPath, options, &error), qPrintable(error.message));

    QByteArray userData;
    QVERIFY(readIsoEntry(seedPath, QStringLiteral("user-data"), &userData));
    QCOMPARE(QString::fromUtf8(userData), options.userData);
}

void MachinesTest::seedEnvironment()
{
    const QString directory = m_root + QStringLiteral("/seed-environment");
    QDir(directory).removeRecursively();
    QVERIFY(QDir().mkpath(directory));
    const QString seedPath = directory + QStringLiteral("/seed.iso");

    CloudInitOptions options =
        seedOptions(QStringLiteral("machine-environment"), QStringLiteral("podstack-env"));
    options.environment.insert(QStringLiteral("HTTP_PROXY"),
                               QStringLiteral("http://127.0.0.1:3128"));
    options.environment.insert(QStringLiteral("NO_PROXY"),
                               QStringLiteral("localhost,127.0.0.1"));
    PodStack::Error error;
    QVERIFY2(CloudInit::writeSeedIso(seedPath, options, &error), qPrintable(error.message));

    QByteArray userData;
    QVERIFY(readIsoEntry(seedPath, QStringLiteral("user-data"), &userData));
    const QString yaml = QString::fromUtf8(userData);
    QVERIFY(yaml.contains(QStringLiteral("write_files:\n")));
    QVERIFY(yaml.contains(QStringLiteral("  - path: /etc/environment\n")));
    QVERIFY(yaml.contains(QStringLiteral("    append: true\n")));
    QVERIFY(yaml.contains(QStringLiteral("    content: |\n")));
    QVERIFY(yaml.contains(QStringLiteral("      HTTP_PROXY=http://127.0.0.1:3128\n")));
    QVERIFY(yaml.contains(QStringLiteral("      NO_PROXY=localhost,127.0.0.1\n")));
    QVERIFY(yaml.contains(QStringLiteral("10-podstack-x11.conf\n")));

    options.userData = QStringLiteral("#cloud-config\nruncmd:\n  - echo verbatim\n");
    QVERIFY2(CloudInit::writeSeedIso(seedPath, options, &error), qPrintable(error.message));
    QVERIFY(readIsoEntry(seedPath, QStringLiteral("user-data"), &userData));
    QCOMPARE(QString::fromUtf8(userData), options.userData);
    QVERIFY(!userData.contains("write_files:"));
}

void MachinesTest::seedEscapesYaml()
{
    const QString directory = m_root + QStringLiteral("/seed-invalid");
    QDir(directory).removeRecursively();
    QVERIFY(QDir().mkpath(directory));
    const QString seedPath = directory + QStringLiteral("/seed.iso");

    const CloudInitOptions valid =
        seedOptions(QStringLiteral("machine-valid"), QStringLiteral("podstack-valid"));
    PodStack::Error error;

    CloudInitOptions options = valid;
    options.hostname = QStringLiteral("host name");
    QVERIFY(!CloudInit::writeSeedIso(seedPath, options, &error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_INVALID_PARAMS"));

    options = valid;
    options.hostname = QStringLiteral("-podstack");
    QVERIFY(!CloudInit::writeSeedIso(seedPath, options, &error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_INVALID_PARAMS"));

    options = valid;
    options.hostname = QStringLiteral("podstack_underscore");
    QVERIFY(!CloudInit::writeSeedIso(seedPath, options, &error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_INVALID_PARAMS"));

    options = valid;
    options.user = QStringLiteral("Bad User");
    QVERIFY(!CloudInit::writeSeedIso(seedPath, options, &error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_INVALID_PARAMS"));

    options = valid;
    options.user = QStringLiteral("9user");
    QVERIFY(!CloudInit::writeSeedIso(seedPath, options, &error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_INVALID_PARAMS"));

    options = valid;
    options.machineId.clear();
    QVERIFY(!CloudInit::writeSeedIso(seedPath, options, &error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_INVALID_PARAMS"));
    QVERIFY(!QFileInfo::exists(seedPath));

    options = valid;
    options.sshAuthorizedKeys = {QStringLiteral("ssh-ed25519 AAA'BBB comment")};
    QVERIFY2(CloudInit::writeSeedIso(seedPath, options, &error), qPrintable(error.message));
    QVERIFY(error.isEmpty());

    QByteArray userData;
    QVERIFY(readIsoEntry(seedPath, QStringLiteral("user-data"), &userData));
    QVERIFY2(userData.contains(
                 QByteArrayLiteral("      - 'ssh-ed25519 AAA''BBB comment'\n")),
             userData.constData());
}

void MachinesTest::seedValidationErrors()
{
    const QString root = m_root + QStringLiteral("/seed-validation-errors");
    QDir(root).removeRecursively();
    QVERIFY(QDir().mkpath(root));

    const CloudInitOptions valid =
        seedOptions(QStringLiteral("machine-validation"), QStringLiteral("podstack-validation"));
    PodStack::Error error;
    CloudInitOptions options = valid;

    QVERIFY(!CloudInit::writeSeedIso(QString(), options, &error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_INVALID_PARAMS"));

    options = valid;
    options.environment.insert(QStringLiteral("1BAD"), QStringLiteral("x"));
    QVERIFY(!CloudInit::writeSeedIso(root + QStringLiteral("/env.iso"), options, &error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_INVALID_PARAMS"));

    options = valid;
    options.environment.insert(QStringLiteral("GOOD"), QStringLiteral("bad\nvalue"));
    QVERIFY(!CloudInit::writeSeedIso(root + QStringLiteral("/env2.iso"), options, &error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_INVALID_PARAMS"));

    options = valid;
    options.sshAuthorizedKeys = {QString()};
    QVERIFY(!CloudInit::writeSeedIso(root + QStringLiteral("/key.iso"), options, &error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_INVALID_PARAMS"));

    options = valid;
    QVERIFY(!CloudInit::writeSeedIso(root, options, &error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_IO_ERROR"));

    const QString parentFile = root + QStringLiteral("/not-a-dir");
    QVERIFY(writeTestFile(parentFile, QByteArrayLiteral("x")));
    error = {};
    QVERIFY(!CloudInit::writeSeedIso(parentFile + QStringLiteral("/seed.iso"), options, &error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_IO_ERROR"));

    const QString tempDir = root + QStringLiteral("/seed-tmp.iso.tmp");
    QVERIFY(QDir().mkpath(tempDir));
    error = {};
    QVERIFY(!CloudInit::writeSeedIso(root + QStringLiteral("/seed-tmp.iso"), options, &error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_IO_ERROR"));
}

void MachinesTest::qemuBinary()
{
    PodStack::Error error;

    QCOMPARE(QemuCommand::binary(QStringLiteral("amd64"), &error),
             QStringLiteral("qemu-system-x86_64"));
    QVERIFY(error.isEmpty());

    QCOMPARE(QemuCommand::binary(QStringLiteral("arm64"), &error),
             QStringLiteral("qemu-system-aarch64"));
    QVERIFY(error.isEmpty());

    QVERIFY(QemuCommand::binary(QStringLiteral("riscv64"), &error).isEmpty());
    QCOMPARE(error.code, QStringLiteral("PODSTACK_INVALID_PARAMS"));

    QVERIFY(QemuCommand::binary(QString(), &error).isEmpty());
    QCOMPARE(error.code, QStringLiteral("PODSTACK_INVALID_PARAMS"));
}

void MachinesTest::qemuCommandKvm()
{
    const QemuSpec spec = qemuSpec(m_root);
    PodStack::Error error;
    const QStringList args = QemuCommand::arguments(spec, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));

    QCOMPARE(valueAfter(args, QStringLiteral("-M")), QStringLiteral("q35"));
    QCOMPARE(valueAfter(args, QStringLiteral("-accel")), QStringLiteral("kvm"));
    QCOMPARE(valueAfter(args, QStringLiteral("-cpu")), QStringLiteral("host"));
    QCOMPARE(valueAfter(args, QStringLiteral("-smp")), QStringLiteral("2"));
    QCOMPARE(valueAfter(args, QStringLiteral("-m")), QStringLiteral("4096"));
    QCOMPARE(valueAfter(args, QStringLiteral("-name")),
             QStringLiteral("podstack:%1").arg(spec.machineId));
    QCOMPARE(valueAfter(args, QStringLiteral("-uuid")), spec.machineId);

    QCOMPARE(args.count(QStringLiteral("-drive")), 2);
    QVERIFY(args.contains(QStringLiteral("file=%1,if=virtio,format=qcow2,cache=writeback,"
                                         "discard=unmap")
                              .arg(spec.diskPath)));
    QVERIFY(args.contains(
        QStringLiteral("file=%1,format=raw,if=virtio,readonly=on").arg(spec.seedPath)));

    QCOMPARE(valueAfter(args, QStringLiteral("-netdev")),
             QStringLiteral("user,id=net0,hostfwd=tcp:127.0.0.1:32222-:22"));
    QCOMPARE(valueAfter(args, QStringLiteral("-device")),
             QStringLiteral("virtio-net-pci,netdev=net0"));
    QCOMPARE(valueAfter(args, QStringLiteral("-chardev")),
             QStringLiteral("socket,id=qmp,path=%1,server=on,wait=off")
                 .arg(spec.qmpSocketPath));
    QCOMPARE(valueAfter(args, QStringLiteral("-mon")),
             QStringLiteral("chardev=qmp,mode=control"));
    QCOMPARE(valueAfter(args, QStringLiteral("-monitor")), QStringLiteral("none"));
    QCOMPARE(valueAfter(args, QStringLiteral("-display")), QStringLiteral("none"));
    QCOMPARE(valueAfter(args, QStringLiteral("-serial")),
             QStringLiteral("file:%1").arg(spec.consoleLogPath));
    QVERIFY(args.contains(QStringLiteral("-daemonize")));
    QCOMPARE(valueAfter(args, QStringLiteral("-pidfile")), spec.pidFile);
}

void MachinesTest::qemuCommandTcg()
{
    QemuSpec spec = qemuSpec(m_root);
    spec.kvm = false;

    PodStack::Error error;
    const QStringList args = QemuCommand::arguments(spec, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));

    QCOMPARE(valueAfter(args, QStringLiteral("-accel")), QStringLiteral("tcg"));
    QCOMPARE(valueAfter(args, QStringLiteral("-cpu")), QStringLiteral("max"));
    QCOMPARE(valueAfter(args, QStringLiteral("-M")), QStringLiteral("q35"));
}

void MachinesTest::qemuCommandArm64()
{
    QemuSpec spec = qemuSpec(m_root);
    spec.arch = QStringLiteral("arm64");

    PodStack::Error error;
    QCOMPARE(QemuCommand::binary(spec.arch, &error),
             QStringLiteral("qemu-system-aarch64"));
    QVERIFY(error.isEmpty());

    const QStringList args = QemuCommand::arguments(spec, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QCOMPARE(valueAfter(args, QStringLiteral("-M")), QStringLiteral("virt"));
    QCOMPARE(valueAfter(args, QStringLiteral("-cpu")), QStringLiteral("host"));
}

void MachinesTest::qemuCommandUefi()
{
    QemuSpec spec = qemuSpec(m_root);
    spec.ovmfCodePath = m_root + QStringLiteral("/qemu/OVMF_CODE.fd");
    spec.ovmfVarsPath = m_root + QStringLiteral("/qemu/OVMF_VARS.fd");

    PodStack::Error error;
    const QStringList args = QemuCommand::arguments(spec, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QCOMPARE(args.count(QStringLiteral("-drive")), 4);
    QVERIFY(args.contains(QStringLiteral("if=pflash,format=raw,readonly=on,file=%1")
                              .arg(spec.ovmfCodePath)));
    QVERIFY(args.contains(
        QStringLiteral("if=pflash,format=raw,file=%1").arg(spec.ovmfVarsPath)));

    QemuSpec onlyCode = qemuSpec(m_root);
    onlyCode.ovmfCodePath = m_root + QStringLiteral("/qemu/OVMF_CODE.fd");
    QVERIFY(QemuCommand::arguments(onlyCode, &error).isEmpty());
    QCOMPARE(error.code, QStringLiteral("PODSTACK_INVALID_PARAMS"));

    QemuSpec onlyVars = qemuSpec(m_root);
    onlyVars.ovmfVarsPath = m_root + QStringLiteral("/qemu/OVMF_VARS.fd");
    QVERIFY(QemuCommand::arguments(onlyVars, &error).isEmpty());
    QCOMPARE(error.code, QStringLiteral("PODSTACK_INVALID_PARAMS"));
}

void MachinesTest::qemuCommandIsolated()
{
    QemuSpec spec = qemuSpec(m_root);
    spec.isolateNetwork = true;

    PodStack::Error error;
    QStringList args = QemuCommand::arguments(spec, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QCOMPARE(valueAfter(args, QStringLiteral("-netdev")),
             QStringLiteral("user,id=net0,hostfwd=tcp:127.0.0.1:32222-:22,restrict=on"));

    spec.isolateNetwork = false;
    args = QemuCommand::arguments(spec, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QCOMPARE(valueAfter(args, QStringLiteral("-netdev")),
             QStringLiteral("user,id=net0,hostfwd=tcp:127.0.0.1:32222-:22"));
}

void MachinesTest::qemuCommandMounts()
{
    QemuSpec spec = qemuSpec(m_root);
    const QemuMount first{QStringLiteral("host0"),
                          m_root + QStringLiteral("/qemu/virtiofs-host0.sock"), false};
    const QemuMount second{QStringLiteral("dados"),
                           m_root + QStringLiteral("/qemu/virtiofs-dados.sock"), true};
    spec.mounts = {first, second};

    PodStack::Error error;
    QStringList args = QemuCommand::arguments(spec, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QVERIFY(args.contains(QStringLiteral("socket,id=vfs0,path=%1").arg(first.socketPath)));
    QVERIFY(args.contains(QStringLiteral("vhost-user-fs-pci,chardev=vfs0,tag=host0")));
    QVERIFY(args.contains(QStringLiteral("socket,id=vfs1,path=%1").arg(second.socketPath)));
    QVERIFY(args.contains(QStringLiteral("vhost-user-fs-pci,chardev=vfs1,tag=dados")));
    QCOMPARE(args.count(QStringLiteral("-chardev")), 3);
    QVERIFY(!args.join(QLatin1Char(' ')).contains(QStringLiteral("vfs2")));

    spec.mounts.clear();
    args = QemuCommand::arguments(spec, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QVERIFY(!args.join(QLatin1Char(' ')).contains(QStringLiteral("vhost-user-fs")));

    const auto rejects = [](QemuSpec candidate) {
        PodStack::Error candidateError;
        return QemuCommand::arguments(candidate, &candidateError).isEmpty()
               && candidateError.code == QStringLiteral("PODSTACK_INVALID_PARAMS");
    };

    spec = qemuSpec(m_root);
    spec.mounts = {QemuMount{QStringLiteral("Host0"), first.socketPath, false}};
    QVERIFY(rejects(spec));

    spec = qemuSpec(m_root);
    spec.mounts = {QemuMount{QString(), first.socketPath, false}};
    QVERIFY(rejects(spec));

    spec = qemuSpec(m_root);
    spec.mounts = {first, QemuMount{QStringLiteral("host0"), second.socketPath, true}};
    QVERIFY(rejects(spec));

    spec = qemuSpec(m_root);
    spec.mounts = {QemuMount{QStringLiteral("host0"), QStringLiteral("vfs.sock"), false}};
    QVERIFY(rejects(spec));

    spec = qemuSpec(m_root);
    for (int i = 0; i < 9; ++i) {
        const QString tag = QStringLiteral("host%1").arg(i);
        spec.mounts.append(
            QemuMount{tag, m_root + QStringLiteral("/qemu/virtiofs-%1.sock").arg(tag), false});
    }
    QVERIFY(rejects(spec));
}

void MachinesTest::qemuCommandPorts()
{
    QemuSpec spec = qemuSpec(m_root);
    spec.ports.append(QemuPort{QStringLiteral("tcp"), 8080, 80, QStringLiteral("127.0.0.1")});
    spec.ports.append(QemuPort{QStringLiteral("udp"), 5353, 53, QStringLiteral("0.0.0.0")});

    PodStack::Error error;
    QStringList args = QemuCommand::arguments(spec, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    const QString netdev = valueAfter(args, QStringLiteral("-netdev"));
    QCOMPARE(netdev,
             QStringLiteral("user,id=net0,hostfwd=tcp:127.0.0.1:32222-:22,"
                            "hostfwd=tcp:127.0.0.1:8080-:80,"
                            "hostfwd=udp:0.0.0.0:5353-:53"));
    QCOMPARE(netdev.count(QStringLiteral("hostfwd=tcp:127.0.0.1:32222-:22")), 1);
    QVERIFY(!netdev.contains(QStringLiteral("restrict=on")));

    spec.isolateNetwork = true;
    args = QemuCommand::arguments(spec, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    const QString isolated = valueAfter(args, QStringLiteral("-netdev"));
    QVERIFY(isolated.endsWith(QStringLiteral(",restrict=on")));
    QCOMPARE(isolated.count(QStringLiteral("restrict=on")), 1);
    QCOMPARE(isolated.count(QStringLiteral("hostfwd=")), 3);

    const auto rejects = [](QemuSpec candidate) {
        PodStack::Error candidateError;
        return QemuCommand::arguments(candidate, &candidateError).isEmpty()
               && candidateError.code == QStringLiteral("PODSTACK_INVALID_PARAMS");
    };

    spec = qemuSpec(m_root);
    spec.ports = {QemuPort{QStringLiteral("sctp"), 8080, 80, QStringLiteral("127.0.0.1")}};
    QVERIFY(rejects(spec));

    spec = qemuSpec(m_root);
    spec.ports = {QemuPort{QStringLiteral("tcp"), 0, 80, QStringLiteral("127.0.0.1")}};
    QVERIFY(rejects(spec));

    spec = qemuSpec(m_root);
    spec.ports = {QemuPort{QStringLiteral("tcp"), 8080, 70000, QStringLiteral("127.0.0.1")}};
    QVERIFY(rejects(spec));

    spec = qemuSpec(m_root);
    spec.ports = {QemuPort{QStringLiteral("tcp"), 8080, 80, QStringLiteral("localhost")}};
    QVERIFY(rejects(spec));

    spec = qemuSpec(m_root);
    spec.ports = {QemuPort{QStringLiteral("tcp"), 8080, 80, QStringLiteral("127.0.0.1")},
                  QemuPort{QStringLiteral("tcp"), 8080, 81, QStringLiteral("127.0.0.1")}};
    QVERIFY(rejects(spec));

    spec = qemuSpec(m_root);
    spec.ports.clear();
    QVERIFY(rejects(spec));
}

void MachinesTest::qemuCommandSerialPorts()
{
    QemuSpec spec = qemuSpec(m_root);
    spec.serialPorts = {QStringLiteral("/dev/ttyUSB0"), QStringLiteral("/dev/ttyACM0")};

    PodStack::Error error;
    const QStringList args = QemuCommand::arguments(spec, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));

    QStringList serialValues;
    for (int index = 0; index + 1 < args.size(); ++index) {
        if (args.at(index) == QStringLiteral("-serial")) {
            serialValues.append(args.at(index + 1));
        }
    }
    QCOMPARE(serialValues.size(), 3);
    QCOMPARE(serialValues.at(0), QStringLiteral("file:%1").arg(spec.consoleLogPath));
    QCOMPARE(serialValues.at(1), QStringLiteral("/dev/ttyUSB0"));
    QCOMPARE(serialValues.at(2), QStringLiteral("/dev/ttyACM0"));
}

void MachinesTest::qemuCommandUsbDevices()
{
    QemuSpec spec = qemuSpec(m_root);
    spec.usbDevices = {QemuUsbDevice{1, 4}, QemuUsbDevice{2, 3}};

    PodStack::Error error;
    const QStringList args = QemuCommand::arguments(spec, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));

    QCOMPARE(args.count(QStringLiteral("qemu-xhci,id=usbhost")), 1);
    QCOMPARE(args.count(QStringLiteral("usb-host,hostbus=1,hostaddr=4")), 1);
    QCOMPARE(args.count(QStringLiteral("usb-host,hostbus=2,hostaddr=3")), 1);
}

void MachinesTest::qemuCommandAudio()
{
    QemuSpec spec = qemuSpec(m_root);
    spec.audioEnabled = true;

    PodStack::Error error;
    const QStringList args = QemuCommand::arguments(spec, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QVERIFY(args.contains(QStringLiteral("pipewire,id=audio0")));
    QVERIFY(args.contains(QStringLiteral("intel-hda")));
    QVERIFY(args.contains(QStringLiteral("hda-duplex,audiodev=audio0")));

    spec.audioEnabled = false;
    const QStringList without = QemuCommand::arguments(spec, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QVERIFY(!without.contains(QStringLiteral("pipewire,id=audio0")));
}

void MachinesTest::qemuCommandInvalid()
{
    const auto rejects = [](const QemuSpec &candidate) {
        PodStack::Error error;
        return QemuCommand::arguments(candidate, &error).isEmpty()
               && error.code == QStringLiteral("PODSTACK_INVALID_PARAMS");
    };

    QemuSpec spec = qemuSpec(m_root);
    spec.machineId.clear();
    QVERIFY(rejects(spec));

    spec = qemuSpec(m_root);
    spec.name.clear();
    QVERIFY(rejects(spec));

    spec = qemuSpec(m_root);
    spec.arch = QStringLiteral("x86_64");
    QVERIFY(rejects(spec));

    spec = qemuSpec(m_root);
    spec.arch.clear();
    QVERIFY(rejects(spec));

    spec = qemuSpec(m_root);
    spec.cpus = 0;
    QVERIFY(rejects(spec));

    spec = qemuSpec(m_root);
    spec.cpus = 257;
    QVERIFY(rejects(spec));

    spec = qemuSpec(m_root);
    spec.memoryBytes = 1024LL * 1024LL * 1024LL - 1;
    QVERIFY(rejects(spec));

    spec = qemuSpec(m_root);
    spec.diskPath = QStringLiteral("disk.qcow2");
    QVERIFY(rejects(spec));

    spec = qemuSpec(m_root);
    spec.diskPath.clear();
    QVERIFY(rejects(spec));

    spec = qemuSpec(m_root);
    spec.seedPath = QStringLiteral("seed.iso");
    QVERIFY(rejects(spec));

    spec = qemuSpec(m_root);
    spec.qmpSocketPath.clear();
    QVERIFY(rejects(spec));

    spec = qemuSpec(m_root);
    spec.consoleLogPath = QStringLiteral("console.log");
    QVERIFY(rejects(spec));

    spec = qemuSpec(m_root);
    spec.pidFile.clear();
    QVERIFY(rejects(spec));

    spec = qemuSpec(m_root);
    spec.sshPort = 0;
    QVERIFY(rejects(spec));

    spec = qemuSpec(m_root);
    spec.cpus = 256;
    spec.memoryBytes = 1024LL * 1024LL * 1024LL;
    PodStack::Error error;
    const QStringList boundary = QemuCommand::arguments(spec, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QCOMPARE(valueAfter(boundary, QStringLiteral("-smp")), QStringLiteral("256"));
    QCOMPARE(valueAfter(boundary, QStringLiteral("-m")), QStringLiteral("1024"));
}

void MachinesTest::qmpHandshake()
{
    MockQmpServer server;
    QVERIFY(server.isListening());

    QmpClient client(server.socketPath());
    PodStack::Error error;
    QVERIFY2(client.connect(2000, &error), qPrintable(error.message));
    QVERIFY(error.isEmpty());
    QVERIFY(client.isConnected());
    QCOMPARE(server.commandCount(QStringLiteral("qmp_capabilities")), 1);
    QCOMPARE(server.connectionCount(), 1);

    client.disconnect();
    QVERIFY(!client.isConnected());
}

void MachinesTest::qmpQueryStatus()
{
    MockQmpServer server;
    server.setStatus(QStringLiteral("running"));
    QVERIFY(server.isListening());

    QmpClient client(server.socketPath());
    PodStack::Error error;
    QVERIFY2(client.connect(2000, &error), qPrintable(error.message));

    QString status;
    QVERIFY2(client.queryStatus(&status, &error), qPrintable(error.message));
    QCOMPARE(status, QStringLiteral("running"));

    server.setStatus(QStringLiteral("paused"));
    QVERIFY2(client.queryStatus(&status, &error), qPrintable(error.message));
    QCOMPARE(status, QStringLiteral("paused"));
}

void MachinesTest::qmpPowerdown()
{
    MockQmpServer server;
    QVERIFY(server.isListening());

    QmpClient client(server.socketPath());
    PodStack::Error error;
    QVERIFY2(client.connect(2000, &error), qPrintable(error.message));

    QVERIFY2(client.systemPowerdown(&error), qPrintable(error.message));
    QVERIFY(error.isEmpty());
    QCOMPARE(server.commandCount(QStringLiteral("system_powerdown")), 1);
}

void MachinesTest::qmpErrorResponse()
{
    MockQmpServer server;
    server.setFailCommand(QStringLiteral("query-status"));
    QVERIFY(server.isListening());

    QmpClient client(server.socketPath());
    PodStack::Error error;
    QVERIFY2(client.connect(2000, &error), qPrintable(error.message));

    QString status;
    QVERIFY(!client.queryStatus(&status, &error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_INTERNAL"));
    QVERIFY2(error.detail.contains(QStringLiteral("GenericError")),
             qPrintable(error.detail));
    QVERIFY2(error.detail.contains(QStringLiteral("falha simulada")),
             qPrintable(error.detail));
    QVERIFY2(error.message.contains(QStringLiteral("query-status")),
             qPrintable(error.message));

    // A conexão continua utilizável após um erro QMP.
    QVERIFY2(client.systemPowerdown(&error), qPrintable(error.message));
    QVERIFY(error.isEmpty());
}

void MachinesTest::qmpInterleavedEvent()
{
    MockQmpServer server;
    server.setInterleavedEvent(QStringLiteral("query-status"));
    QVERIFY(server.isListening());

    QmpClient client(server.socketPath());
    PodStack::Error error;
    QVERIFY2(client.connect(2000, &error), qPrintable(error.message));

    QString status;
    QVERIFY2(client.queryStatus(&status, &error), qPrintable(error.message));
    QCOMPARE(status, QStringLiteral("running"));
    QCOMPARE(server.commandCount(QStringLiteral("query-status")), 1);
}

void MachinesTest::qmpTimeout()
{
    MockQmpServer server;
    server.setSilentCommand(QStringLiteral("query-status"));
    QVERIFY(server.isListening());

    QmpClient client(server.socketPath());
    PodStack::Error error;
    QVERIFY2(client.connect(2000, &error), qPrintable(error.message));
    client.setCommandTimeout(100);

    QElapsedTimer timer;
    timer.start();
    QString status;
    QVERIFY(!client.queryStatus(&status, &error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_TIMEOUT"));
    QVERIFY(timer.elapsed() < 1500);
}

void MachinesTest::qmpQuit()
{
    MockQmpServer server;
    QVERIFY(server.isListening());

    QmpClient client(server.socketPath());
    PodStack::Error error;
    QVERIFY2(client.connect(2000, &error), qPrintable(error.message));

    QVERIFY2(client.quit(&error), qPrintable(error.message));
    QVERIFY(error.isEmpty());
    QVERIFY(!client.isConnected());
    QCOMPARE(server.commandCount(QStringLiteral("quit")), 1);
}

void MachinesTest::qmpConnectMissing()
{
    const QString path = m_root + QStringLiteral("/qemu/missing.sock");
    QFile::remove(path);

    QmpClient client(path);
    PodStack::Error error;
    QVERIFY(!client.connect(200, &error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_IO_ERROR"));
    QVERIFY(!client.isConnected());
}

void MachinesTest::qmpInvalidStates()
{
    PodStack::Error error;
    QmpClient empty{QString{}};
    QVERIFY(!empty.connect(100, &error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_IO_ERROR"));
    QVERIFY(!empty.execute(QString(), {}, nullptr, &error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_INVALID_PARAMS"));

    QString status;
    QVERIFY(!empty.queryStatus(&status, &error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_IO_ERROR"));
    QVERIFY(!empty.systemPowerdown(&error));
    QVERIFY(!empty.quit(&error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_IO_ERROR"));
    empty.disconnect();

    MockQmpServer noGreeting;
    noGreeting.setNoGreeting(true);
    QVERIFY(noGreeting.isListening());
    QmpClient bad(noGreeting.socketPath());
    bad.setCommandTimeout(200);
    QVERIFY(!bad.connect(1000, &error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_TIMEOUT"));
    QVERIFY(!bad.isConnected());
    bad.disconnect();

    MockQmpServer emptyStatus;
    emptyStatus.setStatus(QString());
    QVERIFY(emptyStatus.isListening());
    QmpClient client(emptyStatus.socketPath());
    QVERIFY2(client.connect(1000, &error), qPrintable(error.message));
    QVERIFY(!client.queryStatus(&status, &error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_INTERNAL"));
    client.disconnect();
    QVERIFY(!client.isConnected());
}

void MachinesTest::qmpSocketCloses()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("qmp.sock"));
    QLocalServer server;
    QVERIFY2(server.listen(path), qPrintable(server.errorString()));

    QmpClient client(path);
    PodStack::Error error;
    bool connected = true;
    std::thread thread([&] { connected = client.connect(1500, &error); });

    QVERIFY(server.waitForNewConnection(3000));
    QLocalSocket *socket = server.nextPendingConnection();
    QVERIFY(socket != nullptr);
    socket->write("{\"QMP\":{}}\n");
    socket->flush();
    socket->waitForBytesWritten(1000);
    socket->disconnectFromServer();
    thread.join();

    QVERIFY(!connected);
    QVERIFY(!error.isEmpty());
}

void MachinesTest::idsUuidV7()
{
    static const QRegularExpression pattern(QStringLiteral(
        "^[0-9a-f]{8}-[0-9a-f]{4}-7[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$"));

    QSet<QString> seen;
    for (int index = 0; index < 500; ++index) {
        const QString id = PodStack::newUuidV7();
        QVERIFY2(pattern.match(id).hasMatch(), qPrintable(id));
        QVERIFY2(!seen.contains(id), qPrintable(id));
        seen.insert(id);
    }

    QTest::qWait(10);
    const QString early = PodStack::newUuidV7();
    QTest::qWait(10);
    const QString late = PodStack::newUuidV7();
    QVERIFY2(late > early, qPrintable(QStringLiteral("%1 <= %2").arg(late, early)));

    const qint64 timestamp = static_cast<qint64>(
        (early.left(8).toULongLong(nullptr, 16) << 16)
        | early.mid(9, 4).toULongLong(nullptr, 16));
    QVERIFY(qAbs(QDateTime::currentMSecsSinceEpoch() - timestamp) < 60000);
}

void MachinesTest::machineManagerCreate()
{
    const QString root = m_root + QStringLiteral("/manager-create");
    QDir(root).removeRecursively();
    QVERIFY(QDir().mkpath(root));

    QString ovmfCode;
    QString ovmfVars;
    QVERIFY(createFirmwareFiles(root, &ovmfCode, &ovmfVars));
    const QString qemuImg = createQemuImgScript(root);
    QVERIFY(!qemuImg.isEmpty());
    const QString qemuImgLog = root + QStringLiteral("/qemu-img.log");
    qputenv("PODSTACK_QEMU_IMG_LOG", qemuImgLog.toUtf8());

    const QByteArray payload(4096, 'i');
    m_server.setResponse(QStringLiteral("/manager-create.qcow2"), 200, payload);
    const DistroImage image = testImage(QStringLiteral("/manager-create.qcow2"), payload);

    auto runtime = std::make_unique<FakeQemuRuntime>();
    FakeQemuRuntime *fake = runtime.get();

    MachineManager::Options options;
    QVERIFY(configureSshOptions(&options, root));
    options.storePath = root + QStringLiteral("/podstack.db");
    options.machinesRoot = root + QStringLiteral("/machines");
    options.imageCacheDir = root + QStringLiteral("/images");
    options.qemuImgBinary = qemuImg;
    options.ovmfCodePath = ovmfCode;
    options.ovmfVarsPath = ovmfVars;
    options.sshPortStart = freePortHint();
    options.runtime = std::move(runtime);
    options.resolveDistro =
        [image](const QString &, const QString &, PodStack::Error *) { return image; };

    const QString machinesRoot = options.machinesRoot;
    const QString storePath = options.storePath;
    const QString imageCacheDir = options.imageCacheDir;
    const int portStart = options.sshPortStart;

    MachineManager manager(std::move(options));

    CreateMachineRequest request;
    request.name = QStringLiteral("dev-create");
    request.distro = QStringLiteral("test");
    request.arch = QStringLiteral("amd64");
    request.user = QStringLiteral("podstack");
    request.password = QStringLiteral("Senha-Muito-Secreta-42");
    request.cpus = 2;
    request.diskBytes = 1024LL * 1024LL * 1024LL;

    PodStack::Config config = PodStack::Config::load();
    config.set(QStringLiteral("network.proxy"),
               QStringLiteral("http://127.0.0.1:3128"));
    config.set(QStringLiteral("network.proxyExclude"),
               QJsonArray{QStringLiteral("localhost"), QStringLiteral(".internal")});
    QString configError;
    QVERIFY2(config.save(&configError), qPrintable(configError));

    QStringList stages;
    PodStack::Error error;
    const MachineMeta meta = manager.create(
        request,
        [&stages](const QString &stage, int) { stages.append(stage); },
        {}, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QVERIFY2(!meta.id.isEmpty(), qPrintable(error.detail));

    static const QRegularExpression uuidPattern(QStringLiteral(
        "^[0-9a-f]{8}-[0-9a-f]{4}-7[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$"));
    QVERIFY2(uuidPattern.match(meta.id).hasMatch(), qPrintable(meta.id));

    QCOMPARE(meta.name, QStringLiteral("dev-create"));
    QCOMPARE(meta.distro, QStringLiteral("test"));
    QCOMPARE(meta.distroVersion, QStringLiteral("1.0"));
    QCOMPARE(meta.arch, QStringLiteral("amd64"));
    QCOMPARE(meta.cpus, 2);
    QCOMPARE(meta.memoryBytes, request.memoryBytes);
    QCOMPARE(meta.diskBytes, request.diskBytes);
    QCOMPARE(meta.sshUser, QStringLiteral("podstack"));
    QVERIFY(meta.sshPort >= portStart);
    QVERIFY(meta.sshPort < portStart + 20);
    QVERIFY(!meta.sshKeyPath.isEmpty());
    QCOMPARE(meta.state, QStringLiteral("running"));
    QVERIFY(meta.error.isEmpty());
    QVERIFY(!meta.lastBootAt.isEmpty());
    QCOMPARE(meta.ports.size(), 1);
    QCOMPARE(meta.ports.at(0).hostPort, meta.sshPort);
    QCOMPARE(meta.ports.at(0).guestPort, 22);
    QCOMPARE(meta.ports.at(0).label, QStringLiteral("ssh"));

    const QString directory = machinesRoot + QLatin1Char('/') + meta.id;
    QCOMPARE(meta.sshKeyPath, directory + QStringLiteral("/id_ed25519"));
    QCOMPARE(meta.diskPath, directory + QStringLiteral("/disk.qcow2"));
    QCOMPARE(meta.seedPath, directory + QStringLiteral("/seed.iso"));
    QVERIFY(ownerOnly(QFileInfo(directory).permissions(), true));
    QVERIFY(ownerOnly(QFileInfo(machinesRoot).permissions(), true));
    QVERIFY(ownerOnly(QFileInfo(meta.diskPath).permissions(), false));
    QVERIFY(ownerOnly(QFileInfo(meta.seedPath).permissions(), false));
    QVERIFY(ownerOnly(QFileInfo(directory + QStringLiteral("/meta.json")).permissions(), false));

    QByteArray seedUserData;
    QVERIFY(readIsoEntry(meta.seedPath, QStringLiteral("user-data"), &seedUserData));
    const QString seedYaml = QString::fromUtf8(seedUserData);
    QVERIFY(seedYaml.contains(QStringLiteral("      HTTP_PROXY=http://127.0.0.1:3128\n")));
    QVERIFY(seedYaml.contains(QStringLiteral("      HTTPS_PROXY=http://127.0.0.1:3128\n")));
    QVERIFY(seedYaml.contains(QStringLiteral("      NO_PROXY=localhost,127.0.0.1,::1,"
                                             "*.localhost,.internal")));
    QVERIFY(seedYaml.contains(QStringLiteral("ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAIFAKEPODSTACKKEY")));
    QVERIFY(ownerOnly(QFileInfo(meta.sshKeyPath).permissions(), false));
    const QFileInfo publicKeyInfo(meta.sshKeyPath + QStringLiteral(".pub"));
    QVERIFY(publicKeyInfo.isFile());
    QCOMPARE(publicKeyInfo.permissions(),
             QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ReadUser
                 | QFileDevice::WriteUser | QFileDevice::ReadGroup | QFileDevice::ReadOther);

    config.set(QStringLiteral("network.proxy"), QStringLiteral("auto"));
    config.set(QStringLiteral("network.proxyExclude"), QJsonArray{});
    QVERIFY2(config.save(&configError), qPrintable(configError));

    const QString ovmfVarsTarget = directory + QStringLiteral("/OVMF_VARS.fd");
    QVERIFY(QFileInfo(ovmfVarsTarget).isFile());
    QVERIFY(ownerOnly(QFileInfo(ovmfVarsTarget).permissions(), false));
    QFile varsFile(ovmfVarsTarget);
    QVERIFY(varsFile.open(QIODevice::ReadOnly));
    QCOMPARE(varsFile.readAll(), QByteArrayLiteral("fake-ovmf-vars"));

    PodStack::Error metaError;
    const MachineMeta fromFile =
        MachineMetaFile::read(directory + QStringLiteral("/meta.json"), &metaError);
    QVERIFY2(metaError.isEmpty(), qPrintable(metaError.message));
    QCOMPARE(fromFile.state, QStringLiteral("running"));
    QCOMPARE(fromFile.name, meta.name);

    QCOMPARE(manager.list(&metaError).size(), 1);
    QCOMPARE(manager.get(meta.id, &metaError).state, QStringLiteral("running"));
    QCOMPARE(manager.get(meta.name, &metaError).id, meta.id);
    QVERIFY2(metaError.isEmpty(), qPrintable(metaError.message));

    QFile logFile(qemuImgLog);
    QVERIFY(logFile.open(QIODevice::ReadOnly));
    const QStringList args = QString::fromUtf8(logFile.readAll())
                                 .split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    QCOMPARE(args.value(0), QStringLiteral("create"));
    QCOMPARE(args.value(1), QStringLiteral("-f"));
    QCOMPARE(args.value(2), QStringLiteral("qcow2"));
    QCOMPARE(args.value(3), QStringLiteral("-F"));
    QCOMPARE(args.value(4), QStringLiteral("qcow2"));
    QCOMPARE(args.value(5), QStringLiteral("-b"));
    QCOMPARE(args.value(6), CloudImageCache(imageCacheDir).cachedPath(image));
    QCOMPARE(args.value(args.size() - 2), meta.diskPath);
    QCOMPARE(args.value(args.size() - 1), QString::number(request.diskBytes));

    QCOMPARE(fake->startCount, 1);
    QCOMPARE(fake->lastSpec.machineId, meta.id);
    QCOMPARE(fake->lastSpec.diskPath, meta.diskPath);
    QCOMPARE(fake->lastSpec.seedPath, meta.seedPath);
    QCOMPARE(fake->lastSpec.qmpSocketPath, directory + QStringLiteral("/qmp.sock"));
    QCOMPARE(fake->lastSpec.pidFile, directory + QStringLiteral("/qemu.pid"));
    QCOMPARE(static_cast<int>(fake->lastSpec.sshPort), meta.sshPort);
    QCOMPARE(fake->lastSpec.ovmfCodePath, ovmfCode);
    QCOMPARE(fake->lastSpec.ovmfVarsPath, ovmfVarsTarget);

    QVERIFY(stages.contains(QStringLiteral("image")));
    QVERIFY(stages.contains(QStringLiteral("disk")));
    QVERIFY(stages.contains(QStringLiteral("seed")));
    QVERIFY(stages.contains(QStringLiteral("starting")));
    QCOMPARE(stages.last(), QStringLiteral("running"));

    QVERIFY(QFileInfo(storePath).isFile());
}

void MachinesTest::k8sEnableProvisionsCluster()
{
    const QString root = m_root + QStringLiteral("/k8s-enable");
    QDir(root).removeRecursively();
    QVERIFY(QDir().mkpath(root));
    QString ovmfCode;
    QString ovmfVars;
    QVERIFY(createFirmwareFiles(root, &ovmfCode, &ovmfVars));
    const QString qemuImg = createQemuImgScript(root);
    QVERIFY(!qemuImg.isEmpty());

    const QByteArray payload(4096, 'k');
    m_server.setResponse(QStringLiteral("/k8s-enable.qcow2"), 200, payload);
    const DistroImage image = testImage(QStringLiteral("/k8s-enable.qcow2"), payload);

    MachineManager::Options options = k8sMachineOptions(root, image, qemuImg, ovmfCode, ovmfVars);
    QVERIFY(!options.storePath.isEmpty());
    const QString storePath = options.storePath;
    MachineManager machines(std::move(options));

    QStringList scripts;
    PodStack::Config tlsConfig = PodStack::Config::load();
    tlsConfig.set(QStringLiteral("kubernetes.tlsSan"),
                  QJsonArray{QStringLiteral("k8s.example.test")});
    QString tlsError;
    QVERIFY2(tlsConfig.save(&tlsError), qPrintable(tlsError));
    QCOMPARE(PodStack::Config::load().get(QStringLiteral("kubernetes.tlsSan")).toArray().size(), 1);

    K8sManager::Options k8sOptions;
    k8sOptions.storePath = storePath;
    k8sOptions.kubeconfigDir = root + QStringLiteral("/kubeconfigs");
    k8sOptions.remoteExec =
        [&scripts](const SshInfo &, const QString &script, const K8sManager::Line &onLine,
                   const K8sManager::Cancelled &, int, PodStack::Error *execError) {
            scripts.append(script);
            if (execError) {
                *execError = {};
            }
            if (script.contains(QStringLiteral("sudo cat")) && onLine) {
                for (const QString &line : k8sKubeconfigLines()) {
                    onLine(line);
                }
            }
            return true;
        };
    K8sManager k8s(&machines, k8sOptions);

    QStringList stages;
    int lastProgress = 0;
    K8sManager::EnableRequest request;
    request.cpus = 2;
    request.memoryBytes = 2LL * 1024 * 1024 * 1024;
    request.diskBytes = 10LL * 1024 * 1024 * 1024;
    PodStack::Error error;
    const K8sCluster cluster = k8s.enable(
        request,
        [&stages, &lastProgress](const QString &stage, int value) {
            stages.append(stage);
            lastProgress = value;
        },
        {}, &error);

    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QCOMPARE(cluster.state, QStringLiteral("ready"));
    QVERIFY(!cluster.vmMachineId.isEmpty());
    QCOMPARE(lastProgress, 100);
    QVERIFY(stages.contains(QStringLiteral("machine")));
    QVERIFY(stages.contains(QStringLiteral("ssh")));
    QVERIFY(stages.contains(QStringLiteral("install")));
    QVERIFY(stages.contains(QStringLiteral("ready")));
    QVERIFY(scripts.join(QLatin1Char('\n')).contains(QStringLiteral("get.k3s.io")));
    QVERIFY(scripts.join(QLatin1Char('\n')).contains(QStringLiteral("get nodes")));
    QVERIFY2(scripts.join(QLatin1Char('\n')).contains(QStringLiteral("--tls-san=k8s.example.test")),
             qPrintable(scripts.join(QLatin1Char('\n'))));

    const MachineMeta vm = machines.get(cluster.vmMachineId, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QCOMPARE(vm.name, QStringLiteral("podstack-k8s"));
    QCOMPARE(vm.cpus, 2);
    QVERIFY(!vm.isolateNetwork);
    QVERIFY(!vm.ports.isEmpty());
    QVERIFY(!vm.ports.at(0).exposeLan);
    QCOMPARE(vm.state, QStringLiteral("running"));

    K8sStore store(storePath);
    QVERIFY2(store.open(&error), qPrintable(error.message));
    const K8sCluster persisted = store.get(QStringLiteral("default"), &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QCOMPARE(persisted.state, QStringLiteral("ready"));
    QCOMPARE(persisted.vmMachineId, cluster.vmMachineId);
    QVERIFY(PodStack::Config::load().get(QStringLiteral("kubernetes.enabled")).toBool());
}

void MachinesTest::k8sEnableReusesCluster()
{
    const QString root = m_root + QStringLiteral("/k8s-reuse");
    QDir(root).removeRecursively();
    QVERIFY(QDir().mkpath(root));
    QString ovmfCode;
    QString ovmfVars;
    QVERIFY(createFirmwareFiles(root, &ovmfCode, &ovmfVars));
    const QString qemuImg = createQemuImgScript(root);
    QVERIFY(!qemuImg.isEmpty());

    const QByteArray payload(4096, 'r');
    m_server.setResponse(QStringLiteral("/k8s-reuse.qcow2"), 200, payload);
    const DistroImage image = testImage(QStringLiteral("/k8s-reuse.qcow2"), payload);

    MachineManager::Options options = k8sMachineOptions(root, image, qemuImg, ovmfCode, ovmfVars);
    QVERIFY(!options.storePath.isEmpty());
    const QString storePath = options.storePath;
    MachineManager machines(std::move(options));

    int execCount = 0;
    K8sManager::Options k8sOptions;
    k8sOptions.storePath = storePath;
    k8sOptions.kubeconfigDir = root + QStringLiteral("/kubeconfigs");
    k8sOptions.remoteExec =
        [&execCount](const SshInfo &, const QString &script, const K8sManager::Line &onLine,
                     const K8sManager::Cancelled &, int, PodStack::Error *execError) {
            ++execCount;
            if (execError) {
                *execError = {};
            }
            if (script.contains(QStringLiteral("sudo cat")) && onLine) {
                for (const QString &line : k8sKubeconfigLines()) {
                    onLine(line);
                }
            }
            return true;
        };
    K8sManager k8s(&machines, k8sOptions);

    PodStack::Error error;
    const K8sCluster first = k8s.enable({}, {}, {}, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QVERIFY(execCount > 0);
    const int machineCount = machines.list(&error).size();
    QCOMPARE(machineCount, 1);

    execCount = 0;
    const K8sCluster second = k8s.enable({}, {}, {}, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QCOMPARE(second.vmMachineId, first.vmMachineId);
    QCOMPARE(execCount, 0);
    QCOMPARE(machines.list(&error).size(), 1);
}

void MachinesTest::k8sEnableInstallFailure()
{
    const QString root = m_root + QStringLiteral("/k8s-install-failure");
    QDir(root).removeRecursively();
    QVERIFY(QDir().mkpath(root));
    QString ovmfCode;
    QString ovmfVars;
    QVERIFY(createFirmwareFiles(root, &ovmfCode, &ovmfVars));
    const QString qemuImg = createQemuImgScript(root);
    QVERIFY(!qemuImg.isEmpty());

    const QByteArray payload(4096, 'f');
    m_server.setResponse(QStringLiteral("/k8s-failure.qcow2"), 200, payload);
    const DistroImage image = testImage(QStringLiteral("/k8s-failure.qcow2"), payload);

    MachineManager::Options options = k8sMachineOptions(root, image, qemuImg, ovmfCode, ovmfVars);
    QVERIFY(!options.storePath.isEmpty());
    const QString storePath = options.storePath;
    MachineManager machines(std::move(options));

    K8sManager::Options k8sOptions;
    k8sOptions.storePath = storePath;
    k8sOptions.remoteExec = [](const SshInfo &, const QString &script, const K8sManager::Line &,
                               const K8sManager::Cancelled &, int, PodStack::Error *execError) {
        if (script.contains(QStringLiteral("get.k3s.io"))) {
            if (execError) {
                *execError = PodStack::Error::make(QStringLiteral("PODSTACK_IO_ERROR"),
                                                   QStringLiteral("install falhou"));
            }
            return false;
        }
        if (execError) {
            *execError = {};
        }
        return true;
    };
    K8sManager k8s(&machines, k8sOptions);

    PodStack::Error error;
    const K8sCluster cluster = k8s.enable({}, {}, {}, &error);
    QVERIFY(cluster.id.isEmpty());
    QCOMPARE(error.code, QStringLiteral("PODSTACK_IO_ERROR"));

    K8sStore store(storePath);
    QVERIFY2(store.open(&error), qPrintable(error.message));
    const K8sCluster persisted = store.get(QStringLiteral("default"), &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QCOMPARE(persisted.state, QStringLiteral("error"));
    QVERIFY(persisted.error.contains(QStringLiteral("install falhou")));

    const MachineMeta vm = machines.get(persisted.vmMachineId, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QCOMPARE(vm.state, QStringLiteral("stopped"));
}

void MachinesTest::k8sEnableCancelled()
{
    const QString root = m_root + QStringLiteral("/k8s-cancelled");
    QDir(root).removeRecursively();
    QVERIFY(QDir().mkpath(root));
    QString ovmfCode;
    QString ovmfVars;
    QVERIFY(createFirmwareFiles(root, &ovmfCode, &ovmfVars));
    const QString qemuImg = createQemuImgScript(root);
    QVERIFY(!qemuImg.isEmpty());

    const QByteArray payload(4096, 'c');
    m_server.setResponse(QStringLiteral("/k8s-cancelled.qcow2"), 200, payload);
    const DistroImage image = testImage(QStringLiteral("/k8s-cancelled.qcow2"), payload);

    MachineManager::Options options = k8sMachineOptions(root, image, qemuImg, ovmfCode, ovmfVars);
    QVERIFY(!options.storePath.isEmpty());
    const QString storePath = options.storePath;
    MachineManager machines(std::move(options));

    K8sManager::Options k8sOptions;
    k8sOptions.storePath = storePath;
    k8sOptions.kubeconfigDir = root + QStringLiteral("/kubeconfigs");
    k8sOptions.remoteExec = [](const SshInfo &, const QString &script,
                               const K8sManager::Line &onLine, const K8sManager::Cancelled &, int,
                               PodStack::Error *execError) {
        if (execError) {
            *execError = {};
        }
        if (script.contains(QStringLiteral("sudo cat")) && onLine) {
            for (const QString &line : k8sKubeconfigLines()) {
                onLine(line);
            }
        }
        return true;
    };
    K8sManager k8s(&machines, k8sOptions);

    PodStack::Error error;
    const K8sCluster cluster = k8s.enable({}, {}, [] { return true; }, &error);
    QVERIFY(cluster.id.isEmpty());
    QCOMPARE(error.code, QStringLiteral("PODSTACK_CANCELLED"));
    QVERIFY(machines.list(&error).isEmpty());

    K8sStore store(storePath);
    QVERIFY2(store.open(&error), qPrintable(error.message));
    const K8sCluster persisted = store.get(QStringLiteral("default"), &error);
    if (error.isEmpty()) {
        QVERIFY(persisted.state != QStringLiteral("ready"));
    }
}

void MachinesTest::k8sStatusReconcilesStoppedMachine()
{
    const QString root = m_root + QStringLiteral("/k8s-status");
    QDir(root).removeRecursively();
    QVERIFY(QDir().mkpath(root));
    QString ovmfCode;
    QString ovmfVars;
    QVERIFY(createFirmwareFiles(root, &ovmfCode, &ovmfVars));
    const QString qemuImg = createQemuImgScript(root);
    QVERIFY(!qemuImg.isEmpty());

    const QByteArray payload(4096, 's');
    m_server.setResponse(QStringLiteral("/k8s-status.qcow2"), 200, payload);
    const DistroImage image = testImage(QStringLiteral("/k8s-status.qcow2"), payload);

    MachineManager::Options options = k8sMachineOptions(root, image, qemuImg, ovmfCode, ovmfVars);
    QVERIFY(!options.storePath.isEmpty());
    const QString storePath = options.storePath;
    MachineManager machines(std::move(options));

    K8sManager::Options k8sOptions;
    k8sOptions.storePath = storePath;
    k8sOptions.kubeconfigDir = root + QStringLiteral("/kubeconfigs");
    k8sOptions.remoteExec = [](const SshInfo &, const QString &script,
                               const K8sManager::Line &onLine, const K8sManager::Cancelled &, int,
                               PodStack::Error *execError) {
        if (execError) {
            *execError = {};
        }
        if (script.contains(QStringLiteral("sudo cat")) && onLine) {
            for (const QString &line : k8sKubeconfigLines()) {
                onLine(line);
            }
        }
        return true;
    };
    K8sManager k8s(&machines, k8sOptions);

    PodStack::Error error;
    const K8sCluster ready = k8s.enable({}, {}, {}, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QCOMPARE(k8s.status(&error).state, QStringLiteral("ready"));

    const MachineMeta stopped = machines.stop(ready.vmMachineId, true, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QCOMPARE(stopped.state, QStringLiteral("stopped"));
    QCOMPARE(k8s.status(&error).state, QStringLiteral("stopped"));

    const MachineMeta started = machines.start(ready.vmMachineId, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QCOMPARE(started.state, QStringLiteral("running"));
    QCOMPARE(k8s.status(&error).state, QStringLiteral("ready"));
}

void MachinesTest::k8sStoreLifecycle()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("k8s.db"));
    K8sStore store(path);
    PodStack::Error error;
    QVERIFY2(store.open(&error), qPrintable(error.message));
    QVERIFY(store.list(&error).isEmpty());

    QVERIFY(store.get(QStringLiteral("default"), &error).id.isEmpty());
    QCOMPARE(error.code, QStringLiteral("PODSTACK_NOT_FOUND"));

    K8sCluster cluster;
    cluster.id = QStringLiteral("k8s-default");
    cluster.name = QStringLiteral("default");
    cluster.state = QStringLiteral("ready");
    cluster.apiPort = 6443;
    cluster.kubeconfigPath = QStringLiteral("/tmp/kubeconfig");
    cluster.contextName = QStringLiteral("podstack-default");
    cluster.vmMachineId = QStringLiteral("vm-1");
    cluster.createdAt = QStringLiteral("2026-01-01T00:00:00.000Z");
    cluster.updatedAt = cluster.createdAt;
    QVERIFY2(store.upsert(cluster, &error),
             qPrintable(error.message + QStringLiteral(" | ") + error.detail));

    const K8sCluster loaded = store.get(QStringLiteral("default"), &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QCOMPARE(loaded.apiPort, 6443);
    QCOMPARE(loaded.contextName, QStringLiteral("podstack-default"));
    QCOMPARE(loaded.vmMachineId, QStringLiteral("vm-1"));
    QCOMPARE(store.list(&error).size(), 1);

    cluster.state = QStringLiteral("stopped");
    QVERIFY(store.upsert(cluster, &error));
    QCOMPARE(store.get(QStringLiteral("default"), &error).state, QStringLiteral("stopped"));
    QVERIFY(store.remove(QStringLiteral("default"), &error));
    QVERIFY(store.list(&error).isEmpty());

    K8sCluster invalid;
    QVERIFY(!store.upsert(invalid, &error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_INVALID_PARAMS"));

    const QString blocker = directory.filePath(QStringLiteral("blocker"));
    QVERIFY(writeTestFile(blocker, QByteArrayLiteral("x")));
    K8sStore broken(blocker + QStringLiteral("/k8s.db"));
    error = {};
    QVERIFY(!broken.open(&error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_DB_ERROR"));
    QVERIFY(broken.get(QStringLiteral("default"), &error).id.isEmpty());
    QVERIFY(broken.list(&error).isEmpty());
    QVERIFY(!broken.remove(QStringLiteral("default"), &error));

    const QString readonlyDir = directory.filePath(QStringLiteral("readonly"));
    QVERIFY(QDir().mkpath(readonlyDir));
    QVERIFY(QFile::setPermissions(readonlyDir, QFileDevice::ReadOwner | QFileDevice::ExeOwner));
    // Root ignora as permissões do diretório; o cenário só vale para usuário comum.
    if (::geteuid() != 0) {
        K8sStore readonlyStore(readonlyDir + QStringLiteral("/k8s.db"));
        error = {};
        QVERIFY(!readonlyStore.open(&error));
        QCOMPARE(error.code, QStringLiteral("PODSTACK_DB_ERROR"));
    }
    QFile::setPermissions(readonlyDir,
                          QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
}

void MachinesTest::k8sStatusStaleAndMissingVm()
{
    const QString root = m_root + QStringLiteral("/k8s-status-errors");
    QDir(root).removeRecursively();
    QVERIFY(QDir().mkpath(root));

    MachineManager::Options options;
    QVERIFY(configureSshOptions(&options, root));
    options.storePath = root + QStringLiteral("/podstack.db");
    options.machinesRoot = root + QStringLiteral("/machines");
    options.imageCacheDir = root + QStringLiteral("/images");
    options.runtime = std::make_unique<FakeQemuRuntime>();
    MachineManager machines(std::move(options));

    K8sManager::Options k8sOptions;
    k8sOptions.storePath = root + QStringLiteral("/podstack.db");
    K8sManager k8s(&machines, k8sOptions);

    QCOMPARE(k8s.status().state, QStringLiteral("disabled"));

    K8sStore store(k8sOptions.storePath);
    K8sCluster cluster;
    cluster.id = QStringLiteral("k8s-default");
    cluster.name = QStringLiteral("default");
    cluster.state = QStringLiteral("installing");
    cluster.createdAt = QStringLiteral("2026-01-01T00:00:00.000Z");
    cluster.updatedAt = cluster.createdAt;
    PodStack::Error error;
    QVERIFY2(store.upsert(cluster, &error),
             qPrintable(error.message + QStringLiteral(" | ") + error.detail));
    QCOMPARE(k8s.status(&error).state, QStringLiteral("error"));
    QVERIFY(k8s.status(&error).error.contains(QStringLiteral("interrompido")));

    cluster.state = QStringLiteral("ready");
    cluster.vmMachineId.clear();
    QVERIFY(store.upsert(cluster, &error));
    QCOMPARE(k8s.status(&error).state, QStringLiteral("ready"));

    cluster.vmMachineId = QStringLiteral("missing-vm");
    QVERIFY(store.upsert(cluster, &error));
    QCOMPARE(k8s.status(&error).state, QStringLiteral("error"));
    QVERIFY(k8s.status(&error).error.contains(QStringLiteral("não existe")));
}

void MachinesTest::k8sEnableNameConflict()
{
    const QString root = m_root + QStringLiteral("/k8s-name-conflict");
    QDir(root).removeRecursively();
    QVERIFY(QDir().mkpath(root));
    QString ovmfCode;
    QString ovmfVars;
    QVERIFY(createFirmwareFiles(root, &ovmfCode, &ovmfVars));
    const QString qemuImg = createQemuImgScript(root);
    QVERIFY(!qemuImg.isEmpty());

    const QByteArray payload(4096, 'n');
    m_server.setResponse(QStringLiteral("/k8s-conflict.qcow2"), 200, payload);
    const DistroImage image = testImage(QStringLiteral("/k8s-conflict.qcow2"), payload);

    MachineManager::Options options = k8sMachineOptions(root, image, qemuImg, ovmfCode, ovmfVars);
    QVERIFY(!options.storePath.isEmpty());
    const QString storePath = options.storePath;
    MachineManager machines(std::move(options));

    CreateMachineRequest existing;
    existing.name = QStringLiteral("podstack-k8s");
    existing.distro = QStringLiteral("test");
    existing.arch = QStringLiteral("amd64");
    existing.user = QStringLiteral("podstack");
    existing.diskBytes = 1024LL * 1024LL * 1024LL;
    PodStack::Error error;
    QVERIFY2(!machines.create(existing, {}, {}, &error).id.isEmpty(), qPrintable(error.message));

    K8sManager::Options k8sOptions;
    k8sOptions.storePath = storePath;
    k8sOptions.kubeconfigDir = root + QStringLiteral("/kubeconfigs");
    k8sOptions.remoteExec = [](const SshInfo &, const QString &script,
                               const K8sManager::Line &onLine, const K8sManager::Cancelled &, int,
                               PodStack::Error *execError) {
        if (execError) {
            *execError = {};
        }
        if (script.contains(QStringLiteral("sudo cat")) && onLine) {
            for (const QString &line : k8sKubeconfigLines()) {
                onLine(line);
            }
        }
        return true;
    };
    K8sManager k8s(&machines, k8sOptions);

    K8sManager::EnableRequest invalid;
    invalid.cpus = 0;
    QVERIFY(k8s.enable(invalid, {}, {}, &error).id.isEmpty());
    QCOMPARE(error.code, QStringLiteral("PODSTACK_INVALID_PARAMS"));

    const K8sCluster cluster = k8s.enable({}, {}, {}, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QCOMPARE(cluster.state, QStringLiteral("ready"));
    const MachineMeta vm = machines.get(cluster.vmMachineId, &error);
    QCOMPARE(vm.name, QStringLiteral("podstack-k8s-2"));
}

void MachinesTest::k8sSshRunnerSuccess()
{
    const QString root = m_root + QStringLiteral("/k8s-ssh-success");
    QDir(root).removeRecursively();
    QVERIFY(QDir().mkpath(root));
    QString ovmfCode;
    QString ovmfVars;
    QVERIFY(createFirmwareFiles(root, &ovmfCode, &ovmfVars));
    const QString qemuImg = createQemuImgScript(root);
    QVERIFY(!qemuImg.isEmpty());

    const QByteArray payload(4096, 'x');
    m_server.setResponse(QStringLiteral("/k8s-ssh.qcow2"), 200, payload);
    const DistroImage image = testImage(QStringLiteral("/k8s-ssh.qcow2"), payload);

    MachineManager::Options options = k8sMachineOptions(root, image, qemuImg, ovmfCode, ovmfVars);
    QVERIFY(!options.storePath.isEmpty());
    const QString storePath = options.storePath;
    MachineManager machines(std::move(options));

    const QString marker = root + QStringLiteral("/ssh-ready");
    const QString ssh = k8sFakeSsh(
        root, QByteArrayLiteral(
                  "#!/bin/sh\n"
                  "for last do :; done\n"
                  "case \"$last\" in\n"
                  "  *cloud-init*)\n"
                  "    if [ ! -f \"$PODSTACK_FAKE_SSH_MARKER\" ]; then\n"
                  "      : > \"$PODSTACK_FAKE_SSH_MARKER\"\n"
                  "      echo 'ssh ainda subindo' >&2\n"
                  "      exit 1\n"
                  "    fi\n"
                  "    echo 'cloud-init ok'; exit 0;;\n"
                  "  *get.k3s.io*)\n"
                  "    i=1\n"
                  "    while [ $i -le 25 ]; do printf 'linha %s\\r\\n' \"$i\"; i=$((i+1)); done\n"
                  "    printf 'ultima-linha-sem-newline'\n"
                  "    exit 0;;\n"
                  "  *\"sudo cat\"*)\n"
                  "    cat <<'YAML'\n"
                  "apiVersion: v1\n"
                  "clusters:\n"
                  "- cluster:\n"
                  "    server: https://127.0.0.1:6443\n"
                  "  name: default\n"
                  "contexts:\n"
                  "- context:\n"
                  "    cluster: default\n"
                  "    user: default\n"
                  "  name: default\n"
                  "current-context: default\n"
                  "users:\n"
                  "- name: default\n"
                  "YAML\n"
                  "    exit 0;;\n"
                  "  *\"get nodes\"*) echo 'node Ready'; exit 0;;\n"
                  "  *) echo \"inesperado: $last\" >&2; exit 1;;\n"
                  "esac\n"));
    QVERIFY(!ssh.isEmpty());
    qputenv("PODSTACK_FAKE_SSH_MARKER", marker.toUtf8());
    const auto restore = qScopeGuard([] { qunsetenv("PODSTACK_FAKE_SSH_MARKER"); });

    K8sManager::Options k8sOptions;
    k8sOptions.storePath = storePath;
    k8sOptions.kubeconfigDir = root + QStringLiteral("/kubeconfigs");
    k8sOptions.sshBinary = ssh;
    K8sManager k8s(&machines, k8sOptions);

    PodStack::Error error;
    const K8sCluster cluster = k8s.enable({}, {}, {}, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QCOMPARE(cluster.state, QStringLiteral("ready"));
    QVERIFY(QFileInfo::exists(marker));
}

void MachinesTest::k8sSshRunnerFailure()
{
    const QString root = m_root + QStringLiteral("/k8s-ssh-failure");
    QDir(root).removeRecursively();
    QVERIFY(QDir().mkpath(root));
    QString ovmfCode;
    QString ovmfVars;
    QVERIFY(createFirmwareFiles(root, &ovmfCode, &ovmfVars));
    const QString qemuImg = createQemuImgScript(root);
    QVERIFY(!qemuImg.isEmpty());

    const QByteArray payload(4096, 'y');
    m_server.setResponse(QStringLiteral("/k8s-ssh-failure.qcow2"), 200, payload);
    const DistroImage image = testImage(QStringLiteral("/k8s-ssh-failure.qcow2"), payload);

    MachineManager::Options options = k8sMachineOptions(root, image, qemuImg, ovmfCode, ovmfVars);
    QVERIFY(!options.storePath.isEmpty());
    const QString storePath = options.storePath;
    MachineManager machines(std::move(options));

    const QString ssh = k8sFakeSsh(
        root, QByteArrayLiteral("#!/bin/sh\n"
                                "for last do :; done\n"
                                "case \"$last\" in\n"
                                "  *cloud-init*) exit 0;;\n"
                                "  *get.k3s.io*) echo 'falha ao baixar k3s' >&2; exit 3;;\n"
                                "  *) exit 0;;\n"
                                "esac\n"));
    QVERIFY(!ssh.isEmpty());

    K8sManager::Options k8sOptions;
    k8sOptions.storePath = storePath;
    k8sOptions.sshBinary = ssh;
    K8sManager k8s(&machines, k8sOptions);

    PodStack::Error error;
    const K8sCluster cluster = k8s.enable({}, {}, {}, &error);
    QVERIFY(cluster.id.isEmpty());
    QCOMPARE(error.code, QStringLiteral("PODSTACK_IO_ERROR"));
    QVERIFY(error.detail.contains(QStringLiteral("falha ao baixar k3s")));
}

void MachinesTest::k8sSshRunnerTimeout()
{
    const QString root = m_root + QStringLiteral("/k8s-ssh-timeout");
    QDir(root).removeRecursively();
    QVERIFY(QDir().mkpath(root));
    QString ovmfCode;
    QString ovmfVars;
    QVERIFY(createFirmwareFiles(root, &ovmfCode, &ovmfVars));
    const QString qemuImg = createQemuImgScript(root);
    QVERIFY(!qemuImg.isEmpty());

    const QByteArray payload(4096, 'z');
    m_server.setResponse(QStringLiteral("/k8s-ssh-timeout.qcow2"), 200, payload);
    const DistroImage image = testImage(QStringLiteral("/k8s-ssh-timeout.qcow2"), payload);

    MachineManager::Options options = k8sMachineOptions(root, image, qemuImg, ovmfCode, ovmfVars);
    QVERIFY(!options.storePath.isEmpty());
    const QString storePath = options.storePath;
    MachineManager machines(std::move(options));

    const QString ssh = k8sFakeSsh(root, QByteArrayLiteral("#!/bin/sh\n"
                                                           "for last do :; done\n"
                                                           "case \"$last\" in\n"
                                                           "  *cloud-init*) exit 0;;\n"
                                                           "  *get.k3s.io*) sleep 10;;\n"
                                                           "  *) exit 0;;\n"
                                                           "esac\n"));
    QVERIFY(!ssh.isEmpty());

    K8sManager::Options k8sOptions;
    k8sOptions.storePath = storePath;
    k8sOptions.sshBinary = ssh;
    k8sOptions.installTimeoutMs = 500;
    K8sManager k8s(&machines, k8sOptions);

    PodStack::Error error;
    const K8sCluster cluster = k8s.enable({}, {}, {}, &error);
    QVERIFY(cluster.id.isEmpty());
    QCOMPARE(error.code, QStringLiteral("PODSTACK_TIMEOUT"));
}

void MachinesTest::k8sSshRunnerCancelled()
{
    const QString root = m_root + QStringLiteral("/k8s-ssh-cancelled");
    QDir(root).removeRecursively();
    QVERIFY(QDir().mkpath(root));
    QString ovmfCode;
    QString ovmfVars;
    QVERIFY(createFirmwareFiles(root, &ovmfCode, &ovmfVars));
    const QString qemuImg = createQemuImgScript(root);
    QVERIFY(!qemuImg.isEmpty());

    const QByteArray payload(4096, 'w');
    m_server.setResponse(QStringLiteral("/k8s-ssh-cancelled.qcow2"), 200, payload);
    const DistroImage image = testImage(QStringLiteral("/k8s-ssh-cancelled.qcow2"), payload);

    MachineManager::Options options = k8sMachineOptions(root, image, qemuImg, ovmfCode, ovmfVars);
    QVERIFY(!options.storePath.isEmpty());
    const QString storePath = options.storePath;
    MachineManager machines(std::move(options));

    const QString ssh = k8sFakeSsh(root, QByteArrayLiteral("#!/bin/sh\n"
                                                           "for last do :; done\n"
                                                           "case \"$last\" in\n"
                                                           "  *cloud-init*) exit 0;;\n"
                                                           "  *get.k3s.io*) sleep 10;;\n"
                                                           "  *) exit 0;;\n"
                                                           "esac\n"));
    QVERIFY(!ssh.isEmpty());

    bool cancelRequested = false;
    K8sManager::Options k8sOptions;
    k8sOptions.storePath = storePath;
    k8sOptions.sshBinary = ssh;
    K8sManager k8s(&machines, k8sOptions);

    PodStack::Error error;
    const K8sCluster cluster = k8s.enable(
        {},
        [&cancelRequested](const QString &stage, int) {
            if (stage == QStringLiteral("install")) {
                cancelRequested = true;
            }
        },
        [&cancelRequested] { return cancelRequested; }, &error);
    QVERIFY(cluster.id.isEmpty());
    QCOMPARE(error.code, QStringLiteral("PODSTACK_CANCELLED"));
}

void MachinesTest::k8sSshRunnerStartFailure()
{
    const QString root = m_root + QStringLiteral("/k8s-ssh-start-failure");
    QDir(root).removeRecursively();
    QVERIFY(QDir().mkpath(root));
    QString ovmfCode;
    QString ovmfVars;
    QVERIFY(createFirmwareFiles(root, &ovmfCode, &ovmfVars));
    const QString qemuImg = createQemuImgScript(root);
    QVERIFY(!qemuImg.isEmpty());

    const QByteArray payload(4096, 'a');
    m_server.setResponse(QStringLiteral("/k8s-ssh-start.qcow2"), 200, payload);
    const DistroImage image = testImage(QStringLiteral("/k8s-ssh-start.qcow2"), payload);

    MachineManager::Options options = k8sMachineOptions(root, image, qemuImg, ovmfCode, ovmfVars);
    QVERIFY(!options.storePath.isEmpty());
    MachineManager machines(std::move(options));

    K8sManager::Options k8sOptions;
    k8sOptions.storePath = root + QStringLiteral("/podstack.db");
    k8sOptions.sshBinary = QStringLiteral("podstack-ssh-inexistente");
    k8sOptions.sshReadyTimeoutMs = 100;
    K8sManager k8s(&machines, k8sOptions);

    PodStack::Error error;
    const K8sCluster cluster = k8s.enable({}, {}, {}, &error);
    QVERIFY(cluster.id.isEmpty());
    QCOMPARE(error.code, QStringLiteral("PODSTACK_TIMEOUT"));
    QVERIFY(error.message.contains(QStringLiteral("SSH")));
}

void MachinesTest::k8sSshReadyTimeout()
{
    const QString root = m_root + QStringLiteral("/k8s-ssh-ready-timeout");
    QDir(root).removeRecursively();
    QVERIFY(QDir().mkpath(root));
    QString ovmfCode;
    QString ovmfVars;
    QVERIFY(createFirmwareFiles(root, &ovmfCode, &ovmfVars));
    const QString qemuImg = createQemuImgScript(root);
    QVERIFY(!qemuImg.isEmpty());

    const QByteArray payload(4096, 'b');
    m_server.setResponse(QStringLiteral("/k8s-ready-timeout.qcow2"), 200, payload);
    const DistroImage image = testImage(QStringLiteral("/k8s-ready-timeout.qcow2"), payload);

    MachineManager::Options options = k8sMachineOptions(root, image, qemuImg, ovmfCode, ovmfVars);
    QVERIFY(!options.storePath.isEmpty());
    MachineManager machines(std::move(options));

    const QString marker = root + QStringLiteral("/ssh-attempt");
    const QString ssh =
        k8sFakeSsh(root, QByteArrayLiteral("#!/bin/sh\n"
                                           "for last do :; done\n"
                                           "case \"$last\" in\n"
                                           "  *cloud-init*)\n"
                                           "    if [ ! -f \"$PODSTACK_FAKE_SSH_MARKER\" ]; then\n"
                                           "      : > \"$PODSTACK_FAKE_SSH_MARKER\"\n"
                                           "      exit 1\n"
                                           "    fi\n"
                                           "    echo 'sem ssh' >&2; exit 1;;\n"
                                           "  *) exit 0;;\n"
                                           "esac\n"));
    QVERIFY(!ssh.isEmpty());
    qputenv("PODSTACK_FAKE_SSH_MARKER", marker.toUtf8());
    const auto restoreMarker = qScopeGuard([] { qunsetenv("PODSTACK_FAKE_SSH_MARKER"); });

    K8sManager::Options k8sOptions;
    k8sOptions.storePath = root + QStringLiteral("/podstack.db");
    k8sOptions.sshBinary = ssh;
    k8sOptions.sshReadyTimeoutMs = 1500;
    K8sManager k8s(&machines, k8sOptions);

    PodStack::Error error;
    const K8sCluster cluster = k8s.enable({}, {}, {}, &error);
    QVERIFY(cluster.id.isEmpty());
    QCOMPARE(error.code, QStringLiteral("PODSTACK_TIMEOUT"));
}

void MachinesTest::k8sNodeNotReady()
{
    const QString root = m_root + QStringLiteral("/k8s-node-not-ready");
    QDir(root).removeRecursively();
    QVERIFY(QDir().mkpath(root));
    QString ovmfCode;
    QString ovmfVars;
    QVERIFY(createFirmwareFiles(root, &ovmfCode, &ovmfVars));
    const QString qemuImg = createQemuImgScript(root);
    QVERIFY(!qemuImg.isEmpty());

    const QByteArray payload(4096, 'd');
    m_server.setResponse(QStringLiteral("/k8s-node-not-ready.qcow2"), 200, payload);
    const DistroImage image = testImage(QStringLiteral("/k8s-node-not-ready.qcow2"), payload);

    MachineManager::Options options = k8sMachineOptions(root, image, qemuImg, ovmfCode, ovmfVars);
    QVERIFY(!options.storePath.isEmpty());
    MachineManager machines(std::move(options));

    const QString ssh =
        k8sFakeSsh(root, QByteArrayLiteral("#!/bin/sh\n"
                                           "for last do :; done\n"
                                           "case \"$last\" in\n"
                                           "  *cloud-init*) exit 0;;\n"
                                           "  *get.k3s.io*) exit 0;;\n"
                                           "  *\"get nodes\"*) echo 'node NotReady' >&2; exit 1;;\n"
                                           "  *) exit 0;;\n"
                                           "esac\n"));
    QVERIFY(!ssh.isEmpty());

    K8sManager::Options k8sOptions;
    k8sOptions.storePath = root + QStringLiteral("/podstack.db");
    k8sOptions.sshBinary = ssh;
    k8sOptions.readyTimeoutMs = 3000;
    K8sManager k8s(&machines, k8sOptions);

    PodStack::Error error;
    const K8sCluster cluster = k8s.enable({}, {}, {}, &error);
    QVERIFY(cluster.id.isEmpty());
    QCOMPARE(error.code, QStringLiteral("PODSTACK_IO_ERROR"));
    QVERIFY(error.detail.contains(QStringLiteral("node NotReady")));
}

void MachinesTest::k8sInstallFailureWithoutError()
{
    const QString root = m_root + QStringLiteral("/k8s-install-no-error");
    QDir(root).removeRecursively();
    QVERIFY(QDir().mkpath(root));
    QString ovmfCode;
    QString ovmfVars;
    QVERIFY(createFirmwareFiles(root, &ovmfCode, &ovmfVars));
    const QString qemuImg = createQemuImgScript(root);
    QVERIFY(!qemuImg.isEmpty());

    const QByteArray payload(4096, 'e');
    m_server.setResponse(QStringLiteral("/k8s-no-error.qcow2"), 200, payload);
    const DistroImage image = testImage(QStringLiteral("/k8s-no-error.qcow2"), payload);

    MachineManager::Options options = k8sMachineOptions(root, image, qemuImg, ovmfCode, ovmfVars);
    QVERIFY(!options.storePath.isEmpty());
    MachineManager machines(std::move(options));

    K8sManager::Options k8sOptions;
    k8sOptions.storePath = root + QStringLiteral("/podstack.db");
    k8sOptions.remoteExec = [](const SshInfo &, const QString &script, const K8sManager::Line &,
                               const K8sManager::Cancelled &, int, PodStack::Error *execError) {
        if (execError) {
            *execError = {};
        }
        return !script.contains(QStringLiteral("get.k3s.io"));
    };
    K8sManager k8s(&machines, k8sOptions);

    PodStack::Error error;
    const K8sCluster cluster = k8s.enable({}, {}, {}, &error);
    QVERIFY(cluster.id.isEmpty());
    QCOMPARE(error.code, QStringLiteral("PODSTACK_IO_ERROR"));
    QVERIFY(error.message.contains(QStringLiteral("instalar o k3s")));
}

void MachinesTest::k8sSshReadyCancelledByRemote()
{
    const QString root = m_root + QStringLiteral("/k8s-ready-cancelled");
    QDir(root).removeRecursively();
    QVERIFY(QDir().mkpath(root));
    QString ovmfCode;
    QString ovmfVars;
    QVERIFY(createFirmwareFiles(root, &ovmfCode, &ovmfVars));
    const QString qemuImg = createQemuImgScript(root);
    QVERIFY(!qemuImg.isEmpty());

    const QByteArray payload(4096, 'f');
    m_server.setResponse(QStringLiteral("/k8s-ready-cancelled.qcow2"), 200, payload);
    const DistroImage image = testImage(QStringLiteral("/k8s-ready-cancelled.qcow2"), payload);

    MachineManager::Options options = k8sMachineOptions(root, image, qemuImg, ovmfCode, ovmfVars);
    QVERIFY(!options.storePath.isEmpty());
    MachineManager machines(std::move(options));

    K8sManager::Options k8sOptions;
    k8sOptions.storePath = root + QStringLiteral("/podstack.db");
    k8sOptions.remoteExec = [](const SshInfo &, const QString &script, const K8sManager::Line &,
                               const K8sManager::Cancelled &, int, PodStack::Error *execError) {
        if (script.contains(QStringLiteral("cloud-init"))) {
            if (execError) {
                *execError = PodStack::Error::make(QStringLiteral("PODSTACK_CANCELLED"),
                                                   QStringLiteral("cancelado no ssh"));
            }
            return false;
        }
        if (execError) {
            *execError = {};
        }
        return true;
    };
    K8sManager k8s(&machines, k8sOptions);

    PodStack::Error error;
    const K8sCluster cluster = k8s.enable({}, {}, {}, &error);
    QVERIFY(cluster.id.isEmpty());
    QCOMPARE(error.code, QStringLiteral("PODSTACK_CANCELLED"));
}

void MachinesTest::k8sClusterJsonRoundtrip()
{
    K8sCluster cluster;
    cluster.id = QStringLiteral("k8s-default");
    cluster.name = QStringLiteral("default");
    cluster.state = QStringLiteral("ready");
    cluster.apiPort = 6443;
    cluster.kubeconfigPath = QStringLiteral("/kubeconfig");
    cluster.contextName = QStringLiteral("podstack-default");
    cluster.vmMachineId = QStringLiteral("vm");
    cluster.createdAt = QStringLiteral("2026-01-01T00:00:00.000Z");
    cluster.updatedAt = cluster.createdAt;
    const K8sCluster loaded = K8sCluster::fromJson(cluster.toJson());
    QCOMPARE(loaded.id, cluster.id);
    QCOMPARE(loaded.name, cluster.name);
    QCOMPARE(loaded.provider, QStringLiteral("k3s-vm"));
    QCOMPARE(loaded.state, cluster.state);
    QCOMPARE(loaded.apiPort, cluster.apiPort);
    QCOMPARE(loaded.kubeconfigPath, cluster.kubeconfigPath);
    QCOMPARE(loaded.contextName, cluster.contextName);
    QCOMPARE(loaded.vmMachineId, cluster.vmMachineId);
    QCOMPARE(loaded.createdAt, cluster.createdAt);
    QCOMPARE(loaded.updatedAt, cluster.updatedAt);
    QVERIFY(loaded.isReady());

    const K8sCluster defaults = K8sCluster::fromJson(QJsonObject{});
    QCOMPARE(defaults.provider, QStringLiteral("k3s-vm"));
    QCOMPARE(defaults.state, QStringLiteral("disabled"));
}

void MachinesTest::k8sNullManager()
{
    K8sManager k8s(nullptr);
    PodStack::Error error;
    QVERIFY(k8s.status(&error).id.isEmpty());
    QCOMPARE(error.code, QStringLiteral("PODSTACK_INTERNAL"));

    error = {};
    QVERIFY(k8s.enable({}, {}, {}, &error).id.isEmpty());
    QCOMPARE(error.code, QStringLiteral("PODSTACK_INTERNAL"));
}

void MachinesTest::k8sEnableClearsMissingVmAndStartsStopped()
{
    const QString root = m_root + QStringLiteral("/k8s-reuse-stopped");
    QDir(root).removeRecursively();
    QVERIFY(QDir().mkpath(root));
    QString ovmfCode;
    QString ovmfVars;
    QVERIFY(createFirmwareFiles(root, &ovmfCode, &ovmfVars));
    const QString qemuImg = createQemuImgScript(root);
    QVERIFY(!qemuImg.isEmpty());

    const QByteArray payload(4096, 'g');
    m_server.setResponse(QStringLiteral("/k8s-reuse-stopped.qcow2"), 200, payload);
    const DistroImage image = testImage(QStringLiteral("/k8s-reuse-stopped.qcow2"), payload);

    FakeQemuRuntime *runtime = nullptr;
    MachineManager::Options options =
        k8sMachineOptions(root, image, qemuImg, ovmfCode, ovmfVars, &runtime);
    QVERIFY(!options.storePath.isEmpty());
    QVERIFY(runtime != nullptr);
    const QString storePath = options.storePath;
    MachineManager machines(std::move(options));

    K8sManager::Options k8sOptions;
    k8sOptions.storePath = storePath;
    k8sOptions.kubeconfigDir = root + QStringLiteral("/kubeconfigs");
    k8sOptions.remoteExec = [](const SshInfo &, const QString &script,
                               const K8sManager::Line &onLine, const K8sManager::Cancelled &, int,
                               PodStack::Error *execError) {
        if (execError) {
            *execError = {};
        }
        if (script.contains(QStringLiteral("sudo cat")) && onLine) {
            for (const QString &line : k8sKubeconfigLines()) {
                onLine(line);
            }
        }
        return true;
    };
    K8sManager k8s(&machines, k8sOptions);

    K8sStore store(storePath);
    K8sCluster cluster;
    cluster.id = QStringLiteral("k8s-default");
    cluster.name = QStringLiteral("default");
    cluster.state = QStringLiteral("ready");
    cluster.vmMachineId = QStringLiteral("missing-vm");
    cluster.createdAt = QStringLiteral("2026-01-01T00:00:00.000Z");
    cluster.updatedAt = cluster.createdAt;
    PodStack::Error error;
    QVERIFY2(store.upsert(cluster, &error),
             qPrintable(error.message + QStringLiteral(" | ") + error.detail));

    const K8sCluster created = k8s.enable({}, {}, {}, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QCOMPARE(created.state, QStringLiteral("ready"));
    QVERIFY(created.vmMachineId != QStringLiteral("missing-vm"));
    const MachineMeta running = machines.get(created.vmMachineId, &error);
    QCOMPARE(running.state, QStringLiteral("running"));

    const MachineMeta stopped = machines.stop(running.id, true, &error);
    QCOMPARE(stopped.state, QStringLiteral("stopped"));

    cluster.state = QStringLiteral("ready");
    cluster.vmMachineId = running.id;
    cluster.createdAt.clear();
    QVERIFY(store.upsert(cluster, &error));
    const K8sCluster restarted = k8s.enable({}, {}, {}, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QCOMPARE(restarted.vmMachineId, running.id);
    QCOMPARE(machines.get(running.id, &error).state, QStringLiteral("running"));

    // Cancelamento depois do provisionamento, antes do SSH.
    cluster.state = QStringLiteral("error");
    cluster.vmMachineId = running.id;
    QVERIFY(store.upsert(cluster, &error));
    error = {};
    QVERIFY(k8s.enable({}, {}, [] { return true; }, &error).id.isEmpty());
    QCOMPARE(error.code, QStringLiteral("PODSTACK_CANCELLED"));

    // Start silencioso: runtime não inicia e não devolve erro.
    runtime->failStartSilently = true;
    cluster.state = QStringLiteral("ready");
    cluster.vmMachineId = running.id;
    QVERIFY(store.upsert(cluster, &error));
    error = {};
    QVERIFY(k8s.enable({}, {}, {}, &error).id.isEmpty());
    QCOMPARE(error.code, QStringLiteral("PODSTACK_IO_ERROR"));
}

void MachinesTest::k8sNodeReadyWithoutError()
{
    const QString root = m_root + QStringLiteral("/k8s-node-no-error");
    QDir(root).removeRecursively();
    QVERIFY(QDir().mkpath(root));
    QString ovmfCode;
    QString ovmfVars;
    QVERIFY(createFirmwareFiles(root, &ovmfCode, &ovmfVars));
    const QString qemuImg = createQemuImgScript(root);
    QVERIFY(!qemuImg.isEmpty());

    const QByteArray payload(4096, 'h');
    m_server.setResponse(QStringLiteral("/k8s-node-no-error.qcow2"), 200, payload);
    const DistroImage image = testImage(QStringLiteral("/k8s-node-no-error.qcow2"), payload);

    MachineManager::Options options = k8sMachineOptions(root, image, qemuImg, ovmfCode, ovmfVars);
    QVERIFY(!options.storePath.isEmpty());
    MachineManager machines(std::move(options));

    K8sManager::Options k8sOptions;
    k8sOptions.storePath = root + QStringLiteral("/podstack.db");
    k8sOptions.remoteExec = [](const SshInfo &, const QString &script, const K8sManager::Line &,
                               const K8sManager::Cancelled &, int, PodStack::Error *execError) {
        if (execError) {
            *execError = {};
        }
        return !script.contains(QStringLiteral("get nodes"));
    };
    K8sManager k8s(&machines, k8sOptions);

    PodStack::Error error;
    const K8sCluster cluster = k8s.enable({}, {}, {}, &error);
    QVERIFY(cluster.id.isEmpty());
    QCOMPARE(error.code, QStringLiteral("PODSTACK_TIMEOUT"));
}

void MachinesTest::k8sBrokenStore()
{
    const QString root = m_root + QStringLiteral("/k8s-broken-store");
    QDir(root).removeRecursively();
    QVERIFY(QDir().mkpath(root));
    const QString blocker = root + QStringLiteral("/blocker");
    QVERIFY(writeTestFile(blocker, QByteArrayLiteral("x")));

    MachineManager::Options options;
    QVERIFY(configureSshOptions(&options, root));
    options.storePath = root + QStringLiteral("/podstack.db");
    options.machinesRoot = root + QStringLiteral("/machines");
    options.imageCacheDir = root + QStringLiteral("/images");
    options.runtime = std::make_unique<FakeQemuRuntime>();
    MachineManager machines(std::move(options));

    K8sManager::Options k8sOptions;
    k8sOptions.storePath = blocker + QStringLiteral("/k8s.db");
    K8sManager k8s(&machines, k8sOptions);

    PodStack::Error error;
    QVERIFY(k8s.status(&error).id.isEmpty());
    QCOMPARE(error.code, QStringLiteral("PODSTACK_DB_ERROR"));

    error = {};
    QVERIFY(k8s.enable({}, {}, {}, &error).id.isEmpty());
    QCOMPARE(error.code, QStringLiteral("PODSTACK_DB_ERROR"));
}

void MachinesTest::k8sDefaultSshTimeout()
{
    const QString root = m_root + QStringLiteral("/k8s-default-ssh");
    QDir(root).removeRecursively();
    QVERIFY(QDir().mkpath(root));
    QString ovmfCode;
    QString ovmfVars;
    QVERIFY(createFirmwareFiles(root, &ovmfCode, &ovmfVars));
    const QString qemuImg = createQemuImgScript(root);
    QVERIFY(!qemuImg.isEmpty());

    const QByteArray payload(4096, 'j');
    m_server.setResponse(QStringLiteral("/k8s-default-ssh.qcow2"), 200, payload);
    const DistroImage image = testImage(QStringLiteral("/k8s-default-ssh.qcow2"), payload);

    MachineManager::Options options = k8sMachineOptions(root, image, qemuImg, ovmfCode, ovmfVars);
    QVERIFY(!options.storePath.isEmpty());
    MachineManager machines(std::move(options));

    K8sManager::Options k8sOptions;
    k8sOptions.storePath = options.storePath;
    k8sOptions.sshReadyTimeoutMs = 100;
    K8sManager k8s(&machines, k8sOptions);

    PodStack::Error error;
    const K8sCluster cluster = k8s.enable({}, {}, {}, &error);
    QVERIFY(cluster.id.isEmpty());
    QCOMPARE(error.code, QStringLiteral("PODSTACK_TIMEOUT"));
}

void MachinesTest::k8sCreateStartFailure()
{
    const QString root = m_root + QStringLiteral("/k8s-create-start-failure");
    QDir(root).removeRecursively();
    QVERIFY(QDir().mkpath(root));
    QString ovmfCode;
    QString ovmfVars;
    QVERIFY(createFirmwareFiles(root, &ovmfCode, &ovmfVars));
    const QString qemuImg = createQemuImgScript(root);
    QVERIFY(!qemuImg.isEmpty());

    const QByteArray payload(4096, 'q');
    m_server.setResponse(QStringLiteral("/k8s-create-start.qcow2"), 200, payload);
    const DistroImage image = testImage(QStringLiteral("/k8s-create-start.qcow2"), payload);

    FakeQemuRuntime *runtime = nullptr;
    MachineManager::Options options =
        k8sMachineOptions(root, image, qemuImg, ovmfCode, ovmfVars, &runtime);
    QVERIFY(!options.storePath.isEmpty());
    QVERIFY(runtime != nullptr);
    runtime->failStart = true;
    const QString storePath = options.storePath;
    MachineManager machines(std::move(options));

    K8sManager::Options k8sOptions;
    k8sOptions.storePath = storePath;
    k8sOptions.kubeconfigDir = root + QStringLiteral("/kubeconfigs");
    k8sOptions.remoteExec = [](const SshInfo &, const QString &script,
                               const K8sManager::Line &onLine, const K8sManager::Cancelled &, int,
                               PodStack::Error *execError) {
        if (execError) {
            *execError = {};
        }
        if (script.contains(QStringLiteral("sudo cat")) && onLine) {
            for (const QString &line : k8sKubeconfigLines()) {
                onLine(line);
            }
        }
        return true;
    };
    K8sManager k8s(&machines, k8sOptions);

    PodStack::Error error;
    const K8sCluster cluster = k8s.enable({}, {}, {}, &error);
    QVERIFY(cluster.id.isEmpty());
    QCOMPARE(error.code, QStringLiteral("PODSTACK_IO_ERROR"));
}

void MachinesTest::k8sEnableWritesKubeconfig()
{
    const QString root = m_root + QStringLiteral("/k8s-kubeconfig");
    QDir(root).removeRecursively();
    QVERIFY(QDir().mkpath(root));
    QString ovmfCode;
    QString ovmfVars;
    QVERIFY(createFirmwareFiles(root, &ovmfCode, &ovmfVars));
    const QString qemuImg = createQemuImgScript(root);
    QVERIFY(!qemuImg.isEmpty());

    const QByteArray payload(4096, 'u');
    m_server.setResponse(QStringLiteral("/k8s-kubeconfig.qcow2"), 200, payload);
    const DistroImage image = testImage(QStringLiteral("/k8s-kubeconfig.qcow2"), payload);

    MachineManager::Options options = k8sMachineOptions(root, image, qemuImg, ovmfCode, ovmfVars);
    QVERIFY(!options.storePath.isEmpty());
    const QString storePath = options.storePath;
    MachineManager machines(std::move(options));

    const QStringList kubeconfigLines{
        QStringLiteral("apiVersion: v1"),
        QStringLiteral("clusters:"),
        QStringLiteral("- cluster:"),
        QStringLiteral("    certificate-authority-data: Y2E="),
        QStringLiteral("    server: https://127.0.0.1:6443"),
        QStringLiteral("  name: default"),
        QStringLiteral("contexts:"),
        QStringLiteral("- context:"),
        QStringLiteral("    cluster: default"),
        QStringLiteral("    user: default"),
        QStringLiteral("  name: default"),
        QStringLiteral("current-context: default"),
        QStringLiteral("kind: Config"),
        QStringLiteral("users:"),
        QStringLiteral("- name: default"),
        QStringLiteral("  user:"),
        QStringLiteral("    client-certificate-data: Y2VydA=="),
    };
    int execCount = 0;
    K8sManager::Options k8sOptions;
    k8sOptions.storePath = storePath;
    k8sOptions.kubeconfigDir = root + QStringLiteral("/kubeconfigs");
    k8sOptions.contextPrefix = QStringLiteral("podstack-");
    const int requestedApiPort = freePortHint();
    k8sOptions.apiPort = requestedApiPort;
    k8sOptions.remoteExec = [&kubeconfigLines, &execCount](const SshInfo &, const QString &script,
                                                           const K8sManager::Line &onLine,
                                                           const K8sManager::Cancelled &, int,
                                                           PodStack::Error *execError) {
        ++execCount;
        if (execError) {
            *execError = {};
        }
        if (script.contains(QStringLiteral("sudo cat"))) {
            for (const QString &line : kubeconfigLines) {
                if (onLine) {
                    onLine(line);
                }
            }
        }
        return true;
    };
    K8sManager k8s(&machines, k8sOptions);

    PodStack::Error error;
    const K8sCluster cluster = k8s.enable({}, {}, {}, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QCOMPARE(cluster.state, QStringLiteral("ready"));
    QCOMPARE(cluster.apiPort, requestedApiPort);
    QCOMPARE(cluster.contextName, QStringLiteral("podstack-default"));
    QVERIFY(cluster.kubeconfigPath.endsWith(QStringLiteral("/default.yaml")));

    const QFileInfo kubeconfigInfo(cluster.kubeconfigPath);
    QVERIFY(kubeconfigInfo.isFile());
    QVERIFY(ownerOnly(kubeconfigInfo.permissions(), false));
    QFile kubeconfig(cluster.kubeconfigPath);
    QVERIFY(kubeconfig.open(QIODevice::ReadOnly));
    const QString content = QString::fromUtf8(kubeconfig.readAll());
    kubeconfig.close();
    QVERIFY(content.contains(QStringLiteral("server: https://127.0.0.1:%1").arg(cluster.apiPort)));
    QVERIFY(content.contains(QStringLiteral("current-context: podstack-default")));
    QVERIFY(content.contains(QStringLiteral("name: podstack-default")));
    QVERIFY(content.contains(QStringLiteral("cluster: podstack-default")));
    QVERIFY(content.contains(QStringLiteral("user: podstack-default")));
    QVERIFY2(!content.contains(QStringLiteral("name: default")), qPrintable(content));

    const MachineMeta vm = machines.get(cluster.vmMachineId, &error);
    bool hasApiPort = false;
    for (const MachinePort &port : vm.ports) {
        if (port.label == QStringLiteral("k8s-api")) {
            hasApiPort = port.hostPort == cluster.apiPort && port.guestPort == 6443 &&
                         port.protocol == QStringLiteral("tcp") && !port.exposeLan;
        }
    }
    QVERIFY(hasApiPort);

    K8sStore store(storePath);
    QVERIFY2(store.open(&error), qPrintable(error.message));
    const K8sCluster persisted = store.get(QStringLiteral("default"), &error);
    QCOMPARE(persisted.apiPort, cluster.apiPort);
    QCOMPARE(persisted.kubeconfigPath, cluster.kubeconfigPath);
    QCOMPARE(persisted.contextName, cluster.contextName);

    execCount = 0;
    const K8sCluster again = k8s.enable({}, {}, {}, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QCOMPARE(again.apiPort, cluster.apiPort);
    QCOMPARE(execCount, 0);
}

void MachinesTest::k8sKubeconfigFormatError()
{
    const QString root = m_root + QStringLiteral("/k8s-kubeconfig-error");
    QDir(root).removeRecursively();
    QVERIFY(QDir().mkpath(root));
    QString ovmfCode;
    QString ovmfVars;
    QVERIFY(createFirmwareFiles(root, &ovmfCode, &ovmfVars));
    const QString qemuImg = createQemuImgScript(root);
    QVERIFY(!qemuImg.isEmpty());

    const QByteArray payload(4096, 'v');
    m_server.setResponse(QStringLiteral("/k8s-kubeconfig-error.qcow2"), 200, payload);
    const DistroImage image = testImage(QStringLiteral("/k8s-kubeconfig-error.qcow2"), payload);

    MachineManager::Options options = k8sMachineOptions(root, image, qemuImg, ovmfCode, ovmfVars);
    QVERIFY(!options.storePath.isEmpty());
    const QString storePath = options.storePath;
    MachineManager machines(std::move(options));

    K8sManager::Options k8sOptions;
    k8sOptions.storePath = storePath;
    k8sOptions.kubeconfigDir = root + QStringLiteral("/kubeconfigs");
    k8sOptions.remoteExec = [](const SshInfo &, const QString &script,
                               const K8sManager::Line &onLine, const K8sManager::Cancelled &, int,
                               PodStack::Error *execError) {
        if (execError) {
            *execError = {};
        }
        if (script.contains(QStringLiteral("sudo cat")) && onLine) {
            onLine(QStringLiteral("nao-e-um-kubeconfig"));
        }
        return true;
    };
    K8sManager k8s(&machines, k8sOptions);

    PodStack::Error error;
    const K8sCluster cluster = k8s.enable({}, {}, {}, &error);
    QVERIFY(cluster.id.isEmpty());
    QCOMPARE(error.code, QStringLiteral("PODSTACK_IO_ERROR"));
    QVERIFY(error.message.contains(QStringLiteral("kubeconfig")));
}

void MachinesTest::k8sKubeconfigReadFailure()
{
    const QString root = m_root + QStringLiteral("/k8s-read-failure");
    QDir(root).removeRecursively();
    QVERIFY(QDir().mkpath(root));
    QString ovmfCode;
    QString ovmfVars;
    QVERIFY(createFirmwareFiles(root, &ovmfCode, &ovmfVars));
    const QString qemuImg = createQemuImgScript(root);
    QVERIFY(!qemuImg.isEmpty());

    const QByteArray payload(4096, 'r');
    m_server.setResponse(QStringLiteral("/k8s-read-failure.qcow2"), 200, payload);
    const DistroImage image = testImage(QStringLiteral("/k8s-read-failure.qcow2"), payload);

    MachineManager::Options options = k8sMachineOptions(root, image, qemuImg, ovmfCode, ovmfVars);
    QVERIFY(!options.storePath.isEmpty());
    MachineManager machines(std::move(options));

    K8sManager::Options k8sOptions;
    k8sOptions.storePath = root + QStringLiteral("/podstack.db");
    k8sOptions.kubeconfigDir = root + QStringLiteral("/kubeconfigs");
    k8sOptions.remoteExec = [](const SshInfo &, const QString &script, const K8sManager::Line &,
                               const K8sManager::Cancelled &, int, PodStack::Error *execError) {
        if (script.contains(QStringLiteral("sudo cat"))) {
            if (execError) {
                *execError = PodStack::Error::make(QStringLiteral("PODSTACK_IO_ERROR"),
                                                   QStringLiteral("cat falhou"));
            }
            return false;
        }
        if (execError) {
            *execError = {};
        }
        return true;
    };
    K8sManager k8s(&machines, k8sOptions);

    PodStack::Error error;
    const K8sCluster cluster = k8s.enable({}, {}, {}, &error);
    QVERIFY(cluster.id.isEmpty());
    QCOMPARE(error.code, QStringLiteral("PODSTACK_IO_ERROR"));
    QVERIFY(error.message.contains(QStringLiteral("cat falhou")));
}

void MachinesTest::k8sKubeconfigDirFailure()
{
    const QString root = m_root + QStringLiteral("/k8s-dir-failure");
    QDir(root).removeRecursively();
    QVERIFY(QDir().mkpath(root));
    const QString blocker = root + QStringLiteral("/kubeconfigs");
    QVERIFY(writeTestFile(blocker, QByteArrayLiteral("x")));
    QString ovmfCode;
    QString ovmfVars;
    QVERIFY(createFirmwareFiles(root, &ovmfCode, &ovmfVars));
    const QString qemuImg = createQemuImgScript(root);
    QVERIFY(!qemuImg.isEmpty());

    const QByteArray payload(4096, 's');
    m_server.setResponse(QStringLiteral("/k8s-dir-failure.qcow2"), 200, payload);
    const DistroImage image = testImage(QStringLiteral("/k8s-dir-failure.qcow2"), payload);

    MachineManager::Options options = k8sMachineOptions(root, image, qemuImg, ovmfCode, ovmfVars);
    QVERIFY(!options.storePath.isEmpty());
    MachineManager machines(std::move(options));

    K8sManager::Options k8sOptions;
    k8sOptions.storePath = root + QStringLiteral("/podstack.db");
    k8sOptions.kubeconfigDir = blocker;
    k8sOptions.remoteExec = [](const SshInfo &, const QString &script,
                               const K8sManager::Line &onLine, const K8sManager::Cancelled &, int,
                               PodStack::Error *execError) {
        if (execError) {
            *execError = {};
        }
        if (script.contains(QStringLiteral("sudo cat")) && onLine) {
            for (const QString &line : k8sKubeconfigLines()) {
                onLine(line);
            }
        }
        return true;
    };
    K8sManager k8s(&machines, k8sOptions);

    PodStack::Error error;
    const K8sCluster cluster = k8s.enable({}, {}, {}, &error);
    QVERIFY(cluster.id.isEmpty());
    QCOMPARE(error.code, QStringLiteral("PODSTACK_IO_ERROR"));
    QVERIFY(error.message.contains(QStringLiteral("kubeconfigs")));
}

void MachinesTest::k8sListsPodsAndServices()
{
    const QString root = m_root + QStringLiteral("/k8s-lists");
    QDir(root).removeRecursively();
    QVERIFY(QDir().mkpath(root));
    QString ovmfCode;
    QString ovmfVars;
    QVERIFY(createFirmwareFiles(root, &ovmfCode, &ovmfVars));
    const QString qemuImg = createQemuImgScript(root);
    QVERIFY(!qemuImg.isEmpty());

    const QByteArray payload(4096, 'l');
    m_server.setResponse(QStringLiteral("/k8s-lists.qcow2"), 200, payload);
    const DistroImage image = testImage(QStringLiteral("/k8s-lists.qcow2"), payload);

    MachineManager::Options options = k8sMachineOptions(root, image, qemuImg, ovmfCode, ovmfVars);
    QVERIFY(!options.storePath.isEmpty());
    const QString storePath = options.storePath;
    MachineManager machines(std::move(options));

    const QByteArray podsJson =
        QByteArrayLiteral("{\"items\":["
                          "{\"metadata\":{\"name\":\"web-1\",\"namespace\":\"prod\","
                          "\"creationTimestamp\":\"2026-11-10T10:00:00Z\"},"
                          "\"spec\":{\"nodeName\":\"node-a\",\"containers\":[{\"name\":\"web\"}]},"
                          "\"status\":{\"phase\":\"Running\",\"podIP\":\"10.42.0.5\","
                          "\"containerStatuses\":[{\"ready\":true,\"restartCount\":1}]}},"
                          "{\"metadata\":{\"name\":\"db-0\",\"namespace\":\"data\","
                          "\"creationTimestamp\":\"2026-11-10T09:00:00Z\"},"
                          "\"spec\":{\"nodeName\":\"node-b\",\"containers\":[{\"name\":\"db\"}]},"
                          "\"status\":{\"phase\":\"Pending\","
                          "\"containerStatuses\":[{\"ready\":false,\"restartCount\":0}]}}]}");
    const QByteArray servicesJson =
        QByteArrayLiteral("{\"items\":[{\"metadata\":{\"name\":\"web\",\"namespace\":\"prod\","
                          "\"creationTimestamp\":\"2026-11-10T10:00:00Z\"},"
                          "\"spec\":{\"type\":\"ClusterIP\",\"clusterIP\":\"10.43.0.10\","
                          "\"ports\":[{\"port\":80,\"protocol\":\"TCP\"}],"
                          "\"selector\":{\"app\":\"web\"}},\"status\":{}}]}");

    QStringList commands;
    K8sManager::Options k8sOptions;
    k8sOptions.storePath = storePath;
    k8sOptions.kubeconfigDir = root + QStringLiteral("/kubeconfigs");
    k8sOptions.remoteExec = [&commands, &podsJson, &servicesJson](
                                const SshInfo &, const QString &script,
                                const K8sManager::Line &onLine, const K8sManager::Cancelled &, int,
                                PodStack::Error *execError) {
        commands.append(script);
        if (execError) {
            *execError = {};
        }
        if (script.contains(QStringLiteral("sudo cat")) && onLine) {
            for (const QString &line : k8sKubeconfigLines()) {
                onLine(line);
            }
        } else if (script.contains(QStringLiteral("kubectl get pods")) && onLine) {
            onLine(QString::fromUtf8(podsJson));
        } else if (script.contains(QStringLiteral("kubectl get services")) && onLine) {
            onLine(QString::fromUtf8(servicesJson));
        }
        return true;
    };
    K8sManager k8s(&machines, k8sOptions);

    PodStack::Error error;
    QVERIFY2(k8s.enable({}, {}, {}, &error).isReady(), qPrintable(error.message));

    const QJsonArray pods = k8s.pods({}, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QCOMPARE(pods.size(), 2);
    const QJsonObject firstPod = pods.at(0).toObject();
    QCOMPARE(firstPod.value(QStringLiteral("id")).toString(), QStringLiteral("data/db-0"));
    QCOMPARE(firstPod.value(QStringLiteral("status")).toString(), QStringLiteral("Pending"));
    QCOMPARE(firstPod.value(QStringLiteral("ready")).toString(), QStringLiteral("0/1"));
    const QJsonObject secondPod = pods.at(1).toObject();
    QCOMPARE(secondPod.value(QStringLiteral("id")).toString(), QStringLiteral("prod/web-1"));
    QCOMPARE(secondPod.value(QStringLiteral("node")).toString(), QStringLiteral("node-a"));
    QCOMPARE(secondPod.value(QStringLiteral("ip")).toString(), QStringLiteral("10.42.0.5"));
    QCOMPARE(secondPod.value(QStringLiteral("restarts")).toInt(), 1);
    QCOMPARE(secondPod.value(QStringLiteral("ready")).toString(), QStringLiteral("1/1"));
    QVERIFY(!secondPod.value(QStringLiteral("age")).toString().isEmpty());

    const QJsonArray services = k8s.services({}, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QCOMPARE(services.size(), 1);
    const QJsonObject service = services.first().toObject();
    QCOMPARE(service.value(QStringLiteral("id")).toString(), QStringLiteral("prod/web"));
    QCOMPARE(service.value(QStringLiteral("type")).toString(), QStringLiteral("ClusterIP"));
    QCOMPARE(service.value(QStringLiteral("clusterIP")).toString(), QStringLiteral("10.43.0.10"));
    QCOMPARE(service.value(QStringLiteral("ports")).toString(), QStringLiteral("80/TCP"));
    QCOMPARE(service.value(QStringLiteral("selector")).toString(), QStringLiteral("app=web"));

    error = {};
    QVERIFY(k8s.pods(QStringLiteral("bad namespace"), &error).isEmpty());
    QCOMPARE(error.code, QStringLiteral("PODSTACK_INVALID_PARAMS"));

    error = {};
    QVERIFY(k8s.pods(QStringLiteral("prod"), &error).size() == 2);
    QVERIFY(commands.join(QLatin1Char('\n')).contains(QStringLiteral("get pods -n prod")));

    K8sManager::Options emptyOptions;
    emptyOptions.storePath = root + QStringLiteral("/empty.db");
    K8sManager empty(&machines, emptyOptions);
    error = {};
    QVERIFY(empty.pods({}, &error).isEmpty());
    QCOMPARE(error.code, QStringLiteral("PODSTACK_NOT_FOUND"));
}

void MachinesTest::k8sListRequiresReadyCluster()
{
    const QString root = m_root + QStringLiteral("/k8s-list-errors");
    QDir(root).removeRecursively();
    QVERIFY(QDir().mkpath(root));
    QString ovmfCode;
    QString ovmfVars;
    QVERIFY(createFirmwareFiles(root, &ovmfCode, &ovmfVars));
    const QString qemuImg = createQemuImgScript(root);
    QVERIFY(!qemuImg.isEmpty());

    const QByteArray payload(4096, 'e');
    m_server.setResponse(QStringLiteral("/k8s-list-errors.qcow2"), 200, payload);
    const DistroImage image = testImage(QStringLiteral("/k8s-list-errors.qcow2"), payload);

    MachineManager::Options options = k8sMachineOptions(root, image, qemuImg, ovmfCode, ovmfVars);
    QVERIFY(!options.storePath.isEmpty());
    const QString storePath = options.storePath;
    MachineManager machines(std::move(options));

    K8sManager::Options k8sOptions;
    k8sOptions.storePath = storePath;
    K8sManager k8s(&machines, k8sOptions);

    K8sStore store(storePath);
    PodStack::Error error;
    QVERIFY2(store.open(&error), qPrintable(error.message));
    K8sCluster cluster;
    cluster.id = QStringLiteral("k8s-default");
    cluster.name = QStringLiteral("default");
    cluster.state = QStringLiteral("stopped");
    cluster.createdAt = QStringLiteral("2026-01-01T00:00:00.000Z");
    cluster.updatedAt = cluster.createdAt;
    QVERIFY2(store.upsert(cluster, &error), qPrintable(error.message));

    error = {};
    QVERIFY(k8s.pods({}, &error).isEmpty());
    QCOMPARE(error.code, QStringLiteral("PODSTACK_CONFLICT"));

    cluster.state = QStringLiteral("ready");
    cluster.vmMachineId = QStringLiteral("missing-vm");
    QVERIFY(store.upsert(cluster, &error));
    error = {};
    QVERIFY(k8s.services({}, &error).isEmpty());
    QCOMPARE(error.code, QStringLiteral("PODSTACK_NOT_FOUND"));

    CreateMachineRequest request;
    request.name = QStringLiteral("k8s-list-stopped");
    request.distro = QStringLiteral("test");
    request.arch = QStringLiteral("amd64");
    request.user = QStringLiteral("podstack");
    request.diskBytes = 1024LL * 1024LL * 1024LL;
    const MachineMeta vm = machines.create(request, {}, {}, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QVERIFY(machines.stop(vm.id, true, &error).state == QStringLiteral("stopped"));

    cluster.vmMachineId = vm.id;
    QVERIFY(store.upsert(cluster, &error));
    error = {};
    QVERIFY(k8s.pods({}, &error).isEmpty());
    QCOMPARE(error.code, QStringLiteral("PODSTACK_CONFLICT"));
    QVERIFY(error.message.contains(QStringLiteral("não está rodando")));
}

void MachinesTest::k8sStreamLogs()
{
    const QString root = m_root + QStringLiteral("/k8s-logs");
    QDir(root).removeRecursively();
    QVERIFY(QDir().mkpath(root));
    QString ovmfCode;
    QString ovmfVars;
    QVERIFY(createFirmwareFiles(root, &ovmfCode, &ovmfVars));
    const QString qemuImg = createQemuImgScript(root);
    QVERIFY(!qemuImg.isEmpty());

    const QByteArray payload(4096, 'g');
    m_server.setResponse(QStringLiteral("/k8s-logs.qcow2"), 200, payload);
    const DistroImage image = testImage(QStringLiteral("/k8s-logs.qcow2"), payload);

    MachineManager::Options options = k8sMachineOptions(root, image, qemuImg, ovmfCode, ovmfVars);
    QVERIFY(!options.storePath.isEmpty());
    const QString storePath = options.storePath;
    MachineManager machines(std::move(options));

    QString logCommand;
    K8sManager::Options k8sOptions;
    k8sOptions.storePath = storePath;
    k8sOptions.kubeconfigDir = root + QStringLiteral("/kubeconfigs");
    k8sOptions.remoteExec =
        [&logCommand](const SshInfo &, const QString &script, const K8sManager::Line &onLine,
                      const K8sManager::Cancelled &, int, PodStack::Error *execError) {
            if (execError) {
                *execError = {};
            }
            if (script.contains(QStringLiteral("sudo cat")) && onLine) {
                for (const QString &line : k8sKubeconfigLines()) {
                    onLine(line);
                }
            } else if (script.contains(QStringLiteral("kubectl logs"))) {
                logCommand = script;
                if (onLine) {
                    onLine(QStringLiteral("2026-11-10T10:00:00Z primeira linha"));
                    onLine(QStringLiteral("2026-11-10T10:00:01Z segunda linha"));
                }
            }
            return true;
        };
    K8sManager k8s(&machines, k8sOptions);

    PodStack::Error error;
    QVERIFY2(k8s.enable({}, {}, {}, &error).isReady(), qPrintable(error.message));

    QStringList received;
    const bool ok = k8s.streamLogs(
        QStringLiteral("prod"), QStringLiteral("web"), QStringLiteral("web"), 100,
        [&received](const QString &line) { received.append(line); }, {}, &error);
    QVERIFY2(ok, qPrintable(error.message));
    QCOMPARE(received.size(), 2);
    QCOMPARE(received.at(0), QStringLiteral("2026-11-10T10:00:00Z primeira linha"));
    QVERIFY(logCommand.contains(QStringLiteral("logs -n prod web -c web")));
    QVERIFY(logCommand.contains(QStringLiteral("--tail=100")));
    QVERIFY(logCommand.contains(QStringLiteral("--timestamps")));

    error = {};
    QVERIFY(!k8s.streamLogs(QStringLiteral("bad namespace"), QStringLiteral("web"),
                            QStringLiteral("web"), 100, {}, {}, &error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_INVALID_PARAMS"));

    error = {};
    QVERIFY(!k8s.streamLogs(QStringLiteral("prod"), QStringLiteral("web"), QStringLiteral("web"), 0,
                            {}, {}, &error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_INVALID_PARAMS"));
}

void MachinesTest::k8sDeleteAndPortForward()
{
    const QString root = m_root + QStringLiteral("/k8s-actions");
    QDir(root).removeRecursively();
    QVERIFY(QDir().mkpath(root));
    QString ovmfCode;
    QString ovmfVars;
    QVERIFY(createFirmwareFiles(root, &ovmfCode, &ovmfVars));
    const QString qemuImg = createQemuImgScript(root);
    QVERIFY(!qemuImg.isEmpty());

    const QByteArray payload(4096, 'h');
    m_server.setResponse(QStringLiteral("/k8s-actions.qcow2"), 200, payload);
    const DistroImage image = testImage(QStringLiteral("/k8s-actions.qcow2"), payload);

    MachineManager::Options options = k8sMachineOptions(root, image, qemuImg, ovmfCode, ovmfVars);
    QVERIFY(!options.storePath.isEmpty());
    const QString storePath = options.storePath;
    MachineManager machines(std::move(options));

    QStringList commands;
    K8sManager::Options k8sOptions;
    k8sOptions.storePath = storePath;
    k8sOptions.kubeconfigDir = root + QStringLiteral("/kubeconfigs");
    k8sOptions.remoteExec =
        [&commands](const SshInfo &, const QString &script, const K8sManager::Line &onLine,
                    const K8sManager::Cancelled &, int, PodStack::Error *execError) {
            commands.append(script);
            if (execError) {
                *execError = {};
            }
            if (script.contains(QStringLiteral("sudo cat")) && onLine) {
                for (const QString &line : k8sKubeconfigLines()) {
                    onLine(line);
                }
            } else if (script.contains(QStringLiteral("port-forward")) && onLine) {
                onLine(QStringLiteral("Forwarding from 127.0.0.1:8080 -> 80"));
            }
            return true;
        };
    K8sManager k8s(&machines, k8sOptions);

    PodStack::Error error;
    QVERIFY2(k8s.enable({}, {}, {}, &error).isReady(), qPrintable(error.message));

    QVERIFY2(k8s.deleteResource(QStringLiteral("pod"), QStringLiteral("prod"),
                                QStringLiteral("web"), &error),
             qPrintable(error.message));
    QVERIFY(commands.last().contains(QStringLiteral("delete pod -n prod web")));
    QVERIFY(k8s.deleteResource(QStringLiteral("service"), QStringLiteral("prod"),
                               QStringLiteral("web"), &error));
    QVERIFY(commands.last().contains(QStringLiteral("delete service -n prod web")));

    QStringList lines;
    QVERIFY2(k8s.portForward(
                 QStringLiteral("pod"), QStringLiteral("prod"), QStringLiteral("web"), 8080, 80,
                 false, [&lines](const QString &line) { lines.append(line); }, {}, &error),
             qPrintable(error.message));
    QCOMPARE(lines.size(), 1);
    QVERIFY(commands.last().contains(
        QStringLiteral("port-forward --address 127.0.0.1 -n prod pod/web 8080:80")));

    QVERIFY2(k8s.portForward(QStringLiteral("service"), QStringLiteral("prod"),
                             QStringLiteral("web"), 8080, 80, true, {}, {}, &error),
             qPrintable(error.message));
    QVERIFY(commands.last().contains(
        QStringLiteral("port-forward --address 0.0.0.0 -n prod service/web 8080:80")));

    error = {};
    QVERIFY(!k8s.portForward(QStringLiteral("pod"), QStringLiteral("prod"), QStringLiteral("web"),
                             0, 80, false, {}, {}, &error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_INVALID_PARAMS"));
    error = {};
    QVERIFY(!k8s.deleteResource(QStringLiteral("pod"), QStringLiteral("bad namespace"),
                                QStringLiteral("web"), &error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_INVALID_PARAMS"));
}

void MachinesTest::k8sDisableIsIdempotent()
{
    const QString root = m_root + QStringLiteral("/k8s-disable");
    QDir(root).removeRecursively();
    QVERIFY(QDir().mkpath(root));
    QString ovmfCode;
    QString ovmfVars;
    QVERIFY(createFirmwareFiles(root, &ovmfCode, &ovmfVars));
    const QString qemuImg = createQemuImgScript(root);
    QVERIFY(!qemuImg.isEmpty());

    const QByteArray payload(4096, 'i');
    m_server.setResponse(QStringLiteral("/k8s-disable.qcow2"), 200, payload);
    const DistroImage image = testImage(QStringLiteral("/k8s-disable.qcow2"), payload);

    MachineManager::Options options = k8sMachineOptions(root, image, qemuImg, ovmfCode, ovmfVars);
    QVERIFY(!options.storePath.isEmpty());
    const QString storePath = options.storePath;
    MachineManager machines(std::move(options));

    K8sManager::Options k8sOptions;
    k8sOptions.storePath = storePath;
    k8sOptions.kubeconfigDir = root + QStringLiteral("/kubeconfigs");
    k8sOptions.remoteExec = [](const SshInfo &, const QString &script,
                               const K8sManager::Line &onLine, const K8sManager::Cancelled &, int,
                               PodStack::Error *execError) {
        if (execError) {
            *execError = {};
        }
        if (script.contains(QStringLiteral("sudo cat")) && onLine) {
            for (const QString &line : k8sKubeconfigLines()) {
                onLine(line);
            }
        }
        return true;
    };
    K8sManager k8s(&machines, k8sOptions);

    PodStack::Error error;
    const K8sCluster cluster = k8s.enable({}, {}, {}, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QVERIFY(QFileInfo::exists(cluster.kubeconfigPath));

    const K8sCluster disabled = k8s.disable(true, {}, {}, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QCOMPARE(disabled.state, QStringLiteral("disabled"));
    QVERIFY(machines.get(cluster.vmMachineId, &error).id.isEmpty());
    QCOMPARE(error.code, QStringLiteral("PODSTACK_NOT_FOUND"));
    QVERIFY(!QFileInfo::exists(cluster.kubeconfigPath));

    K8sStore store(storePath);
    QVERIFY2(store.open(&error), qPrintable(error.message));
    QVERIFY(store.get(QStringLiteral("default"), &error).id.isEmpty());
    QCOMPARE(error.code, QStringLiteral("PODSTACK_NOT_FOUND"));
    QVERIFY(!PodStack::Config::load().get(QStringLiteral("kubernetes.enabled")).toBool());

    error = {};
    const K8sCluster again = k8s.disable(true, {}, {}, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QCOMPARE(again.state, QStringLiteral("disabled"));
}

void MachinesTest::k8sUseContext()
{
    const QString root = m_root + QStringLiteral("/k8s-context");
    QDir(root).removeRecursively();
    QVERIFY(QDir().mkpath(root));
    QString ovmfCode;
    QString ovmfVars;
    QVERIFY(createFirmwareFiles(root, &ovmfCode, &ovmfVars));
    const QString qemuImg = createQemuImgScript(root);
    QVERIFY(!qemuImg.isEmpty());

    const QByteArray payload(4096, 'j');
    m_server.setResponse(QStringLiteral("/k8s-context.qcow2"), 200, payload);
    const DistroImage image = testImage(QStringLiteral("/k8s-context.qcow2"), payload);

    MachineManager::Options options = k8sMachineOptions(root, image, qemuImg, ovmfCode, ovmfVars);
    QVERIFY(!options.storePath.isEmpty());
    MachineManager machines(std::move(options));

    K8sManager::Options k8sOptions;
    k8sOptions.storePath = root + QStringLiteral("/podstack.db");
    k8sOptions.kubeconfigDir = root + QStringLiteral("/kubeconfigs");
    k8sOptions.remoteExec = [](const SshInfo &, const QString &script,
                               const K8sManager::Line &onLine, const K8sManager::Cancelled &, int,
                               PodStack::Error *execError) {
        if (execError) {
            *execError = {};
        }
        if (script.contains(QStringLiteral("sudo cat")) && onLine) {
            for (const QString &line : k8sKubeconfigLines()) {
                onLine(line);
            }
        }
        return true;
    };
    K8sManager k8s(&machines, k8sOptions);

    PodStack::Error error;
    const K8sCluster cluster = k8s.enable({}, {}, {}, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));

    QVERIFY2(k8s.useContext(QStringLiteral("podstack-default"), &error), qPrintable(error.message));
    QFile file(cluster.kubeconfigPath);
    QVERIFY(file.open(QIODevice::ReadOnly));
    QVERIFY(QString::fromUtf8(file.readAll())
                .contains(QStringLiteral("current-context: podstack-default")));
    file.close();

    error = {};
    QVERIFY(!k8s.useContext(QString(), &error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_INVALID_PARAMS"));

    error = {};
    QVERIFY(!k8s.useContext(QStringLiteral("missing"), &error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_NOT_FOUND"));

    QVERIFY(QFile::remove(cluster.kubeconfigPath));
    error = {};
    QVERIFY(!k8s.useContext(QStringLiteral("podstack-default"), &error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_IO_ERROR"));

    K8sManager::Options emptyOptions;
    emptyOptions.storePath = root + QStringLiteral("/empty.db");
    K8sManager empty(&machines, emptyOptions);
    error = {};
    QVERIFY(!empty.useContext(QStringLiteral("podstack-default"), &error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_NOT_FOUND"));
}

void MachinesTest::machineManagerCreateErrors()
{
    const QString root = m_root + QStringLiteral("/manager-create-errors");
    QDir(root).removeRecursively();
    QVERIFY(QDir().mkpath(root));

    QString ovmfCode;
    QString ovmfVars;
    QVERIFY(createFirmwareFiles(root, &ovmfCode, &ovmfVars));
    const QString qemuImg = createQemuImgScript(root);
    QVERIFY(!qemuImg.isEmpty());

    const QByteArray payload(2048, 'c');
    m_server.setResponse(QStringLiteral("/manager-create-ok.qcow2"), 200, payload);
    const DistroImage good = testImage(QStringLiteral("/manager-create-ok.qcow2"), payload);

    auto runtime = std::make_unique<FakeQemuRuntime>();
    auto *fake = runtime.get();
    MachineManager::Options options;
    QVERIFY(configureSshOptions(&options, root));
    options.storePath = root + QStringLiteral("/podstack.db");
    options.machinesRoot = root + QStringLiteral("/machines");
    options.imageCacheDir = root + QStringLiteral("/images");
    options.qemuImgBinary = qemuImg;
    options.ovmfCodePath = ovmfCode;
    options.ovmfVarsPath = ovmfVars;
    options.sshPortStart = freePortHint();
    options.runtime = std::move(runtime);
    options.resolveDistro = [good](const QString &spec, const QString &, PodStack::Error *error) {
        if (error) {
            *error = {};
        }
        DistroImage image = good;
        if (spec == QStringLiteral("bad-arch")) {
            image.arch = QStringLiteral("arm64");
        } else if (spec == QStringLiteral("bad-image")) {
            image.url.clear();
            image.checksum.clear();
        } else if (spec == QStringLiteral("download-fail")) {
            image.url = QStringLiteral("http://127.0.0.1:%1/missing-create.qcow2").arg(1);
        }
        return image;
    };

    MachineManager manager(std::move(options));
    CreateMachineRequest base;
    base.name = QStringLiteral("create-errors");
    base.arch = QStringLiteral("amd64");
    base.user = QStringLiteral("podstack");
    base.diskBytes = 1024LL * 1024LL * 1024LL;

    PodStack::Error error;
    CreateMachineRequest request = base;
    request.distro = QStringLiteral("bad-arch");
    QVERIFY(manager.create(request, {}, {}, &error).id.isEmpty());
    QCOMPARE(error.code, QStringLiteral("PODSTACK_UNSUPPORTED"));

    request = base;
    request.distro = QStringLiteral("bad-image");
    error = {};
    QVERIFY(manager.create(request, {}, {}, &error).id.isEmpty());
    QCOMPARE(error.code, QStringLiteral("PODSTACK_INTERNAL"));

    request = base;
    request.distro = QStringLiteral("download-fail");
    error = {};
    QVERIFY(manager.create(request, {}, {}, &error).id.isEmpty());
    QCOMPARE(error.code, QStringLiteral("PODSTACK_DOWNLOAD_FAILED"));

    // A imagem baixa, mas o QEMU fake falha ao iniciar.
    fake->failStart = true;
    request = base;
    request.distro = QStringLiteral("ok");
    error = {};
    const MachineMeta failed = manager.create(request, {}, {}, &error);
    QVERIFY(!failed.id.isEmpty());
    QCOMPARE(failed.state, QStringLiteral("error"));
    QVERIFY(!error.isEmpty());
}

void MachinesTest::machineManagerPortsErrors()
{
    const QString root = m_root + QStringLiteral("/manager-ports-errors");
    QDir(root).removeRecursively();
    QVERIFY(QDir().mkpath(root));

    QString ovmfCode;
    QString ovmfVars;
    QVERIFY(createFirmwareFiles(root, &ovmfCode, &ovmfVars));
    const QString qemuImg = createQemuImgScript(root);
    QVERIFY(!qemuImg.isEmpty());

    const QByteArray payload(2048, 'p');
    m_server.setResponse(QStringLiteral("/manager-ports-errors.qcow2"), 200, payload);
    const DistroImage image = testImage(QStringLiteral("/manager-ports-errors.qcow2"), payload);

    auto runtime = std::make_unique<FakeQemuRuntime>();
    MachineManager::Options options;
    QVERIFY(configureSshOptions(&options, root));
    options.storePath = root + QStringLiteral("/podstack.db");
    options.machinesRoot = root + QStringLiteral("/machines");
    options.imageCacheDir = root + QStringLiteral("/images");
    options.qemuImgBinary = qemuImg;
    options.ovmfCodePath = ovmfCode;
    options.ovmfVarsPath = ovmfVars;
    options.sshPortStart = freePortHint();
    options.runtime = std::move(runtime);
    options.resolveDistro = [image](const QString &, const QString &, PodStack::Error *error) {
        if (error) {
            *error = {};
        }
        return image;
    };

    MachineManager manager(std::move(options));
    CreateMachineRequest base;
    base.distro = QStringLiteral("test");
    base.arch = QStringLiteral("amd64");
    base.user = QStringLiteral("podstack");
    base.diskBytes = 1024LL * 1024LL * 1024LL;

    base.name = QStringLiteral("ports-a");
    PodStack::Error error;
    const MachineMeta first = manager.create(base, {}, {}, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    base.name = QStringLiteral("ports-b");
    const MachineMeta second = manager.create(base, {}, {}, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));

    const auto expect = [&](const QVector<MachinePort> &ports, const QString &code) {
        error = {};
        manager.setPorts(first.id, ports, &error);
        QCOMPARE(error.code, code);
    };

    expect({MachinePort{QStringLiteral("p"), 18080, 80, QStringLiteral("sctp"), false, {}}},
           QStringLiteral("PODSTACK_INVALID_PARAMS"));
    expect({MachinePort{QStringLiteral("p"), 0, 80, QStringLiteral("tcp"), false, {}}},
           QStringLiteral("PODSTACK_INVALID_PARAMS"));
    expect({MachinePort{QStringLiteral("p"), 18080, 0, QStringLiteral("tcp"), false, {}}},
           QStringLiteral("PODSTACK_INVALID_PARAMS"));
    expect({MachinePort{QStringLiteral("p"), 18080, 80, QStringLiteral("tcp"), false,
                        QString(65, QLatin1Char('a'))}},
           QStringLiteral("PODSTACK_INVALID_PARAMS"));
    expect({MachinePort{QStringLiteral("p"), 18080, 80, QStringLiteral("tcp"), false,
                        QStringLiteral("a\nb")}},
           QStringLiteral("PODSTACK_INVALID_PARAMS"));
    expect({MachinePort{QStringLiteral("p1"), 18080, 80, QStringLiteral("tcp"), false, {}},
            MachinePort{QStringLiteral("p2"), 18080, 81, QStringLiteral("tcp"), false, {}}},
           QStringLiteral("PODSTACK_CONFLICT"));
    expect({MachinePort{QStringLiteral("p"), 2222, 22, QStringLiteral("tcp"), false, {}}},
           QStringLiteral("PODSTACK_INVALID_PARAMS"));
    expect({MachinePort{QStringLiteral("p"), second.sshPort, 80, QStringLiteral("tcp"), false, {}}},
           QStringLiteral("PODSTACK_CONFLICT"));

    error = {};
    manager.rename(first.id, second.name, &error);
    QCOMPARE(error.code, QStringLiteral("PODSTACK_CONFLICT"));
    error = {};
    manager.clone(first.id, second.name, {}, {}, &error);
    QCOMPARE(error.code, QStringLiteral("PODSTACK_CONFLICT"));
    UpdateMachineRequest agentUpdate;
    agentUpdate.forwardSshAgent = true;
    error = {};
    manager.update(first.id, agentUpdate, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));

    base.name = QStringLiteral("ports-isolated");
    base.isolated = true;
    const MachineMeta isolated = manager.create(base, {}, {}, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    error = {};
    manager.setPorts(isolated.id,
                     {MachinePort{QStringLiteral("p"), 18081, 80, QStringLiteral("tcp"), true, {}}},
                     &error);
    QCOMPARE(error.code, QStringLiteral("PODSTACK_INVALID_PARAMS"));

    error = {};
    const MachineMeta updated =
        manager.setPorts(first.id,
                         {MachinePort{QStringLiteral("web"), 18080, 80, QStringLiteral("tcp"),
                                      false, QStringLiteral("web")}},
                         &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QVERIFY(updated.ports.size() >= 2);
}

void MachinesTest::machineManagerSerialPorts()
{
    const QString root = m_root + QStringLiteral("/manager-serial-ports");
    QDir(root).removeRecursively();
    QVERIFY(QDir().mkpath(root));

    QString ovmfCode;
    QString ovmfVars;
    QVERIFY(createFirmwareFiles(root, &ovmfCode, &ovmfVars));
    const QString qemuImg = createQemuImgScript(root);
    QVERIFY(!qemuImg.isEmpty());

    const QByteArray payload(2048, 'p');
    m_server.setResponse(QStringLiteral("/manager-serial-ports.qcow2"), 200, payload);
    const DistroImage image = testImage(QStringLiteral("/manager-serial-ports.qcow2"), payload);

    auto runtime = std::make_unique<FakeQemuRuntime>();
    FakeQemuRuntime *runtimePtr = runtime.get();
    MachineManager::Options options;
    QVERIFY(configureSshOptions(&options, root));
    const QString storePath = root + QStringLiteral("/podstack.db");
    options.storePath = storePath;
    options.machinesRoot = root + QStringLiteral("/machines");
    options.imageCacheDir = root + QStringLiteral("/images");
    options.qemuImgBinary = qemuImg;
    options.ovmfCodePath = ovmfCode;
    options.ovmfVarsPath = ovmfVars;
    options.sshPortStart = freePortHint();
    options.runtime = std::move(runtime);
    options.resolveDistro = [image](const QString &, const QString &, PodStack::Error *error) {
        if (error) {
            *error = {};
        }
        return image;
    };

    MachineManager manager(std::move(options));
    CreateMachineRequest base;
    base.distro = QStringLiteral("test");
    base.arch = QStringLiteral("amd64");
    base.user = QStringLiteral("podstack");
    base.diskBytes = 1024LL * 1024LL * 1024LL;

    PodStack::Error error;
    base.name = QStringLiteral("serial-a");
    const MachineMeta first = manager.create(base, {}, {}, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    base.name = QStringLiteral("serial-b");
    const MachineMeta second = manager.create(base, {}, {}, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));

    const QString portPath = root + QStringLiteral("/ttyUSB-test");
    QFile portFile(portPath);
    QVERIFY(portFile.open(QIODevice::WriteOnly));
    portFile.write("serial");
    portFile.close();

    const MachineMeta attached = manager.setSerialPorts(first.id, {portPath}, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QCOMPARE(attached.serialPorts, QStringList{portPath});

    error = {};
    const MachineMeta started = manager.start(first.id, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QCOMPARE(started.state, QStringLiteral("running"));

    const QString secondPath = root + QStringLiteral("/ttyACM-test");
    QFile secondFile(secondPath);
    QVERIFY(secondFile.open(QIODevice::WriteOnly));
    secondFile.write("serial");
    secondFile.close();
    error = {};
    const MachineMeta restarted = manager.setSerialPorts(first.id, {portPath, secondPath}, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QVERIFY(restarted.serialPorts.contains(secondPath));
    QCOMPARE(restarted.state, QStringLiteral("running"));

    runtimePtr->failStart = true;
    error = {};
    (void) manager.setSerialPorts(first.id, {portPath}, &error);
    QCOMPARE(error.code, QStringLiteral("PODSTACK_CONFLICT"));
    QVERIFY(error.message.contains(QStringLiteral("não reiniciou")));
    runtimePtr->failStart = false;

    QVERIFY(manager.stop(first.id, false, &error).state != QStringLiteral("running"));

    PodStack::MachineStore store(storePath);
    QVERIFY(store.open(&error));
    QVERIFY(store.updateState(first.id, QStringLiteral("deleting"), QString(), &error));
    error = {};
    (void) manager.setSerialPorts(first.id, {portPath}, &error);
    QCOMPARE(error.code, QStringLiteral("PODSTACK_CONFLICT"));
    QVERIFY(store.updateState(first.id, QStringLiteral("stopped"), QString(), &error));

    error = {};
    (void) manager.setSerialPorts(second.id, {portPath}, &error);
    QCOMPARE(error.code, QStringLiteral("PODSTACK_CONFLICT"));

    error = {};
    (void) manager.setSerialPorts(first.id, {QStringLiteral("relative")}, &error);
    QCOMPARE(error.code, QStringLiteral("PODSTACK_INVALID_PARAMS"));

    error = {};
    (void) manager.setSerialPorts(first.id, {root + QStringLiteral("/missing-tty")}, &error);
    QCOMPARE(error.code, QStringLiteral("PODSTACK_INVALID_PARAMS"));

    error = {};
    const MachineMeta detached = manager.setSerialPorts(first.id, {}, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QVERIFY(detached.serialPorts.isEmpty());
}

void MachinesTest::machineManagerUsbDevices()
{
    const QString root = m_root + QStringLiteral("/manager-usb-devices");
    QDir(root).removeRecursively();
    QVERIFY(QDir().mkpath(root));

    const QString sysRoot = root + QStringLiteral("/sys-usb");
    const auto touch = [](const QString &path, const QByteArray &content) {
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly)) {
            return false;
        }
        file.write(content);
        return true;
    };
    QVERIFY(touch(sysRoot + QStringLiteral("/1-4/busnum"), "1\n"));
    QVERIFY(touch(sysRoot + QStringLiteral("/1-4/devnum"), "4\n"));
    QVERIFY(touch(sysRoot + QStringLiteral("/1-4/idVendor"), "0781\n"));
    QVERIFY(touch(sysRoot + QStringLiteral("/1-4/idProduct"), "5567\n"));
    QVERIFY(touch(sysRoot + QStringLiteral("/1-4/manufacturer"), "SanDisk\n"));
    QVERIFY(touch(sysRoot + QStringLiteral("/1-4/product"), "Cruzer\n"));
    QVERIFY(touch(sysRoot + QStringLiteral("/1-5/busnum"), "1\n"));
    QVERIFY(touch(sysRoot + QStringLiteral("/1-5/devnum"), "5\n"));
    QVERIFY(touch(sysRoot + QStringLiteral("/1-5/idVendor"), "abcd\n"));
    QVERIFY(touch(sysRoot + QStringLiteral("/1-5/idProduct"), "ef01\n"));
    qputenv("PODSTACK_SYSFS_USB", sysRoot.toUtf8());
    const auto restoreEnv = qScopeGuard([] { qunsetenv("PODSTACK_SYSFS_USB"); });

    QString ovmfCode;
    QString ovmfVars;
    QVERIFY(createFirmwareFiles(root, &ovmfCode, &ovmfVars));
    const QString qemuImg = createQemuImgScript(root);
    QVERIFY(!qemuImg.isEmpty());

    const QByteArray payload(2048, 'p');
    m_server.setResponse(QStringLiteral("/manager-usb-devices.qcow2"), 200, payload);
    const DistroImage image = testImage(QStringLiteral("/manager-usb-devices.qcow2"), payload);

    auto runtime = std::make_unique<FakeQemuRuntime>();
    FakeQemuRuntime *runtimePtr = runtime.get();
    MachineManager::Options options;
    QVERIFY(configureSshOptions(&options, root));
    const QString storePath = root + QStringLiteral("/podstack.db");
    options.storePath = storePath;
    options.machinesRoot = root + QStringLiteral("/machines");
    options.imageCacheDir = root + QStringLiteral("/images");
    options.qemuImgBinary = qemuImg;
    options.ovmfCodePath = ovmfCode;
    options.ovmfVarsPath = ovmfVars;
    options.sshPortStart = freePortHint();
    options.runtime = std::move(runtime);
    options.resolveDistro = [image](const QString &, const QString &, PodStack::Error *error) {
        if (error) {
            *error = {};
        }
        return image;
    };

    MachineManager manager(std::move(options));
    CreateMachineRequest base;
    base.distro = QStringLiteral("test");
    base.arch = QStringLiteral("amd64");
    base.user = QStringLiteral("podstack");
    base.diskBytes = 1024LL * 1024LL * 1024LL;

    PodStack::Error error;
    base.name = QStringLiteral("usb-a");
    const MachineMeta first = manager.create(base, {}, {}, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    base.name = QStringLiteral("usb-b");
    const MachineMeta second = manager.create(base, {}, {}, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));

    const MachineMeta attached =
        manager.setUsbDevices(first.id, {MachineUsbDevice{1, 4, {}, {}, {}}}, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QCOMPARE(attached.usbDevices.size(), 1);
    QCOMPARE(attached.usbDevices.at(0).description, QStringLiteral("SanDisk Cruzer"));

    error = {};
    (void) manager.setUsbDevices(second.id, {MachineUsbDevice{1, 4, {}, {}, {}}}, &error);
    QCOMPARE(error.code, QStringLiteral("PODSTACK_CONFLICT"));

    error = {};
    (void) manager.setUsbDevices(first.id, {MachineUsbDevice{9, 9, {}, {}, {}}}, &error);
    QCOMPARE(error.code, QStringLiteral("PODSTACK_INVALID_PARAMS"));

    error = {};
    const MachineMeta started = manager.start(first.id, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QCOMPARE(started.state, QStringLiteral("running"));

    runtimePtr->failStart = true;
    error = {};
    (void) manager.setUsbDevices(first.id, {}, &error);
    QCOMPARE(error.code, QStringLiteral("PODSTACK_CONFLICT"));
    QVERIFY(error.message.contains(QStringLiteral("não reiniciou")));
    runtimePtr->failStart = false;

    QVERIFY(manager.stop(first.id, false, &error).state != QStringLiteral("running"));

    PodStack::MachineStore store(storePath);
    QVERIFY(store.open(&error));
    QVERIFY(store.updateState(first.id, QStringLiteral("deleting"), QString(), &error));
    error = {};
    (void) manager.setUsbDevices(first.id, {}, &error);
    QCOMPARE(error.code, QStringLiteral("PODSTACK_CONFLICT"));
    QVERIFY(store.updateState(first.id, QStringLiteral("stopped"), QString(), &error));

    error = {};
    const MachineMeta detached = manager.setUsbDevices(first.id, {}, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QVERIFY(detached.usbDevices.isEmpty());
}

void MachinesTest::machineManagerAudio()
{
    const QString root = m_root + QStringLiteral("/manager-audio");
    QDir(root).removeRecursively();
    QVERIFY(QDir().mkpath(root));

    QString ovmfCode;
    QString ovmfVars;
    QVERIFY(createFirmwareFiles(root, &ovmfCode, &ovmfVars));
    const QString qemuImg = createQemuImgScript(root);
    QVERIFY(!qemuImg.isEmpty());

    const QByteArray payload(2048, 'a');
    m_server.setResponse(QStringLiteral("/manager-audio.qcow2"), 200, payload);
    const DistroImage image = testImage(QStringLiteral("/manager-audio.qcow2"), payload);

    auto runtime = std::make_unique<FakeQemuRuntime>();
    MachineManager::Options options;
    QVERIFY(configureSshOptions(&options, root));
    options.storePath = root + QStringLiteral("/podstack.db");
    options.machinesRoot = root + QStringLiteral("/machines");
    options.imageCacheDir = root + QStringLiteral("/images");
    options.qemuImgBinary = qemuImg;
    options.ovmfCodePath = ovmfCode;
    options.ovmfVarsPath = ovmfVars;
    options.sshPortStart = freePortHint();
    options.runtime = std::move(runtime);
    options.resolveDistro = [image](const QString &, const QString &, PodStack::Error *error) {
        if (error) {
            *error = {};
        }
        return image;
    };

    MachineManager manager(std::move(options));
    CreateMachineRequest base;
    base.distro = QStringLiteral("test");
    base.arch = QStringLiteral("amd64");
    base.user = QStringLiteral("podstack");
    base.diskBytes = 1024LL * 1024LL * 1024LL;
    base.name = QStringLiteral("audio-a");

    PodStack::Error error;
    const MachineMeta machine = manager.create(base, {}, {}, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));

    error = {};
    const MachineMeta enabled = manager.setAudioEnabled(machine.id, true, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QVERIFY(enabled.audioEnabled);

    error = {};
    const MachineMeta started = manager.start(machine.id, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QCOMPARE(started.state, QStringLiteral("running"));

    error = {};
    const MachineMeta restarted = manager.setAudioEnabled(machine.id, false, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QVERIFY(!restarted.audioEnabled);
    QCOMPARE(restarted.state, QStringLiteral("running"));

    error = {};
    (void) manager.setAudioEnabled(machine.id, true, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QVERIFY(manager.stop(machine.id, false, &error).state != QStringLiteral("running"));
}

void MachinesTest::machineManagerUpdateRemoveErrors()
{
    const QString root = m_root + QStringLiteral("/manager-update-remove-errors");
    QDir(root).removeRecursively();
    QVERIFY(QDir().mkpath(root));

    QString ovmfCode;
    QString ovmfVars;
    QVERIFY(createFirmwareFiles(root, &ovmfCode, &ovmfVars));
    const QString qemuImg = createQemuImgScript(root);
    QVERIFY(!qemuImg.isEmpty());

    const QByteArray payload(2048, 'u');
    m_server.setResponse(QStringLiteral("/manager-update-remove.qcow2"), 200, payload);
    const DistroImage image = testImage(QStringLiteral("/manager-update-remove.qcow2"), payload);

    auto runtime = std::make_unique<FakeQemuRuntime>();
    MachineManager::Options options;
    QVERIFY(configureSshOptions(&options, root));
    options.storePath = root + QStringLiteral("/podstack.db");
    options.machinesRoot = root + QStringLiteral("/machines");
    options.imageCacheDir = root + QStringLiteral("/images");
    options.qemuImgBinary = qemuImg;
    options.ovmfCodePath = ovmfCode;
    options.ovmfVarsPath = ovmfVars;
    options.sshPortStart = freePortHint();
    options.runtime = std::move(runtime);
    options.resolveDistro = [image](const QString &, const QString &, PodStack::Error *error) {
        if (error) {
            *error = {};
        }
        return image;
    };

    MachineManager manager(std::move(options));
    CreateMachineRequest base;
    base.distro = QStringLiteral("test");
    base.arch = QStringLiteral("amd64");
    base.user = QStringLiteral("podstack");
    base.diskBytes = 1024LL * 1024LL * 1024LL;

    base.name = QStringLiteral("update-errors");
    PodStack::Error error;
    const MachineMeta created = manager.create(base, {}, {}, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));

    UpdateMachineRequest update;
    update.diskBytes = created.diskBytes / 2;
    error = {};
    manager.update(created.id, update, &error);
    QCOMPARE(error.code, QStringLiteral("PODSTACK_INVALID_PARAMS"));

    // O diretório da machine virou um symlink: remoção deve recusar.
    base.name = QStringLiteral("remove-symlink");
    const MachineMeta symlinkMachine = manager.create(base, {}, {}, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    const QString directory = root + QStringLiteral("/machines/") + symlinkMachine.id;
    QVERIFY(QDir(directory).removeRecursively());
    QVERIFY(QFile::link(QStringLiteral("/tmp"), directory));
    error = {};
    QVERIFY(!manager.remove(symlinkMachine.id, true, &error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_PERMISSION_DENIED"));
    QFile::remove(directory);
}

void MachinesTest::machineManagerIsolated()
{
    const QString root = m_root + QStringLiteral("/manager-isolated");
    QDir(root).removeRecursively();
    QVERIFY(QDir().mkpath(root));

    QString ovmfCode;
    QString ovmfVars;
    QVERIFY(createFirmwareFiles(root, &ovmfCode, &ovmfVars));
    const QString qemuImg = createQemuImgScript(root);
    QVERIFY(!qemuImg.isEmpty());

    const QByteArray payload(2048, 's');
    m_server.setResponse(QStringLiteral("/manager-isolated.qcow2"), 200, payload);
    const DistroImage image = testImage(QStringLiteral("/manager-isolated.qcow2"), payload);

    auto runtime = std::make_unique<FakeQemuRuntime>();
    FakeQemuRuntime *fake = runtime.get();
    MachineManager::Options options;
    QVERIFY(configureSshOptions(&options, root));
    options.storePath = root + QStringLiteral("/podstack.db");
    options.machinesRoot = root + QStringLiteral("/machines");
    options.imageCacheDir = root + QStringLiteral("/images");
    options.qemuImgBinary = qemuImg;
    options.ovmfCodePath = ovmfCode;
    options.ovmfVarsPath = ovmfVars;
    options.sshPortStart = freePortHint();
    options.runtime = std::move(runtime);
    options.resolveDistro = [image](const QString &, const QString &, PodStack::Error *) {
        return image;
    };

    MachineManager manager(std::move(options));

    CreateMachineRequest request;
    request.name = QStringLiteral("isolated-vm");
    request.distro = QStringLiteral("test");
    request.arch = QStringLiteral("amd64");
    request.user = QStringLiteral("podstack");
    request.diskBytes = 1024LL * 1024LL * 1024LL;
    request.isolated = true;

    PodStack::Error error;
    const auto expectInvalid = [&manager, &request](const CreateMachineRequest &invalid,
                                                    const QString &message) {
        PodStack::Error validateError;
        const bool valid = manager.validate(invalid, &validateError);
        return !valid && validateError.code == QStringLiteral("PODSTACK_INVALID_PARAMS") &&
               !validateError.remediation.isEmpty() && validateError.message.contains(message);
    };

    CreateMachineRequest withMount = request;
    MachineMount mount;
    mount.source = root;
    mount.target = QStringLiteral("/mnt/host");
    withMount.mounts = {mount};
    QVERIFY(expectInvalid(withMount, QStringLiteral("mounts")));

    CreateMachineRequest withAgent = request;
    withAgent.forwardSshAgent = true;
    QVERIFY(expectInvalid(withAgent, QStringLiteral("agent")));

    const int startsBefore = fake->startCount;
    const MachineMeta meta = manager.create(request, {}, {}, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message + QStringLiteral(" ") + error.detail));
    QCOMPARE(meta.state, QStringLiteral("running"));
    QVERIFY(meta.isolated);
    QVERIFY(meta.isolateNetwork);
    QVERIFY(!meta.forwardSshAgent);
    QVERIFY(meta.mounts.isEmpty());
    QCOMPARE(meta.ports.size(), 1);
    QCOMPARE(meta.ports.at(0).hostPort, meta.sshPort);
    QCOMPARE(meta.ports.at(0).guestPort, 22);
    QVERIFY(!meta.ports.at(0).exposeLan);
    QCOMPARE(fake->startCount, startsBefore + 1);
    QVERIFY(fake->lastSpec.isolateNetwork);
    QVERIFY(fake->lastSpec.mounts.isEmpty());
    QCOMPARE(fake->lastSpec.ports.size(), 1);
    QCOMPARE(fake->lastSpec.ports.at(0).hostAddress, QStringLiteral("127.0.0.1"));
    QCOMPARE(static_cast<int>(fake->lastSpec.sshPort), meta.sshPort);

    PodStack::Error qemuError;
    const QStringList args = QemuCommand::arguments(fake->lastSpec, &qemuError);
    QVERIFY2(qemuError.isEmpty(), qPrintable(qemuError.message));
    const QString netdev = valueAfter(args, QStringLiteral("-netdev"));
    QVERIFY(netdev.endsWith(QStringLiteral(",restrict=on")));
    QCOMPARE(netdev.count(QStringLiteral("restrict=on")), 1);
    QCOMPARE(netdev.count(QStringLiteral("hostfwd=")), 1);
    QVERIFY(netdev.contains(QStringLiteral("hostfwd=tcp:127.0.0.1:%1-:22")
                                .arg(meta.sshPort)));
    QVERIFY(!netdev.contains(QStringLiteral("0.0.0.0")));

    const int stopsBeforePorts = fake->stopCount;
    const int startsBeforePorts = fake->startCount;
    MachinePort exposed;
    exposed.hostPort = 8080;
    exposed.guestPort = 80;
    exposed.protocol = QStringLiteral("tcp");
    exposed.exposeLan = true;
    (void)manager.setPorts(meta.id, {exposed}, &error);
    QCOMPARE(error.code, QStringLiteral("PODSTACK_INVALID_PARAMS"));
    QVERIFY(!error.remediation.isEmpty());
    QCOMPARE(fake->stopCount, stopsBeforePorts);
    QCOMPARE(fake->startCount, startsBeforePorts);
    QCOMPARE(manager.get(meta.id, &error).ports.size(), 1);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));

    const MachineMeta clone =
        manager.clone(meta.id, QStringLiteral("isolated-clone"), {}, {}, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message + QStringLiteral(" ") + error.detail));
    QCOMPARE(clone.state, QStringLiteral("stopped"));
    QVERIFY(clone.id != meta.id);
    QVERIFY(clone.isolated);
    QVERIFY(clone.isolateNetwork);
    QVERIFY(!clone.forwardSshAgent);
    QVERIFY(clone.mounts.isEmpty());
    QCOMPARE(clone.ports.size(), 1);

    const QString archive = root + QStringLiteral("/isolated.tar.zst");
    const MachineMeta exported =
        manager.exportMachine(meta.id, archive, false, {}, {}, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message + QStringLiteral(" ") + error.detail));
    QCOMPARE(exported.state, QStringLiteral("running"));
    QVERIFY(QFileInfo(archive).isFile());

    const MachineMeta imported =
        manager.importMachine(archive, QStringLiteral("isolated-copy"), false, {}, {}, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message + QStringLiteral(" ") + error.detail));
    QCOMPARE(imported.state, QStringLiteral("stopped"));
    QVERIFY(imported.id != meta.id);
    QVERIFY(imported.isolated);
    QVERIFY(imported.isolateNetwork);
    QVERIFY(!imported.forwardSshAgent);
    QVERIFY(imported.mounts.isEmpty());
    QCOMPARE(imported.ports.size(), 1);
    QCOMPARE(imported.ports.at(0).guestPort, 22);
    QVERIFY(!imported.ports.at(0).exposeLan);

    QVERIFY2(manager.remove(imported.id, true, &error), qPrintable(error.message));
    QVERIFY2(manager.remove(clone.id, true, &error), qPrintable(error.message));
    QVERIFY2(manager.remove(meta.id, true, &error), qPrintable(error.message));
}

void MachinesTest::machineManagerMounts()
{
    const QString root = QDir::tempPath()
                         + QStringLiteral("/psm-%1").arg(QCoreApplication::applicationPid());
    QDir(root).removeRecursively();
    QVERIFY(QDir().mkpath(root));

    QString ovmfCode;
    QString ovmfVars;
    QVERIFY(createFirmwareFiles(root, &ovmfCode, &ovmfVars));
    const QString qemuImg = createQemuImgScript(root);
    QVERIFY(!qemuImg.isEmpty());
    const QString virtiofsd = createVirtiofsdScript(root);
    QVERIFY(!virtiofsd.isEmpty());

    const QString argsLog = root + QStringLiteral("/virtiofsd-args.log");
    QFile::remove(argsLog);
    qputenv("PODSTACK_VIRTIOFSD_LOG", argsLog.toUtf8());
    qunsetenv("PODSTACK_VIRTIOFSD_FAIL_TAG");

    const QByteArray payload(2048, 'm');
    m_server.setResponse(QStringLiteral("/manager-mounts.qcow2"), 200, payload);
    const DistroImage image = testImage(QStringLiteral("/manager-mounts.qcow2"), payload);

    auto runtime = std::make_unique<FakeQemuRuntime>();
    FakeQemuRuntime *fake = runtime.get();

    MachineManager::Options options;
    QVERIFY(configureSshOptions(&options, root));
    options.storePath = root + QStringLiteral("/podstack.db");
    options.machinesRoot = root + QStringLiteral("/machines");
    options.imageCacheDir = root + QStringLiteral("/images");
    options.qemuImgBinary = qemuImg;
    options.virtiofsdBinary = virtiofsd;
    options.ovmfCodePath = ovmfCode;
    options.ovmfVarsPath = ovmfVars;
    options.sshPortStart = freePortHint();
    options.runtime = std::move(runtime);
    options.resolveDistro =
        [image](const QString &, const QString &, PodStack::Error *) { return image; };

    const QString machinesRoot = options.machinesRoot;
    MachineManager manager(std::move(options));

    const QString sourceA = root + QStringLiteral("/shared-a");
    const QString sourceB = root + QStringLiteral("/shared-b");
    QVERIFY(QDir().mkpath(sourceA));
    QVERIFY(QDir().mkpath(sourceB));
    const QString linkA = root + QStringLiteral("/shared-a-link");
    QVERIFY(QFile::link(sourceA, linkA));
    QVERIFY(QFileInfo(linkA).isSymLink());

    CreateMachineRequest request;
    request.name = QStringLiteral("manager-mounts");
    request.distro = QStringLiteral("test");
    request.arch = QStringLiteral("amd64");
    request.user = QStringLiteral("podstack");
    request.diskBytes = 1024LL * 1024LL * 1024LL;

    MachineMount first;
    first.source = linkA;
    first.target = QStringLiteral("/mnt/podstack/a");
    MachineMount second;
    second.source = sourceB + QLatin1Char('/');
    second.target = QStringLiteral("/mnt/podstack/b/");
    second.readOnly = true;
    second.tag = QStringLiteral("dados");
    request.mounts = {first, second};

    PodStack::Error error;
    QVERIFY2(manager.validate(request, &error), qPrintable(error.message));
    QVERIFY(error.isEmpty());

    const auto expectInvalid = [&manager, &request](const QVector<MachineMount> &mounts) {
        CreateMachineRequest invalid = request;
        invalid.mounts = mounts;
        PodStack::Error validateError;
        QVERIFY(!manager.validate(invalid, &validateError));
        QCOMPARE(validateError.code, QStringLiteral("PODSTACK_INVALID_PARAMS"));
    };

    MachineMount relativeSource;
    relativeSource.source = QStringLiteral("shared-a");
    relativeSource.target = QStringLiteral("/mnt/a");
    expectInvalid({relativeSource});

    MachineMount missingSource;
    missingSource.source = root + QStringLiteral("/missing-source");
    missingSource.target = QStringLiteral("/mnt/a");
    expectInvalid({missingSource});

    MachineMount relativeTarget;
    relativeTarget.source = sourceA;
    relativeTarget.target = QStringLiteral("mnt/a");
    expectInvalid({relativeTarget});

    MachineMount escapingTarget;
    escapingTarget.source = sourceA;
    escapingTarget.target = QStringLiteral("/mnt/../etc");
    expectInvalid({escapingTarget});

    MachineMount customTag;
    customTag.source = sourceA;
    customTag.target = QStringLiteral("/mnt/a");
    customTag.tag = QStringLiteral("Host0");
    expectInvalid({customTag});

    MachineMount duplicateA;
    duplicateA.source = sourceA;
    duplicateA.target = QStringLiteral("/mnt/a");
    duplicateA.tag = QStringLiteral("dados");
    MachineMount duplicateB;
    duplicateB.source = sourceB;
    duplicateB.target = QStringLiteral("/mnt/b");
    duplicateB.tag = QStringLiteral("dados");
    expectInvalid({duplicateA, duplicateB});

    QVector<MachineMount> tooMany;
    for (int i = 0; i < 9; ++i) {
        MachineMount mount;
        mount.source = sourceA;
        mount.target = QStringLiteral("/mnt/%1").arg(i);
        tooMany.append(mount);
    }
    expectInvalid(tooMany);

    const MachineMeta meta = manager.create(request, {}, {}, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message + QStringLiteral(" ") + error.detail));
    QVERIFY(!meta.id.isEmpty());
    QCOMPARE(meta.state, QStringLiteral("running"));
    QCOMPARE(meta.mounts.size(), 2);

    const QString directory = machinesRoot + QLatin1Char('/') + meta.id;
    const QString socketA = directory + QStringLiteral("/virtiofs-host0.sock");
    const QString socketB = directory + QStringLiteral("/virtiofs-dados.sock");

    const auto mountByTarget = [&meta](const QString &target) {
        for (const MachineMount &mount : meta.mounts) {
            if (mount.target == target) {
                return mount;
            }
        }
        return MachineMount{};
    };
    const MachineMount storedA = mountByTarget(QStringLiteral("/mnt/podstack/a"));
    const MachineMount storedB = mountByTarget(QStringLiteral("/mnt/podstack/b"));
    QCOMPARE(storedA.tag, QStringLiteral("host0"));
    QCOMPARE(storedA.source, QFileInfo(sourceA).canonicalFilePath());
    QVERIFY(!storedA.id.isEmpty());
    QVERIFY(!storedA.readOnly);
    QCOMPARE(storedB.tag, QStringLiteral("dados"));
    QCOMPARE(storedB.source, QFileInfo(sourceB).canonicalFilePath());
    QVERIFY(!storedB.id.isEmpty());
    QVERIFY(storedB.readOnly);

    QVERIFY(QFileInfo::exists(socketA));
    QVERIFY(QFileInfo::exists(socketB));

    QHash<QString, QemuMount> specMounts;
    for (const QemuMount &mount : fake->lastSpec.mounts) {
        specMounts.insert(mount.tag, mount);
    }
    QCOMPARE(specMounts.size(), 2);
    QCOMPARE(specMounts.value(QStringLiteral("host0")).socketPath, socketA);
    QCOMPARE(specMounts.value(QStringLiteral("dados")).socketPath, socketB);

    const qint64 pidA = readPidFile(socketA + QStringLiteral(".pid"));
    const qint64 pidB = readPidFile(socketB + QStringLiteral(".pid"));
    QVERIFY(pidA > 0);
    QVERIFY(pidB > 0);
    QVERIFY(processAlive(pidA));
    QVERIFY(processAlive(pidB));

    QFile argsFile(argsLog);
    QVERIFY(argsFile.open(QIODevice::ReadOnly));
    const QStringList args = QString::fromUtf8(argsFile.readAll())
                                 .split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    QCOMPARE(args.count(QStringLiteral("--shared-dir")), 2);
    QCOMPARE(args.count(QStringLiteral("--socket-path")), 2);
    QCOMPARE(args.count(QStringLiteral("--tag")), 2);
    QCOMPARE(args.count(QStringLiteral("--cache=auto")), 2);
    QCOMPARE(args.count(QStringLiteral("--xattr")), 2);
    QCOMPARE(args.count(QStringLiteral("--readonly")), 1);
    QVERIFY(args.contains(QStringLiteral("host0")));
    QVERIFY(args.contains(QStringLiteral("dados")));
    QVERIFY(args.contains(socketA));
    QVERIFY(args.contains(socketB));
    QVERIFY(args.contains(QStringLiteral("--sandbox=chroot"))
            || args.contains(QStringLiteral("--sandbox=namespace")));

    const MachineMeta stopped = manager.stop(meta.id, false, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QCOMPARE(stopped.state, QStringLiteral("stopped"));
    QVERIFY(waitForProcessGone(pidA));
    QVERIFY(waitForProcessGone(pidB));
    QVERIFY(!QFileInfo::exists(socketA));
    QVERIFY(!QFileInfo::exists(socketB));
    QCOMPARE(fake->stopCount, 1);

    const MachineMeta started = manager.start(meta.id, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message + QStringLiteral(" ") + error.detail));
    QCOMPARE(started.state, QStringLiteral("running"));
    QVERIFY(QFileInfo::exists(socketA));
    QVERIFY(QFileInfo::exists(socketB));
    const qint64 restartedPidA = readPidFile(socketA + QStringLiteral(".pid"));
    const qint64 restartedPidB = readPidFile(socketB + QStringLiteral(".pid"));
    QVERIFY(restartedPidA > 0);
    QVERIFY(restartedPidB > 0);
    QVERIFY(processAlive(restartedPidA));
    QVERIFY(processAlive(restartedPidB));
    QCOMPARE(fake->startCount, 2);

    // Sem processo QEMU, a reconciliação marca a machine como stopped e
    // encerra o virtiofsd que este manager iniciou.
    fake->running = false;
    manager.reconcile(&error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QVERIFY(waitForProcessGone(restartedPidA));
    QVERIFY(waitForProcessGone(restartedPidB));
    QVERIFY(!QFileInfo::exists(socketA));
    QVERIFY(!QFileInfo::exists(socketB));
    QCOMPARE(manager.get(meta.id, &error).state, QStringLiteral("stopped"));

    QVERIFY2(manager.remove(meta.id, true, &error), qPrintable(error.message));
    QVERIFY(!QFileInfo::exists(directory));

    // Falha no segundo virtiofsd: o primeiro precisa ser encerrado e os
    // sockets removidos antes de a criação terminar em erro.
    const QString failRoot = root + QStringLiteral("-fail");
    QDir(failRoot).removeRecursively();
    QVERIFY(QDir().mkpath(failRoot));
    QString failCode;
    QString failVars;
    QVERIFY(createFirmwareFiles(failRoot, &failCode, &failVars));
    const QString failQemuImg = createQemuImgScript(failRoot);
    const QString failVirtiofsd = createVirtiofsdScript(failRoot);
    QVERIFY(!failQemuImg.isEmpty());
    QVERIFY(!failVirtiofsd.isEmpty());
    qputenv("PODSTACK_VIRTIOFSD_FAIL_TAG", QByteArrayLiteral("host1"));

    auto failRuntime = std::make_unique<FakeQemuRuntime>();
    FakeQemuRuntime *failFake = failRuntime.get();
    MachineManager::Options failOptions;
    QVERIFY(configureSshOptions(&failOptions, failRoot));
    failOptions.storePath = failRoot + QStringLiteral("/podstack.db");
    failOptions.machinesRoot = failRoot + QStringLiteral("/machines");
    failOptions.imageCacheDir = failRoot + QStringLiteral("/images");
    failOptions.qemuImgBinary = failQemuImg;
    failOptions.virtiofsdBinary = failVirtiofsd;
    failOptions.ovmfCodePath = failCode;
    failOptions.ovmfVarsPath = failVars;
    failOptions.sshPortStart = freePortHint();
    failOptions.runtime = std::move(failRuntime);
    failOptions.resolveDistro =
        [image](const QString &, const QString &, PodStack::Error *) { return image; };
    MachineManager failManager(std::move(failOptions));

    const QString failSource = failRoot + QStringLiteral("/shared");
    QVERIFY(QDir().mkpath(failSource));
    CreateMachineRequest failRequest;
    failRequest.name = QStringLiteral("manager-mounts-fail");
    failRequest.distro = QStringLiteral("test");
    failRequest.arch = QStringLiteral("amd64");
    failRequest.diskBytes = 1024LL * 1024LL * 1024LL;
    MachineMount okMount;
    okMount.source = failSource;
    okMount.target = QStringLiteral("/mnt/ok");
    okMount.tag = QStringLiteral("host0");
    MachineMount failingMount;
    failingMount.source = failSource;
    failingMount.target = QStringLiteral("/mnt/fail");
    failingMount.tag = QStringLiteral("host1");
    failRequest.mounts = {okMount, failingMount};

    PodStack::Error failError;
    const MachineMeta failed = failManager.create(failRequest, {}, {}, &failError);
    QVERIFY(!failed.id.isEmpty());
    QCOMPARE(failed.state, QStringLiteral("error"));
    QVERIFY(!failError.isEmpty());
    QVERIFY(!failed.error.isEmpty());
    QCOMPARE(failFake->startCount, 0);

    const QString failDirectory = failRoot + QStringLiteral("/machines/") + failed.id;
    const QString failSocket = failDirectory + QStringLiteral("/virtiofs-host0.sock");
    QVERIFY(!QFileInfo::exists(failSocket));
    const qint64 failPid = readPidFile(failSocket + QStringLiteral(".pid"));
    if (failPid > 0) {
        QVERIFY(waitForProcessGone(failPid));
    }

    qunsetenv("PODSTACK_VIRTIOFSD_LOG");
    qunsetenv("PODSTACK_VIRTIOFSD_FAIL_TAG");
}

void MachinesTest::machineManagerValidation()
{
    const QString root = m_root + QStringLiteral("/manager-validation");
    QDir(root).removeRecursively();
    QVERIFY(QDir().mkpath(root));

    QString ovmfCode;
    QString ovmfVars;
    QVERIFY(createFirmwareFiles(root, &ovmfCode, &ovmfVars));
    const QString qemuImg = createQemuImgScript(root);
    QVERIFY(!qemuImg.isEmpty());

    const QByteArray payload(2048, 'v');
    m_server.setResponse(QStringLiteral("/manager-validation.qcow2"), 200, payload);
    const DistroImage image = testImage(QStringLiteral("/manager-validation.qcow2"), payload);

    auto runtime = std::make_unique<FakeQemuRuntime>();
    MachineManager::Options options;
    QVERIFY(configureSshOptions(&options, root));
    options.storePath = root + QStringLiteral("/podstack.db");
    options.machinesRoot = root + QStringLiteral("/machines");
    options.imageCacheDir = root + QStringLiteral("/images");
    options.qemuImgBinary = qemuImg;
    options.ovmfCodePath = ovmfCode;
    options.ovmfVarsPath = ovmfVars;
    options.sshPortStart = freePortHint();
    options.runtime = std::move(runtime);
    options.resolveDistro = [image](const QString &spec, const QString &, PodStack::Error *error) {
        if (spec == QStringLiteral("test")) {
            return image;
        }
        if (error) {
            *error = PodStack::Error::make(QStringLiteral("PODSTACK_INVALID_PARAMS"),
                                           QStringLiteral("Distro não suportada."), spec);
        }
        return DistroImage{};
    };

    const QString machinesRoot = options.machinesRoot;
    const QString storePath = options.storePath;
    MachineManager manager(std::move(options));

    CreateMachineRequest base;
    base.name = QStringLiteral("valid-name");
    base.distro = QStringLiteral("test");
    base.arch = QStringLiteral("amd64");
    base.user = QStringLiteral("podstack");
    base.diskBytes = 1024LL * 1024LL * 1024LL;

    PodStack::Error validateError;
    QVERIFY(manager.validate(base, &validateError));
    QVERIFY(validateError.isEmpty());
    CreateMachineRequest invalidCpus = base;
    invalidCpus.cpus = 0;
    QVERIFY(!manager.validate(invalidCpus, &validateError));
    QCOMPARE(validateError.code, QStringLiteral("PODSTACK_INVALID_PARAMS"));

    const auto expectInvalid = [&manager](CreateMachineRequest request, const QString &code) {
        PodStack::Error error;
        const MachineMeta meta = manager.create(request, {}, {}, &error);
        QVERIFY2(meta.id.isEmpty(), qPrintable(request.name));
        QCOMPARE(error.code, code);
    };

    CreateMachineRequest request = base;
    request.name.clear();
    expectInvalid(request, QStringLiteral("PODSTACK_INVALID_PARAMS"));

    const QStringList invalidNames{
        QStringLiteral("Bad-Name"), QStringLiteral("bad_name"),    QStringLiteral("-bad"),
        QStringLiteral("bad-"),     QString(64, QLatin1Char('a')),
    };
    for (const QString &name : invalidNames) {
        request = base;
        request.name = name;
        expectInvalid(request, QStringLiteral("PODSTACK_INVALID_PARAMS"));
    }

    request = base;
    request.cpus = 0;
    expectInvalid(request, QStringLiteral("PODSTACK_INVALID_PARAMS"));

    request = base;
    request.cpus = 257;
    expectInvalid(request, QStringLiteral("PODSTACK_INVALID_PARAMS"));

    request = base;
    request.memoryBytes = 1024LL * 1024LL * 1024LL - 1;
    expectInvalid(request, QStringLiteral("PODSTACK_INVALID_PARAMS"));

    request = base;
    request.diskBytes = 1024LL * 1024LL * 1024LL - 1;
    expectInvalid(request, QStringLiteral("PODSTACK_INVALID_PARAMS"));

    request = base;
    request.arch = QStringLiteral("riscv64");
    expectInvalid(request, QStringLiteral("PODSTACK_INVALID_PARAMS"));

    request = base;
    request.distro.clear();
    expectInvalid(request, QStringLiteral("PODSTACK_INVALID_PARAMS"));

    request = base;
    request.name = QStringLiteral("unknown-distro");
    request.distro = QStringLiteral("gentoo");
    expectInvalid(request, QStringLiteral("PODSTACK_INVALID_PARAMS"));

    QVERIFY(!QFileInfo::exists(machinesRoot) ||
            QDir(machinesRoot).entryList(QDir::AllEntries | QDir::NoDotAndDotDot).isEmpty());
    QVERIFY(!QFileInfo::exists(storePath));

    PodStack::Error error;
    const MachineMeta created = manager.create(base, {}, {}, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QCOMPARE(created.state, QStringLiteral("running"));

    const MachineMeta duplicate = manager.create(base, {}, {}, &error);
    QVERIFY(duplicate.id.isEmpty());
    QCOMPARE(error.code, QStringLiteral("PODSTACK_CONFLICT"));
    QCOMPARE(manager.list(&error).size(), 1);
}

void MachinesTest::machineManagerErrors()
{
    const QString root = m_root + QStringLiteral("/manager-errors");
    QDir(root).removeRecursively();
    QVERIFY(QDir().mkpath(root));

    QString ovmfCode;
    QString ovmfVars;
    QVERIFY(createFirmwareFiles(root, &ovmfCode, &ovmfVars));
    const QString qemuImg = createQemuImgScript(root);
    QVERIFY(!qemuImg.isEmpty());

    const QByteArray payload(2048, 'e');
    m_server.setResponse(QStringLiteral("/manager-errors.qcow2"), 200, payload);
    const DistroImage image = testImage(QStringLiteral("/manager-errors.qcow2"), payload);

    auto runtime = std::make_unique<FakeQemuRuntime>();
    MachineManager::Options options;
    QVERIFY(configureSshOptions(&options, root));
    options.storePath = root + QStringLiteral("/podstack.db");
    options.machinesRoot = root + QStringLiteral("/machines");
    options.imageCacheDir = root + QStringLiteral("/images");
    options.qemuImgBinary = qemuImg;
    options.ovmfCodePath = ovmfCode;
    options.ovmfVarsPath = ovmfVars;
    options.sshPortStart = freePortHint();
    options.runtime = std::move(runtime);
    options.resolveDistro = [image](const QString &spec, const QString &, PodStack::Error *error) {
        if (spec == QStringLiteral("test")) {
            return image;
        }
        if (error) {
            *error = PodStack::Error::make(QStringLiteral("PODSTACK_INVALID_PARAMS"),
                                           QStringLiteral("Distro não suportada."), spec);
        }
        return DistroImage{};
    };

    MachineManager manager(std::move(options));

    CreateMachineRequest base;
    base.name = QStringLiteral("errors-machine");
    base.distro = QStringLiteral("test");
    base.arch = QStringLiteral("amd64");
    base.user = QStringLiteral("podstack");
    base.diskBytes = 1024LL * 1024LL * 1024LL;

    PodStack::Error error;
    MachineMeta created = manager.create(base, {}, {}, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QVERIFY(!created.id.isEmpty());

    const QString missing = QStringLiteral("0190f0f0-0000-7000-8000-00000000dead");
    const auto expectMissing = [&](auto call) {
        error = {};
        const MachineMeta result = call();
        QVERIFY(result.id.isEmpty());
        QCOMPARE(error.code, QStringLiteral("PODSTACK_NOT_FOUND"));
    };
    expectMissing([&] { return manager.start(missing, &error); });
    expectMissing([&] { return manager.stop(missing, false, &error); });
    expectMissing([&] { return manager.restart(missing, &error); });
    expectMissing([&] { return manager.update(missing, {}, &error); });
    expectMissing([&] { return manager.setPorts(missing, {}, &error); });
    expectMissing([&] { return manager.rename(missing, QStringLiteral("new-name"), &error); });
    expectMissing(
        [&] { return manager.clone(missing, QStringLiteral("new-clone"), {}, {}, &error); });
    error = {};
    const MachineSnapshot missingSnapshot =
        manager.createSnapshot(missing, QStringLiteral("snap"), &error);
    QVERIFY(missingSnapshot.id.isEmpty());
    QCOMPARE(error.code, QStringLiteral("PODSTACK_NOT_FOUND"));
    expectMissing([&] { return manager.restoreSnapshot(missing, QStringLiteral("snap"), &error); });
    expectMissing([&] {
        return manager.exportMachine(missing, root + QStringLiteral("/x.tar.zst"), false, {}, {},
                                     &error);
    });

    error = {};
    QVERIFY(!manager.removeSnapshot(missing, QStringLiteral("snap"), &error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_NOT_FOUND"));

    error = {};
    QVERIFY(!manager.sshInfo(missing, nullptr, &error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_INVALID_PARAMS"));

    SshInfo info;
    error = {};
    QVERIFY(!manager.sshInfo(missing, &info, &error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_NOT_FOUND"));

    error = {};
    QVERIFY(!manager.setDefaultMachine(missing, &error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_NOT_FOUND"));

    // Recursos inválidos.
    UpdateMachineRequest update;
    update.cpus = 0;
    manager.update(created.id, update, &error);
    QCOMPARE(error.code, QStringLiteral("PODSTACK_INVALID_PARAMS"));
    update = {};
    update.memoryBytes = 1024LL;
    error = {};
    manager.update(created.id, update, &error);
    QCOMPARE(error.code, QStringLiteral("PODSTACK_INVALID_PARAMS"));
    update = {};
    update.diskBytes = 1024LL;
    error = {};
    manager.update(created.id, update, &error);
    QCOMPARE(error.code, QStringLiteral("PODSTACK_INVALID_PARAMS"));

    // Portas inválidas.
    MachinePort port;
    port.id = QStringLiteral("port-error");
    port.hostPort = 0;
    port.guestPort = 80;
    port.protocol = QStringLiteral("tcp");
    error = {};
    manager.setPorts(created.id, {port}, &error);
    QCOMPARE(error.code, QStringLiteral("PODSTACK_INVALID_PARAMS"));

    // Nome inválido e snapshot inválido.
    error = {};
    manager.rename(created.id, QStringLiteral("Bad Name"), &error);
    QCOMPARE(error.code, QStringLiteral("PODSTACK_INVALID_PARAMS"));
    error = {};
    QVERIFY(manager.createSnapshot(created.id, QStringLiteral("bad name"), &error).id.isEmpty());
    QCOMPARE(error.code, QStringLiteral("PODSTACK_INVALID_PARAMS"));

    // Export/import com caminhos inválidos.
    error = {};
    manager.exportMachine(created.id, QString(), false, {}, {}, &error);
    QCOMPARE(error.code, QStringLiteral("PODSTACK_INVALID_PARAMS"));
    error = {};
    QVERIFY(
        manager.importMachine(root + QStringLiteral("/missing.tar.zst"), {}, false, {}, {}, &error)
            .id.isEmpty());
    QCOMPARE(error.code, QStringLiteral("PODSTACK_INVALID_PARAMS"));

    // SSH com a chave gerada na criação.
    error = {};
    QVERIFY2(manager.sshInfo(created.id, &info, &error), qPrintable(error.message));
    QCOMPARE(info.user, QStringLiteral("podstack"));
    QVERIFY(!info.identityFile.isEmpty());

    // Default é persistida por nome.
    error = {};
    QVERIFY2(manager.setDefaultMachine(created.id, &error), qPrintable(error.message));
    QCOMPARE(manager.defaultMachine(&error), created.name);

    // Remover também encerra a machine em execução.
    error = {};
    QVERIFY2(manager.remove(created.id, true, &error), qPrintable(error.message));
    QVERIFY(manager.get(created.id, &error).id.isEmpty());
}

void MachinesTest::machineManagerQemuImgFailures()
{
    const QString root = m_root + QStringLiteral("/manager-qemu-img-fail");
    QDir(root).removeRecursively();
    QVERIFY(QDir().mkpath(root));

    QString ovmfCode;
    QString ovmfVars;
    QVERIFY(createFirmwareFiles(root, &ovmfCode, &ovmfVars));

    const QString script = root + QStringLiteral("/qemu-img-fail.sh");
    QVERIFY(writeTestFile(
        script,
        QByteArrayLiteral(
            "#!/bin/sh\n"
            "if [ \"$1\" = 'info' ]; then printf '%s' '{\"snapshots\":[]}'; exit 0; fi\n"
            "if [ \"$1\" = 'create' ]; then\n"
            "  eval \"disk=\\${$(( $# - 1 ))}\"\n"
            "  printf 'fake-qcow2' > \"$disk\"\n"
            "  exit 0\n"
            "fi\n"
            "echo 'qemu-img falhou de propósito' >&2\n"
            "exit 1\n"),
        QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner));

    const QByteArray payload(2048, 'f');
    m_server.setResponse(QStringLiteral("/manager-qemu-img-fail.qcow2"), 200, payload);
    const DistroImage image = testImage(QStringLiteral("/manager-qemu-img-fail.qcow2"), payload);

    auto runtime = std::make_unique<FakeQemuRuntime>();
    MachineManager::Options options;
    QVERIFY(configureSshOptions(&options, root));
    options.storePath = root + QStringLiteral("/podstack.db");
    options.machinesRoot = root + QStringLiteral("/machines");
    options.imageCacheDir = root + QStringLiteral("/images");
    options.qemuImgBinary = script;
    options.ovmfCodePath = ovmfCode;
    options.ovmfVarsPath = ovmfVars;
    options.sshPortStart = freePortHint();
    options.runtime = std::move(runtime);
    options.resolveDistro = [image](const QString &, const QString &, PodStack::Error *error) {
        if (error) {
            *error = {};
        }
        return image;
    };

    MachineManager manager(std::move(options));
    CreateMachineRequest base;
    base.name = QStringLiteral("qemu-img-fail");
    base.distro = QStringLiteral("test");
    base.arch = QStringLiteral("amd64");
    base.user = QStringLiteral("podstack");
    base.diskBytes = 1024LL * 1024LL * 1024LL;

    PodStack::Error error;
    MachineMeta created = manager.create(base, {}, {}, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));

    // resize falha: o erro do qemu-img precisa chegar ao metadado.
    UpdateMachineRequest update;
    update.diskBytes = created.diskBytes * 2;
    manager.update(created.id, update, &error);
    QCOMPARE(error.code, QStringLiteral("PODSTACK_IO_ERROR"));

    // convert falha: export não deixa um arquivo válido para trás.
    error = {};
    const QString archive = root + QStringLiteral("/export.tar.zst");
    manager.exportMachine(created.id, archive, false, {}, {}, &error);
    QCOMPARE(error.code, QStringLiteral("PODSTACK_IO_ERROR"));
    QVERIFY(!QFileInfo::exists(archive));

    // create falha em outro manager: nenhum metadado é persistido.
    QDir(root + QStringLiteral("/fail-create")).removeRecursively();
    QVERIFY(QDir().mkpath(root + QStringLiteral("/fail-create")));
    const QString failScript = root + QStringLiteral("/fail-create/qemu-img.sh");
    QVERIFY(
        writeTestFile(failScript, QByteArrayLiteral("#!/bin/sh\nexit 1\n"),
                      QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner));
    auto failRuntime = std::make_unique<FakeQemuRuntime>();
    MachineManager::Options failOptions;
    QVERIFY(configureSshOptions(&failOptions, root + QStringLiteral("/fail-create")));
    failOptions.storePath = root + QStringLiteral("/fail-create/podstack.db");
    failOptions.machinesRoot = root + QStringLiteral("/fail-create/machines");
    failOptions.imageCacheDir = root + QStringLiteral("/fail-create/images");
    failOptions.qemuImgBinary = failScript;
    failOptions.ovmfCodePath = ovmfCode;
    failOptions.ovmfVarsPath = ovmfVars;
    failOptions.sshPortStart = freePortHint();
    failOptions.runtime = std::move(failRuntime);
    failOptions.resolveDistro = [image](const QString &, const QString &,
                                        PodStack::Error *distroError) {
        if (distroError) {
            *distroError = {};
        }
        return image;
    };
    MachineManager failManager(std::move(failOptions));
    base.name = QStringLiteral("qemu-img-create-fail");
    error = {};
    QVERIFY(failManager.create(base, {}, {}, &error).id.isEmpty());
    QCOMPARE(error.code, QStringLiteral("PODSTACK_IO_ERROR"));
}

void MachinesTest::machineManagerDiskErrors()
{
    const QString root = m_root + QStringLiteral("/manager-disk-errors");
    QDir(root).removeRecursively();
    QVERIFY(QDir().mkpath(root));

    QString ovmfCode;
    QString ovmfVars;
    QVERIFY(createFirmwareFiles(root, &ovmfCode, &ovmfVars));

    const QByteArray payload(2048, 'd');
    m_server.setResponse(QStringLiteral("/manager-disk-errors.qcow2"), 200, payload);
    const DistroImage image = testImage(QStringLiteral("/manager-disk-errors.qcow2"), payload);

    const auto runCreate = [&](const QString &name, const QString &qemuImg) -> PodStack::Error {
        const QString caseRoot = root + QLatin1Char('/') + name;
        QDir(caseRoot).removeRecursively();
        if (!QDir().mkpath(caseRoot)) {
            return PodStack::Error::make(QStringLiteral("TEST"), QStringLiteral("mkpath"));
        }
        auto runtime = std::make_unique<FakeQemuRuntime>();
        MachineManager::Options options;
        options.storePath = caseRoot + QStringLiteral("/podstack.db");
        options.machinesRoot = caseRoot + QStringLiteral("/machines");
        options.imageCacheDir = root + QStringLiteral("/images");
        options.qemuImgBinary = qemuImg;
        if (!configureSshOptions(&options, caseRoot)) {
            return PodStack::Error::make(QStringLiteral("TEST"), QStringLiteral("ssh options"));
        }
        options.ovmfCodePath = ovmfCode;
        options.ovmfVarsPath = ovmfVars;
        options.sshPortStart = freePortHint();
        options.runtime = std::move(runtime);
        options.resolveDistro = [image](const QString &, const QString &,
                                        PodStack::Error *distroError) {
            if (distroError) {
                *distroError = {};
            }
            return image;
        };
        MachineManager manager(std::move(options));
        CreateMachineRequest request;
        request.name = name;
        request.distro = QStringLiteral("test");
        request.arch = QStringLiteral("amd64");
        request.user = QStringLiteral("podstack");
        request.diskBytes = 1024LL * 1024LL * 1024LL;
        PodStack::Error error;
        manager.create(request, {}, {}, &error);
        return error;
    };

    const PodStack::Error missingBinary = runCreate(
        QStringLiteral("disk-missing-binary"), root + QStringLiteral("/does-not-exist/qemu-img"));
    QCOMPARE(missingBinary.code, QStringLiteral("PODSTACK_IO_ERROR"));

    const QString noOutput = root + QStringLiteral("/qemu-img-no-output.sh");
    QVERIFY(
        writeTestFile(noOutput, QByteArrayLiteral("#!/bin/sh\nexit 0\n"),
                      QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner));
    const PodStack::Error noDisk = runCreate(QStringLiteral("disk-no-output"), noOutput);
    QCOMPARE(noDisk.code, QStringLiteral("PODSTACK_IO_ERROR"));
    QVERIFY(noDisk.message.contains(QStringLiteral("não foi criado")) ||
            noDisk.detail.contains(QStringLiteral("disco")));
}

void MachinesTest::machineManagerSshKeyFailures()
{
    const QString root = m_root + QStringLiteral("/manager-ssh-key-fail");
    QDir(root).removeRecursively();
    QVERIFY(QDir().mkpath(root));

    QString ovmfCode;
    QString ovmfVars;
    QVERIFY(createFirmwareFiles(root, &ovmfCode, &ovmfVars));
    const QString qemuImg = createQemuImgScript(root);
    QVERIFY(!qemuImg.isEmpty());

    const QByteArray payload(2048, 'k');
    m_server.setResponse(QStringLiteral("/manager-ssh-key-fail.qcow2"), 200, payload);
    const DistroImage image = testImage(QStringLiteral("/manager-ssh-key-fail.qcow2"), payload);

    const auto runCase = [&](const QString &name, const QByteArray &script,
                             const QString &binaryOverride) -> PodStack::Error {
        const QString caseRoot = root + QLatin1Char('/') + name;
        QDir(caseRoot).removeRecursively();
        if (!QDir().mkpath(caseRoot)) {
            return PodStack::Error::make(QStringLiteral("TEST"), QStringLiteral("mkpath"));
        }
        QString keygen = binaryOverride;
        if (!script.isEmpty()) {
            keygen = caseRoot + QStringLiteral("/ssh-keygen.sh");
            if (!writeTestFile(keygen, script,
                               QFileDevice::ReadOwner | QFileDevice::WriteOwner |
                                   QFileDevice::ExeOwner)) {
                return PodStack::Error::make(QStringLiteral("TEST"),
                                             QStringLiteral("write script"));
            }
        }

        auto runtime = std::make_unique<FakeQemuRuntime>();
        MachineManager::Options options;
        options.storePath = caseRoot + QStringLiteral("/podstack.db");
        options.machinesRoot = caseRoot + QStringLiteral("/machines");
        options.imageCacheDir = root + QStringLiteral("/images");
        options.qemuImgBinary = qemuImg;
        options.sshKeygenBinary = keygen;
        options.sshConfigPath = caseRoot + QStringLiteral("/ssh/config");
        options.ovmfCodePath = ovmfCode;
        options.ovmfVarsPath = ovmfVars;
        options.sshPortStart = freePortHint();
        options.runtime = std::move(runtime);
        options.resolveDistro = [image](const QString &, const QString &,
                                        PodStack::Error *distroError) {
            if (distroError) {
                *distroError = {};
            }
            return image;
        };

        MachineManager manager(std::move(options));
        CreateMachineRequest request;
        request.name = name;
        request.distro = QStringLiteral("test");
        request.arch = QStringLiteral("amd64");
        request.user = QStringLiteral("podstack");
        request.diskBytes = 1024LL * 1024LL * 1024LL;
        PodStack::Error error;
        const MachineMeta created = manager.create(request, {}, {}, &error);
        if (!created.id.isEmpty()) {
            return PodStack::Error::make(QStringLiteral("TEST"),
                                         QStringLiteral("create deveria falhar"));
        }
        return error;
    };

    QCOMPARE(runCase(QStringLiteral("keygen-fails"),
                     QByteArrayLiteral("#!/bin/sh\necho falha >&2\nexit 1\n"), {})
                 .code,
             QStringLiteral("PODSTACK_IO_ERROR"));
    QCOMPARE(
        runCase(QStringLiteral("keygen-empty"), QByteArrayLiteral("#!/bin/sh\nexit 0\n"), {}).code,
        QStringLiteral("PODSTACK_IO_ERROR"));
    QCOMPARE(runCase(QStringLiteral("keygen-empty-public"),
                     QByteArrayLiteral("#!/bin/sh\n"
                                       "key=''\n"
                                       "for last in \"$@\"; do key=\"$last\"; done\n"
                                       "printf 'private' > \"$key\"\n"
                                       "printf '' > \"$key.pub\"\n"
                                       "exit 0\n"),
                     {})
                 .code,
             QStringLiteral("PODSTACK_IO_ERROR"));
    QCOMPARE(runCase(QStringLiteral("keygen-missing"), QByteArrayLiteral(""),
                     root + QStringLiteral("/does-not-exist/ssh-keygen"))
                 .code,
             QStringLiteral("PODSTACK_IO_ERROR"));
}

void MachinesTest::machineManagerStateErrors()
{
    const QString root = m_root + QStringLiteral("/manager-state-errors");
    QDir(root).removeRecursively();
    QVERIFY(QDir().mkpath(root));

    QString ovmfCode;
    QString ovmfVars;
    QVERIFY(createFirmwareFiles(root, &ovmfCode, &ovmfVars));
    const QString qemuImg = createQemuImgScript(root);
    QVERIFY(!qemuImg.isEmpty());

    const QByteArray payload(2048, 's');
    m_server.setResponse(QStringLiteral("/manager-state-errors.qcow2"), 200, payload);
    const DistroImage image = testImage(QStringLiteral("/manager-state-errors.qcow2"), payload);

    auto runtime = std::make_unique<FakeQemuRuntime>();
    MachineManager::Options options;
    QVERIFY(configureSshOptions(&options, root));
    options.storePath = root + QStringLiteral("/podstack.db");
    options.machinesRoot = root + QStringLiteral("/machines");
    options.imageCacheDir = root + QStringLiteral("/images");
    options.qemuImgBinary = qemuImg;
    options.ovmfCodePath = ovmfCode;
    options.ovmfVarsPath = ovmfVars;
    options.sshPortStart = freePortHint();
    options.runtime = std::move(runtime);
    options.resolveDistro = [image](const QString &, const QString &, PodStack::Error *error) {
        if (error) {
            *error = {};
        }
        return image;
    };

    const QString storePath = options.storePath;
    MachineManager manager(std::move(options));

    CreateMachineRequest base;
    base.name = QStringLiteral("state-errors");
    base.distro = QStringLiteral("test");
    base.arch = QStringLiteral("amd64");
    base.user = QStringLiteral("podstack");
    base.diskBytes = 1024LL * 1024LL * 1024LL;

    PodStack::Error error;
    const MachineMeta created = manager.create(base, {}, {}, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));

    const auto setState = [&](const QString &state) {
        MachineStore store(storePath);
        PodStack::Error storeError;
        if (!store.open(&storeError)) {
            return false;
        }
        MachineMeta meta = manager.get(created.id, &storeError);
        meta.state = state;
        return store.upsert(meta, &storeError);
    };

    for (const QString &state : {QStringLiteral("deleting"), QStringLiteral("deleted")}) {
        QVERIFY(setState(state));

        error = {};
        manager.start(created.id, &error);
        QCOMPARE(error.code, QStringLiteral("PODSTACK_CONFLICT"));

        error = {};
        manager.stop(created.id, false, &error);
        QCOMPARE(error.code, QStringLiteral("PODSTACK_CONFLICT"));

        UpdateMachineRequest update;
        update.cpus = 4;
        error = {};
        manager.update(created.id, update, &error);
        QCOMPARE(error.code, QStringLiteral("PODSTACK_CONFLICT"));

        error = {};
        manager.setPorts(created.id, {}, &error);
        QCOMPARE(error.code, QStringLiteral("PODSTACK_CONFLICT"));

        error = {};
        QVERIFY(!manager.remove(created.id, false, &error));
        QCOMPARE(error.code, QStringLiteral("PODSTACK_CONFLICT"));

        error = {};
        manager.exportMachine(created.id, root + QStringLiteral("/state.tar.zst"), false, {}, {},
                              &error);
        QCOMPARE(error.code, QStringLiteral("PODSTACK_CONFLICT"));

        error = {};
        manager.rename(created.id, QStringLiteral("state-new"), &error);
        QCOMPARE(error.code, QStringLiteral("PODSTACK_CONFLICT"));

        error = {};
        manager.clone(created.id, QStringLiteral("state-clone"), {}, {}, &error);
        QCOMPARE(error.code, QStringLiteral("PODSTACK_CONFLICT"));
    }

    QVERIFY(setState(QStringLiteral("stopped")));

    // Chave SSH ausente.
    QFile::remove(created.sshKeyPath);
    SshInfo info;
    error = {};
    QVERIFY(!manager.sshInfo(created.id, &info, &error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_IO_ERROR"));

    // Conflito de porta com a própria SSH já publicada.
    MachinePort conflicting;
    conflicting.id = QStringLiteral("conflict-port");
    conflicting.hostPort = created.sshPort;
    conflicting.guestPort = 8080;
    conflicting.protocol = QStringLiteral("tcp");
    error = {};
    manager.setPorts(created.id, {conflicting}, &error);
    QCOMPARE(error.code, QStringLiteral("PODSTACK_CONFLICT"));

    // Nome duplicado na criação.
    base.name = created.name;
    error = {};
    QVERIFY(manager.create(base, {}, {}, &error).id.isEmpty());
    QCOMPARE(error.code, QStringLiteral("PODSTACK_CONFLICT"));
}

void MachinesTest::machineManagerRuntimeErrors()
{
    const QString root = m_root + QStringLiteral("/manager-runtime-errors");
    QDir(root).removeRecursively();
    QVERIFY(QDir().mkpath(root));

    QString ovmfCode;
    QString ovmfVars;
    QVERIFY(createFirmwareFiles(root, &ovmfCode, &ovmfVars));
    const QString qemuImg = createQemuImgScript(root);
    QVERIFY(!qemuImg.isEmpty());

    const QByteArray payload(2048, 'r');
    m_server.setResponse(QStringLiteral("/manager-runtime-errors.qcow2"), 200, payload);
    const DistroImage image = testImage(QStringLiteral("/manager-runtime-errors.qcow2"), payload);

    auto runtime = std::make_unique<FakeQemuRuntime>();
    auto *fake = runtime.get();
    MachineManager::Options options;
    QVERIFY(configureSshOptions(&options, root));
    options.storePath = root + QStringLiteral("/podstack.db");
    options.machinesRoot = root + QStringLiteral("/machines");
    options.imageCacheDir = root + QStringLiteral("/images");
    options.qemuImgBinary = qemuImg;
    options.ovmfCodePath = ovmfCode;
    options.ovmfVarsPath = ovmfVars;
    options.sshPortStart = freePortHint();
    options.runtime = std::move(runtime);
    options.resolveDistro = [image](const QString &, const QString &, PodStack::Error *error) {
        if (error) {
            *error = {};
        }
        return image;
    };

    MachineManager manager(std::move(options));
    CreateMachineRequest base;
    base.name = QStringLiteral("runtime-errors");
    base.distro = QStringLiteral("test");
    base.arch = QStringLiteral("amd64");
    base.user = QStringLiteral("podstack");
    base.diskBytes = 1024LL * 1024LL * 1024LL;

    PodStack::Error error;
    const MachineMeta created = manager.create(base, {}, {}, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QCOMPARE(created.state, QStringLiteral("running"));

    fake->failStop = true;
    error = {};
    const MachineMeta failedStop = manager.stop(created.id, false, &error);
    QVERIFY(!error.isEmpty());
    QCOMPARE(failedStop.state, QStringLiteral("error"));

    fake->failStop = false;
    error = {};
    QCOMPARE(manager.stop(created.id, false, &error).state, QStringLiteral("stopped"));
    QVERIFY2(error.isEmpty(), qPrintable(error.message));

    fake->failStart = true;
    error = {};
    const MachineMeta failedStart = manager.start(created.id, &error);
    QVERIFY(!error.isEmpty());
    QCOMPARE(failedStart.state, QStringLiteral("error"));

    fake->failStart = false;
    error = {};
    QCOMPARE(manager.start(created.id, &error).state, QStringLiteral("running"));

    fake->failStart = true;
    error = {};
    const MachineMeta failedRestart = manager.restart(created.id, &error);
    QVERIFY(!error.isEmpty());
    QVERIFY(failedRestart.state == QStringLiteral("error") ||
            failedRestart.state == QStringLiteral("stopping"));

    fake->failStart = false;
    error = {};
    QVERIFY(manager.start(created.id, &error).id.isEmpty() == false);

    UpdateMachineRequest update;
    update.diskBytes = created.diskBytes * 2;
    fake->failStart = true;
    error = {};
    manager.update(created.id, update, &error);
    QVERIFY(!error.isEmpty());
    fake->failStart = false;
}

void MachinesTest::machineManagerLifecycle()
{
    const QString root = m_root + QStringLiteral("/manager-lifecycle");
    QDir(root).removeRecursively();
    QVERIFY(QDir().mkpath(root));

    QString ovmfCode;
    QString ovmfVars;
    QVERIFY(createFirmwareFiles(root, &ovmfCode, &ovmfVars));
    const QString qemuImg = createQemuImgScript(root);
    QVERIFY(!qemuImg.isEmpty());

    const QByteArray payload(2048, 'l');
    m_server.setResponse(QStringLiteral("/manager-lifecycle.qcow2"), 200, payload);
    const DistroImage image = testImage(QStringLiteral("/manager-lifecycle.qcow2"), payload);

    auto runtime = std::make_unique<FakeQemuRuntime>();
    FakeQemuRuntime *fake = runtime.get();
    MachineManager::Options options;
    QVERIFY(configureSshOptions(&options, root));
    options.storePath = root + QStringLiteral("/podstack.db");
    options.machinesRoot = root + QStringLiteral("/machines");
    options.imageCacheDir = root + QStringLiteral("/images");
    options.qemuImgBinary = qemuImg;
    options.ovmfCodePath = ovmfCode;
    options.ovmfVarsPath = ovmfVars;
    options.sshPortStart = freePortHint();
    options.runtime = std::move(runtime);
    options.resolveDistro = [image](const QString &, const QString &, PodStack::Error *) {
        return image;
    };

    const QString machinesRoot = options.machinesRoot;
    MachineManager manager(std::move(options));

    CreateMachineRequest request;
    request.name = QStringLiteral("lifecycle");
    request.distro = QStringLiteral("test");
    request.arch = QStringLiteral("amd64");
    request.user = QStringLiteral("podstack");
    request.diskBytes = 1024LL * 1024LL * 1024LL;

    PodStack::Error error;
    MachineMeta meta = manager.create(request, {}, {}, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QCOMPARE(meta.state, QStringLiteral("running"));
    const QString directory = machinesRoot + QLatin1Char('/') + meta.id;
    QVERIFY(QFileInfo(directory).isDir());

    meta = manager.stop(meta.id, false, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QCOMPARE(meta.state, QStringLiteral("stopped"));
    QCOMPARE(fake->stopCount, 1);
    QCOMPARE(manager.get(meta.id, &error).state, QStringLiteral("stopped"));
    QCOMPARE(MachineMetaFile::read(directory + QStringLiteral("/meta.json"), &error).state,
             QStringLiteral("stopped"));

    meta = manager.stop(meta.id, false, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QCOMPARE(meta.state, QStringLiteral("stopped"));
    QCOMPARE(fake->stopCount, 1);

    meta = manager.start(meta.id, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QCOMPARE(meta.state, QStringLiteral("running"));
    QCOMPARE(fake->startCount, 2);

    meta = manager.restart(meta.id, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QCOMPARE(meta.state, QStringLiteral("running"));
    QCOMPARE(fake->stopCount, 2);
    QCOMPARE(fake->startCount, 3);

    meta = manager.stop(meta.id, true, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QCOMPARE(meta.state, QStringLiteral("stopped"));
    QVERIFY(fake->lastForce);

    fake->failStart = true;
    meta = manager.start(meta.id, &error);
    QVERIFY(!error.isEmpty());
    QCOMPARE(meta.state, QStringLiteral("error"));
    PodStack::Error storedError;
    const MachineMeta failed = manager.get(meta.id, &storedError);
    QVERIFY2(storedError.isEmpty(), qPrintable(storedError.message));
    QCOMPARE(failed.state, QStringLiteral("error"));
    QVERIFY(!failed.error.isEmpty());
    PodStack::Error fileError;
    QCOMPARE(MachineMetaFile::read(directory + QStringLiteral("/meta.json"), &fileError).state,
             QStringLiteral("error"));
    QVERIFY(!MachineMetaFile::read(directory + QStringLiteral("/meta.json"), &fileError)
                 .error.isEmpty());

    fake->failStart = false;
    meta = manager.start(meta.id, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QCOMPARE(meta.state, QStringLiteral("running"));
    QVERIFY(meta.error.isEmpty());

    CreateMachineRequest forced = request;
    forced.name = QStringLiteral("lifecycle-force");
    const MachineMeta other = manager.create(forced, {}, {}, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QCOMPARE(other.state, QStringLiteral("running"));
    const QString otherDirectory = machinesRoot + QLatin1Char('/') + other.id;
    QVERIFY(QFileInfo(otherDirectory).isDir());

    QVERIFY2(manager.remove(other.id, true, &error), qPrintable(error.message));
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QVERIFY(fake->lastForce);
    QVERIFY(!QFileInfo::exists(otherDirectory));
    QVERIFY(manager.get(other.id, &error).id.isEmpty());
    QCOMPARE(error.code, QStringLiteral("PODSTACK_NOT_FOUND"));

    meta = manager.stop(meta.id, false, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QCOMPARE(meta.state, QStringLiteral("stopped"));
    QVERIFY2(manager.remove(meta.id, false, &error), qPrintable(error.message));
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QVERIFY(!QFileInfo::exists(directory));

    QVERIFY(!manager.remove(meta.id, false, &error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_NOT_FOUND"));
}

void MachinesTest::machineManagerUpdate()
{
    const QString root = m_root + QStringLiteral("/manager-update");
    QDir(root).removeRecursively();
    QVERIFY(QDir().mkpath(root));

    QString ovmfCode;
    QString ovmfVars;
    QVERIFY(createFirmwareFiles(root, &ovmfCode, &ovmfVars));
    const QString qemuImg = createQemuImgScript(root);
    QVERIFY(!qemuImg.isEmpty());
    const QString qemuImgLog = root + QStringLiteral("/qemu-img.log");
    qputenv("PODSTACK_QEMU_IMG_LOG", qemuImgLog.toUtf8());

    const QByteArray payload(2048, 'u');
    m_server.setResponse(QStringLiteral("/manager-update.qcow2"), 200, payload);
    const DistroImage image = testImage(QStringLiteral("/manager-update.qcow2"), payload);

    auto runtime = std::make_unique<FakeQemuRuntime>();
    FakeQemuRuntime *fake = runtime.get();
    MachineManager::Options options;
    QVERIFY(configureSshOptions(&options, root));
    options.storePath = root + QStringLiteral("/podstack.db");
    options.machinesRoot = root + QStringLiteral("/machines");
    options.imageCacheDir = root + QStringLiteral("/images");
    options.qemuImgBinary = qemuImg;
    options.ovmfCodePath = ovmfCode;
    options.ovmfVarsPath = ovmfVars;
    options.sshPortStart = freePortHint();
    options.runtime = std::move(runtime);
    options.resolveDistro =
        [image](const QString &, const QString &, PodStack::Error *) { return image; };

    const QString machinesRoot = options.machinesRoot;
    MachineManager manager(std::move(options));

    constexpr qint64 kGiB = 1024LL * 1024LL * 1024LL;
    CreateMachineRequest request;
    request.name = QStringLiteral("update-limits");
    request.distro = QStringLiteral("test");
    request.arch = QStringLiteral("amd64");
    request.user = QStringLiteral("podstack");
    request.cpus = 2;
    request.memoryBytes = 4LL * kGiB;
    request.diskBytes = 1LL * kGiB;

    PodStack::Error error;
    const MachineMeta created = manager.create(request, {}, {}, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QCOMPARE(created.state, QStringLiteral("running"));

    const QString metaPath = machinesRoot + QLatin1Char('/') + created.id
                             + QStringLiteral("/meta.json");

    UpdateMachineRequest resources;
    resources.cpus = 4;
    resources.memoryBytes = 8LL * kGiB;
    const int stopsBeforeResources = fake->stopCount;
    const int startsBeforeResources = fake->startCount;
    MachineMeta updated = manager.update(created.id, resources, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QCOMPARE(updated.state, QStringLiteral("running"));
    QCOMPARE(updated.cpus, 4);
    QCOMPARE(updated.memoryBytes, 8LL * kGiB);
    QCOMPARE(fake->stopCount, stopsBeforeResources + 1);
    QCOMPARE(fake->startCount, startsBeforeResources + 1);
    QCOMPARE(manager.get(created.id, &error).cpus, 4);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    PodStack::Error metaError;
    const MachineMeta stored = MachineMetaFile::read(metaPath, &metaError);
    QVERIFY2(metaError.isEmpty(), qPrintable(metaError.message));
    QCOMPARE(stored.cpus, 4);
    QCOMPARE(stored.memoryBytes, 8LL * kGiB);
    QCOMPARE(stored.diskBytes, 1LL * kGiB);

    const QString sshConfigPath = root + QStringLiteral("/ssh/config");
    const auto sshConfigText = [&sshConfigPath] {
        QFile file(sshConfigPath);
        if (!file.open(QIODevice::ReadOnly)) {
            return QString();
        }
        return QString::fromUtf8(file.readAll());
    };
    QVERIFY(!sshConfigText().contains(QStringLiteral("ForwardAgent yes")));

    // Mudança só do agent: persiste e sincroniza o ssh config sem tocar no QEMU.
    UpdateMachineRequest enableAgent;
    enableAgent.forwardSshAgent = true;
    const int stopsBeforeAgent = fake->stopCount;
    const int startsBeforeAgent = fake->startCount;
    updated = manager.update(created.id, enableAgent, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QCOMPARE(updated.state, QStringLiteral("running"));
    QCOMPARE(updated.forwardSshAgent, true);
    QCOMPARE(fake->stopCount, stopsBeforeAgent);
    QCOMPARE(fake->startCount, startsBeforeAgent);
    QCOMPARE(manager.get(created.id, &error).forwardSshAgent, true);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QCOMPARE(MachineMetaFile::read(metaPath, &metaError).forwardSshAgent, true);
    QVERIFY2(metaError.isEmpty(), qPrintable(metaError.message));
    QVERIFY(sshConfigText().contains(QStringLiteral("  ForwardAgent yes")));

    UpdateMachineRequest disableAgent;
    disableAgent.forwardSshAgent = false;
    const int stopsBeforeDisableAgent = fake->stopCount;
    const int startsBeforeDisableAgent = fake->startCount;
    updated = manager.update(created.id, disableAgent, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QCOMPARE(updated.forwardSshAgent, false);
    QCOMPARE(fake->stopCount, stopsBeforeDisableAgent);
    QCOMPARE(fake->startCount, startsBeforeDisableAgent);
    QCOMPARE(manager.get(created.id, &error).forwardSshAgent, false);
    QCOMPARE(MachineMetaFile::read(metaPath, &metaError).forwardSshAgent, false);
    QVERIFY2(metaError.isEmpty(), qPrintable(metaError.message));
    QVERIFY(!sshConfigText().contains(QStringLiteral("ForwardAgent yes")));

    // Falha ao sincronizar o ssh config desfaz o metadado (rollback).
    QVERIFY(QFile::remove(sshConfigPath));
    QVERIFY(QDir().mkpath(sshConfigPath));
    UpdateMachineRequest failingAgent;
    failingAgent.forwardSshAgent = true;
    const int stopsBeforeRollback = fake->stopCount;
    const int startsBeforeRollback = fake->startCount;
    const MachineMeta rolledBack = manager.update(created.id, failingAgent, &error);
    QCOMPARE(error.code, QStringLiteral("PODSTACK_IO_ERROR"));
    QCOMPARE(rolledBack.forwardSshAgent, false);
    QCOMPARE(fake->stopCount, stopsBeforeRollback);
    QCOMPARE(fake->startCount, startsBeforeRollback);
    QCOMPARE(manager.get(created.id, &error).forwardSshAgent, false);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QCOMPARE(MachineMetaFile::read(metaPath, &metaError).forwardSshAgent, false);
    QVERIFY2(metaError.isEmpty(), qPrintable(metaError.message));
    QVERIFY(QDir(sshConfigPath).removeRecursively());

    UpdateMachineRequest grow;
    grow.diskBytes = 2LL * kGiB;
    const int stopsBeforeGrow = fake->stopCount;
    const int startsBeforeGrow = fake->startCount;
    updated = manager.update(created.id, grow, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message + QStringLiteral(" ") + error.detail));
    QCOMPARE(updated.state, QStringLiteral("running"));
    QCOMPARE(updated.diskBytes, 2LL * kGiB);
    QCOMPARE(fake->stopCount, stopsBeforeGrow + 1);
    QCOMPARE(fake->startCount, startsBeforeGrow + 1);
    QCOMPARE(manager.get(created.id, &error).diskBytes, 2LL * kGiB);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QCOMPARE(MachineMetaFile::read(metaPath, &metaError).diskBytes, 2LL * kGiB);
    QVERIFY2(metaError.isEmpty(), qPrintable(metaError.message));

    QFile logFile(qemuImgLog);
    QVERIFY(logFile.open(QIODevice::ReadOnly));
    const QStringList resizeArgs = QString::fromUtf8(logFile.readAll())
                                       .split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    logFile.close();
    QCOMPARE(resizeArgs.size(), 3);
    QCOMPARE(resizeArgs.value(0), QStringLiteral("resize"));
    QCOMPARE(resizeArgs.value(1), created.diskPath);
    QCOMPARE(resizeArgs.value(2), QString::number(2LL * kGiB));

    UpdateMachineRequest shrink;
    shrink.diskBytes = 1LL * kGiB;
    const int stopsBeforeShrink = fake->stopCount;
    const int startsBeforeShrink = fake->startCount;
    const MachineMeta rejected = manager.update(created.id, shrink, &error);
    Q_UNUSED(rejected);
    QCOMPARE(error.code, QStringLiteral("PODSTACK_INVALID_PARAMS"));
    QCOMPARE(fake->stopCount, stopsBeforeShrink);
    QCOMPARE(fake->startCount, startsBeforeShrink);
    QCOMPARE(manager.get(created.id, &error).state, QStringLiteral("running"));
    QCOMPARE(manager.get(created.id, &error).diskBytes, 2LL * kGiB);

    const int stopsBeforeStop = fake->stopCount;
    const MachineMeta stopped = manager.stop(created.id, false, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QCOMPARE(stopped.state, QStringLiteral("stopped"));
    QCOMPARE(fake->stopCount, stopsBeforeStop + 1);

    UpdateMachineRequest offLine;
    offLine.memoryBytes = 2LL * kGiB;
    const int stopsBeforeOffline = fake->stopCount;
    const int startsBeforeOffline = fake->startCount;
    updated = manager.update(created.id, offLine, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QCOMPARE(updated.state, QStringLiteral("stopped"));
    QCOMPARE(updated.memoryBytes, 2LL * kGiB);
    QCOMPARE(fake->stopCount, stopsBeforeOffline);
    QCOMPARE(fake->startCount, startsBeforeOffline);
    QCOMPARE(manager.get(created.id, &error).state, QStringLiteral("stopped"));
    QCOMPARE(manager.get(created.id, &error).memoryBytes, 2LL * kGiB);

    UpdateMachineRequest invalidCpus;
    invalidCpus.cpus = 0;
    const MachineMeta invalid = manager.update(created.id, invalidCpus, &error);
    Q_UNUSED(invalid);
    QCOMPARE(error.code, QStringLiteral("PODSTACK_INVALID_PARAMS"));
    QCOMPARE(manager.get(created.id, &error).cpus, 4);

    UpdateMachineRequest empty;
    (void)manager.update(created.id, empty, &error);
    QCOMPARE(error.code, QStringLiteral("PODSTACK_INVALID_PARAMS"));

    QVERIFY2(manager.remove(created.id, true, &error), qPrintable(error.message));
    qunsetenv("PODSTACK_QEMU_IMG_LOG");
}

void MachinesTest::machineManagerSnapshots()
{
    const QString root = m_root + QStringLiteral("/manager-snapshots");
    QDir(root).removeRecursively();
    QVERIFY(QDir().mkpath(root));

    QString ovmfCode;
    QString ovmfVars;
    QVERIFY(createFirmwareFiles(root, &ovmfCode, &ovmfVars));
    const QString qemuImg = createQemuImgScript(root);
    QVERIFY(!qemuImg.isEmpty());
    const QString qemuImgLog = root + QStringLiteral("/qemu-img.log");
    qputenv("PODSTACK_QEMU_IMG_LOG", qemuImgLog.toUtf8());
    qputenv("PODSTACK_QEMU_IMG_LOG_APPEND", "1");

    const QByteArray payload(2048, 's');
    m_server.setResponse(QStringLiteral("/manager-snapshots.qcow2"), 200, payload);
    const DistroImage image = testImage(QStringLiteral("/manager-snapshots.qcow2"), payload);

    auto runtime = std::make_unique<FakeQemuRuntime>();
    FakeQemuRuntime *fake = runtime.get();
    MachineManager::Options options;
    QVERIFY(configureSshOptions(&options, root));
    options.storePath = root + QStringLiteral("/podstack.db");
    options.machinesRoot = root + QStringLiteral("/machines");
    options.imageCacheDir = root + QStringLiteral("/images");
    options.qemuImgBinary = qemuImg;
    options.ovmfCodePath = ovmfCode;
    options.ovmfVarsPath = ovmfVars;
    options.sshPortStart = freePortHint();
    options.runtime = std::move(runtime);
    options.resolveDistro =
        [image](const QString &, const QString &, PodStack::Error *) { return image; };

    MachineManager manager(std::move(options));

    constexpr qint64 kGiB = 1024LL * 1024LL * 1024LL;
    CreateMachineRequest request;
    request.name = QStringLiteral("snapshots");
    request.distro = QStringLiteral("test");
    request.arch = QStringLiteral("amd64");
    request.user = QStringLiteral("podstack");
    request.cpus = 2;
    request.memoryBytes = 4LL * kGiB;
    request.diskBytes = 1LL * kGiB;

    PodStack::Error error;
    const MachineMeta created = manager.create(request, {}, {}, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QCOMPARE(created.state, QStringLiteral("running"));

    const QByteArray info = QByteArrayLiteral(
        "{\"snapshots\":["
        "{\"id\":\"1\",\"name\":\"clean\",\"vm-state-size\":1048576,"
        "\"date-sec\":1700000000,\"date-nsec\":123000000},"
        "{\"id\":\"2\",\"name\":\"before-upgrade\",\"vm-state-size\":0,"
        "\"date-sec\":1700000100,\"date-nsec\":0}]}");
    qputenv("PODSTACK_QEMU_IMG_INFO_JSON", info);

    const auto logText = [&qemuImgLog] {
        QFile file(qemuImgLog);
        if (!file.open(QIODevice::ReadOnly)) {
            return QString();
        }
        return QString::fromUtf8(file.readAll());
    };

    const QVector<MachineSnapshot> listed = manager.snapshots(created.name, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QCOMPARE(listed.size(), 2);
    QCOMPARE(listed.at(0).id, QStringLiteral("1"));
    QCOMPARE(listed.at(0).name, QStringLiteral("clean"));
    QCOMPARE(listed.at(0).sizeBytes, 1048576LL);
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    const QTimeZone utc = QTimeZone::UTC;
#else
    const QTimeZone utc = QTimeZone(Qt::UTC);
#endif
    QCOMPARE(
        listed.at(0).date,
        QDateTime::fromSecsSinceEpoch(1700000000, utc).addMSecs(123).toString(Qt::ISODateWithMs));
    QCOMPARE(listed.at(1).name, QStringLiteral("before-upgrade"));
    QCOMPARE(listed.at(1).sizeBytes, 0LL);
    QVERIFY(logText().contains(QStringLiteral("info\n-U\n--output=json\n") + created.diskPath));

    // Running: cria parando e reiniciando.
    int stopsBefore = fake->stopCount;
    int startsBefore = fake->startCount;
    const MachineSnapshot createdSnapshot =
        manager.createSnapshot(created.id, QStringLiteral("snap-new"), &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QCOMPARE(createdSnapshot.name, QStringLiteral("snap-new"));
    QCOMPARE(fake->stopCount, stopsBefore + 1);
    QCOMPARE(fake->startCount, startsBefore + 1);
    QCOMPARE(manager.get(created.id, &error).state, QStringLiteral("running"));
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QVERIFY(logText().contains(QStringLiteral("snapshot\n-c\nsnap-new\n") + created.diskPath));

    // Restaura parando e reiniciando.
    stopsBefore = fake->stopCount;
    startsBefore = fake->startCount;
    const MachineMeta restored =
        manager.restoreSnapshot(created.name, QStringLiteral("clean"), &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QCOMPARE(restored.state, QStringLiteral("running"));
    QCOMPARE(fake->stopCount, stopsBefore + 1);
    QCOMPARE(fake->startCount, startsBefore + 1);
    QVERIFY(logText().contains(QStringLiteral("snapshot\n-a\nclean\n") + created.diskPath));

    // Remove parando e reiniciando.
    stopsBefore = fake->stopCount;
    startsBefore = fake->startCount;
    QVERIFY2(manager.removeSnapshot(created.id, QStringLiteral("clean"), &error),
             qPrintable(error.message));
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QCOMPARE(fake->stopCount, stopsBefore + 1);
    QCOMPARE(fake->startCount, startsBefore + 1);
    QVERIFY(logText().contains(QStringLiteral("snapshot\n-d\nclean\n") + created.diskPath));

    // Parada permanece parada em create/restore/remove.
    QVERIFY2(manager.stop(created.id, false, &error).state == QStringLiteral("stopped"),
             qPrintable(error.message));
    stopsBefore = fake->stopCount;
    startsBefore = fake->startCount;
    const MachineSnapshot offline =
        manager.createSnapshot(created.id, QStringLiteral("offline"), &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QCOMPARE(offline.name, QStringLiteral("offline"));
    QCOMPARE(fake->stopCount, stopsBefore);
    QCOMPARE(fake->startCount, startsBefore);

    const MachineMeta offlineRestored =
        manager.restoreSnapshot(created.id, QStringLiteral("clean"), &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QCOMPARE(offlineRestored.state, QStringLiteral("stopped"));
    QVERIFY2(manager.removeSnapshot(created.id, QStringLiteral("clean"), &error),
             qPrintable(error.message));
    QCOMPARE(fake->stopCount, stopsBefore);
    QCOMPARE(fake->startCount, startsBefore);
    QVERIFY(logText().contains(QStringLiteral("snapshot\n-c\noffline\n") + created.diskPath));
    QVERIFY(logText().contains(QStringLiteral("snapshot\n-a\nclean\n") + created.diskPath));
    QVERIFY(logText().contains(QStringLiteral("snapshot\n-d\nclean\n") + created.diskPath));

    // Nome inválido, duplicado e inexistente.
    QVector<MachineSnapshot> rejected = manager.snapshots(created.id, &error);
    QCOMPARE(rejected.size(), 2);
    manager.createSnapshot(created.id, QStringLiteral("bad name"), &error);
    QCOMPARE(error.code, QStringLiteral("PODSTACK_INVALID_PARAMS"));
    manager.createSnapshot(created.id, QStringLiteral("clean"), &error);
    QCOMPARE(error.code, QStringLiteral("PODSTACK_CONFLICT"));
    manager.restoreSnapshot(created.id, QStringLiteral("missing"), &error);
    QCOMPARE(error.code, QStringLiteral("PODSTACK_NOT_FOUND"));
    QVERIFY(!manager.removeSnapshot(created.id, QStringLiteral("missing"), &error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_NOT_FOUND"));
    QCOMPARE(fake->stopCount, stopsBefore);
    QCOMPARE(fake->startCount, startsBefore);

    // Disco ausente.
    QVERIFY(QFile::remove(created.diskPath));
    rejected = manager.snapshots(created.id, &error);
    QCOMPARE(error.code, QStringLiteral("PODSTACK_IO_ERROR"));
    QVERIFY(rejected.isEmpty());

    QVERIFY2(manager.remove(created.id, true, &error), qPrintable(error.message));
    qunsetenv("PODSTACK_QEMU_IMG_INFO_JSON");
    qunsetenv("PODSTACK_QEMU_IMG_LOG_APPEND");
    qunsetenv("PODSTACK_QEMU_IMG_LOG");
}

void MachinesTest::machineManagerRename()
{
    const QString root = m_root + QStringLiteral("/manager-rename");
    QDir(root).removeRecursively();
    QVERIFY(QDir().mkpath(root));

    QString ovmfCode;
    QString ovmfVars;
    QVERIFY(createFirmwareFiles(root, &ovmfCode, &ovmfVars));
    const QString qemuImg = createQemuImgScript(root);
    QVERIFY(!qemuImg.isEmpty());

    const QByteArray payload(1024, 'r');
    m_server.setResponse(QStringLiteral("/manager-rename.qcow2"), 200, payload);
    const DistroImage image =
        testImage(QStringLiteral("/manager-rename.qcow2"), payload);

    auto runtime = std::make_unique<FakeQemuRuntime>();
    MachineManager::Options options;
    QVERIFY(configureSshOptions(&options, root));
    const QString sshConfigPath = options.sshConfigPath;
    options.storePath = root + QStringLiteral("/podstack.db");
    options.machinesRoot = root + QStringLiteral("/machines");
    options.imageCacheDir = root + QStringLiteral("/images");
    options.qemuImgBinary = qemuImg;
    options.ovmfCodePath = ovmfCode;
    options.ovmfVarsPath = ovmfVars;
    options.sshPortStart = freePortHint();
    options.runtime = std::move(runtime);
    options.resolveDistro =
        [image](const QString &, const QString &, PodStack::Error *) { return image; };

    MachineManager manager(std::move(options));

    PodStack::Config config = PodStack::Config::load();
    config.set(QStringLiteral("machines.default"), QJsonValue::Null);
    QString configError;
    QVERIFY2(config.save(&configError), qPrintable(configError));

    CreateMachineRequest request;
    request.name = QStringLiteral("rename-orig");
    request.distro = QStringLiteral("test");
    request.arch = QStringLiteral("amd64");
    request.user = QStringLiteral("podstack");
    request.diskBytes = 1024LL * 1024LL * 1024LL;

    PodStack::Error error;
    const MachineMeta origin = manager.create(request, {}, {}, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QCOMPARE(origin.state, QStringLiteral("running"));

    CreateMachineRequest taken = request;
    taken.name = QStringLiteral("rename-taken");
    const MachineMeta other = manager.create(taken, {}, {}, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));

    QVERIFY2(manager.setDefaultMachine(origin.id, &error), qPrintable(error.message));
    QCOMPARE(manager.defaultMachine(&error), origin.name);

    const MachineMeta renamed = manager.rename(origin.id, QStringLiteral("renamed-machine"),
                                               &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message + QStringLiteral(" ") + error.detail));
    QCOMPARE(renamed.id, origin.id);
    QCOMPARE(renamed.name, QStringLiteral("renamed-machine"));
    QCOMPARE(manager.get(renamed.id, &error).name, QStringLiteral("renamed-machine"));
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QCOMPARE(manager.get(QStringLiteral("renamed-machine"), &error).id, origin.id);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QVERIFY(manager.get(QStringLiteral("rename-orig"), &error).id.isEmpty());
    QCOMPARE(error.code, QStringLiteral("PODSTACK_NOT_FOUND"));

    QFile sshFile(sshConfigPath);
    QVERIFY(sshFile.open(QIODevice::ReadOnly));
    const QString sshConfig = QString::fromUtf8(sshFile.readAll());
    QVERIFY(sshConfig.contains(QStringLiteral("# >>> podstack: renamed-machine")));
    QVERIFY(sshConfig.contains(QStringLiteral("Host renamed-machine.podstack "
                                              "renamed-machine")));
    QVERIFY(!sshConfig.contains(QStringLiteral("# >>> podstack: rename-orig")));
    QVERIFY(sshConfig.contains(QStringLiteral("# >>> podstack: rename-taken")));
    QCOMPARE(manager.defaultMachine(&error), QStringLiteral("renamed-machine"));
    QVERIFY2(error.isEmpty(), qPrintable(error.message));

    const MachineMeta conflict =
        manager.rename(renamed.id, QStringLiteral("rename-taken"), &error);
    QVERIFY(conflict.id.isEmpty());
    QCOMPARE(error.code, QStringLiteral("PODSTACK_CONFLICT"));
    QCOMPARE(manager.get(renamed.id, &error).name, QStringLiteral("renamed-machine"));

    const MachineMeta invalid =
        manager.rename(renamed.id, QStringLiteral("Bad_Name"), &error);
    QVERIFY(invalid.id.isEmpty());
    QCOMPARE(error.code, QStringLiteral("PODSTACK_INVALID_PARAMS"));

    QVERIFY2(manager.remove(renamed.id, true, &error), qPrintable(error.message));
    QVERIFY2(manager.defaultMachine(&error).isEmpty(), qPrintable(error.message));
    QVERIFY2(manager.remove(other.id, true, &error), qPrintable(error.message));
}

void MachinesTest::machineManagerClone()
{
    const QString root = m_root + QStringLiteral("/manager-clone");
    QDir(root).removeRecursively();
    QVERIFY(QDir().mkpath(root));

    QString ovmfCode;
    QString ovmfVars;
    QVERIFY(createFirmwareFiles(root, &ovmfCode, &ovmfVars));
    const QString qemuImg = createQemuImgScript(root);
    QVERIFY(!qemuImg.isEmpty());
    const QString qemuImgLog = root + QStringLiteral("/qemu-img.log");
    qputenv("PODSTACK_QEMU_IMG_LOG", qemuImgLog.toUtf8());

    const QByteArray payload(2048, 'c');
    m_server.setResponse(QStringLiteral("/manager-clone.qcow2"), 200, payload);
    const DistroImage image =
        testImage(QStringLiteral("/manager-clone.qcow2"), payload);

    auto runtime = std::make_unique<FakeQemuRuntime>();
    FakeQemuRuntime *fake = runtime.get();
    MachineManager::Options options;
    QVERIFY(configureSshOptions(&options, root));
    const QString sshConfigPath = options.sshConfigPath;
    options.storePath = root + QStringLiteral("/podstack.db");
    options.machinesRoot = root + QStringLiteral("/machines");
    options.imageCacheDir = root + QStringLiteral("/images");
    options.qemuImgBinary = qemuImg;
    options.ovmfCodePath = ovmfCode;
    options.ovmfVarsPath = ovmfVars;
    options.sshPortStart = freePortHint();
    options.runtime = std::move(runtime);
    options.resolveDistro =
        [image](const QString &, const QString &, PodStack::Error *) { return image; };

    const QString machinesRoot = options.machinesRoot;
    MachineManager manager(std::move(options));

    CreateMachineRequest request;
    request.name = QStringLiteral("clone-origin");
    request.distro = QStringLiteral("test");
    request.arch = QStringLiteral("amd64");
    request.user = QStringLiteral("podstack");
    request.cpus = 3;
    request.diskBytes = 1024LL * 1024LL * 1024LL;
    request.isolateNetwork = true;

    PodStack::Error error;
    const MachineMeta origin = manager.create(request, {}, {}, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QCOMPARE(origin.state, QStringLiteral("running"));
    const int startsBeforeClone = fake->startCount;
    const int stopsBeforeClone = fake->stopCount;

    QStringList stages;
    const MachineMeta clone = manager.clone(
        origin.id, QStringLiteral("clone-copy"),
        [&stages](const QString &stage, int) { stages.append(stage); }, {}, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message + QStringLiteral(" ") + error.detail));
    QVERIFY(!clone.id.isEmpty());
    QVERIFY(clone.id != origin.id);
    QCOMPARE(clone.name, QStringLiteral("clone-copy"));
    QCOMPARE(clone.state, QStringLiteral("stopped"));
    QCOMPARE(clone.distro, origin.distro);
    QCOMPARE(clone.distroVersion, origin.distroVersion);
    QCOMPARE(clone.arch, origin.arch);
    QCOMPARE(clone.cpus, origin.cpus);
    QCOMPARE(clone.memoryBytes, origin.memoryBytes);
    QCOMPARE(clone.diskBytes, origin.diskBytes);
    QCOMPARE(clone.sshUser, origin.sshUser);
    QCOMPARE(clone.isolateNetwork, origin.isolateNetwork);
    QCOMPARE(clone.forwardSshAgent, origin.forwardSshAgent);
    QVERIFY(clone.sshPort > 0);
    QVERIFY(clone.sshPort != origin.sshPort);
    QCOMPARE(clone.ports.size(), 1);
    QCOMPARE(clone.ports.at(0).hostPort, clone.sshPort);
    QCOMPARE(clone.ports.at(0).guestPort, 22);
    QCOMPARE(clone.ports.at(0).label, QStringLiteral("ssh"));
    QVERIFY(clone.mounts.isEmpty());

    const QString cloneDirectory = machinesRoot + QLatin1Char('/') + clone.id;
    QCOMPARE(clone.diskPath, cloneDirectory + QStringLiteral("/disk.qcow2"));
    QCOMPARE(clone.seedPath, cloneDirectory + QStringLiteral("/seed.iso"));
    QCOMPARE(clone.sshKeyPath, cloneDirectory + QStringLiteral("/id_ed25519"));
    QVERIFY(clone.lastBootAt.isEmpty());

    const auto readFile = [](const QString &path) {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) {
            return QByteArray();
        }
        return file.readAll();
    };
    QVERIFY(QFileInfo(clone.diskPath).isFile());
    QCOMPARE(readFile(clone.diskPath), readFile(origin.diskPath));
    QCOMPARE(readFile(clone.seedPath), readFile(origin.seedPath));
    QCOMPARE(readFile(clone.sshKeyPath), readFile(origin.sshKeyPath));
    QCOMPARE(readFile(clone.sshKeyPath + QStringLiteral(".pub")),
             readFile(origin.sshKeyPath + QStringLiteral(".pub")));
    QVERIFY(ownerOnly(QFileInfo(clone.sshKeyPath).permissions(), false));
    QVERIFY(ownerOnly(QFileInfo(clone.seedPath).permissions(), false));

    QFile logFile(qemuImgLog);
    QVERIFY(logFile.open(QIODevice::ReadOnly));
    const QStringList args = QString::fromUtf8(logFile.readAll())
                                 .split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    QCOMPARE(args.size(), 5);
    QCOMPARE(args.value(0), QStringLiteral("convert"));
    QCOMPARE(args.value(1), QStringLiteral("-O"));
    QCOMPARE(args.value(2), QStringLiteral("qcow2"));
    QCOMPARE(args.value(3), origin.diskPath);
    QCOMPARE(args.value(4), clone.diskPath);

    QFile sshFile(sshConfigPath);
    QVERIFY(sshFile.open(QIODevice::ReadOnly));
    const QString sshConfig = QString::fromUtf8(sshFile.readAll());
    QVERIFY(sshConfig.contains(QStringLiteral("# >>> podstack: clone-origin")));
    QVERIFY(sshConfig.contains(QStringLiteral("# >>> podstack: clone-copy")));

    QCOMPARE(manager.get(origin.id, &error).state, QStringLiteral("running"));
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QCOMPARE(fake->stopCount, stopsBeforeClone + 1);
    QVERIFY(fake->startCount >= startsBeforeClone + 1);

    QVERIFY(stages.contains(QStringLiteral("stopping")));
    QVERIFY(stages.contains(QStringLiteral("disk")));
    QVERIFY(stages.contains(QStringLiteral("copy")));
    QVERIFY(stages.contains(QStringLiteral("metadata")));
    QVERIFY(stages.contains(QStringLiteral("starting")));
    QCOMPARE(stages.last(), QStringLiteral("done"));

    const MachineMeta started = manager.start(clone.id, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message + QStringLiteral(" ") + error.detail));
    QCOMPARE(started.state, QStringLiteral("running"));

    QVERIFY2(manager.remove(clone.id, true, &error), qPrintable(error.message));
    QVERIFY2(manager.remove(origin.id, true, &error), qPrintable(error.message));
    qunsetenv("PODSTACK_QEMU_IMG_LOG");
}

void MachinesTest::machineDefaultConfig()
{
    const QString root = m_root + QStringLiteral("/manager-default");
    QDir(root).removeRecursively();
    QVERIFY(QDir().mkpath(root));

    PodStack::Config config = PodStack::Config::load();
    config.set(QStringLiteral("machines.default"), QJsonValue::Null);
    QString configError;
    QVERIFY2(config.save(&configError), qPrintable(configError));

    MachineManager::Options options;
    options.storePath = root + QStringLiteral("/podstack.db");
    options.machinesRoot = root + QStringLiteral("/machines");
    options.sshConfigPath = root + QStringLiteral("/ssh/config");
    MachineManager manager(std::move(options));

    const MachineMeta meta = testMachine(
        QStringLiteral("0190f0f0-0000-7000-8000-0000000000d1"),
        QStringLiteral("default-machine"));
    QVERIFY(seedMachine(root + QStringLiteral("/podstack.db"), root + QStringLiteral("/machines"),
                        meta));

    PodStack::Error error;
    QVERIFY(manager.defaultMachine(&error).isEmpty());
    QVERIFY2(error.isEmpty(), qPrintable(error.message));

    QVERIFY2(manager.setDefaultMachine(QStringLiteral("default-machine"), &error),
             qPrintable(error.message));
    QCOMPARE(manager.defaultMachine(&error), QStringLiteral("default-machine"));
    QVERIFY2(error.isEmpty(), qPrintable(error.message));

    QVERIFY2(manager.setDefaultMachine(meta.id, &error), qPrintable(error.message));
    QCOMPARE(manager.defaultMachine(&error), QStringLiteral("default-machine"));

    QVERIFY(!manager.setDefaultMachine(QStringLiteral("missing-machine"), &error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_NOT_FOUND"));
    QCOMPARE(manager.defaultMachine(&error), QStringLiteral("default-machine"));

    QVERIFY2(manager.remove(meta.id, true, &error), qPrintable(error.message));
    QVERIFY2(manager.defaultMachine(&error).isEmpty(), qPrintable(error.message));
}

void MachinesTest::machineManagerExportImport()
{
    const QString root = m_root + QStringLiteral("/manager-export");
    QDir(root).removeRecursively();
    QVERIFY(QDir().mkpath(root));

    QString ovmfCode;
    QString ovmfVars;
    QVERIFY(createFirmwareFiles(root, &ovmfCode, &ovmfVars));
    const QString qemuImg = createQemuImgScript(root);
    QVERIFY(!qemuImg.isEmpty());

    const QByteArray payload(2048, 'e');
    m_server.setResponse(QStringLiteral("/manager-export.qcow2"), 200, payload);
    const DistroImage image = testImage(QStringLiteral("/manager-export.qcow2"), payload);

    auto runtime = std::make_unique<FakeQemuRuntime>();
    FakeQemuRuntime *fake = runtime.get();
    MachineManager::Options options;
    QVERIFY(configureSshOptions(&options, root));
    options.storePath = root + QStringLiteral("/podstack.db");
    options.machinesRoot = root + QStringLiteral("/machines");
    options.imageCacheDir = root + QStringLiteral("/images");
    options.qemuImgBinary = qemuImg;
    options.ovmfCodePath = ovmfCode;
    options.ovmfVarsPath = ovmfVars;
    options.sshPortStart = freePortHint();
    options.runtime = std::move(runtime);
    options.resolveDistro =
        [image](const QString &, const QString &, PodStack::Error *) { return image; };

    MachineManager manager(std::move(options));

    CreateMachineRequest request;
    request.name = QStringLiteral("export-src");
    request.distro = QStringLiteral("test");
    request.arch = QStringLiteral("amd64");
    request.user = QStringLiteral("podstack");
    request.diskBytes = 1024LL * 1024LL * 1024LL;

    PodStack::Error error;
    const MachineMeta created = manager.create(request, {}, {}, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QCOMPARE(created.state, QStringLiteral("running"));

    const QString cancelledArchive = root + QStringLiteral("/cancelled.tar.zst");
    const MachineMeta cancelledExport =
        manager.exportMachine(created.id, cancelledArchive, false, {}, [] { return true; },
                              &error);
    QVERIFY(cancelledExport.id.isEmpty());
    QCOMPARE(error.code, QStringLiteral("PODSTACK_CANCELLED"));
    QVERIFY(!QFileInfo::exists(cancelledArchive));
    QCOMPARE(manager.get(created.id, &error).state, QStringLiteral("running"));

    const QByteArray sourceDisk = [&created] {
        QFile file(created.diskPath);
        return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
    }();
    QVERIFY(!sourceDisk.isEmpty());

    const QString archive = root + QStringLiteral("/export-src.tar.zst");
    const MachineMeta relative =
        manager.exportMachine(created.id, QStringLiteral("relative.tar.zst"), false, {}, {},
                              &error);
    QVERIFY(relative.id.isEmpty());
    QCOMPARE(error.code, QStringLiteral("PODSTACK_INVALID_PARAMS"));

    QStringList stages;
    const MachineMeta exported = manager.exportMachine(
        created.id, archive, false,
        [&stages](const QString &stage, int) { stages.append(stage); }, {}, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QCOMPARE(exported.state, QStringLiteral("running"));
    QVERIFY(QFileInfo(archive).isFile());
    const QFileDevice::Permissions archivePermissions = QFileInfo(archive).permissions();
    QVERIFY(archivePermissions.testFlag(QFileDevice::ReadOwner));
    QVERIFY(!archivePermissions.testFlag(QFileDevice::ReadGroup));
    QVERIFY(!archivePermissions.testFlag(QFileDevice::ReadOther));
    QVERIFY(stages.contains(QStringLiteral("stopping")));
    QVERIFY(stages.contains(QStringLiteral("disk")));
    QVERIFY(stages.contains(QStringLiteral("archive")));
    QVERIFY(stages.contains(QStringLiteral("starting")));
    QCOMPARE(fake->stopCount, 2);
    QCOMPARE(fake->startCount, 3);

    const MachineMeta existing =
        manager.exportMachine(created.id, archive, false, {}, {}, &error);
    QVERIFY(existing.id.isEmpty());
    QCOMPARE(error.code, QStringLiteral("PODSTACK_CONFLICT"));

    const MachineMeta imported =
        manager.importMachine(archive, QStringLiteral("export-dst"), false, {}, {}, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QCOMPARE(imported.name, QStringLiteral("export-dst"));
    QCOMPARE(imported.state, QStringLiteral("stopped"));
    QVERIFY(imported.id != created.id);
    QVERIFY(QFileInfo(imported.diskPath).isFile());
    QVERIFY(QFileInfo(imported.seedPath).isFile());
    QVERIFY(QFileInfo(imported.sshKeyPath).isFile());
    const QFileDevice::Permissions keyPermissions = QFileInfo(imported.sshKeyPath).permissions();
    QVERIFY(keyPermissions.testFlag(QFileDevice::ReadOwner));
    QVERIFY(!keyPermissions.testFlag(QFileDevice::ReadGroup));
    QVERIFY(!keyPermissions.testFlag(QFileDevice::ReadOther));
    QCOMPARE(QFileInfo(imported.diskPath).size(), sourceDisk.size());
    QFile importedDisk(imported.diskPath);
    QVERIFY(importedDisk.open(QIODevice::ReadOnly));
    QCOMPARE(importedDisk.readAll(), sourceDisk);
    QVERIFY(QFileInfo(QFileInfo(imported.diskPath).absolutePath()
                      + QStringLiteral("/meta.json"))
                .isFile());

    const MachineMeta stored = manager.get(QStringLiteral("export-dst"), &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QCOMPARE(stored.id, imported.id);

    QFile sshConfig(root + QStringLiteral("/ssh/config"));
    QVERIFY(sshConfig.open(QIODevice::ReadOnly));
    const QString sshText = QString::fromUtf8(sshConfig.readAll());
    QVERIFY(sshText.contains(QStringLiteral("Host export-dst.podstack export-dst")));
    QVERIFY(sshText.contains(imported.sshKeyPath));

    const MachineMeta duplicate =
        manager.importMachine(archive, QStringLiteral("export-dst"), false, {}, {}, &error);
    QVERIFY(duplicate.id.isEmpty());
    QCOMPARE(error.code, QStringLiteral("PODSTACK_CONFLICT"));

    const MachineMeta replaced =
        manager.importMachine(archive, QStringLiteral("export-dst"), true, {}, {}, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QVERIFY(replaced.id != imported.id);
    QCOMPARE(replaced.name, QStringLiteral("export-dst"));
    QCOMPARE(manager.list(&error).size(), 2);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));

    const MachineMeta started = manager.start(replaced.id, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QCOMPARE(started.state, QStringLiteral("running"));
    QCOMPARE(fake->startCount, 4);

    const MachineMeta stopped = manager.stop(replaced.id, false, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QCOMPARE(stopped.state, QStringLiteral("stopped"));

    const int stopsBefore = fake->stopCount;
    const int startsBefore = fake->startCount;
    const MachineMeta stoppedExport = manager.exportMachine(
        replaced.id, root + QStringLiteral("/stopped.tar.zst"), false, {}, {}, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QCOMPARE(stoppedExport.state, QStringLiteral("stopped"));
    QCOMPARE(fake->stopCount, stopsBefore);
    QCOMPARE(fake->startCount, startsBefore);
}

void MachinesTest::machineArchiveManifestValidation()
{
    const QString hash(64, QLatin1Char('a'));
    const QJsonArray artifacts = {
        QJsonObject{{QStringLiteral("path"), QStringLiteral("disk.qcow2")},
                    {QStringLiteral("size"), 10},
                    {QStringLiteral("sha256"), hash}},
        QJsonObject{{QStringLiteral("path"), QStringLiteral("seed.iso")},
                    {QStringLiteral("size"), 20},
                    {QStringLiteral("sha256"), hash}},
        QJsonObject{{QStringLiteral("path"), QStringLiteral("id_ed25519")},
                    {QStringLiteral("size"), 30},
                    {QStringLiteral("sha256"), hash}},
        QJsonObject{{QStringLiteral("path"), QStringLiteral("id_ed25519.pub")},
                    {QStringLiteral("size"), 40},
                    {QStringLiteral("sha256"), hash}},
    };
    const QJsonObject valid{
        {QStringLiteral("format"), MachineArchiveManifest::formatId()},
        {QStringLiteral("name"), QStringLiteral("manifest")},
        {QStringLiteral("distro"), QStringLiteral("ubuntu")},
        {QStringLiteral("distroVersion"), QStringLiteral("24.04")},
        {QStringLiteral("arch"), QStringLiteral("amd64")},
        {QStringLiteral("cpus"), 2},
        {QStringLiteral("memoryBytes"), 2.0 * 1024 * 1024 * 1024},
        {QStringLiteral("diskBytes"), 20.0 * 1024 * 1024 * 1024},
        {QStringLiteral("sshUser"), QStringLiteral("podstack")},
        {QStringLiteral("artifacts"), artifacts},
    };

    PodStack::Error error;
    const MachineArchiveManifest parsed = MachineArchiveManifest::fromJson(valid, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QCOMPARE(parsed.name, QStringLiteral("manifest"));
    QCOMPARE(parsed.cpus, 2);
    QCOMPARE(parsed.artifacts.size(), 4);
    QVERIFY(!parsed.isolated);

    const auto rejected = [](const QJsonObject &object) {
        PodStack::Error rejectError;
        const MachineArchiveManifest result =
            MachineArchiveManifest::fromJson(object, &rejectError);
        return result.name.isEmpty() &&
               rejectError.code == QStringLiteral("PODSTACK_INVALID_PARAMS");
    };

    QJsonObject invalid = valid;
    invalid.insert(QStringLiteral("format"), QStringLiteral("podstack.machine/0"));
    QVERIFY(rejected(invalid));

    invalid = valid;
    invalid.remove(QStringLiteral("name"));
    QVERIFY(rejected(invalid));

    invalid = valid;
    invalid.insert(QStringLiteral("sshUser"), QStringLiteral("-root"));
    QVERIFY(rejected(invalid));

    invalid = valid;
    invalid.insert(QStringLiteral("sshUser"), QString(33, QLatin1Char('a')));
    QVERIFY(rejected(invalid));

    invalid = valid;
    invalid.insert(QStringLiteral("cpus"), 0);
    QVERIFY(rejected(invalid));

    invalid = valid;
    invalid.insert(QStringLiteral("cpus"), 257);
    QVERIFY(rejected(invalid));

    invalid = valid;
    invalid.insert(QStringLiteral("memoryBytes"), 2.0 * 1024 * 1024 * 1024 * 1024);
    QVERIFY(rejected(invalid));

    invalid = valid;
    invalid.insert(QStringLiteral("diskBytes"), 65.0 * 1024 * 1024 * 1024 * 1024);
    QVERIFY(rejected(invalid));

    invalid = valid;
    invalid.insert(QStringLiteral("memoryBytes"), 1024.0);
    QVERIFY(rejected(invalid));

    invalid = valid;
    invalid.insert(QStringLiteral("diskBytes"), 1.0);
    QVERIFY(rejected(invalid));

    invalid = valid;
    invalid.insert(QStringLiteral("arch"), QStringLiteral("sparc"));
    QVERIFY(rejected(invalid));

    invalid = valid;
    invalid.insert(QStringLiteral("artifacts"), QStringLiteral("none"));
    QVERIFY(rejected(invalid));

    invalid = valid;
    invalid.insert(QStringLiteral("artifacts"), QJsonArray{QStringLiteral("bad")});
    QVERIFY(rejected(invalid));

    invalid = valid;
    invalid.insert(QStringLiteral("artifacts"),
                   QJsonArray{QJsonObject{{QStringLiteral("path"), QStringLiteral("disk.qcow2")},
                                          {QStringLiteral("size"), -1},
                                          {QStringLiteral("sha256"), hash}}});
    QVERIFY(rejected(invalid));

    invalid = valid;
    invalid.insert(QStringLiteral("artifacts"),
                   QJsonArray{QJsonObject{{QStringLiteral("path"), QString()},
                                          {QStringLiteral("size"), 1},
                                          {QStringLiteral("sha256"), hash}}});
    QVERIFY(rejected(invalid));

    invalid = valid;
    invalid.insert(
        QStringLiteral("artifacts"),
        QJsonArray{QJsonObject{{QStringLiteral("path"), QStringLiteral("disk.qcow2")},
                               {QStringLiteral("size"), 1},
                               {QStringLiteral("sha256"), QString(63, QLatin1Char('a'))}}});
    QVERIFY(rejected(invalid));

    // Campos opcionais de isolamento são lidos quando presentes.
    QJsonObject isolated = valid;
    isolated.insert(QStringLiteral("isolated"), true);
    isolated.insert(QStringLiteral("isolateNetwork"), true);
    isolated.insert(QStringLiteral("forwardSshAgent"), true);
    error = {};
    const MachineArchiveManifest isolatedManifest =
        MachineArchiveManifest::fromJson(isolated, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QVERIFY(isolatedManifest.isolated);
    QVERIFY(isolatedManifest.isolateNetwork);
    QVERIFY(isolatedManifest.forwardSshAgent);
}

void MachinesTest::machineArchiveExtractErrors()
{
    const QString root = m_root + QStringLiteral("/archive-extract-errors");
    QDir(root).removeRecursively();
    QVERIFY(QDir().mkpath(root));

    const QString disk = root + QStringLiteral("/disk.qcow2");
    const QString seed = root + QStringLiteral("/seed.iso");
    const QString key = root + QStringLiteral("/id_ed25519");
    const QString pub = root + QStringLiteral("/id_ed25519.pub");
    QVERIFY(writeTestFile(disk, QByteArrayLiteral("disk-data")));
    QVERIFY(writeTestFile(seed, QByteArrayLiteral("seed-data")));
    QVERIFY(writeTestFile(key, QByteArrayLiteral("private-key")));
    QVERIFY(writeTestFile(pub, QByteArrayLiteral("ssh-ed25519 AAAA test")));

    MachineArchiveManifest manifest;
    manifest.format = MachineArchiveManifest::formatId();
    manifest.createdAt = QStringLiteral("2026-01-01T00:00:00.000Z");
    manifest.name = QStringLiteral("archive-errors");
    manifest.distro = QStringLiteral("ubuntu");
    manifest.distroVersion = QStringLiteral("24.04");
    manifest.arch = QStringLiteral("amd64");
    manifest.cpus = 2;
    manifest.memoryBytes = 2LL * 1024 * 1024 * 1024;
    manifest.diskBytes = 20LL * 1024 * 1024 * 1024;
    manifest.sshUser = QStringLiteral("podstack");

    const QString archive = root + QStringLiteral("/machine.tar.zst");
    PodStack::Error error;
    QVERIFY2(MachineArchive::write(archive, manifest, disk, seed, key, pub, {}, {}, &error),
             qPrintable(error.message));

    QTemporaryDir out;
    QVERIFY(out.isValid());

    error = {};
    QVERIFY(!MachineArchive::extract(root + QStringLiteral("/missing.tar.zst"), out.path(), nullptr,
                                     nullptr, {}, {}, &error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_INVALID_PARAMS"));

    const QString notDirectory = root + QStringLiteral("/not-a-directory");
    QVERIFY(writeTestFile(notDirectory, QByteArrayLiteral("x")));
    error = {};
    QVERIFY(!MachineArchive::extract(archive, notDirectory, nullptr, nullptr, {}, {}, &error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_IO_ERROR"));

    error = {};
    QVERIFY(!MachineArchive::extract(root, out.path(), nullptr, nullptr, {}, {}, &error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_INVALID_PARAMS"));

    error = {};
    QVERIFY(!MachineArchive::extract(
        archive, out.path(), nullptr, nullptr, {}, [] { return true; }, &error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_CANCELLED"));

    const auto extractFrom = [&](const QString &candidate) {
        QTemporaryDir target;
        if (!target.isValid()) {
            return PodStack::Error{};
        }
        PodStack::Error extractError;
        MachineArchive::extract(candidate, target.path(), nullptr, nullptr, {}, {}, &extractError);
        return extractError;
    };

    const QString badJson = root + QStringLiteral("/bad-json.tar.zst");
    QVERIFY(rewriteArchiveEntry(archive, badJson, QStringLiteral("manifest.json"),
                                QByteArrayLiteral("not-json")));
    QCOMPARE(extractFrom(badJson).code, QStringLiteral("PODSTACK_INVALID_PARAMS"));

    const QString badFormat = root + QStringLiteral("/bad-format.tar.zst");
    QVERIFY(rewriteArchiveEntry(archive, badFormat, QStringLiteral("manifest.json"),
                                QByteArrayLiteral("{\"format\":\"podstack.machine/0\"}")));
    QCOMPARE(extractFrom(badFormat).code, QStringLiteral("PODSTACK_INVALID_PARAMS"));

    const QString badChecksum = root + QStringLiteral("/bad-checksum.tar.zst");
    QVERIFY(rewriteArchiveEntry(archive, badChecksum, QStringLiteral("disk.qcow2"),
                                QByteArrayLiteral("tampered-disk")));
    QCOMPARE(extractFrom(badChecksum).code, QStringLiteral("PODSTACK_IO_ERROR"));

    QFile validArchive(archive);
    QVERIFY(validArchive.open(QIODevice::ReadOnly));
    const QByteArray archiveBytes = validArchive.readAll();
    validArchive.close();
    const QString truncated = root + QStringLiteral("/truncated.tar.zst");
    QFile truncatedFile(truncated);
    QVERIFY(truncatedFile.open(QIODevice::WriteOnly | QIODevice::Truncate));
    QVERIFY(truncatedFile.write(archiveBytes.left(archiveBytes.size() / 2)) > 0);
    truncatedFile.close();
    QCOMPARE(extractFrom(truncated).code, QStringLiteral("PODSTACK_IO_ERROR"));
}

void MachinesTest::machineArchiveWriteErrors()
{
    const QString root = m_root + QStringLiteral("/archive-write-errors");
    QDir(root).removeRecursively();
    QVERIFY(QDir().mkpath(root));

    const QString disk = root + QStringLiteral("/disk.qcow2");
    const QString seed = root + QStringLiteral("/seed.iso");
    const QString key = root + QStringLiteral("/id_ed25519");
    const QString pub = root + QStringLiteral("/id_ed25519.pub");
    QVERIFY(writeTestFile(disk, QByteArrayLiteral("disk")));
    QVERIFY(writeTestFile(seed, QByteArrayLiteral("seed")));
    QVERIFY(writeTestFile(key, QByteArrayLiteral("key")));
    QVERIFY(writeTestFile(pub, QByteArrayLiteral("pub")));

    MachineArchiveManifest manifest;
    manifest.format = MachineArchiveManifest::formatId();
    manifest.name = QStringLiteral("write-errors");
    manifest.distro = QStringLiteral("ubuntu");
    manifest.arch = QStringLiteral("amd64");
    manifest.cpus = 2;
    manifest.memoryBytes = 2LL * 1024 * 1024 * 1024;
    manifest.diskBytes = 20LL * 1024 * 1024 * 1024;
    manifest.sshUser = QStringLiteral("podstack");

    PodStack::Error error;
    QVERIFY(!MachineArchive::write(QString(), manifest, disk, seed, key, pub, {}, {}, &error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_INVALID_PARAMS"));

    error = {};
    QVERIFY(!MachineArchive::write(root + QStringLiteral("/missing.tar.zst"), manifest,
                                   root + QStringLiteral("/missing-disk"), seed, key, pub, {}, {},
                                   &error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_IO_ERROR"));

    error = {};
    QVERIFY(!MachineArchive::write(root, manifest, disk, seed, key, pub, {}, {}, &error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_IO_ERROR"));

    error = {};
    QVERIFY(!MachineArchive::write(root + QStringLiteral("/missing-dir/machine.tar.zst"), manifest,
                                   disk, seed, key, pub, {}, {}, &error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_IO_ERROR"));

    error = {};
    QVERIFY(!MachineArchive::write(
        root + QStringLiteral("/cancelled.tar.zst"), manifest, disk, seed, key, pub, {},
        [] { return true; }, &error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_CANCELLED"));

    const QString archive = root + QStringLiteral("/ok.tar.zst");
    error = {};
    int progress = 0;
    QVERIFY2(MachineArchive::write(
                 archive, manifest, disk, seed, key, pub,
                 [&progress](const QString &, int value) { progress = value; }, {}, &error),
             qPrintable(error.message));
    QCOMPARE(progress, 100);
    QVERIFY(QFileInfo(archive).isFile());
    QVERIFY(ownerOnly(QFileInfo(archive).permissions(), false));
}

void MachinesTest::machineManagerExportImportErrors()
{
    const QString root = m_root + QStringLiteral("/manager-export-import-errors");
    QDir(root).removeRecursively();
    QVERIFY(QDir().mkpath(root));

    QString ovmfCode;
    QString ovmfVars;
    QVERIFY(createFirmwareFiles(root, &ovmfCode, &ovmfVars));
    const QString qemuImg = createQemuImgScript(root);
    QVERIFY(!qemuImg.isEmpty());

    const QByteArray payload(2048, 'x');
    m_server.setResponse(QStringLiteral("/manager-export-import-errors.qcow2"), 200, payload);
    const DistroImage image =
        testImage(QStringLiteral("/manager-export-import-errors.qcow2"), payload);

    auto runtime = std::make_unique<FakeQemuRuntime>();
    MachineManager::Options options;
    QVERIFY(configureSshOptions(&options, root));
    options.storePath = root + QStringLiteral("/podstack.db");
    options.machinesRoot = root + QStringLiteral("/machines");
    options.imageCacheDir = root + QStringLiteral("/images");
    options.qemuImgBinary = qemuImg;
    options.ovmfCodePath = ovmfCode;
    options.ovmfVarsPath = ovmfVars;
    options.sshPortStart = freePortHint();
    options.runtime = std::move(runtime);
    options.resolveDistro = [image](const QString &, const QString &, PodStack::Error *error) {
        if (error) {
            *error = {};
        }
        return image;
    };

    MachineManager manager(std::move(options));

    CreateMachineRequest base;
    base.name = QStringLiteral("export-import-errors");
    base.distro = QStringLiteral("test");
    base.arch = QStringLiteral("amd64");
    base.user = QStringLiteral("podstack");
    base.diskBytes = 1024LL * 1024LL * 1024LL;

    PodStack::Error error;
    const MachineMeta created = manager.create(base, {}, {}, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));

    QVERIFY(QDir().mkpath(root + QStringLiteral("/archive-dir")));
    error = {};
    manager.exportMachine(created.id, root + QStringLiteral("/archive-dir"), false, {}, {}, &error);
    QCOMPARE(error.code, QStringLiteral("PODSTACK_INVALID_PARAMS"));

    QFile::remove(created.diskPath);
    error = {};
    manager.exportMachine(created.id, root + QStringLiteral("/missing-disk.tar.zst"), false, {}, {},
                          &error);
    QCOMPARE(error.code, QStringLiteral("PODSTACK_IO_ERROR"));

    CreateMachineRequest secondRequest = base;
    secondRequest.name = QStringLiteral("export-import-source");
    const MachineMeta second = manager.create(secondRequest, {}, {}, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));

    const QString archive = root + QStringLiteral("/machine.tar.zst");
    error = {};
    manager.exportMachine(second.id, archive, false, {}, {}, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));

    error = {};
    manager.exportMachine(
        second.id, root + QStringLiteral("/cancelled.tar.zst"), false, {}, [] { return true; },
        &error);
    QCOMPARE(error.code, QStringLiteral("PODSTACK_CANCELLED"));

    error = {};
    manager.clone(second.id, QStringLiteral("cancelled-clone"), {}, [] { return true; }, &error);
    QCOMPARE(error.code, QStringLiteral("PODSTACK_CANCELLED"));

    error = {};
    manager.importMachine(archive, QStringLiteral("Bad Name"), false, {}, {}, &error);
    QCOMPARE(error.code, QStringLiteral("PODSTACK_INVALID_PARAMS"));

    error = {};
    manager.importMachine(archive, second.name, false, {}, {}, &error);
    QCOMPARE(error.code, QStringLiteral("PODSTACK_CONFLICT"));

    error = {};
    const MachineMeta imported = manager.importMachine(archive, second.name, true, {}, {}, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QVERIFY(!imported.id.isEmpty());
    QVERIFY(imported.id != second.id);

    error = {};
    manager.importMachine(root, QStringLiteral("invalid-archive"), false, {}, {}, &error);
    QCOMPARE(error.code, QStringLiteral("PODSTACK_INVALID_PARAMS"));
}

void MachinesTest::machineManagerExportConcurrency()
{
    const QString root = m_root + QStringLiteral("/manager-export-concurrency");
    QDir(root).removeRecursively();
    QVERIFY(QDir().mkpath(root));

    QString ovmfCode;
    QString ovmfVars;
    QVERIFY(createFirmwareFiles(root, &ovmfCode, &ovmfVars));
    const QString qemuImg = createQemuImgScript(root);
    QVERIFY(!qemuImg.isEmpty());

    const QByteArray payload(1024, 'x');
    m_server.setResponse(QStringLiteral("/manager-export-concurrency.qcow2"), 200, payload);
    const DistroImage image =
        testImage(QStringLiteral("/manager-export-concurrency.qcow2"), payload);

    auto runtime = std::make_unique<FakeQemuRuntime>();
    MachineManager::Options options;
    QVERIFY(configureSshOptions(&options, root));
    options.storePath = root + QStringLiteral("/podstack.db");
    options.machinesRoot = root + QStringLiteral("/machines");
    options.imageCacheDir = root + QStringLiteral("/images");
    options.qemuImgBinary = qemuImg;
    options.ovmfCodePath = ovmfCode;
    options.ovmfVarsPath = ovmfVars;
    options.sshPortStart = freePortHint();
    options.runtime = std::move(runtime);
    options.resolveDistro =
        [image](const QString &, const QString &, PodStack::Error *) { return image; };

    MachineManager manager(std::move(options));

    CreateMachineRequest request;
    request.name = QStringLiteral("export-concurrent");
    request.distro = QStringLiteral("test");
    request.arch = QStringLiteral("amd64");
    request.user = QStringLiteral("podstack");
    request.diskBytes = 1024LL * 1024LL * 1024LL;

    PodStack::Error error;
    const MachineMeta created = manager.create(request, {}, {}, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QCOMPARE(created.state, QStringLiteral("running"));

    const QString started = root + QStringLiteral("/qemu-img-started");
    const QString release = root + QStringLiteral("/qemu-img-release");
    QFile::remove(started);
    QFile::remove(release);
    qputenv("PODSTACK_QEMU_IMG_STARTED", started.toUtf8());
    qputenv("PODSTACK_QEMU_IMG_BLOCK_RELEASE", release.toUtf8());

    const QString archive = root + QStringLiteral("/concurrent.tar.zst");
    PodStack::Error exportError;
    std::thread exporter([&] {
        manager.exportMachine(created.id, archive, false, {}, {}, &exportError);
    });

    QElapsedTimer timer;
    timer.start();
    while (!QFileInfo::exists(started) && timer.elapsed() < 5000) {
        QTest::qWait(20);
    }
    QVERIFY2(QFileInfo::exists(started), "qemu-img convert não chegou a iniciar");

    PodStack::Error conflictError;
    const MachineMeta blocked = manager.start(created.id, &conflictError);
    QCOMPARE(conflictError.code, QStringLiteral("PODSTACK_CONFLICT"));
    QCOMPARE(blocked.state, QStringLiteral("stopped"));

    QFile releaseFile(release);
    QVERIFY(releaseFile.open(QIODevice::WriteOnly));
    releaseFile.close();
    exporter.join();

    qunsetenv("PODSTACK_QEMU_IMG_STARTED");
    qunsetenv("PODSTACK_QEMU_IMG_BLOCK_RELEASE");

    QVERIFY2(exportError.isEmpty(), qPrintable(exportError.message));
    QVERIFY(QFileInfo(archive).isFile());
    QCOMPARE(manager.get(created.id, &error).state, QStringLiteral("running"));
}

void MachinesTest::machineManagerImportChecksum()
{
    const QString root = m_root + QStringLiteral("/manager-import-checksum");
    QDir(root).removeRecursively();
    QVERIFY(QDir().mkpath(root));

    QString ovmfCode;
    QString ovmfVars;
    QVERIFY(createFirmwareFiles(root, &ovmfCode, &ovmfVars));
    const QString qemuImg = createQemuImgScript(root);
    QVERIFY(!qemuImg.isEmpty());

    const QByteArray payload(1024, 'c');
    m_server.setResponse(QStringLiteral("/manager-import-checksum.qcow2"), 200, payload);
    const DistroImage image =
        testImage(QStringLiteral("/manager-import-checksum.qcow2"), payload);

    auto runtime = std::make_unique<FakeQemuRuntime>();
    MachineManager::Options options;
    QVERIFY(configureSshOptions(&options, root));
    options.storePath = root + QStringLiteral("/podstack.db");
    options.machinesRoot = root + QStringLiteral("/machines");
    options.imageCacheDir = root + QStringLiteral("/images");
    options.qemuImgBinary = qemuImg;
    options.ovmfCodePath = ovmfCode;
    options.ovmfVarsPath = ovmfVars;
    options.sshPortStart = freePortHint();
    options.runtime = std::move(runtime);
    options.resolveDistro =
        [image](const QString &, const QString &, PodStack::Error *) { return image; };

    MachineManager manager(std::move(options));

    CreateMachineRequest request;
    request.name = QStringLiteral("checksum-src");
    request.distro = QStringLiteral("test");
    request.arch = QStringLiteral("amd64");
    request.user = QStringLiteral("podstack");
    request.diskBytes = 1024LL * 1024LL * 1024LL;

    PodStack::Error error;
    const MachineMeta created = manager.create(request, {}, {}, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));

    const QString archive = root + QStringLiteral("/checksum.tar.zst");
    const MachineMeta exported = manager.exportMachine(created.id, archive, false, {}, {},
                                                       &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QCOMPARE(exported.state, QStringLiteral("running"));

    const QString tampered = root + QStringLiteral("/tampered.tar.zst");
    QVERIFY(rewriteArchiveEntry(archive, tampered, QStringLiteral("disk.qcow2"),
                                QByteArrayLiteral("FAKE-QCOW2")));
    const MachineMeta corrupted =
        manager.importMachine(tampered, QStringLiteral("tampered-import"), false, {}, {},
                              &error);
    QVERIFY(corrupted.id.isEmpty());
    QCOMPARE(error.code, QStringLiteral("PODSTACK_IO_ERROR"));
    QVERIFY(error.message.contains(QStringLiteral("checksum")));

    const MachineMeta missing =
        manager.importMachine(root + QStringLiteral("/missing.tar.zst"),
                              QStringLiteral("missing-import"), false, {}, {}, &error);
    QVERIFY(missing.id.isEmpty());
    QCOMPARE(error.code, QStringLiteral("PODSTACK_INVALID_PARAMS"));

    const MachineMeta cancelled = manager.importMachine(
        archive, QStringLiteral("cancelled-import"), false, {}, [] { return true; }, &error);
    QVERIFY(cancelled.id.isEmpty());
    QCOMPARE(error.code, QStringLiteral("PODSTACK_CANCELLED"));

    QCOMPARE(manager.list(&error).size(), 1);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));

    const MachineMeta restored =
        manager.importMachine(archive, QStringLiteral("checksum-ok"), false, {}, {}, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QCOMPARE(restored.state, QStringLiteral("stopped"));
    QCOMPARE(manager.list(&error).size(), 2);
}

void MachinesTest::machineManagerCancelled()
{
    const QString root = m_root + QStringLiteral("/manager-cancelled");
    QDir(root).removeRecursively();
    QVERIFY(QDir().mkpath(root));

    QString ovmfCode;
    QString ovmfVars;
    QVERIFY(createFirmwareFiles(root, &ovmfCode, &ovmfVars));
    const QString qemuImg = createQemuImgScript(root);
    QVERIFY(!qemuImg.isEmpty());

    const QByteArray payload(2 * 1024 * 1024, 'c');
    m_server.setResponse(QStringLiteral("/manager-cancelled.qcow2"), 200, payload);
    const DistroImage image =
        testImage(QStringLiteral("/manager-cancelled.qcow2"), payload);

    auto runtime = std::make_unique<FakeQemuRuntime>();
    FakeQemuRuntime *fake = runtime.get();
    MachineManager::Options options;
    QVERIFY(configureSshOptions(&options, root));
    options.storePath = root + QStringLiteral("/podstack.db");
    options.machinesRoot = root + QStringLiteral("/machines");
    options.imageCacheDir = root + QStringLiteral("/images");
    options.qemuImgBinary = qemuImg;
    options.ovmfCodePath = ovmfCode;
    options.ovmfVarsPath = ovmfVars;
    options.sshPortStart = freePortHint();
    options.runtime = std::move(runtime);
    options.resolveDistro =
        [image](const QString &, const QString &, PodStack::Error *) { return image; };

    const QString machinesRoot = options.machinesRoot;
    const QString storePath = options.storePath;
    MachineManager manager(std::move(options));

    CreateMachineRequest request;
    request.name = QStringLiteral("cancelled");
    request.distro = QStringLiteral("test");
    request.arch = QStringLiteral("amd64");
    request.user = QStringLiteral("podstack");
    request.diskBytes = 1024LL * 1024LL * 1024LL;

    bool cancelFlag = false;
    int progressCalls = 0;
    PodStack::Error error;
    const MachineMeta meta = manager.create(
        request,
        [&cancelFlag, &progressCalls](const QString &stage, int) {
            if (stage == QStringLiteral("image")) {
                ++progressCalls;
                cancelFlag = true;
            }
        },
        [&cancelFlag] { return cancelFlag; }, &error);

    QVERIFY(meta.id.isEmpty());
    QCOMPARE(error.code, QStringLiteral("PODSTACK_CANCELLED"));
    QVERIFY(progressCalls > 0);
    QCOMPARE(fake->startCount, 0);
    QVERIFY(!QFileInfo::exists(storePath));
    QVERIFY(!QFileInfo::exists(machinesRoot)
            || QDir(machinesRoot)
                   .entryList(QDir::AllEntries | QDir::NoDotAndDotDot)
                   .isEmpty());
}

void MachinesTest::machineManagerPort()
{
    const QString root = m_root + QStringLiteral("/manager-port");
    QDir(root).removeRecursively();
    QVERIFY(QDir().mkpath(root));

    QString ovmfCode;
    QString ovmfVars;
    QVERIFY(createFirmwareFiles(root, &ovmfCode, &ovmfVars));
    const QString qemuImg = createQemuImgScript(root);
    QVERIFY(!qemuImg.isEmpty());

    const QByteArray payload(2048, 'p');
    m_server.setResponse(QStringLiteral("/manager-port.qcow2"), 200, payload);
    const DistroImage image = testImage(QStringLiteral("/manager-port.qcow2"), payload);

    QTcpServer busy;
    QVERIFY(busy.listen(QHostAddress::LocalHost, 0));
    const int busyPort = busy.serverPort();
    if (busyPort > 65515) {
        QSKIP("porta efêmera próxima do limite superior");
    }

    auto runtime = std::make_unique<FakeQemuRuntime>();
    MachineManager::Options options;
    QVERIFY(configureSshOptions(&options, root));
    options.storePath = root + QStringLiteral("/podstack.db");
    options.machinesRoot = root + QStringLiteral("/machines");
    options.imageCacheDir = root + QStringLiteral("/images");
    options.qemuImgBinary = qemuImg;
    options.ovmfCodePath = ovmfCode;
    options.ovmfVarsPath = ovmfVars;
    options.sshPortStart = busyPort;
    options.runtime = std::move(runtime);
    options.resolveDistro =
        [image](const QString &, const QString &, PodStack::Error *) { return image; };

    MachineManager manager(std::move(options));

    CreateMachineRequest request;
    request.distro = QStringLiteral("test");
    request.arch = QStringLiteral("amd64");
    request.user = QStringLiteral("podstack");
    request.diskBytes = 1024LL * 1024LL * 1024LL;

    PodStack::Error error;
    request.name = QStringLiteral("port-one");
    const MachineMeta first = manager.create(request, {}, {}, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QCOMPARE(first.state, QStringLiteral("running"));
    QVERIFY(first.sshPort > busyPort);
    QVERIFY(first.sshPort <= busyPort + 19);

    request.name = QStringLiteral("port-two");
    const MachineMeta second = manager.create(request, {}, {}, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QCOMPARE(second.state, QStringLiteral("running"));
    QVERIFY(second.sshPort > busyPort);
    QVERIFY(second.sshPort <= busyPort + 19);
    QVERIFY(second.sshPort != first.sshPort);
}

void MachinesTest::machineManagerPorts()
{
    const QString root = m_root + QStringLiteral("/manager-ports");
    QDir(root).removeRecursively();
    QVERIFY(QDir().mkpath(root));

    QString ovmfCode;
    QString ovmfVars;
    QVERIFY(createFirmwareFiles(root, &ovmfCode, &ovmfVars));
    const QString qemuImg = createQemuImgScript(root);
    QVERIFY(!qemuImg.isEmpty());

    const QByteArray payload(2048, 'f');
    m_server.setResponse(QStringLiteral("/manager-ports.qcow2"), 200, payload);
    const DistroImage image = testImage(QStringLiteral("/manager-ports.qcow2"), payload);

    auto runtime = std::make_unique<FakeQemuRuntime>();
    FakeQemuRuntime *fake = runtime.get();
    MachineManager::Options options;
    QVERIFY(configureSshOptions(&options, root));
    options.storePath = root + QStringLiteral("/podstack.db");
    options.machinesRoot = root + QStringLiteral("/machines");
    options.imageCacheDir = root + QStringLiteral("/images");
    options.qemuImgBinary = qemuImg;
    options.ovmfCodePath = ovmfCode;
    options.ovmfVarsPath = ovmfVars;
    options.sshPortStart = freePortHint();
    options.runtime = std::move(runtime);
    options.resolveDistro =
        [image](const QString &, const QString &, PodStack::Error *) { return image; };

    const QString storePath = options.storePath;
    MachineManager manager(std::move(options));

    CreateMachineRequest request;
    request.distro = QStringLiteral("test");
    request.arch = QStringLiteral("amd64");
    request.user = QStringLiteral("podstack");
    request.diskBytes = 1024LL * 1024LL * 1024LL;

    PodStack::Error error;
    request.name = QStringLiteral("ports-a");
    const MachineMeta first = manager.create(request, {}, {}, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QCOMPARE(first.state, QStringLiteral("running"));
    QCOMPARE(first.ports.size(), 1);
    QCOMPARE(first.ports.at(0).guestPort, 22);
    QCOMPARE(first.ports.at(0).protocol, QStringLiteral("tcp"));
    QCOMPARE(first.ports.at(0).hostPort, first.sshPort);
    QVERIFY(!first.ports.at(0).id.isEmpty());

    request.name = QStringLiteral("ports-b");
    const MachineMeta second = manager.create(request, {}, {}, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QCOMPARE(second.state, QStringLiteral("running"));

    QSet<int> reserved{first.sshPort, second.sshPort};
    const auto nextPort = [&reserved]() {
        for (int attempt = 0; attempt < 50; ++attempt) {
            QTcpServer probe;
            if (!probe.listen(QHostAddress::LocalHost, 0)) {
                continue;
            }
            const int port = probe.serverPort();
            probe.close();
            if (port > 0 && !reserved.contains(port)) {
                reserved.insert(port);
                return port;
            }
        }
        return 0;
    };

    const int extraTcpPort = nextPort();
    const int extraUdpPort = nextPort();
    QVERIFY(extraTcpPort > 0);
    QVERIFY(extraUdpPort > 0);
    QVERIFY(extraTcpPort != extraUdpPort);

    MachinePort extraTcp;
    extraTcp.hostPort = extraTcpPort;
    extraTcp.guestPort = 8080;
    extraTcp.protocol = QStringLiteral("tcp");
    extraTcp.label = QStringLiteral("web");

    MachinePort extraUdp;
    extraUdp.hostPort = extraUdpPort;
    extraUdp.guestPort = 5353;
    extraUdp.protocol = QStringLiteral("udp");
    extraUdp.exposeLan = true;
    extraUdp.label = QStringLiteral("dns");

    const auto portByHost = [](const QVector<MachinePort> &ports, const QString &protocol,
                               int hostPort) {
        for (const MachinePort &port : ports) {
            if (port.hostPort == hostPort
                && port.protocol.compare(protocol, Qt::CaseInsensitive) == 0) {
                return port;
            }
        }
        return MachinePort{};
    };
    const auto sshPortOf = [](const QVector<MachinePort> &ports) {
        for (const MachinePort &port : ports) {
            if (port.guestPort == 22
                && port.protocol.compare(QStringLiteral("tcp"), Qt::CaseInsensitive) == 0) {
                return port;
            }
        }
        return MachinePort{};
    };
    const auto qemuPortByHost = [](const QVector<QemuPort> &ports, int hostPort) {
        for (const QemuPort &port : ports) {
            if (port.hostPort == hostPort) {
                return port;
            }
        }
        return QemuPort{};
    };

    const int stopsBefore = fake->stopCount;
    const int startsBefore = fake->startCount;
    MachineMeta updated = manager.setPorts(first.id, {extraTcp, extraUdp}, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QCOMPARE(updated.state, QStringLiteral("running"));
    QCOMPARE(updated.ports.size(), 3);
    const MachinePort updatedSsh = sshPortOf(updated.ports);
    QCOMPARE(updatedSsh.id, first.ports.at(0).id);
    QCOMPARE(updatedSsh.hostPort, first.sshPort);
    const MachinePort updatedTcp = portByHost(updated.ports, QStringLiteral("tcp"),
                                               extraTcpPort);
    QCOMPARE(updatedTcp.guestPort, 8080);
    QVERIFY(!updatedTcp.id.isEmpty());
    QCOMPARE(updatedTcp.label, QStringLiteral("web"));
    QVERIFY(!updatedTcp.exposeLan);
    const MachinePort updatedUdp = portByHost(updated.ports, QStringLiteral("udp"),
                                               extraUdpPort);
    QCOMPARE(updatedUdp.guestPort, 5353);
    QCOMPARE(updatedUdp.label, QStringLiteral("dns"));
    QVERIFY(updatedUdp.exposeLan);
    QCOMPARE(fake->stopCount, stopsBefore + 1);
    QCOMPARE(fake->startCount, startsBefore + 1);
    QCOMPARE(fake->lastSpec.ports.size(), 3);
    QCOMPARE(qemuPortByHost(fake->lastSpec.ports, first.sshPort).guestPort, 22);
    QCOMPARE(qemuPortByHost(fake->lastSpec.ports, extraTcpPort).hostAddress,
             QStringLiteral("127.0.0.1"));
    QCOMPARE(qemuPortByHost(fake->lastSpec.ports, extraUdpPort).hostAddress,
             QStringLiteral("0.0.0.0"));

    QCOMPARE(manager.get(first.id, &error).ports.size(), 3);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    const QString firstMetaPath =
        root + QStringLiteral("/machines/") + first.id + QStringLiteral("/meta.json");
    PodStack::Error metaError;
    QCOMPARE(MachineMetaFile::read(firstMetaPath, &metaError).ports.size(), 3);
    QVERIFY2(metaError.isEmpty(), qPrintable(metaError.message));
    QCOMPARE(machineRowCount(storePath, QStringLiteral("machine_ports"), first.id), 3);

    // A lista enviada não contém a SSH; ela continua preservada.
    const int stopsBeforeRemoval = fake->stopCount;
    MachineMeta trimmed = manager.setPorts(first.id, {updatedUdp}, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QCOMPARE(trimmed.state, QStringLiteral("running"));
    QCOMPARE(trimmed.ports.size(), 2);
    QCOMPARE(sshPortOf(trimmed.ports).id, first.ports.at(0).id);
    QCOMPARE(sshPortOf(trimmed.ports).guestPort, 22);
    QCOMPARE(portByHost(trimmed.ports, QStringLiteral("udp"), extraUdpPort).id, updatedUdp.id);
    QVERIFY(portByHost(trimmed.ports, QStringLiteral("tcp"), extraTcpPort).id.isEmpty());
    QCOMPARE(fake->stopCount, stopsBeforeRemoval + 1);
    QCOMPARE(machineRowCount(storePath, QStringLiteral("machine_ports"), first.id), 2);

    // Alterar a porta SSH (mesmo guest 22 com outro hostPort) é recusado.
    const int startsBeforeSsh = fake->startCount;
    MachinePort alteredSsh;
    alteredSsh.hostPort = first.sshPort + 1;
    alteredSsh.guestPort = 22;
    alteredSsh.protocol = QStringLiteral("tcp");
    (void)manager.setPorts(first.id, {alteredSsh}, &error);
    QCOMPARE(error.code, QStringLiteral("PODSTACK_INVALID_PARAMS"));
    QCOMPARE(fake->startCount, startsBeforeSsh);
    QCOMPARE(manager.get(first.id, &error).state, QStringLiteral("running"));

    // hostPort já usado por outra machine no mesmo protocolo → conflito.
    MachinePort conflict;
    conflict.hostPort = extraUdpPort;
    conflict.guestPort = 9090;
    conflict.protocol = QStringLiteral("udp");
    const int stopsBeforeConflict = fake->stopCount;
    const int startsBeforeConflict = fake->startCount;
    (void)manager.setPorts(second.id, {conflict}, &error);
    QCOMPARE(error.code, QStringLiteral("PODSTACK_CONFLICT"));
    QCOMPARE(fake->stopCount, stopsBeforeConflict);
    QCOMPARE(fake->startCount, startsBeforeConflict);
    QCOMPARE(manager.get(second.id, &error).state, QStringLiteral("running"));

    // Mesmo hostPort em outro protocolo é permitido.
    MachinePort crossProtocol;
    crossProtocol.hostPort = extraUdpPort;
    crossProtocol.guestPort = 9090;
    crossProtocol.protocol = QStringLiteral("tcp");
    const MachineMeta cross = manager.setPorts(second.id, {crossProtocol}, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QCOMPARE(cross.ports.size(), 2);
    const MachinePort storedCross = portByHost(cross.ports, QStringLiteral("tcp"), extraUdpPort);
    QCOMPARE(storedCross.hostPort, extraUdpPort);
    QVERIFY(!storedCross.id.isEmpty());
    QVERIFY(!sshPortOf(cross.ports).id.isEmpty());

    const auto rejects = [&manager, &second, &fake, &error](const MachinePort &port,
                                                             const QString &code) {
        const int stops = fake->stopCount;
        const int starts = fake->startCount;
        (void)manager.setPorts(second.id, {port}, &error);
        return error.code == code && fake->stopCount == stops && fake->startCount == starts;
    };

    MachinePort invalidHost;
    invalidHost.hostPort = 0;
    invalidHost.guestPort = 80;
    invalidHost.protocol = QStringLiteral("tcp");
    QVERIFY(rejects(invalidHost, QStringLiteral("PODSTACK_INVALID_PARAMS")));

    MachinePort invalidGuest;
    invalidGuest.hostPort = nextPort();
    invalidGuest.guestPort = 70000;
    invalidGuest.protocol = QStringLiteral("tcp");
    QVERIFY(rejects(invalidGuest, QStringLiteral("PODSTACK_INVALID_PARAMS")));

    MachinePort invalidProtocol;
    invalidProtocol.hostPort = nextPort();
    invalidProtocol.guestPort = 80;
    invalidProtocol.protocol = QStringLiteral("sctp");
    QVERIFY(rejects(invalidProtocol, QStringLiteral("PODSTACK_INVALID_PARAMS")));

    // Machine parada recebe as portas sem start.
    const MachineMeta stopped = manager.stop(second.id, false, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QCOMPARE(stopped.state, QStringLiteral("stopped"));
    const int stopsBeforeStopped = fake->stopCount;
    const int startsBeforeStopped = fake->startCount;
    MachinePort stoppedPort;
    stoppedPort.hostPort = nextPort();
    stoppedPort.guestPort = 8443;
    stoppedPort.protocol = QStringLiteral("tcp");
    stoppedPort.label = QStringLiteral("https");
    const MachineMeta stoppedUpdated = manager.setPorts(second.id, {storedCross, stoppedPort},
                                                        &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QCOMPARE(stoppedUpdated.state, QStringLiteral("stopped"));
    QCOMPARE(stoppedUpdated.ports.size(), 3);
    QCOMPARE(portByHost(stoppedUpdated.ports, QStringLiteral("tcp"), extraUdpPort).id,
             storedCross.id);
    QCOMPARE(portByHost(stoppedUpdated.ports, QStringLiteral("tcp"), stoppedPort.hostPort)
                 .guestPort,
             8443);
    QCOMPARE(sshPortOf(stoppedUpdated.ports).hostPort, second.sshPort);
    QCOMPARE(fake->stopCount, stopsBeforeStopped);
    QCOMPARE(fake->startCount, startsBeforeStopped);
    QCOMPARE(manager.get(second.id, &error).ports.size(), 3);

    QVERIFY2(manager.remove(first.id, true, &error), qPrintable(error.message));
    QVERIFY2(manager.remove(second.id, true, &error), qPrintable(error.message));
}

void MachinesTest::qemuRuntimeFallback()
{
    const QString id = QStringLiteral("0190f0f0-0000-7000-8000-000000001101");
    const QString foreignId = QStringLiteral("0190f0f0-0000-7000-8000-000000001102");
    const QString root = m_root + QStringLiteral("/runtime-fallback");
    QDir(root).removeRecursively();
    QVERIFY(QDir().mkpath(root));

    QemuSpec spec = qemuSpec(root);
    spec.machineId = id;
    spec.pidFile = root + QStringLiteral("/qemu.pid");
    spec.qmpSocketPath = root + QStringLiteral("/qmp.sock");
    QVERIFY(!QFileInfo::exists(spec.pidFile));

    FakeMachineProcess process;
    QVERIFY2(process.start(id) > 0, "não foi possível iniciar o processo fake");
    FakeMachineProcess foreign;
    QVERIFY2(foreign.start(foreignId, QStringLiteral("notpodstack:")) > 0,
             "não foi possível iniciar o processo estranho");

    QemuProcessRuntime runtime;
    QVERIFY(waitForMachinePid(&runtime, id, process.pid()));
    QTRY_VERIFY_WITH_TIMEOUT(
        QFileInfo(QStringLiteral("/proc/%1").arg(foreign.pid())).isDir(), 3000);

    // O fallback encontra o processo sem pidfile, mas ignora prefixos alheios.
    QCOMPARE(runtime.findPid(id), process.pid());
    QCOMPARE(runtime.runningMachines().value(id), process.pid());
    QCOMPARE(runtime.findPid(foreignId), 0);
    QVERIFY(!runtime.runningMachines().contains(foreignId));
    QVERIFY2(runtime.isRunning(spec, nullptr), "isRunning deveria usar o cmdline");
    QCOMPARE(runtime.pid(spec), QString::number(process.pid()));

    // status também deve reconhecer o processo sem pidfile (o QMP não existe,
    // mas o erro é de conexão, não de "machine parada").
    PodStack::Error statusError;
    QCOMPARE(runtime.status(spec, &statusError), QString());
    QCOMPARE(statusError.code, QStringLiteral("PODSTACK_IO_ERROR"));

    // stop encerra o processo mesmo sem pidfile.
    PodStack::Error error;
    QVERIFY2(runtime.stop(spec, true, &error), qPrintable(error.message));
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QVERIFY(waitForMachineGone(&runtime, id));
    QVERIFY(!runtime.isRunning(spec, nullptr));
    QVERIFY(runtime.pid(spec).isEmpty());

    // O processo de prefixo diferente permanece vivo (só o guard o encerra).
    QCOMPARE(runtime.findPid(foreignId), 0);
    QVERIFY(QFileInfo(QStringLiteral("/proc/%1").arg(foreign.pid())).isDir());
}

void MachinesTest::qemuRuntimePidFile()
{
    const QString id = QStringLiteral("0190f0f0-0000-7000-8000-00000000110a");
    const QString foreignId = QStringLiteral("0190f0f0-0000-7000-8000-00000000110b");
    const QString root = m_root + QStringLiteral("/runtime-pidfile");
    QDir(root).removeRecursively();
    QVERIFY(QDir().mkpath(root));

    QemuSpec spec = qemuSpec(root);
    spec.machineId = id;
    FakeMachineProcess process;
    QVERIFY2(process.start(id) > 0, "não foi possível iniciar o processo fake");
    FakeMachineProcess foreign;
    QVERIFY2(foreign.start(foreignId) > 0, "não foi possível iniciar o processo estranho");

    QemuProcessRuntime runtime;
    QVERIFY(waitForMachinePid(&runtime, id, process.pid()));
    QVERIFY(waitForMachinePid(&runtime, foreignId, foreign.pid()));

    QVERIFY(QDir().mkpath(QFileInfo(spec.pidFile).absolutePath()));
    QFile pidFile(spec.pidFile);
    QVERIFY(pidFile.open(QIODevice::WriteOnly | QIODevice::Truncate));
    pidFile.write(QByteArray::number(process.pid()));
    pidFile.close();

    QVERIFY(runtime.isRunning(spec, nullptr));
    QCOMPARE(runtime.pid(spec), QString::number(process.pid()));

    // Pidfile inválido (texto) e grande demais não podem ser aceitos.
    QVERIFY(pidFile.open(QIODevice::WriteOnly | QIODevice::Truncate));
    pidFile.write("not-a-pid");
    pidFile.close();
    QVERIFY(runtime.isRunning(spec, nullptr));
    QCOMPARE(runtime.pid(spec), QString::number(process.pid()));

    QVERIFY(pidFile.open(QIODevice::WriteOnly | QIODevice::Truncate));
    pidFile.write(QByteArray(80, '9'));
    pidFile.close();
    QVERIFY(runtime.isRunning(spec, nullptr));

    // Pidfile apontando para outro processo: o fallback pelo cmdline decide.
    QVERIFY(pidFile.open(QIODevice::WriteOnly | QIODevice::Truncate));
    pidFile.write(QByteArray::number(foreign.pid()));
    pidFile.close();
    QVERIFY(runtime.isRunning(spec, nullptr));
    QCOMPARE(runtime.pid(spec), QString::number(process.pid()));

    QVERIFY(runtime.terminate(QString(), 0, nullptr));
    QCOMPARE(runtime.findPid(QString()), 0);

    process.stop();
    QVERIFY(waitForMachineGone(&runtime, id));
    foreign.stop();
    QVERIFY(waitForMachineGone(&runtime, foreignId));

    // Sem processo e com pidfile obsoleto, stop limpa o arquivo.
    PodStack::Error stopError;
    QVERIFY(runtime.stop(spec, false, &stopError));
    QVERIFY(stopError.isEmpty());
    QVERIFY(!QFileInfo::exists(spec.pidFile));
}

void MachinesTest::qemuRuntimeStartErrors()
{
    const QString root = m_root + QStringLiteral("/runtime-start-errors");
    QDir(root).removeRecursively();
    QVERIFY(QDir().mkpath(root));

    QemuProcessRuntime runtime;
    PodStack::Error error;

    // Já em execução.
    const QString runningId = QStringLiteral("0190f0f0-0000-7000-8000-00000000110c");
    QemuSpec runningSpec = qemuSpec(root);
    runningSpec.machineId = runningId;
    FakeMachineProcess process;
    QVERIFY2(process.start(runningId) > 0, "não foi possível iniciar o processo fake");
    QVERIFY(waitForMachinePid(&runtime, runningId, process.pid()));
    QVERIFY(!runtime.start(runningSpec, &error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_CONFLICT"));
    process.stop();

    // Arquitetura inválida.
    QemuSpec invalidArch = qemuSpec(root);
    invalidArch.machineId = QStringLiteral("0190f0f0-0000-7000-8000-00000000110d");
    invalidArch.arch = QStringLiteral("sparc");
    error = {};
    QVERIFY(!runtime.start(invalidArch, &error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_INVALID_PARAMS"));

    // Binário ausente no PATH.
    QTemporaryDir emptyBin;
    QVERIFY(emptyBin.isValid());
    const QByteArray previousPath = qgetenv("PATH");
    qputenv("PATH", emptyBin.path().toUtf8());
    const auto restore = qScopeGuard([previousPath] { qputenv("PATH", previousPath); });

    QemuSpec missingBinary = qemuSpec(root);
    missingBinary.machineId = QStringLiteral("0190f0f0-0000-7000-8000-00000000110e");
    error = {};
    QVERIFY(!runtime.start(missingBinary, &error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_UNSUPPORTED"));
    QVERIFY(error.message.contains(QStringLiteral("não foi encontrado")));
}

void MachinesTest::qemuRuntimeFakeQemu()
{
    const QString id = QStringLiteral("0190f0f0-0000-7000-8000-00000000110f");
    const QString root = m_root + QStringLiteral("/runtime-fake-qemu");
    QDir(root).removeRecursively();
    QVERIFY(QDir().mkpath(root));

    QTemporaryDir bin;
    QVERIFY(bin.isValid());
    const QByteArray script = QByteArrayLiteral(R"(#!/usr/bin/env python3
import json, os, signal, socket, sys

args = sys.argv[1:]
def value(flag):
    try:
        return args[args.index(flag) + 1]
    except (ValueError, IndexError):
        return ""

pidfile = value("-pidfile")
chardev = value("-chardev")
qmp_path = ""
if "path=" in chardev:
    qmp_path = chardev.split("path=", 1)[1].split(",", 1)[0]

def cleanup(*_):
    for path in (pidfile, qmp_path):
        if path:
            try:
                os.unlink(path)
            except OSError:
                pass
    sys.exit(0)

signal.signal(signal.SIGTERM, cleanup)
signal.signal(signal.SIGINT, cleanup)
if pidfile:
    with open(pidfile, "w") as handle:
        handle.write(str(os.getpid()))
if not qmp_path:
    signal.pause()

server = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
try:
    os.unlink(qmp_path)
except OSError:
    pass
server.bind(qmp_path)
server.listen(1)
server.settimeout(30)

def send(conn, payload):
    conn.sendall(json.dumps(payload).encode() + b"\n")

while True:
    try:
        conn, _ = server.accept()
    except socket.timeout:
        cleanup()
    send(conn, {"QMP": {"version": {"qemu": {"major": 0, "minor": 0, "micro": 0},
                                    "package": ""}, "capabilities": []}})
    buffer = b""
    while True:
        data = conn.recv(4096)
        if not data:
            break
        buffer += data
        while b"\n" in buffer:
            line, buffer = buffer.split(b"\n", 1)
            if not line.strip():
                continue
            message = json.loads(line)
            command = message.get("execute")
            request_id = message.get("id")
            if command == "system_powerdown" or command == "quit":
                send(conn, {"return": {}, "id": request_id})
                cleanup()
            elif command == "query-status":
                send(conn, {"return": {"status": "running"}, "id": request_id})
            else:
                send(conn, {"return": {}, "id": request_id})
)");
    QVERIFY(
        writeTestFile(bin.filePath(QStringLiteral("qemu-system-x86_64")), script,
                      QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner));

    const QByteArray previousPath = qgetenv("PATH");
    qputenv("PATH", bin.path().toUtf8() + ':' + previousPath);
    const auto restore = qScopeGuard([previousPath] { qputenv("PATH", previousPath); });

    QemuSpec spec = qemuSpec(root);
    spec.machineId = id;
    spec.pidFile = root + QStringLiteral("/qemu.pid");
    spec.qmpSocketPath = root + QStringLiteral("/qmp.sock");
    spec.diskPath = root + QStringLiteral("/disk.qcow2");
    spec.seedPath = root + QStringLiteral("/seed.iso");
    QVERIFY(writeTestFile(spec.diskPath, QByteArrayLiteral("disk")));
    QVERIFY(writeTestFile(spec.seedPath, QByteArrayLiteral("seed")));

    QemuProcessRuntime runtime;
    PodStack::Error error;
    QVERIFY2(runtime.start(spec, &error),
             qPrintable(error.message + QStringLiteral(" ") + error.detail));
    QVERIFY(runtime.isRunning(spec, &error));
    QCOMPARE(runtime.status(spec, &error), QStringLiteral("running"));
    const qint64 pid = runtime.pid(spec).toLongLong();
    QVERIFY(pid > 0);

    // Powerdown gracioso: o fake QEMU responde e encerra.
    QVERIFY2(runtime.stop(spec, false, &error), qPrintable(error.message));
    QVERIFY(!runtime.isRunning(spec, &error));
    QVERIFY(QFileInfo::exists(spec.pidFile) == false);

    // Quit forçado via QMP.
    QVERIFY2(runtime.start(spec, &error), qPrintable(error.message));
    QVERIFY2(runtime.stop(spec, true, &error), qPrintable(error.message));
    QVERIFY(!runtime.isRunning(spec, &error));

    // Encerramento de órfão por cmdline (SIGTERM).
    QVERIFY2(runtime.start(spec, &error), qPrintable(error.message));
    const qint64 orphanPid = runtime.pid(spec).toLongLong();
    QVERIFY(orphanPid > 0);
    QVERIFY2(runtime.terminate(id, orphanPid, &error), qPrintable(error.message));
    QVERIFY(waitForMachineGone(&runtime, id));
}

void MachinesTest::machineManagerReconcile()
{
    const QString root = m_root + QStringLiteral("/manager-reconcile");
    QDir(root).removeRecursively();
    QVERIFY(QDir().mkpath(root));

    MachineManager::Options options;
    options.storePath = root + QStringLiteral("/podstack.db");
    options.machinesRoot = root + QStringLiteral("/machines");
    options.sshConfigPath = root + QStringLiteral("/ssh/config");
    const QString storePath = options.storePath;
    const QString machinesRoot = options.machinesRoot;
    MachineManager manager(std::move(options));

    const QString missingId = QStringLiteral("0190f0f0-0000-7000-8000-000000001103");
    const QString liveId = QStringLiteral("0190f0f0-0000-7000-8000-000000001104");
    const QString stoppedLiveId = QStringLiteral("0190f0f0-0000-7000-8000-000000001105");
    const QString orphanId = QStringLiteral("0190f0f0-0000-7000-8000-000000001106");
    const QString outsideId = QStringLiteral("0190f0f0-0000-7000-8000-000000001109");

    MachineMeta missing = testMachine(missingId, QStringLiteral("reconcile-missing"));
    missing.state = QStringLiteral("running");
    missing.lastBootAt = QStringLiteral("2026-11-09T10:00:00.000Z");
    QVERIFY(seedMachine(storePath, machinesRoot, missing));

    MachineMeta live = testMachine(liveId, QStringLiteral("reconcile-live"));
    live.state = QStringLiteral("starting");
    live.lastBootAt.clear();
    live.diskPath = machinesRoot + QLatin1Char('/') + liveId
                    + QStringLiteral("/disk.qcow2");
    QVERIFY(seedMachine(storePath, machinesRoot, live));

    MachineMeta stoppedLive = testMachine(stoppedLiveId, QStringLiteral("reconcile-adopt"));
    stoppedLive.state = QStringLiteral("stopped");
    QVERIFY(seedMachine(storePath, machinesRoot, stoppedLive));

    FakeMachineProcess liveProcess;
    QVERIFY2(liveProcess.start(liveId) > 0, "não foi possível iniciar o processo live");
    FakeMachineProcess stoppedProcess;
    QVERIFY2(stoppedProcess.start(stoppedLiveId) > 0,
             "não foi possível iniciar o processo adotado");
    FakeMachineProcess orphanProcess;
    QVERIFY2(orphanProcess.start(
                 orphanId, QString(),
                 machinesRoot + QLatin1Char('/') + orphanId) > 0,
             "não foi possível iniciar o órfão");
    FakeMachineProcess outsideProcess;
    QVERIFY2(outsideProcess.start(outsideId) > 0,
             "não foi possível iniciar o processo de outra instalação");

    QemuProcessRuntime scanner;
    QVERIFY(waitForMachinePid(&scanner, liveId, liveProcess.pid()));
    QVERIFY(waitForMachinePid(&scanner, stoppedLiveId, stoppedProcess.pid()));
    QVERIFY(waitForMachinePid(&scanner, orphanId, orphanProcess.pid()));
    QVERIFY(waitForMachinePid(&scanner, outsideId, outsideProcess.pid()));

    PodStack::Error error;
    manager.reconcile(&error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));

    const MachineMeta missingAfter = manager.get(missingId, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QCOMPARE(missingAfter.state, QStringLiteral("stopped"));
    QVERIFY(missingAfter.error.isEmpty());

    const MachineMeta liveAfter = manager.get(liveId, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QCOMPARE(liveAfter.state, QStringLiteral("running"));
    QVERIFY(!liveAfter.lastBootAt.isEmpty());
    QCOMPARE(manager.get(stoppedLiveId, &error).state, QStringLiteral("running"));

    QCOMPARE(MachineMetaFile::read(machinesRoot + QLatin1Char('/') + missingId
                                       + QStringLiteral("/meta.json"), &error)
                 .state,
             QStringLiteral("stopped"));
    QCOMPARE(MachineMetaFile::read(machinesRoot + QLatin1Char('/') + liveId
                                       + QStringLiteral("/meta.json"), &error)
                 .state,
             QStringLiteral("running"));

    QVERIFY(waitForMachineGone(&scanner, orphanId));
    QCOMPARE(scanner.findPid(liveId), liveProcess.pid());
    QCOMPARE(scanner.findPid(stoppedLiveId), stoppedProcess.pid());
    // Processo marcado com podstack:<id> de outra instalação não é tocado.
    QCOMPARE(scanner.findPid(outsideId), outsideProcess.pid());

    // Reconciliar de novo é idempotente.
    error = {};
    manager.reconcile(&error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QCOMPARE(manager.get(liveId, &error).state, QStringLiteral("running"));
    QVERIFY(waitForMachineGone(&scanner, orphanId));
    QCOMPARE(scanner.findPid(outsideId), outsideProcess.pid());
}

void MachinesTest::machineManagerStartFallback()
{
    const QString root = m_root + QStringLiteral("/manager-start-fallback");
    QDir(root).removeRecursively();
    QVERIFY(QDir().mkpath(root));

    QString ovmfCode;
    QString ovmfVars;
    QVERIFY(createFirmwareFiles(root, &ovmfCode, &ovmfVars));

    MachineManager::Options options;
    options.storePath = root + QStringLiteral("/podstack.db");
    options.machinesRoot = root + QStringLiteral("/machines");
    options.sshConfigPath = root + QStringLiteral("/ssh/config");
    options.ovmfCodePath = ovmfCode;
    options.ovmfVarsPath = ovmfVars;
    options.sshPortStart = freePortHint();
    const QString storePath = options.storePath;
    const QString machinesRoot = options.machinesRoot;
    MachineManager manager(std::move(options));

    const QString id = QStringLiteral("0190f0f0-0000-7000-8000-000000001107");
    const QString directory = machinesRoot + QLatin1Char('/') + id;
    MachineMeta meta = testMachine(id, QStringLiteral("start-fallback"));
    meta.state = QStringLiteral("stopped");
    meta.sshKeyPath.clear();
    meta.diskPath = directory + QStringLiteral("/disk.qcow2");
    meta.seedPath = directory + QStringLiteral("/seed.iso");
    QVERIFY(writeTestFile(meta.diskPath, QByteArrayLiteral("fake-disk")));
    QVERIFY(writeTestFile(meta.seedPath, QByteArrayLiteral("fake-seed")));
    QVERIFY(seedMachine(storePath, machinesRoot, meta));

    FakeMachineProcess process;
    QVERIFY2(process.start(id) > 0, "não foi possível iniciar o processo fake");

    QemuProcessRuntime scanner;
    QVERIFY(waitForMachinePid(&scanner, id, process.pid()));

    // Sem pidfile e com processo vivo, start adota em vez de lançar outro QEMU.
    PodStack::Error error;
    const MachineMeta started = manager.start(id, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QCOMPARE(started.state, QStringLiteral("running"));
    QVERIFY(!started.lastBootAt.isEmpty());
    QCOMPARE(scanner.findPid(id), process.pid());
    QVERIFY(QFileInfo(QStringLiteral("/proc/%1").arg(process.pid())).isDir());

    process.stop();
}

void MachinesTest::machineManagerRemoveOrphan()
{
    const QString root = m_root + QStringLiteral("/manager-remove-orphan");
    QDir(root).removeRecursively();
    QVERIFY(QDir().mkpath(root));

    MachineManager::Options options;
    options.storePath = root + QStringLiteral("/podstack.db");
    options.machinesRoot = root + QStringLiteral("/machines");
    options.sshConfigPath = root + QStringLiteral("/ssh/config");
    const QString storePath = options.storePath;
    const QString machinesRoot = options.machinesRoot;
    MachineManager manager(std::move(options));

    const QString id = QStringLiteral("0190f0f0-0000-7000-8000-000000001108");
    const QString directory = machinesRoot + QLatin1Char('/') + id;
    MachineMeta meta = testMachine(id, QStringLiteral("remove-orphan"));
    meta.state = QStringLiteral("running");
    meta.sshKeyPath = directory + QStringLiteral("/id_ed25519");
    QVERIFY(seedMachine(storePath, machinesRoot, meta));

    FakeMachineProcess process;
    QVERIFY2(process.start(id) > 0, "não foi possível iniciar o processo fake");

    QemuProcessRuntime scanner;
    QVERIFY(waitForMachinePid(&scanner, id, process.pid()));
    QVERIFY(!QFileInfo::exists(directory + QStringLiteral("/qemu.pid")));

    PodStack::Error error;
    QVERIFY2(manager.remove(id, false, &error), qPrintable(error.message));
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QVERIFY(waitForMachineGone(&scanner, id));
    QVERIFY(!QFileInfo::exists(directory));

    const MachineMeta removed = manager.get(id, &error);
    QVERIFY(removed.id.isEmpty());
    QCOMPARE(error.code, QStringLiteral("PODSTACK_NOT_FOUND"));
}

void MachinesTest::sshConfigPreserves()
{
    const QString root = m_root + QStringLiteral("/ssh-config");
    QDir(root).removeRecursively();
    QVERIFY(QDir().mkpath(root));

    const QString configPath = root + QStringLiteral("/home/.ssh/config");
    QVERIFY(QDir().mkpath(QFileInfo(configPath).absolutePath()));
    const QByteArray original = QByteArrayLiteral("Host github.com\n"
                                                  "  HostName github.com\n"
                                                  "  User git\n");
    QVERIFY(writeTestFile(configPath, original));

    MachineMeta meta = testMachine(QStringLiteral("0190f0f0-0000-7000-8000-000000000099"),
                                   QStringLiteral("ssh-demo"));
    meta.sshPort = 32222;
    meta.sshUser = QStringLiteral("alice");
    meta.sshKeyPath = root + QStringLiteral("/machines/id_ed25519");

    SshConfig config(configPath);
    PodStack::Error error;
    QVERIFY2(config.sync(meta, false, &error), qPrintable(error.message));
    QVERIFY(error.isEmpty());

    QFile file(configPath);
    QVERIFY(file.open(QIODevice::ReadOnly));
    QString content = QString::fromUtf8(file.readAll());
    file.close();
    QVERIFY(content.contains(QStringLiteral("Host github.com")));
    QVERIFY(content.contains(QStringLiteral("# >>> podstack: ssh-demo")));
    QVERIFY(content.contains(QStringLiteral("Host ssh-demo.podstack ssh-demo")));
    QVERIFY(content.contains(QStringLiteral("  Port 32222")));
    QVERIFY(content.contains(QStringLiteral("  User alice")));
    QVERIFY(content.contains(QStringLiteral("  IdentityFile ") + meta.sshKeyPath));
    QVERIFY(content.contains(QStringLiteral("  IdentitiesOnly yes")));
    QVERIFY(content.contains(QStringLiteral("  UserKnownHostsFile ") +
                             QFileInfo(meta.sshKeyPath).absolutePath() +
                             QStringLiteral("/known_hosts")));
    QVERIFY(!content.contains(QStringLiteral("ForwardAgent yes")));
    QVERIFY(content.contains(QStringLiteral("# <<< podstack: ssh-demo")));
    QVERIFY(ownerOnly(QFileInfo(configPath).permissions(), false));
    QVERIFY(ownerOnly(QFileInfo(QFileInfo(configPath).absolutePath()).permissions(), true));

    QVERIFY2(config.sync(meta, false, &error), qPrintable(error.message));
    QVERIFY(file.open(QIODevice::ReadOnly));
    content = QString::fromUtf8(file.readAll());
    file.close();
    QCOMPARE(content.count(QStringLiteral("# >>> podstack: ssh-demo")), 1);
    QCOMPARE(content.count(QStringLiteral("Host ssh-demo.podstack ssh-demo")), 1);
    QCOMPARE(content.count(QStringLiteral("Host github.com")), 1);

    meta.sshPort = 33333;
    QVERIFY2(config.sync(meta, false, &error), qPrintable(error.message));
    QVERIFY(file.open(QIODevice::ReadOnly));
    content = QString::fromUtf8(file.readAll());
    file.close();
    QVERIFY(content.contains(QStringLiteral("  Port 33333")));
    QVERIFY(!content.contains(QStringLiteral("  Port 32222")));
    QCOMPARE(content.count(QStringLiteral("Host ssh-demo.podstack ssh-demo")), 1);

    QVERIFY2(config.sync(meta, true, &error), qPrintable(error.message));
    QVERIFY(file.open(QIODevice::ReadOnly));
    content = QString::fromUtf8(file.readAll());
    file.close();
    QVERIFY(content.contains(QStringLiteral("  ForwardAgent yes")));

    QVERIFY2(config.remove(QStringLiteral("ssh-demo"), &error), qPrintable(error.message));
    QVERIFY(file.open(QIODevice::ReadOnly));
    content = QString::fromUtf8(file.readAll());
    file.close();
    QVERIFY(!content.contains(QStringLiteral("podstack: ssh-demo")));
    QVERIFY(!content.contains(QStringLiteral("Host ssh-demo.podstack")));
    QVERIFY(content.contains(QStringLiteral("Host github.com")));

    // Remover de novo e remover de arquivo inexistente são sucessos.
    QVERIFY2(config.remove(QStringLiteral("ssh-demo"), &error), qPrintable(error.message));
    SshConfig missing(root + QStringLiteral("/missing/config"));
    QVERIFY2(missing.remove(QStringLiteral("ssh-demo"), &error), qPrintable(error.message));

    meta.name = QStringLiteral("Bad_Name");
    QVERIFY(!config.sync(meta, false, &error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_INVALID_PARAMS"));
    meta.name = QStringLiteral("ssh-demo");
    meta.sshPort = 0;
    QVERIFY(!config.sync(meta, false, &error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_INVALID_PARAMS"));
}

void MachinesTest::sshConfigPrefixNames()
{
    const QString root = m_root + QStringLiteral("/ssh-config-prefix");
    QDir(root).removeRecursively();
    QVERIFY(QDir().mkpath(root));

    const QString configPath = root + QStringLiteral("/config");
    SshConfig config(configPath);
    PodStack::Error error;

    MachineMeta longer = testMachine(QStringLiteral("0190f0f0-0000-7000-8000-000000000071"),
                                     QStringLiteral("teste02"));
    longer.sshPort = 32222;
    longer.sshKeyPath = root + QStringLiteral("/machines/a/id_ed25519");
    QVERIFY2(config.sync(longer, false, &error), qPrintable(error.message));

    MachineMeta shorter = testMachine(QStringLiteral("0190f0f0-0000-7000-8000-000000000072"),
                                      QStringLiteral("teste"));
    shorter.sshPort = 32223;
    shorter.sshKeyPath = root + QStringLiteral("/machines/b/id_ed25519");
    QVERIFY2(config.sync(shorter, false, &error), qPrintable(error.message));

    QFile file(configPath);
    QVERIFY(file.open(QIODevice::ReadOnly));
    QString content = QString::fromUtf8(file.readAll());
    file.close();
    QCOMPARE(content.count(QStringLiteral("# >>> podstack: teste02")), 1);
    QCOMPARE(content.count(QStringLiteral("# >>> podstack: teste\n")), 1);
    QVERIFY(!content.contains(QStringLiteral("\n02\n")));

    QVERIFY2(config.remove(QStringLiteral("teste"), &error), qPrintable(error.message));
    QVERIFY(file.open(QIODevice::ReadOnly));
    content = QString::fromUtf8(file.readAll());
    file.close();
    QVERIFY(content.contains(QStringLiteral("# >>> podstack: teste02")));
    QVERIFY(content.contains(QStringLiteral("Host teste02.podstack teste02")));
    QVERIFY(!content.contains(QStringLiteral("# >>> podstack: teste\n")));
    QVERIFY(!content.contains(QStringLiteral("\n02\n")));
}

void MachinesTest::sshConfigErrors()
{
    const QString root = m_root + QStringLiteral("/ssh-config-errors");
    QDir(root).removeRecursively();
    QVERIFY(QDir().mkpath(root));

    const QString path = root + QStringLiteral("/config");
    SshConfig config(path);
    MachineMeta meta = testMachine(QStringLiteral("0190f0f0-0000-7000-8000-000000000040"),
                                   QStringLiteral("ssh-errors"));
    PodStack::Error error;

    error = {};
    QVERIFY(!config.remove(QStringLiteral("Bad_Name"), &error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_INVALID_PARAMS"));

    meta.sshUser = QStringLiteral("bad user");
    QVERIFY(!config.sync(meta, false, &error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_INVALID_PARAMS"));

    meta.sshUser = QStringLiteral("podstack");
    meta.sshKeyPath.clear();
    error = {};
    QVERIFY(!config.sync(meta, false, &error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_INVALID_PARAMS"));

    meta.sshKeyPath = QStringLiteral("/tmp/key with space");
    error = {};
    QVERIFY(!config.sync(meta, false, &error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_INVALID_PARAMS"));

    meta.sshKeyPath = root + QStringLiteral("/id_ed25519");
    error = {};
    QVERIFY2(config.sync(meta, false, &error), qPrintable(error.message));
    QVERIFY2(config.sync(meta, false, &error), qPrintable(error.message));

    const QString incomplete = root + QStringLiteral("/incomplete/config");
    QVERIFY(QDir().mkpath(QFileInfo(incomplete).absolutePath()));
    QVERIFY(writeTestFile(incomplete, QByteArrayLiteral("# >>> podstack: ssh-errors\n")));
    SshConfig incompleteConfig(incomplete);
    error = {};
    QVERIFY(!incompleteConfig.sync(meta, false, &error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_IO_ERROR"));
    error = {};
    QVERIFY(!incompleteConfig.remove(meta.name, &error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_IO_ERROR"));

    error = {};
    SshConfig directoryConfig(root);
    QVERIFY(!directoryConfig.sync(meta, false, &error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_IO_ERROR"));
    error = {};
    QVERIFY(!directoryConfig.remove(meta.name, &error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_IO_ERROR"));

    const QString parentFile = root + QStringLiteral("/not-a-dir");
    QVERIFY(writeTestFile(parentFile, QByteArrayLiteral("x")));
    error = {};
    SshConfig parentConfig(parentFile + QStringLiteral("/config"));
    QVERIFY(!parentConfig.sync(meta, false, &error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_IO_ERROR"));

    // Marcadores fora do início da linha não podem ser tratados como bloco.
    const QString markerPath = root + QStringLiteral("/markers/config");
    QVERIFY(writeTestFile(markerPath, QByteArrayLiteral("prefixo # >>> podstack: ssh-errors\n"
                                                        "# <<< podstack: ssh-errors\n")));
    SshConfig markerConfig(markerPath);
    error = {};
    QVERIFY2(markerConfig.sync(meta, false, &error), qPrintable(error.message));

    QVERIFY(writeTestFile(markerPath, QByteArrayLiteral("# >>> podstack: ssh-errors\n"
                                                        "prefixo # <<< podstack: ssh-errors\n")));
    error = {};
    QVERIFY2(markerConfig.sync(meta, false, &error), qPrintable(error.message));
}

void MachinesTest::machineManagerSsh()
{
    const QString root = m_root + QStringLiteral("/manager-ssh");
    QDir(root).removeRecursively();
    QVERIFY(QDir().mkpath(root));

    QString ovmfCode;
    QString ovmfVars;
    QVERIFY(createFirmwareFiles(root, &ovmfCode, &ovmfVars));
    const QString qemuImg = createQemuImgScript(root);
    QVERIFY(!qemuImg.isEmpty());

    const QByteArray payload(2048, 's');
    m_server.setResponse(QStringLiteral("/manager-ssh.qcow2"), 200, payload);
    const DistroImage image = testImage(QStringLiteral("/manager-ssh.qcow2"), payload);

    auto runtime = std::make_unique<FakeQemuRuntime>();
    MachineManager::Options options;
    QVERIFY(configureSshOptions(&options, root));
    options.storePath = root + QStringLiteral("/podstack.db");
    options.machinesRoot = root + QStringLiteral("/machines");
    options.imageCacheDir = root + QStringLiteral("/images");
    options.qemuImgBinary = qemuImg;
    options.ovmfCodePath = ovmfCode;
    options.ovmfVarsPath = ovmfVars;
    options.sshPortStart = freePortHint();
    options.runtime = std::move(runtime);
    options.resolveDistro =
        [image](const QString &, const QString &, PodStack::Error *) { return image; };

    MachineManager manager(std::move(options));
    CreateMachineRequest request;
    request.name = QStringLiteral("manager-ssh");
    request.distro = QStringLiteral("test");
    request.arch = QStringLiteral("amd64");
    request.user = QStringLiteral("podstack");
    request.diskBytes = 1024LL * 1024LL * 1024LL;
    request.forwardSshAgent = true;

    PodStack::Error error;
    const MachineMeta meta = manager.create(request, {}, {}, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QVERIFY(!meta.sshKeyPath.isEmpty());
    QCOMPARE(meta.sshKeyPath, root + QStringLiteral("/machines/") + meta.id
                                  + QStringLiteral("/id_ed25519"));
    QVERIFY(QFileInfo(meta.sshKeyPath).isFile());
    QVERIFY(ownerOnly(QFileInfo(meta.sshKeyPath).permissions(), false));

    const QFileInfo publicKeyInfo(meta.sshKeyPath + QStringLiteral(".pub"));
    QVERIFY(publicKeyInfo.isFile());
    QCOMPARE(publicKeyInfo.permissions(),
             QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ReadUser
                 | QFileDevice::WriteUser | QFileDevice::ReadGroup | QFileDevice::ReadOther);
    QFile publicKey(publicKeyInfo.absoluteFilePath());
    QVERIFY(publicKey.open(QIODevice::ReadOnly));
    const QString publicKeyText = QString::fromUtf8(publicKey.readAll()).trimmed();
    QVERIFY(publicKeyText.startsWith(QStringLiteral("ssh-ed25519 ")));

    QByteArray seedUserData;
    QVERIFY(readIsoEntry(meta.seedPath, QStringLiteral("user-data"), &seedUserData));
    QVERIFY(QString::fromUtf8(seedUserData).contains(publicKeyText));

    const QString configPath = root + QStringLiteral("/ssh/config");
    QFile config(configPath);
    QVERIFY(config.open(QIODevice::ReadOnly));
    const QString configText = QString::fromUtf8(config.readAll());
    config.close();
    QVERIFY(configText.contains(QStringLiteral("# >>> podstack: manager-ssh")));
    QVERIFY(configText.contains(QStringLiteral("  Port %1").arg(meta.sshPort)));
    QVERIFY(configText.contains(QStringLiteral("  ForwardAgent yes")));
    QCOMPARE(configText.count(QStringLiteral("# >>> podstack: manager-ssh")), 1);

    SshInfo info;
    QVERIFY2(manager.sshInfo(meta.name, &info, &error), qPrintable(error.message));
    QCOMPARE(info.host, QStringLiteral("127.0.0.1"));
    QCOMPARE(info.port, meta.sshPort);
    QCOMPARE(info.user, QStringLiteral("podstack"));
    QCOMPARE(info.identityFile, meta.sshKeyPath);
    QCOMPARE(info.forwardAgent, true);

    SshInfo byId;
    QVERIFY2(manager.sshInfo(meta.id, &byId, &error), qPrintable(error.message));
    QCOMPARE(byId.identityFile, meta.sshKeyPath);

    QVERIFY(!manager.sshInfo(QStringLiteral("missing"), &info, &error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_NOT_FOUND"));

    // start reaplica o bloco removido manualmente (idempotente).
    SshConfig configFile(configPath);
    QVERIFY2(configFile.remove(meta.name, &error), qPrintable(error.message));
    QVERIFY(config.open(QIODevice::ReadOnly));
    QVERIFY(!QString::fromUtf8(config.readAll()).contains(QStringLiteral("podstack: manager-ssh")));
    config.close();
    const MachineMeta started = manager.start(meta.id, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QCOMPARE(started.state, QStringLiteral("running"));
    QVERIFY(config.open(QIODevice::ReadOnly));
    QVERIFY(QString::fromUtf8(config.readAll()).contains(QStringLiteral("podstack: manager-ssh")));
    config.close();

    QVERIFY2(manager.remove(meta.id, true, &error), qPrintable(error.message));
    QVERIFY(config.open(QIODevice::ReadOnly));
    QVERIFY(!QString::fromUtf8(config.readAll()).contains(QStringLiteral("podstack: manager-ssh")));
    config.close();
    QVERIFY(!QFileInfo::exists(meta.sshKeyPath));
}

void MachinesTest::machineStats()
{
    const QString root = m_root + QStringLiteral("/machine-stats");
    QDir(root).removeRecursively();
    QVERIFY(QDir().mkpath(root));

    const QString keyPath = root + QStringLiteral("/id_ed25519");
    QFile key(keyPath);
    QVERIFY(key.open(QIODevice::WriteOnly | QIODevice::Truncate));
    key.write("FAKE-PRIVATE-KEY");
    key.close();
    QVERIFY(key.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner));

    const QString argsLog = root + QStringLiteral("/ssh-args.log");
    const QString counter = root + QStringLiteral("/ssh-counter");
    const QString failFlag = root + QStringLiteral("/ssh-fail");
    const QString fakeSsh = root + QStringLiteral("/ssh-fake.sh");
    QFile script(fakeSsh);
    QVERIFY(script.open(QIODevice::WriteOnly | QIODevice::Truncate));
    const QString scriptBody =
        QStringLiteral(
            "#!/bin/sh\n"
            "echo \"$@\" >> \"%1\"\n"
            "if [ -f \"%3\" ]; then\n"
            "  echo \"ssh: connect to host 127.0.0.1 port 32222: Connection refused\" >&2\n"
            "  exit 255\n"
            "fi\n"
            "count=0\n"
            "if [ -f \"%2\" ]; then count=$(cat \"%2\"); fi\n"
            "count=$((count + 1))\n"
            "echo \"$count\" > \"%2\"\n"
            "if [ \"$count\" -eq 1 ]; then\n"
            "  echo \"cpu  100 0 100 800 0 0 0 0\"\n"
            "  net=\"  eth0: 1000 10 0 0 0 0 0 0 500 5 0 0 0 0 0 0\"\n"
            "  disk=\"   8 0 vda 100 0 200 0 50 0 400 0 0 0 0 0 0 0 0 0 0\"\n"
            "else\n"
            "  echo \"cpu  200 0 200 1200 0 0 0 0\"\n"
            "  net=\"  eth0: 3000 20 0 0 0 0 0 0 2500 25 0 0 0 0 0 0\"\n"
            "  disk=\"   8 0 vda 300 0 600 0 60 0 800 0 0 0 0 0 0 0 0 0 0\"\n"
            "fi\n"
            "echo \"__PODSTACK_MEM__\"\n"
            "echo \"MemTotal:        2097152 kB\"\n"
            "echo \"MemAvailable:    1048576 kB\"\n"
            "echo \"__PODSTACK_NET__\"\n"
            "echo \"Inter-|   Receive                                                |  Transmit\"\n"
            "echo \" face |bytes    packets errs drop fifo frame compressed multicast|bytes    packets errs drop fifo colls carrier compressed\"\n"
            "echo \"$net\"\n"
            "echo \"__PODSTACK_DISK__\"\n"
            "echo \"$disk\"\n"
            "echo \"__PODSTACK_DF__\"\n"
            "echo \"Filesystem     1B-blocks        Used   Available Capacity Mounted on\"\n"
            "echo \"/dev/vda1    10000000000  2500000000  7500000000      25%% /\"\n")
            .arg(argsLog, counter, failFlag);
    script.write(scriptBody.toUtf8());
    script.close();
    QVERIFY(script.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner
                                  | QFileDevice::ExeOwner));

    qputenv("PODSTACK_SSH_BINARY", fakeSsh.toUtf8());

    MachineMeta meta;
    meta.id = QStringLiteral("stats-machine");
    meta.name = QStringLiteral("stats-demo");
    meta.state = QStringLiteral("running");
    meta.sshKeyPath = keyPath;
    meta.sshUser = QStringLiteral("podstack");
    meta.sshPort = 32222;

    SshInfo info;
    info.host = QStringLiteral("127.0.0.1");
    info.port = meta.sshPort;
    info.user = meta.sshUser;
    info.identityFile = keyPath;

    MachineStatsCollector collector;
    PodStack::Error error;
    const QJsonObject first = collector.sample(meta, info, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QCOMPARE(first.value(QStringLiteral("cpuPercent")).toDouble(), 0.0);
    QCOMPARE(first.value(QStringLiteral("memUsed")).toInteger(), 1073741824LL);
    QCOMPARE(first.value(QStringLiteral("memTotal")).toInteger(), 2147483648LL);
    QCOMPARE(first.value(QStringLiteral("memPercent")).toDouble(), 50.0);
    QCOMPARE(first.value(QStringLiteral("netRx")).toInteger(), 0);
    QCOMPARE(first.value(QStringLiteral("netTx")).toInteger(), 0);
    QCOMPARE(first.value(QStringLiteral("diskRead")).toInteger(), 0);
    QCOMPARE(first.value(QStringLiteral("diskWrite")).toInteger(), 0);
    QCOMPARE(first.value(QStringLiteral("diskUsed")).toInteger(), 2500000000LL);
    QCOMPARE(first.value(QStringLiteral("diskTotal")).toInteger(), 10000000000LL);
    QCOMPARE(first.value(QStringLiteral("diskPercent")).toDouble(), 25.0);
    QVERIFY(first.value(QStringLiteral("ts")).toInteger() > 0);

    QTest::qWait(1000);
    error = {};
    const QJsonObject second = collector.sample(meta, info, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QVERIFY2(second.value(QStringLiteral("cpuPercent")).toDouble() > 30.0
                 && second.value(QStringLiteral("cpuPercent")).toDouble() < 36.5,
             qPrintable(QString::number(
                 second.value(QStringLiteral("cpuPercent")).toDouble())));
    QVERIFY2(second.value(QStringLiteral("netRx")).toInteger() > 1000,
             qPrintable(QString::number(second.value(QStringLiteral("netRx")).toInteger())));
    QVERIFY2(second.value(QStringLiteral("netTx")).toInteger() > 1000,
             qPrintable(QString::number(second.value(QStringLiteral("netTx")).toInteger())));
    QVERIFY2(second.value(QStringLiteral("diskRead")).toInteger() > 100000,
             qPrintable(QString::number(second.value(QStringLiteral("diskRead")).toInteger())));
    QVERIFY2(second.value(QStringLiteral("diskWrite")).toInteger() > 100000,
             qPrintable(QString::number(second.value(QStringLiteral("diskWrite")).toInteger())));

    QFile argsFile(argsLog);
    QVERIFY(argsFile.open(QIODevice::ReadOnly));
    const QString argsText = QString::fromUtf8(argsFile.readAll());
    QVERIFY(argsText.contains(QStringLiteral("-i")));
    QVERIFY(argsText.contains(keyPath));
    QVERIFY(argsText.contains(QStringLiteral("-p 32222")));
    QVERIFY(argsText.contains(QStringLiteral("IdentitiesOnly=yes")));
    QVERIFY(argsText.contains(QStringLiteral("StrictHostKeyChecking=accept-new")));
    QVERIFY(argsText.contains(QStringLiteral("UserKnownHostsFile=") +
                              QFileInfo(keyPath).absolutePath() + QStringLiteral("/known_hosts")));
    QVERIFY(argsText.contains(QStringLiteral("BatchMode=yes")));
    QVERIFY(argsText.contains(QStringLiteral("ConnectTimeout=3")));
    QVERIFY(argsText.contains(QStringLiteral("podstack@127.0.0.1")));
    QVERIFY(argsText.contains(QStringLiteral("__PODSTACK_DF__")));
    QVERIFY(argsText.contains(QStringLiteral("df -B1 -P /")));

    collector.reset();
    error = {};
    const QJsonObject afterReset = collector.sample(meta, info, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QCOMPARE(afterReset.value(QStringLiteral("cpuPercent")).toDouble(), 0.0);
    QCOMPARE(afterReset.value(QStringLiteral("netRx")).toInteger(), 0);

    MachineMeta stopped = meta;
    stopped.state = QStringLiteral("stopped");
    error = {};
    QVERIFY(collector.sample(stopped, info, &error).isEmpty());
    QCOMPARE(error.code, QStringLiteral("PODSTACK_CONFLICT"));

    MachineMeta missingKey = meta;
    missingKey.sshKeyPath = root + QStringLiteral("/missing-key");
    error = {};
    QVERIFY(collector.sample(missingKey, info, &error).isEmpty());
    QCOMPARE(error.code, QStringLiteral("PODSTACK_IO_ERROR"));

    QFile fail(failFlag);
    QVERIFY(fail.open(QIODevice::WriteOnly | QIODevice::Truncate));
    fail.close();
    error = {};
    QVERIFY(collector.sample(meta, info, &error).isEmpty());
    QCOMPARE(error.code, QStringLiteral("PODSTACK_IO_ERROR"));
    QVERIFY(!error.detail.isEmpty());
    QFile::remove(failFlag);

    qputenv("PODSTACK_SSH_BINARY", "/nonexistent/podstack-ssh");
    error = {};
    QVERIFY(collector.sample(meta, info, &error).isEmpty());
    QCOMPARE(error.code, QStringLiteral("PODSTACK_UNSUPPORTED"));
    qunsetenv("PODSTACK_SSH_BINARY");
}

void MachinesTest::machineStatsErrors()
{
    const QString root = m_root + QStringLiteral("/machine-stats-errors");
    QDir(root).removeRecursively();
    QVERIFY(QDir().mkpath(root));

    const QString keyPath = root + QStringLiteral("/id_ed25519");
    QVERIFY(writeTestFile(keyPath, QByteArrayLiteral("FAKE-PRIVATE-KEY")));

    const QString fakeSsh = root + QStringLiteral("/ssh-fake.sh");
    QVERIFY(writeTestFile(
        fakeSsh,
        QByteArrayLiteral("#!/bin/sh\n"
                          "case \"$PODSTACK_STATS_MODE\" in\n"
                          "  no-markers) echo 'cpu  1 0 1 1'; exit 0;;\n"
                          "  bad-df)\n"
                          "    echo 'cpu  100 0 100 800'\n"
                          "    echo '__PODSTACK_MEM__'\n"
                          "    echo 'MemTotal: 2097152 kB'\n"
                          "    echo 'MemAvailable: 1048576 kB'\n"
                          "    echo '__PODSTACK_NET__'\n"
                          "    echo '  eth0: 1 0 0 0 0 0 0 0 2 0 0 0 0 0 0 0'\n"
                          "    echo '__PODSTACK_DISK__'\n"
                          "    echo '   8 0 vda 1 0 2 0 3 0 4 0 0 0 0 0 0 0 0 0 0'\n"
                          "    echo '__PODSTACK_DF__'\n"
                          "    echo 'Filesystem'\n"
                          "    exit 0;;\n"
                          "  big) head -c 2100000 /dev/zero | tr '\\0' 'x'; exit 0;;\n"
                          "  fail) echo 'ssh falhou' >&2; exit 255;;\n"
                          "esac\n"
                          "exit 0\n"),
        QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner));

    const QByteArray previousSsh = qgetenv("PODSTACK_SSH_BINARY");
    const QByteArray previousMode = qgetenv("PODSTACK_STATS_MODE");
    qputenv("PODSTACK_SSH_BINARY", fakeSsh.toUtf8());
    const auto restore = qScopeGuard([previousSsh, previousMode] {
        qputenv("PODSTACK_SSH_BINARY", previousSsh);
        if (previousMode.isEmpty()) {
            qunsetenv("PODSTACK_STATS_MODE");
        } else {
            qputenv("PODSTACK_STATS_MODE", previousMode);
        }
    });

    MachineMeta meta;
    meta.id = QStringLiteral("stats-errors");
    meta.name = QStringLiteral("stats-errors");
    meta.state = QStringLiteral("running");
    meta.sshKeyPath = keyPath;
    meta.sshUser = QStringLiteral("podstack");
    meta.sshPort = 32222;

    SshInfo info;
    info.host = QStringLiteral("127.0.0.1");
    info.port = meta.sshPort;
    info.user = meta.sshUser;
    info.identityFile = keyPath;

    MachineStatsCollector collector;
    PodStack::Error error;

    MachineMeta stopped = meta;
    stopped.state = QStringLiteral("stopped");
    QVERIFY(collector.sample(stopped, info, &error).isEmpty());
    QCOMPARE(error.code, QStringLiteral("PODSTACK_CONFLICT"));

    MachineMeta noKey = meta;
    noKey.sshKeyPath.clear();
    error = {};
    QVERIFY(collector.sample(noKey, info, &error).isEmpty());
    QCOMPARE(error.code, QStringLiteral("PODSTACK_IO_ERROR"));

    SshInfo noHost = info;
    noHost.host.clear();
    error = {};
    QVERIFY(collector.sample(meta, noHost, &error).isEmpty());
    QCOMPARE(error.code, QStringLiteral("PODSTACK_IO_ERROR"));

    const QByteArray savedPath = qgetenv("PATH");
    qputenv("PODSTACK_SSH_BINARY", "/nonexistent/ssh");
    qputenv("PATH", QByteArrayLiteral("/nonexistent"));
    error = {};
    QVERIFY(collector.sample(meta, info, &error).isEmpty());
    QCOMPARE(error.code, QStringLiteral("PODSTACK_UNSUPPORTED"));
    qputenv("PODSTACK_SSH_BINARY", fakeSsh.toUtf8());
    qputenv("PATH", savedPath);

    for (const QByteArray &mode : {QByteArrayLiteral("no-markers"), QByteArrayLiteral("bad-df"),
                                   QByteArrayLiteral("big"), QByteArrayLiteral("fail")}) {
        qputenv("PODSTACK_STATS_MODE", mode);
        error = {};
        QVERIFY2(collector.sample(meta, info, &error).isEmpty(), mode.constData());
        QCOMPARE(error.code, QStringLiteral("PODSTACK_IO_ERROR"));
    }
}

void MachinesTest::machineFiles()
{
    const QString root = m_root + QStringLiteral("/machine-files");
    QDir(root).removeRecursively();
    QVERIFY(QDir().mkpath(root));

    const QString keyPath = root + QStringLiteral("/id_ed25519");
    QFile key(keyPath);
    QVERIFY(key.open(QIODevice::WriteOnly | QIODevice::Truncate));
    key.write("FAKE-PRIVATE-KEY");
    key.close();
    QVERIFY(key.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner));

    const QString sshLog = root + QStringLiteral("/ssh-args.log");
    const QString failFlag = root + QStringLiteral("/fail");
    const QString fakeSsh = root + QStringLiteral("/ssh-fake.sh");
    QFile sshScript(fakeSsh);
    QVERIFY(sshScript.open(QIODevice::WriteOnly | QIODevice::Truncate));
    sshScript.write(
        QStringLiteral(
            "#!/bin/sh\n"
            "for arg in \"$@\"; do printf '%s\\n' \"$arg\" >> \"%1\"; done\n"
            "if [ -f \"%2\" ]; then\n"
            "  echo \"ssh: connect to host 127.0.0.1 port 32222: Connection refused\" >&2\n"
            "  exit 255\n"
            "fi\n"
            "echo \"total 8\"\n"
            "echo \"drwxr-xr-x 2 podstack podstack 4096 2026-11-09 10:00 docs\"\n"
            "echo \"-rw-r--r--. 1 podstack podstack 1234 2026-11-09 10:05 report.txt\"\n"
            "echo \"lrwxrwxrwx 1 podstack podstack 10 2026-11-09 10:06 latest -> report.txt\"\n"
            "echo \"-rw-r--r-- 1 podstack podstack 5 2026-11-09 10:07 it's ok.txt\"\n"
            "echo \"drwxr-xr-x 2 podstack podstack 4096 2026-11-09 10:00 .\"\n"
            "echo \"drwxr-xr-x 3 podstack podstack 4096 2026-11-09 09:00 ..\"\n"
            "exit 0\n")
            .arg(sshLog, failFlag)
            .toUtf8());
    sshScript.close();
    QVERIFY(sshScript.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner
                                     | QFileDevice::ExeOwner));

    const QString downloads = root + QStringLiteral("/downloads");
    QVERIFY(QDir().mkpath(downloads));
    const QString scpLog = root + QStringLiteral("/scp-args.log");
    const QString fakeScp = root + QStringLiteral("/scp-fake.sh");
    QFile scpScript(fakeScp);
    QVERIFY(scpScript.open(QIODevice::WriteOnly | QIODevice::Truncate));
    scpScript.write(
        QStringLiteral(
            "#!/bin/sh\n"
            "for arg in \"$@\"; do printf '%s\\n' \"$arg\" >> \"%1\"; done\n"
            "if [ -f \"%2\" ]; then\n"
            "  echo \"scp: fake failure\" >&2\n"
            "  exit 1\n"
            "fi\n"
            "for last in \"$@\"; do :; done\n"
            "case \"$last\" in\n"
            "  \"%3\"/*) : > \"$last\";;\n"
            "esac\n"
            "exit 0\n")
            .arg(scpLog, failFlag, downloads)
            .toUtf8());
    scpScript.close();
    QVERIFY(scpScript.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner
                                     | QFileDevice::ExeOwner));

    qputenv("PODSTACK_SSH_BINARY", fakeSsh.toUtf8());
    qputenv("PODSTACK_SCP_BINARY", fakeScp.toUtf8());

    SshInfo info;
    info.host = QStringLiteral("127.0.0.1");
    info.port = 32222;
    info.user = QStringLiteral("podstack");
    info.identityFile = keyPath;

    PodStack::Error error;
    QJsonArray items;
    QVERIFY2(MachineFiles::list(info, QStringLiteral("/home/podstack"), &items, &error),
             qPrintable(error.message));
    QCOMPARE(items.size(), 4);

    const auto findItem = [](const QJsonArray &array, const QString &name) {
        for (const QJsonValue &value : array) {
            if (value.toObject().value(QStringLiteral("name")).toString() == name) {
                return value.toObject();
            }
        }
        return QJsonObject();
    };

    const QJsonObject docs = findItem(items, QStringLiteral("docs"));
    QVERIFY(!docs.isEmpty());
    QCOMPARE(docs.value(QStringLiteral("directory")).toBool(), true);
    QCOMPARE(docs.value(QStringLiteral("path")).toString(),
             QStringLiteral("/home/podstack/docs"));

    const QJsonObject report = findItem(items, QStringLiteral("report.txt"));
    QVERIFY(!report.isEmpty());
    QCOMPARE(report.value(QStringLiteral("directory")).toBool(), false);
    QCOMPARE(report.value(QStringLiteral("size")).toInteger(), 1234LL);
    QCOMPARE(report.value(QStringLiteral("path")).toString(),
             QStringLiteral("/home/podstack/report.txt"));
    QCOMPARE(report.value(QStringLiteral("modified")).toString(),
             QStringLiteral("2026-11-09 10:05"));

    const QJsonObject latest = findItem(items, QStringLiteral("latest"));
    QVERIFY(!latest.isEmpty());
    QCOMPARE(latest.value(QStringLiteral("directory")).toBool(), false);
    QVERIFY(!findItem(items, QStringLiteral(".")).contains(QStringLiteral("name")));
    QVERIFY(!findItem(items, QStringLiteral("..")).contains(QStringLiteral("name")));
    QVERIFY(!findItem(items, QStringLiteral("it's ok.txt")).isEmpty());

    const auto readArguments = [](const QString &path) {
        QStringList values;
        QFile file(path);
        if (file.open(QIODevice::ReadOnly)) {
            values = QString::fromUtf8(file.readAll())
                         .split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        }
        return values;
    };

    const QStringList sshArguments = readArguments(sshLog);
    QVERIFY(sshArguments.contains(QStringLiteral("-i")));
    QVERIFY(sshArguments.contains(keyPath));
    QVERIFY(sshArguments.contains(QStringLiteral("-p")));
    QVERIFY(sshArguments.contains(QStringLiteral("32222")));
    QVERIFY(sshArguments.contains(QStringLiteral("IdentitiesOnly=yes")));
    QVERIFY(sshArguments.contains(QStringLiteral("StrictHostKeyChecking=accept-new")));
    QVERIFY(sshArguments.contains(QStringLiteral("UserKnownHostsFile=") +
                                  QFileInfo(keyPath).absolutePath() +
                                  QStringLiteral("/known_hosts")));
    QVERIFY(sshArguments.contains(QStringLiteral("BatchMode=yes")));
    QVERIFY(sshArguments.contains(QStringLiteral("ConnectTimeout=5")));
    QVERIFY(sshArguments.contains(QStringLiteral("podstack@127.0.0.1")));
    QVERIFY(sshArguments.contains(
        QStringLiteral("ls -la --time-style=long-iso -- '/home/podstack'")));

    QFile::remove(sshLog);
    error = {};
    QVERIFY2(MachineFiles::list(info, QStringLiteral("/tmp/it's"), &items, &error),
             qPrintable(error.message));
    QVERIFY(readArguments(sshLog).contains(
        QStringLiteral("ls -la --time-style=long-iso -- '/tmp/it'\\''s'")));

    for (const QString &invalidPath :
         {QString(), QStringLiteral("/tmp/bad\npath"), QStringLiteral("/tmp/bad\tpath")}) {
        error = {};
        QVERIFY(!MachineFiles::list(info, invalidPath, &items, &error));
        QCOMPARE(error.code, QStringLiteral("PODSTACK_INVALID_PARAMS"));
    }

    SshInfo invalidSsh = info;
    invalidSsh.port = 0;
    error = {};
    QVERIFY(!MachineFiles::list(invalidSsh, QStringLiteral("/"), &items, &error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_INVALID_PARAMS"));

    QFile fail(failFlag);
    QVERIFY(fail.open(QIODevice::WriteOnly | QIODevice::Truncate));
    fail.close();
    error = {};
    QVERIFY(!MachineFiles::list(info, QStringLiteral("/"), &items, &error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_IO_ERROR"));
    QVERIFY(!error.detail.isEmpty());
    QFile::remove(failFlag);

    qputenv("PODSTACK_SSH_BINARY", "/nonexistent/podstack-ssh");
    error = {};
    QVERIFY(!MachineFiles::list(info, QStringLiteral("/"), &items, &error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_UNSUPPORTED"));
    qputenv("PODSTACK_SSH_BINARY", fakeSsh.toUtf8());

    QFile::remove(scpLog);
    const QString destination = downloads + QStringLiteral("/report.txt");
    error = {};
    QVERIFY2(MachineFiles::download(info, QStringLiteral("/home/podstack/report.txt"),
                                    destination, &error),
             qPrintable(error.message));
    QVERIFY(QFileInfo::exists(destination));
    const QStringList downloadArguments = readArguments(scpLog);
    QVERIFY(downloadArguments.contains(QStringLiteral("-r")));
    QVERIFY(downloadArguments.contains(QStringLiteral("-P")));
    QVERIFY(downloadArguments.contains(QStringLiteral("32222")));
    QVERIFY(downloadArguments.contains(QStringLiteral("IdentitiesOnly=yes")));
    QVERIFY(downloadArguments.contains(QStringLiteral("UserKnownHostsFile=") +
                                       QFileInfo(keyPath).absolutePath() +
                                       QStringLiteral("/known_hosts")));
    QVERIFY(downloadArguments.contains(QStringLiteral("ConnectTimeout=5")));
    QVERIFY(downloadArguments.contains(
        QStringLiteral("podstack@127.0.0.1:/home/podstack/report.txt")));
    QVERIFY(downloadArguments.contains(destination));

    QVERIFY(QDir().mkpath(downloads + QStringLiteral("/dir")));
    for (const QString &invalidDestination :
         {downloads + QStringLiteral("/sub/../report.txt"),
          QStringLiteral("report.txt"), downloads + QStringLiteral("/dir"),
          downloads + QStringLiteral("/"), QString()}) {
        error = {};
        QVERIFY(!MachineFiles::download(info, QStringLiteral("/remote.txt"),
                                        invalidDestination, &error));
        QCOMPARE(error.code, QStringLiteral("PODSTACK_INVALID_PARAMS"));
    }

    QFile uploadSource(root + QStringLiteral("/upload.txt"));
    QVERIFY(uploadSource.open(QIODevice::WriteOnly | QIODevice::Truncate));
    uploadSource.write("upload payload");
    uploadSource.close();

    QFile::remove(scpLog);
    error = {};
    QVERIFY2(MachineFiles::upload(info, uploadSource.fileName(),
                                  QStringLiteral("/tmp/upload name.txt"), &error),
             qPrintable(error.message));
    const QStringList uploadArguments = readArguments(scpLog);
    QVERIFY(uploadArguments.contains(uploadSource.fileName()));
    QVERIFY(uploadArguments.contains(
        QStringLiteral("podstack@127.0.0.1:/tmp/upload name.txt")));

    error = {};
    QVERIFY(!MachineFiles::upload(info, root + QStringLiteral("/missing.txt"),
                                  QStringLiteral("/tmp/dest"), &error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_IO_ERROR"));

    for (const QString &invalidSource :
         {QString(), QStringLiteral("relative.txt"),
          root + QStringLiteral("/sub/../upload.txt")}) {
        error = {};
        QVERIFY(!MachineFiles::upload(info, invalidSource, QStringLiteral("/tmp/dest"),
                                      &error));
        QCOMPARE(error.code, QStringLiteral("PODSTACK_INVALID_PARAMS"));
    }

    error = {};
    QVERIFY(!MachineFiles::upload(info, uploadSource.fileName(),
                                  QStringLiteral("/tmp/bad\npath"), &error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_INVALID_PARAMS"));

    QVERIFY(fail.open(QIODevice::WriteOnly | QIODevice::Truncate));
    fail.close();
    error = {};
    QVERIFY(!MachineFiles::download(info, QStringLiteral("/remote.txt"),
                                    downloads + QStringLiteral("/fail.txt"), &error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_IO_ERROR"));
    QVERIFY(!error.detail.isEmpty());
    QFile::remove(failFlag);

    qputenv("PODSTACK_SCP_BINARY", "/nonexistent/podstack-scp");
    error = {};
    QVERIFY(!MachineFiles::download(info, QStringLiteral("/remote.txt"),
                                    downloads + QStringLiteral("/x.txt"), &error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_UNSUPPORTED"));
    qunsetenv("PODSTACK_SCP_BINARY");
    qunsetenv("PODSTACK_SSH_BINARY");

    QDir(root).removeRecursively();
}

void MachinesTest::machineFilesErrors()
{
    const QString root = m_root + QStringLiteral("/machine-files-errors");
    QDir(root).removeRecursively();
    QVERIFY(QDir().mkpath(root));

    MachineFiles files;
    QJsonArray items;
    PodStack::Error error;
    SshInfo ssh;
    ssh.host = QStringLiteral("127.0.0.1");
    ssh.user = QStringLiteral("podstack");
    ssh.port = 32222;
    ssh.identityFile = root + QStringLiteral("/id_ed25519");
    QVERIFY(writeTestFile(ssh.identityFile, QByteArrayLiteral("key")));

    QVERIFY(!files.list(ssh, QStringLiteral("/"), nullptr, &error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_INVALID_PARAMS"));

    SshInfo badSsh = ssh;
    badSsh.host.clear();
    error = {};
    QVERIFY(!files.list(badSsh, QStringLiteral("/"), &items, &error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_INVALID_PARAMS"));

    error = {};
    QVERIFY(!files.list(ssh, QString(), &items, &error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_INVALID_PARAMS"));

    error = {};
    QVERIFY(!files.list(ssh, QStringLiteral("/tmp/\n"), &items, &error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_INVALID_PARAMS"));

    const QByteArray previousSsh = qgetenv("PODSTACK_SSH_BINARY");
    const QByteArray previousScp = qgetenv("PODSTACK_SCP_BINARY");
    const QByteArray previousPath = qgetenv("PATH");
    qputenv("PODSTACK_SSH_BINARY", "/nonexistent/ssh");
    qputenv("PODSTACK_SCP_BINARY", "/nonexistent/scp");
    qputenv("PATH", QByteArrayLiteral("/nonexistent"));
    const auto restore = qScopeGuard([previousSsh, previousScp, previousPath] {
        qputenv("PODSTACK_SSH_BINARY", previousSsh);
        qputenv("PODSTACK_SCP_BINARY", previousScp);
        qputenv("PATH", previousPath);
    });

    error = {};
    QVERIFY(!files.list(ssh, QStringLiteral("/"), &items, &error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_UNSUPPORTED"));

    error = {};
    QVERIFY(
        !files.download(ssh, QStringLiteral("/tmp/file"), root + QStringLiteral("/file"), &error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_UNSUPPORTED"));

    error = {};
    QVERIFY(!files.upload(ssh, ssh.identityFile, QStringLiteral("/tmp/file"), &error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_UNSUPPORTED"));

    qputenv("PODSTACK_SSH_BINARY", previousSsh);
    qputenv("PODSTACK_SCP_BINARY", previousScp);
    qputenv("PATH", previousPath);

    error = {};
    QVERIFY(!files.download(ssh, QStringLiteral("/tmp/file"), QStringLiteral("relative"), &error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_INVALID_PARAMS"));

    error = {};
    QVERIFY(!files.download(ssh, QStringLiteral("/tmp/file"), root + QStringLiteral("/a/../file"),
                            &error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_INVALID_PARAMS"));

    error = {};
    QVERIFY(!files.download(ssh, QStringLiteral("/tmp/file"), root + QLatin1Char('/'), &error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_INVALID_PARAMS"));

    error = {};
    QVERIFY(!files.download(ssh, QStringLiteral("/tmp/file"),
                            root + QStringLiteral("/missing/dir/file"), &error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_IO_ERROR"));

    QVERIFY(QDir().mkpath(root + QStringLiteral("/directory")));
    error = {};
    QVERIFY(!files.download(ssh, QStringLiteral("/tmp/file"), root + QStringLiteral("/directory"),
                            &error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_INVALID_PARAMS"));

    error = {};
    QVERIFY(!files.upload(ssh, QStringLiteral("relative"), QStringLiteral("/tmp/file"), &error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_INVALID_PARAMS"));

    error = {};
    QVERIFY(!files.upload(ssh, root + QStringLiteral("/missing-file"), QStringLiteral("/tmp/file"),
                          &error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_IO_ERROR"));
}

void MachinesTest::machineFilesIntegration()
{
    if (!qEnvironmentVariableIsSet("PODSTACK_MACHINE_FILES_INTEGRATION")) {
        QSKIP("defina PODSTACK_MACHINE_FILES_INTEGRATION=1 para usar SSH real");
    }
    const QString host = qEnvironmentVariable("PODSTACK_MACHINE_FILES_HOST").trimmed();
    const QString user = qEnvironmentVariable("PODSTACK_MACHINE_FILES_USER").trimmed();
    const QString key = qEnvironmentVariable("PODSTACK_MACHINE_FILES_KEY").trimmed();
    const int port = qEnvironmentVariableIntValue("PODSTACK_MACHINE_FILES_PORT");
    if (host.isEmpty() || user.isEmpty() || key.isEmpty() || port <= 0) {
        QSKIP("defina PODSTACK_MACHINE_FILES_HOST/USER/KEY/PORT para o SSH real");
    }

    SshInfo info;
    info.host = host;
    info.port = port;
    info.user = user;
    info.identityFile = key;

    PodStack::Error error;
    QJsonArray items;
    const QString path = qEnvironmentVariable("PODSTACK_MACHINE_FILES_PATH",
                                              QStringLiteral("/"));
    QVERIFY2(MachineFiles::list(info, path, &items, &error), qPrintable(error.message));
}

void MachinesTest::qemuRuntimeIntegration()
{
    if (!qEnvironmentVariableIsSet("PODSTACK_QEMU_INTEGRATION")) {
        QSKIP("defina PODSTACK_QEMU_INTEGRATION=1 para rodar o QEMU real");
    }
    if (QStandardPaths::findExecutable(QStringLiteral("qemu-system-x86_64")).isEmpty()) {
        QSKIP("qemu-system-x86_64 não encontrado");
    }
    if (QStandardPaths::findExecutable(QStringLiteral("qemu-img")).isEmpty()) {
        QSKIP("qemu-img não encontrado");
    }
    if (!QFileInfo(QStringLiteral("/dev/kvm")).isReadable()) {
        QSKIP("/dev/kvm indisponível");
    }

    const QString ovmfCode = QStringLiteral("/usr/share/edk2/ovmf/OVMF_CODE.fd");
    const QString ovmfVars = QStringLiteral("/usr/share/edk2/ovmf/OVMF_VARS.fd");
    if (!QFileInfo(ovmfCode).isFile() || !QFileInfo(ovmfVars).isFile()) {
        QSKIP("OVMF não encontrado");
    }

    const QString root = m_root + QStringLiteral("/qemu-runtime-integration");
    QDir(root).removeRecursively();
    QVERIFY(QDir().mkpath(root + QStringLiteral("/qemu")));
    const QString varsTarget = root + QStringLiteral("/qemu/OVMF_VARS.fd");
    QVERIFY(QFile::copy(ovmfVars, varsTarget));
    QVERIFY(QFile::setPermissions(varsTarget, QFileDevice::ReadOwner
                                                 | QFileDevice::WriteOwner));

    const QemuSpec base = qemuSpec(root);
    CloudInitOptions seed;
    seed.machineId = base.machineId;
    seed.hostname = QStringLiteral("podstack-integration");
    seed.user = QStringLiteral("podstack");
    PodStack::Error error;
    QVERIFY2(CloudInit::writeSeedIso(base.seedPath, seed, &error),
             qPrintable(error.message));

    const QStringList qemuImgArgs{
        QStringLiteral("create"),
        QStringLiteral("-f"),
        QStringLiteral("qcow2"),
        base.diskPath,
        QStringLiteral("1073741824"),
    };
    QCOMPARE(QProcess::execute(QStringLiteral("qemu-img"), qemuImgArgs), 0);

    QemuSpec spec = base;
    spec.kvm = true;
    spec.sshPort = static_cast<quint16>(freePortHint());
    spec.ports = {QemuPort{QStringLiteral("tcp"), spec.sshPort, 22,
                           QStringLiteral("127.0.0.1")}};
    spec.ovmfCodePath = ovmfCode;
    spec.ovmfVarsPath = varsTarget;

    const QString shareDir = root + QStringLiteral("/share");
    QVERIFY(QDir().mkpath(shareDir));
    const QString virtiofsSocket = root + QStringLiteral("/qemu/virtiofs-itest.sock");
    VirtiofsdProcess virtiofsd;
    if (!virtiofsd.start(QString(), shareDir, virtiofsSocket, QStringLiteral("itest"), false,
                         &error)) {
        QSKIP(qPrintable(QStringLiteral("virtiofsd indisponível: %1 %2")
                             .arg(error.message, error.detail)));
    }
    spec.mounts = {QemuMount{QStringLiteral("itest"), virtiofsSocket, false}};

    PodStack::QemuProcessRuntime runtime;
    const bool started = runtime.start(spec, &error);
    if (!started) {
        virtiofsd.stop(nullptr);
    }
    QVERIFY2(started, qPrintable(error.message + QStringLiteral(" ") + error.detail));
    QVERIFY(runtime.isRunning(spec, &error));
    QCOMPARE(runtime.status(spec, &error), QStringLiteral("running"));
    QVERIFY(!runtime.pid(spec).isEmpty());

    QVERIFY2(runtime.stop(spec, true, &error), qPrintable(error.message));
    QVERIFY(!runtime.isRunning(spec, &error));
    QVERIFY2(virtiofsd.stop(&error), qPrintable(error.message));
}

void MachinesTest::virtiofsdValidation()
{
    const QString root = m_root + QStringLiteral("/virtiofsd-validation");
    QDir(root).removeRecursively();
    QVERIFY(QDir().mkpath(root + QStringLiteral("/share")));
    QVERIFY(QDir().mkpath(root + QStringLiteral("/qemu")));
    const QString share = root + QStringLiteral("/share");
    const QString binary = createVirtiofsdScript(root);
    QVERIFY(!binary.isEmpty());

    VirtiofsdProcess process;
    PodStack::Error error;
    QVERIFY(!process.isRunning());
    QCOMPARE(process.pid(), 0);
    QVERIFY(process.stop(&error));

    QVERIFY(!process.start(binary, share, root + QStringLiteral("/qemu/bad-tag.sock"),
                           QStringLiteral("Bad_Tag"), false, &error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_INVALID_PARAMS"));

    error = {};
    QVERIFY(!process.start(binary, share, QStringLiteral("relative.sock"), QStringLiteral("host"),
                           false, &error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_INVALID_PARAMS"));

    error = {};
    const QString longSocket =
        root + QLatin1Char('/') + QString(120, QLatin1Char('a')) + QStringLiteral(".sock");
    QVERIFY(!process.start(binary, share, longSocket, QStringLiteral("host"), false, &error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_INVALID_PARAMS"));

    error = {};
    QVERIFY(!process.start(binary, root + QStringLiteral("/missing"),
                           root + QStringLiteral("/qemu/missing-source.sock"),
                           QStringLiteral("host"), false, &error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_INVALID_PARAMS"));

    error = {};
    QVERIFY(!process.start(binary, share, root + QStringLiteral("/missing-dir/socket.sock"),
                           QStringLiteral("host"), false, &error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_IO_ERROR"));

    QVERIFY(QDir().mkpath(root + QStringLiteral("/qemu/socket-dir.sock")));
    error = {};
    QVERIFY(!process.start(binary, share, root + QStringLiteral("/qemu/socket-dir.sock"),
                           QStringLiteral("host"), false, &error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_IO_ERROR"));

    const QString notExecutable = root + QStringLiteral("/not-executable");
    QVERIFY(writeTestFile(notExecutable, QByteArrayLiteral("#!/bin/sh\nexit 0\n")));
    error = {};
    QVERIFY(!process.start(notExecutable, share, root + QStringLiteral("/qemu/noexec.sock"),
                           QStringLiteral("host"), false, &error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_UNSUPPORTED"));

    const QString failing = root + QStringLiteral("/failing-virtiofsd");
    QVERIFY(
        writeTestFile(failing, QByteArrayLiteral("#!/bin/sh\nexit 1\n"),
                      QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner));
    error = {};
    QVERIFY(!process.start(failing, share, root + QStringLiteral("/qemu/failing.sock"),
                           QStringLiteral("host"), false, &error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_IO_ERROR"));

    const QString socketPath = root + QStringLiteral("/qemu/ok.sock");
    error = {};
    QVERIFY2(process.start(binary, share, socketPath, QStringLiteral("host"), false, &error),
             qPrintable(error.message + QStringLiteral(" ") + error.detail));
    QVERIFY(process.isRunning());
    QVERIFY(process.pid() > 0);

    error = {};
    QVERIFY(!process.start(binary, share, root + QStringLiteral("/qemu/second.sock"),
                           QStringLiteral("host2"), false, &error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_CONFLICT"));

    QVERIFY2(process.stop(&error), qPrintable(error.message));
    QVERIFY(!process.isRunning());
    QCOMPARE(process.pid(), 0);
    QVERIFY(process.stop(&error));
}

void MachinesTest::virtiofsdIntegration()
{
    PodStack::Error error;

    const QString root = m_root + QStringLiteral("/virtiofsd-integration");
    QDir(root).removeRecursively();
    QVERIFY(QDir().mkpath(root + QStringLiteral("/share")));

    const QString envBinary = root + QStringLiteral("/env-virtiofsd");
    const QByteArray previousVirtiofsdBinary = qgetenv("PODSTACK_VIRTIOFSD_BINARY");
    qputenv("PODSTACK_VIRTIOFSD_BINARY", envBinary.toUtf8());
    QCOMPARE(VirtiofsdProcess::resolveBinary(QString(), &error), envBinary);
    QVERIFY(error.isEmpty());
    if (previousVirtiofsdBinary.isEmpty()) {
        qunsetenv("PODSTACK_VIRTIOFSD_BINARY");
    } else {
        qputenv("PODSTACK_VIRTIOFSD_BINARY", previousVirtiofsdBinary);
    }

    VirtiofsdProcess missing;
    QVERIFY(!missing.start(root + QStringLiteral("/missing-virtiofsd"),
                           root + QStringLiteral("/share"),
                           root + QStringLiteral("/missing.sock"), QStringLiteral("itest"),
                           false, &error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_UNSUPPORTED"));
    QVERIFY(!error.remediation.isEmpty());

    const QString binary = VirtiofsdProcess::resolveBinary(QString(), &error);
    if (binary.isEmpty()) {
        QSKIP("virtiofsd não encontrado");
    }

    const QString socketPath = root + QStringLiteral("/virtiofs.sock");
    VirtiofsdProcess process;
    if (!process.start(QString(), root + QStringLiteral("/share"), socketPath,
                       QStringLiteral("itest"), true, &error)) {
        QSKIP(qPrintable(QStringLiteral("virtiofsd indisponível neste ambiente: %1 %2")
                             .arg(error.message, error.detail)));
    }

    QVERIFY(process.isRunning());
    QVERIFY(process.pid() > 0);
    QVERIFY(QFileInfo::exists(socketPath));

    QVERIFY2(process.stop(&error), qPrintable(error.message));
    QVERIFY(!process.isRunning());
    QVERIFY(!QFileInfo::exists(socketPath));
}

void MachinesTest::ubuntuOvmfCandidates()
{
    const QStringList codes = PodStack::MachineManager::ovmfCodeCandidates();
    QVERIFY(codes.contains(QStringLiteral("/usr/share/OVMF/OVMF_CODE_4M.fd")));

    const QStringList vars = PodStack::MachineManager::ovmfVarsCandidates();
    QVERIFY(vars.contains(QStringLiteral("/usr/share/OVMF/OVMF_VARS_4M.fd")));
}

QTEST_GUILESS_MAIN(MachinesTest)

#include "test_machines.moc"
