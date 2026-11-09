#include "MockPodmanServer.h"

#include "machines/MachineStore.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTest>

namespace {

struct CliResult {
    int code = -1;
    QString out;
    QString err;
};

class CliTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void versionAndHelp();
    void statusAndPs();
    void execAndDoctor();
    void lifecycleAndInspect();
    void catalog();
    void machineDistros();
    void machineCommands();
    void machineSsh();
    void topCommand();
    void systemDf();
    void composeAndBuild();
    void setupCommand();
    void errorPaths();

private:
    QString cliPath() const;
    QString daemonPath() const;
    QProcessEnvironment environment(const QString &controlSocket) const;
    CliResult run(const QStringList &args, const QProcessEnvironment &env) const;

    MockPodmanServer m_mock;
    QProcess m_daemon;
    QString m_controlSocket;
    QString m_testDataDir;
    QString m_fakeProvider;
};

QString CliTest::cliPath() const
{
    return QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("../podstack"));
}

QString CliTest::daemonPath() const
{
    return QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("../podstackd"));
}

QProcessEnvironment CliTest::environment(const QString &controlSocket) const
{
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("PODSTACK_CONTROL_SOCKET"), controlSocket);
    env.insert(QStringLiteral("PODSTACK_PODMAN_SOCKET"), m_mock.socketPath());
    env.insert(QStringLiteral("PODSTACK_COMPOSE_PROVIDER"), m_fakeProvider);
    env.insert(QStringLiteral("PODSTACK_SSH_CONFIG"),
               m_testDataDir + QStringLiteral("/ssh/config"));
    env.insert(QStringLiteral("XDG_DATA_HOME"), m_testDataDir + QStringLiteral("/data"));
    env.insert(QStringLiteral("XDG_STATE_HOME"), m_testDataDir + QStringLiteral("/state"));
    env.insert(QStringLiteral("XDG_CONFIG_HOME"), m_testDataDir + QStringLiteral("/config"));
    env.insert(QStringLiteral("XDG_CACHE_HOME"), m_testDataDir + QStringLiteral("/cache"));
    return env;
}

CliResult CliTest::run(const QStringList &args, const QProcessEnvironment &env) const
{
    QProcess process;
    process.setProcessEnvironment(env);
    process.setProcessChannelMode(QProcess::SeparateChannels);
    process.start(cliPath(), args);
    if (!process.waitForStarted(3000)) {
        return {-1, {}, process.errorString()};
    }
    if (!process.waitForFinished(15000)) {
        process.kill();
        process.waitForFinished(1000);
        return {-2, {}, QStringLiteral("timeout")};
    }
    return {process.exitStatus() == QProcess::NormalExit ? process.exitCode() : -1,
            QString::fromUtf8(process.readAllStandardOutput()),
            QString::fromUtf8(process.readAllStandardError())};
}

void CliTest::initTestCase()
{
    QVERIFY(m_mock.isListening());
    QDir().mkpath(QStringLiteral("/tmp/mock-project"));
    QFile mockCompose(QStringLiteral("/tmp/mock-project/compose.yaml"));
    if (mockCompose.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        mockCompose.write("services: {}\n");
        mockCompose.close();
    }

    m_testDataDir = QDir::tempPath()
                    + QStringLiteral("/podstack-cli-data-%1")
                          .arg(QCoreApplication::applicationPid());
    QDir(m_testDataDir).removeRecursively();
    QDir().mkpath(m_testDataDir);
    m_fakeProvider = m_testDataDir + QStringLiteral("/compose-provider.sh");
    QFile provider(m_fakeProvider);
    QVERIFY(provider.open(QIODevice::WriteOnly | QIODevice::Truncate));
    provider.write("#!/bin/sh\ncase \"$*\" in *fail*) echo \"fake failure\" >&2; exit 1;; esac\necho \"fake compose $@\"\nexit 0\n");
    provider.close();
    QVERIFY(provider.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner
                                    | QFileDevice::ExeOwner));

    m_controlSocket = QDir::tempPath()
                      + QStringLiteral("/podstack-cli-%1.sock")
                            .arg(QCoreApplication::applicationPid());
    QFile::remove(m_controlSocket);

    m_daemon.setProcessEnvironment(environment(m_controlSocket));
    m_daemon.setProcessChannelMode(QProcess::ForwardedChannels);
    m_daemon.start(daemonPath(), {QStringLiteral("--socket"), m_controlSocket});
    QVERIFY2(m_daemon.waitForStarted(5000), qPrintable(m_daemon.errorString()));

    for (int attempt = 0; attempt < 100 && !QFileInfo::exists(m_controlSocket); ++attempt) {
        QTest::qWait(20);
    }
    QVERIFY(QFileInfo::exists(m_controlSocket));

    for (int attempt = 0; attempt < 50; ++attempt) {
        const CliResult result = run({QStringLiteral("status")}, environment(m_controlSocket));
        if (result.code == 0) {
            return;
        }
        QTest::qWait(100);
    }
    QFAIL("podstackd não ficou pronto para os testes de CLI");
}

