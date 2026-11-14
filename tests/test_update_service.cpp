// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/UpdateManifest.h"
#include "helper/UpdateService.h"

#include "UpdateTestUtils.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QScopeGuard>
#include <QStandardPaths>
#include <QSysInfo>
#include <QTemporaryDir>
#include <QTest>

#include <fcntl.h>
#include <sys/file.h>
#include <sys/types.h>
#include <unistd.h>

namespace {

using PodStack::UpdateService;

bool writeExecutable(const QString &path, const QByteArray &body)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    file.write("#!/bin/sh\n");
    file.write(body);
    file.write("\n");
    file.close();
    return file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner |
                               QFileDevice::ExeOwner);
}

QString hostFormat()
{
    return QFileInfo::exists(QStringLiteral("/etc/debian_version")) ? QStringLiteral("deb")
                                                                    : QStringLiteral("rpm");
}

QString hostArch()
{
    const QString arch = QSysInfo::buildCpuArchitecture();
    if (arch == QStringLiteral("x86_64")) {
        return QStringLiteral("x86_64");
    }
    if (arch == QStringLiteral("arm64")) {
        return QStringLiteral("aarch64");
    }
    return arch;
}

QByteArray sha256File(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return QCryptographicHash::hash(file.readAll(), QCryptographicHash::Sha256).toHex();
}

/// O helper real recebe o uid do cliente via SO_PEERCRED e recusa uid 0. Nos
/// containers do CI os testes rodam como root; nesse caso simulamos um usuário
/// comum e transferimos o artefato para ele.
uid_t testUid()
{
    const uid_t current = ::getuid();
    return current == 0 ? static_cast<uid_t>(1000) : current;
}

QByteArray signedManifest(const QJsonObject &root, const UpdateTestUtils::KeyPair &key,
                          QByteArray *signature)
{
    const QByteArray manifest = QJsonDocument(root).toJson(QJsonDocument::Compact);
    *signature = UpdateTestUtils::minisignFile(manifest, key, false);
    return manifest;
}

bool setTestOwner(const QString &path, uid_t uid)
{
    if (::getuid() != 0) {
        return true;
    }
    return ::chown(QFile::encodeName(path).constData(), uid, static_cast<gid_t>(-1)) == 0;
}

} // namespace

class UpdateServiceTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void stageAndStatus();
    void stageRejectsSignatureAndHash();
    void stageRejectsVersionAndIdentity();
    void stageRejectsArtifactVariants();
    void stageRejectsManifestInputs();
    void stageRejectsSignedManifestVariants();
    void stageRejectsReleaseKeyAndState();
    void stageReadsReleaseKeyFile();
    void stageRejectsEmptyPackageTool();
    void stageRejectsKnownInvalidChannel();
    void stageRejectsPackageTool();
    void statusAndInstallRejectInvalidJob();
    void installRejectsStateVariants();
    void logsEdgeCases();
    void logsReadRootFiles();
    void installRunsSystemdRun();
    void installRefusesActiveUnit();
    void installReportsSystemdRunFailure();

private:
    struct Fixture {
        QString artifact;
        QByteArray manifest;
        QByteArray signature;
        UpdateService::Options options;
        uid_t uid = 0;
    };

    Fixture makeFixture(const QString &name, const QString &version = QStringLiteral("0.1.5"),
                        bool valid = true);
    QString makeTool(const QString &root, const QString &format, const QString &name,
                     const QString &version, const QString &arch);
    void writeFakes(const QString &root);

    std::unique_ptr<QTemporaryDir> m_root;
    UpdateTestUtils::KeyPair m_key;
};

void UpdateServiceTest::initTestCase()
{
    m_root = std::make_unique<QTemporaryDir>();
    QVERIFY(m_root->isValid());
    m_key = UpdateTestUtils::generateKeyPair();
    QVERIFY(!m_key.publicKey.isEmpty());
}

void UpdateServiceTest::cleanupTestCase()
{
    m_root.reset();
}

void UpdateServiceTest::writeFakes(const QString &root)
{
    const QString systemctl = root + QStringLiteral("/systemctl");
    QVERIFY(writeExecutable(
        systemctl,
        QByteArrayLiteral("if [ \"$1\" = list-units ] && [ -n \"${FAKE_ACTIVE_UNIT:-}\" ]; "
                          "then echo \"$FAKE_ACTIVE_UNIT loaded active running\"; fi\nexit 0\n")));

    const QString systemdRun = root + QStringLiteral("/systemd-run");
    QVERIFY(writeExecutable(systemdRun,
                            QByteArrayLiteral("printf '%s\\n' \"$@\" > \"$FAKE_SYSTEMD_RUN_LOG\"\n"
                                              "exit \"${FAKE_SYSTEMD_RUN_EXIT:-0}\"\n")));
}

