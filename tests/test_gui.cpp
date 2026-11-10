#include "MockPodmanServer.h"

#include "core/Config.h"
#include "gui/Autostart.h"
#include "gui/Backend.h"
#include "gui/ContainerListModel.h"
#include "gui/StreamClient.h"
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
#include <QScopeGuard>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QStringList>
#include <QTest>
#include <QVariantList>
#include <QVariantMap>

namespace {

class GuiTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void containerListModel();
    void backendSuccess();
    void backendError();
    void backendActions();
    void backendExposeLan();
    void backendStreams();
    void backendContainerDetail();
    void backendCatalog();
    void backendKubernetes();
    void backendMachines();
    void backendMachineStats();
    void backendMachineConsole();
    void backendMachineFiles();
    void backendTerminal();
    void backendM3();
    void backendErrors();
    void backendInvalidArguments();
    void backendOperations();
    void backendBatchActions();
    void backendTheme();
    void backendSettings();
    void backendProxy();
    void backendSetup();
    void backendSetupJob();
    void backendTray();
    void backendActivityMonitor();
    void streamClientProtocol();
    void guiStartsAndQuits();

private:
    QProcessEnvironment daemonEnvironment() const;

    MockPodmanServer m_mock;
    QProcess m_daemon;
    QString m_controlSocket;
    QString m_testDataDir;
    QString m_fakeProvider;
    QString m_fakePodman;
    QString m_fakeSsh;
    QString m_fakeScp;
};

QProcessEnvironment GuiTest::daemonEnvironment() const
{
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("PODSTACK_CONTROL_SOCKET"), m_controlSocket);
    env.insert(QStringLiteral("PODSTACK_PODMAN_SOCKET"), m_mock.socketPath());
    env.insert(QStringLiteral("PODSTACK_COMPOSE_PROVIDER"), m_fakeProvider);
    env.insert(QStringLiteral("PODSTACK_PODMAN_CLI"), m_fakePodman);
    env.insert(QStringLiteral("PODSTACK_SSH_BINARY"), m_fakeSsh);
    env.insert(QStringLiteral("PODSTACK_SCP_BINARY"), m_fakeScp);
    // Garante que o helper esteja indisponível mesmo em hosts com o serviço instalado.
    env.insert(QStringLiteral("DBUS_SYSTEM_BUS_ADDRESS"),
               QStringLiteral("unix:path=/tmp/podstack-gui-no-system-bus.sock"));
    env.insert(QStringLiteral("PODSTACK_SSH_CONFIG"),
               m_testDataDir + QStringLiteral("/ssh/config"));
    env.insert(QStringLiteral("XDG_DATA_HOME"), m_testDataDir + QStringLiteral("/data"));
    env.insert(QStringLiteral("XDG_STATE_HOME"), m_testDataDir + QStringLiteral("/state"));
    env.insert(QStringLiteral("XDG_CONFIG_HOME"), m_testDataDir + QStringLiteral("/config"));
    return env;
}

void GuiTest::initTestCase()
{
    QVERIFY(m_mock.isListening());
    QDir().mkpath(QStringLiteral("/tmp/mock-project"));
    QFile mockCompose(QStringLiteral("/tmp/mock-project/compose.yaml"));
    if (mockCompose.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        mockCompose.write("services: {}\n");
        mockCompose.close();
    }

    m_testDataDir = QDir::tempPath()
                    + QStringLiteral("/podstack-gui-data-%1")
                          .arg(QCoreApplication::applicationPid());
    QDir(m_testDataDir).removeRecursively();
    QDir().mkpath(m_testDataDir);
    QDir().mkpath(m_testDataDir + QStringLiteral("/config"));
    qputenv("XDG_CONFIG_HOME", (m_testDataDir + QStringLiteral("/config")).toUtf8());
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

    m_fakeSsh = m_testDataDir + QStringLiteral("/ssh-fake.sh");
    QFile ssh(m_fakeSsh);
    QVERIFY(ssh.open(QIODevice::WriteOnly | QIODevice::Truncate));
    const QString fakeSsh =
        QStringLiteral("#!/bin/sh\n"
                       "echo \"$@\" >> \"%1\"\n"
                       "case \"$*\" in\n"
                       "  *\"ls -la --time-style=long-iso\"*)\n"
                       "    echo \"total 4\"\n"
                       "    echo \"drwxr-xr-x 2 podstack podstack 4096 2026-11-09 10:00 docs\"\n"
                       "    echo \"-rw-r--r-- 1 podstack podstack 12 2026-11-09 10:05 gui.txt\"\n"
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
            .arg(m_testDataDir + QStringLiteral("/ssh-args.log"));
    ssh.write(fakeSsh.toUtf8());
    ssh.close();
    QVERIFY(ssh.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner
                               | QFileDevice::ExeOwner));

    const QString downloadDir = m_testDataDir + QStringLiteral("/Downloads");
    QVERIFY(QDir().mkpath(downloadDir));
    QFile userDirs(m_testDataDir + QStringLiteral("/config/user-dirs.dirs"));
    QVERIFY(userDirs.open(QIODevice::WriteOnly | QIODevice::Truncate));
    userDirs.write(QStringLiteral("XDG_DOWNLOAD_DIR=\"%1\"\n").arg(downloadDir).toUtf8());
    userDirs.close();

    m_fakeScp = m_testDataDir + QStringLiteral("/scp-fake.sh");
    QFile scp(m_fakeScp);
    QVERIFY(scp.open(QIODevice::WriteOnly | QIODevice::Truncate));
    scp.write(QStringLiteral("#!/bin/sh\n"
                             "for last in \"$@\"; do :; done\n"
                             "case \"$last\" in\n"
                             "  \"%1\"/*) : > \"$last\";;\n"
                             "esac\n"
                             "exit 0\n")
                  .arg(downloadDir)
                  .toUtf8());
    scp.close();
    QVERIFY(scp.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner
                               | QFileDevice::ExeOwner));

    m_controlSocket = QDir::tempPath()
                      + QStringLiteral("/podstack-gui-%1.sock")
                            .arg(QCoreApplication::applicationPid());
    QFile::remove(m_controlSocket);

    const QString daemonPath =
        QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("../podstackd"));
    m_daemon.setProcessEnvironment(daemonEnvironment());
    m_daemon.setProcessChannelMode(QProcess::ForwardedChannels);
    m_daemon.start(daemonPath, {QStringLiteral("--socket"), m_controlSocket});
    QVERIFY2(m_daemon.waitForStarted(5000), qPrintable(m_daemon.errorString()));

    for (int attempt = 0; attempt < 100 && !QFileInfo::exists(m_controlSocket); ++attempt) {
        QTest::qWait(20);
    }
    QVERIFY(QFileInfo::exists(m_controlSocket));

    qputenv("PODSTACK_CONTROL_SOCKET", m_controlSocket.toUtf8());
}

void GuiTest::cleanupTestCase()
{
    qunsetenv("PODSTACK_CONTROL_SOCKET");
    m_daemon.terminate();
    if (!m_daemon.waitForFinished(3000)) {
        m_daemon.kill();
        m_daemon.waitForFinished(1000);
    }
    QFile::remove(m_controlSocket);
    QDir(m_testDataDir).removeRecursively();
    QDir(QStringLiteral("/tmp/mock-project")).removeRecursively();
}

void GuiTest::containerListModel()
{
    PodStack::ContainerListModel model;
    QSignalSpy countSpy(&model, &PodStack::ContainerListModel::countChanged);

    QVariantMap item;
    item.insert(QStringLiteral("name"), QStringLiteral("demo"));
    item.insert(QStringLiteral("state"), QStringLiteral("running"));
    item.insert(QStringLiteral("image"), QStringLiteral("alpine:3.20"));
    item.insert(QStringLiteral("ports"), QStringLiteral("8080->80/tcp"));
    item.insert(QStringLiteral("id"), QStringLiteral("abc123"));
    item.insert(QStringLiteral("health"), QStringLiteral("healthy"));
    item.insert(QStringLiteral("status"), QStringLiteral("Up 1 minute (healthy)"));

    model.setContainers({item});
    QCOMPARE(model.count(), 1);
    QCOMPARE(countSpy.count(), 1);
    QCOMPARE(model.rowCount(), 1);
    QCOMPARE(model.rowCount(model.index(0, 0)), 0);
    QCOMPARE(model.roleNames().size(), 7);

    const QModelIndex index = model.index(0, 0);
    QCOMPARE(model.data(index, PodStack::ContainerListModel::NameRole).toString(),
             QStringLiteral("demo"));
    QCOMPARE(model.data(index, PodStack::ContainerListModel::StateRole).toString(),
             QStringLiteral("running"));
    QCOMPARE(model.data(index, PodStack::ContainerListModel::ImageRole).toString(),
             QStringLiteral("alpine:3.20"));
    QCOMPARE(model.data(index, PodStack::ContainerListModel::PortsRole).toString(),
             QStringLiteral("8080->80/tcp"));
    QCOMPARE(model.data(index, PodStack::ContainerListModel::IdRole).toString(),
             QStringLiteral("abc123"));
    QCOMPARE(model.data(index, PodStack::ContainerListModel::HealthRole).toString(),
             QStringLiteral("healthy"));
    QCOMPARE(model.data(index, PodStack::ContainerListModel::StatusRole).toString(),
             QStringLiteral("Up 1 minute (healthy)"));
    QVERIFY(model.data(index, Qt::DisplayRole).isNull());
    QVERIFY(model.data(QModelIndex(), PodStack::ContainerListModel::NameRole).isNull());

    QCOMPARE(model.get(0).value(QStringLiteral("name")).toString(), QStringLiteral("demo"));
    QVERIFY(model.get(-1).isEmpty());
    QVERIFY(model.get(99).isEmpty());

    model.setContainers({});
    QCOMPARE(model.count(), 0);
    QCOMPARE(countSpy.count(), 2);
}

void GuiTest::backendSuccess()
{
    PodStack::Backend backend;
    QSignalSpy containersSpy(&backend, &PodStack::Backend::containersChanged);
    QSignalSpy stateSpy(&backend, &PodStack::Backend::stateChanged);

    QTRY_VERIFY_WITH_TIMEOUT(containersSpy.count() > 0, 5000);
    QVERIFY(backend.connected());
    QVERIFY(backend.error().isEmpty());
    QCOMPARE(backend.version(), QStringLiteral(PODSTACK_VERSION));

    const QVariantList items = containersSpy.last().at(0).value<QVariantList>();
    QCOMPARE(items.size(), 1);
}

void GuiTest::backendError()
{
    const QByteArray previous = qgetenv("PODSTACK_CONTROL_SOCKET");
    const QString missing = QDir::tempPath()
                            + QStringLiteral("/podstack-gui-missing-%1.sock")
                                  .arg(QCoreApplication::applicationPid());
    QFile::remove(missing);
    qputenv("PODSTACK_CONTROL_SOCKET", missing.toUtf8());

    const auto restore = qScopeGuard([&] {
        if (previous.isEmpty()) {
            qunsetenv("PODSTACK_CONTROL_SOCKET");
        } else {
            qputenv("PODSTACK_CONTROL_SOCKET", previous);
        }
    });

    PodStack::Backend backend;
    QSignalSpy stateSpy(&backend, &PodStack::Backend::stateChanged);
    QTRY_VERIFY_WITH_TIMEOUT(stateSpy.count() > 0, 5000);
    QVERIFY(!backend.connected());
    QVERIFY(!backend.error().isEmpty());
}