void CliTest::cleanupTestCase()
{
    m_daemon.terminate();
    if (!m_daemon.waitForFinished(3000)) {
        m_daemon.kill();
        m_daemon.waitForFinished(1000);
    }
    QFile::remove(m_controlSocket);
    QDir(m_testDataDir).removeRecursively();
    QDir(QStringLiteral("/tmp/mock-project")).removeRecursively();
}

void CliTest::versionAndHelp()
{
    const QProcessEnvironment env = environment(m_controlSocket);

    CliResult result = run({QStringLiteral("version")}, env);
    QCOMPARE(result.code, 0);
    QVERIFY(result.out.contains(QStringLiteral("PodStack")));

    result = run({QStringLiteral("help")}, env);
    QCOMPARE(result.code, 0);
    QVERIFY(result.out.contains(QStringLiteral("Uso:")));

    result = run({}, env);
    QCOMPARE(result.code, 2);
    QVERIFY(result.out.contains(QStringLiteral("Uso:")));
}

void CliTest::statusAndPs()
{
    const QProcessEnvironment env = environment(m_controlSocket);

    CliResult result = run({QStringLiteral("status")}, env);
    QCOMPARE(result.code, 0);
    QVERIFY(result.out.contains(QStringLiteral("estado: ok")));

    result = run({QStringLiteral("ps"), QStringLiteral("--all")}, env);
    QCOMPARE(result.code, 0);
    QVERIFY(result.out.contains(QStringLiteral("demo")));

    result = run({QStringLiteral("ps")}, env);
    QCOMPARE(result.code, 0);

    result = run({QStringLiteral("ps"), QStringLiteral("--format"), QStringLiteral("table")}, env);
    QCOMPARE(result.code, 0);
    QVERIFY(result.out.contains(QStringLiteral("NAME")));

    result = run({QStringLiteral("ps"), QStringLiteral("--quiet")}, env);
    QCOMPARE(result.code, 0);
    QCOMPARE(result.out.trimmed(), QStringLiteral("demo"));

    result = run({QStringLiteral("ps"), QStringLiteral("-a"), QStringLiteral("-q")}, env);
    QCOMPARE(result.code, 0);
    QCOMPARE(result.out.trimmed(), QStringLiteral("demo"));

    result = run({QStringLiteral("ps"), QStringLiteral("--format"), QStringLiteral("json")}, env);
    QCOMPARE(result.code, 0);
    const QJsonObject json = QJsonDocument::fromJson(result.out.toUtf8()).object();
    QCOMPARE(json.value(QStringLiteral("api")).toInt(), 1);
    QCOMPARE(json.value(QStringLiteral("items")).toArray().size(), 1);

    result = run({QStringLiteral("ps"), QStringLiteral("--format=json")}, env);
    QCOMPARE(result.code, 0);
    QVERIFY(result.out.contains(QStringLiteral("\"items\"")));

    result = run({QStringLiteral("ps"), QStringLiteral("--running")}, env);
    QCOMPARE(result.code, 0);
    QVERIFY(result.out.contains(QStringLiteral("demo")));

    result = run({QStringLiteral("ps"), QStringLiteral("--stopped")}, env);
    QCOMPARE(result.code, 0);

    result = run({QStringLiteral("ps"), QStringLiteral("--running"), QStringLiteral("--stopped")},
                 env);
    QCOMPARE(result.code, 2);
}

void CliTest::lifecycleAndInspect()
{
    const QProcessEnvironment env = environment(m_controlSocket);

    CliResult result = run({QStringLiteral("pause"), QStringLiteral("abc123")}, env);
    QCOMPARE(result.code, 0);
    QVERIFY(result.out.contains(QStringLiteral("ok")));

    result = run({QStringLiteral("unpause"), QStringLiteral("abc123")}, env);
    QCOMPARE(result.code, 0);

    result = run({QStringLiteral("kill"), QStringLiteral("--signal"), QStringLiteral("SIGTERM"),
                  QStringLiteral("abc123")},
                 env);
    QCOMPARE(result.code, 0);

    result = run({QStringLiteral("inspect"), QStringLiteral("abc123")}, env);
    QCOMPARE(result.code, 0);
    QVERIFY(result.out.contains(QStringLiteral("abc123")));

    result = run({QStringLiteral("pause")}, env);
    QCOMPARE(result.code, 2);

    result = run({QStringLiteral("kill"), QStringLiteral("--signal")}, env);
    QCOMPARE(result.code, 2);

    result = run({QStringLiteral("inspect")}, env);
    QCOMPARE(result.code, 2);
}