QString UpdateServiceTest::makeTool(const QString &root, const QString &format, const QString &name,
                                    const QString &version, const QString &arch)
{
    const QString path = root + QStringLiteral("/package-tool-") + format;
    QByteArray body;
    if (format == QStringLiteral("deb")) {
        body = QByteArrayLiteral("printf 'Package: %s\\nVersion: %s-1\\nArchitecture: %s\\n' "
                                 "\"${FAKE_PKG_NAME:-") +
               name.toUtf8() + QByteArrayLiteral("}\" \"${FAKE_PKG_VERSION:-") + version.toUtf8() +
               QByteArrayLiteral("}\" \"${FAKE_PKG_ARCH:-amd64}\"\n");
    } else {
        body = QByteArrayLiteral("printf '%s\\n%s\\n%s\\n' \"${FAKE_PKG_NAME:-") + name.toUtf8() +
               QByteArrayLiteral("}\" \"${FAKE_PKG_VERSION:-") + version.toUtf8() +
               QByteArrayLiteral("}\" \"${FAKE_PKG_ARCH:-x86_64}\"\n");
    }
    if (!writeExecutable(path, body)) {
        return {};
    }
    return path;
}

UpdateServiceTest::Fixture UpdateServiceTest::makeFixture(const QString &name,
                                                          const QString &version, bool valid)
{
    Fixture fixture;
    fixture.uid = testUid();
    const QString root = m_root->filePath(name);
    if (!QDir().mkpath(root)) {
        return {};
    }
    writeFakes(root);

    const QString format = hostFormat();
    const QString artifact =
        root + QStringLiteral("/podstack-%1.%2.%3").arg(version, hostArch(), format);
    QFile artifactFile(artifact);
    if (!artifactFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return {};
    }
    artifactFile.write("pacote de teste");
    artifactFile.close();
    if (!artifactFile.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner)) {
        return {};
    }
    if (::getuid() == 0) {
        const QByteArray path = QFile::encodeName(artifact);
        if (::chown(path.constData(), fixture.uid, static_cast<gid_t>(-1)) != 0) {
            return {};
        }
    }

    const QString sha = QString::fromLatin1(sha256File(artifact));
    QJsonObject release;
    release.insert(QStringLiteral("version"), version);
    release.insert(QStringLiteral("releaseUrl"),
                   QStringLiteral("https://example.test/tag/v%1").arg(version));
    release.insert(QStringLiteral("notes"), QStringLiteral("notas"));
    QJsonObject artifactObject;
    artifactObject.insert(QStringLiteral("arch"), hostArch());
    artifactObject.insert(QStringLiteral("format"), format);
    artifactObject.insert(QStringLiteral("url"),
                          QStringLiteral("https://example.test/download/v%1/podstack-%1.%2.%3")
                              .arg(version, hostArch(), format));
    artifactObject.insert(QStringLiteral("sha256"), sha);
    release.insert(QStringLiteral("artifacts"), QJsonArray{artifactObject});
    QJsonObject channels;
    channels.insert(QStringLiteral("stable"), release);
    QJsonObject manifest;
    manifest.insert(QStringLiteral("schemaVersion"), 1);
    manifest.insert(QStringLiteral("channels"), channels);
    fixture.manifest = QJsonDocument(manifest).toJson(QJsonDocument::Compact);
    fixture.signature = UpdateTestUtils::minisignFile(fixture.manifest, m_key, false);

    UpdateService::Options options;
    options.stateRoot = root + QStringLiteral("/state");
    options.stagingRoot = options.stateRoot;
    options.admissionLock = root + QStringLiteral("/admission.lock");
    options.updateLock = root + QStringLiteral("/update.lock");
    options.embeddedKey = UpdateTestUtils::publicKeyBase64(m_key);
    options.systemdRun = root + QStringLiteral("/systemd-run");
    options.systemctl = root + QStringLiteral("/systemctl");
    options.packageTool =
        makeTool(root, format, QStringLiteral("podstack"), version,
                 format == QStringLiteral("deb") ? QStringLiteral("amd64") : hostArch());
    options.wrapperPath = root + QStringLiteral("/wrapper");
    options.logRoot = root + QStringLiteral("/logs");
    options.maxArtifactBytes = 1024 * 1024;
    options.includeJournal = false;
    fixture.options = options;
    fixture.artifact = artifact;

    // Ajusta o nome do artefato para não coincidir com o glob de poderes.
    if (!valid) {
        fixture.signature =
            UpdateTestUtils::minisignFile(fixture.manifest + QByteArrayLiteral("x"), m_key, false);
    }
    return fixture;
}

