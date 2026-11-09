#include "MockPodmanServer.h"

#include "core/RpcClient.h"
#include "machines/MachineStore.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalSocket>
#include <QProcess>
#include <QProcessEnvironment>
#include <QScopeGuard>
#include <QTest>

namespace {

class RpcTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void lifecycle();
    void proxyEnvironment();
    void exposeLan();
    void subscriptions();
    void catalog();
    void machinesDistros();
    void machinesFlow();
    void machineLogs();
    void machineSsh();
    void machinePorts();
    void machineStats();
    void machineFiles();
    void composeAndBuilds();
    void multiPlatformBuild();
    void setupFlow();
    void invalidRequests();

private:
    QJsonObject readMessage(QLocalSocket &socket, QByteArray &buffer, int timeoutMs = 3000);
    void sendMessage(QLocalSocket &socket, const QJsonObject &message);

    MockPodmanServer m_mock;
    QProcess m_daemon;
    QString m_controlSocket;
    QString m_testDataDir;
    QString m_fakeProvider;
    QString m_fakePodman;
    QString m_fakeSsh;
    QString m_fakeScp;
    QString m_downloadDir;
};

namespace {
void writeFakeProvider(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return;
    }
    file.write("#!/bin/sh\ncase \"$*\" in *fail*) echo \"fake failure\" >&2; exit 1;; esac\necho \"fake compose $@\"\nexit 0\n");
    file.close();
    file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner
                        | QFileDevice::ExeOwner);
}

void writeFakePodman(const QString &path, const QString &logPath)
{    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return;
    }
    const QString script = QStringLiteral(
        "#!/bin/sh\n"
        "echo \"$@\" >> \"%1\"\n"
        "if [ \"$1\" = \"build\" ]; then\n"
        "  case \"$*\" in\n"
        "    *slow*) sleep 10; echo \"slow line\"; exit 0;;\n"
        "    *fail-multi*) echo \"fake multi failure\" >&2; exit 1;;\n"
        "  esac\n"
        "  echo \"line one\"\n"
        "  echo \"line two\"\n"
        "fi\n"
        "exit 0\n")
                               .arg(logPath);
    file.write(script.toUtf8());
    file.close();
    file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner
                        | QFileDevice::ExeOwner);
}

void writeFakeSsh(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return;
    }
    const QString script = QStringLiteral(
        "#!/bin/sh\n"
        "echo \"$@\" >> \"%1\"\n"
        "case \"$*\" in\n"
        "  *\"ls -la --time-style=long-iso\"*)\n"
        "    echo \"total 8\"\n"
        "    echo \"drwxr-xr-x 2 podstack podstack 4096 2026-11-09 10:00 docs\"\n"
        "    echo \"-rw-r--r-- 1 podstack podstack 42 2026-11-09 10:05 rpc.txt\"\n"
        "    exit 0;;\n"
        "esac\n"
        "echo \"cpu  100 0 100 800 0 0 0 0\"\n"
        "echo \"__PODSTACK_MEM__\"\n"
        "echo \"MemTotal:        2097152 kB\"\n"
        "echo \"MemAvailable:    1048576 kB\"\n"
        "echo \"__PODSTACK_NET__\"\n"
        "echo \"  eth0: 1000 10 0 0 0 0 0 0 500 5 0 0 0 0 0 0\"\n"
        "echo \"__PODSTACK_DISK__\"\n"
        "echo \"   8 0 vda 100 0 200 0 50 0 400 0 0 0 0 0 0 0 0 0 0\"\n"
        "echo \"__PODSTACK_DF__\"\n"
        "echo \"Filesystem     1B-blocks        Used   Available Capacity Mounted on\"\n"
        "echo \"/dev/vda1    10000000000  2500000000  7500000000      25% /\"\n")
                               .arg(path + QStringLiteral(".args.log"));
    file.write(script.toUtf8());
    file.close();
    file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner
                        | QFileDevice::ExeOwner);
}

void writeFakeScp(const QString &path, const QString &downloadDir)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return;
    }
    const QString script = QStringLiteral(
        "#!/bin/sh\n"
        "echo \"$@\" >> \"%1\"\n"
        "for last in \"$@\"; do :; done\n"
        "case \"$last\" in\n"
        "  \"%2\"/*) : > \"$last\";;\n"
        "esac\n"
        "exit 0\n")
                               .arg(path + QStringLiteral(".args.log"), downloadDir);
    file.write(script.toUtf8());
    file.close();
    file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner
                        | QFileDevice::ExeOwner);
}
} // namespace

void RpcTest::initTestCase()
{
    QVERIFY(m_mock.isListening());
    QDir().mkpath(QStringLiteral("/tmp/mock-project"));
    QFile mockCompose(QStringLiteral("/tmp/mock-project/compose.yaml"));
    if (mockCompose.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        mockCompose.write("services: {}\n");
        mockCompose.close();
    }

    m_testDataDir = QDir::tempPath()
                    + QStringLiteral("/podstack-rpc-data-%1")
                          .arg(QCoreApplication::applicationPid());
    QDir(m_testDataDir).removeRecursively();
    QDir().mkpath(m_testDataDir);
    m_fakeProvider = m_testDataDir + QStringLiteral("/compose-provider.sh");
    writeFakeProvider(m_fakeProvider);
    m_fakePodman = m_testDataDir + QStringLiteral("/podman-fake.sh");
    writeFakePodman(m_fakePodman, m_testDataDir + QStringLiteral("/podman-args.log"));
    m_fakeSsh = m_testDataDir + QStringLiteral("/ssh-fake.sh");
    writeFakeSsh(m_fakeSsh);
    m_downloadDir = m_testDataDir + QStringLiteral("/downloads");
    QVERIFY(QDir().mkpath(m_downloadDir));
    m_fakeScp = m_testDataDir + QStringLiteral("/scp-fake.sh");
    writeFakeScp(m_fakeScp, m_downloadDir);

    m_controlSocket = QDir::tempPath()
                      + QStringLiteral("/podstack-rpc-%1.sock")
                            .arg(QCoreApplication::applicationPid());
    QFile::remove(m_controlSocket);

    const QString daemonPath =
        QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("../podstackd"));

    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("PODSTACK_PODMAN_SOCKET"), m_mock.socketPath());
    environment.insert(QStringLiteral("PODSTACK_CONTROL_SOCKET"), m_controlSocket);
    environment.insert(QStringLiteral("PODSTACK_COMPOSE_PROVIDER"), m_fakeProvider);
    environment.insert(QStringLiteral("PODSTACK_PODMAN_CLI"), m_fakePodman);
    environment.insert(QStringLiteral("PODSTACK_SSH_BINARY"), m_fakeSsh);
    environment.insert(QStringLiteral("PODSTACK_SCP_BINARY"), m_fakeScp);
    // Garante que o helper esteja indisponível mesmo em hosts com o serviço instalado.
    environment.insert(QStringLiteral("DBUS_SYSTEM_BUS_ADDRESS"),
                       QStringLiteral("unix:path=/tmp/podstack-rpc-no-system-bus.sock"));
    environment.insert(QStringLiteral("PODSTACK_SSH_CONFIG"),
                       m_testDataDir + QStringLiteral("/ssh/config"));
    environment.insert(QStringLiteral("XDG_DATA_HOME"),
                       m_testDataDir + QStringLiteral("/data"));
    environment.insert(QStringLiteral("XDG_STATE_HOME"),
                       m_testDataDir + QStringLiteral("/state"));
    environment.insert(QStringLiteral("XDG_CONFIG_HOME"),
                       m_testDataDir + QStringLiteral("/config"));
    environment.insert(QStringLiteral("XDG_CACHE_HOME"),
                       m_testDataDir + QStringLiteral("/cache"));
    m_daemon.setProcessEnvironment(environment);
    m_daemon.setProcessChannelMode(QProcess::ForwardedChannels);
    m_daemon.start(daemonPath, {QStringLiteral("--socket"), m_controlSocket});
    QVERIFY2(m_daemon.waitForStarted(5000), qPrintable(m_daemon.errorString()));

    for (int attempt = 0; attempt < 100 && !QFileInfo::exists(m_controlSocket); ++attempt) {
        QTest::qWait(20);
    }
    QVERIFY(QFileInfo::exists(m_controlSocket));

    PodStack::RpcClient client(m_controlSocket);
    PodStack::Error error;
    for (int attempt = 0; attempt < 50; ++attempt) {
        client.call(QStringLiteral("system.hello"), {}, &error, 1000);
        if (error.isEmpty()) {
            return;
        }
        QTest::qWait(100);
    }
    QFAIL(qPrintable(error.message));
}

void RpcTest::cleanupTestCase()
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