void CliTest::execAndDoctor()
{
    const QProcessEnvironment env = environment(m_controlSocket);

    CliResult result = run({QStringLiteral("exec"), QStringLiteral("abc123"), QStringLiteral("--"),
                            QStringLiteral("/bin/echo"), QStringLiteral("hello")},
                           env);
    QCOMPARE(result.code, 0);
    QVERIFY(result.out.contains(QStringLiteral("mock-exec-output")));

    result = run({QStringLiteral("doctor")}, env);
    QCOMPARE(result.code, 0);
    QVERIFY(result.out.contains(QStringLiteral("[ok]")));
}

void CliTest::catalog()
{
    const QProcessEnvironment env = environment(m_controlSocket);

    CliResult result = run({QStringLiteral("images")}, env);
    QCOMPARE(result.code, 0);
    QVERIFY(result.out.contains(QStringLiteral("alpine")));

    result = run({QStringLiteral("images"), QStringLiteral("pull"), QStringLiteral("alpine")},
                 env);
    QCOMPARE(result.code, 0);
    QVERIFY(result.out.contains(QStringLiteral("Downloading"))
            || result.out.contains(QStringLiteral("concluído")));

    result = run({QStringLiteral("images"), QStringLiteral("pull"), QStringLiteral("fail")},
                 env);
    QCOMPARE(result.code, 1);
    QVERIFY(result.err.contains(QStringLiteral("pull failed")));

    result = run({QStringLiteral("images"), QStringLiteral("history"),
                  QStringLiteral("img1234567890")},
                 env);
    QCOMPARE(result.code, 0);
    QVERIFY(result.out.contains(QStringLiteral("COPY /app")));

    result = run({QStringLiteral("images"), QStringLiteral("tag"),
                  QStringLiteral("img1234567890"), QStringLiteral("repo:v1")},
                 env);
    QCOMPARE(result.code, 0);

    result = run({QStringLiteral("images"), QStringLiteral("untag"),
                  QStringLiteral("img1234567890"), QStringLiteral("repo:v1")},
                 env);
    QCOMPARE(result.code, 0);

    const QString imageArchive =
        QDir::tempPath() + QStringLiteral("/podstack-cli-image-%1.tar")
                               .arg(QCoreApplication::applicationPid());
    QFile::remove(imageArchive);
    result = run({QStringLiteral("images"), QStringLiteral("export"),
                  QStringLiteral("img1234567890"), imageArchive},
                 env);
    QCOMPARE(result.code, 0);
    QVERIFY(QFileInfo(imageArchive).size() > 0);

    result = run({QStringLiteral("images"), QStringLiteral("load"), imageArchive}, env);
    QCOMPARE(result.code, 0);
    QFile::remove(imageArchive);

    result = run({QStringLiteral("volumes")}, env);
    QCOMPARE(result.code, 0);
    QVERIFY(result.out.contains(QStringLiteral("vol1")));

    const QString sourceDir = QStringLiteral("/tmp/podstack-mock-volume");
    const QString destinationDir = QStringLiteral("/tmp/podstack-mock-volume2");
    QDir(sourceDir).removeRecursively();
    QDir(destinationDir).removeRecursively();
    QDir().mkpath(sourceDir);
    QFile sourceFile(sourceDir + QStringLiteral("/a.txt"));
    QVERIFY(sourceFile.open(QIODevice::WriteOnly));
    sourceFile.write("hello");
    sourceFile.close();

    result = run({QStringLiteral("volumes"), QStringLiteral("create"), QStringLiteral("vol2")},
                 env);
    QCOMPARE(result.code, 0);

    result = run({QStringLiteral("volumes"), QStringLiteral("clone"), QStringLiteral("vol1"),
                  QStringLiteral("vol2")},
                 env);
    QCOMPARE(result.code, 0);

    const QString volumeArchive =
        QDir::tempPath() + QStringLiteral("/podstack-cli-volume-%1.tar.zst")
                               .arg(QCoreApplication::applicationPid());
    QFile::remove(volumeArchive);
    result = run({QStringLiteral("volumes"), QStringLiteral("export"),
                  QStringLiteral("vol1"), volumeArchive},
                 env);
    QCOMPARE(result.code, 0);
    QVERIFY(QFileInfo(volumeArchive).size() > 0);

    result = run({QStringLiteral("volumes"), QStringLiteral("import"),
                  QStringLiteral("vol2"), volumeArchive, QStringLiteral("--force")},
                 env);
    QCOMPARE(result.code, 0);
    QFile::remove(volumeArchive);

    const QString hostDir = QStringLiteral("/tmp/podstack-host-src");
    QDir(hostDir).removeRecursively();
    QDir().mkpath(hostDir);
    QFile hostFile(hostDir + QStringLiteral("/b.txt"));
    QVERIFY(hostFile.open(QIODevice::WriteOnly));
    hostFile.write("host-content");
    hostFile.close();
    result = run({QStringLiteral("volumes"), QStringLiteral("import-host"),
                  QStringLiteral("vol2"), hostDir},
                 env);
    QCOMPARE(result.code, 0);
    QDir(hostDir).removeRecursively();

    result = run({QStringLiteral("volumes"), QStringLiteral("prune")}, env);
    QCOMPARE(result.code, 0);

    result = run({QStringLiteral("networks")}, env);
    QCOMPARE(result.code, 0);
    QVERIFY(result.out.contains(QStringLiteral("mock-net")));

    result = run({QStringLiteral("networks"), QStringLiteral("create"),
                  QStringLiteral("mock-net"), QStringLiteral("--subnet"),
                  QStringLiteral("10.90.0.0/24")},
                 env);
    QCOMPARE(result.code, 0);

    result = run({QStringLiteral("networks"), QStringLiteral("connect"),
                  QStringLiteral("mock-net"), QStringLiteral("abc123")},
                 env);
    QCOMPARE(result.code, 0);

    result = run({QStringLiteral("networks"), QStringLiteral("disconnect"),
                  QStringLiteral("mock-net"), QStringLiteral("abc123")},
                 env);
    QCOMPARE(result.code, 0);

    result = run({QStringLiteral("pods")}, env);
    QCOMPARE(result.code, 0);
    QVERIFY(result.out.contains(QStringLiteral("demo-pod")));

    result = run({QStringLiteral("pods"), QStringLiteral("create"),
                  QStringLiteral("demo-pod")},
                 env);
    QCOMPARE(result.code, 0);

    result = run({QStringLiteral("pods"), QStringLiteral("start"),
                  QStringLiteral("pod123")},
                 env);
    QCOMPARE(result.code, 0);

    result = run({QStringLiteral("images"), QStringLiteral("prune")}, env);
    QCOMPARE(result.code, 0);

    result = run({QStringLiteral("images"), QStringLiteral("rm"),
                  QStringLiteral("img1234567890")},
                 env);
    QCOMPARE(result.code, 0);

    result = run({QStringLiteral("volumes"), QStringLiteral("rm"),
                  QStringLiteral("vol1")},
                 env);
    QCOMPARE(result.code, 0);

    result = run({QStringLiteral("networks"), QStringLiteral("rm"),
                  QStringLiteral("mock-net")},
                 env);
    QCOMPARE(result.code, 0);

    result = run({QStringLiteral("pods"), QStringLiteral("rm"), QStringLiteral("pod123")},
                 env);
    QCOMPARE(result.code, 0);

    QDir(sourceDir).removeRecursively();
    QDir(destinationDir).removeRecursively();
}

