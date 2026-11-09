#include "MockPodmanServer.h"

#include "core/Config.h"
#include "gui/Autostart.h"
#include "gui/Backend.h"
#include "gui/ContainerListModel.h"
#include "machines/MachineStore.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QProcessEnvironment>
#include <QScopeGuard>
#include <QSignalSpy>
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
    void backendStreams();
    void backendCatalog();
    void backendMachines();
    void backendM3();
    void backendErrors();
    void backendTheme();
    void backendProxy();
    void backendSetup();
    void backendTray();
    void backendActivityMonitor();
    void guiStartsAndQuits();

private:
    QProcessEnvironment daemonEnvironment() const;

    MockPodmanServer m_mock;
    QProcess m_daemon;
    QString m_controlSocket;
    QString m_testDataDir;
    QString m_fakeProvider;
};

QProcessEnvironment GuiTest::daemonEnvironment() const
{
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("PODSTACK_CONTROL_SOCKET"), m_controlSocket);
    env.insert(QStringLiteral("PODSTACK_PODMAN_SOCKET"), m_mock.socketPath());
    env.insert(QStringLiteral("PODSTACK_COMPOSE_PROVIDER"), m_fakeProvider);
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

void GuiTest::backendCatalog()
{
    PodStack::Backend backend;
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
    meta.createdAt = QStringLiteral("2026-01-01T00:00:00.000Z");
    meta.updatedAt = meta.createdAt;
    QVERIFY2(store.upsert(meta, &storeError), qPrintable(storeError.message));

    const QString machineDir =
        m_testDataDir + QStringLiteral("/data/podstack/machines/") + meta.id;
    QVERIFY(QDir().mkpath(machineDir));
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

    QVERIFY(store.remove(meta.id));
    QDir(machineDir).removeRecursively();
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

void GuiTest::backendTheme()
{
    PodStack::Backend backend;
    QSignalSpy errorSpy(&backend, &PodStack::Backend::lastErrorChanged);
    QSignalSpy actionSpy(&backend, &PodStack::Backend::actionFinished);

    backend.saveTheme(QStringLiteral("light"));
    QCOMPARE(backend.settings().value(QStringLiteral("theme")).toString(),
             QStringLiteral("light"));

    backend.saveTheme(QStringLiteral("system"));
    QCOMPARE(backend.settings().value(QStringLiteral("theme")).toString(),
             QStringLiteral("system"));

    backend.saveTheme(QStringLiteral("bogus"));
    QVERIFY(!backend.lastError().isEmpty());
    QVERIFY(errorSpy.count() > 0);
    QCOMPARE(backend.settings().value(QStringLiteral("theme")).toString(),
             QStringLiteral("system"));
    QVERIFY(actionSpy.count() >= 2);
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
    const QVariantMap settings = backend.settings();
    QCOMPARE(settings.value(QStringLiteral("keepRunningInTray")).toBool(), false);
    QCOMPARE(settings.value(QStringLiteral("showInTray")).toBool(), false);
    QCOMPARE(settings.value(QStringLiteral("startAtLogin")).toBool(), true);
    QCOMPARE(settings.value(QStringLiteral("notifications")).toBool(), false);
    QVERIFY(QFile::exists(PodStack::Autostart::filePath()));

    backend.saveTraySettings(true, true, false, true);
    QVERIFY(!QFile::exists(PodStack::Autostart::filePath()));
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
    gui.setProcessChannelMode(QProcess::ForwardedChannels);
    gui.start(guiPath, {QStringLiteral("--hidden")});
    QVERIFY2(gui.waitForStarted(5000), qPrintable(gui.errorString()));

    const QString instanceSocket = runtimeDir + QStringLiteral("/podstack/gui.sock");
    for (int attempt = 0; attempt < 100 && !QFileInfo::exists(instanceSocket); ++attempt) {
        QTest::qWait(50);
    }
    QVERIFY(QFileInfo::exists(instanceSocket));

    QProcess quit;
    quit.setProcessEnvironment(environment);
    quit.start(guiPath, {QStringLiteral("--quit")});
    QVERIFY(quit.waitForFinished(5000));
    QCOMPARE(quit.exitCode(), 0);

    QVERIFY(gui.waitForFinished(5000));
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

void GuiTest::backendProxy()
{
    PodStack::Backend backend;
    QSignalSpy errorSpy(&backend, &PodStack::Backend::lastErrorChanged);
    QSignalSpy actionSpy(&backend, &PodStack::Backend::actionFinished);
    QSignalSpy settingsSpy(&backend, &PodStack::Backend::settingsChanged);

    backend.saveProxySettings(QStringLiteral("http://127.0.0.1:3128"),
                              QStringLiteral("localhost, .internal"));
    QVariantMap settings = backend.settings();
    QCOMPARE(settings.value(QStringLiteral("proxy")).toString(),
             QStringLiteral("http://127.0.0.1:3128"));
    QCOMPARE(settings.value(QStringLiteral("proxyExclude")).toStringList(),
             QStringList({QStringLiteral("localhost"), QStringLiteral(".internal")}));
    QCOMPARE(actionSpy.last().at(0).toBool(), true);
    QVERIFY(settingsSpy.count() > 0);

    const int errorsBefore = errorSpy.count();
    backend.saveProxySettings(QStringLiteral("ftp://127.0.0.1:21"),
                              QStringLiteral("localhost"));
    QVERIFY(!backend.lastError().isEmpty());
    QVERIFY(errorSpy.count() > errorsBefore);
    QCOMPARE(actionSpy.last().at(0).toBool(), false);
    settings = backend.settings();
    QCOMPARE(settings.value(QStringLiteral("proxy")).toString(),
             QStringLiteral("http://127.0.0.1:3128"));
    QCOMPARE(settings.value(QStringLiteral("proxyExclude")).toStringList(),
             QStringList({QStringLiteral("localhost"), QStringLiteral(".internal")}));

    backend.saveProxySettings(QStringLiteral("http://127.0.0.1:3128"),
                              QStringLiteral("bad host"));
    QVERIFY(!backend.lastError().isEmpty());
    QCOMPARE(actionSpy.last().at(0).toBool(), false);
    QCOMPARE(backend.settings().value(QStringLiteral("proxyExclude")).toStringList(),
             QStringList({QStringLiteral("localhost"), QStringLiteral(".internal")}));

    backend.saveProxySettings(QStringLiteral("auto"),
                              QStringLiteral("localhost, .internal"));
    QCOMPARE(backend.settings().value(QStringLiteral("proxy")).toString(),
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

} // namespace

QTEST_GUILESS_MAIN(GuiTest)
#include "test_gui.moc"