void GuiTest::backendActions()
{
    PodStack::Backend backend;
    QSignalSpy actionSpy(&backend, &PodStack::Backend::actionFinished);

    backend.lifecycle(QStringLiteral("pause"), QStringLiteral("abc123"));
    QTRY_VERIFY_WITH_TIMEOUT(actionSpy.count() > 0, 5000);
    QCOMPARE(actionSpy.last().at(0).toBool(), true);
    QVERIFY(backend.lastError().isEmpty());

    backend.removeContainer(QStringLiteral("abc123"), true, false);
    QTRY_VERIFY_WITH_TIMEOUT(actionSpy.count() > 1, 5000);
    QCOMPARE(actionSpy.last().at(0).toBool(), true);

    backend.lifecycle(QStringLiteral("does-not-exist"), QStringLiteral("abc123"));
    QTRY_VERIFY_WITH_TIMEOUT(actionSpy.count() > 2, 5000);
    QCOMPARE(actionSpy.last().at(0).toBool(), false);
    QVERIFY(!backend.lastError().isEmpty());

    backend.lifecycle(QString(), QStringLiteral("abc123"));
    QCOMPARE(actionSpy.last().at(0).toBool(), false);

    QVariantMap spec;
    spec.insert(QStringLiteral("image"), QStringLiteral("alpine:3.20"));
    spec.insert(QStringLiteral("name"), QStringLiteral("created"));
    spec.insert(QStringLiteral("portsText"), QStringLiteral("8080:80"));
    spec.insert(QStringLiteral("envText"), QStringLiteral("A=B"));
    spec.insert(QStringLiteral("mountsText"), QStringLiteral("/tmp:/mnt"));
    backend.createContainer(spec, true);
    QTRY_VERIFY_WITH_TIMEOUT(actionSpy.count() > 4, 5000);
    QCOMPARE(actionSpy.last().at(0).toBool(), true);

    QSignalSpy containersSpy(&backend, &PodStack::Backend::containersChanged);
    backend.refresh();
    QTRY_VERIFY_WITH_TIMEOUT(containersSpy.count() > 0, 5000);

    backend.batchAll(QStringLiteral("start"));
    QTRY_VERIFY_WITH_TIMEOUT(actionSpy.count() > 5, 5000);
    QCOMPARE(actionSpy.last().at(0).toBool(), true);

    backend.batchAll(QStringLiteral("invalid"));
    QCOMPARE(actionSpy.last().at(0).toBool(), false);

    backend.renameContainer(QStringLiteral("abc123"), QStringLiteral("renamed"));
    QTRY_VERIFY_WITH_TIMEOUT(actionSpy.count() > 7, 5000);
    QCOMPARE(actionSpy.last().at(0).toBool(), true);

    backend.duplicateContainer(QStringLiteral("abc123"));
    QTRY_VERIFY_WITH_TIMEOUT(actionSpy.count() > 8, 5000);
    QCOMPARE(actionSpy.last().at(0).toBool(), true);

    backend.batchIds({QStringLiteral("abc123")}, QStringLiteral("start"));
    QTRY_VERIFY_WITH_TIMEOUT(actionSpy.count() > 9, 5000);
    QCOMPARE(actionSpy.last().at(0).toBool(), true);

    const int before = actionSpy.count();
    backend.batchIds({}, QStringLiteral("start"));
    QTest::qWait(100);
    QCOMPARE(actionSpy.count(), before);
}

void GuiTest::backendExposeLan()
{
    PodStack::Backend backend;
    QSignalSpy actionSpy(&backend, &PodStack::Backend::actionFinished);
    const QVariantMap settingsBefore = backend.settings();

    QVariantMap spec;
    spec.insert(QStringLiteral("image"), QStringLiteral("alpine:3.20"));
    spec.insert(QStringLiteral("name"), QStringLiteral("lan"));
    spec.insert(QStringLiteral("portsText"), QStringLiteral("8080:80"));
    spec.insert(QStringLiteral("exposeLan"), true);

    const int createsBefore = m_mock.createCount();
    backend.createContainer(spec, true);
    QTRY_VERIFY_WITH_TIMEOUT(actionSpy.count() > 0, 5000);
    QCOMPARE(actionSpy.last().at(0).toBool(), false);
    QVERIFY(!backend.lastError().isEmpty());
    QCOMPARE(m_mock.createCount(), createsBefore);

    backend.exposeContainerLan(QStringLiteral("abc123"), true);
    QTRY_VERIFY_WITH_TIMEOUT(actionSpy.count() > 1, 5000);
    QCOMPARE(actionSpy.last().at(0).toBool(), false);
    QVERIFY(!backend.lastError().isEmpty());
    QVERIFY(backend.settings() == settingsBefore);

    QVariantMap plain;
    plain.insert(QStringLiteral("image"), QStringLiteral("alpine:3.20"));
    plain.insert(QStringLiteral("name"), QStringLiteral("local"));
    plain.insert(QStringLiteral("portsText"), QStringLiteral("8081:81"));
    backend.createContainer(plain, false);
    QTRY_VERIFY_WITH_TIMEOUT(actionSpy.count() > 2, 5000);
    QCOMPARE(actionSpy.last().at(0).toBool(), true);
    const QJsonObject body = QJsonDocument::fromJson(m_mock.lastCreateBody()).object();
    const QJsonArray mappings = body.value(QStringLiteral("portmappings")).toArray();
    QCOMPARE(mappings.size(), 1);
    QCOMPARE(mappings.first().toObject().value(QStringLiteral("host_ip")).toString(),
             QStringLiteral("127.0.0.1"));
}

void GuiTest::backendStreams()
{
    PodStack::Backend backend;

    backend.subscribeLogs(QStringLiteral("abc123"), 10, false);
    QTRY_VERIFY_WITH_TIMEOUT(backend.logLines().size() >= 2, 5000);
    backend.unsubscribeLogs();

    backend.subscribeStats(QStringLiteral("abc123"));
    QTRY_VERIFY_WITH_TIMEOUT(
        backend.stats().value(QStringLiteral("cpu_percent")).toString().contains(
            QStringLiteral("1.5")),
        5000);
    QCOMPARE(backend.cpuHistory().size(), 1);
    QCOMPARE(backend.memHistory().size(), 1);
    backend.unsubscribeStats();

    QSignalSpy filesSpy(&backend, &PodStack::Backend::filesChanged);
    backend.listFiles(QStringLiteral("abc123"), QStringLiteral("/"));
    QTRY_VERIFY_WITH_TIMEOUT(filesSpy.count() > 0, 5000);
    QCOMPARE(backend.files().size(), 2);
}

void GuiTest::backendContainerDetail()
{
    PodStack::Backend backend;
    QSignalSpy detailSpy(&backend, &PodStack::Backend::containerDetailChanged);

    backend.loadContainerDetail(QStringLiteral("abc123"));
    QVERIFY(backend.containerDetailLoading());
    QTRY_VERIFY_WITH_TIMEOUT(!backend.containerDetailLoading(), 5000);
    QVERIFY2(backend.containerDetailError().isEmpty(),
             qPrintable(backend.containerDetailError()));
    QVERIFY(detailSpy.count() > 0);

    const QVariantMap detail = backend.containerDetail();
    QCOMPARE(detail.value(QStringLiteral("Id")).toString(), QStringLiteral("abc123"));
    const QVariantMap config = detail.value(QStringLiteral("Config")).toMap();
    const QVariantList env = config.value(QStringLiteral("Env")).toList();
    QCOMPARE(env.size(), 3);
    QCOMPARE(env.first().toString(), QStringLiteral("A=B"));
    QCOMPARE(env.at(1).toString(), QStringLiteral("TOKEN=super-secret"));

    const QVariantList mounts = detail.value(QStringLiteral("Mounts")).toList();
    QCOMPARE(mounts.size(), 2);
    QCOMPARE(mounts.first().toMap().value(QStringLiteral("Destination")).toString(),
             QStringLiteral("/mnt"));
    QCOMPARE(mounts.at(1).toMap().value(QStringLiteral("Name")).toString(),
             QStringLiteral("data"));

    const QVariantMap networks =
        detail.value(QStringLiteral("NetworkSettings"))
            .toMap()
            .value(QStringLiteral("Networks"))
            .toMap();
    QVERIFY(networks.contains(QStringLiteral("podman")));
    QCOMPARE(networks.value(QStringLiteral("podman"))
                 .toMap()
                 .value(QStringLiteral("Gateway"))
                 .toString(),
             QStringLiteral("10.0.0.1"));

    // Sem QGuiApplication (suíte guiless) o clipboard é nulo; a chamada não pode quebrar.
    backend.copyContainerInspect();

    backend.loadContainerDetail(QStringLiteral("missing123"));
    QTRY_VERIFY_WITH_TIMEOUT(!backend.containerDetailLoading(), 5000);
    QVERIFY(!backend.containerDetailError().isEmpty());

    backend.startContainerDetailStats(QStringLiteral("abc123"));
    QTRY_VERIFY_WITH_TIMEOUT(
        backend.containerDetailStats().value(QStringLiteral("cpu_percent")).toString().contains(
            QStringLiteral("1.5")),
        5000);
    QCOMPARE(backend.containerDetailCpuHistory().size(), 1);
    QCOMPARE(backend.containerDetailMemHistory().size(), 1);
    QCOMPARE(backend.containerDetailNetRxHistory().size(), 1);
    QCOMPARE(backend.containerDetailNetTxHistory().size(), 1);
    QCOMPARE(backend.containerDetailDiskReadHistory().size(), 1);
    QCOMPARE(backend.containerDetailDiskWriteHistory().size(), 1);
    QVERIFY(backend.containerDetailStats()
                .value(QStringLiteral("netRxBytes"))
                .toLongLong()
            > 0);
    QVERIFY(backend.containerDetailStatsError().isEmpty());
    backend.stopContainerDetailStats();

    QSignalSpy logsSpy(&backend, &PodStack::Backend::containerDetailLogsChanged);
    backend.startContainerDetailLogs(QStringLiteral("abc123"), 10, false);
    QTRY_VERIFY_WITH_TIMEOUT(backend.containerDetailLogLines().size() >= 2, 5000);
    QVERIFY(logsSpy.count() > 0);
    QVERIFY(backend.containerDetailLogsError().isEmpty());
    backend.clearContainerDetailLogs();
    QVERIFY(backend.containerDetailLogLines().isEmpty());
    backend.stopContainerDetailLogs();

    backend.containerDetailLifecycle(QStringLiteral("invalid"), QStringLiteral("abc123"));
    QVERIFY(!backend.lastError().isEmpty());
}

void GuiTest::backendKubernetes()
{
    PodStack::Backend backend;
    QSignalSpy spy(&backend, &PodStack::Backend::k8sChanged);

    backend.refreshK8s();
    QTRY_VERIFY_WITH_TIMEOUT(spy.count() > 0, 5000);
    QTRY_VERIFY_WITH_TIMEOUT(backend.k8sState() == QStringLiteral("disabled"), 5000);
    QVERIFY2(!backend.k8sEnabled(),
             qPrintable(backend.k8sState() + QStringLiteral(" | ") + backend.k8sError()));
    QVERIFY(backend.k8sPods().isEmpty());
    QVERIFY(backend.k8sServices().isEmpty());

    backend.startK8sWatch();
    backend.stopK8sWatch();
}