void UpdateServiceTest::stageAndStatus()
{
    const Fixture fixture = makeFixture(QStringLiteral("stage"));
    UpdateService service(fixture.options);
    QVariantMap result;
    PodStack::Error error;
    QVERIFY2(service.stage(fixture.uid, fixture.manifest, fixture.signature, fixture.artifact,
                           QStringLiteral("stable"), &result, &error),
             qPrintable(error.message + QStringLiteral(" ") + error.detail));
    QVERIFY(!result.value(QStringLiteral("jobId")).toString().isEmpty());
    QVERIFY(error.isEmpty());

    const QString stateDir = result.value(QStringLiteral("stateDir")).toString();
    const QFileInfo stateFile(stateDir + QStringLiteral("/state.json"));
    QVERIFY(stateFile.isFile());
    QVERIFY((stateFile.permissions() & QFileDevice::ReadGroup) != 0 ||
            stateFile.permissions() == (QFileDevice::ReadOwner | QFileDevice::WriteOwner));
    QVERIFY(QFileInfo(stateDir).isDir());

    const QString artifact = stateDir + QStringLiteral("/artifact.") + hostFormat();
    QVERIFY(QFileInfo::exists(artifact));
    const QFileDevice::Permissions permissions = QFileInfo(artifact).permissions();
    QVERIFY(permissions.testFlag(QFileDevice::ReadOwner));
    QVERIFY(permissions.testFlag(QFileDevice::WriteOwner));
    QVERIFY(!permissions.testFlag(QFileDevice::WriteGroup));
    QVERIFY(!permissions.testFlag(QFileDevice::WriteOther));

    QVariantMap status;
    QVERIFY(service.status(fixture.uid, result.value(QStringLiteral("jobId")).toString(), &status,
                           &error));
    QCOMPARE(status.value(QStringLiteral("phase")).toString(), QStringLiteral("staged"));
    QCOMPARE(status.value(QStringLiteral("version")).toString(), QStringLiteral("0.1.5"));
}

