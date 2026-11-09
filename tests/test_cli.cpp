#include "MockPodmanServer.h"

#include "machines/MachineStore.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalServer>
#include <QLocalSocket>
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
    void machineCreateForwardAgent();
    void machineSsh();
    void machinePorts();
    void machineSnapshots();
    void machineFiles();
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
    QString m_fakePodman;
    QString m_fakeFilesSsh;
    QString m_fakeFilesScp;
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
    env.insert(QStringLiteral("PODSTACK_PODMAN_CLI"), m_fakePodman);
    env.insert(QStringLiteral("PODSTACK_SSH_BINARY"), m_fakeFilesSsh);
    env.insert(QStringLiteral("PODSTACK_SCP_BINARY"), m_fakeFilesScp);
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

    m_fakePodman = m_testDataDir + QStringLiteral("/podman-fake.sh");
    QFile podman(m_fakePodman);
    QVERIFY(podman.open(QIODevice::WriteOnly | QIODevice::Truncate));
    const QString fakePodman =
        QStringLiteral("#!/bin/sh\n"
                       "echo \"$@\" >> \"%1\"\n"
                       "if [ \"$1\" = \"build\" ]; then\n"
                       "  echo \"line one\"\n"
                       "  echo \"line two\"\n"
                       "fi\n"
                       "exit 0\n")
            .arg(m_testDataDir + QStringLiteral("/podman-args.log"));
    podman.write(fakePodman.toUtf8());
    podman.close();
    QVERIFY(podman.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner
                                  | QFileDevice::ExeOwner));

    m_fakeFilesSsh = m_testDataDir + QStringLiteral("/ssh-files-fake.sh");
    QFile sshFiles(m_fakeFilesSsh);
    QVERIFY(sshFiles.open(QIODevice::WriteOnly | QIODevice::Truncate));
    sshFiles.write("#!/bin/sh\n"
                   "case \"$*\" in\n"
                   "  *\"ls -la --time-style=long-iso\"*)\n"
                   "    echo \"total 4\"\n"
                   "    echo \"drwxr-xr-x 2 podstack podstack 4096 2026-11-09 10:00 docs\"\n"
                   "    echo \"-rw-r--r-- 1 podstack podstack 12 2026-11-09 10:05 cli.txt\"\n"
                   "    exit 0;;\n"
                   "esac\n"
                   "exit 0\n");
    sshFiles.close();
    QVERIFY(sshFiles.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner
                                    | QFileDevice::ExeOwner));

    const QString downloadDir = m_testDataDir + QStringLiteral("/downloads");
    QVERIFY(QDir().mkpath(downloadDir));
    m_fakeFilesScp = m_testDataDir + QStringLiteral("/scp-files-fake.sh");
    QFile scpFiles(m_fakeFilesScp);
    QVERIFY(scpFiles.open(QIODevice::WriteOnly | QIODevice::Truncate));
    scpFiles.write(QStringLiteral("#!/bin/sh\n"
                                  "for last in \"$@\"; do :; done\n"
                                  "case \"$last\" in\n"
                                  "  \"%1\"/*) : > \"$last\";;\n"
                                  "esac\n"
                                  "exit 0\n")
                       .arg(downloadDir)
                       .toUtf8());
    scpFiles.close();
    QVERIFY(scpFiles.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner
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
    QVERIFY(result.out.contains(QStringLiteral("--forward-ssh-agent")));
    QVERIFY(result.out.contains(QStringLiteral("--forward-agent")));
    QVERIFY(result.out.contains(QStringLiteral("--no-forward-agent")));
    QVERIFY(result.out.contains(QStringLiteral("console <id|nome>")));

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

    result = run({QStringLiteral("machine"), QStringLiteral("console")}, env);
    QCOMPARE(result.code, 2);
    QVERIFY(result.err.contains(QStringLiteral("Uso: podstack machine")));

    result = run({QStringLiteral("machine"), QStringLiteral("console"),
                  QStringLiteral("missing"), QStringLiteral("--no-follow")},
                 env);
    QCOMPARE(result.code, 1);
    QVERIFY(result.err.contains(QStringLiteral("não encontrada")));

    result = run({QStringLiteral("machine"), QStringLiteral("console"),
                  QStringLiteral("cli-logs"), QStringLiteral("--no-follow")},
                 env);
    QCOMPARE(result.code, 0);
    QCOMPARE(result.out, QStringLiteral("cli boot 1\ncli boot 2\ncli boot 3\n"));

    result = run({QStringLiteral("machine"), QStringLiteral("console"),
                  QStringLiteral("cli-logs"), QStringLiteral("--tail"),
                  QStringLiteral("11"), QStringLiteral("--no-follow")},
                 env);
    QCOMPARE(result.code, 0);
    QCOMPARE(result.out, QStringLiteral("cli boot 3\n"));

    result = run({QStringLiteral("machine"), QStringLiteral("console"),
                  QStringLiteral("cli-logs"), QStringLiteral("--no-follow"),
                  QStringLiteral("--json")},
                 env);
    QCOMPARE(result.code, 0);
    QJsonParseError consoleParse{};
    const QJsonDocument consoleDocument =
        QJsonDocument::fromJson(result.out.toUtf8(), &consoleParse);
    QCOMPARE(consoleParse.error, QJsonParseError::NoError);
    QCOMPARE(consoleDocument.object().value(QStringLiteral("data")).toString(),
             QStringLiteral("cli boot 1\ncli boot 2\ncli boot 3\n"));

    result = run({QStringLiteral("machine"), QStringLiteral("console"),
                  QStringLiteral("cli-logs"), QStringLiteral("--json")},
                 env);
    QCOMPARE(result.code, 2);
    QVERIFY(result.err.contains(QStringLiteral("--no-follow")));

    QProcess follow;
    follow.setProcessEnvironment(env);
    follow.setProcessChannelMode(QProcess::SeparateChannels);
    follow.start(cliPath(), {QStringLiteral("machine"), QStringLiteral("console"),
                             QStringLiteral("cli-logs")});
    QVERIFY(follow.waitForStarted(3000));

    QByteArray followOutput;
    QElapsedTimer followTimer;
    followTimer.start();
    while (followTimer.elapsed() < 5000
           && !followOutput.contains("cli boot 3")) {
        if (follow.waitForReadyRead(100)) {
            followOutput += follow.readAllStandardOutput();
        }
    }
    QVERIFY2(followOutput.contains("cli boot 3"), followOutput.constData());

    QFile followAppend(machineDir + QStringLiteral("/console.log"));
    QVERIFY(followAppend.open(QIODevice::Append));
    followAppend.write("cli boot 4\n");
    followAppend.close();

    followTimer.restart();
    while (followTimer.elapsed() < 5000
           && !followOutput.contains("cli boot 4")) {
        if (follow.waitForReadyRead(100)) {
            followOutput += follow.readAllStandardOutput();
        }
    }
    QVERIFY2(followOutput.contains("cli boot 4"), followOutput.constData());
    follow.kill();
    QVERIFY(follow.waitForFinished(2000));

    QVERIFY(store.remove(meta.id));
    QDir(machineDir).removeRecursively();

    result = run({QStringLiteral("machine"), QStringLiteral("create"),
                  QStringLiteral("ubuntu:24.04"), QStringLiteral("Bad_Name")},
                 env);
    QCOMPARE(result.code, 1);
    QVERIFY(result.err.contains(QStringLiteral("Nome de machine inválido")));

    // A flag é aceita no parsing e chega à validação do daemon.
    result = run({QStringLiteral("machine"), QStringLiteral("create"),
                  QStringLiteral("ubuntu:24.04"), QStringLiteral("Bad_Name"),
                  QStringLiteral("--forward-ssh-agent")},
                 env);
    QCOMPARE(result.code, 1);
    QVERIFY(result.err.contains(QStringLiteral("Nome de machine inválido")));
    QVERIFY(!result.err.contains(QStringLiteral("opção desconhecida")));

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

void CliTest::machineCreateForwardAgent()
{
    const QProcessEnvironment baseEnv = environment(m_controlSocket);
    const QString socketPath = m_testDataDir + QStringLiteral("/fake-create.sock");
    QLocalServer server;
    QVERIFY2(server.listen(socketPath), qPrintable(server.errorString()));

    QJsonObject captured;
    connect(&server, &QLocalServer::newConnection, &server, [&] {
        while (QLocalSocket *socket = server.nextPendingConnection()) {
            auto handleRequest = [&, socket] {
                QByteArray buffer = socket->property("podstackCliBuffer").toByteArray()
                                    + socket->readAll();
                const int newline = buffer.indexOf('\n');
                if (newline < 0) {
                    socket->setProperty("podstackCliBuffer", buffer);
                    return;
                }
                const QJsonObject request =
                    QJsonDocument::fromJson(buffer.left(newline)).object();
                captured = request.value(QStringLiteral("params")).toObject();

                const QJsonObject subscribed{
                    {QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
                    {QStringLiteral("id"), request.value(QStringLiteral("id"))},
                    {QStringLiteral("result"),
                     QJsonObject{{QStringLiteral("subscriptionId"),
                                  QStringLiteral("fake-subscription")}}}};
                const QJsonObject done{
                    {QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
                    {QStringLiteral("method"), QStringLiteral("machines.done")},
                    {QStringLiteral("params"),
                     QJsonObject{{QStringLiteral("jobId"), QStringLiteral("fake-job")},
                                 {QStringLiteral("error"),
                                  QJsonObject{{QStringLiteral("code"),
                                               QStringLiteral("PODSTACK_INTERNAL")},
                                              {QStringLiteral("message"),
                                               QStringLiteral("fake failure")}}}}}};
                QByteArray response =
                    QJsonDocument(subscribed).toJson(QJsonDocument::Compact) + '\n';
                response += QJsonDocument(done).toJson(QJsonDocument::Compact) + '\n';
                socket->write(response);
                socket->flush();
            };
            connect(socket, &QLocalSocket::readyRead, socket, handleRequest);
            if (socket->bytesAvailable() > 0) {
                handleRequest();
            }
        }
    });

    QProcessEnvironment env = baseEnv;
    env.insert(QStringLiteral("PODSTACK_CONTROL_SOCKET"), socketPath);

    QProcess process;
    process.setProcessEnvironment(env);
    process.setProcessChannelMode(QProcess::SeparateChannels);
    process.start(cliPath(), {QStringLiteral("machine"), QStringLiteral("create"),
                              QStringLiteral("ubuntu:24.04"),
                              QStringLiteral("agent-create"),
                              QStringLiteral("--isolate-network"),
                              QStringLiteral("--forward-ssh-agent"),
                              QStringLiteral("--isolated")});
    QVERIFY(process.waitForStarted(3000));
    QElapsedTimer timer;
    timer.start();
    while (process.state() != QProcess::NotRunning && timer.elapsed() < 15000) {
        QTest::qWait(10);
    }
    if (process.state() != QProcess::NotRunning) {
        process.kill();
        process.waitForFinished(1000);
    }
    QCOMPARE(process.exitStatus(), QProcess::NormalExit);
    QCOMPARE(process.exitCode(), 1);
    QVERIFY(QString::fromUtf8(process.readAllStandardError())
                .contains(QStringLiteral("fake failure")));

    QCOMPARE(captured.value(QStringLiteral("name")).toString(),
             QStringLiteral("agent-create"));
    QCOMPARE(captured.value(QStringLiteral("forwardSshAgent")).toBool(), true);
    QCOMPARE(captured.value(QStringLiteral("isolateNetwork")).toBool(), true);
    QCOMPARE(captured.value(QStringLiteral("isolated")).toBool(), true);
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
    QCOMPARE(result.code, 1);
    QVERIFY(result.err.contains(QStringLiteral("Nenhuma machine default definida")));
    QVERIFY(result.err.contains(QStringLiteral("machine default <nome>")));

    result = run({QStringLiteral("ssh")}, env);
    QCOMPARE(result.code, 1);
    QVERIFY(result.err.contains(QStringLiteral("Nenhuma machine default definida")));
    QVERIFY(result.err.contains(QStringLiteral("machine default <nome>")));

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
    const QString machineDir =
        m_testDataDir + QStringLiteral("/data/podstack/machines/") + meta.id;
    QVERIFY(QDir().mkpath(machineDir));
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

    // O toggle pós-criação sincroniza o metadado e o bloco do ssh config.
    result = run({QStringLiteral("machine"), QStringLiteral("update"),
                  QStringLiteral("cli-ssh"), QStringLiteral("--forward-agent")},
                 env);
    QVERIFY2(result.code == 0, qPrintable(result.err));
    QVERIFY(result.out.contains(QStringLiteral("atualizada")));
    const QString sshConfigPath = m_testDataDir + QStringLiteral("/ssh/config");
    QFile config(sshConfigPath);
    QVERIFY(config.open(QIODevice::ReadOnly));
    QVERIFY(QString::fromUtf8(config.readAll())
                .contains(QStringLiteral("  ForwardAgent yes")));
    config.close();

    result = run({QStringLiteral("machine"), QStringLiteral("ssh"),
                  QStringLiteral("cli-ssh"), QStringLiteral("--"),
                  QStringLiteral("echo"), QStringLiteral("agent")},
                 sshEnv);
    QVERIFY2(result.code == 0, qPrintable(result.err));
    QVERIFY(log.open(QIODevice::ReadOnly));
    const QStringList forwardedArgs =
        QString::fromUtf8(log.readAll()).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    log.close();
    QCOMPARE(forwardedArgs.size(), 12);
    QCOMPARE(forwardedArgs.value(0), QStringLiteral("-A"));
    QCOMPARE(forwardedArgs.value(1), QStringLiteral("-i"));
    QCOMPARE(forwardedArgs.value(2), meta.sshKeyPath);
    QCOMPARE(forwardedArgs.value(9), QStringLiteral("podstack@127.0.0.1"));
    QCOMPARE(forwardedArgs.value(10), QStringLiteral("echo"));
    QCOMPARE(forwardedArgs.value(11), QStringLiteral("agent"));

    result = run({QStringLiteral("machine"), QStringLiteral("update"),
                  QStringLiteral("cli-ssh"), QStringLiteral("--no-forward-agent")},
                 env);
    QVERIFY2(result.code == 0, qPrintable(result.err));
    QVERIFY(config.open(QIODevice::ReadOnly));
    QVERIFY(!QString::fromUtf8(config.readAll())
                 .contains(QStringLiteral("ForwardAgent yes")));
    config.close();

    result = run({QStringLiteral("machine"), QStringLiteral("ssh"),
                  QStringLiteral("cli-ssh")},
                 sshEnv);
    QCOMPARE(result.code, 0);
    QVERIFY(log.open(QIODevice::ReadOnly));
    const QStringList plainArgs =
        QString::fromUtf8(log.readAll()).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    log.close();
    QCOMPARE(plainArgs.size(), 9);
    QVERIFY(!plainArgs.contains(QStringLiteral("-A")));

    result = run({QStringLiteral("machine"), QStringLiteral("update"),
                  QStringLiteral("cli-ssh"), QStringLiteral("--forward-agent"),
                  QStringLiteral("--no-forward-agent")},
                 env);
    QCOMPARE(result.code, 2);
    QVERIFY(result.err.contains(QStringLiteral("--forward-agent")));
    result = run({QStringLiteral("machine"), QStringLiteral("update"),
                  QStringLiteral("cli-ssh")},
                 env);
    QCOMPARE(result.code, 2);

    QVERIFY(store.remove(meta.id));
    QFile::remove(meta.sshKeyPath);
    QDir().rmdir(QFileInfo(meta.sshKeyPath).absolutePath());
    QDir(machineDir).removeRecursively();
    QFile::remove(sshConfigPath);
}

void CliTest::machinePorts()
{
    const QProcessEnvironment env = environment(m_controlSocket);

    CliResult result = run({QStringLiteral("machine"), QStringLiteral("port"),
                            QStringLiteral("ls")},
                           env);
    QCOMPARE(result.code, 2);
    QVERIFY(result.err.contains(QStringLiteral("Uso: podstack machine")));

    result = run({QStringLiteral("machine"), QStringLiteral("port"),
                  QStringLiteral("add")},
                 env);
    QCOMPARE(result.code, 2);

    PodStack::MachineStore store(m_testDataDir + QStringLiteral("/data/podstack/podstack.db"));
    PodStack::Error storeError;
    QVERIFY2(store.open(&storeError), qPrintable(storeError.message));

    PodStack::MachineMeta meta;
    meta.id = QStringLiteral("cli-machine-ports");
    meta.name = QStringLiteral("cli-ports");
    meta.distro = QStringLiteral("fedora");
    meta.distroVersion = QStringLiteral("44");
    meta.arch = QStringLiteral("amd64");
    meta.state = QStringLiteral("stopped");
    meta.sshPort = 32222;
    meta.createdAt = QStringLiteral("2026-01-01T00:00:00.000Z");
    meta.updatedAt = meta.createdAt;
    meta.ports = {PodStack::MachinePort{QStringLiteral("ssh-port-cli"),
                                       32222,
                                       22,
                                       QStringLiteral("tcp"),
                                       false,
                                       QStringLiteral("ssh")}};
    QVERIFY2(store.upsert(meta, &storeError), qPrintable(storeError.message));

    const QString machineDir =
        m_testDataDir + QStringLiteral("/data/podstack/machines/") + meta.id;
    QVERIFY(QDir().mkpath(machineDir));

    result = run({QStringLiteral("machine"), QStringLiteral("port"), QStringLiteral("add"),
                  QStringLiteral("cli-ports"), QStringLiteral("--host"), QStringLiteral("0"),
                  QStringLiteral("--guest"), QStringLiteral("80")},
                 env);
    QCOMPARE(result.code, 2);

    result = run({QStringLiteral("machine"), QStringLiteral("port"), QStringLiteral("add"),
                  QStringLiteral("cli-ports"), QStringLiteral("--host"),
                  QStringLiteral("18080"), QStringLiteral("--guest"), QStringLiteral("80"),
                  QStringLiteral("--label"), QStringLiteral("web")},
                 env);
    QVERIFY2(result.code == 0, qPrintable(result.err));
    QVERIFY(result.out.contains(QStringLiteral("adicionada")));

    result = run({QStringLiteral("machine"), QStringLiteral("port"), QStringLiteral("ls"),
                  QStringLiteral("cli-ports"), QStringLiteral("--json")},
                 env);
    QCOMPARE(result.code, 0);
    QJsonParseError parseError{};
    const QJsonDocument document = QJsonDocument::fromJson(result.out.toUtf8(), &parseError);
    QCOMPARE(parseError.error, QJsonParseError::NoError);
    const QJsonArray items = document.object().value(QStringLiteral("items")).toArray();
    QCOMPARE(items.size(), 2);
    bool sawWeb = false;
    bool sawSsh = false;
    for (const QJsonValue &value : items) {
        const QJsonObject port = value.toObject();
        if (port.value(QStringLiteral("hostPort")).toInt() == 18080) {
            sawWeb = port.value(QStringLiteral("guestPort")).toInt() == 80
                     && port.value(QStringLiteral("label")).toString() == QStringLiteral("web");
        }
        if (port.value(QStringLiteral("guestPort")).toInt() == 22) {
            sawSsh = port.value(QStringLiteral("hostPort")).toInt() == 32222;
        }
    }
    QVERIFY(sawWeb);
    QVERIFY(sawSsh);

    result = run({QStringLiteral("machine"), QStringLiteral("port"), QStringLiteral("ls"),
                  QStringLiteral("cli-ports")},
                 env);
    QCOMPARE(result.code, 0);
    QVERIFY(result.out.contains(QStringLiteral("HOST")));
    QVERIFY(result.out.contains(QStringLiteral("18080")));

    result = run({QStringLiteral("machine"), QStringLiteral("port"), QStringLiteral("rm"),
                  QStringLiteral("cli-ports"), QStringLiteral("18080")},
                 env);
    QVERIFY2(result.code == 0, qPrintable(result.err));

    result = run({QStringLiteral("machine"), QStringLiteral("port"), QStringLiteral("ls"),
                  QStringLiteral("cli-ports"), QStringLiteral("--json")},
                 env);
    QCOMPARE(result.code, 0);
    const QJsonDocument afterDocument =
        QJsonDocument::fromJson(result.out.toUtf8(), &parseError);
    QCOMPARE(parseError.error, QJsonParseError::NoError);
    const QJsonArray after = afterDocument.object().value(QStringLiteral("items")).toArray();
    QCOMPARE(after.size(), 1);
    QCOMPARE(after.at(0).toObject().value(QStringLiteral("guestPort")).toInt(), 22);

    result = run({QStringLiteral("machine"), QStringLiteral("port"), QStringLiteral("rm"),
                  QStringLiteral("cli-ports"), QStringLiteral("32222")},
                 env);
    QCOMPARE(result.code, 1);
    QVERIFY(result.err.contains(QStringLiteral("SSH")));

    result = run({QStringLiteral("machine"), QStringLiteral("port"), QStringLiteral("rm"),
                  QStringLiteral("cli-ports"), QStringLiteral("9999")},
                 env);
    QCOMPARE(result.code, 1);
    QVERIFY(result.err.contains(QStringLiteral("não encontrada")));

    QVERIFY(store.remove(meta.id));
    QDir(machineDir).removeRecursively();
}

void CliTest::machineSnapshots()
{
    const QProcessEnvironment env = environment(m_controlSocket);

    CliResult result = run({QStringLiteral("machine"), QStringLiteral("--help")}, env);
    QCOMPARE(result.code, 0);
    QVERIFY(result.out.contains(QStringLiteral("snapshot ls <id|nome>")));

    result = run({QStringLiteral("machine"), QStringLiteral("snapshot")}, env);
    QCOMPARE(result.code, 2);
    QVERIFY(result.err.contains(QStringLiteral("Uso: podstack machine")));

    result = run({QStringLiteral("machine"), QStringLiteral("snapshot"), QStringLiteral("ls")},
                 env);
    QCOMPARE(result.code, 2);

    result = run({QStringLiteral("machine"), QStringLiteral("snapshot"), QStringLiteral("ls"),
                  QStringLiteral("missing")},
                 env);
    QCOMPARE(result.code, 1);
    QVERIFY(result.err.contains(QStringLiteral("não encontrada")));

    result = run({QStringLiteral("machine"), QStringLiteral("snapshot"),
                  QStringLiteral("create"), QStringLiteral("missing")},
                 env);
    QCOMPARE(result.code, 2);

    result = run({QStringLiteral("machine"), QStringLiteral("snapshot"),
                  QStringLiteral("create"), QStringLiteral("missing"),
                  QStringLiteral("snap")},
                 env);
    QCOMPARE(result.code, 1);
    QVERIFY(result.err.contains(QStringLiteral("não encontrada")));

    result = run({QStringLiteral("machine"), QStringLiteral("snapshot"),
                  QStringLiteral("create"), QStringLiteral("missing"),
                  QStringLiteral("bad name")},
                 env);
    QCOMPARE(result.code, 1);
    QVERIFY(result.err.contains(QStringLiteral("snapshot inválido")));

    result = run({QStringLiteral("machine"), QStringLiteral("snapshot"),
                  QStringLiteral("restore"), QStringLiteral("missing"),
                  QStringLiteral("snap")},
                 env);
    QCOMPARE(result.code, 1);
    QVERIFY(result.err.contains(QStringLiteral("aviso:")));
    QVERIFY(result.err.contains(QStringLiteral("substitui o disco atual")));

    result = run({QStringLiteral("machine"), QStringLiteral("snapshot"),
                  QStringLiteral("rm"), QStringLiteral("missing"), QStringLiteral("snap")},
                 env);
    QCOMPARE(result.code, 1);
    QVERIFY(result.err.contains(QStringLiteral("não encontrada")));
}

void CliTest::machineFiles()
{
    const QProcessEnvironment env = environment(m_controlSocket);

    CliResult result = run({QStringLiteral("machine"), QStringLiteral("files")}, env);
    QCOMPARE(result.code, 2);
    QVERIFY(result.err.contains(QStringLiteral("Uso: podstack machine")));

    result = run({QStringLiteral("machine"), QStringLiteral("files"), QStringLiteral("ls")},
                 env);
    QCOMPARE(result.code, 2);
    QVERIFY(result.err.contains(QStringLiteral("Uso: podstack machine")));

    result = run({QStringLiteral("machine"), QStringLiteral("files"), QStringLiteral("ls"),
                  QStringLiteral("missing")},
                 env);
    QCOMPARE(result.code, 1);
    QVERIFY(result.err.contains(QStringLiteral("não encontrada")));

    result = run({QStringLiteral("machine"), QStringLiteral("pull")}, env);
    QCOMPARE(result.code, 2);

    result = run({QStringLiteral("machine"), QStringLiteral("pull"),
                  QStringLiteral("missing"), QStringLiteral("/remote"),
                  QStringLiteral("/tmp/podstack-cli-missing")},
                 env);
    QCOMPARE(result.code, 1);
    QVERIFY(result.err.contains(QStringLiteral("não encontrada")));

    result = run({QStringLiteral("machine"), QStringLiteral("--help")}, env);
    QCOMPARE(result.code, 0);
    QVERIFY(result.out.contains(QStringLiteral("files ls")));
    QVERIFY(result.out.contains(QStringLiteral("pull <id|nome>")));
    QVERIFY(result.out.contains(QStringLiteral("push <id|nome>")));

    PodStack::MachineStore store(m_testDataDir + QStringLiteral("/data/podstack/podstack.db"));
    PodStack::Error storeError;
    QVERIFY2(store.open(&storeError), qPrintable(storeError.message));

    PodStack::MachineMeta meta;
    meta.id = QStringLiteral("cli-machine-files");
    meta.name = QStringLiteral("cli-files");
    meta.distro = QStringLiteral("fedora");
    meta.distroVersion = QStringLiteral("44");
    meta.arch = QStringLiteral("amd64");
    meta.state = QStringLiteral("running");
    meta.sshUser = QStringLiteral("podstack");
    meta.sshPort = 32222;
    meta.sshKeyPath = m_testDataDir + QStringLiteral("/keys/cli_files_ed25519");
    meta.createdAt = QStringLiteral("2026-01-01T00:00:00.000Z");
    meta.updatedAt = meta.createdAt;
    QVERIFY2(store.upsert(meta, &storeError), qPrintable(storeError.message));
    QVERIFY(QDir().mkpath(QFileInfo(meta.sshKeyPath).absolutePath()));
    QFile key(meta.sshKeyPath);
    QVERIFY(key.open(QIODevice::WriteOnly | QIODevice::Truncate));
    key.write("FAKE-PRIVATE-KEY");
    key.close();
    QVERIFY(key.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner));

    result = run({QStringLiteral("machine"), QStringLiteral("files"), QStringLiteral("ls"),
                  QStringLiteral("cli-files"), QStringLiteral("--json")},
                 env);
    QVERIFY2(result.code == 0, qPrintable(result.err));
    QJsonParseError parseError{};
    const QJsonDocument document = QJsonDocument::fromJson(result.out.toUtf8(), &parseError);
    QCOMPARE(parseError.error, QJsonParseError::NoError);
    QCOMPARE(document.object().value(QStringLiteral("path")).toString(),
             QStringLiteral("/"));
    QCOMPARE(document.object().value(QStringLiteral("items")).toArray().size(), 2);

    result = run({QStringLiteral("machine"), QStringLiteral("files"), QStringLiteral("ls"),
                  QStringLiteral("cli-machine-files"), QStringLiteral("--path"),
                  QStringLiteral("/tmp")},
                 env);
    QVERIFY2(result.code == 0, qPrintable(result.err));
    QVERIFY(result.out.contains(QStringLiteral("caminho: /tmp")));
    QVERIFY(result.out.contains(QStringLiteral("docs")));
    QVERIFY(result.out.contains(QStringLiteral("cli.txt")));

    const QString downloaded = m_testDataDir + QStringLiteral("/downloads/cli.txt");
    result = run({QStringLiteral("machine"), QStringLiteral("pull"),
                  QStringLiteral("cli-files"), QStringLiteral("/home/podstack/cli.txt"),
                  downloaded},
                 env);
    QVERIFY2(result.code == 0, qPrintable(result.err));
    QVERIFY(result.out.contains(QStringLiteral("baixado")));
    QVERIFY(QFileInfo::exists(downloaded));

    const QString uploadSource = m_testDataDir + QStringLiteral("/cli-upload.txt");
    QFile upload(uploadSource);
    QVERIFY(upload.open(QIODevice::WriteOnly | QIODevice::Truncate));
    upload.write("cli upload");
    upload.close();

    result = run({QStringLiteral("machine"), QStringLiteral("push"),
                  QStringLiteral("cli-files"), uploadSource,
                  QStringLiteral("/tmp/cli-upload.txt")},
                 env);
    QVERIFY2(result.code == 0, qPrintable(result.err));
    QVERIFY(result.out.contains(QStringLiteral("enviado")));

    result = run({QStringLiteral("machine"), QStringLiteral("push"),
                  QStringLiteral("cli-files"), uploadSource},
                 env);
    QCOMPARE(result.code, 2);

    QVERIFY(store.remove(meta.id));
    QFile::remove(meta.sshKeyPath);
    QFile::remove(downloaded);
    QFile::remove(uploadSource);
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

    const QString argsLog = m_testDataDir + QStringLiteral("/podman-args.log");
    QFile::remove(argsLog);
    result = run({QStringLiteral("build"), projectDir, QStringLiteral("-t"),
                  QStringLiteral("localhost/mock:multi"), QStringLiteral("--platform"),
                  QStringLiteral("linux/amd64,linux/arm64"),
                  QStringLiteral("--build-arg"), QStringLiteral("A=B")},
                 env);
    QCOMPARE(result.code, 0);
    QVERIFY(result.out.contains(QStringLiteral("line one")));
    QVERIFY(result.out.contains(QStringLiteral("concluído")));

    QFile argsFile(argsLog);
    QVERIFY(argsFile.open(QIODevice::ReadOnly));
    const QString args = QString::fromUtf8(argsFile.readAll());
    argsFile.close();
    QVERIFY2(args.contains(QStringLiteral("manifest rm localhost/mock:multi")),
             qPrintable(args));
    QVERIFY2(args.contains(QStringLiteral("build --platform linux/amd64,linux/arm64 "
                                          "--manifest localhost/mock:multi -f Dockerfile "
                                          "--build-arg A=B ")),
             qPrintable(args));

    result = run({QStringLiteral("builds")}, env);
    QCOMPARE(result.code, 0);
    QVERIFY(result.out.contains(QStringLiteral("PLATFORMS")));
    QVERIFY(result.out.contains(QStringLiteral("localhost/mock:multi")));
    QVERIFY(result.out.contains(QStringLiteral("linux/amd64,linux/arm64")));
    QString multiBuildId;
    const QStringList multiLines = result.out.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (const QString &line : multiLines) {
        if (line.contains(QStringLiteral("localhost/mock:multi"))) {
            multiBuildId = line.section(QLatin1Char(' '), 0, 0);
            break;
        }
    }
    QVERIFY(!multiBuildId.isEmpty());
    result = run({QStringLiteral("builds"), QStringLiteral("rm"), multiBuildId}, env);
    QCOMPARE(result.code, 0);

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
