// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/UpdateRunner.h"
#include "core/UpdateManifest.h"

#include "MockHttpServer.h"
#include "UpdateTestUtils.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QScopeGuard>
#include <QSysInfo>
#include <QTemporaryDir>
#include <QTest>

#include <memory>

namespace {

class FakeUpdateHelper : public PodStack::UpdateHelper {
public:
    bool call(const QString &method, const QVariantList &arguments, QVariantMap *reply,
              PodStack::Error *error) override
    {
        Q_UNUSED(arguments);
        calls.append(method);
        if (method == QStringLiteral("StageUpdate")) {
            if (failStage) {
                if (error) {
                    error->code = QStringLiteral("PODSTACK_UPDATE_SIGNATURE_INVALID");
                    error->message = QStringLiteral("assinatura simulada");
                }
                return false;
            }
            if (reply) {
                *reply = QVariantMap{{QStringLiteral("ok"), true},
                                     {QStringLiteral("jobId"), QStringLiteral("update-fake")},
                                     {QStringLiteral("stateDir"), stateDir}};
            }
            return true;
        }
        if (method == QStringLiteral("InstallUpdate")) {
            if (reply) {
                *reply = QVariantMap{{QStringLiteral("ok"), true},
                                     {QStringLiteral("unit"),
                                      QStringLiteral("podstack-update-fake.service")}};
            }
            return true;
        }
        if (method == QStringLiteral("GetUpdateStatus")) {
            if (reply) {
                *reply = QVariantMap{{QStringLiteral("ok"), true},
                                     {QStringLiteral("phase"), failed ? QStringLiteral("failed")
                                                                      : QStringLiteral("done")},
                                     {QStringLiteral("percent"), 100},
                                     {QStringLiteral("errorCode"),
                                      failed ? QStringLiteral("PODSTACK_UPDATE_INSTALL_FAILED")
                                             : QString()},
                                     {QStringLiteral("message"), failed ? QStringLiteral("falhou")
                                                                        : QStringLiteral("ok")}};
            }
            return true;
        }
        return false;
    }

    QStringList calls;
    QString stateDir;
    bool failStage = false;
    bool failed = false;
};

QString hostFormat()
{
    return QFileInfo::exists(QStringLiteral("/etc/debian_version")) ? QStringLiteral("deb")
                                                                    : QStringLiteral("rpm");
}

QString hostArch()
{
    const QString arch = QSysInfo::buildCpuArchitecture();
    return arch == QStringLiteral("arm64") ? QStringLiteral("aarch64") : arch;
}

QByteArray manifestFor(const QByteArray &artifactSha, const QString &version,
                       const QString &artifactUrl, const QString &format)
{
    QJsonObject artifact;
    artifact.insert(QStringLiteral("arch"), hostArch());
    artifact.insert(QStringLiteral("format"), format);
    artifact.insert(QStringLiteral("url"), artifactUrl);
    artifact.insert(QStringLiteral("sha256"), QString::fromLatin1(artifactSha));
    QJsonObject release;
    release.insert(QStringLiteral("version"), version);
    release.insert(QStringLiteral("releaseUrl"), QStringLiteral("https://example.test/release"));
    release.insert(QStringLiteral("notes"), QStringLiteral("notas"));
    release.insert(QStringLiteral("artifacts"), QJsonArray{artifact});
    QJsonObject channels;
    channels.insert(QStringLiteral("stable"), release);
    QJsonObject root;
    root.insert(QStringLiteral("schemaVersion"), 1);
    root.insert(QStringLiteral("channels"), channels);
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

} // namespace

class UpdateRunnerTest : public QObject {
    Q_OBJECT

private slots:
    void prepareDownloadsAndVerifies();
    void prepareRejectsSignature();
    void prepareRejectsEqualVersion();
    void prepareRejectsDowngrade();
    void prepareRejectsMissingFeed();
    void prepareUsesCache();
    void prepareRejectsMissingKey();
    void installStageFails();
    void installSystemctlFails();
    void prepareFromManifestMissing();
    void installWithoutPrepare();
    void exitCodeMapping();
    void installOrchestratesHelper();
    void installRestartsServices();
    void installRelaunchesGui();
    void resumeInstallingJob();
    void installRefusesExistingState();
    void resumeFailedAndDoneStates();
    void resumeMissingState();
    void notifyOnInstall();
    void installReportsFailure();
    void prepareFromManifestCache();

private:
    struct Fixture {
        std::unique_ptr<QTemporaryDir> dir;
        MockHttpServer server;
        UpdateTestUtils::KeyPair key;
        QByteArray artifact;
        QByteArray manifest;
        QByteArray signature;
        QString artifactUrl;
        PodStack::UpdateRunnerOptions options;
    };