void GuiTest::backendCatalog()
{    PodStack::Backend backend;
    QSignalSpy imagesSpy(&backend, &PodStack::Backend::imagesChanged);
    QSignalSpy volumesSpy(&backend, &PodStack::Backend::volumesChanged);
    QSignalSpy networksSpy(&backend, &PodStack::Backend::networksChanged);
    QSignalSpy podsSpy(&backend, &PodStack::Backend::podsChanged);
    QSignalSpy actionSpy(&backend, &PodStack::Backend::actionFinished);
    QSignalSpy pullSpy(&backend, &PodStack::Backend::pullStateChanged);
    QSignalSpy diskSpy(&backend, &PodStack::Backend::diskUsageChanged);

    backend.refreshCatalog();
    QTRY_VERIFY_WITH_TIMEOUT(imagesSpy.count() > 0, 5000);
    QTRY_VERIFY_WITH_TIMEOUT(volumesSpy.count() > 0, 5000);
    QTRY_VERIFY_WITH_TIMEOUT(networksSpy.count() > 0, 5000);
    QTRY_VERIFY_WITH_TIMEOUT(podsSpy.count() > 0, 5000);
    QTRY_VERIFY_WITH_TIMEOUT(diskSpy.count() > 0, 5000);
    QVERIFY(backend.diskUsage().contains(QStringLiteral("images")));

    const QVariantMap preview = backend.prunePreview();
    QVERIFY(preview.contains(QStringLiteral("images")));
    QVERIFY(preview.contains(QStringLiteral("volumes")));
    QCOMPARE(preview.value(QStringLiteral("images"))
                 .toMap()
                 .value(QStringLiteral("count"))
                 .toInt(),
             1);
    QCOMPARE(preview.value(QStringLiteral("volumes"))
                 .toMap()
                 .value(QStringLiteral("count"))
                 .toInt(),
             1);
    QVERIFY(preview.value(QStringLiteral("totalReclaimableBytes")).toDouble() > 0);

    const int diskCount = diskSpy.count();
    backend.refreshDiskUsage();
    QTRY_VERIFY_WITH_TIMEOUT(diskSpy.count() > diskCount, 5000);
    QCOMPARE(backend.prunePreview()
                 .value(QStringLiteral("images"))
                 .toMap()
                 .value(QStringLiteral("count"))
                 .toInt(),
             1);

    QCOMPARE(backend.images().size(), 2);
    QCOMPARE(backend.volumes().size(), 1);
    QCOMPARE(backend.networks().size(), 1);
    QCOMPARE(backend.pods().size(), 1);
    QCOMPARE(backend.images().first().toMap().value(QStringLiteral("name")).toString(),
             QStringLiteral("docker.io/library/alpine:3.20"));

    backend.pullImage(QStringLiteral("alpine"), {});
    QTRY_VERIFY_WITH_TIMEOUT(!backend.pullActive() && pullSpy.count() > 0, 5000);
    QVERIFY(backend.pullStatus().contains(QStringLiteral("concluído")));

    backend.tagImage(QStringLiteral("img1234567890"), QStringLiteral("repo:v1"));
    QTRY_VERIFY_WITH_TIMEOUT(actionSpy.count() > 0, 5000);
    QCOMPARE(actionSpy.last().at(0).toBool(), true);

    backend.untagImage(QStringLiteral("img1234567890"), QStringLiteral("repo:v1"));
    QTRY_VERIFY_WITH_TIMEOUT(actionSpy.count() > 1, 5000);
    QCOMPARE(actionSpy.last().at(0).toBool(), true);

    backend.removeImage(QStringLiteral("img1234567890"), false);
    QTRY_VERIFY_WITH_TIMEOUT(actionSpy.count() > 2, 5000);
    QCOMPARE(actionSpy.last().at(0).toBool(), true);

    backend.pruneImages(false);
    QTRY_VERIFY_WITH_TIMEOUT(actionSpy.count() > 3, 5000);
    QCOMPARE(actionSpy.last().at(0).toBool(), true);

    backend.createVolume(QStringLiteral("vol2"), QStringLiteral("local"));
    QTRY_VERIFY_WITH_TIMEOUT(actionSpy.count() > 4, 5000);
    QCOMPARE(actionSpy.last().at(0).toBool(), true);

    backend.removeVolume(QStringLiteral("vol1"), false);
    QTRY_VERIFY_WITH_TIMEOUT(actionSpy.count() > 5, 5000);
    QCOMPARE(actionSpy.last().at(0).toBool(), true);

    backend.pruneVolumes();
    QTRY_VERIFY_WITH_TIMEOUT(actionSpy.count() > 6, 5000);
    QCOMPARE(actionSpy.last().at(0).toBool(), true);

    backend.createNetwork(QStringLiteral("mock-net"), QStringLiteral("10.90.0.0/24"), {});
    QTRY_VERIFY_WITH_TIMEOUT(actionSpy.count() > 7, 5000);
    QCOMPARE(actionSpy.last().at(0).toBool(), true);

    backend.connectNetwork(QStringLiteral("mock-net"), QStringLiteral("abc123"));
    QTRY_VERIFY_WITH_TIMEOUT(actionSpy.count() > 8, 5000);
    QCOMPARE(actionSpy.last().at(0).toBool(), true);

    backend.disconnectNetwork(QStringLiteral("mock-net"), QStringLiteral("abc123"));
    QTRY_VERIFY_WITH_TIMEOUT(actionSpy.count() > 9, 5000);
    QCOMPARE(actionSpy.last().at(0).toBool(), true);

    backend.removeNetwork(QStringLiteral("mock-net"), false);
    QTRY_VERIFY_WITH_TIMEOUT(actionSpy.count() > 10, 5000);
    QCOMPARE(actionSpy.last().at(0).toBool(), true);

    backend.createPod(QStringLiteral("demo-pod"));
    QTRY_VERIFY_WITH_TIMEOUT(actionSpy.count() > 11, 5000);
    QCOMPARE(actionSpy.last().at(0).toBool(), true);

    backend.podAction(QStringLiteral("start"), QStringLiteral("pod123"));
    QTRY_VERIFY_WITH_TIMEOUT(actionSpy.count() > 12, 5000);
    QCOMPARE(actionSpy.last().at(0).toBool(), true);

    backend.removePod(QStringLiteral("pod123"), true);
    QTRY_VERIFY_WITH_TIMEOUT(actionSpy.count() > 13, 5000);
    QCOMPARE(actionSpy.last().at(0).toBool(), true);

    const QString uploadPath = QDir::tempPath()
                               + QStringLiteral("/podstack-gui-upload-%1.txt")
                                     .arg(QCoreApplication::applicationPid());
    QFile uploadFile(uploadPath);
    QVERIFY(uploadFile.open(QIODevice::WriteOnly));
    uploadFile.write("gui-upload");
    uploadFile.close();
    backend.uploadFile(QStringLiteral("abc123"), QStringLiteral("/tmp"), uploadPath);
    QTRY_VERIFY_WITH_TIMEOUT(actionSpy.count() > 14, 5000);
    QCOMPARE(actionSpy.last().at(0).toBool(), true);
    QFile::remove(uploadPath);

    const QString hostDir = QStringLiteral("/tmp/podstack-host-src");
    QDir(hostDir).removeRecursively();
    QDir().mkpath(hostDir);
    backend.importHostPath(QStringLiteral("vol2"), hostDir);
    QTRY_VERIFY_WITH_TIMEOUT(actionSpy.count() > 15, 5000);
    QCOMPARE(actionSpy.last().at(0).toBool(), true);
    QDir(hostDir).removeRecursively();
}