void RpcTest::lifecycle()
{
    PodStack::RpcClient client(m_controlSocket);
    PodStack::Error error;

    const QJsonObject hello = client.call(QStringLiteral("system.hello"), {}, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QCOMPARE(hello.value(QStringLiteral("apiVersion")).toInt(), 1);

    const QJsonObject status = client.call(QStringLiteral("system.status"), {}, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QCOMPARE(status.value(QStringLiteral("state")).toString(), QStringLiteral("ok"));

    const QJsonObject doctor = client.call(QStringLiteral("system.doctor"), {}, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QCOMPARE(doctor.value(QStringLiteral("checks")).toArray().size(), 2);

    QJsonObject listParams;
    listParams.insert(QStringLiteral("all"), false);
    const QJsonObject list = client.call(QStringLiteral("containers.list"), listParams, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QCOMPARE(list.value(QStringLiteral("items")).toArray().size(), 1);

    QJsonObject idParams;
    idParams.insert(QStringLiteral("id"), QStringLiteral("abc123"));

    QVERIFY2(client.call(QStringLiteral("containers.start"), idParams, &error)
                 .value(QStringLiteral("ok"))
                 .toBool(),
             qPrintable(error.message));
    QVERIFY2(client.call(QStringLiteral("containers.stop"), idParams, &error)
                 .value(QStringLiteral("ok"))
                 .toBool(),
             qPrintable(error.message));
    QVERIFY2(client.call(QStringLiteral("containers.restart"), idParams, &error)
                 .value(QStringLiteral("ok"))
                 .toBool(),
             qPrintable(error.message));
    QVERIFY2(client.call(QStringLiteral("containers.remove"), idParams, &error)
                 .value(QStringLiteral("ok"))
                 .toBool(),
             qPrintable(error.message));
    QVERIFY2(client.call(QStringLiteral("containers.pause"), idParams, &error)
                 .value(QStringLiteral("ok"))
                 .toBool(),
             qPrintable(error.message));
    QVERIFY2(client.call(QStringLiteral("containers.unpause"), idParams, &error)
                 .value(QStringLiteral("ok"))
                 .toBool(),
             qPrintable(error.message));
    QVERIFY2(client.call(QStringLiteral("containers.kill"), idParams, &error)
                 .value(QStringLiteral("ok"))
                 .toBool(),
             qPrintable(error.message));

    QJsonObject renameParams = idParams;
    renameParams.insert(QStringLiteral("name"), QStringLiteral("renamed"));
    QVERIFY2(client.call(QStringLiteral("containers.rename"), renameParams, &error)
                 .value(QStringLiteral("ok"))
                 .toBool(),
             qPrintable(error.message));

    const QJsonObject inspect = client.call(QStringLiteral("containers.inspect"), idParams, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QCOMPARE(inspect.value(QStringLiteral("json"))
                 .toObject()
                 .value(QStringLiteral("Id"))
                 .toString(),
             QStringLiteral("abc123"));

    QJsonObject createParams;
    createParams.insert(QStringLiteral("image"), QStringLiteral("alpine:3.20"));
    createParams.insert(QStringLiteral("name"), QStringLiteral("created"));
    createParams.insert(QStringLiteral("command"), QStringLiteral("sleep 10"));
    createParams.insert(QStringLiteral("env"),
                        QJsonObject{{QStringLiteral("A"), QStringLiteral("B")}});
    createParams.insert(QStringLiteral("ports"),
                        QJsonArray{QJsonObject{{QStringLiteral("hostPort"), 18080},
                                               {QStringLiteral("containerPort"), 80},
                                               {QStringLiteral("protocol"), QStringLiteral("tcp")}}});
    createParams.insert(QStringLiteral("mounts"),
                        QJsonArray{QJsonObject{{QStringLiteral("source"), QStringLiteral("/tmp")},
                                               {QStringLiteral("destination"),
                                                QStringLiteral("/mnt")},
                                               {QStringLiteral("readOnly"), false}}});
    createParams.insert(QStringLiteral("workDir"), QStringLiteral("/tmp"));
    createParams.insert(QStringLiteral("user"), QStringLiteral("0:0"));
    createParams.insert(QStringLiteral("remove"), true);
    createParams.insert(QStringLiteral("start"), true);

    const QJsonObject created =
        client.call(QStringLiteral("containers.create"), createParams, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QCOMPARE(created.value(QStringLiteral("id")).toString(), QStringLiteral("created123"));
    QVERIFY(created.value(QStringLiteral("started")).toBool());

    const QJsonObject duplicated =
        client.call(QStringLiteral("containers.duplicate"), idParams, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QCOMPARE(duplicated.value(QStringLiteral("id")).toString(), QStringLiteral("created123"));
    QCOMPARE(duplicated.value(QStringLiteral("source")).toString(), QStringLiteral("abc123"));

    PodStack::Error createError;
    client.call(QStringLiteral("containers.create"), {}, &createError);
    QCOMPARE(createError.code, QStringLiteral("PODSTACK_INVALID_PARAMS"));

    QJsonObject filesParams;
    filesParams.insert(QStringLiteral("id"), QStringLiteral("abc123"));
    filesParams.insert(QStringLiteral("path"), QStringLiteral("/"));
    const QJsonObject files =
        client.call(QStringLiteral("containers.files.list"), filesParams, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QCOMPARE(files.value(QStringLiteral("entries")).toArray().size(), 2);

    const QString destination = QDir::tempPath()
                                + QStringLiteral("/podstack-rpc-file-%1.txt")
                                      .arg(QCoreApplication::applicationPid());
    QFile::remove(destination);
    QJsonObject downloadParams = filesParams;
    downloadParams.insert(QStringLiteral("path"), QStringLiteral("/file.txt"));
    downloadParams.insert(QStringLiteral("destination"), destination);
    const QJsonObject download =
        client.call(QStringLiteral("containers.files.download"), downloadParams, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QCOMPARE(download.value(QStringLiteral("destination")).toString(), destination);
    QFile::remove(destination);

    const QString uploadSource = QDir::tempPath()
                                 + QStringLiteral("/podstack-rpc-upload-%1.txt")
                                       .arg(QCoreApplication::applicationPid());
    QFile uploadFile(uploadSource);
    QVERIFY(uploadFile.open(QIODevice::WriteOnly));
    uploadFile.write("rpc-upload");
    uploadFile.close();
    QJsonObject uploadParams = filesParams;
    uploadParams.insert(QStringLiteral("path"), QStringLiteral("/tmp"));
    uploadParams.insert(QStringLiteral("sourcePath"), uploadSource);
    const QJsonObject upload =
        client.call(QStringLiteral("containers.files.upload"), uploadParams, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QVERIFY(upload.value(QStringLiteral("ok")).toBool());
    QFile::remove(uploadSource);

    QJsonObject runningParams;
    runningParams.insert(QStringLiteral("all"), true);
    runningParams.insert(QStringLiteral("state"), QStringLiteral("running"));
    QCOMPARE(client.call(QStringLiteral("containers.list"), runningParams, &error)
                 .value(QStringLiteral("items"))
                 .toArray()
                 .size(),
             1);

    runningParams.insert(QStringLiteral("state"), QStringLiteral("stopped"));
    QCOMPARE(client.call(QStringLiteral("containers.list"), runningParams, &error)
                 .value(QStringLiteral("items"))
                 .toArray()
                 .size(),
             0);

    QJsonObject execParams;
    execParams.insert(QStringLiteral("id"), QStringLiteral("abc123"));
    execParams.insert(QStringLiteral("cmd"),
                      QJsonArray{QStringLiteral("/bin/echo"), QStringLiteral("hello")});
    const QJsonObject exec = client.call(QStringLiteral("containers.exec"), execParams, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QVERIFY(exec.value(QStringLiteral("stdout"))
                .toString()
                .contains(QStringLiteral("mock-exec-output")));
    QCOMPARE(exec.value(QStringLiteral("exitCode")).toInt(), 0);

    PodStack::Error missingCmdError;
    QJsonObject missingCmd;
    missingCmd.insert(QStringLiteral("id"), QStringLiteral("abc123"));
    client.call(QStringLiteral("containers.exec"), missingCmd, &missingCmdError);
    QCOMPARE(missingCmdError.code, QStringLiteral("PODSTACK_INVALID_PARAMS"));

    PodStack::Error missingIdError;
    client.call(QStringLiteral("containers.start"), {}, &missingIdError);
    QCOMPARE(missingIdError.code, QStringLiteral("PODSTACK_INVALID_PARAMS"));

    PodStack::Error unknownError;
    client.call(QStringLiteral("no.such.method"), {}, &unknownError);
    QCOMPARE(unknownError.code, QStringLiteral("PODSTACK_METHOD_NOT_FOUND"));
}

void RpcTest::proxyEnvironment()
{
    const QString configDir =
        m_testDataDir + QStringLiteral("/config/podstack");
    QVERIFY(QDir().mkpath(configDir));
    const QString configPath = configDir + QStringLiteral("/config.json");
    const auto writeConfig = [&configPath](const QByteArray &data) {
        QFile file(configPath);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            return false;
        }
        const bool ok = file.write(data) == data.size();
        file.close();
        return ok;
    };
    const QByteArray proxyConfig =
        QByteArrayLiteral("{\"network\":{\"proxy\":\"http://127.0.0.1:3128\","
                          "\"proxyExclude\":[\".internal\",\"10.0.0.0/8\"]}}");
    QVERIFY(writeConfig(proxyConfig));

    PodStack::RpcClient client(m_controlSocket);
    PodStack::Error error;
    QJsonObject createParams;
    createParams.insert(QStringLiteral("image"), QStringLiteral("alpine:3.20"));
    createParams.insert(QStringLiteral("start"), false);
    const QJsonObject created =
        client.call(QStringLiteral("containers.create"), createParams, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QCOMPARE(created.value(QStringLiteral("id")).toString(), QStringLiteral("created123"));

    const QJsonObject body =
        QJsonDocument::fromJson(m_mock.lastCreateBody()).object();
    const QJsonObject env = body.value(QStringLiteral("env")).toObject();
    const QString proxy = QStringLiteral("http://127.0.0.1:3128");
    QCOMPARE(env.value(QStringLiteral("HTTP_PROXY")).toString(), proxy);
    QCOMPARE(env.value(QStringLiteral("HTTPS_PROXY")).toString(), proxy);
    QCOMPARE(env.value(QStringLiteral("ALL_PROXY")).toString(), proxy);
    QCOMPARE(env.value(QStringLiteral("http_proxy")).toString(), proxy);
    QCOMPARE(env.value(QStringLiteral("https_proxy")).toString(), proxy);
    QCOMPARE(env.value(QStringLiteral("all_proxy")).toString(), proxy);
    const QString noProxy = env.value(QStringLiteral("NO_PROXY")).toString();
    QCOMPARE(env.value(QStringLiteral("no_proxy")).toString(), noProxy);
    const QStringList entries = noProxy.split(QLatin1Char(','));
    QCOMPARE(entries.count(QStringLiteral("localhost")), 1);
    QCOMPARE(entries.count(QStringLiteral("127.0.0.1")), 1);
    QCOMPARE(entries.count(QStringLiteral("::1")), 1);
    QCOMPARE(entries.count(QStringLiteral("*.localhost")), 1);
    QCOMPARE(entries.count(QStringLiteral(".internal")), 1);
    QCOMPARE(entries.count(QStringLiteral("10.0.0.0/8")), 1);

    QJsonObject duplicateParams;
    duplicateParams.insert(QStringLiteral("id"), QStringLiteral("abc123"));
    const QJsonObject duplicated =
        client.call(QStringLiteral("containers.duplicate"), duplicateParams, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QCOMPARE(duplicated.value(QStringLiteral("id")).toString(),
             QStringLiteral("created123"));
    QCOMPARE(duplicated.value(QStringLiteral("source")).toString(),
             QStringLiteral("abc123"));
    const QJsonObject duplicatedEnv = QJsonDocument::fromJson(m_mock.lastCreateBody())
                                          .object()
                                          .value(QStringLiteral("env"))
                                          .toObject();
    QCOMPARE(duplicatedEnv.value(QStringLiteral("A")).toString(), QStringLiteral("B"));
    QCOMPARE(duplicatedEnv.value(QStringLiteral("HTTP_PROXY")).toString(), proxy);

    QJsonObject explicitEnv;
    explicitEnv.insert(QStringLiteral("HTTP_PROXY"),
                       QStringLiteral("http://explicit:9999"));
    explicitEnv.insert(QStringLiteral("NO_PROXY"), QStringLiteral("custom.local"));
    createParams.insert(QStringLiteral("env"), explicitEnv);
    const QJsonObject explicitCreated =
        client.call(QStringLiteral("containers.create"), createParams, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QCOMPARE(explicitCreated.value(QStringLiteral("id")).toString(),
             QStringLiteral("created123"));
    const QJsonObject explicitBody =
        QJsonDocument::fromJson(m_mock.lastCreateBody()).object();
    const QJsonObject explicitValues =
        explicitBody.value(QStringLiteral("env")).toObject();
    QCOMPARE(explicitValues.value(QStringLiteral("HTTP_PROXY")).toString(),
             QStringLiteral("http://explicit:9999"));
    QCOMPARE(explicitValues.value(QStringLiteral("NO_PROXY")).toString(),
             QStringLiteral("custom.local"));
    QCOMPARE(explicitValues.value(QStringLiteral("HTTPS_PROXY")).toString(), proxy);
    QCOMPARE(explicitValues.value(QStringLiteral("ALL_PROXY")).toString(), proxy);

    QVERIFY(writeConfig(
        QByteArrayLiteral("{\"network\":{\"proxy\":\"auto\",\"proxyExclude\":[]}}")));
}

void RpcTest::exposeLan()
{
    PodStack::RpcClient client(m_controlSocket);
    PodStack::Error error;

    QJsonObject port;
    port.insert(QStringLiteral("hostPort"), 18080);
    port.insert(QStringLiteral("containerPort"), 80);
    port.insert(QStringLiteral("protocol"), QStringLiteral("tcp"));
    QJsonArray ports;
    ports.append(port);

    QJsonObject createParams;
    createParams.insert(QStringLiteral("image"), QStringLiteral("alpine:3.20"));
    createParams.insert(QStringLiteral("start"), false);
    createParams.insert(QStringLiteral("ports"), ports);

    const QJsonObject created =
        client.call(QStringLiteral("containers.create"), createParams, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QCOMPARE(created.value(QStringLiteral("id")).toString(), QStringLiteral("created123"));
    const QJsonObject body = QJsonDocument::fromJson(m_mock.lastCreateBody()).object();
    const QJsonArray mappings =
        body.value(QStringLiteral("portmappings")).toArray();
    QCOMPARE(mappings.size(), 1);
    QCOMPARE(mappings.first().toObject().value(QStringLiteral("host_ip")).toString(),
             QStringLiteral("127.0.0.1"));

    const int createsBefore = m_mock.createCount();
    createParams.insert(QStringLiteral("exposeLan"), true);
    error = {};
    const QJsonObject exposed =
        client.call(QStringLiteral("containers.create"), createParams, &error);
    QVERIFY(exposed.isEmpty());
    QCOMPARE(error.code, QStringLiteral("PODSTACK_HELPER_REQUIRED"));
    QCOMPARE(m_mock.createCount(), createsBefore);

    QJsonObject exposeParams;
    exposeParams.insert(QStringLiteral("id"), QStringLiteral("abc123"));
    exposeParams.insert(QStringLiteral("expose"), true);
    error = {};
    const QJsonObject exposeResult =
        client.call(QStringLiteral("containers.exposeLan"), exposeParams, &error);
    QVERIFY(exposeResult.isEmpty());
    QCOMPARE(error.code, QStringLiteral("PODSTACK_HELPER_REQUIRED"));

    exposeParams.insert(QStringLiteral("id"), QStringLiteral("loopback123"));
    error = {};
    (void)client.call(QStringLiteral("containers.exposeLan"), exposeParams, &error);
    QCOMPARE(error.code, QStringLiteral("PODSTACK_UNSUPPORTED"));
    QVERIFY(error.message.contains(QStringLiteral("Recrie o container")));
    QVERIFY(!error.remediation.isEmpty());

    exposeParams.insert(QStringLiteral("id"), QStringLiteral("missing"));
    error = {};
    (void)client.call(QStringLiteral("containers.exposeLan"), exposeParams, &error);
    QCOMPARE(error.code, QStringLiteral("PODSTACK_NOT_FOUND"));
}

void RpcTest::subscriptions()
{
    QLocalSocket socket;
    socket.connectToServer(m_controlSocket, QIODevice::ReadWrite);
    QVERIFY(socket.waitForConnected(3000));

    QByteArray buffer;
    sendMessage(socket,
                {{QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
                 {QStringLiteral("id"), 1},
                 {QStringLiteral("method"), QStringLiteral("containers.logs.subscribe")},
                 {QStringLiteral("params"),
                  QJsonObject{{QStringLiteral("id"), QStringLiteral("abc123")},
                              {QStringLiteral("tail"), 10},
                              {QStringLiteral("follow"), false}}}});

    QString subscriptionId;
    int deltas = 0;
    for (int i = 0; i < 20 && (subscriptionId.isEmpty() || deltas < 2); ++i) {
        const QJsonObject message = readMessage(socket, buffer);
        if (message.isEmpty()) {
            break;
        }
        if (message.value(QStringLiteral("id")).toInt() == 1) {
            subscriptionId = message.value(QStringLiteral("result"))
                                 .toObject()
                                 .value(QStringLiteral("subscriptionId"))
                                 .toString();
        }
        if (message.value(QStringLiteral("method")).toString()
            == QStringLiteral("containers.logs.delta")) {
            ++deltas;
        }
    }
    QVERIFY(!subscriptionId.isEmpty());
    QCOMPARE(deltas, 2);

    sendMessage(socket,
                {{QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
                 {QStringLiteral("id"), 2},
                 {QStringLiteral("method"), QStringLiteral("containers.logs.unsubscribe")},
                 {QStringLiteral("params"),
                  QJsonObject{{QStringLiteral("subscriptionId"), subscriptionId}}}});
    QVERIFY(!readMessage(socket, buffer).isEmpty());

    sendMessage(socket,
                {{QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
                 {QStringLiteral("id"), 3},
                 {QStringLiteral("method"), QStringLiteral("events.subscribe")},
                 {QStringLiteral("params"), QJsonObject{}}});

    int events = 0;
    for (int i = 0; i < 20 && events < 1; ++i) {
        const QJsonObject message = readMessage(socket, buffer);
        if (message.isEmpty()) {
            break;
        }
        if (message.value(QStringLiteral("method")).toString() == QStringLiteral("event")) {
            ++events;
        }
    }
    QCOMPARE(events, 1);

    sendMessage(socket,
                {{QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
                 {QStringLiteral("id"), 4},
                 {QStringLiteral("method"), QStringLiteral("containers.stats.subscribe")},
                 {QStringLiteral("params"),
                  QJsonObject{{QStringLiteral("id"), QStringLiteral("abc123")},
                              {QStringLiteral("intervalSeconds"), 1}}}});

    int stats = 0;
    QString statsSubscription;
    for (int i = 0; i < 20 && stats < 1; ++i) {
        const QJsonObject message = readMessage(socket, buffer);
        if (message.isEmpty()) {
            break;
        }
        if (message.value(QStringLiteral("id")).toInt() == 4) {
            statsSubscription = message.value(QStringLiteral("result"))
                                    .toObject()
                                    .value(QStringLiteral("subscriptionId"))
                                    .toString();
        }
        if (message.value(QStringLiteral("method")).toString()
            == QStringLiteral("containers.stats.delta")) {
            ++stats;
        }
    }
    QCOMPARE(stats, 1);
    QVERIFY(!statsSubscription.isEmpty());
    sendMessage(socket,
                {{QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
                 {QStringLiteral("id"), 5},
                 {QStringLiteral("method"), QStringLiteral("containers.stats.unsubscribe")},
                 {QStringLiteral("params"),
                  QJsonObject{{QStringLiteral("subscriptionId"), statsSubscription}}}});
    QVERIFY(!readMessage(socket, buffer).isEmpty());
}

void RpcTest::catalog()
{
    PodStack::RpcClient client(m_controlSocket);
    PodStack::Error error;

    QJsonObject result = client.call(QStringLiteral("images.list"), {}, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QCOMPARE(result.value(QStringLiteral("items")).toArray().size(), 2);

    result = client.call(QStringLiteral("images.history"),
                         QJsonObject{{QStringLiteral("id"), QStringLiteral("img1234567890")}},
                         &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QCOMPARE(result.value(QStringLiteral("items")).toArray().size(), 1);

    result = client.call(QStringLiteral("images.tag"),
                         QJsonObject{{QStringLiteral("id"), QStringLiteral("img1234567890")},
                                     {QStringLiteral("repo"), QStringLiteral("repo")},
                                     {QStringLiteral("tag"), QStringLiteral("v1")}},
                         &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));

    result = client.call(QStringLiteral("images.untag"),
                         QJsonObject{{QStringLiteral("id"), QStringLiteral("img1234567890")},
                                     {QStringLiteral("repo"), QStringLiteral("repo")},
                                     {QStringLiteral("tag"), QStringLiteral("v1")}},
                         &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));

    result = client.call(QStringLiteral("images.remove"),
                         QJsonObject{{QStringLiteral("id"), QStringLiteral("img1234567890")}},
                         &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QVERIFY(result.value(QStringLiteral("removed")).toBool());

    result = client.call(QStringLiteral("images.prune"), {}, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QCOMPARE(static_cast<qint64>(
                 result.value(QStringLiteral("reclaimedBytes")).toDouble()),
             12345);

    result = client.call(QStringLiteral("volumes.list"), {}, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QCOMPARE(result.value(QStringLiteral("items")).toArray().size(), 1);

    const QString sourceDir = QStringLiteral("/tmp/podstack-mock-volume");
    const QString destinationDir = QStringLiteral("/tmp/podstack-mock-volume2");
    QDir(sourceDir).removeRecursively();
    QDir(destinationDir).removeRecursively();
    QDir().mkpath(sourceDir);
    QFile sourceFile(sourceDir + QStringLiteral("/a.txt"));
    QVERIFY(sourceFile.open(QIODevice::WriteOnly));
    sourceFile.write("hello");
    sourceFile.close();

    result = client.call(QStringLiteral("volumes.create"),
                         QJsonObject{{QStringLiteral("name"), QStringLiteral("vol2")}},
                         &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QCOMPARE(result.value(QStringLiteral("Name")).toString(), QStringLiteral("vol2"));

    result = client.call(QStringLiteral("volumes.clone"),
                         QJsonObject{{QStringLiteral("source"), QStringLiteral("vol1")},
                                     {QStringLiteral("destination"), QStringLiteral("vol2")}},
                         &error, 60000);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));

    const QString archivePath = QDir::tempPath()
                                + QStringLiteral("/podstack-rpc-volume-%1.tar.zst")
                                      .arg(QCoreApplication::applicationPid());
    QFile::remove(archivePath);
    result = client.call(QStringLiteral("volumes.export"),
                         QJsonObject{{QStringLiteral("name"), QStringLiteral("vol1")},
                                     {QStringLiteral("path"), archivePath}},
                         &error, 60000);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QVERIFY(QFileInfo(archivePath).size() > 0);

    result = client.call(QStringLiteral("volumes.import"),
                         QJsonObject{{QStringLiteral("name"), QStringLiteral("vol2")},
                                     {QStringLiteral("path"), archivePath},
                                     {QStringLiteral("force"), true}},
                         &error, 60000);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QFile::remove(archivePath);

    const QString hostDir = QStringLiteral("/tmp/podstack-host-src");
    QDir(hostDir).removeRecursively();
    QDir().mkpath(hostDir);
    QFile hostFile(hostDir + QStringLiteral("/b.txt"));
    QVERIFY(hostFile.open(QIODevice::WriteOnly));
    hostFile.write("host-content");
    hostFile.close();
    result = client.call(QStringLiteral("volumes.importHostPath"),
                         QJsonObject{{QStringLiteral("name"), QStringLiteral("vol2")},
                                     {QStringLiteral("sourcePath"), hostDir}},
                         &error, 60000);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QDir(hostDir).removeRecursively();

    result = client.call(QStringLiteral("volumes.remove"),
                         QJsonObject{{QStringLiteral("name"), QStringLiteral("vol1")}},
                         &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));

    result = client.call(QStringLiteral("volumes.prune"), {}, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QCOMPARE(static_cast<qint64>(
                 result.value(QStringLiteral("reclaimedBytes")).toDouble()),
             456);

    result = client.call(QStringLiteral("networks.list"), {}, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QCOMPARE(result.value(QStringLiteral("items")).toArray().size(), 1);

    result = client.call(QStringLiteral("networks.create"),
                         QJsonObject{{QStringLiteral("name"), QStringLiteral("mock-net")},
                                     {QStringLiteral("subnet"),
                                      QStringLiteral("10.90.0.0/24")}},
                         &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QCOMPARE(result.value(QStringLiteral("name")).toString(), QStringLiteral("mock-net"));

    result = client.call(QStringLiteral("networks.connect"),
                         QJsonObject{{QStringLiteral("name"), QStringLiteral("mock-net")},
                                     {QStringLiteral("containerId"),
                                      QStringLiteral("abc123")}},
                         &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));

    result = client.call(QStringLiteral("networks.disconnect"),
                         QJsonObject{{QStringLiteral("name"), QStringLiteral("mock-net")},
                                     {QStringLiteral("containerId"),
                                      QStringLiteral("abc123")}},
                         &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));

    result = client.call(QStringLiteral("networks.remove"),
                         QJsonObject{{QStringLiteral("name"), QStringLiteral("mock-net")}},
                         &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));

    result = client.call(QStringLiteral("pods.list"), {}, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QCOMPARE(result.value(QStringLiteral("items")).toArray().size(), 1);

    result = client.call(QStringLiteral("pods.create"),
                         QJsonObject{{QStringLiteral("name"), QStringLiteral("demo-pod")}},
                         &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QCOMPARE(result.value(QStringLiteral("Id")).toString(), QStringLiteral("pod123"));

    result = client.call(QStringLiteral("pods.start"),
                         QJsonObject{{QStringLiteral("id"), QStringLiteral("pod123")}},
                         &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QVERIFY(result.value(QStringLiteral("ok")).toBool());

    result = client.call(QStringLiteral("pods.remove"),
                         QJsonObject{{QStringLiteral("id"), QStringLiteral("pod123")}},
                         &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));

    result = client.call(QStringLiteral("system.df"), {}, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QCOMPARE(static_cast<qint64>(result.value(QStringLiteral("json"))
                                     .toObject()
                                     .value(QStringLiteral("ImagesSize"))
                                     .toDouble()),
             7001000);
    QVERIFY(result.value(QStringLiteral("detailed")).toBool());
    QJsonObject sections = result.value(QStringLiteral("sections")).toObject();
    QCOMPARE(sections.value(QStringLiteral("images"))
                 .toObject()
                 .value(QStringLiteral("count"))
                 .toInt(),
             1);
    QCOMPARE(sections.value(QStringLiteral("images"))
                 .toObject()
                 .value(QStringLiteral("items"))
                 .toArray()
                 .size(),
             1);
    const QJsonObject imageItem = sections.value(QStringLiteral("images"))
                                      .toObject()
                                      .value(QStringLiteral("items"))
                                      .toArray()
                                      .first()
                                      .toObject();
    QVERIFY(imageItem.value(QStringLiteral("dangling")).toBool());
    QVERIFY(imageItem.value(QStringLiteral("unused")).toBool());
    QCOMPARE(static_cast<qint64>(
                 imageItem.value(QStringLiteral("uniqueSizeBytes")).toDouble()),
             6000000);
    QCOMPARE(static_cast<qint64>(sections.value(QStringLiteral("images"))
                                     .toObject()
                                     .value(QStringLiteral("reclaimableBytes"))
                                     .toDouble()),
             6000000);
    QCOMPARE(sections.value(QStringLiteral("containers"))
                 .toObject()
                 .value(QStringLiteral("count"))
                 .toInt(),
             1);
    QCOMPARE(static_cast<qint64>(sections.value(QStringLiteral("containers"))
                                     .toObject()
                                     .value(QStringLiteral("reclaimableBytes"))
                                     .toDouble()),
             1234);
    QCOMPARE(sections.value(QStringLiteral("volumes"))
                 .toObject()
                 .value(QStringLiteral("count"))
                 .toInt(),
             1);
    QVERIFY(sections.value(QStringLiteral("volumes"))
                .toObject()
                .value(QStringLiteral("items"))
                .toArray()
                .first()
                .toObject()
                .value(QStringLiteral("inUse"))
                .toBool());
    QCOMPARE(static_cast<qint64>(sections.value(QStringLiteral("volumes"))
                                     .toObject()
                                     .value(QStringLiteral("reclaimableBytes"))
                                     .toDouble()),
             0);
    QCOMPARE(static_cast<qint64>(sections.value(QStringLiteral("volumes"))
                                     .toObject()
                                     .value(QStringLiteral("items"))
                                     .toArray()
                                     .first()
                                     .toObject()
                                     .value(QStringLiteral("reclaimableBytes"))
                                     .toDouble()),
             0);
    QCOMPARE(static_cast<qint64>(
                 result.value(QStringLiteral("totalReclaimableBytes")).toDouble()),
             6001234);

    result = client.call(QStringLiteral("system.df"),
                         QJsonObject{{QStringLiteral("detailed"), false}}, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QVERIFY(!result.value(QStringLiteral("detailed")).toBool());
    sections = result.value(QStringLiteral("sections")).toObject();
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
    QVERIFY(!sections.value(QStringLiteral("images"))
                 .toObject()
                 .contains(QStringLiteral("items")));
    QCOMPARE(static_cast<qint64>(
                 result.value(QStringLiteral("totalReclaimableBytes")).toDouble()),
             6001234);

    QLocalSocket socket;
    socket.connectToServer(m_controlSocket, QIODevice::ReadWrite);
    QVERIFY(socket.waitForConnected(5000));
    QByteArray buffer;
    sendMessage(socket,
                {{QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
                 {QStringLiteral("id"), 1},
                 {QStringLiteral("method"), QStringLiteral("images.pull")},
                 {QStringLiteral("params"),
                  QJsonObject{{QStringLiteral("reference"), QStringLiteral("alpine")}}}});

    const QJsonObject pullResponse = readMessage(socket, buffer);
    QVERIFY(!pullResponse.value(QStringLiteral("result"))
                 .toObject()
                 .value(QStringLiteral("subscriptionId"))
                 .toString()
                 .isEmpty());

    int deltas = 0;
    bool done = false;
    for (int i = 0; i < 20 && !done; ++i) {
        const QJsonObject message = readMessage(socket, buffer);
        if (message.isEmpty()) {
            break;
        }
        const QString method = message.value(QStringLiteral("method")).toString();
        if (method == QStringLiteral("images.pull.delta")) {
            ++deltas;
        } else if (method == QStringLiteral("images.pull.done")) {
            done = true;
        }
    }
    QVERIFY(deltas >= 2);
    QVERIFY(done);
    socket.disconnectFromServer();

    QDir(sourceDir).removeRecursively();
    QDir(destinationDir).removeRecursively();
}

void RpcTest::machinesDistros()
{
    PodStack::RpcClient client(m_controlSocket);
    PodStack::Error error;

    const QJsonObject result = client.call(QStringLiteral("machines.distros"), {}, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));

    const QJsonArray items = result.value(QStringLiteral("items")).toArray();
    QVERIFY(items.size() >= 3);

    QStringList distros;
    for (const QJsonValue &value : items) {
        const QJsonObject item = value.toObject();
        distros.append(item.value(QStringLiteral("distro")).toString());
        QVERIFY(!item.value(QStringLiteral("version")).toString().isEmpty());
        QCOMPARE(item.value(QStringLiteral("arch")).toString(), QStringLiteral("amd64"));
        QVERIFY(!item.value(QStringLiteral("url")).toString().isEmpty());
        QVERIFY(!item.value(QStringLiteral("checksum")).toString().isEmpty());
        QVERIFY(!item.value(QStringLiteral("algorithm")).toString().isEmpty());
        QVERIFY(item.contains(QStringLiteral("size")));
        QVERIFY(item.contains(QStringLiteral("default")));
        QVERIFY(item.contains(QStringLiteral("cached")));
    }
    QVERIFY(distros.contains(QStringLiteral("ubuntu")));
    QVERIFY(distros.contains(QStringLiteral("debian")));
    QVERIFY(distros.contains(QStringLiteral("fedora")));
}

void RpcTest::machinesFlow()
{
    PodStack::RpcClient client(m_controlSocket);
    PodStack::Error error;

    const QJsonObject list = client.call(QStringLiteral("machines.list"), {}, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QCOMPARE(list.value(QStringLiteral("items")).toArray().size(), 0);

    PodStack::Error missing;
    client.call(QStringLiteral("machines.get"),
                QJsonObject{{QStringLiteral("id"), QStringLiteral("missing-machine")}},
                &missing);
    QCOMPARE(missing.code, QStringLiteral("PODSTACK_NOT_FOUND"));

    PodStack::Error invalid;
    client.call(QStringLiteral("machines.create"), {}, &invalid);
    QCOMPARE(invalid.code, QStringLiteral("PODSTACK_INVALID_PARAMS"));

    invalid = {};
    client.call(QStringLiteral("machines.create"),
                QJsonObject{{QStringLiteral("name"), QStringLiteral("valid-name")},
                            {QStringLiteral("cpus"), 0}},
                &invalid);
    QCOMPARE(invalid.code, QStringLiteral("PODSTACK_INVALID_PARAMS"));

    invalid = {};
    client.call(QStringLiteral("machines.create"),
                QJsonObject{{QStringLiteral("name"), QStringLiteral("valid-name")},
                            {QStringLiteral("distro"), QStringLiteral("ubuntu")},
                            {QStringLiteral("memoryBytes"), 1}},
                &invalid);
    QCOMPARE(invalid.code, QStringLiteral("PODSTACK_INVALID_PARAMS"));

    const QString sharedSource = m_testDataDir + QStringLiteral("/shared");
    QVERIFY(QDir().mkpath(sharedSource));

    const auto expectInvalidMounts = [&client](const QJsonArray &mounts) {
        PodStack::Error mountError;
        client.call(QStringLiteral("machines.create"),
                    QJsonObject{{QStringLiteral("name"), QStringLiteral("valid-name")},
                                {QStringLiteral("distro"), QStringLiteral("ubuntu")},
                                {QStringLiteral("mounts"), mounts}},
                    &mountError);
        QCOMPARE(mountError.code, QStringLiteral("PODSTACK_INVALID_PARAMS"));
    };

    expectInvalidMounts(QJsonArray{QJsonObject{
        {QStringLiteral("source"), sharedSource}}});
    expectInvalidMounts(QJsonArray{QJsonObject{
        {QStringLiteral("source"), QStringLiteral("shared")},
        {QStringLiteral("target"), QStringLiteral("/mnt/shared")}}});
    expectInvalidMounts(QJsonArray{QJsonObject{
        {QStringLiteral("source"), m_testDataDir + QStringLiteral("/missing")},
        {QStringLiteral("target"), QStringLiteral("/mnt/shared")}}});
    expectInvalidMounts(QJsonArray{QJsonObject{
        {QStringLiteral("source"), sharedSource},
        {QStringLiteral("target"), QStringLiteral("mnt/shared")}}});
    expectInvalidMounts(QJsonArray{QJsonObject{
        {QStringLiteral("source"), sharedSource},
        {QStringLiteral("target"), QStringLiteral("/mnt/../etc")}}});
    expectInvalidMounts(QJsonArray{QJsonObject{
        {QStringLiteral("source"), sharedSource},
        {QStringLiteral("target"), QStringLiteral("/mnt/a")},
        {QStringLiteral("tag"), QStringLiteral("Host 0")}}});

    QJsonArray duplicateTags{
        QJsonObject{{QStringLiteral("source"), sharedSource},
                    {QStringLiteral("target"), QStringLiteral("/mnt/a")},
                    {QStringLiteral("tag"), QStringLiteral("dados")}},
        QJsonObject{{QStringLiteral("source"), sharedSource},
                    {QStringLiteral("target"), QStringLiteral("/mnt/b")},
                    {QStringLiteral("tag"), QStringLiteral("dados")}}};
    expectInvalidMounts(duplicateTags);

    QJsonArray tooManyMounts;
    for (int i = 0; i < 9; ++i) {
        tooManyMounts.append(QJsonObject{
            {QStringLiteral("source"), sharedSource},
            {QStringLiteral("target"), QStringLiteral("/mnt/%1").arg(i)}});
    }
    expectInvalidMounts(tooManyMounts);

    PodStack::Error isolatedAgent;
    client.call(QStringLiteral("machines.create"),
                QJsonObject{{QStringLiteral("name"), QStringLiteral("isolated-agent")},
                            {QStringLiteral("distro"), QStringLiteral("ubuntu")},
                            {QStringLiteral("isolated"), true},
                            {QStringLiteral("forwardSshAgent"), true}},
                &isolatedAgent);
    QCOMPARE(isolatedAgent.code, QStringLiteral("PODSTACK_INVALID_PARAMS"));
    QVERIFY(!isolatedAgent.remediation.isEmpty());

    PodStack::Error isolatedMount;
    client.call(QStringLiteral("machines.create"),
                QJsonObject{
                    {QStringLiteral("name"), QStringLiteral("isolated-mount")},
                    {QStringLiteral("distro"), QStringLiteral("ubuntu")},
                    {QStringLiteral("isolated"), true},
                    {QStringLiteral("mounts"),
                     QJsonArray{QJsonObject{
                         {QStringLiteral("source"), sharedSource},
                         {QStringLiteral("target"), QStringLiteral("/mnt/isolated")}}}}},
                &isolatedMount);
    QCOMPARE(isolatedMount.code, QStringLiteral("PODSTACK_INVALID_PARAMS"));
    QVERIFY(!isolatedMount.remediation.isEmpty());

    // `isolated` não é mais recusado de forma síncrona: o job inicia e falha
    // depois na resolução da distro desconhecida.
    PodStack::Error isolatedAcceptedError;
    const QJsonObject isolatedJob =
        client.call(QStringLiteral("machines.create"),
                    QJsonObject{{QStringLiteral("name"), QStringLiteral("isolated-accepted")},
                                {QStringLiteral("distro"),
                                 QStringLiteral("distro-inexistente")},
                                {QStringLiteral("isolated"), true}},
                    &isolatedAcceptedError);
    QVERIFY2(isolatedAcceptedError.isEmpty(), qPrintable(isolatedAcceptedError.message));
    QVERIFY(!isolatedJob.value(QStringLiteral("jobId")).toString().isEmpty());

    // Com mounts válidos o create síncrono precisa aceitar o array e iniciar o
    // job. A distro desconhecida falha antes de qualquer download/QEMU.
    QLocalSocket mountSocket;
    mountSocket.connectToServer(m_controlSocket, QIODevice::ReadWrite);
    QVERIFY(mountSocket.waitForConnected(3000));
    QByteArray mountBuffer;
    sendMessage(mountSocket,
                QJsonObject{
                    {QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
                    {QStringLiteral("id"), 4242},
                    {QStringLiteral("method"), QStringLiteral("machines.create")},
                    {QStringLiteral("params"),
                     QJsonObject{
                         {QStringLiteral("name"), QStringLiteral("mounts-valid")},
                         {QStringLiteral("distro"), QStringLiteral("distro-inexistente")},
                         {QStringLiteral("mounts"),
                          QJsonArray{QJsonObject{
                              {QStringLiteral("source"), sharedSource},
                              {QStringLiteral("target"), QStringLiteral("/mnt/shared")},
                              {QStringLiteral("readOnly"), true}}}}}},
                });

    QJsonObject createResponse;
    QJsonObject donePayload;
    QElapsedTimer mountTimer;
    mountTimer.start();
    while (mountTimer.elapsed() < 5000 && donePayload.isEmpty()) {
        const int remaining = 5000 - static_cast<int>(mountTimer.elapsed());
        if (remaining <= 0) {
            break;
        }
        const QJsonObject message = readMessage(mountSocket, mountBuffer, remaining);
        if (message.isEmpty()) {
            break;
        }
        if (message.value(QStringLiteral("id")).toInt() == 4242) {
            createResponse = message;
        }
        if (message.value(QStringLiteral("method")).toString()
            == QStringLiteral("machines.done")) {
            donePayload = message.value(QStringLiteral("params")).toObject();
        }
    }
    QVERIFY(!createResponse.isEmpty());
    QVERIFY(createResponse.value(QStringLiteral("error")).isUndefined());
    QVERIFY(!createResponse.value(QStringLiteral("result"))
                 .toObject()
                 .value(QStringLiteral("jobId"))
                 .toString()
                 .isEmpty());
    QVERIFY(!donePayload.isEmpty());
    const QJsonObject jobError = donePayload.value(QStringLiteral("error")).toObject();
    QVERIFY(!jobError.isEmpty());
    QVERIFY(jobError.value(QStringLiteral("code")).toString()
            != QStringLiteral("PODSTACK_UNSUPPORTED"));

    for (const QString &method :
         {QStringLiteral("machines.start"), QStringLiteral("machines.stop"),
          QStringLiteral("machines.restart"), QStringLiteral("machines.delete")}) {
        PodStack::Error lifecycle;
        client.call(method,
                    QJsonObject{{QStringLiteral("id"), QStringLiteral("missing-machine")}},
                    &lifecycle);
        QCOMPARE(lifecycle.code, QStringLiteral("PODSTACK_NOT_FOUND"));
    }

    PodStack::Error cancel;
    client.call(QStringLiteral("machines.cancel"),
                QJsonObject{{QStringLiteral("jobId"), QStringLiteral("missing-job")}}, &cancel);
    QCOMPARE(cancel.code, QStringLiteral("PODSTACK_NOT_FOUND"));

    const QJsonObject after = client.call(QStringLiteral("machines.list"), {}, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QCOMPARE(after.value(QStringLiteral("items")).toArray().size(), 0);
}

void RpcTest::machineLogs()
{
    PodStack::RpcClient client(m_controlSocket);
    PodStack::Error error;

    client.call(QStringLiteral("machines.logs"),
                QJsonObject{{QStringLiteral("id"), QStringLiteral("missing-machine")}},
                &error);
    QCOMPARE(error.code, QStringLiteral("PODSTACK_NOT_FOUND"));

    PodStack::MachineStore store(m_testDataDir + QStringLiteral("/data/podstack/podstack.db"));
    PodStack::Error storeError;
    QVERIFY2(store.open(&storeError), qPrintable(storeError.message));

    PodStack::MachineMeta meta;
    meta.id = QStringLiteral("machine-logs-1");
    meta.name = QStringLiteral("logs-demo");
    meta.distro = QStringLiteral("ubuntu");
    meta.distroVersion = QStringLiteral("24.04");
    meta.arch = QStringLiteral("amd64");
    meta.cpus = 2;
    meta.memoryBytes = 4LL * 1024LL * 1024LL * 1024LL;
    meta.diskBytes = 20LL * 1024LL * 1024LL * 1024LL;
    meta.state = QStringLiteral("stopped");
    meta.createdAt = QStringLiteral("2026-01-01T00:00:00.000Z");
    meta.updatedAt = meta.createdAt;
    QVERIFY2(store.upsert(meta, &storeError), qPrintable(storeError.message));

    const QString machinesRoot = m_testDataDir + QStringLiteral("/data/podstack/machines");
    const QString machineDir = machinesRoot + QLatin1Char('/') + meta.id;
    QVERIFY(QDir().mkpath(machineDir));

    QFile console(machineDir + QStringLiteral("/console.log"));
    QVERIFY(console.open(QIODevice::WriteOnly | QIODevice::Truncate));
    console.write("boot 1\nboot 2\nboot 3\nboot 4\nboot 5\n");
    console.close();
    QVERIFY(console.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner));

    const QJsonObject all =
        client.call(QStringLiteral("machines.logs"),
                    QJsonObject{{QStringLiteral("id"), meta.id}}, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QJsonArray lines = all.value(QStringLiteral("lines")).toArray();
    QCOMPARE(lines.size(), 5);
    QCOMPARE(lines.first().toString(), QStringLiteral("boot 1"));
    QCOMPARE(lines.last().toString(), QStringLiteral("boot 5"));

    const QJsonObject tail =
        client.call(QStringLiteral("machines.logs"),
                    QJsonObject{{QStringLiteral("id"), QStringLiteral("logs-demo")},
                                {QStringLiteral("tail"), 2}},
                    &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    lines = tail.value(QStringLiteral("lines")).toArray();
    QCOMPARE(lines.size(), 2);
    QCOMPARE(lines.first().toString(), QStringLiteral("boot 4"));
    QCOMPARE(lines.last().toString(), QStringLiteral("boot 5"));

    PodStack::MachineMeta unstarted = meta;
    unstarted.id = QStringLiteral("machine-logs-2");
    unstarted.name = QStringLiteral("no-console");
    QVERIFY2(store.upsert(unstarted, &storeError), qPrintable(storeError.message));
    QVERIFY(QDir().mkpath(machinesRoot + QLatin1Char('/') + unstarted.id));

    const QJsonObject empty =
        client.call(QStringLiteral("machines.logs"),
                    QJsonObject{{QStringLiteral("id"), unstarted.id}}, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QCOMPARE(empty.value(QStringLiteral("lines")).toArray().size(), 0);

    PodStack::MachineMeta symlinked = meta;
    symlinked.id = QStringLiteral("machine-logs-3");
    symlinked.name = QStringLiteral("symlink-console");
    QVERIFY2(store.upsert(symlinked, &storeError), qPrintable(storeError.message));
    const QString symlinkDir = machinesRoot + QLatin1Char('/') + symlinked.id;
    QVERIFY(QDir().mkpath(symlinkDir));
    const QString outside = m_testDataDir + QStringLiteral("/outside.log");
    QFile outsideFile(outside);
    QVERIFY(outsideFile.open(QIODevice::WriteOnly | QIODevice::Truncate));
    outsideFile.write("secret\n");
    outsideFile.close();
    QVERIFY(QFile::link(outside, symlinkDir + QStringLiteral("/console.log")));

    PodStack::Error symlinkError;
    client.call(QStringLiteral("machines.logs"),
                QJsonObject{{QStringLiteral("id"), symlinked.id}}, &symlinkError);
    QCOMPARE(symlinkError.code, QStringLiteral("PODSTACK_IO_ERROR"));

    QVERIFY(store.remove(meta.id));
    QVERIFY(store.remove(unstarted.id));
    QVERIFY(store.remove(symlinked.id));
    QDir(machineDir).removeRecursively();
    QDir(machinesRoot + QLatin1Char('/') + unstarted.id).removeRecursively();
    QDir(symlinkDir).removeRecursively();
    QFile::remove(outside);
}

void RpcTest::machineSsh()
{
    PodStack::RpcClient client(m_controlSocket);
    PodStack::Error error;

    client.call(QStringLiteral("machines.ssh"),
                QJsonObject{{QStringLiteral("id"), QStringLiteral("missing-machine")}},
                &error);
    QCOMPARE(error.code, QStringLiteral("PODSTACK_NOT_FOUND"));

    PodStack::MachineStore store(m_testDataDir + QStringLiteral("/data/podstack/podstack.db"));
    PodStack::Error storeError;
    QVERIFY2(store.open(&storeError), qPrintable(storeError.message));

    PodStack::MachineMeta meta;
    meta.id = QStringLiteral("machine-ssh-1");
    meta.name = QStringLiteral("ssh-demo");
    meta.distro = QStringLiteral("ubuntu");
    meta.distroVersion = QStringLiteral("24.04");
    meta.arch = QStringLiteral("amd64");
    meta.state = QStringLiteral("running");
    meta.sshUser = QStringLiteral("podstack");
    meta.sshPort = 32222;
    meta.sshKeyPath = m_testDataDir + QStringLiteral("/keys/id_ed25519");
    meta.forwardSshAgent = true;
    meta.createdAt = QStringLiteral("2026-01-01T00:00:00.000Z");
    meta.updatedAt = meta.createdAt;
    QVERIFY2(store.upsert(meta, &storeError), qPrintable(storeError.message));

    QVERIFY(QDir().mkpath(QFileInfo(meta.sshKeyPath).absolutePath()));
    QFile key(meta.sshKeyPath);
    QVERIFY(key.open(QIODevice::WriteOnly | QIODevice::Truncate));
    key.write("FAKE-PRIVATE-KEY");
    key.close();
    QVERIFY(key.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner));

    const QJsonObject result =
        client.call(QStringLiteral("machines.ssh"),
                    QJsonObject{{QStringLiteral("id"), QStringLiteral("ssh-demo")}}, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QCOMPARE(result.value(QStringLiteral("host")).toString(), QStringLiteral("127.0.0.1"));
    QCOMPARE(result.value(QStringLiteral("port")).toInt(), 32222);
    QCOMPARE(result.value(QStringLiteral("user")).toString(), QStringLiteral("podstack"));
    QCOMPARE(result.value(QStringLiteral("identityFile")).toString(), meta.sshKeyPath);
    QCOMPARE(result.value(QStringLiteral("forwardAgent")).toBool(), true);
    QVERIFY(!result.contains(QStringLiteral("name")));
    QVERIFY(!result.contains(QStringLiteral("keyContent")));

    const QJsonObject byId =
        client.call(QStringLiteral("machines.ssh"),
                    QJsonObject{{QStringLiteral("id"), meta.id}}, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QCOMPARE(byId.value(QStringLiteral("identityFile")).toString(), meta.sshKeyPath);

    const QString configPath = m_testDataDir + QStringLiteral("/ssh/config");
    QFile config(configPath);
    QVERIFY(config.open(QIODevice::ReadOnly));
    QVERIFY(QString::fromUtf8(config.readAll())
                .contains(QStringLiteral("# >>> podstack: ssh-demo")));
    config.close();

    QVERIFY(store.remove(meta.id));
    QFile::remove(meta.sshKeyPath);
    QDir().rmdir(QFileInfo(meta.sshKeyPath).absolutePath());
}

void RpcTest::machineFiles()
{
    PodStack::RpcClient client(m_controlSocket);
    PodStack::Error error;

    for (const QString &method :
         {QStringLiteral("machines.files.list"),
          QStringLiteral("machines.files.download"),
          QStringLiteral("machines.files.upload")}) {
        PodStack::Error missing;
        client.call(method,
                    QJsonObject{{QStringLiteral("id"), QStringLiteral("missing-machine")},
                                {QStringLiteral("path"), QStringLiteral("/")},
                                {QStringLiteral("destination"),
                                 m_downloadDir + QStringLiteral("/missing.txt")},
                                {QStringLiteral("sourcePath"),
                                 m_testDataDir + QStringLiteral("/missing.txt")}},
                    &missing);
        QCOMPARE(missing.code, QStringLiteral("PODSTACK_NOT_FOUND"));
    }

    PodStack::MachineStore store(m_testDataDir + QStringLiteral("/data/podstack/podstack.db"));
    PodStack::Error storeError;
    QVERIFY2(store.open(&storeError), qPrintable(storeError.message));

    PodStack::MachineMeta meta;
    meta.id = QStringLiteral("machine-files-1");
    meta.name = QStringLiteral("files-demo");
    meta.distro = QStringLiteral("fedora");
    meta.distroVersion = QStringLiteral("44");
    meta.arch = QStringLiteral("amd64");
    meta.state = QStringLiteral("running");
    meta.sshUser = QStringLiteral("podstack");
    meta.sshPort = 32222;
    meta.sshKeyPath = m_testDataDir + QStringLiteral("/keys/files_ed25519");
    meta.createdAt = QStringLiteral("2026-01-01T00:00:00.000Z");
    meta.updatedAt = meta.createdAt;
    QVERIFY2(store.upsert(meta, &storeError), qPrintable(storeError.message));
    QVERIFY(QDir().mkpath(QFileInfo(meta.sshKeyPath).absolutePath()));
    QFile key(meta.sshKeyPath);
    QVERIFY(key.open(QIODevice::WriteOnly | QIODevice::Truncate));
    key.write("FAKE-PRIVATE-KEY");
    key.close();
    QVERIFY(key.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner));

    PodStack::Error invalidPath;
    client.call(QStringLiteral("machines.files.list"),
                QJsonObject{{QStringLiteral("id"), meta.id},
                            {QStringLiteral("path"), QStringLiteral("/tmp/bad\npath")}},
                &invalidPath);
    QCOMPARE(invalidPath.code, QStringLiteral("PODSTACK_INVALID_PARAMS"));

    const QJsonObject listing =
        client.call(QStringLiteral("machines.files.list"),
                    QJsonObject{{QStringLiteral("id"), QStringLiteral("files-demo")},
                                {QStringLiteral("path"), QStringLiteral("/home/podstack")}},
                    &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QCOMPARE(listing.value(QStringLiteral("path")).toString(),
             QStringLiteral("/home/podstack"));
    const QJsonArray items = listing.value(QStringLiteral("items")).toArray();
    QCOMPARE(items.size(), 2);
    bool sawDirectory = false;
    bool sawFile = false;
    for (const QJsonValue &value : items) {
        const QJsonObject item = value.toObject();
        QVERIFY(!item.value(QStringLiteral("path")).toString().contains(m_testDataDir));
        if (item.value(QStringLiteral("name")).toString() == QStringLiteral("docs")) {
            sawDirectory = item.value(QStringLiteral("directory")).toBool();
            QCOMPARE(item.value(QStringLiteral("path")).toString(),
                     QStringLiteral("/home/podstack/docs"));
        }
        if (item.value(QStringLiteral("name")).toString() == QStringLiteral("rpc.txt")) {
            sawFile = !item.value(QStringLiteral("directory")).toBool();
            QCOMPARE(item.value(QStringLiteral("size")).toInteger(), 42LL);
        }
    }
    QVERIFY(sawDirectory);
    QVERIFY(sawFile);

    const QJsonObject download =
        client.call(QStringLiteral("machines.files.download"),
                    QJsonObject{{QStringLiteral("id"), meta.id},
                                {QStringLiteral("path"),
                                 QStringLiteral("/home/podstack/rpc.txt")},
                                {QStringLiteral("destination"),
                                 m_downloadDir + QStringLiteral("/rpc.txt")}},
                    &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QCOMPARE(download.value(QStringLiteral("path")).toString(),
             QStringLiteral("/home/podstack/rpc.txt"));
    QCOMPARE(download.value(QStringLiteral("destination")).toString(),
             m_downloadDir + QStringLiteral("/rpc.txt"));
    QVERIFY(QFileInfo::exists(m_downloadDir + QStringLiteral("/rpc.txt")));

    const QString sourcePath = m_testDataDir + QStringLiteral("/rpc-upload.txt");
    QFile source(sourcePath);
    QVERIFY(source.open(QIODevice::WriteOnly | QIODevice::Truncate));
    source.write("rpc upload");
    source.close();

    const QJsonObject upload =
        client.call(QStringLiteral("machines.files.upload"),
                    QJsonObject{{QStringLiteral("id"), meta.id},
                                {QStringLiteral("path"),
                                 QStringLiteral("/tmp/rpc-upload.txt")},
                                {QStringLiteral("sourcePath"), sourcePath}},
                    &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QCOMPARE(upload.value(QStringLiteral("path")).toString(),
             QStringLiteral("/tmp/rpc-upload.txt"));
    QCOMPARE(upload.value(QStringLiteral("sourcePath")).toString(), sourcePath);
    QCOMPARE(upload.value(QStringLiteral("ok")).toBool(), true);

    PodStack::Error invalidSource;
    client.call(QStringLiteral("machines.files.upload"),
                QJsonObject{{QStringLiteral("id"), meta.id},
                            {QStringLiteral("path"), QStringLiteral("/tmp/x")},
                            {QStringLiteral("sourcePath"),
                             m_testDataDir + QStringLiteral("/does-not-exist")}},
                &invalidSource);
    QCOMPARE(invalidSource.code, QStringLiteral("PODSTACK_IO_ERROR"));

    QVERIFY(store.remove(meta.id));
    QFile::remove(meta.sshKeyPath);
    QFile::remove(sourcePath);
    QFile::remove(m_downloadDir + QStringLiteral("/rpc.txt"));
    QDir().rmdir(QFileInfo(meta.sshKeyPath).absolutePath());
}

void RpcTest::machinePorts()
{
    PodStack::RpcClient client(m_controlSocket);
    PodStack::Error error;

    client.call(QStringLiteral("machines.ports.list"),
                QJsonObject{{QStringLiteral("id"), QStringLiteral("missing-machine")}}, &error);
    QCOMPARE(error.code, QStringLiteral("PODSTACK_NOT_FOUND"));

    PodStack::MachineStore store(m_testDataDir + QStringLiteral("/data/podstack/podstack.db"));
    PodStack::Error storeError;
    QVERIFY2(store.open(&storeError), qPrintable(storeError.message));

    PodStack::MachineMeta meta;
    meta.id = QStringLiteral("machine-ports-1");
    meta.name = QStringLiteral("ports-rpc");
    meta.distro = QStringLiteral("ubuntu");
    meta.distroVersion = QStringLiteral("24.04");
    meta.arch = QStringLiteral("amd64");
    meta.state = QStringLiteral("stopped");
    meta.sshPort = 32222;
    meta.sshUser = QStringLiteral("podstack");
    meta.createdAt = QStringLiteral("2026-01-01T00:00:00.000Z");
    meta.updatedAt = meta.createdAt;
    meta.ports = {PodStack::MachinePort{QStringLiteral("ssh-rpc"),
                                       32222,
                                       22,
                                       QStringLiteral("tcp"),
                                       false,
                                       QStringLiteral("ssh")}};
    QVERIFY2(store.upsert(meta, &storeError), qPrintable(storeError.message));

    const QJsonObject initial =
        client.call(QStringLiteral("machines.ports.list"),
                    QJsonObject{{QStringLiteral("id"), meta.name}}, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QCOMPARE(initial.value(QStringLiteral("items")).toArray().size(), 1);

    PodStack::Error invalidType;
    client.call(QStringLiteral("machines.ports.set"),
                QJsonObject{{QStringLiteral("id"), meta.id},
                            {QStringLiteral("ports"), QStringLiteral("8080")}},
                &invalidType);
    QCOMPARE(invalidType.code, QStringLiteral("PODSTACK_INVALID_PARAMS"));

    PodStack::Error invalidMissing;
    client.call(QStringLiteral("machines.ports.set"),
                QJsonObject{{QStringLiteral("id"), meta.id}}, &invalidMissing);
    QCOMPARE(invalidMissing.code, QStringLiteral("PODSTACK_INVALID_PARAMS"));

    PodStack::Error invalidHostType;
    client.call(QStringLiteral("machines.ports.set"),
                QJsonObject{{QStringLiteral("id"), meta.id},
                            {QStringLiteral("ports"),
                             QJsonArray{QJsonObject{{QStringLiteral("hostPort"),
                                                     QStringLiteral("8080")},
                                                   {QStringLiteral("guestPort"), 80}}}}},
                &invalidHostType);
    QCOMPARE(invalidHostType.code, QStringLiteral("PODSTACK_INVALID_PARAMS"));

    PodStack::Error invalidHost;
    client.call(QStringLiteral("machines.ports.set"),
                QJsonObject{{QStringLiteral("id"), meta.id},
                            {QStringLiteral("ports"),
                             QJsonArray{QJsonObject{{QStringLiteral("hostPort"), 0},
                                                   {QStringLiteral("guestPort"), 80}}}}},
                &invalidHost);
    QCOMPARE(invalidHost.code, QStringLiteral("PODSTACK_INVALID_PARAMS"));

    const QJsonObject configured =
        client.call(QStringLiteral("machines.ports.set"),
                    QJsonObject{
                        {QStringLiteral("id"), meta.id},
                        {QStringLiteral("ports"),
                         QJsonArray{QJsonObject{{QStringLiteral("hostPort"), 18080},
                                               {QStringLiteral("guestPort"), 80},
                                               {QStringLiteral("protocol"),
                                                QStringLiteral("tcp")},
                                               {QStringLiteral("exposeLan"), false},
                                               {QStringLiteral("label"),
                                                QStringLiteral("web")}}}}},
                    &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    const QJsonArray configuredPorts = configured.value(QStringLiteral("ports")).toArray();
    QCOMPARE(configuredPorts.size(), 2);
    bool sawSsh = false;
    bool sawWeb = false;
    for (const QJsonValue &value : configuredPorts) {
        const QJsonObject port = value.toObject();
        if (port.value(QStringLiteral("guestPort")).toInt() == 22) {
            sawSsh = port.value(QStringLiteral("id")).toString() == QStringLiteral("ssh-rpc");
        } else if (port.value(QStringLiteral("hostPort")).toInt() == 18080) {
            sawWeb = port.value(QStringLiteral("guestPort")).toInt() == 80
                     && port.value(QStringLiteral("label")).toString() == QStringLiteral("web")
                     && !port.value(QStringLiteral("id")).toString().isEmpty();
        }
    }
    QVERIFY(sawSsh);
    QVERIFY(sawWeb);

    const QJsonObject listed =
        client.call(QStringLiteral("machines.ports.list"),
                    QJsonObject{{QStringLiteral("id"), meta.id}}, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QCOMPARE(listed.value(QStringLiteral("items")).toArray().size(), 2);

    // Enviar só as portas não-SSH mantém a SSH; alterar a SSH é recusado.
    const QJsonObject cleared =
        client.call(QStringLiteral("machines.ports.set"),
                    QJsonObject{{QStringLiteral("id"), meta.id},
                                {QStringLiteral("ports"), QJsonArray{}}},
                    &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QCOMPARE(cleared.value(QStringLiteral("ports")).toArray().size(), 1);
    QCOMPARE(cleared.value(QStringLiteral("ports"))
                 .toArray()
                 .at(0)
                 .toObject()
                 .value(QStringLiteral("id"))
                 .toString(),
             QStringLiteral("ssh-rpc"));

    PodStack::Error alteredSsh;
    client.call(QStringLiteral("machines.ports.set"),
                QJsonObject{{QStringLiteral("id"), meta.id},
                            {QStringLiteral("ports"),
                             QJsonArray{QJsonObject{{QStringLiteral("hostPort"), 32223},
                                                   {QStringLiteral("guestPort"), 22},
                                                   {QStringLiteral("protocol"),
                                                    QStringLiteral("tcp")}}}}},
                &alteredSsh);
    QCOMPARE(alteredSsh.code, QStringLiteral("PODSTACK_INVALID_PARAMS"));

    QVERIFY(store.remove(meta.id));
    QDir(m_testDataDir + QStringLiteral("/data/podstack/machines/") + meta.id)
        .removeRecursively();
}

void RpcTest::machineStats()
{
    PodStack::RpcClient client(m_controlSocket);
    PodStack::Error error;

    client.call(QStringLiteral("machines.stats"),
                QJsonObject{{QStringLiteral("id"), QStringLiteral("missing-machine")}},
                &error);
    QCOMPARE(error.code, QStringLiteral("PODSTACK_NOT_FOUND"));

    PodStack::MachineStore store(m_testDataDir + QStringLiteral("/data/podstack/podstack.db"));
    PodStack::Error storeError;
    QVERIFY2(store.open(&storeError), qPrintable(storeError.message));

    PodStack::MachineMeta meta;
    meta.id = QStringLiteral("machine-stats-1");
    meta.name = QStringLiteral("stats-demo");
    meta.distro = QStringLiteral("ubuntu");
    meta.distroVersion = QStringLiteral("24.04");
    meta.arch = QStringLiteral("amd64");
    meta.state = QStringLiteral("running");
    meta.sshUser = QStringLiteral("podstack");
    meta.sshPort = 32222;
    meta.sshKeyPath = m_testDataDir + QStringLiteral("/keys/stats-id_ed25519");
    meta.createdAt = QStringLiteral("2026-01-01T00:00:00.000Z");
    meta.updatedAt = meta.createdAt;
    QVERIFY2(store.upsert(meta, &storeError), qPrintable(storeError.message));

    QVERIFY(QDir().mkpath(QFileInfo(meta.sshKeyPath).absolutePath()));
    QFile key(meta.sshKeyPath);
    QVERIFY(key.open(QIODevice::WriteOnly | QIODevice::Truncate));
    key.write("FAKE-PRIVATE-KEY");
    key.close();
    QVERIFY(key.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner));

    error = {};
    const QJsonObject sample =
        client.call(QStringLiteral("machines.stats"),
                    QJsonObject{{QStringLiteral("id"), QStringLiteral("stats-demo")}}, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QVERIFY(sample.value(QStringLiteral("ts")).toInteger() > 0);
    QCOMPARE(sample.value(QStringLiteral("cpuPercent")).toDouble(), 0.0);
    QCOMPARE(sample.value(QStringLiteral("memUsed")).toInteger(), 1073741824LL);
    QCOMPARE(sample.value(QStringLiteral("memTotal")).toInteger(), 2147483648LL);
    QCOMPARE(sample.value(QStringLiteral("memPercent")).toDouble(), 50.0);
    QCOMPARE(sample.value(QStringLiteral("netRx")).toInteger(), 0);
    QCOMPARE(sample.value(QStringLiteral("netTx")).toInteger(), 0);
    QCOMPARE(sample.value(QStringLiteral("diskRead")).toInteger(), 0);
    QCOMPARE(sample.value(QStringLiteral("diskWrite")).toInteger(), 0);
    QCOMPARE(sample.value(QStringLiteral("diskUsed")).toInteger(), 2500000000LL);
    QCOMPARE(sample.value(QStringLiteral("diskTotal")).toInteger(), 10000000000LL);
    QCOMPARE(sample.value(QStringLiteral("diskPercent")).toDouble(), 25.0);

    QLocalSocket socket;
    socket.connectToServer(m_controlSocket, QIODevice::ReadWrite);
    QVERIFY(socket.waitForConnected(3000));
    QByteArray buffer;
    sendMessage(socket,
                {{QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
                 {QStringLiteral("id"), 1},
                 {QStringLiteral("method"), QStringLiteral("machines.stats.subscribe")},
                 {QStringLiteral("params"),
                  QJsonObject{{QStringLiteral("id"), meta.id},
                              {QStringLiteral("intervalSeconds"), 1}}}});

    QString subscriptionId;
    QJsonObject delta;
    for (int i = 0; i < 20 && delta.isEmpty(); ++i) {
        const QJsonObject message = readMessage(socket, buffer);
        if (message.isEmpty()) {
            break;
        }
        if (message.value(QStringLiteral("id")).toInt() == 1) {
            subscriptionId = message.value(QStringLiteral("result"))
                                 .toObject()
                                 .value(QStringLiteral("subscriptionId"))
                                 .toString();
        }
        if (message.value(QStringLiteral("method")).toString()
            == QStringLiteral("machines.stats.delta")) {
            delta = message.value(QStringLiteral("params")).toObject();
        }
    }
    QVERIFY(!subscriptionId.isEmpty());
    QVERIFY2(!delta.isEmpty(), "nenhum machines.stats.delta recebido");
    QCOMPARE(delta.value(QStringLiteral("subscriptionId")).toString(), subscriptionId);
    QCOMPARE(delta.value(QStringLiteral("id")).toString(), meta.id);
    QCOMPARE(delta.value(QStringLiteral("memTotal")).toInteger(), 2147483648LL);
    QCOMPARE(delta.value(QStringLiteral("diskPercent")).toDouble(), 25.0);

    sendMessage(socket,
                {{QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
                 {QStringLiteral("id"), 2},
                 {QStringLiteral("method"), QStringLiteral("machines.stats.unsubscribe")},
                 {QStringLiteral("params"),
                  QJsonObject{{QStringLiteral("subscriptionId"), subscriptionId}}}});
    QJsonObject unsubscribeResponse;
    for (int i = 0; i < 20 && unsubscribeResponse.isEmpty(); ++i) {
        const QJsonObject message = readMessage(socket, buffer);
        if (message.isEmpty()) {
            break;
        }
        if (message.value(QStringLiteral("id")).toInt() == 2) {
            unsubscribeResponse = message;
        }
    }
    QCOMPARE(unsubscribeResponse.value(QStringLiteral("result"))
                 .toObject()
                 .value(QStringLiteral("subscriptionId"))
                 .toString(),
             subscriptionId);

    QVERIFY(store.remove(meta.id));
    QFile::remove(meta.sshKeyPath);
    QDir().rmdir(QFileInfo(meta.sshKeyPath).absolutePath());
}

void RpcTest::composeAndBuilds()
{
    PodStack::RpcClient client(m_controlSocket);
    PodStack::Error error;

    const QJsonObject projects =
        client.call(QStringLiteral("compose.projects.list"), {}, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    const QJsonArray projectItems = projects.value(QStringLiteral("items")).toArray();
    QCOMPARE(projectItems.size(), 1);
    QCOMPARE(projectItems.first().toObject().value(QStringLiteral("name")).toString(),
             QStringLiteral("mock-project"));

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

    const QJsonObject up =
        client.call(QStringLiteral("compose.up"),
                    QJsonObject{{QStringLiteral("projectPath"), projectDir}},
                    &error, 60000);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QVERIFY(up.value(QStringLiteral("ok")).toBool());

    QVERIFY(client.call(QStringLiteral("compose.restart"),
                        QJsonObject{{QStringLiteral("projectPath"), projectDir}},
                        &error, 60000)
                .value(QStringLiteral("ok"))
                .toBool());
    QVERIFY(client.call(QStringLiteral("compose.stop"),
                        QJsonObject{{QStringLiteral("projectPath"), projectDir}},
                        &error, 60000)
                .value(QStringLiteral("ok"))
                .toBool());
    QVERIFY(client.call(QStringLiteral("compose.start"),
                        QJsonObject{{QStringLiteral("projectPath"), projectDir}},
                        &error, 60000)
                .value(QStringLiteral("ok"))
                .toBool());

    PodStack::Error invalidProject;
    client.call(QStringLiteral("compose.up"),
                QJsonObject{{QStringLiteral("projectPath"),
                             QStringLiteral("/nonexistent-project")}},
                &invalidProject);
    QCOMPARE(invalidProject.code, QStringLiteral("PODSTACK_INVALID_PARAMS"));

    PodStack::Error invalidBuild;
    client.call(QStringLiteral("builds.start"),
                QJsonObject{{QStringLiteral("contextPath"), projectDir}}, &invalidBuild);
    QCOMPARE(invalidBuild.code, QStringLiteral("PODSTACK_INVALID_PARAMS"));

    QLocalSocket logsSocket;
    logsSocket.connectToServer(m_controlSocket, QIODevice::ReadWrite);
    QVERIFY(logsSocket.waitForConnected(5000));
    QByteArray logsBuffer;
    sendMessage(logsSocket,
                {{QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
                 {QStringLiteral("id"), 1},
                 {QStringLiteral("method"), QStringLiteral("compose.logs.subscribe")},
                 {QStringLiteral("params"),
                  QJsonObject{{QStringLiteral("project"), QStringLiteral("mock-project")},
                              {QStringLiteral("service"), QStringLiteral("web")}}}});
    const QJsonObject logsResponse = readMessage(logsSocket, logsBuffer);
    QVERIFY(!logsResponse.value(QStringLiteral("result"))
                 .toObject()
                 .value(QStringLiteral("subscriptionId"))
                 .toString()
                 .isEmpty());
    int logDeltas = 0;
    for (int i = 0; i < 20 && logDeltas < 2; ++i) {
        const QJsonObject message = readMessage(logsSocket, logsBuffer);
        if (message.isEmpty()) {
            break;
        }
        if (message.value(QStringLiteral("method")).toString()
            == QStringLiteral("compose.logs.delta")) {
            ++logDeltas;
        }
    }
    QVERIFY(logDeltas >= 2);
    logsSocket.disconnectFromServer();

    QLocalSocket buildSocket;
    buildSocket.connectToServer(m_controlSocket, QIODevice::ReadWrite);
    QVERIFY(buildSocket.waitForConnected(5000));
    QByteArray buildBuffer;
    sendMessage(buildSocket,
                {{QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
                 {QStringLiteral("id"), 1},
                 {QStringLiteral("method"), QStringLiteral("builds.start")},
                 {QStringLiteral("params"),
                  QJsonObject{{QStringLiteral("contextPath"), projectDir},
                              {QStringLiteral("tag"), QStringLiteral("localhost/mock:1")},
                              {QStringLiteral("dockerfile"), QStringLiteral("Dockerfile")},
                              {QStringLiteral("platform"), QStringLiteral("linux/amd64")},
                              {QStringLiteral("buildArgs"),
                               QJsonObject{{QStringLiteral("A"), QStringLiteral("B")}}}}}});
    const QJsonObject buildResponse = readMessage(buildSocket, buildBuffer);
    const QString buildId = buildResponse.value(QStringLiteral("result"))
                                .toObject()
                                .value(QStringLiteral("buildId"))
                                .toString();
    QVERIFY(!buildId.isEmpty());

    int buildDeltas = 0;
    QString finalStatus;
    for (int i = 0; i < 30; ++i) {
        const QJsonObject message = readMessage(buildSocket, buildBuffer);
        if (message.isEmpty()) {
            break;
        }
        const QString method = message.value(QStringLiteral("method")).toString();
        if (method == QStringLiteral("builds.delta")) {
            ++buildDeltas;
        } else if (method == QStringLiteral("builds.done")) {
            finalStatus = message.value(QStringLiteral("params"))
                              .toObject()
                              .value(QStringLiteral("status"))
                              .toString();
            break;
        }
    }
    QVERIFY(buildDeltas >= 2);
    QCOMPARE(finalStatus, QStringLiteral("success"));
    buildSocket.disconnectFromServer();

    const QJsonObject list = client.call(QStringLiteral("builds.list"), {}, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QVERIFY(list.value(QStringLiteral("items")).toArray().size() >= 1);

    const QJsonObject build =
        client.call(QStringLiteral("builds.get"),
                    QJsonObject{{QStringLiteral("id"), buildId}}, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QCOMPARE(build.value(QStringLiteral("status")).toString(), QStringLiteral("success"));
    QCOMPARE(build.value(QStringLiteral("imageRef")).toString(),
             QStringLiteral("localhost/mock:1"));
    const QJsonArray singlePlatforms = build.value(QStringLiteral("platforms")).toArray();
    QCOMPARE(singlePlatforms.size(), 1);
    QCOMPARE(singlePlatforms.at(0).toString(), QStringLiteral("linux/amd64"));

    const QJsonObject removed =
        client.call(QStringLiteral("builds.remove"),
                    QJsonObject{{QStringLiteral("id"), buildId}}, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QVERIFY(removed.value(QStringLiteral("removed")).toBool());

    PodStack::Error cancelError;
    client.call(QStringLiteral("builds.cancel"),
                QJsonObject{{QStringLiteral("id"), QStringLiteral("missing")}},
                &cancelError);
    QCOMPARE(cancelError.code, QStringLiteral("PODSTACK_NOT_FOUND"));

    PodStack::Error missingBuild;
    client.call(QStringLiteral("builds.get"),
                QJsonObject{{QStringLiteral("id"), QStringLiteral("missing")}},
                &missingBuild);
    QCOMPARE(missingBuild.code, QStringLiteral("PODSTACK_NOT_FOUND"));

    QVERIFY(client.call(QStringLiteral("compose.down"),
                        QJsonObject{{QStringLiteral("projectPath"), projectDir}},
                        &error, 60000)
                .value(QStringLiteral("ok"))
                .toBool());

    PodStack::Error unknownService;
    client.call(QStringLiteral("compose.logs.subscribe"),
                QJsonObject{{QStringLiteral("project"), QStringLiteral("mock-project")},
                            {QStringLiteral("service"), QStringLiteral("missing")}},
                &unknownService);
    QCOMPARE(unknownService.code, QStringLiteral("PODSTACK_NOT_FOUND"));

    QLocalSocket failBuildSocket;
    failBuildSocket.connectToServer(m_controlSocket, QIODevice::ReadWrite);
    QVERIFY(failBuildSocket.waitForConnected(5000));
    QByteArray failBuildBuffer;
    sendMessage(failBuildSocket,
                {{QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
                 {QStringLiteral("id"), 1},
                 {QStringLiteral("method"), QStringLiteral("builds.start")},
                 {QStringLiteral("params"),
                  QJsonObject{{QStringLiteral("contextPath"),
                               QStringLiteral("/nonexistent-context")},
                              {QStringLiteral("tag"), QStringLiteral("localhost/mock:fail")}}}});
    const QJsonObject failBuildResponse = readMessage(failBuildSocket, failBuildBuffer);
    QVERIFY(!failBuildResponse.value(QStringLiteral("result"))
                 .toObject()
                 .value(QStringLiteral("buildId"))
                 .toString()
                 .isEmpty());
    QString failStatus;
    for (int i = 0; i < 30; ++i) {
        const QJsonObject message = readMessage(failBuildSocket, failBuildBuffer);
        if (message.isEmpty()) {
            break;
        }
        if (message.value(QStringLiteral("method")).toString()
            == QStringLiteral("builds.done")) {
            failStatus = message.value(QStringLiteral("params"))
                             .toObject()
                             .value(QStringLiteral("status"))
                             .toString();
            break;
        }
    }
    QCOMPARE(failStatus, QStringLiteral("failed"));
    failBuildSocket.disconnectFromServer();

    PodStack::Error emptyCompose;
    client.call(QStringLiteral("compose.up"), {}, &emptyCompose);
    QCOMPARE(emptyCompose.code, QStringLiteral("PODSTACK_INVALID_PARAMS"));

    const QString failDir = m_testDataDir + QStringLiteral("/fail-project");
    QDir().mkpath(failDir);
    PodStack::Error failCompose;
    client.call(QStringLiteral("compose.up"),
                QJsonObject{{QStringLiteral("projectPath"), failDir}}, &failCompose, 60000);
    QCOMPARE(failCompose.code, QStringLiteral("PODSTACK_COMPOSE_ERROR"));

    PodStack::Error emptyLogs;
    client.call(QStringLiteral("compose.logs.subscribe"), {}, &emptyLogs);
    QCOMPARE(emptyLogs.code, QStringLiteral("PODSTACK_INVALID_PARAMS"));

    PodStack::Error unknownLogs;
    client.call(QStringLiteral("compose.logs.subscribe"),
                QJsonObject{{QStringLiteral("project"), QStringLiteral("unknown")}},
                &unknownLogs);
    QCOMPARE(unknownLogs.code, QStringLiteral("PODSTACK_NOT_FOUND"));

    PodStack::Error emptyCancel;
    client.call(QStringLiteral("builds.cancel"), {}, &emptyCancel);
    QCOMPARE(emptyCancel.code, QStringLiteral("PODSTACK_INVALID_PARAMS"));

    PodStack::Error emptyGet;
    client.call(QStringLiteral("builds.get"), {}, &emptyGet);
    QCOMPARE(emptyGet.code, QStringLiteral("PODSTACK_INVALID_PARAMS"));

    PodStack::Error emptyRemove;
    client.call(QStringLiteral("builds.remove"), {}, &emptyRemove);
    QCOMPARE(emptyRemove.code, QStringLiteral("PODSTACK_INVALID_PARAMS"));
}

void RpcTest::multiPlatformBuild()
{
    const QString projectDir = m_testDataDir + QStringLiteral("/multi-project");
    QDir().mkpath(projectDir);
    QFile dockerfile(projectDir + QStringLiteral("/Dockerfile"));
    QVERIFY(dockerfile.open(QIODevice::WriteOnly | QIODevice::Truncate));
    dockerfile.write("FROM alpine:3.20\n");
    dockerfile.close();

    const QString argsLog = m_testDataDir + QStringLiteral("/podman-args.log");
    QFile::remove(argsLog);

    PodStack::RpcClient client(m_controlSocket);
    PodStack::Error error;
    const QJsonArray platforms{QStringLiteral("linux/amd64"), QStringLiteral("linux/arm64")};

    const auto waitDone = [this](QLocalSocket &socket, QByteArray &buffer, int *deltas) {
        QString status;
        for (int i = 0; i < 100; ++i) {
            const QJsonObject message = readMessage(socket, buffer, 5000);
            if (message.isEmpty()) {
                break;
            }
            const QString method = message.value(QStringLiteral("method")).toString();
            if (method == QStringLiteral("builds.delta")) {
                if (deltas) {
                    ++(*deltas);
                }
            } else if (method == QStringLiteral("builds.done")) {
                status = message.value(QStringLiteral("params"))
                             .toObject()
                             .value(QStringLiteral("status"))
                             .toString();
                break;
            }
        }
        return status;
    };

    // Sucesso: duas plataformas viram manifest list via CLI falso.
    QLocalSocket socket;
    socket.connectToServer(m_controlSocket, QIODevice::ReadWrite);
    QVERIFY(socket.waitForConnected(5000));
    QByteArray buffer;
    sendMessage(socket,
                {{QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
                 {QStringLiteral("id"), 1},
                 {QStringLiteral("method"), QStringLiteral("builds.start")},
                 {QStringLiteral("params"),
                  QJsonObject{{QStringLiteral("contextPath"), projectDir},
                              {QStringLiteral("tag"), QStringLiteral("localhost/mock:multi")},
                              {QStringLiteral("platforms"), platforms},
                              {QStringLiteral("buildArgs"),
                               QJsonObject{{QStringLiteral("A"), QStringLiteral("B")}}}}}});
    const QJsonObject response = readMessage(socket, buffer);
    const QString buildId = response.value(QStringLiteral("result"))
                                .toObject()
                                .value(QStringLiteral("buildId"))
                                .toString();
    QVERIFY(!buildId.isEmpty());
    int deltas = 0;
    QCOMPARE(waitDone(socket, buffer, &deltas), QStringLiteral("success"));
    QVERIFY(deltas >= 2);
    socket.disconnectFromServer();

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
    QVERIFY2(args.contains(projectDir), qPrintable(args));

    const QJsonObject build =
        client.call(QStringLiteral("builds.get"),
                    QJsonObject{{QStringLiteral("id"), buildId}}, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QCOMPARE(build.value(QStringLiteral("status")).toString(), QStringLiteral("success"));
    const QJsonArray storedPlatforms = build.value(QStringLiteral("platforms")).toArray();
    QCOMPARE(storedPlatforms.size(), 2);
    QCOMPARE(storedPlatforms.at(0).toString(), QStringLiteral("linux/amd64"));
    QCOMPARE(storedPlatforms.at(1).toString(), QStringLiteral("linux/arm64"));
    client.call(QStringLiteral("builds.remove"),
                QJsonObject{{QStringLiteral("id"), buildId}}, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));

    // Falha do CLI falso: o build termina como failed e mantém platforms.
    QLocalSocket failSocket;
    failSocket.connectToServer(m_controlSocket, QIODevice::ReadWrite);
    QVERIFY(failSocket.waitForConnected(5000));
    QByteArray failBuffer;
    sendMessage(failSocket,
                {{QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
                 {QStringLiteral("id"), 1},
                 {QStringLiteral("method"), QStringLiteral("builds.start")},
                 {QStringLiteral("params"),
                  QJsonObject{{QStringLiteral("contextPath"), projectDir},
                              {QStringLiteral("tag"), QStringLiteral("localhost/mock:fail-multi")},
                              {QStringLiteral("platforms"), platforms}}}});
    const QString failBuildId = readMessage(failSocket, failBuffer)
                                    .value(QStringLiteral("result"))
                                    .toObject()
                                    .value(QStringLiteral("buildId"))
                                    .toString();
    QVERIFY(!failBuildId.isEmpty());
    QCOMPARE(waitDone(failSocket, failBuffer, nullptr), QStringLiteral("failed"));
    failSocket.disconnectFromServer();

    const QJsonObject failedBuild =
        client.call(QStringLiteral("builds.get"),
                    QJsonObject{{QStringLiteral("id"), failBuildId}}, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QCOMPARE(failedBuild.value(QStringLiteral("status")).toString(), QStringLiteral("failed"));
    QVERIFY(!failedBuild.value(QStringLiteral("error")).toString().isEmpty());
    const QJsonArray failedPlatforms =
        failedBuild.value(QStringLiteral("platforms")).toArray();
    QCOMPARE(failedPlatforms.size(), 2);
    QCOMPARE(failedPlatforms.at(0).toString(), QStringLiteral("linux/amd64"));
    QCOMPARE(failedPlatforms.at(1).toString(), QStringLiteral("linux/arm64"));
    client.call(QStringLiteral("builds.remove"),
                QJsonObject{{QStringLiteral("id"), failBuildId}}, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));

    // Cancelamento: builds.cancel sinaliza a subscription e mata o CLI lento.
    QLocalSocket cancelSocket;
    cancelSocket.connectToServer(m_controlSocket, QIODevice::ReadWrite);
    QVERIFY(cancelSocket.waitForConnected(5000));
    QByteArray cancelBuffer;
    sendMessage(cancelSocket,
                {{QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
                 {QStringLiteral("id"), 1},
                 {QStringLiteral("method"), QStringLiteral("builds.start")},
                 {QStringLiteral("params"),
                  QJsonObject{{QStringLiteral("contextPath"), projectDir},
                              {QStringLiteral("tag"), QStringLiteral("localhost/mock:slow")},
                              {QStringLiteral("platform"),
                               QStringLiteral("linux/amd64,linux/arm64")}}}});
    const QString cancelBuildId = readMessage(cancelSocket, cancelBuffer)
                                      .value(QStringLiteral("result"))
                                      .toObject()
                                      .value(QStringLiteral("buildId"))
                                      .toString();
    QVERIFY(!cancelBuildId.isEmpty());
    QTest::qWait(150);
    const QJsonObject cancelled =
        client.call(QStringLiteral("builds.cancel"),
                    QJsonObject{{QStringLiteral("id"), cancelBuildId}}, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QVERIFY(cancelled.value(QStringLiteral("cancelled")).toBool());
    QCOMPARE(waitDone(cancelSocket, cancelBuffer, nullptr), QStringLiteral("cancelled"));
    cancelSocket.disconnectFromServer();

    const QJsonObject cancelledBuild =
        client.call(QStringLiteral("builds.get"),
                    QJsonObject{{QStringLiteral("id"), cancelBuildId}}, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QCOMPARE(cancelledBuild.value(QStringLiteral("status")).toString(),
             QStringLiteral("cancelled"));
    const QJsonArray cancelledPlatforms =
        cancelledBuild.value(QStringLiteral("platforms")).toArray();
    QCOMPARE(cancelledPlatforms.size(), 2);
    QCOMPARE(cancelledPlatforms.at(0).toString(), QStringLiteral("linux/amd64"));
    QCOMPARE(cancelledPlatforms.at(1).toString(), QStringLiteral("linux/arm64"));
    client.call(QStringLiteral("builds.remove"),
                QJsonObject{{QStringLiteral("id"), cancelBuildId}}, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));

    PodStack::Error invalidPlatform;
    client.call(QStringLiteral("builds.start"),
                QJsonObject{{QStringLiteral("contextPath"), projectDir},
                            {QStringLiteral("tag"), QStringLiteral("localhost/mock:invalid")},
                            {QStringLiteral("platforms"),
                             QJsonArray{QStringLiteral("linux/amd64/extra/v8/bad")}}},
                &invalidPlatform);
    QCOMPARE(invalidPlatform.code, QStringLiteral("PODSTACK_INVALID_PARAMS"));

    QJsonArray tooMany;
    for (int i = 0; i < 9; ++i) {
        tooMany.append(QStringLiteral("linux/arch%1").arg(i));
    }
    PodStack::Error tooManyError;
    client.call(QStringLiteral("builds.start"),
                QJsonObject{{QStringLiteral("contextPath"), projectDir},
                            {QStringLiteral("tag"), QStringLiteral("localhost/mock:too-many")},
                            {QStringLiteral("platforms"), tooMany}},
                &tooManyError);
    QCOMPARE(tooManyError.code, QStringLiteral("PODSTACK_INVALID_PARAMS"));
}

void RpcTest::setupFlow()
{
    PodStack::RpcClient client(m_controlSocket);
    PodStack::Error error;

    const QJsonObject status = client.call(QStringLiteral("setup.status"), {}, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QVERIFY(status.contains(QStringLiteral("completed")));
    QVERIFY(status.contains(QStringLiteral("showOnboarding")));
    QVERIFY(status.contains(QStringLiteral("summary")));
    QVERIFY(status.value(QStringLiteral("checks")).toArray().size() >= 8);

    const QJsonObject check = client.call(QStringLiteral("setup.check"), {}, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    const QJsonArray checks = check.value(QStringLiteral("checks")).toArray();
    QVERIFY(checks.size() >= 8);
    QStringList ids;
    for (const QJsonValue &value : checks) {
        const QJsonObject object = value.toObject();
        ids.append(object.value(QStringLiteral("id")).toString());
        QVERIFY(object.contains(QStringLiteral("status")));
        QVERIFY(object.contains(QStringLiteral("detail")));
    }
    QVERIFY(ids.contains(QStringLiteral("podman")));
    QVERIFY(ids.contains(QStringLiteral("docker")));
    QVERIFY(ids.contains(QStringLiteral("kvm")));
    QVERIFY(ids.contains(QStringLiteral("qemu")));
    QVERIFY(ids.contains(QStringLiteral("sqlite")));

    const QJsonObject validate = client.call(QStringLiteral("setup.validate"), {}, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QVERIFY(validate.contains(QStringLiteral("ok")));
    QVERIFY(validate.contains(QStringLiteral("warnings")));

    QJsonObject invalidAction;
    invalidAction.insert(QStringLiteral("action"), QStringLiteral("nope"));
    (void)client.call(QStringLiteral("setup.apply"), invalidAction, &error);
    QCOMPARE(error.code, QStringLiteral("PODSTACK_INVALID_PARAMS"));

    error = {};
    (void)client.call(QStringLiteral("setup.removeDocker"), {}, &error);
    QCOMPARE(error.code, QStringLiteral("PODSTACK_CONFIRMATION_REQUIRED"));

    error = {};
    QJsonObject skipped;
    skipped.insert(QStringLiteral("skipped"), true);
    const QJsonObject skippedResult =
        client.call(QStringLiteral("setup.complete"), skipped, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QCOMPARE(skippedResult.value(QStringLiteral("skipped")).toBool(), true);

    const QJsonObject afterSkip = client.call(QStringLiteral("setup.status"), {}, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QCOMPARE(afterSkip.value(QStringLiteral("completed")).toBool(), false);
    QCOMPARE(afterSkip.value(QStringLiteral("showOnboarding")).toBool(), false);

    QJsonObject completed;
    completed.insert(QStringLiteral("skipped"), false);
    const QJsonObject completedResult =
        client.call(QStringLiteral("setup.complete"), completed, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QCOMPARE(completedResult.value(QStringLiteral("completed")).toBool(), true);

    const QJsonObject afterComplete = client.call(QStringLiteral("setup.status"), {}, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QCOMPARE(afterComplete.value(QStringLiteral("completed")).toBool(), true);
    QVERIFY(!afterComplete.value(QStringLiteral("completedAt")).isNull());
}

void RpcTest::invalidRequests()
{
    QLocalSocket socket;
    socket.connectToServer(m_controlSocket, QIODevice::ReadWrite);
    QVERIFY(socket.waitForConnected(3000));

    QByteArray buffer;
    socket.write("this is not json\n");
    socket.flush();
    QJsonObject response = readMessage(socket, buffer);
    QCOMPARE(response.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toInt(),
             -32700);

    socket.write("{\"jsonrpc\":\"2.0\",\"id\":7}\n");
    socket.flush();
    response = readMessage(socket, buffer);
    QCOMPARE(response.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toInt(),
             -32600);
}

QJsonObject RpcTest::readMessage(QLocalSocket &socket, QByteArray &buffer, int timeoutMs)
{
    QElapsedTimer timer;
    timer.start();

    while (true) {
        const int newline = buffer.indexOf('\n');
        if (newline >= 0) {
            const QByteArray line = buffer.left(newline);
            buffer.remove(0, newline + 1);
            if (line.trimmed().isEmpty()) {
                continue;
            }
            return QJsonDocument::fromJson(line).object();
        }

        const int remaining = timeoutMs - static_cast<int>(timer.elapsed());
        if (remaining <= 0 || !socket.waitForReadyRead(remaining)) {
            return {};
        }
        buffer.append(socket.readAll());
    }
}

void RpcTest::sendMessage(QLocalSocket &socket, const QJsonObject &message)
{
    socket.write(QJsonDocument(message).toJson(QJsonDocument::Compact) + '\n');
    socket.flush();
}

} // namespace

QTEST_GUILESS_MAIN(RpcTest)
#include "test_rpc.moc"