void CliTest::machineDistros()
{
    const QProcessEnvironment env = environment(m_controlSocket);

    CliResult result = run({QStringLiteral("machine"), QStringLiteral("distros")}, env);
    QCOMPARE(result.code, 0);
    QVERIFY(result.out.contains(QStringLiteral("ubuntu")));
    QVERIFY(result.out.contains(QStringLiteral("debian")));
    QVERIFY(result.out.contains(QStringLiteral("fedora")));

    result = run({QStringLiteral("machine"), QStringLiteral("distros"),
                  QStringLiteral("--json")},
                 env);
    QCOMPARE(result.code, 0);

    QJsonParseError parseError{};
    const QJsonDocument document = QJsonDocument::fromJson(result.out.toUtf8(), &parseError);
    QCOMPARE(parseError.error, QJsonParseError::NoError);
    QVERIFY(document.isObject());

    const QJsonArray items =
        document.object().value(QStringLiteral("items")).toArray();
    QVERIFY(items.size() >= 3);

    QStringList distros;
    for (const QJsonValue &value : items) {
        distros.append(value.toObject().value(QStringLiteral("distro")).toString());
    }
    QVERIFY(distros.contains(QStringLiteral("ubuntu")));
    QVERIFY(distros.contains(QStringLiteral("debian")));
    QVERIFY(distros.contains(QStringLiteral("fedora")));
}