void GuiTest::backendMachines()
{
    PodStack::Backend backend;
    QSignalSpy machinesSpy(&backend, &PodStack::Backend::machinesChanged);
    QSignalSpy errorSpy(&backend, &PodStack::Backend::lastErrorChanged);
    QSignalSpy actionSpy(&backend, &PodStack::Backend::actionFinished);

    QTRY_VERIFY_WITH_TIMEOUT(machinesSpy.count() > 0, 5000);
    QCOMPARE(backend.machines().size(), 0);

    QSignalSpy distrosSpy(&backend, &PodStack::Backend::machineDistrosChanged);
    backend.refreshMachineDistros();
    QTRY_VERIFY_WITH_TIMEOUT(distrosSpy.count() > 0, 5000);
    QVERIFY(backend.machineDistros().size() >= 3);
    QVERIFY(backend.machineDistros()
                .first()
                .toMap()
                .contains(QStringLiteral("label")));

    backend.loadMachineInfo(QStringLiteral("missing-machine"));
    QTRY_VERIFY_WITH_TIMEOUT(!backend.lastError().isEmpty(), 5000);

    int errorsBefore = errorSpy.count();
    backend.loadMachineLogs(QStringLiteral("missing-machine"), 10);
    QTRY_VERIFY_WITH_TIMEOUT(errorSpy.count() > errorsBefore, 5000);

    backend.machineLifecycle(QStringLiteral("start"), QStringLiteral("missing-machine"));
    QTRY_VERIFY_WITH_TIMEOUT(actionSpy.count() > 0, 5000);
    QCOMPARE(actionSpy.last().at(0).toBool(), false);

    backend.removeMachine(QStringLiteral("missing-machine"), true);
    QTRY_VERIFY_WITH_TIMEOUT(actionSpy.count() > 1, 5000);
    QCOMPARE(actionSpy.last().at(0).toBool(), false);

    backend.openMachineSsh(QString());
    QCOMPARE(backend.lastError(), QStringLiteral("Machine inválida."));
    backend.openMachineSsh(QStringLiteral("missing-machine"));
    QTRY_VERIFY_WITH_TIMEOUT(backend.lastError().contains(QStringLiteral("não encontrada")),
                             5000);

    backend.createMachine({});
    QVERIFY(!backend.machineCreateActive());
    QVERIFY(!backend.machineCreateError().isEmpty());

    PodStack::MachineStore store(m_testDataDir + QStringLiteral("/data/podstack/podstack.db"));
    PodStack::Error storeError;
    QVERIFY2(store.open(&storeError), qPrintable(storeError.message));

    PodStack::MachineMeta meta;
    meta.id = QStringLiteral("gui-machine-logs");
    meta.name = QStringLiteral("gui-logs");
    meta.distro = QStringLiteral("debian");
    meta.distroVersion = QStringLiteral("12");
    meta.arch = QStringLiteral("amd64");
    meta.cpus = 2;
    meta.memoryBytes = 2LL * 1024LL * 1024LL * 1024LL;
    meta.diskBytes = 20LL * 1024LL * 1024LL * 1024LL;
    meta.sshUser = QStringLiteral("podstack");
    meta.sshPort = 32222;
    meta.state = QStringLiteral("stopped");
    meta.forwardSshAgent = true;
    meta.createdAt = QStringLiteral("2026-01-01T00:00:00.000Z");
    meta.updatedAt = meta.createdAt;

    const QString machineDir =
        m_testDataDir + QStringLiteral("/data/podstack/machines/") + meta.id;
    QVERIFY(QDir().mkpath(machineDir));
    meta.sshKeyPath = machineDir + QStringLiteral("/id_ed25519");
    QFile keyFile(meta.sshKeyPath);
    QVERIFY(keyFile.open(QIODevice::WriteOnly | QIODevice::Truncate));
    keyFile.write("test-key\n");
    keyFile.close();
    QVERIFY(keyFile.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner));
    QVERIFY2(store.upsert(meta, &storeError), qPrintable(storeError.message));

    QFile console(machineDir + QStringLiteral("/console.log"));
    QVERIFY(console.open(QIODevice::WriteOnly | QIODevice::Truncate));
    console.write("gui boot 1\ngui boot 2\ngui boot 3\n");
    console.close();

    backend.refreshMachines();
    QTRY_COMPARE_WITH_TIMEOUT(backend.machines().size(), 1, 5000);
    const QVariantMap item = backend.machines().first().toMap();
    QCOMPARE(item.value(QStringLiteral("name")).toString(), QStringLiteral("gui-logs"));
    QCOMPARE(item.value(QStringLiteral("distroLabel")).toString(),
             QStringLiteral("debian:12"));
    QCOMPARE(item.value(QStringLiteral("ssh")).toString(),
             QStringLiteral("podstack@127.0.0.1:32222"));

    backend.loadMachineInfo(meta.id);
    QTRY_COMPARE_WITH_TIMEOUT(backend.machineInfo()
                                  .value(QStringLiteral("id"))
                                  .toString(),
                              meta.id, 5000);
    QCOMPARE(backend.machineInfo().value(QStringLiteral("state")).toString(),
             QStringLiteral("stopped"));

    backend.loadMachineLogs(meta.id, 2);
    QTRY_COMPARE_WITH_TIMEOUT(backend.machineLogLines().size(), 2, 5000);
    QCOMPARE(backend.machineLogLines().first(), QStringLiteral("gui boot 2"));
    QCOMPARE(backend.machineLogLines().last(), QStringLiteral("gui boot 3"));

    // Compatibilidade: o Backend continua emitindo `terminalRequested` (dock/
    // TerminalWindow); a GUI usa `MainWindow::openMachineDetail`, coberto em
    // `test_terminal::machineDetailDialog*`.
    QSignalSpy terminalSpy(&backend, &PodStack::Backend::terminalRequested);
    backend.openMachineSsh(meta.id);
    QTRY_COMPARE_WITH_TIMEOUT(terminalSpy.count(), 1, 5000);
    const QList<QVariant> request = terminalSpy.takeFirst();
    QCOMPARE(request.at(0).toString(), QStringLiteral("gui-logs"));
    QCOMPARE(request.at(1).toString(), QStringLiteral("ssh"));
    const QStringList sshArgs = request.at(2).toStringList();
    QVERIFY(sshArgs.contains(QStringLiteral("-A")));
    QVERIFY(sshArgs.contains(QStringLiteral("-i")));
    QVERIFY(sshArgs.contains(meta.sshKeyPath));
    QVERIFY(sshArgs.contains(QStringLiteral("-p")));
    QVERIFY(sshArgs.contains(QStringLiteral("32222")));
    QCOMPARE(sshArgs.last(), QStringLiteral("podstack@127.0.0.1"));

    // updateMachine aceita só o agent, sem CPU/memória/disco.
    const int actionsBeforeUpdate = actionSpy.count();
    backend.updateMachine(QVariantMap{{QStringLiteral("id"), meta.id},
                                      {QStringLiteral("forwardSshAgent"), false}});
    QTRY_VERIFY_WITH_TIMEOUT(actionSpy.count() > actionsBeforeUpdate, 5000);
    QVERIFY2(actionSpy.last().at(0).toBool(),
             qPrintable(actionSpy.last().at(1).toString()));
    backend.loadMachineInfo(meta.id);
    QTRY_VERIFY_WITH_TIMEOUT(
        !backend.machineInfo().value(QStringLiteral("forwardSshAgent")).toBool(), 5000);

    QVERIFY(store.remove(meta.id));
    QDir(machineDir).removeRecursively();
}

void GuiTest::backendMachineStats()
{
    PodStack::Backend backend;
    QSignalSpy statsSpy(&backend, &PodStack::Backend::machineStatsChanged);
    QSignalSpy errorSpy(&backend, &PodStack::Backend::lastErrorChanged);

    backend.startMachineStats(QString());
    QVERIFY(!backend.machineStatsError().isEmpty());
    QVERIFY(errorSpy.count() > 0);

    PodStack::MachineStore store(m_testDataDir + QStringLiteral("/data/podstack/podstack.db"));
    PodStack::Error storeError;
    QVERIFY2(store.open(&storeError), qPrintable(storeError.message));

    PodStack::MachineMeta meta;
    meta.id = QStringLiteral("gui-machine-stats");
    meta.name = QStringLiteral("gui-stats");
    meta.distro = QStringLiteral("debian");
    meta.distroVersion = QStringLiteral("12");
    meta.arch = QStringLiteral("amd64");
    meta.cpus = 2;
    meta.memoryBytes = 2LL * 1024LL * 1024LL * 1024LL;
    meta.diskBytes = 20LL * 1024LL * 1024LL * 1024LL;
    meta.sshUser = QStringLiteral("podstack");
    meta.sshPort = 32222;
    meta.state = QStringLiteral("running");
    meta.createdAt = QStringLiteral("2026-01-01T00:00:00.000Z");
    meta.updatedAt = meta.createdAt;

    const QString machineDir =
        m_testDataDir + QStringLiteral("/data/podstack/machines/") + meta.id;
    QVERIFY(QDir().mkpath(machineDir));
    meta.sshKeyPath = machineDir + QStringLiteral("/id_ed25519");
    QFile keyFile(meta.sshKeyPath);
    QVERIFY(keyFile.open(QIODevice::WriteOnly | QIODevice::Truncate));
    keyFile.write("test-key\n");
    keyFile.close();
    QVERIFY(keyFile.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner));
    QVERIFY2(store.upsert(meta, &storeError), qPrintable(storeError.message));

    backend.startMachineStats(meta.id);
    QTRY_VERIFY_WITH_TIMEOUT(
        backend.machineStats().value(QStringLiteral("memTotal")).toLongLong() > 0, 7000);
    QVERIFY(statsSpy.count() > 0);
    QCOMPARE(backend.machineStats().value(QStringLiteral("memTotal")).toLongLong(),
             2147483648LL);
    QCOMPARE(backend.machineStats().value(QStringLiteral("diskPercent")).toDouble(), 25.0);
    QCOMPARE(backend.machineCpuHistory().size(), 1);
    QCOMPARE(backend.machineMemHistory().size(), 1);
    QCOMPARE(backend.machineNetRxHistory().size(), 1);
    QCOMPARE(backend.machineNetTxHistory().size(), 1);
    QVERIFY(backend.machineStatsError().isEmpty());

    backend.stopMachineStats();
    const int samplesAfterStop = backend.machineCpuHistory().size();
    QTest::qWait(2300);
    QCOMPARE(backend.machineCpuHistory().size(), samplesAfterStop);

    // Erros do stream de stats da machine não vazam para o lastError global
    // (o inspector lateral de containers exibe backend.lastError).
    PodStack::Backend statsOnly;
    QSignalSpy statsOnlyErrorSpy(&statsOnly, &PodStack::Backend::lastErrorChanged);
    statsOnly.startMachineStats(QStringLiteral("missing-machine-stats"));
    QTRY_VERIFY_WITH_TIMEOUT(!statsOnly.machineStatsError().isEmpty(), 5000);
    QVERIFY(statsOnly.lastError().isEmpty());
    QCOMPARE(statsOnlyErrorSpy.count(), 0);
    statsOnly.stopMachineStats();
    QVERIFY(statsOnly.machineStatsError().isEmpty());

    QVERIFY(store.remove(meta.id));
    QDir(machineDir).removeRecursively();
}

void GuiTest::backendMachineConsole()
{
    PodStack::Backend backend;
    QSignalSpy consoleSpy(&backend, &PodStack::Backend::machineConsoleChanged);
    QSignalSpy errorSpy(&backend, &PodStack::Backend::lastErrorChanged);

    backend.followMachineConsole(QString(), 1024);
    QVERIFY(!backend.machineConsoleActive());
    QVERIFY(!backend.machineConsoleError().isEmpty());

    PodStack::MachineStore store(m_testDataDir + QStringLiteral("/data/podstack/podstack.db"));
    PodStack::Error storeError;
    QVERIFY2(store.open(&storeError), qPrintable(storeError.message));

    PodStack::MachineMeta meta;
    meta.id = QStringLiteral("gui-machine-console");
    meta.name = QStringLiteral("gui-console");
    meta.distro = QStringLiteral("debian");
    meta.distroVersion = QStringLiteral("12");
    meta.arch = QStringLiteral("amd64");
    meta.cpus = 2;
    meta.memoryBytes = 2LL * 1024LL * 1024LL * 1024LL;
    meta.diskBytes = 20LL * 1024LL * 1024LL * 1024LL;
    meta.state = QStringLiteral("running");
    meta.createdAt = QStringLiteral("2026-01-01T00:00:00.000Z");
    meta.updatedAt = meta.createdAt;

    const QString machineDir =
        m_testDataDir + QStringLiteral("/data/podstack/machines/") + meta.id;
    QVERIFY(QDir().mkpath(machineDir));
    QVERIFY2(store.upsert(meta, &storeError), qPrintable(storeError.message));

    const QString consolePath = machineDir + QStringLiteral("/console.log");
    QFile console(consolePath);
    QVERIFY(console.open(QIODevice::WriteOnly | QIODevice::Truncate));
    console.write("serial 1\nserial 2\n");
    console.close();

    backend.followMachineConsole(meta.id, 1024);
    QVERIFY(backend.machineConsoleActive());
    QTRY_COMPARE_WITH_TIMEOUT(backend.machineConsoleText(),
                              QStringLiteral("serial 1\nserial 2\n"), 5000);
    QVERIFY(backend.machineConsoleError().isEmpty());
    QVERIFY(consoleSpy.count() > 0);

    QFile appendConsole(consolePath);
    QVERIFY(appendConsole.open(QIODevice::Append));
    appendConsole.write("serial 3\n");
    appendConsole.close();
    QTRY_VERIFY_WITH_TIMEOUT(backend.machineConsoleText().contains(
                                 QStringLiteral("serial 3")),
                             5000);

    backend.stopMachineConsole();
    QVERIFY(!backend.machineConsoleActive());
    QVERIFY(backend.machineConsoleText().isEmpty());
    QVERIFY(backend.machineConsoleError().isEmpty());

    QFile afterStop(consolePath);
    QVERIFY(afterStop.open(QIODevice::Append));
    afterStop.write("serial 4\n");
    afterStop.close();
    QTest::qWait(700);
    QVERIFY(backend.machineConsoleText().isEmpty());

    backend.followMachineConsole(QStringLiteral("missing-machine-console"), 1024);
    QTRY_VERIFY_WITH_TIMEOUT(!backend.machineConsoleError().isEmpty(), 5000);
    QVERIFY(!backend.machineConsoleActive());
    QVERIFY(backend.lastError().isEmpty());
    QCOMPARE(errorSpy.count(), 0);
    backend.stopMachineConsole();

    QVERIFY(store.remove(meta.id));
    QDir(machineDir).removeRecursively();
}

