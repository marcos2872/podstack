// SPDX-License-Identifier: GPL-3.0-or-later

#include "MockHttpServer.h"
#include "MockQmpServer.h"

#include "core/Paths.h"
#include "daemon/BuildStore.h"
#include "machines/CloudImageCache.h"
#include "machines/CloudInit.h"
#include "machines/DistroRegistry.h"
#include "machines/MachineMeta.h"
#include "machines/MachineStore.h"
#include "machines/QemuCommand.h"
#include "machines/QmpClient.h"

#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>

#include <archive.h>
#include <archive_entry.h>
#include <sqlite3.h>

using PodStack::BuildRecord;
using PodStack::BuildStore;
using PodStack::CloudImageCache;
using PodStack::CloudInit;
using PodStack::CloudInitOptions;
using PodStack::DistroImage;
using PodStack::DistroRegistry;
using PodStack::MachineMeta;
using PodStack::MachineMetaFile;
using PodStack::MachineMount;
using PodStack::MachinePort;
using PodStack::MachineStore;
using PodStack::QemuCommand;
using PodStack::QemuSpec;
using PodStack::QmpClient;

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
    void machineMetaInvalid();
    void machineStoreCrud();
    void machineStoreSharesDb();
    void downloadAndReuse();
    void downloadHttpError();
    void downloadChecksumMismatch();
    void cacheSidecarMissing();
    void downloadCancelled();
    void seedIsoEntries();
    void seedYamlDefaults();
    void seedPasswordHashed();
    void seedVerbatimUserData();
    void seedEscapesYaml();
    void qemuBinary();
    void qemuCommandKvm();
    void qemuCommandTcg();
    void qemuCommandArm64();
    void qemuCommandUefi();
    void qemuCommandIsolated();
    void qemuCommandInvalid();
    void qmpHandshake();
    void qmpQueryStatus();
    void qmpPowerdown();
    void qmpErrorResponse();
    void qmpInterleavedEvent();
    void qmpTimeout();
    void qmpQuit();
    void qmpConnectMissing();

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

QTEST_GUILESS_MAIN(MachinesTest)

#include "test_machines.moc"