    std::unique_ptr<Fixture> makeFixture(const QString &version = QStringLiteral("0.1.4"));
};

std::unique_ptr<UpdateRunnerTest::Fixture> UpdateRunnerTest::makeFixture(const QString &version)
{
    auto fixture = std::make_unique<Fixture>();
    fixture->dir = std::make_unique<QTemporaryDir>();
    if (!fixture->dir->isValid()) {
        return nullptr;
    }
    fixture->key = UpdateTestUtils::generateKeyPair();
    const QString format = hostFormat();
    const QString artifactPath = fixture->dir->filePath(
        QStringLiteral("podstack-%1.%2.%3").arg(version, hostArch(), format));
    QFile artifactFile(artifactPath);
    if (!artifactFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return nullptr;
    }
    fixture->artifact = "pacote de teste";
    artifactFile.write(fixture->artifact);
    artifactFile.close();
    const QByteArray sha =
        QCryptographicHash::hash(fixture->artifact, QCryptographicHash::Sha256).toHex();
    fixture->artifactUrl = fixture->server.url(QStringLiteral("/podstack-") + version
                                               + QStringLiteral(".") + hostArch()
                                               + QStringLiteral(".") + format);
    fixture->manifest =
        manifestFor(sha, version, fixture->artifactUrl, format);
    fixture->signature = UpdateTestUtils::minisignFile(fixture->manifest, fixture->key, false);
    fixture->server.setResponse(QStringLiteral("/podstack-update.json"), 200, fixture->manifest);
    fixture->server.setResponse(QStringLiteral("/podstack-update.json.minisig"), 200,
                                fixture->signature);
    fixture->server.setResponse(QStringLiteral("/podstack-") + version + QStringLiteral(".")
                                    + hostArch() + QStringLiteral(".") + format,
                                200, fixture->artifact);

    fixture->options.cacheDir = fixture->dir->filePath(QStringLiteral("cache"));
    fixture->options.stateDir = fixture->dir->filePath(QStringLiteral("state"));
    fixture->options.feedUrl = fixture->server.url(QStringLiteral("/podstack-update.json"));
    fixture->options.publicKey = UpdateTestUtils::publicKeyBase64(fixture->key);
    fixture->options.channel = QStringLiteral("stable");
    fixture->options.restartServices = false;
    fixture->options.pollIntervalMs = 1;
    fixture->options.installTimeoutMs = 5000;
    fixture->options.maxArtifactBytes = 1024 * 1024;
    fixture->options.notifyBinary = QStringLiteral("false");
    return fixture;
}

void UpdateRunnerTest::prepareDownloadsAndVerifies()
{
    auto fixture = makeFixture();
    QVERIFY(fixture != nullptr);
    FakeUpdateHelper helper;
    PodStack::UpdateRunner runner(&helper, fixture->options);
    PodStack::Error error;
    QVERIFY2(runner.prepare(&error),
             qPrintable(error.message + QStringLiteral(" ") + error.detail));
    QCOMPARE(runner.version(), QStringLiteral("0.1.4"));
    QCOMPARE(runner.channel(), QStringLiteral("stable"));
    QVERIFY(QFileInfo::exists(runner.artifactPath()));
    QCOMPARE(fixture->server.requestCount(QStringLiteral("/podstack-update.json")), 1);
    QVERIFY(QFileInfo::exists(fixture->options.cacheDir + QStringLiteral("/manifest.json")));
}

void UpdateRunnerTest::prepareRejectsSignature()
{
    auto fixture = makeFixture();
    QVERIFY(fixture != nullptr);
    fixture->server.setResponse(QStringLiteral("/podstack-update.json.minisig"), 200,
                                QByteArrayLiteral("assinatura inválida"));
    FakeUpdateHelper helper;
    PodStack::UpdateRunner runner(&helper, fixture->options);
    PodStack::Error error;
    QVERIFY(!runner.prepare(&error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_UPDATE_SIGNATURE_INVALID"));
}

void UpdateRunnerTest::prepareRejectsEqualVersion()
{
    auto fixture = makeFixture(QStringLiteral("0.1.3"));
    QVERIFY(fixture != nullptr);
    FakeUpdateHelper helper;
    PodStack::UpdateRunner runner(&helper, fixture->options);
    PodStack::Error error;
    QVERIFY(!runner.prepare(&error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_UPDATE_VERSION_EQUAL"));
}

void UpdateRunnerTest::prepareRejectsDowngrade()
{
    auto fixture = makeFixture(QStringLiteral("0.1.2"));
    QVERIFY(fixture != nullptr);
    FakeUpdateHelper helper;
    PodStack::UpdateRunner runner(&helper, fixture->options);
    PodStack::Error error;
    QVERIFY(!runner.prepare(&error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_UPDATE_MANIFEST_INVALID"));
    QCOMPARE(error.detail, QStringLiteral("version_regression"));
}

void UpdateRunnerTest::prepareRejectsMissingFeed()
{
    auto fixture = makeFixture();
    QVERIFY(fixture != nullptr);
    fixture->options.feedUrl = fixture->server.url(QStringLiteral("/missing-feed.json"));
    fixture->server.setResponse(QStringLiteral("/missing-feed.json"), 404, "{}");
    FakeUpdateHelper helper;
    PodStack::UpdateRunner runner(&helper, fixture->options);
    PodStack::Error error;
    QVERIFY(!runner.prepare(&error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_UPDATE_DOWNLOAD_FAILED"));
}

void UpdateRunnerTest::prepareUsesCache()
{
    auto fixture = makeFixture();
    QVERIFY(fixture != nullptr);
    FakeUpdateHelper helper;
    PodStack::UpdateRunner runner(&helper, fixture->options);
    PodStack::Error error;
    QVERIFY2(runner.prepare(&error), qPrintable(error.message));
    const int manifestRequests = fixture->server.requestCount(QStringLiteral("/podstack-update.json"));
    QVERIFY2(runner.prepare(&error), qPrintable(error.message));
    QCOMPARE(fixture->server.requestCount(QStringLiteral("/podstack-update.json")),
             manifestRequests);
}

void UpdateRunnerTest::prepareRejectsMissingKey()
{
    auto fixture = makeFixture();
    QVERIFY(fixture != nullptr);
    fixture->options.publicKey.clear();
    const QByteArray previousConfig = qgetenv("XDG_CONFIG_HOME");
    qputenv("XDG_CONFIG_HOME", (fixture->dir->filePath(QStringLiteral("config"))).toUtf8());
    const auto restore = qScopeGuard([previousConfig] {
        if (previousConfig.isEmpty()) {
            qunsetenv("XDG_CONFIG_HOME");
        } else {
            qputenv("XDG_CONFIG_HOME", previousConfig);
        }
    });
    FakeUpdateHelper helper;
    PodStack::UpdateRunner runner(&helper, fixture->options);
    PodStack::Error error;
    QVERIFY(!runner.prepare(&error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_UPDATE_UNSUPPORTED"));
}

void UpdateRunnerTest::installStageFails()
{
    auto fixture = makeFixture();
    QVERIFY(fixture != nullptr);
    FakeUpdateHelper helper;
    helper.stateDir = fixture->dir->filePath(QStringLiteral("helper-stage-fail"));
    PodStack::UpdateRunner runner(&helper, fixture->options);
    PodStack::Error error;
    QVERIFY2(runner.prepare(&error), qPrintable(error.message));
    helper.failStage = true;
    QVERIFY(!runner.install(&error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_UPDATE_SIGNATURE_INVALID"));
    QCOMPARE(runner.phase(), QStringLiteral("failed"));
}

void UpdateRunnerTest::installSystemctlFails()
{
    auto fixture = makeFixture();
    QVERIFY(fixture != nullptr);
    fixture->options.restartServices = true;
    fixture->options.systemctlBinary = QStringLiteral("/bin/false");
    FakeUpdateHelper helper;
    PodStack::UpdateRunner runner(&helper, fixture->options);
    PodStack::Error error;
    QVERIFY2(runner.prepare(&error), qPrintable(error.message));
    QVERIFY(!runner.install(&error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_UPDATE_INSTALL_FAILED"));
}

void UpdateRunnerTest::prepareFromManifestMissing()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    FakeUpdateHelper helper;
    PodStack::UpdateRunnerOptions options;
    options.cacheDir = dir.path();
    options.stateDir = dir.path();
    options.publicKey = QStringLiteral("invalid");
    options.restartServices = false;
    PodStack::UpdateRunner runner(&helper, options);
    PodStack::Error error;
    QVERIFY(!runner.prepareFromManifest(dir.filePath(QStringLiteral("missing.json")), &error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_UPDATE_MANIFEST_INVALID"));
}

void UpdateRunnerTest::installWithoutPrepare()
{
    auto fixture = makeFixture();
    QVERIFY(fixture != nullptr);
    FakeUpdateHelper helper;
    PodStack::UpdateRunner runner(&helper, fixture->options);
    PodStack::Error error;
    QVERIFY(!runner.install(&error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_UPDATE_INSTALL_FAILED"));
}

void UpdateRunnerTest::exitCodeMapping()
{
    PodStack::Error error;
    error.code = QStringLiteral("PODSTACK_HELPER_REQUIRED");
    QCOMPARE(PodStack::UpdateRunner::exitCodeFor(error), 6);
    error.code = QStringLiteral("PODSTACK_TIMEOUT");
    QCOMPARE(PodStack::UpdateRunner::exitCodeFor(error), 8);
    error.code = QStringLiteral("PODSTACK_CANCELLED");
    QCOMPARE(PodStack::UpdateRunner::exitCodeFor(error), 9);
    error.code = QStringLiteral("PODSTACK_UPDATE_INSTALL_FAILED");
    QCOMPARE(PodStack::UpdateRunner::exitCodeFor(error), 1);
}

void UpdateRunnerTest::installOrchestratesHelper()
{
    auto fixture = makeFixture();
    QVERIFY(fixture != nullptr);
    FakeUpdateHelper helper;
    helper.stateDir = fixture->dir->filePath(QStringLiteral("helper-state"));
    PodStack::UpdateRunner runner(&helper, fixture->options);
    PodStack::Error error;
    QVERIFY2(runner.prepare(&error), qPrintable(error.message));
    QVERIFY2(runner.install(&error),
             qPrintable(error.message + QStringLiteral(" ") + error.detail));
    QCOMPARE(helper.calls,
             QStringList({QStringLiteral("StageUpdate"), QStringLiteral("InstallUpdate"),
                          QStringLiteral("GetUpdateStatus")}));
    QCOMPARE(runner.phase(), QStringLiteral("done"));
    QFile state(fixture->options.stateDir + QStringLiteral("/state.json"));
    QVERIFY(state.open(QIODevice::ReadOnly));
    const QJsonObject object = QJsonDocument::fromJson(state.readAll()).object();
    QCOMPARE(object.value(QStringLiteral("phase")).toString(), QStringLiteral("done"));
    QVERIFY(state.permissions().testFlag(QFileDevice::ReadOwner));
    QVERIFY(!state.permissions().testFlag(QFileDevice::WriteOther));
}

void UpdateRunnerTest::installRestartsServices()
{
    auto fixture = makeFixture();
    QVERIFY(fixture != nullptr);
    const QString logPath = fixture->dir->filePath(QStringLiteral("systemctl.log"));
    const QString systemctl = fixture->dir->filePath(QStringLiteral("systemctl"));
    QFile script(systemctl);
    QVERIFY(script.open(QIODevice::WriteOnly | QIODevice::Truncate));
    script.write("#!/bin/sh\nprintf '%s\\n' \"$@\" >> \"");
    script.write(logPath.toUtf8());
    script.write("\"\nexit 0\n");
    script.close();
    QVERIFY(script.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner
                                  | QFileDevice::ExeOwner));

    fixture->options.restartServices = true;
    fixture->options.systemctlBinary = systemctl;
    FakeUpdateHelper helper;
    helper.stateDir = fixture->dir->filePath(QStringLiteral("helper-state-services"));
    PodStack::UpdateRunner runner(&helper, fixture->options);
    PodStack::Error error;
    QVERIFY2(runner.prepare(&error), qPrintable(error.message));
    QVERIFY2(runner.install(&error), qPrintable(error.message));

    QFile log(logPath);
    QVERIFY(log.open(QIODevice::ReadOnly));
    const QStringList calls =
        QString::fromUtf8(log.readAll()).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    QVERIFY(calls.contains(QStringLiteral("stop")));
    QVERIFY(calls.contains(QStringLiteral("start")));
    QVERIFY(calls.contains(QStringLiteral("podstack.socket")));
}

void UpdateRunnerTest::installRelaunchesGui()
{
    auto fixture = makeFixture();
    QVERIFY(fixture != nullptr);
    const QString guiLog = fixture->dir->filePath(QStringLiteral("gui.log"));
    const QString gui = fixture->dir->filePath(QStringLiteral("podstack-gui"));
    QFile script(gui);
    QVERIFY(script.open(QIODevice::WriteOnly | QIODevice::Truncate));
    script.write("#!/bin/sh\nprintf '%s\\n' \"$@\" > \"");
    script.write(guiLog.toUtf8());
    script.write("\"\nexit 0\n");
    script.close();
    QVERIFY(script.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner
                                  | QFileDevice::ExeOwner));

    const QString marker = fixture->dir->filePath(QStringLiteral("gui-was-open"));
    QFile markerFile(marker);
    QVERIFY(markerFile.open(QIODevice::WriteOnly | QIODevice::Truncate));
    markerFile.close();
    fixture->options.guiMarkerPath = marker;
    fixture->options.guiBinary = gui;

    FakeUpdateHelper helper;
    helper.stateDir = fixture->dir->filePath(QStringLiteral("helper-state-gui"));
    PodStack::UpdateRunner runner(&helper, fixture->options);
    PodStack::Error error;
    QVERIFY2(runner.prepare(&error), qPrintable(error.message));
    QVERIFY2(runner.install(&error), qPrintable(error.message));
    QVERIFY(!QFileInfo::exists(marker));
    QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(guiLog), 3000);
    QFile log(guiLog);
    QVERIFY(log.open(QIODevice::ReadOnly));
    QVERIFY(QString::fromUtf8(log.readAll()).contains(QStringLiteral("--relaunch")));
}

void UpdateRunnerTest::resumeInstallingJob()
{
    auto fixture = makeFixture();
    QVERIFY(fixture != nullptr);
    QDir().mkpath(fixture->options.stateDir);
    QFile state(fixture->options.stateDir + QStringLiteral("/state.json"));
    QVERIFY(state.open(QIODevice::WriteOnly | QIODevice::Truncate));
    state.write(QByteArrayLiteral(
        R"({"phase":"installing","jobId":"update-fake","stateDir":"/tmp/fake","unit":"podstack-update-fake.service","version":"0.1.4","channel":"stable"})"));
    state.close();

    FakeUpdateHelper helper;
    helper.stateDir = fixture->dir->filePath(QStringLiteral("helper-resume"));
    PodStack::UpdateRunner runner(&helper, fixture->options);
    PodStack::Error error;
    QVERIFY2(runner.resume(&error), qPrintable(error.message));
    QCOMPARE(runner.phase(), QStringLiteral("done"));
    QVERIFY(helper.calls.contains(QStringLiteral("GetUpdateStatus")));
}

void UpdateRunnerTest::installRefusesExistingState()
{
    auto fixture = makeFixture();
    QVERIFY(fixture != nullptr);
    QDir().mkpath(fixture->options.stateDir);
    QFile state(fixture->options.stateDir + QStringLiteral("/state.json"));
    QVERIFY(state.open(QIODevice::WriteOnly | QIODevice::Truncate));
    state.write(QByteArrayLiteral(R"({"phase":"installing","jobId":"update-x"})"));
    state.close();
    FakeUpdateHelper helper;
    PodStack::UpdateRunner runner(&helper, fixture->options);
    PodStack::Error error;
    QVERIFY2(runner.prepare(&error), qPrintable(error.message));
    QVERIFY(!runner.install(&error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_UPDATE_LOCKED"));
}

void UpdateRunnerTest::resumeFailedAndDoneStates()
{
    auto fixture = makeFixture();
    QVERIFY(fixture != nullptr);
    QDir().mkpath(fixture->options.stateDir);
    const QString path = fixture->options.stateDir + QStringLiteral("/state.json");
    QFile state(path);
    QVERIFY(state.open(QIODevice::WriteOnly | QIODevice::Truncate));
    state.write(QByteArrayLiteral(R"({"phase":"failed","jobId":"update-x","message":"falhou"})"));
    state.close();
    FakeUpdateHelper helper;
    PodStack::UpdateRunner runner(&helper, fixture->options);
    PodStack::Error error;
    QVERIFY(!runner.resume(&error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_UPDATE_INSTALL_FAILED"));

    QVERIFY(state.open(QIODevice::WriteOnly | QIODevice::Truncate));
    state.write(QByteArrayLiteral(R"({"phase":"done","jobId":"update-x"})"));
    state.close();
    error = {};
    QVERIFY(runner.resume(&error));
    QCOMPARE(runner.phase(), QStringLiteral("done"));
}

void UpdateRunnerTest::resumeMissingState()
{
    auto fixture = makeFixture();
    QVERIFY(fixture != nullptr);
    FakeUpdateHelper helper;
    PodStack::UpdateRunner runner(&helper, fixture->options);
    PodStack::Error error;
    QVERIFY(!runner.resume(&error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_NOT_FOUND"));
}

void UpdateRunnerTest::notifyOnInstall()
{
    auto fixture = makeFixture();
    QVERIFY(fixture != nullptr);
    const QString log = fixture->dir->filePath(QStringLiteral("notify.log"));
    const QString notify = fixture->dir->filePath(QStringLiteral("notify-send"));
    QFile script(notify);
    QVERIFY(script.open(QIODevice::WriteOnly | QIODevice::Truncate));
    script.write("#!/bin/sh\nprintf '%s\\n' \"$@\" > \"");
    script.write(log.toUtf8());
    script.write("\"\nexit 0\n");
    script.close();
    QVERIFY(script.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner
                                  | QFileDevice::ExeOwner));
    fixture->options.notifyBinary = notify;
    fixture->options.restartServices = false;
    FakeUpdateHelper helper;
    helper.stateDir = fixture->dir->filePath(QStringLiteral("helper-notify"));
    PodStack::UpdateRunner runner(&helper, fixture->options);
    PodStack::Error error;
    QVERIFY2(runner.prepare(&error), qPrintable(error.message));
    QVERIFY2(runner.install(&error), qPrintable(error.message));
    QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(log), 3000);
}

void UpdateRunnerTest::installReportsFailure()
{
    auto fixture = makeFixture();
    QVERIFY(fixture != nullptr);
    FakeUpdateHelper helper;
    helper.stateDir = fixture->dir->filePath(QStringLiteral("helper-state"));
    helper.failed = true;
    PodStack::UpdateRunner runner(&helper, fixture->options);
    PodStack::Error error;
    QVERIFY2(runner.prepare(&error), qPrintable(error.message));
    QVERIFY(!runner.install(&error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_UPDATE_INSTALL_FAILED"));
    QCOMPARE(runner.phase(), QStringLiteral("failed"));
}

void UpdateRunnerTest::prepareFromManifestCache()
{
    auto fixture = makeFixture();
    QVERIFY(fixture != nullptr);
    FakeUpdateHelper helper;
    PodStack::UpdateRunner runner(&helper, fixture->options);
    PodStack::Error error;
    QVERIFY2(runner.prepare(&error), qPrintable(error.message));

    const QString manifestPath = fixture->options.cacheDir + QStringLiteral("/manifest.json");
    const QString signaturePath =
        fixture->options.cacheDir + QStringLiteral("/manifest.json.minisig");
    QVERIFY(QFile::exists(manifestPath));
    QVERIFY(QFile::exists(signaturePath));

    PodStack::UpdateRunner cached(&helper, fixture->options);
    QVERIFY2(cached.prepareFromManifest(manifestPath, &error), qPrintable(error.message));
    QCOMPARE(cached.version(), QStringLiteral("0.1.4"));
}

QTEST_GUILESS_MAIN(UpdateRunnerTest)
#include "test_update_runner.moc"