void GuiTest::backendMachineFiles()
{
    PodStack::Backend backend;
    QSignalSpy filesSpy(&backend, &PodStack::Backend::machineFilesChanged);

    backend.refreshMachineFiles(QString(), QStringLiteral("/"));
    QVERIFY(!backend.machineFilesError().isEmpty());
    QVERIFY(!backend.machineFilesBusy());

    backend.refreshMachineFiles(QStringLiteral("missing-machine"), QStringLiteral("/"));
    QTRY_VERIFY_WITH_TIMEOUT(!backend.machineFilesBusy(), 5000);
    QVERIFY(backend.machineFilesError().contains(QStringLiteral("não encontrada")));

    backend.downloadMachineFile(QStringLiteral("missing-machine"),
                                QStringLiteral("/tmp/report.txt"),
                                QStringLiteral("report.txt"));
    QTRY_VERIFY_WITH_TIMEOUT(!backend.machineFilesBusy(), 5000);
    QVERIFY(backend.machineFilesError().contains(QStringLiteral("não encontrada")));

    const QString uploadSource = m_testDataDir + QStringLiteral("/gui-upload.txt");
    QFile upload(uploadSource);
    QVERIFY(upload.open(QIODevice::WriteOnly | QIODevice::Truncate));
    upload.write("gui upload");
    upload.close();

    backend.uploadMachineFile(QStringLiteral("missing-machine"), QStringLiteral("/tmp"),
                              uploadSource);
    QTRY_VERIFY_WITH_TIMEOUT(!backend.machineFilesBusy(), 5000);
    QVERIFY(backend.machineFilesError().contains(QStringLiteral("não encontrada")));

    PodStack::MachineStore store(m_testDataDir + QStringLiteral("/data/podstack/podstack.db"));
    PodStack::Error storeError;
    QVERIFY2(store.open(&storeError), qPrintable(storeError.message));

    PodStack::MachineMeta meta;
    meta.id = QStringLiteral("gui-machine-files");
    meta.name = QStringLiteral("gui-files");
    meta.distro = QStringLiteral("fedora");
    meta.distroVersion = QStringLiteral("44");
    meta.arch = QStringLiteral("amd64");
    meta.state = QStringLiteral("running");
    meta.sshUser = QStringLiteral("podstack");
    meta.sshPort = 32222;
    meta.createdAt = QStringLiteral("2026-01-01T00:00:00.000Z");
    meta.updatedAt = meta.createdAt;

    const QString machineDir =
        m_testDataDir + QStringLiteral("/data/podstack/machines/") + meta.id;
    QVERIFY(QDir().mkpath(machineDir));
    meta.sshKeyPath = machineDir + QStringLiteral("/id_ed25519");
    QFile keyFile(meta.sshKeyPath);
    QVERIFY(keyFile.open(QIODevice::WriteOnly | QIODevice::Truncate));
    keyFile.write("test-key\n");
    keyFile.close();
    QVERIFY(keyFile.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner));
    QVERIFY2(store.upsert(meta, &storeError), qPrintable(storeError.message));

    backend.refreshMachineFiles(meta.id, QStringLiteral("/home"));
    QTRY_COMPARE_WITH_TIMEOUT(backend.machineFiles().size(), 2, 5000);
    QVERIFY(!backend.machineFilesBusy());
    QVERIFY2(backend.machineFilesError().isEmpty(),
             qPrintable(backend.machineFilesError()));
    QCOMPARE(backend.machineFilesPath(), QStringLiteral("/home"));
    QVERIFY(filesSpy.count() > 0);

    QStringList names;
    for (const QVariant &value : backend.machineFiles()) {
        const QVariantMap item = value.toMap();
        names.append(item.value(QStringLiteral("name")).toString());
        QVERIFY(item.contains(QStringLiteral("path")));
        QVERIFY(item.contains(QStringLiteral("directory")));
        QVERIFY(item.contains(QStringLiteral("size")));
        QVERIFY(item.contains(QStringLiteral("modified")));
    }
    QVERIFY(names.contains(QStringLiteral("docs")));
    QVERIFY(names.contains(QStringLiteral("gui.txt")));

    backend.downloadMachineFile(meta.id, QStringLiteral("/home/podstack/gui.txt"),
                                QStringLiteral("gui.txt"));
    QTRY_VERIFY_WITH_TIMEOUT(!backend.machineFilesBusy(), 5000);
    QVERIFY2(backend.machineFilesError().isEmpty(),
             qPrintable(backend.machineFilesError()));
    const QString downloadDir =
        QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    QVERIFY2(downloadDir.startsWith(m_testDataDir), qPrintable(downloadDir));
    const QString downloaded = downloadDir + QStringLiteral("/gui.txt");
    QVERIFY(QFileInfo::exists(downloaded));

    backend.downloadMachineFile(meta.id, QStringLiteral("/home/podstack/gui.txt"),
                                QStringLiteral("../escape.txt"));
    QVERIFY(backend.machineFilesError().contains(QStringLiteral("inválido")));
    QVERIFY(!backend.machineFilesBusy());

    backend.uploadMachineFile(meta.id, QStringLiteral("/home/podstack"), uploadSource);
    QTRY_VERIFY_WITH_TIMEOUT(!backend.machineFilesBusy(), 5000);
    QVERIFY2(backend.machineFilesError().isEmpty(),
             qPrintable(backend.machineFilesError()));

    QVERIFY(store.remove(meta.id));
    QDir(machineDir).removeRecursively();
    QFile::remove(uploadSource);
    QFile::remove(downloaded);
}

void GuiTest::backendTerminal()
{
    PodStack::Backend backend;
    QSignalSpy terminalSpy(&backend, &PodStack::Backend::terminalRequested);
    QSignalSpy errorSpy(&backend, &PodStack::Backend::lastErrorChanged);

    backend.openTerminal(QStringLiteral("abc123"));
    QCOMPARE(terminalSpy.count(), 1);
    const QList<QVariant> request = terminalSpy.takeFirst();
    QCOMPARE(request.at(0).toString(), QStringLiteral("abc123"));
    QCOMPARE(request.at(1).toString(), QStringLiteral("podman"));
    QCOMPARE(request.at(2).toStringList(),
             QStringList({QStringLiteral("exec"), QStringLiteral("-it"),
                          QStringLiteral("abc123"), QStringLiteral("/bin/sh")}));

    backend.openTerminal(QString());
    QCOMPARE(terminalSpy.count(), 0);
    QCOMPARE(backend.lastError(), QStringLiteral("Container inválido."));
    QVERIFY(errorSpy.count() > 0);

    const int errorsBefore = errorSpy.count();
    backend.openMachineSsh(QStringLiteral("missing-machine"));
    QTRY_VERIFY_WITH_TIMEOUT(errorSpy.count() > errorsBefore, 5000);
    QVERIFY(backend.lastError().contains(QStringLiteral("não encontrada")));
    // O caminho de erro não abre sessão; o sucesso visual do QTermWidget é manual.
    QCOMPARE(terminalSpy.count(), 0);
}

void GuiTest::backendM3()
{
    PodStack::Backend backend;
    QSignalSpy composeSpy(&backend, &PodStack::Backend::composeProjectsChanged);
    QSignalSpy composeLogsSpy(&backend, &PodStack::Backend::composeLogsChanged);
    QSignalSpy buildSpy(&backend, &PodStack::Backend::buildStateChanged);
    QSignalSpy buildsSpy(&backend, &PodStack::Backend::buildsChanged);
    QSignalSpy eventsSpy(&backend, &PodStack::Backend::eventsChanged);
    QSignalSpy actionSpy(&backend, &PodStack::Backend::actionFinished);

    backend.refreshCompose();
    QTRY_VERIFY_WITH_TIMEOUT(composeSpy.count() > 0, 5000);
    QCOMPARE(backend.composeProjects().size(), 1);

    backend.subscribeComposeLogs(QStringLiteral("mock-project"), QStringLiteral("web"));
    QTRY_VERIFY_WITH_TIMEOUT(backend.composeLogLines().size() >= 2, 5000);
    backend.unsubscribeComposeLogs();

    const QString projectDir = m_testDataDir + QStringLiteral("/project");
    QDir().mkpath(projectDir);
    QFile dockerfile(projectDir + QStringLiteral("/Dockerfile"));
    QVERIFY(dockerfile.open(QIODevice::WriteOnly));
    dockerfile.write("FROM alpine:3.20\n");
    dockerfile.close();

    QVariantMap spec;
    spec.insert(QStringLiteral("contextPath"), projectDir);
    spec.insert(QStringLiteral("tag"), QStringLiteral("localhost/mock:1"));
    spec.insert(QStringLiteral("dockerfile"), QStringLiteral("Dockerfile"));
    backend.startBuild(spec);
    QTRY_VERIFY_WITH_TIMEOUT(!backend.buildActive() && buildSpy.count() > 0, 5000);
    QVERIFY(backend.buildLog().size() >= 2);
    QVERIFY(!backend.activeBuildId().isEmpty());

    QTRY_VERIFY_WITH_TIMEOUT(buildsSpy.count() > 0, 5000);
    QVERIFY(backend.builds().size() >= 1);

    QFile::remove(m_testDataDir + QStringLiteral("/podman-args.log"));
    QVariantMap multiSpec = spec;
    multiSpec.insert(QStringLiteral("tag"), QStringLiteral("localhost/mock:multi"));
    multiSpec.insert(QStringLiteral("platforms"),
                     QStringLiteral("linux/amd64,linux/arm64"));
    backend.startBuild(multiSpec);
    QTRY_VERIFY_WITH_TIMEOUT(!backend.buildActive() && backend.builds().size() >= 2, 5000);
    bool foundPlatforms = false;
    for (const QVariant &value : backend.builds()) {
        const QVariantMap build = value.toMap();
        if (build.value(QStringLiteral("imageRef")).toString()
            == QStringLiteral("localhost/mock:multi")) {
            foundPlatforms = build.value(QStringLiteral("platforms")).toString()
                             == QStringLiteral("linux/amd64, linux/arm64");
            break;
        }
    }
    QVERIFY(foundPlatforms);
    QFile argsFile(m_testDataDir + QStringLiteral("/podman-args.log"));
    QVERIFY(argsFile.open(QIODevice::ReadOnly));
    const QString args = QString::fromUtf8(argsFile.readAll());
    argsFile.close();
    QVERIFY2(args.contains(QStringLiteral("--platform linux/amd64,linux/arm64")),
             qPrintable(args));

    QTRY_VERIFY_WITH_TIMEOUT(eventsSpy.count() > 0, 5000);
    QVERIFY(backend.eventMessage().contains(QStringLiteral("start")));

    backend.composeUp(projectDir);
    QTRY_VERIFY_WITH_TIMEOUT(actionSpy.count() > 0, 5000);
    QCOMPARE(actionSpy.last().at(0).toBool(), true);

    backend.composeDown(projectDir, false);
    QTRY_VERIFY_WITH_TIMEOUT(actionSpy.count() > 1, 5000);
    QCOMPARE(actionSpy.last().at(0).toBool(), true);

    backend.composeRestart(projectDir, QString());
    QTRY_VERIFY_WITH_TIMEOUT(actionSpy.count() > 2, 5000);
    QCOMPARE(actionSpy.last().at(0).toBool(), true);

    backend.composeStop(projectDir, QString());
    QTRY_VERIFY_WITH_TIMEOUT(actionSpy.count() > 3, 5000);
    QCOMPARE(actionSpy.last().at(0).toBool(), true);

    backend.composeStart(projectDir, QString());
    QTRY_VERIFY_WITH_TIMEOUT(actionSpy.count() > 4, 5000);
    QCOMPARE(actionSpy.last().at(0).toBool(), true);

    backend.removeBuild(backend.activeBuildId());
    QTest::qWait(100);
}