void CliTest::machineCommands()
{
    const QProcessEnvironment env = environment(m_controlSocket);

    CliResult result = run({QStringLiteral("machine"), QStringLiteral("list"),
                            QStringLiteral("--json")},
                           env);
    QCOMPARE(result.code, 0);
    QJsonParseError parseError{};
    const QJsonDocument document = QJsonDocument::fromJson(result.out.toUtf8(), &parseError);
    QCOMPARE(parseError.error, QJsonParseError::NoError);
    QVERIFY(document.isObject());
    QCOMPARE(document.object().value(QStringLiteral("items")).toArray().size(), 0);

    result = run({QStringLiteral("machine"), QStringLiteral("list")}, env);
    QCOMPARE(result.code, 0);
    QVERIFY(result.out.contains(QStringLiteral("NOME")));
    QVERIFY(result.out.contains(QStringLiteral("SSH")));

    result = run({QStringLiteral("machine"), QStringLiteral("--help")}, env);
    QCOMPARE(result.code, 0);
    QVERIFY(result.out.contains(QStringLiteral("Uso: podstack machine")));

    result = run({QStringLiteral("machine")}, env);
    QCOMPARE(result.code, 2);
    QVERIFY(result.err.contains(QStringLiteral("Uso: podstack machine")));

    result = run({QStringLiteral("machine"), QStringLiteral("info"), QStringLiteral("missing")},
                 env);
    QCOMPARE(result.code, 1);
    QVERIFY(result.err.contains(QStringLiteral("não encontrada")));

    result = run({QStringLiteral("machine"), QStringLiteral("logs"), QStringLiteral("missing")},
                 env);
    QCOMPARE(result.code, 1);
    QVERIFY(result.err.contains(QStringLiteral("não encontrada")));

    result = run({QStringLiteral("machine"), QStringLiteral("logs")}, env);
    QCOMPARE(result.code, 2);
    QVERIFY(result.err.contains(QStringLiteral("Uso: podstack machine")));

    PodStack::MachineStore store(m_testDataDir + QStringLiteral("/data/podstack/podstack.db"));
    PodStack::Error storeError;
    QVERIFY2(store.open(&storeError), qPrintable(storeError.message));

    PodStack::MachineMeta meta;
    meta.id = QStringLiteral("cli-machine-logs");
    meta.name = QStringLiteral("cli-logs");
    meta.distro = QStringLiteral("fedora");
    meta.distroVersion = QStringLiteral("44");
    meta.arch = QStringLiteral("amd64");
    meta.state = QStringLiteral("stopped");
    meta.createdAt = QStringLiteral("2026-01-01T00:00:00.000Z");
    meta.updatedAt = meta.createdAt;
    QVERIFY2(store.upsert(meta, &storeError), qPrintable(storeError.message));

    const QString machineDir =
        m_testDataDir + QStringLiteral("/data/podstack/machines/") + meta.id;
    QVERIFY(QDir().mkpath(machineDir));
    QFile console(machineDir + QStringLiteral("/console.log"));
    QVERIFY(console.open(QIODevice::WriteOnly | QIODevice::Truncate));
    console.write("cli boot 1\ncli boot 2\ncli boot 3\n");
    console.close();

    result = run({QStringLiteral("machine"), QStringLiteral("logs"),
                  QStringLiteral("cli-logs")},
                 env);
    QCOMPARE(result.code, 0);
    QVERIFY(result.out.contains(QStringLiteral("cli boot 1")));
    QVERIFY(result.out.contains(QStringLiteral("cli boot 3")));

    result = run({QStringLiteral("machine"), QStringLiteral("logs"),
                  QStringLiteral("cli-logs"), QStringLiteral("--tail"),
                  QStringLiteral("1")},
                 env);
    QCOMPARE(result.code, 0);
    QVERIFY(!result.out.contains(QStringLiteral("cli boot 1")));
    QVERIFY(result.out.contains(QStringLiteral("cli boot 3")));

    result = run({QStringLiteral("machine"), QStringLiteral("logs"),
                  QStringLiteral("cli-logs"), QStringLiteral("--json")},
                 env);
    QCOMPARE(result.code, 0);
    QJsonParseError logsParse{};
    const QJsonDocument logsDocument =
        QJsonDocument::fromJson(result.out.toUtf8(), &logsParse);
    QCOMPARE(logsParse.error, QJsonParseError::NoError);
    QCOMPARE(logsDocument.object().value(QStringLiteral("lines")).toArray().size(), 3);

    QVERIFY(store.remove(meta.id));
    QDir(machineDir).removeRecursively();

    result = run({QStringLiteral("machine"), QStringLiteral("create"),
                  QStringLiteral("ubuntu:24.04"), QStringLiteral("Bad_Name")},
                 env);
    QCOMPARE(result.code, 1);
    QVERIFY(result.err.contains(QStringLiteral("Nome de machine inválido")));

    result = run({QStringLiteral("machine"), QStringLiteral("create")}, env);
    QCOMPARE(result.code, 2);
    QVERIFY(result.err.contains(QStringLiteral("Uso: podstack machine")));

    result = run({QStringLiteral("machine"), QStringLiteral("create"),
                  QStringLiteral("ubuntu:24.04"), QStringLiteral("valid-name"),
                  QStringLiteral("--memory"), QStringLiteral("invalid")},
                 env);
    QCOMPARE(result.code, 2);
    QVERIFY(result.err.contains(QStringLiteral("--memory inválido")));

    result = run({QStringLiteral("machine"), QStringLiteral("create"),
                  QStringLiteral("ubuntu:24.04"), QStringLiteral("valid-name"),
                  QStringLiteral("--unknown")},
                 env);
    QCOMPARE(result.code, 2);

    for (const QStringList &args :
         {QStringList{QStringLiteral("machine"), QStringLiteral("start"),
                      QStringLiteral("missing")},
          QStringList{QStringLiteral("machine"), QStringLiteral("stop"),
                      QStringLiteral("missing"), QStringLiteral("--force")},
          QStringList{QStringLiteral("machine"), QStringLiteral("restart"),
                      QStringLiteral("missing")},
          QStringList{QStringLiteral("machine"), QStringLiteral("delete"),
                      QStringLiteral("missing"), QStringLiteral("--force")}}) {
        result = run(args, env);
        QCOMPARE(result.code, 1);
        QVERIFY(result.err.contains(QStringLiteral("não encontrada")));
    }
}

