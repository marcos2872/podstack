// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/UpdateManifest.h"
#include "helper/UpdateService.h"

#include "UpdateTestUtils.h"

#include <QCryptographicHash>
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
    return file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner
                               | QFileDevice::ExeOwner);
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
    void statusAndInstallRejectInvalidJob();
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

    Fixture makeFixture(const QString &name, const QString &version = QStringLiteral("0.1.4"),
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
    QVERIFY(writeExecutable(
        systemdRun,
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
                                 "\"${FAKE_PKG_NAME:-") + name.toUtf8()
               + QByteArrayLiteral("}\" \"${FAKE_PKG_VERSION:-") + version.toUtf8()
               + QByteArrayLiteral("}\" \"${FAKE_PKG_ARCH:-amd64}\"\n");
    } else {
        body = QByteArrayLiteral("printf '%s\\n%s\\n%s\\n' \"${FAKE_PKG_NAME:-") + name.toUtf8()
               + QByteArrayLiteral("}\" \"${FAKE_PKG_VERSION:-") + version.toUtf8()
               + QByteArrayLiteral("}\" \"${FAKE_PKG_ARCH:-x86_64}\"\n");
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
    fixture.uid = ::getuid();
    const QString root = m_root->filePath(name);
    if (!QDir().mkpath(root)) {
        return {};
    }
    writeFakes(root);

    const QString format = hostFormat();
    const QString artifact = root + QStringLiteral("/podstack-%1.%2.%3")
                                       .arg(version, hostArch(), format);
    QFile artifactFile(artifact);
    if (!artifactFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return {};
    }
    artifactFile.write("pacote de teste");
    artifactFile.close();
    if (!artifactFile.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner)) {
        return {};
    }

    const QString sha = QString::fromLatin1(sha256File(artifact));
    QJsonObject release;
    release.insert(QStringLiteral("version"), version);
    release.insert(QStringLiteral("releaseUrl"), QStringLiteral("https://example.test/tag/v%1")
                                                     .arg(version));
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
    options.packageTool = makeTool(root, format, QStringLiteral("podstack"), version,
                                   format == QStringLiteral("deb") ? QStringLiteral("amd64")
                                                                   : hostArch());
    options.wrapperPath = root + QStringLiteral("/wrapper");
    options.logRoot = root + QStringLiteral("/logs");
    options.maxArtifactBytes = 1024 * 1024;
    fixture.options = options;
    fixture.artifact = artifact;

    // Ajusta o nome do artefato para não coincidir com o glob de poderes.
    if (!valid) {
        fixture.signature = UpdateTestUtils::minisignFile(fixture.manifest + QByteArrayLiteral("x"),
                                                           m_key, false);
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
    QVERIFY((stateFile.permissions() & QFileDevice::ReadGroup) != 0
            || stateFile.permissions() == (QFileDevice::ReadOwner | QFileDevice::WriteOwner));
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
    QCOMPARE(status.value(QStringLiteral("version")).toString(), QStringLiteral("0.1.4"));
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
    Fixture same = makeFixture(QStringLiteral("same-version"), QStringLiteral("0.1.3"));
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
                           fixture.artifact + QStringLiteral(".missing"),
                           QStringLiteral("stable"), &result, &error));
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
    QVERIFY(!smallService.stage(fixture.uid, fixture.manifest, fixture.signature,
                                fixture.artifact, QStringLiteral("stable"), &result, &error));
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
    QVERIFY(install.value(QStringLiteral("unit")).toString().startsWith(
        QStringLiteral("podstack-update-")));

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
    QVERIFY(!service.install(fixture.uid, stage.value(QStringLiteral("jobId")).toString(),
                             &install, &error));
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
    QVERIFY(!service.install(fixture.uid, stage.value(QStringLiteral("jobId")).toString(),
                             &install, &error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_UPDATE_INSTALL_FAILED"));
}

QTEST_GUILESS_MAIN(UpdateServiceTest)
#include "test_update_service.moc"