void GuiTest::backendErrors()
{
    PodStack::Backend backend;
    QSignalSpy errorSpy(&backend, &PodStack::Backend::lastErrorChanged);
    QSignalSpy actionSpy(&backend, &PodStack::Backend::actionFinished);

    backend.pullImage(QString());
    QVERIFY(errorSpy.count() > 0);

    backend.startBuild({});
    QVERIFY(!backend.lastError().isEmpty());

    backend.uploadFile(QString(), QString(), QString());
    QVERIFY(!backend.lastError().isEmpty());

    backend.renameContainer(QString(), QString());
    QVERIFY(actionSpy.count() > 0);
    QCOMPARE(actionSpy.last().at(0).toBool(), false);

    backend.duplicateContainer(QString());
    QVERIFY(!backend.lastError().isEmpty());

    backend.openTerminal(QString());
    QVERIFY(!backend.lastError().isEmpty());

    backend.saveEngineMode(QStringLiteral("bogus"));
    QVERIFY(!backend.lastError().isEmpty());
}

void GuiTest::backendInvalidArguments()
{
    PodStack::Backend backend;
    QSignalSpy errorSpy(&backend, &PodStack::Backend::lastErrorChanged);
    QSignalSpy actionSpy(&backend, &PodStack::Backend::actionFinished);

    // Entradas vazias devem ser recusadas no Backend, sem chegar ao daemon.
    backend.removeImage(QString(), false);
    backend.tagImage(QString(), QString());
    backend.untagImage(QString(), QString());
    backend.exportImage(QString(), QString());
    backend.loadImage(QString());
    backend.createVolume(QString(), QString());
    backend.removeVolume(QString(), false);
    backend.cloneVolume(QString(), QString());
    backend.importHostPath(QString(), QString());
    backend.createNetwork(QString(), QString(), QString());
    backend.removeNetwork(QString(), false);
    backend.connectNetwork(QString(), QString());
    backend.disconnectNetwork(QString(), QString());
    backend.createPod(QString());
    backend.podAction(QString(), QString());
    backend.removePod(QString(), false);
    backend.composeUp(QString());
    backend.composeDown(QString(), false);
    backend.composeRestart(QString(), QString());
    backend.composeStop(QString(), QString());
    backend.composeStart(QString(), QString());
    backend.subscribeComposeLogs(QString(), QString());
    backend.removeBuild(QString());
    backend.loadMachineLogs(QString(), 0);
    backend.startMachineStats(QString());
    backend.followMachineConsole(QString(), 0);
    backend.refreshMachineFiles(QString(), QString());
    backend.refreshMachineSnapshots(QString());
    backend.createMachineSnapshot(QString(), QString());
    backend.restoreMachineSnapshot(QString(), QString());
    backend.removeMachineSnapshot(QString(), QString());
    backend.downloadMachineFile(QString(), QString(), QString());
    backend.uploadMachineFile(QString(), QString(), QString());
    backend.renameMachine(QString(), QString());
    backend.cloneMachine(QString(), QString());
    backend.updateMachine({});
    backend.setMachinePorts(QString(), {});
    backend.exportMachine(QString(), QString());
    backend.importMachine(QString());
    backend.loadContainerDetail(QString());
    backend.startContainerDetailStats(QString());
    backend.startContainerDetailLogs(QString(), 0, true);
    backend.containerDetailLifecycle(QString(), QString());
    backend.removeContainerDetail(QString(), false);
    backend.createContainer({}, false);
    backend.exposeContainerLan(QString(), true);
    backend.batchAll(QString());
    backend.batchIds({}, QString());
    backend.listFiles(QString(), QString());
    backend.downloadFile(QString(), QString());
    backend.uploadFile(QString(), QString(), QString());
    backend.setSetting(QString(), {});
    backend.setSetting(QStringLiteral("does.not.exist"), 1);
    backend.setSetting(QStringLiteral("app.theme"), QStringLiteral("bogus"));
    backend.resetSetting(QString());
    backend.resetSetting(QStringLiteral("does.not.exist"));
    backend.setupApply(QStringLiteral("bogus"));

    QTRY_VERIFY_WITH_TIMEOUT(!backend.lastError().isEmpty(), 5000);
    QVERIFY(errorSpy.count() > 0);
    QVERIFY(actionSpy.count() > 0);
}

void GuiTest::backendOperations()
{
    PodStack::Backend backend;
    const QString tempFile = m_testDataDir + QStringLiteral("/image.tar");
    QFile file(tempFile);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("tar");
    file.close();

    backend.refreshImages();
    backend.refreshVolumes();
    backend.refreshNetworks();
    backend.refreshPods();
    backend.refreshDiskUsage();
    backend.refreshCompose();
    backend.refreshBuilds();

    backend.createVolume(QStringLiteral("vol2"), QStringLiteral("local"));
    backend.cloneVolume(QStringLiteral("vol1"), QStringLiteral("vol2"));
    backend.importHostPath(QStringLiteral("vol3"), m_testDataDir);
    backend.removeVolume(QStringLiteral("vol1"), false);
    backend.pruneVolumes();

    backend.createNetwork(QStringLiteral("net2"), QStringLiteral("10.90.0.0/24"),
                          QStringLiteral("10.90.0.1"));
    backend.connectNetwork(QStringLiteral("mock-net"), QStringLiteral("abc123"));
    backend.disconnectNetwork(QStringLiteral("mock-net"), QStringLiteral("abc123"));
    backend.removeNetwork(QStringLiteral("net2"), false);

    backend.createPod(QStringLiteral("pod2"));
    backend.podAction(QStringLiteral("start"), QStringLiteral("pod1"));
    backend.removePod(QStringLiteral("pod1"), false);

    backend.tagImage(QStringLiteral("img1234567890"), QStringLiteral("repo:tag"));
    backend.untagImage(QStringLiteral("img1234567890"), QStringLiteral("repo:tag"));
    backend.exportImage(QStringLiteral("img1234567890"), tempFile);
    backend.loadImage(tempFile);
    backend.pruneImages(false);
    backend.removeImage(QStringLiteral("img1234567890"), false);

    backend.composeUp(QStringLiteral("/tmp/mock-project"));
    backend.composeRestart(QStringLiteral("/tmp/mock-project"), QStringLiteral("web"));
    backend.composeStop(QStringLiteral("/tmp/mock-project"), QStringLiteral("web"));
    backend.composeStart(QStringLiteral("/tmp/mock-project"), QStringLiteral("web"));
    backend.composeDown(QStringLiteral("/tmp/mock-project"), false);
    backend.subscribeComposeLogs(QStringLiteral("mock-project"), QStringLiteral("web"));
    backend.unsubscribeComposeLogs();
    backend.removeBuild(QStringLiteral("missing"));

    backend.startActivityMonitor();
    backend.stopActivityMonitor();

    // Dá tempo para os jobs assíncronos concluírem antes de destruir o Backend.
    QTest::qWait(1500);
}

void GuiTest::backendBatchActions()
{
    PodStack::Backend backend;
    QSignalSpy actionSpy(&backend, &PodStack::Backend::actionFinished);

    backend.batchPods({QStringLiteral("missing")}, QStringLiteral("bogus"));
    QVERIFY(!backend.lastError().isEmpty());

    backend.batchImages({QStringLiteral("missing-image")}, false);
    QTRY_VERIFY_WITH_TIMEOUT(actionSpy.count() >= 1, 5000);
    QCOMPARE(actionSpy.last().at(0).toBool(), false);
    QVERIFY(!backend.lastError().isEmpty());

    const int beforeVolumes = actionSpy.count();
    backend.batchVolumes({QStringLiteral("missing-volume")}, false);
    QTRY_VERIFY_WITH_TIMEOUT(actionSpy.count() > beforeVolumes, 5000);
    QCOMPARE(actionSpy.last().at(0).toBool(), false);

    const int beforeNetworks = actionSpy.count();
    backend.batchNetworks({QStringLiteral("missing-network")}, false);
    QTRY_VERIFY_WITH_TIMEOUT(actionSpy.count() > beforeNetworks, 5000);
    QCOMPARE(actionSpy.last().at(0).toBool(), false);

    const int beforePods = actionSpy.count();
    backend.batchPods({QStringLiteral("missing-pod")}, QStringLiteral("start"));
    QTRY_VERIFY_WITH_TIMEOUT(actionSpy.count() > beforePods, 5000);
    QCOMPARE(actionSpy.last().at(0).toBool(), false);

    const int beforeMachines = actionSpy.count();
    backend.batchMachines({QStringLiteral("missing-machine")}, QStringLiteral("start"));
    QTRY_VERIFY_WITH_TIMEOUT(actionSpy.count() > beforeMachines, 5000);
    QCOMPARE(actionSpy.last().at(0).toBool(), false);

    const int beforeBuilds = actionSpy.count();
    backend.batchBuilds({QStringLiteral("missing-build")});
    QTRY_VERIFY_WITH_TIMEOUT(actionSpy.count() > beforeBuilds, 5000);
    QCOMPARE(actionSpy.last().at(0).toBool(), false);

    const int beforeCompose = actionSpy.count();
    backend.batchCompose({QStringLiteral("/tmp/mock-project")}, QStringLiteral("up"));
    QTRY_VERIFY_WITH_TIMEOUT(actionSpy.count() > beforeCompose, 10000);
    QVERIFY2(actionSpy.last().at(0).toBool(), qPrintable(actionSpy.last().at(1).toString()));
}

void GuiTest::backendTheme()
{
    PodStack::Backend backend;
    QSignalSpy errorSpy(&backend, &PodStack::Backend::lastErrorChanged);
    QSignalSpy actionSpy(&backend, &PodStack::Backend::actionFinished);

    backend.saveTheme(QStringLiteral("light"));
    QTRY_COMPARE(backend.settings().value(QStringLiteral("theme")).toString(),
                 QStringLiteral("light"));

    backend.saveTheme(QStringLiteral("system"));
    QTRY_COMPARE(backend.settings().value(QStringLiteral("theme")).toString(),
                 QStringLiteral("system"));

    backend.saveTheme(QStringLiteral("bogus"));
    QVERIFY(!backend.lastError().isEmpty());
    QVERIFY(errorSpy.count() > 0);
    QCOMPARE(backend.settings().value(QStringLiteral("theme")).toString(),
             QStringLiteral("system"));
    QTRY_VERIFY(actionSpy.count() >= 2);
}