void CliTest::machineSsh()
{
    const QProcessEnvironment env = environment(m_controlSocket);

    CliResult result = run({QStringLiteral("machine"), QStringLiteral("ssh"),
                            QStringLiteral("missing")},
                           env);
    QCOMPARE(result.code, 1);
    QVERIFY(result.err.contains(QStringLiteral("não encontrada")));

    result = run({QStringLiteral("machine"), QStringLiteral("ssh")}, env);
    QCOMPARE(result.code, 2);
    QVERIFY(result.err.contains(QStringLiteral("Uso: podstack machine")));

    result = run({QStringLiteral("ssh")}, env);
    QCOMPARE(result.code, 2);
    QVERIFY(result.err.contains(QStringLiteral("Uso: podstack machine")));

    const QString binDir = m_testDataDir + QStringLiteral("/bin");
    QVERIFY(QDir().mkpath(binDir));
    const QString fakeSsh = binDir + QStringLiteral("/ssh");
    QFile script(fakeSsh);
    QVERIFY(script.open(QIODevice::WriteOnly | QIODevice::Truncate));
    script.write("#!/bin/sh\n"
                 "log=\"${PODSTACK_FAKE_SSH_LOG:-}\"\n"
                 "if [ -n \"$log\" ]; then\n"
                 "  : > \"$log\"\n"
                 "  for arg in \"$@\"; do printf '%s\\n' \"$arg\" >> \"$log\"; done\n"
                 "fi\n"
                 "exit 0\n");
    script.close();
    QVERIFY(script.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner
                                  | QFileDevice::ExeOwner));

    const QString logPath = m_testDataDir + QStringLiteral("/ssh-args.log");
    QProcessEnvironment sshEnv = env;
    sshEnv.insert(QStringLiteral("PATH"),
                  binDir + QLatin1Char(':') + qEnvironmentVariable("PATH"));
    sshEnv.insert(QStringLiteral("PODSTACK_FAKE_SSH_LOG"), logPath);

    PodStack::MachineStore store(m_testDataDir + QStringLiteral("/data/podstack/podstack.db"));
    PodStack::Error storeError;
    QVERIFY2(store.open(&storeError), qPrintable(storeError.message));

    PodStack::MachineMeta meta;
    meta.id = QStringLiteral("cli-machine-ssh");
    meta.name = QStringLiteral("cli-ssh");
    meta.distro = QStringLiteral("fedora");
    meta.distroVersion = QStringLiteral("44");
    meta.arch = QStringLiteral("amd64");
    meta.state = QStringLiteral("running");
    meta.sshUser = QStringLiteral("podstack");
    meta.sshPort = 32222;
    meta.sshKeyPath = m_testDataDir + QStringLiteral("/keys/cli_ed25519");
    meta.createdAt = QStringLiteral("2026-01-01T00:00:00.000Z");
    meta.updatedAt = meta.createdAt;
    QVERIFY2(store.upsert(meta, &storeError), qPrintable(storeError.message));
    QVERIFY(QDir().mkpath(QFileInfo(meta.sshKeyPath).absolutePath()));
    QFile key(meta.sshKeyPath);
    QVERIFY(key.open(QIODevice::WriteOnly | QIODevice::Truncate));
    key.write("FAKE-PRIVATE-KEY");
    key.close();
    QVERIFY(key.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner));

    result = run({QStringLiteral("machine"), QStringLiteral("ssh"),
                  QStringLiteral("cli-ssh"), QStringLiteral("--"),
                  QStringLiteral("echo"), QStringLiteral("hello")},
                 sshEnv);
    QVERIFY2(result.code == 0, qPrintable(result.err));
    QFile log(logPath);
    QVERIFY(log.open(QIODevice::ReadOnly));
    const QStringList args =
        QString::fromUtf8(log.readAll()).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    log.close();
    QCOMPARE(args.size(), 11);
    QCOMPARE(args.value(0), QStringLiteral("-i"));
    QCOMPARE(args.value(1), meta.sshKeyPath);
    QCOMPARE(args.value(2), QStringLiteral("-p"));
    QCOMPARE(args.value(3), QStringLiteral("32222"));
    QCOMPARE(args.value(4), QStringLiteral("-o"));
    QCOMPARE(args.value(5), QStringLiteral("IdentitiesOnly=yes"));
    QCOMPARE(args.value(6), QStringLiteral("-o"));
    QCOMPARE(args.value(7), QStringLiteral("StrictHostKeyChecking=accept-new"));
    QCOMPARE(args.value(8), QStringLiteral("podstack@127.0.0.1"));
    QCOMPARE(args.value(9), QStringLiteral("echo"));
    QCOMPARE(args.value(10), QStringLiteral("hello"));

    result = run({QStringLiteral("ssh"), QStringLiteral("cli-ssh")}, sshEnv);
    QCOMPARE(result.code, 0);
    QVERIFY(log.open(QIODevice::ReadOnly));
    const QStringList directArgs =
        QString::fromUtf8(log.readAll()).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    log.close();
    QCOMPARE(directArgs.size(), 9);
    QCOMPARE(directArgs.value(8), QStringLiteral("podstack@127.0.0.1"));

    QVERIFY(store.remove(meta.id));
    QFile::remove(meta.sshKeyPath);
    QDir().rmdir(QFileInfo(meta.sshKeyPath).absolutePath());
}

