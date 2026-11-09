// SPDX-License-Identifier: GPL-3.0-or-later

#include "MockHttpServer.h"

#include "core/Paths.h"
#include "machines/CloudImageCache.h"
#include "machines/CloudInit.h"
#include "machines/DistroRegistry.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>

#include <archive.h>
#include <archive_entry.h>

using PodStack::CloudImageCache;
using PodStack::CloudInit;
using PodStack::CloudInitOptions;
using PodStack::DistroImage;
using PodStack::DistroRegistry;

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

QTEST_GUILESS_MAIN(MachinesTest)

#include "test_machines.moc"