void GuiTest::backendSettings()
{
    PodStack::Backend backend;
    QSignalSpy schemaSpy(&backend, &PodStack::Backend::settingsSchemaChanged);
    QSignalSpy valuesSpy(&backend, &PodStack::Backend::settingsValuesChanged);
    QSignalSpy finishedSpy(&backend, &PodStack::Backend::settingFinished);

    backend.refreshSettings();
    QTRY_COMPARE(backend.settingsSchema().size(), 82);
    QVERIFY(schemaSpy.count() > 0);
    QVERIFY(backend.settingsValues().contains(QStringLiteral("engine.mode")));
    QVERIFY(valuesSpy.count() > 0);

    backend.setSetting(QStringLiteral("app.theme"), QStringLiteral("light"));
    QTRY_COMPARE(backend.settingsValues()
                     .value(QStringLiteral("app.theme"))
                     .toString(),
                 QStringLiteral("light"));
    QTRY_VERIFY(finishedSpy.count() > 0);
    bool themeSaved = false;
    for (const QList<QVariant> &entry : finishedSpy) {
        if (entry.at(0).toString() == QStringLiteral("app.theme")) {
            themeSaved = entry.at(1).toBool();
        }
    }
    QVERIFY(themeSaved);

    backend.setSetting(QStringLiteral("terminal.fontSize"), 99);
    QTRY_VERIFY(!backend.settingsError().isEmpty());
    QCOMPARE(backend.settingsValues()
                 .value(QStringLiteral("terminal.fontSize"))
                 .toInt(),
             12);

    backend.setSetting(QStringLiteral("engine.mode"), QStringLiteral("rootful"));
    QTRY_COMPARE(backend.settingsValues()
                     .value(QStringLiteral("engine.mode"))
                     .toString(),
                 QStringLiteral("rootful"));
    QTRY_VERIFY(backend.settingsError().isEmpty());

    backend.resetSetting(QStringLiteral("app.theme"));
    QTRY_COMPARE(backend.settingsValues()
                     .value(QStringLiteral("app.theme"))
                     .toString(),
                 QStringLiteral("system"));
    backend.resetSetting(QStringLiteral("engine.mode"));
    QTRY_COMPARE(backend.settingsValues()
                     .value(QStringLiteral("engine.mode"))
                     .toString(),
                 QStringLiteral("rootless"));

    backend.setSetting(QStringLiteral("app.startAtLogin"), true);
    QTRY_VERIFY(QFile::exists(PodStack::Autostart::filePath()));
    backend.setSetting(QStringLiteral("app.startAtLogin"), false);
    QTRY_VERIFY(!QFile::exists(PodStack::Autostart::filePath()));
}

void GuiTest::backendTray()
{
    PodStack::Backend backend;
    QSignalSpy containersSpy(&backend, &PodStack::Backend::containersChanged);
    QTRY_VERIFY_WITH_TIMEOUT(containersSpy.count() > 0, 5000);
    QCOMPARE(backend.containers().size(),
             containersSpy.last().at(0).value<QVariantList>().size());

    QSignalSpy actionSpy(&backend, &PodStack::Backend::actionFinished);
    backend.lifecycle(QStringLiteral("pause"), QStringLiteral("abc123"));
    QVERIFY(backend.actionActive());
    backend.lifecycle(QStringLiteral("unpause"), QStringLiteral("abc123"));
    QVERIFY(backend.actionActive());
    QTRY_VERIFY_WITH_TIMEOUT(actionSpy.count() >= 2, 5000);
    QTRY_VERIFY_WITH_TIMEOUT(!backend.actionActive(), 5000);

    backend.saveTraySettings(false, false, true, false);
    QTRY_COMPARE(backend.settings().value(QStringLiteral("keepRunningInTray")).toBool(),
                 false);
    QTRY_COMPARE(backend.settings().value(QStringLiteral("showInTray")).toBool(), false);
    QTRY_COMPARE(backend.settings().value(QStringLiteral("startAtLogin")).toBool(), true);
    QTRY_COMPARE(backend.settings().value(QStringLiteral("notifications")).toBool(), false);
    QTRY_VERIFY(QFile::exists(PodStack::Autostart::filePath()));

    backend.saveTraySettings(true, true, false, true);
    QTRY_COMPARE(backend.settings().value(QStringLiteral("keepRunningInTray")).toBool(),
                 true);
    QTRY_COMPARE(backend.settings().value(QStringLiteral("showInTray")).toBool(), true);
    QTRY_COMPARE(backend.settings().value(QStringLiteral("startAtLogin")).toBool(), false);
    QTRY_COMPARE(backend.settings().value(QStringLiteral("notifications")).toBool(), true);
    QTRY_VERIFY(!QFile::exists(PodStack::Autostart::filePath()));
}

void GuiTest::guiStartsAndQuits()
{
    PodStack::Config config = PodStack::Config::load();
    config.set(QStringLiteral("app.showInTray"), false);
    QString error;
    QVERIFY2(config.save(&error), qPrintable(error));

    const QString runtimeDir = m_testDataDir + QStringLiteral("/runtime");
    QDir().mkpath(runtimeDir);
    QFile::setPermissions(runtimeDir, QFileDevice::ReadOwner | QFileDevice::WriteOwner
                                         | QFileDevice::ExeOwner);
    const QString guiPath =
        QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("../podstack-gui"));

    QProcessEnvironment environment = daemonEnvironment();
    environment.insert(QStringLiteral("QT_QPA_PLATFORM"), QStringLiteral("offscreen"));
    environment.insert(QStringLiteral("QT_QUICK_BACKEND"), QStringLiteral("software"));
    environment.insert(QStringLiteral("XDG_RUNTIME_DIR"), runtimeDir);

    QProcess gui;
    gui.setProcessEnvironment(environment);
    gui.setProcessChannelMode(QProcess::SeparateChannels);
    gui.start(guiPath, {QStringLiteral("--hidden")});
    QVERIFY2(gui.waitForStarted(10000), qPrintable(gui.errorString()));

    const QString instanceSocket = runtimeDir + QStringLiteral("/podstack/gui.sock");
    // O runner do CI é lento para carregar QML; 60 s cobre o container frio.
    for (int attempt = 0; attempt < 1200 && !QFileInfo::exists(instanceSocket); ++attempt) {
        if (gui.state() == QProcess::NotRunning) {
            break;
        }
        QTest::qWait(50);
    }
    if (!QFileInfo::exists(instanceSocket)) {
        const QByteArray out = gui.readAllStandardOutput();
        const QByteArray err = gui.readAllStandardError();
        QVERIFY2(false, qPrintable(QStringLiteral("socket de instância não apareceu: %1 (state=%2, "
                                                  "exit=%3)\nstdout:\n%4\nstderr:\n%5")
                                       .arg(instanceSocket)
                                       .arg(int(gui.state()))
                                       .arg(gui.exitCode())
                                       .arg(QString::fromUtf8(out), QString::fromUtf8(err))));
    }

    QProcess quit;
    quit.setProcessEnvironment(environment);
    quit.start(guiPath, {QStringLiteral("--quit")});
    QVERIFY(quit.waitForFinished(10000));
    QCOMPARE(quit.exitCode(), 0);

    QVERIFY(gui.waitForFinished(10000));
    QCOMPARE(gui.exitCode(), 0);

    config.set(QStringLiteral("app.showInTray"), true);
    QVERIFY2(config.save(&error), qPrintable(error));
}

void GuiTest::backendActivityMonitor()
{
    PodStack::Backend backend;
    QSignalSpy activitySpy(&backend, &PodStack::Backend::activityChanged);

    backend.startActivityMonitor();
    QVERIFY(backend.activityActive());
    QTRY_VERIFY_WITH_TIMEOUT(activitySpy.count() > 1, 7000);
    QTRY_COMPARE(backend.activityStats()
                     .value(QStringLiteral("containers"))
                     .toList()
                     .size(),
                 2);
    QVERIFY(backend.activityStats()
                .value(QStringLiteral("host"))
                .toMap()
                .value(QStringLiteral("memTotal"))
                .toLongLong()
            > 0);

    backend.stopActivityMonitor();
    QVERIFY(!backend.activityActive());
}