void CliTest::topCommand()
{
    const QProcessEnvironment env = environment(m_controlSocket);

    CliResult result = run({QStringLiteral("top"), QStringLiteral("--once"),
                            QStringLiteral("--interval"), QStringLiteral("1"),
                            QStringLiteral("--json")},
                           env);
    QCOMPARE(result.code, 0);

    const QStringList lines = result.out.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    QVERIFY(!lines.isEmpty());
    QJsonParseError parseError{};
    const QJsonDocument document = QJsonDocument::fromJson(lines.last().toUtf8(), &parseError);
    QCOMPARE(parseError.error, QJsonParseError::NoError);
    QVERIFY(document.isObject());
    const QJsonObject sample = document.object();
    QVERIFY(sample.contains(QStringLiteral("host")));
    QVERIFY(sample.contains(QStringLiteral("containers")));
    QVERIFY(sample.contains(QStringLiteral("totals")));

    result = run({QStringLiteral("top"), QStringLiteral("--help")}, env);
    QCOMPARE(result.code, 0);
    QVERIFY(result.out.contains(QStringLiteral("Uso: podstack top")));
}

void CliTest::systemDf()
{
    const QProcessEnvironment env = environment(m_controlSocket);

    CliResult result = run({QStringLiteral("system"), QStringLiteral("df"),
                            QStringLiteral("--json")},
                           env);
    QCOMPARE(result.code, 0);
    QJsonParseError parseError{};
    const QJsonDocument document = QJsonDocument::fromJson(result.out.toUtf8(), &parseError);
    QCOMPARE(parseError.error, QJsonParseError::NoError);
    const QJsonObject sections = document.object().value(QStringLiteral("sections")).toObject();
    QCOMPARE(sections.value(QStringLiteral("images"))
                 .toObject()
                 .value(QStringLiteral("count"))
                 .toInt(),
             1);
    QCOMPARE(sections.value(QStringLiteral("containers"))
                 .toObject()
                 .value(QStringLiteral("count"))
                 .toInt(),
             1);
    QCOMPARE(sections.value(QStringLiteral("volumes"))
                 .toObject()
                 .value(QStringLiteral("count"))
                 .toInt(),
             1);

    result = run({QStringLiteral("system"), QStringLiteral("df")}, env);
    QCOMPARE(result.code, 0);
    QVERIFY(result.out.contains(QStringLiteral("IMAGENS")));
    QVERIFY(result.out.contains(QStringLiteral("CONTAINERS")));
    QVERIFY(result.out.contains(QStringLiteral("VOLUMES")));
    QVERIFY(result.out.contains(QStringLiteral("TOTAL recuperável")));

    result = run({QStringLiteral("system"), QStringLiteral("df"),
                  QStringLiteral("--detailed")},
                 env);
    QCOMPARE(result.code, 0);
    QVERIFY(result.out.contains(QStringLiteral("<none>:<none>")));
    QVERIFY(result.out.contains(QStringLiteral("mock-stopped")));
    QVERIFY(result.out.contains(QStringLiteral("vol1")));
    QVERIFY(result.out.contains(QStringLiteral("em uso · não será removido")));

    result = run({QStringLiteral("system"), QStringLiteral("df"),
                  QStringLiteral("--unknown")},
                 env);
    QCOMPARE(result.code, 2);
}