void UpdateServiceTest::stageRejectsSignatureAndHash()
{
    Fixture fixture = makeFixture(QStringLiteral("rejects"));
    UpdateService service(fixture.options);
    QVariantMap result;
    PodStack::Error error;
    QVERIFY(!service.stage(fixture.uid, fixture.manifest, QByteArrayLiteral("bad"),
                           fixture.artifact, QStringLiteral("stable"), &result, &error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_UPDATE_SIGNATURE_INVALID"));

    QFile file(fixture.artifact);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    file.write("conteúdo diferente");
    file.close();
    error = {};
    QVERIFY(!service.stage(fixture.uid, fixture.manifest, fixture.signature, fixture.artifact,
                           QStringLiteral("stable"), &result, &error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_UPDATE_PACKAGE_INVALID"));
}

void UpdateServiceTest::stageRejectsVersionAndIdentity()
{
    Fixture same =
        makeFixture(QStringLiteral("same-version"), QString::fromLatin1(PODSTACK_VERSION));
    UpdateService sameService(same.options);
    QVariantMap result;
    PodStack::Error error;
    QVERIFY(!sameService.stage(same.uid, same.manifest, same.signature, same.artifact,
                               QStringLiteral("stable"), &result, &error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_UPDATE_VERSION_EQUAL"));

    Fixture older = makeFixture(QStringLiteral("older"), QStringLiteral("0.1.2"));
    UpdateService olderService(older.options);
    error = {};
    QVERIFY(!olderService.stage(older.uid, older.manifest, older.signature, older.artifact,
                                QStringLiteral("stable"), &result, &error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_UPDATE_MANIFEST_INVALID"));

    Fixture wrongName = makeFixture(QStringLiteral("wrong-name"));
    qputenv("FAKE_PKG_NAME", "outro-pacote");
    const auto restore = qScopeGuard([] { qunsetenv("FAKE_PKG_NAME"); });
    UpdateService wrongNameService(wrongName.options);
    error = {};
    QVERIFY(!wrongNameService.stage(wrongName.uid, wrongName.manifest, wrongName.signature,
                                    wrongName.artifact, QStringLiteral("stable"), &result, &error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_UPDATE_PACKAGE_INVALID"));
}

void UpdateServiceTest::stageRejectsArtifactVariants()
{
    Fixture fixture = makeFixture(QStringLiteral("variants"));
    UpdateService service(fixture.options);
    QVariantMap result;
    PodStack::Error error;

    QVERIFY(!service.stage(fixture.uid, fixture.manifest, fixture.signature,
                           fixture.artifact + QStringLiteral(".missing"), QStringLiteral("stable"),
                           &result, &error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_UPDATE_PACKAGE_INVALID"));

    const QString link = fixture.artifact + QStringLiteral(".link");
    QVERIFY(QFile::link(fixture.artifact, link));
    error = {};
    QVERIFY(!service.stage(fixture.uid, fixture.manifest, fixture.signature, link,
                           QStringLiteral("stable"), &result, &error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_UPDATE_PACKAGE_INVALID"));

    error = {};
    QVERIFY(!service.stage(fixture.uid, fixture.manifest, fixture.signature, fixture.artifact,
                           QStringLiteral("nightly"), &result, &error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_UPDATE_MANIFEST_INVALID"));

    UpdateService::Options small = fixture.options;
    small.maxArtifactBytes = 1;
    UpdateService smallService(small);
    error = {};
    QVERIFY(!smallService.stage(fixture.uid, fixture.manifest, fixture.signature, fixture.artifact,
                                QStringLiteral("stable"), &result, &error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_UPDATE_PACKAGE_INVALID"));

    Fixture wrongArch = makeFixture(QStringLiteral("wrong-arch"));
    qputenv("FAKE_PKG_ARCH", "s390x");
    const auto restore = qScopeGuard([] { qunsetenv("FAKE_PKG_ARCH"); });
    UpdateService wrongArchService(wrongArch.options);
    error = {};
    QVERIFY(!wrongArchService.stage(wrongArch.uid, wrongArch.manifest, wrongArch.signature,
                                    wrongArch.artifact, QStringLiteral("stable"), &result, &error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_UPDATE_PACKAGE_INVALID"));
}

void UpdateServiceTest::stageRejectsManifestInputs()
{
    const Fixture fixture = makeFixture(QStringLiteral("manifest-inputs"));
    UpdateService service(fixture.options);
    UpdateService defaultService;
    Q_UNUSED(defaultService);
    QVariantMap result;
    PodStack::Error error;

    QVERIFY(!service.stage(fixture.uid, QByteArray(), QByteArray(), fixture.artifact,
                           QStringLiteral("stable"), &result, &error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_UPDATE_MANIFEST_INVALID"));

    error = {};
    QVERIFY(!service.stage(0, fixture.manifest, fixture.signature, fixture.artifact,
                           QStringLiteral("stable"), &result, &error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_UPDATE_UNSUPPORTED"));

    error = {};
    const QByteArray huge(2 * 1024 * 1024, 'x');
    QVERIFY(!service.stage(fixture.uid, huge, QByteArrayLiteral("assinatura"), fixture.artifact,
                           QStringLiteral("stable"), &result, &error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_UPDATE_MANIFEST_INVALID"));

    error = {};
    QVERIFY(!service.stage(fixture.uid, fixture.manifest, fixture.signature,
                           QFileInfo(fixture.artifact).fileName(), QStringLiteral("stable"),
                           &result, &error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_UPDATE_PACKAGE_INVALID"));

    error = {};
    QVERIFY(!service.stage(fixture.uid, fixture.manifest, fixture.signature,
                           QStringLiteral("/etc/hosts"), QStringLiteral("stable"), &result,
                           &error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_UPDATE_PACKAGE_INVALID"));
}

void UpdateServiceTest::stageRejectsSignedManifestVariants()
{
    const Fixture fixture = makeFixture(QStringLiteral("signed-variants"));
    const QJsonObject root = QJsonDocument::fromJson(fixture.manifest).object();
    UpdateService service(fixture.options);
    QVariantMap result;
    PodStack::Error error;

    const QByteArray malformed = QByteArrayLiteral("not-json");
    const QByteArray malformedSignature = UpdateTestUtils::minisignFile(malformed, m_key, false);
    QVERIFY(!service.stage(fixture.uid, malformed, malformedSignature, fixture.artifact,
                           QStringLiteral("stable"), &result, &error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_UPDATE_MANIFEST_INVALID"));

    QByteArray signature;
    const QByteArray stableOnly = signedManifest(root, m_key, &signature);
    error = {};
    QVERIFY(!service.stage(fixture.uid, stableOnly, signature, fixture.artifact,
                           QStringLiteral("beta"), &result, &error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_UPDATE_MANIFEST_INVALID"));

    QJsonObject noArch = root;
    QJsonObject channels = noArch.value(QStringLiteral("channels")).toObject();
    QJsonObject release = channels.value(QStringLiteral("stable")).toObject();
    QJsonArray artifacts = release.value(QStringLiteral("artifacts")).toArray();
    QJsonObject artifact = artifacts.at(0).toObject();
    artifact.insert(QStringLiteral("arch"), QStringLiteral("s390x"));
    artifacts.replace(0, artifact);
    release.insert(QStringLiteral("artifacts"), artifacts);
    channels.insert(QStringLiteral("stable"), release);
    noArch.insert(QStringLiteral("channels"), channels);
    const QByteArray noArchManifest = signedManifest(noArch, m_key, &signature);
    error = {};
    QVERIFY(!service.stage(fixture.uid, noArchManifest, signature, fixture.artifact,
                           QStringLiteral("stable"), &result, &error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_UPDATE_PACKAGE_INVALID"));

    const QString wrongFormat = fixture.artifact + QStringLiteral(".txt");
    QVERIFY(QFile::copy(fixture.artifact, wrongFormat));
    QVERIFY(setTestOwner(wrongFormat, fixture.uid));
    error = {};
    QVERIFY(!service.stage(fixture.uid, fixture.manifest, fixture.signature, wrongFormat,
                           QStringLiteral("stable"), &result, &error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_UPDATE_PACKAGE_INVALID"));

    QJsonObject renamed = root;
    channels = renamed.value(QStringLiteral("channels")).toObject();
    release = channels.value(QStringLiteral("stable")).toObject();
    artifacts = release.value(QStringLiteral("artifacts")).toArray();
    artifact = artifacts.at(0).toObject();
    artifact.insert(QStringLiteral("url"), QStringLiteral("https://example.test/outro-pacote.bin"));
    artifacts.replace(0, artifact);
    release.insert(QStringLiteral("artifacts"), artifacts);
    channels.insert(QStringLiteral("stable"), release);
    renamed.insert(QStringLiteral("channels"), channels);
    const QByteArray renamedManifest = signedManifest(renamed, m_key, &signature);
    error = {};
    QVERIFY(!service.stage(fixture.uid, renamedManifest, signature, fixture.artifact,
                           QStringLiteral("stable"), &result, &error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_UPDATE_PACKAGE_INVALID"));
}

void UpdateServiceTest::stageRejectsReleaseKeyAndState()
{
    const Fixture fixture = makeFixture(QStringLiteral("release-key"));
    UpdateService::Options noKey = fixture.options;
    noKey.embeddedKey.clear();
    noKey.releaseKeyPath = fixture.options.stateRoot + QStringLiteral("/missing.pub");
    UpdateService noKeyService(noKey);
    QVariantMap result;
    PodStack::Error error;
    QVERIFY(!noKeyService.stage(fixture.uid, fixture.manifest, fixture.signature, fixture.artifact,
                                QStringLiteral("stable"), &result, &error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_UPDATE_UNSUPPORTED"));

    const Fixture blocked = makeFixture(QStringLiteral("state-blocked"));
    QFile blocker(blocked.options.stateRoot);
    QVERIFY(blocker.open(QIODevice::WriteOnly));
    blocker.write("x");
    blocker.close();
    UpdateService blockedService(blocked.options);
    error = {};
    QVERIFY(!blockedService.stage(blocked.uid, blocked.manifest, blocked.signature,
                                  blocked.artifact, QStringLiteral("stable"), &result, &error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_IO_ERROR"));
}

void UpdateServiceTest::stageReadsReleaseKeyFile()
{
    const Fixture fixture = makeFixture(QStringLiteral("release-file"));
    UpdateService::Options options = fixture.options;
    options.embeddedKey.clear();
    options.releaseKeyPath = QStringLiteral("/etc/hostname");
    UpdateService smallKey(options);
    QVariantMap result;
    PodStack::Error error;
    QVERIFY(!smallKey.stage(fixture.uid, fixture.manifest, fixture.signature, fixture.artifact,
                            QStringLiteral("stable"), &result, &error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_UPDATE_SIGNATURE_INVALID"));

    options.releaseKeyPath = QStringLiteral("/etc/services");
    UpdateService largeKey(options);
    error = {};
    QVERIFY(!largeKey.stage(fixture.uid, fixture.manifest, fixture.signature, fixture.artifact,
                            QStringLiteral("stable"), &result, &error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_UPDATE_MANIFEST_INVALID"));
}

void UpdateServiceTest::stageRejectsEmptyPackageTool()
{
    const Fixture fixture = makeFixture(QStringLiteral("package-tool-empty"));
    UpdateService::Options options = fixture.options;
    options.packageTool.clear();
    UpdateService service(options);
    QVariantMap result;
    PodStack::Error error;
    QVERIFY(!service.stage(fixture.uid, fixture.manifest, fixture.signature, fixture.artifact,
                           QStringLiteral("stable"), &result, &error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_UPDATE_PACKAGE_INVALID"));
}

void UpdateServiceTest::stageRejectsKnownInvalidChannel()
{
    const Fixture fixture = makeFixture(QStringLiteral("invalid-channel"));
    QJsonObject root = QJsonDocument::fromJson(fixture.manifest).object();
    QJsonObject channels = root.value(QStringLiteral("channels")).toObject();
    channels.insert(QStringLiteral("nightly"), channels.value(QStringLiteral("stable")));
    root.insert(QStringLiteral("channels"), channels);
    QByteArray signature;
    const QByteArray manifest = signedManifest(root, m_key, &signature);
    UpdateService service(fixture.options);
    QVariantMap result;
    PodStack::Error error;
    QVERIFY(!service.stage(fixture.uid, manifest, signature, fixture.artifact,
                           QStringLiteral("nightly"), &result, &error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_UPDATE_MANIFEST_INVALID"));
}

void UpdateServiceTest::stageRejectsPackageTool()
{
    const Fixture fixture = makeFixture(QStringLiteral("package-tool"));
    QVERIFY(QDir().mkpath(fixture.options.stateRoot));
    QVariantMap result;
    PodStack::Error error;

    UpdateService::Options missing = fixture.options;
    missing.packageTool = fixture.options.stateRoot + QStringLiteral("/missing-tool");
    UpdateService missingService(missing);
    QVERIFY(!missingService.stage(fixture.uid, fixture.manifest, fixture.signature,
                                  fixture.artifact, QStringLiteral("stable"), &result, &error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_UPDATE_PACKAGE_INVALID"));

    const QString failingTool = fixture.options.stateRoot + QStringLiteral("/failing-tool");
    QVERIFY(writeExecutable(failingTool, QByteArrayLiteral("exit 1\n")));
    UpdateService::Options failing = fixture.options;
    failing.packageTool = failingTool;
    UpdateService failingService(failing);
    error = {};
    QVERIFY(!failingService.stage(fixture.uid, fixture.manifest, fixture.signature,
                                  fixture.artifact, QStringLiteral("stable"), &result, &error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_UPDATE_PACKAGE_INVALID"));

    const QString incompleteTool = fixture.options.stateRoot + QStringLiteral("/incomplete-tool");
    QVERIFY(writeExecutable(incompleteTool, QByteArrayLiteral("printf 'Package: podstack\\n'\n")));
    UpdateService::Options incomplete = fixture.options;
    incomplete.packageTool = incompleteTool;
    UpdateService incompleteService(incomplete);
    error = {};
    QVERIFY(!incompleteService.stage(fixture.uid, fixture.manifest, fixture.signature,
                                     fixture.artifact, QStringLiteral("stable"), &result, &error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_UPDATE_PACKAGE_INVALID"));

    const QString wrongVersionTool =
        fixture.options.stateRoot + QStringLiteral("/wrong-version-tool");
    const QByteArray wrongVersionBody =
        hostFormat() == QStringLiteral("deb")
            ? QByteArrayLiteral(
                  "printf 'Package: podstack\\nVersion: 9.9.9\\nArchitecture: amd64\\n'\n")
            : QByteArrayLiteral("printf 'podstack\\n9.9.9\\nx86_64\\n'\n");
    QVERIFY(writeExecutable(wrongVersionTool, wrongVersionBody));
    UpdateService::Options wrongVersion = fixture.options;
    wrongVersion.packageTool = wrongVersionTool;
    UpdateService wrongVersionService(wrongVersion);
    error = {};
    QVERIFY(!wrongVersionService.stage(fixture.uid, fixture.manifest, fixture.signature,
                                       fixture.artifact, QStringLiteral("stable"), &result,
                                       &error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_UPDATE_PACKAGE_INVALID"));
}

void UpdateServiceTest::statusAndInstallRejectInvalidJob()
{
    const Fixture fixture = makeFixture(QStringLiteral("invalid-job"));
    UpdateService service(fixture.options);
    QVariantMap result;
    PodStack::Error error;

    QVERIFY(!service.status(fixture.uid, QStringLiteral("invalid"), &result, &error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_INVALID_PARAMS"));

    error = {};
    QVERIFY(!service.status(fixture.uid, QStringLiteral("update-missing"), &result, &error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_NOT_FOUND"));

    error = {};
    QVERIFY(!service.install(fixture.uid, QStringLiteral("invalid"), &result, &error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_INVALID_PARAMS"));

    error = {};
    QVERIFY(service.stage(fixture.uid, fixture.manifest, fixture.signature, fixture.artifact,
                          QStringLiteral("stable"), &result, &error));
    const QString jobId = result.value(QStringLiteral("jobId")).toString();
    QVariantMap status;
    error = {};
    QVERIFY(!service.status(fixture.uid + 1, jobId, &status, &error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_NOT_FOUND"));
}

void UpdateServiceTest::installRejectsStateVariants()
{
    const auto stageJob = [](const Fixture &fixture, UpdateService *service) {
        QVariantMap stage;
        PodStack::Error error;
        const bool ok = service->stage(fixture.uid, fixture.manifest, fixture.signature,
                                       fixture.artifact, QStringLiteral("stable"), &stage, &error);
        return ok ? stage : QVariantMap{};
    };
    const auto rewritePhase = [](const QString &stateDir, const QString &phase) {
        QFile file(stateDir + QStringLiteral("/state.json"));
        if (!file.open(QIODevice::ReadOnly)) {
            return false;
        }
        QJsonObject state = QJsonDocument::fromJson(file.readAll()).object();
        file.close();
        state.insert(QStringLiteral("phase"), phase);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            return false;
        }
        file.write(QJsonDocument(state).toJson(QJsonDocument::Compact));
        file.close();
        return true;
    };

    const Fixture wrongUid = makeFixture(QStringLiteral("install-wrong-uid"));
    UpdateService wrongUidService(wrongUid.options);
    QVariantMap wrongStage = stageJob(wrongUid, &wrongUidService);
    QVERIFY(!wrongStage.isEmpty());
    QVariantMap wrongInstall;
    PodStack::Error wrongError;
    QVERIFY(!wrongUidService.install(wrongUid.uid + 1,
                                     wrongStage.value(QStringLiteral("jobId")).toString(),
                                     &wrongInstall, &wrongError));
    QCOMPARE(wrongError.code, QStringLiteral("PODSTACK_NOT_FOUND"));

    const Fixture notStaged = makeFixture(QStringLiteral("install-not-staged"));
    UpdateService notStagedService(notStaged.options);
    QVariantMap stage = stageJob(notStaged, &notStagedService);
    QVERIFY(!stage.isEmpty());
    QVERIFY(rewritePhase(stage.value(QStringLiteral("stateDir")).toString(),
                         QStringLiteral("installing")));
    QVariantMap install;
    PodStack::Error error;
    QVERIFY(!notStagedService.install(
        notStaged.uid, stage.value(QStringLiteral("jobId")).toString(), &install, &error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_UPDATE_LOCKED"));

    const Fixture locked = makeFixture(QStringLiteral("install-locked"));
    UpdateService lockedService(locked.options);
    stage = stageJob(locked, &lockedService);
    QVERIFY(!stage.isEmpty());
    const int fd = ::open(QFile::encodeName(locked.options.admissionLock).constData(),
                          O_CREAT | O_RDWR | O_CLOEXEC, 0600);
    QVERIFY(fd >= 0);
    QCOMPARE(::flock(fd, LOCK_EX | LOCK_NB), 0);
    error = {};
    QVERIFY(!lockedService.install(locked.uid, stage.value(QStringLiteral("jobId")).toString(),
                                   &install, &error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_UPDATE_LOCKED"));
    ::flock(fd, LOCK_UN);
    ::close(fd);

    const Fixture badUnit = makeFixture(QStringLiteral("install-bad-unit"));
    UpdateService::Options noRun = badUnit.options;
    noRun.systemdRun = badUnit.options.stateRoot + QStringLiteral("/missing-systemd-run");
    UpdateService noRunService(noRun);
    stage = stageJob(badUnit, &noRunService);
    QVERIFY(!stage.isEmpty());
    error = {};
    QVERIFY(!noRunService.install(badUnit.uid, stage.value(QStringLiteral("jobId")).toString(),
                                  &install, &error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_UPDATE_INSTALL_FAILED"));
}

void UpdateServiceTest::logsEdgeCases()
{
    const Fixture fixture = makeFixture(QStringLiteral("logs-edge"));
    UpdateService service(fixture.options);
    const QString uidDir =
        fixture.options.logRoot + QLatin1Char('/') + QString::number(fixture.uid);
    QVERIFY(QDir().mkpath(uidDir));

    QFile big(uidDir + QStringLiteral("/update-big.jsonl"));
    QVERIFY(big.open(QIODevice::WriteOnly | QIODevice::Truncate));
    big.write(QByteArray(1024 * 1024 + 32, 'x'));
    big.close();

    QVariantList lines;
    bool truncated = false;
    PodStack::Error error;
    QVERIFY(service.logs(fixture.uid, QStringLiteral("update-big"), &lines, &truncated, &error));
    QVERIFY(truncated);
    QCOMPARE(lines.size(), 0);

    const QString oldPath = uidDir + QStringLiteral("/update-old.jsonl");
    QFile old(oldPath);
    QVERIFY(old.open(QIODevice::WriteOnly | QIODevice::Truncate));
    old.write("{}\n");
    old.flush();
    QVERIFY(old.setFileTime(QDateTime::currentDateTime().addDays(-10),
                            QFileDevice::FileModificationTime));
    old.close();
    error = {};
    truncated = false;
    QVERIFY(service.logs(fixture.uid, QStringLiteral("update-old"), &lines, &truncated, &error));
    QCOMPARE(lines.size(), 0);
    QVERIFY(!truncated);

    const QString stateDir = fixture.options.stateRoot + QStringLiteral("/update-corrupt");
    QVERIFY(QDir().mkpath(stateDir));
    QFile corrupt(stateDir + QStringLiteral("/state.json"));
    QVERIFY(corrupt.open(QIODevice::WriteOnly | QIODevice::Truncate));
    corrupt.write("not-json");
    corrupt.close();
    QVariantMap status;
    error = {};
    QVERIFY(!service.status(fixture.uid, QStringLiteral("update-corrupt"), &status, &error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_UPDATE_MANIFEST_INVALID"));
}

void UpdateServiceTest::logsReadRootFiles()
{
    const Fixture fixture = makeFixture(QStringLiteral("logs"));
    UpdateService service(fixture.options);
    QVERIFY(QDir().mkpath(fixture.options.logRoot));
    const QString fakeJournal = fixture.options.logRoot + QStringLiteral("/journalctl");
    QByteArray journalBody = QByteArrayLiteral("echo \"JOB_ID=update-test UID=") +
                             QByteArray::number(fixture.uid) +
                             QByteArrayLiteral(" Authorization: Bearer journal\"\n") +
                             QByteArrayLiteral("echo \"JOB_ID=other UID=") +
                             QByteArray::number(fixture.uid) + QByteArrayLiteral(" ruido\"\n");
    QVERIFY(writeExecutable(fakeJournal, journalBody));
    const QByteArray previousPath = qgetenv("PATH");
    qputenv("PATH", fixture.options.logRoot.toUtf8() + ':' + previousPath);
    const auto restorePath = qScopeGuard([previousPath] { qputenv("PATH", previousPath); });
    UpdateService::Options withJournal = fixture.options;
    withJournal.includeJournal = true;
    UpdateService journalService(withJournal);

    const QString uidDir =
        fixture.options.logRoot + QLatin1Char('/') + QString::number(fixture.uid);
    QVERIFY(QDir().mkpath(uidDir));
    QFile file(uidDir + QStringLiteral("/update-test.jsonl"));
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    file.write(QByteArrayLiteral(R"({"jobId":"update-test","uid":1000,)") +
               QByteArrayLiteral(R"("message":"Authorization: Bearer segredo"})") + '\n');
    file.close();

    QVariantList lines;
    bool truncated = true;
    PodStack::Error error;
    QVERIFY2(service.logs(fixture.uid, QStringLiteral("update-test"), &lines, &truncated, &error),
             qPrintable(error.message));
    QCOMPARE(lines.size(), 1);
    QVERIFY(!truncated);
    QVERIFY(!lines.first().toString().contains(QStringLiteral("segredo")));
    QVERIFY(lines.first().toString().contains(QStringLiteral("[redacted]")));

    QVariantList all;
    QVERIFY(service.logs(fixture.uid, QString(), &all, &truncated, &error));
    QCOMPARE(all.size(), 1);

    QVariantList journalLines;
    error = {};
    QVERIFY(journalService.logs(fixture.uid, QStringLiteral("update-test"), &journalLines,
                                &truncated, &error));
    QVERIFY(journalLines.size() >= 1);
    QVERIFY(!journalLines.last().toString().contains(QStringLiteral("segredo")));

    error = {};
    QVERIFY(!service.logs(fixture.uid, QStringLiteral("update-missing"), &all, &truncated, &error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_NOT_FOUND"));

    error = {};
    QVERIFY(!service.logs(fixture.uid, QStringLiteral("../etc/passwd"), &all, &truncated, &error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_INVALID_PARAMS"));
}

void UpdateServiceTest::installRunsSystemdRun()
{
    const Fixture fixture = makeFixture(QStringLiteral("install"));
    UpdateService service(fixture.options);
    QVariantMap stage;
    PodStack::Error error;
    QVERIFY2(service.stage(fixture.uid, fixture.manifest, fixture.signature, fixture.artifact,
                           QStringLiteral("stable"), &stage, &error),
             qPrintable(error.message));
    const QString jobId = stage.value(QStringLiteral("jobId")).toString();

    qputenv("FAKE_SYSTEMD_RUN_LOG",
            (fixture.options.stateRoot + QStringLiteral("/systemd-run.log")).toUtf8());
    qunsetenv("FAKE_ACTIVE_UNIT");
    QVariantMap install;
    QVERIFY2(service.install(fixture.uid, jobId, &install, &error), qPrintable(error.message));
    QVERIFY(install.value(QStringLiteral("unit"))
                .toString()
                .startsWith(QStringLiteral("podstack-update-")));

    QFile log(fixture.options.stateRoot + QStringLiteral("/systemd-run.log"));
    QVERIFY(log.open(QIODevice::ReadOnly));
    const QString arguments = QString::fromUtf8(log.readAll());
    QVERIFY(arguments.contains(QStringLiteral("--unit=podstack-update-")));
    QVERIFY(arguments.contains(QStringLiteral("flock")));
    QVERIFY(arguments.contains(fixture.options.wrapperPath));
    QVERIFY(arguments.contains(jobId));
    QVERIFY(arguments.contains(QStringLiteral("--artifact")));
}

void UpdateServiceTest::installRefusesActiveUnit()
{
    const Fixture fixture = makeFixture(QStringLiteral("active"));
    UpdateService service(fixture.options);
    QVariantMap stage;
    PodStack::Error error;
    QVERIFY(service.stage(fixture.uid, fixture.manifest, fixture.signature, fixture.artifact,
                          QStringLiteral("stable"), &stage, &error));
    qputenv("FAKE_ACTIVE_UNIT", "podstack-update-outra.service");
    const auto restore = qScopeGuard([] { qunsetenv("FAKE_ACTIVE_UNIT"); });
    QVariantMap install;
    QVERIFY(!service.install(fixture.uid, stage.value(QStringLiteral("jobId")).toString(), &install,
                             &error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_UPDATE_LOCKED"));
}

void UpdateServiceTest::installReportsSystemdRunFailure()
{
    const Fixture fixture = makeFixture(QStringLiteral("failure"));
    UpdateService service(fixture.options);
    QVariantMap stage;
    PodStack::Error error;
    QVERIFY(service.stage(fixture.uid, fixture.manifest, fixture.signature, fixture.artifact,
                          QStringLiteral("stable"), &stage, &error));
    qputenv("FAKE_SYSTEMD_RUN_LOG",
            (fixture.options.stateRoot + QStringLiteral("/systemd-run.log")).toUtf8());
    qputenv("FAKE_SYSTEMD_RUN_EXIT", "1");
    const auto restore = qScopeGuard([] {
        qunsetenv("FAKE_SYSTEMD_RUN_EXIT");
        qunsetenv("FAKE_SYSTEMD_RUN_LOG");
    });
    QVariantMap install;
    QVERIFY(!service.install(fixture.uid, stage.value(QStringLiteral("jobId")).toString(), &install,
                             &error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_UPDATE_INSTALL_FAILED"));
}

QTEST_GUILESS_MAIN(UpdateServiceTest)
#include "test_update_service.moc"