void GuiTest::streamClientProtocol()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString socketPath = dir.filePath(QStringLiteral("control.sock"));
    QLocalServer server;
    QVERIFY2(server.listen(socketPath), qPrintable(server.errorString()));

    const QByteArray previous = qgetenv("PODSTACK_CONTROL_SOCKET");
    qputenv("PODSTACK_CONTROL_SOCKET", socketPath.toUtf8());
    const auto restore = qScopeGuard([previous] {
        if (previous.isEmpty()) {
            qunsetenv("PODSTACK_CONTROL_SOCKET");
        } else {
            qputenv("PODSTACK_CONTROL_SOCKET", previous);
        }
    });

    const auto accept = [&server](const QByteArray &needle) -> QLocalSocket * {
        QElapsedTimer timer;
        timer.start();
        while (timer.elapsed() < 5000) {
            if (!server.waitForNewConnection(1000)) {
                continue;
            }
            QLocalSocket *socket = server.nextPendingConnection();
            if (!socket) {
                continue;
            }
            QByteArray request;
            while (!request.contains('\n') && socket->waitForReadyRead(1000)) {
                request += socket->readAll();
            }
            if (request.contains(needle)) {
                return socket;
            }
            socket->deleteLater();
        }
        return nullptr;
    };

    PodStack::StreamClient client;
    QSignalSpy readySpy(&client, &PodStack::StreamClient::subscriptionReady);
    QSignalSpy logSpy(&client, &PodStack::StreamClient::logLine);
    QSignalSpy errorSpy(&client, &PodStack::StreamClient::error);
    QSignalSpy statsSpy(&client, &PodStack::StreamClient::stats);
    QSignalSpy consoleSpy(&client, &PodStack::StreamClient::machineConsoleEvent);
    QSignalSpy buildSpy(&client, &PodStack::StreamClient::buildFinished);
    QSignalSpy buildEventSpy(&client, &PodStack::StreamClient::buildEvent);
    QSignalSpy composeLogSpy(&client, &PodStack::StreamClient::composeLog);
    QSignalSpy machineJobSpy(&client, &PodStack::StreamClient::machineCreateFinished);
    QSignalSpy setupSpy(&client, &PodStack::StreamClient::setupFinished);

    client.subscribeLogs(QStringLiteral("abc"), 10, true);
    QLocalSocket *socket = accept(QByteArrayLiteral("containers.logs.subscribe"));
    QVERIFY(socket);
    socket->write(QByteArrayLiteral(
        "{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":{\"subscriptionId\":\"sub-1\"}}\n"
        "{\"jsonrpc\":\"2.0\",\"method\":\"containers.logs.delta\",\"params\":{\"line\":\"ola\"}}\n"
        "not-json\n"
        "{\"jsonrpc\":\"2.0\",\"method\":\"containers.logs.error\",\"params\":{\"error\":"
        "{\"message\":\"falhou\"}}}\n"));
    socket->flush();
    QTRY_VERIFY_WITH_TIMEOUT(readySpy.count() > 0, 5000);
    QTRY_VERIFY_WITH_TIMEOUT(logSpy.count() > 0, 5000);
    QCOMPARE(logSpy.last().at(0).toString(), QStringLiteral("ola"));
    QTRY_VERIFY_WITH_TIMEOUT(errorSpy.count() > 0, 5000);
    QVERIFY(errorSpy.last().at(0).toString().contains(QStringLiteral("falhou")));

    client.subscribeStats(QStringLiteral("abc"), 2);
    socket = accept(QByteArrayLiteral("containers.stats.subscribe"));
    QVERIFY(socket);
    socket->write(QByteArrayLiteral(
        "{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":{\"subscriptionId\":\"sub-2\"}}\n"
        "{\"jsonrpc\":\"2.0\",\"method\":\"containers.stats.delta\",\"params\":{\"cpu_percent\":"
        "\"1.5%\"}}\n"));
    socket->flush();
    QTRY_VERIFY_WITH_TIMEOUT(statsSpy.count() > 0, 5000);
    QCOMPARE(statsSpy.last().at(0).toJsonObject().value(QStringLiteral("cpu_percent")).toString(),
             QStringLiteral("1.5%"));

    client.startMachineConsole(QStringLiteral("machine-1"), 1024);
    socket = accept(QByteArrayLiteral("\"method\":\"machines.console\""));
    QVERIFY(socket);
    socket->write(QByteArrayLiteral(
        "{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":{\"subscriptionId\":\"sub-3\","
        "\"id\":\"machine-1\"}}\n"
        "{\"jsonrpc\":\"2.0\",\"method\":\"machines.console.delta\",\"params\":{\"data\":"
        "\"boot\",\"reset\":true}}\n"));
    socket->flush();
    QTRY_VERIFY_WITH_TIMEOUT(consoleSpy.count() > 0, 5000);

    client.startBuild(QJsonObject{{QStringLiteral("context"), QStringLiteral("/tmp")},
                                  {QStringLiteral("tag"), QStringLiteral("x")}});
    socket = accept(QByteArrayLiteral("builds.start"));
    QVERIFY(socket);
    socket->write(QByteArrayLiteral(
        "{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":{\"subscriptionId\":\"sub-4\"}}\n"
        "{\"jsonrpc\":\"2.0\",\"method\":\"builds.delta\",\"params\":{\"stream\":\"step\"}}\n"
        "{\"jsonrpc\":\"2.0\",\"method\":\"builds.done\",\"params\":{\"state\":\"success\"}}\n"));
    socket->flush();
    QTRY_VERIFY_WITH_TIMEOUT(buildEventSpy.count() > 0, 5000);
    QTRY_VERIFY_WITH_TIMEOUT(buildSpy.count() > 0, 5000);

    client.subscribeComposeLogs(QStringLiteral("proj"), QStringLiteral("web"));
    socket = accept(QByteArrayLiteral("compose.logs.subscribe"));
    QVERIFY(socket);
    socket->write(QByteArrayLiteral(
        "{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":{\"subscriptionId\":\"sub-9\"}}\n"
        "{\"jsonrpc\":\"2.0\",\"method\":\"compose.logs.delta\",\"params\":{"
        "\"service\":\"web\",\"line\":\"oi\"}}\n"));
    socket->flush();
    QTRY_VERIFY_WITH_TIMEOUT(composeLogSpy.count() > 0, 5000);

    const QList<QPair<QString, QString>> machineJobs = {
        {QStringLiteral("machines.create"), QStringLiteral("create")},
        {QStringLiteral("machines.export"), QStringLiteral("export")},
        {QStringLiteral("machines.import"), QStringLiteral("import")},
        {QStringLiteral("machines.clone"), QStringLiteral("clone")},
    };
    for (const auto &[method, label] : machineJobs) {
        if (label == QStringLiteral("create")) {
            client.startMachineCreate({});
        } else if (label == QStringLiteral("export")) {
            client.startMachineExport({});
        } else if (label == QStringLiteral("import")) {
            client.startMachineImport({});
        } else {
            client.startMachineClone({});
        }
        socket = accept(method.toUtf8());
        QVERIFY2(socket, qPrintable(method));
        socket->write(
            QByteArrayLiteral("{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":{"
                              "\"subscriptionId\":\"sub-job\"}}\n"
                              "{\"jsonrpc\":\"2.0\",\"method\":\"machines.delta\",\"params\":{"
                              "\"stage\":\"disk\",\"progress\":50}}\n"
                              "{\"jsonrpc\":\"2.0\",\"method\":\"machines.done\",\"params\":{"
                              "\"state\":\"success\"}}\n"));
        socket->flush();
        QTRY_VERIFY_WITH_TIMEOUT(machineJobSpy.count() > 0, 5000);
        machineJobSpy.clear();
    }

    client.startSetup(QJsonObject{{QStringLiteral("action"), QStringLiteral("enable-socket")}});
    socket = accept(QByteArrayLiteral("setup.apply"));
    QVERIFY(socket);
    socket->write(QByteArrayLiteral(
        "{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":{\"subscriptionId\":\"sub-5\"}}\n"
        "{\"jsonrpc\":\"2.0\",\"method\":\"setup.done\",\"params\":{\"state\":\"success\"}}\n"));
    socket->flush();
    QTRY_VERIFY_WITH_TIMEOUT(setupSpy.count() > 0, 5000);

    QSignalSpy pullSpy(&client, &PodStack::StreamClient::pullFinished);
    client.pullImage(QStringLiteral("alpine"), {});
    socket = accept(QByteArrayLiteral("images.pull"));
    QVERIFY(socket);
    socket->write(QByteArrayLiteral(
        "{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":{\"subscriptionId\":\"sub-6\"}}\n"
        "{\"jsonrpc\":\"2.0\",\"method\":\"images.pull.delta\",\"params\":{"
        "\"status\":\"Downloading\"}}\n"
        "{\"jsonrpc\":\"2.0\",\"method\":\"images.pull.done\",\"params\":{"
        "\"images\":[\"img\"]}}\n"));
    socket->flush();
    QTRY_VERIFY_WITH_TIMEOUT(pullSpy.count() > 0, 5000);
    QCOMPARE(pullSpy.last().at(0).toBool(), true);

    QSignalSpy machineStatsSpy(&client, &PodStack::StreamClient::machineStats);
    client.subscribeMachineStats(QStringLiteral("machine-1"), 2);
    socket = accept(QByteArrayLiteral("machines.stats.subscribe"));
    QVERIFY(socket);
    socket->write(QByteArrayLiteral(
        "{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":{\"subscriptionId\":\"sub-7\"}}\n"
        "{\"jsonrpc\":\"2.0\",\"method\":\"machines.stats.delta\",\"params\":{"
        "\"cpuPercent\":1.0}}\n"));
    socket->flush();
    QTRY_VERIFY_WITH_TIMEOUT(machineStatsSpy.count() > 0, 5000);

    QSignalSpy eventSpy(&client, &PodStack::StreamClient::eventReceived);
    client.subscribeEvents();
    socket = accept(QByteArrayLiteral("events.subscribe"));
    QVERIFY(socket);
    socket->write(QByteArrayLiteral(
        "{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":{\"subscriptionId\":\"sub-8\"}}\n"
        "{\"jsonrpc\":\"2.0\",\"method\":\"event\",\"params\":{"
        "\"Action\":\"start\"}}\n"));
    socket->flush();
    QTRY_VERIFY_WITH_TIMEOUT(eventSpy.count() > 0, 5000);

    client.unsubscribe();
}

void GuiTest::backendProxy()
{
    PodStack::Backend backend;
    QSignalSpy errorSpy(&backend, &PodStack::Backend::lastErrorChanged);
    QSignalSpy actionSpy(&backend, &PodStack::Backend::actionFinished);
    QSignalSpy settingsSpy(&backend, &PodStack::Backend::settingsChanged);

    backend.saveProxySettings(QStringLiteral("http://127.0.0.1:3128"),
                              QStringLiteral("localhost, .internal"));
    QTRY_COMPARE(backend.settings().value(QStringLiteral("proxy")).toString(),
                 QStringLiteral("http://127.0.0.1:3128"));
    QTRY_COMPARE(backend.settings().value(QStringLiteral("proxyExclude")).toStringList(),
                 QStringList({QStringLiteral("localhost"), QStringLiteral(".internal")}));
    QTRY_VERIFY(actionSpy.count() >= 2);
    QCOMPARE(actionSpy.last().at(0).toBool(), true);
    QVERIFY(settingsSpy.count() > 0);

    const int errorsBefore = errorSpy.count();
    backend.saveProxySettings(QStringLiteral("ftp://127.0.0.1:21"),
                              QStringLiteral("localhost"));
    QVERIFY(!backend.lastError().isEmpty());
    QVERIFY(errorSpy.count() > errorsBefore);
    QCOMPARE(actionSpy.last().at(0).toBool(), false);
    QCOMPARE(backend.settings().value(QStringLiteral("proxy")).toString(),
             QStringLiteral("http://127.0.0.1:3128"));
    QCOMPARE(backend.settings().value(QStringLiteral("proxyExclude")).toStringList(),
             QStringList({QStringLiteral("localhost"), QStringLiteral(".internal")}));

    backend.saveProxySettings(QStringLiteral("http://127.0.0.1:3128"),
                              QStringLiteral("bad host"));
    QVERIFY(!backend.lastError().isEmpty());
    QCOMPARE(actionSpy.last().at(0).toBool(), false);
    QCOMPARE(backend.settings().value(QStringLiteral("proxyExclude")).toStringList(),
             QStringList({QStringLiteral("localhost"), QStringLiteral(".internal")}));

    backend.saveProxySettings(QStringLiteral("auto"),
                              QStringLiteral("localhost, .internal"));
    QTRY_COMPARE(backend.settings().value(QStringLiteral("proxy")).toString(),
                 QStringLiteral("auto"));
}

void GuiTest::backendSetup()
{
    PodStack::Backend backend;
    QSignalSpy setupSpy(&backend, &PodStack::Backend::setupChanged);

    QVERIFY(!backend.setupLoaded());
    backend.setupRefresh();
    QTRY_VERIFY_WITH_TIMEOUT(backend.setupLoaded(), 15000);
    QVERIFY(setupSpy.count() > 0);
    QVERIFY(backend.setupChecks().size() >= 8);
    QVERIFY(backend.setupCompleted() == false);
    QVERIFY(backend.setupPending());

    backend.setupSkip();
    QTRY_VERIFY_WITH_TIMEOUT(!backend.setupPending(), 10000);
    QVERIFY(!backend.setupCompleted());

    backend.setupComplete();
    if (!backend.setupMandatoryOk()) {
        // O host (ex.: container do CI) não tem QEMU/OVMF/virtiofsd. A recusa é
        // parte do contrato; o caminho de sucesso é coberto onde os checks passam.
        QVERIFY(!backend.setupError().isEmpty());
        QVERIFY(!backend.setupCompleted());
        QSKIP("Checks obrigatórios indisponíveis no host; conclusão bloqueada.");
    }
    QTRY_VERIFY_WITH_TIMEOUT(backend.setupCompleted(), 10000);
    QVERIFY(!backend.setupPending());
}

void GuiTest::backendSetupJob()
{
    PodStack::Backend backend;
    QSignalSpy jobSpy(&backend, &PodStack::Backend::setupJobChanged);

    backend.setupApply(QStringLiteral("bogus"));
    QTRY_VERIFY_WITH_TIMEOUT(!backend.setupJobActive(), 10000);
    QVERIFY(!backend.setupError().isEmpty());

    backend.setupApply(QStringLiteral("enable-socket"));
    QTRY_VERIFY_WITH_TIMEOUT(!backend.setupJobActive(), 20000);
    QVERIFY(jobSpy.count() > 0);
    QVERIFY(!backend.setupLog().isEmpty());

    backend.setupCancel();
    backend.setupComplete();
    QVERIFY(!backend.setupError().isEmpty());
}

} // namespace

QTEST_GUILESS_MAIN(GuiTest)
#include "test_gui.moc"