void CliTest::composeAndBuild()
{
    const QProcessEnvironment env = environment(m_controlSocket);

    CliResult result = run({QStringLiteral("compose"), QStringLiteral("ls")}, env);
    QCOMPARE(result.code, 0);
    QVERIFY(result.out.contains(QStringLiteral("mock-project")));

    const QString projectDir = m_testDataDir + QStringLiteral("/project");
    QDir().mkpath(projectDir);
    QFile composeFile(projectDir + QStringLiteral("/compose.yaml"));
    QVERIFY(composeFile.open(QIODevice::WriteOnly));
    composeFile.write("services:\n  web:\n    image: alpine\n");
    composeFile.close();
    QFile dockerfile(projectDir + QStringLiteral("/Dockerfile"));
    QVERIFY(dockerfile.open(QIODevice::WriteOnly));
    dockerfile.write("FROM alpine:3.20\n");
    dockerfile.close();

    result = run({QStringLiteral("compose"), QStringLiteral("up"), projectDir}, env);
    QCOMPARE(result.code, 0);

    result = run({QStringLiteral("compose"), QStringLiteral("restart"), projectDir}, env);
    QCOMPARE(result.code, 0);

    result = run({QStringLiteral("compose"), QStringLiteral("stop"), projectDir}, env);
    QCOMPARE(result.code, 0);

    result = run({QStringLiteral("compose"), QStringLiteral("start"), projectDir}, env);
    QCOMPARE(result.code, 0);

    result = run({QStringLiteral("compose"), QStringLiteral("down"), projectDir}, env);
    QCOMPARE(result.code, 0);

    result = run({QStringLiteral("build"), projectDir, QStringLiteral("-t"),
                  QStringLiteral("localhost/mock:1")},
                 env);
    QCOMPARE(result.code, 0);
    QVERIFY(result.out.contains(QStringLiteral("STEP 1/1")));
    QVERIFY(result.out.contains(QStringLiteral("concluído")));

    result = run({QStringLiteral("builds")}, env);
    QCOMPARE(result.code, 0);
    QVERIFY(result.out.contains(QStringLiteral("localhost/mock:1")));
    QVERIFY(result.out.contains(QStringLiteral("success")));

    QString buildId;
    const QStringList lines = result.out.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (const QString &line : lines) {
        if (line.startsWith(QStringLiteral("build-"))) {
            buildId = line.section(QLatin1Char(' '), 0, 0);
            break;
        }
    }
    QVERIFY(!buildId.isEmpty());

    result = run({QStringLiteral("builds"), QStringLiteral("rm"), buildId}, env);
    QCOMPARE(result.code, 0);
}

void CliTest::setupCommand()
{
    const QProcessEnvironment env = environment(m_controlSocket);

    const CliResult check = run({QStringLiteral("setup"), QStringLiteral("--check")}, env);
    QCOMPARE(check.code, 0);
    QVERIFY(check.out.contains(QStringLiteral("podman")));
    QVERIFY(check.out.contains(QStringLiteral("sqlite")));

    const CliResult status = run({QStringLiteral("setup")}, env);
    QCOMPARE(status.code, 0);
    QVERIFY(status.out.contains(QStringLiteral("Política do Docker")));
    QVERIFY(status.out.contains(QStringLiteral("Onboarding")));

    const CliResult validate = run({QStringLiteral("setup"), QStringLiteral("--validate")}, env);
    QVERIFY(validate.code == 0 || validate.code == 1);
    QVERIFY(validate.out.contains(QStringLiteral("Validação")));

    const CliResult remove =
        run({QStringLiteral("setup"), QStringLiteral("--remove-docker")}, env);
    QVERIFY(remove.code != 0);
    QVERIFY(remove.err.contains(QStringLiteral("--yes")));
}

void CliTest::errorPaths()
{
    const QProcessEnvironment env = environment(m_controlSocket);

    CliResult result = run({QStringLiteral("ps"), QStringLiteral("--format")}, env);
    QCOMPARE(result.code, 2);

    result = run({QStringLiteral("ps"), QStringLiteral("--bad")}, env);
    QCOMPARE(result.code, 2);

    result = run({QStringLiteral("exec")}, env);
    QCOMPARE(result.code, 2);

    result = run({QStringLiteral("does-not-exist")}, env);
    QCOMPARE(result.code, 2);

    const QString missingSocket = QDir::tempPath()
                                  + QStringLiteral("/podstack-missing-%1.sock")
                                        .arg(QCoreApplication::applicationPid());
    QFile::remove(missingSocket);
    const QProcessEnvironment offline = environment(missingSocket);

    result = run({QStringLiteral("status")}, offline);
    QCOMPARE(result.code, 7);

    result = run({QStringLiteral("ps")}, offline);
    QCOMPARE(result.code, 7);

    result = run({QStringLiteral("exec"), QStringLiteral("abc123"), QStringLiteral("--"),
                  QStringLiteral("/bin/echo")},
                 offline);
    QCOMPARE(result.code, 7);

    result = run({QStringLiteral("doctor")}, offline);
    QCOMPARE(result.code, 7);
    QCOMPARE(run({QStringLiteral("build")}, environment(m_controlSocket)).code, 2);
    QCOMPARE(run({QStringLiteral("builds"), QStringLiteral("rm")},
                 environment(m_controlSocket)).code, 2);
    QCOMPARE(run({QStringLiteral("compose"), QStringLiteral("up")},
                 environment(m_controlSocket)).code, 2);
}

} // namespace

QTEST_GUILESS_MAIN(CliTest)
#include "test_cli.moc"
