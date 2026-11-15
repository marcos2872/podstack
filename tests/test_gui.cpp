#include "MockPodmanServer.h"

#include "core/config/Config.h"
#include "core/rpc/StreamClient.h"
#include "gui/app/Autostart.h"
#include "gui/app/UiState.h"
#include "gui/app/UnixSignals.h"
#include "gui/controllers/Backend.h"
#include "gui/models/ContainerListModel.h"
#include "k8s/store/K8sStore.h"
#include "machines/model/MachineStore.h"

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

#include <csignal>

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
    void backendCreateSpec();
    void backendContainersSignal();
    void backendCreateMissingImage();
    void backendStreams();
    void backendContainerDetail();
    void backendContainerDetailRace();
    void backendCatalog();
    void backendRefreshUpdatesCatalog();
    void backendKubernetes();
    void backendKubernetesEnabledError();
    void backendKubernetesDisableJob();
    void backendControllerEdges();
    void backendMachines();
    void backendMachineStats();
    void backendMachineConsole();
    void backendMachineFiles();
    void backendTerminal();
    void backendM3();
    void backendBuildCancelIdle();
    void backendErrors();
    void backendInvalidArguments();
    void backendOperations();
    void backendBatchActions();
    void backendBatchPartial();
    void backendComposeErrorDetail();
    void backendRejectDuplicateAction();
    void feedbackQueue();
    void backendTheme();
    void backendSettings();
    void backendProxy();
    void backendSetup();
    void backendSetupJob();
    void backendTray();
    void backendActivityMonitor();
    void streamClientProtocol();
    void uiStateRoundTrip();
    void uiStateInvalidFallsBack();
    void uiStateRejectsInvalidValues();
    void unixTerminationSignal();
    void systemLogs();
    void loadUpdateState();
    void diagnosticReport();
    void clearUpdateState();
    void installUpdateFlatpakGuard();
    void installUpdateSystemdGuard();
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

    m_testDataDir = QDir::tempPath() +
                    QStringLiteral("/podstack-gui-data-%1").arg(QCoreApplication::applicationPid());
    QDir(m_testDataDir).removeRecursively();
    QDir().mkpath(m_testDataDir);
    QDir().mkpath(m_testDataDir + QStringLiteral("/config"));
    qputenv("XDG_CONFIG_HOME", (m_testDataDir + QStringLiteral("/config")).toUtf8());
    qputenv("XDG_STATE_HOME", (m_testDataDir + QStringLiteral("/state")).toUtf8());
    m_fakeProvider = m_testDataDir + QStringLiteral("/compose-provider.sh");
    QFile provider(m_fakeProvider);
    QVERIFY(provider.open(QIODevice::WriteOnly | QIODevice::Truncate));
    provider.write("#!/bin/sh\n"
                   "case \"$*\" in\n"
                   "  *fail*)\n"
                   "    echo \"Emulate Docker CLI using podman. Create /etc/containers/nodocker to "
                   "quiet msg.\" >&2\n"
                   "    echo \"Error response from daemon: rootlessport listen tcp "
                   "0.0.0.0:18081: bind: address already in use\" >&2\n"
                   "    exit 1;;\n"
                   "esac\n"
                   "echo \"fake compose $@\"\nexit 0\n");
    provider.close();
    QVERIFY(provider.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner |
                                    QFileDevice::ExeOwner));

    m_fakePodman = m_testDataDir + QStringLiteral("/podman-fake.sh");
    QFile podman(m_fakePodman);
    QVERIFY(podman.open(QIODevice::WriteOnly | QIODevice::Truncate));
    const QString fakePodman = QStringLiteral("#!/bin/sh\n"
                                              "echo \"$@\" >> \"%1\"\n"
                                              "if [ \"$1\" = \"build\" ]; then\n"
                                              "  echo \"line one\"\n"
                                              "  echo \"line two\"\n"
                                              "fi\n"
                                              "exit 0\n")
                                   .arg(m_testDataDir + QStringLiteral("/podman-args.log"));
    podman.write(fakePodman.toUtf8());
    podman.close();
    QVERIFY(podman.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner |
                                  QFileDevice::ExeOwner));

    m_fakeSsh = m_testDataDir + QStringLiteral("/ssh-fake.sh");
    QFile ssh(m_fakeSsh);
    QVERIFY(ssh.open(QIODevice::WriteOnly | QIODevice::Truncate));
    const QString fakeSsh =
        QStringLiteral(
            "#!/bin/sh\n"
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
    QVERIFY(ssh.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner |
                               QFileDevice::ExeOwner));

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
    QVERIFY(scp.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner |
                               QFileDevice::ExeOwner));

    m_controlSocket =
        QDir::tempPath() +
        QStringLiteral("/podstack-gui-%1.sock").arg(QCoreApplication::applicationPid());
    QFile::remove(m_controlSocket);

    const QString daemonPath =
        QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("podstackd"));
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
    QSignalSpy containersSpy(backend.containers(), &PodStack::ContainerController::itemsChanged);
    QSignalSpy stateSpy(&backend, &PodStack::Backend::stateChanged);

    QTRY_VERIFY_WITH_TIMEOUT(containersSpy.count() > 0, 5000);
    QVERIFY(backend.connected());
    QVERIFY(backend.error().isEmpty());
    QCOMPARE(backend.version(), QStringLiteral(PODSTACK_VERSION));

    QCOMPARE(backend.containers()->items().size(), 1);
}

void GuiTest::backendError()
{
    const QByteArray previous = qgetenv("PODSTACK_CONTROL_SOCKET");
    const QString missing =
        QDir::tempPath() +
        QStringLiteral("/podstack-gui-missing-%1.sock").arg(QCoreApplication::applicationPid());
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
    auto *containers = backend.containers();

    containers->lifecycle(QStringLiteral("pause"), QStringLiteral("abc123"));
    QTRY_VERIFY_WITH_TIMEOUT(!containers->actionActive(), 5000);
    QVERIFY(containers->lastError().isEmpty());

    containers->removeContainer(QStringLiteral("abc123"), true, false);
    QTRY_VERIFY_WITH_TIMEOUT(!containers->actionActive(), 5000);
    QVERIFY(containers->lastError().isEmpty());

    containers->lifecycle(QStringLiteral("does-not-exist"), QStringLiteral("abc123"));
    QTRY_VERIFY_WITH_TIMEOUT(!containers->actionActive(), 5000);
    QVERIFY(!containers->lastError().isEmpty());

    containers->lifecycle(QString(), QStringLiteral("abc123"));
    QVERIFY(!containers->lastError().isEmpty());

    QVariantMap spec;
    spec.insert(QStringLiteral("image"), QStringLiteral("alpine:3.20"));
    spec.insert(QStringLiteral("name"), QStringLiteral("created"));
    spec.insert(QStringLiteral("portsText"), QStringLiteral("8080:80"));
    spec.insert(QStringLiteral("envText"), QStringLiteral("A=B"));
    spec.insert(QStringLiteral("mountsText"), QStringLiteral("/tmp:/mnt"));
    containers->createContainer(spec, true);
    QTRY_VERIFY_WITH_TIMEOUT(!containers->actionActive(), 5000);
    QVERIFY(containers->lastError().isEmpty());

    QSignalSpy containersSpy(containers, &PodStack::ContainerController::itemsChanged);
    backend.refresh();
    QTRY_VERIFY_WITH_TIMEOUT(containersSpy.count() > 0, 5000);

    containers->batchAll(QStringLiteral("start"));
    QTRY_VERIFY_WITH_TIMEOUT(!containers->actionActive(), 5000);
    QVERIFY(containers->lastError().isEmpty());

    containers->batchAll(QStringLiteral("invalid"));
    QVERIFY(!containers->lastError().isEmpty());

    containers->renameContainer(QStringLiteral("abc123"), QStringLiteral("renamed"));
    QTRY_VERIFY_WITH_TIMEOUT(!containers->actionActive(), 5000);
    QVERIFY(containers->lastError().isEmpty());

    containers->duplicateContainer(QStringLiteral("abc123"));
    QTRY_VERIFY_WITH_TIMEOUT(!containers->actionActive(), 5000);
    QVERIFY(containers->lastError().isEmpty());

    containers->batchIds({QStringLiteral("abc123")}, QStringLiteral("start"));
    QTRY_VERIFY_WITH_TIMEOUT(!containers->actionActive(), 5000);
    QVERIFY(containers->lastError().isEmpty());

    containers->batchIds({}, QStringLiteral("start"));
    QTest::qWait(100);
    QVERIFY(containers->lastError().isEmpty());
}

void GuiTest::backendExposeLan()
{
    PodStack::Backend backend;
    auto *containers = backend.containers();
    const QVariantMap settingsBefore = backend.settings()->settings();

    QVariantMap spec;
    spec.insert(QStringLiteral("image"), QStringLiteral("alpine:3.20"));
    spec.insert(QStringLiteral("name"), QStringLiteral("lan"));
    spec.insert(QStringLiteral("portsText"), QStringLiteral("8080:80"));
    spec.insert(QStringLiteral("exposeLan"), true);

    const int createsBefore = m_mock.createCount();
    containers->createContainer(spec, true);
    QTRY_VERIFY_WITH_TIMEOUT(!containers->actionActive(), 5000);
    QVERIFY(!containers->lastError().isEmpty());
    QCOMPARE(m_mock.createCount(), createsBefore);

    containers->exposeContainerLan(QStringLiteral("abc123"), true);
    QTRY_VERIFY_WITH_TIMEOUT(!containers->actionActive(), 5000);
    QVERIFY(!containers->lastError().isEmpty());
    QVERIFY(backend.settings()->settings() == settingsBefore);

    QVariantMap plain;
    plain.insert(QStringLiteral("image"), QStringLiteral("alpine:3.20"));
    plain.insert(QStringLiteral("name"), QStringLiteral("local"));
    plain.insert(QStringLiteral("portsText"), QStringLiteral("8081:81"));
    containers->createContainer(plain, false);
    QTRY_VERIFY_WITH_TIMEOUT(!containers->actionActive(), 5000);
    QVERIFY(containers->lastError().isEmpty());
    const QJsonObject body = QJsonDocument::fromJson(m_mock.lastCreateBody()).object();
    const QJsonArray mappings = body.value(QStringLiteral("portmappings")).toArray();
    QCOMPARE(mappings.size(), 1);
    QCOMPARE(mappings.first().toObject().value(QStringLiteral("host_ip")).toString(),
             QStringLiteral("127.0.0.1"));
}

void GuiTest::backendContainersSignal()
{
    // O `Main.qml` só recebe a lista pelo sinal `containersChanged`; ele sumiu
    // no refactor dos controllers e a página ficou sempre em "0 containers".
    PodStack::Backend backend;
    QSignalSpy spy(&backend, &PodStack::Backend::containersChanged);

    backend.refresh();
    QTRY_VERIFY_WITH_TIMEOUT(spy.count() > 0, 5000);

    const QVariantList items = spy.last().first().toList();
    QVERIFY(!items.isEmpty());
    QCOMPARE(items.first().toMap().value(QStringLiteral("name")).toString(),
             QStringLiteral("demo"));
    QCOMPARE(backend.containerItems().size(), items.size());
}

void GuiTest::backendCreateSpec()
{
    PodStack::Backend backend;
    auto *containers = backend.containers();

    QVariantMap spec;
    spec.insert(QStringLiteral("image"), QStringLiteral("alpine:3.20"));
    spec.insert(QStringLiteral("name"), QStringLiteral("spec"));
    // Antes o parser só quebrava por \n: a vírgula do placeholder gerava um
    // único mount com destino inválido.
    spec.insert(QStringLiteral("mountsText"), QStringLiteral("/tmp:/mnt, /tmp:/mnt2:ro"));
    spec.insert(QStringLiteral("labelsText"), QStringLiteral("a=1, b=2"));
    spec.insert(QStringLiteral("restartPolicy"), QStringLiteral("on-failure"));
    spec.insert(QStringLiteral("restartTries"), 3);
    spec.insert(QStringLiteral("cpus"), 1.5);
    spec.insert(QStringLiteral("memoryMb"), 256);
    spec.insert(QStringLiteral("readOnly"), true);

    const int createsBefore = m_mock.createCount();
    containers->createContainer(spec, true);
    QTRY_VERIFY_WITH_TIMEOUT(!containers->actionActive(), 5000);
    QVERIFY2(containers->lastError().isEmpty(), qPrintable(containers->lastError()));
    QCOMPARE(m_mock.createCount(), createsBefore + 1);

    const QJsonObject body = QJsonDocument::fromJson(m_mock.lastCreateBody()).object();
    const QJsonArray mounts = body.value(QStringLiteral("mounts")).toArray();
    QCOMPARE(mounts.size(), 2);
    QCOMPARE(mounts.at(1).toObject().value(QStringLiteral("options")).toArray().last().toString(),
             QStringLiteral("ro"));
    QCOMPARE(body.value(QStringLiteral("labels")).toObject().value(QStringLiteral("a")).toString(),
             QStringLiteral("1"));
    QCOMPARE(body.value(QStringLiteral("restart_policy")).toString(), QStringLiteral("on-failure"));
    QCOMPARE(body.value(QStringLiteral("restart_tries")).toInt(), 3);
    QCOMPARE(body.value(QStringLiteral("read_only_filesystem")).toBool(), true);

    const QJsonObject limits = body.value(QStringLiteral("resource_limits")).toObject();
    QCOMPARE(
        limits.value(QStringLiteral("memory")).toObject().value(QStringLiteral("limit")).toDouble(),
        256.0 * 1024 * 1024);
    QCOMPARE(
        limits.value(QStringLiteral("cpu")).toObject().value(QStringLiteral("quota")).toDouble(),
        150000.0);
}

void GuiTest::backendCreateMissingImage()
{
    PodStack::Backend backend;
    auto *containers = backend.containers();
    QSignalSpy failed(containers, &PodStack::ContainerController::actionFailed);

    QVariantMap spec;
    spec.insert(QStringLiteral("image"), QStringLiteral("missing:tag"));
    spec.insert(QStringLiteral("name"), QStringLiteral("sem-imagem"));

    const int createsBefore = m_mock.createCount();
    containers->createContainer(spec, true);
    QTRY_VERIFY_WITH_TIMEOUT(!containers->actionActive(), 5000);

    QCOMPARE(failed.count(), 1);
    QCOMPARE(containers->lastErrorCode(), QStringLiteral("PODSTACK_IMAGE_NOT_FOUND"));
    QVERIFY(!containers->lastError().isEmpty());
    // O container nem chega a ser criado no engine.
    QCOMPARE(m_mock.createCount(), createsBefore);
}

void GuiTest::backendStreams()
{
    PodStack::Backend backend;
    auto *containers = backend.containers();

    containers->subscribeLogs(QStringLiteral("abc123"), 10, false);
    QTRY_VERIFY_WITH_TIMEOUT(containers->logLines().size() >= 2, 5000);
    containers->unsubscribeLogs();

    containers->subscribeStats(QStringLiteral("abc123"));
    QTRY_VERIFY_WITH_TIMEOUT(containers->stats()
                                 .value(QStringLiteral("cpu_percent"))
                                 .toString()
                                 .contains(QStringLiteral("1.5")),
                             5000);
    QCOMPARE(containers->cpuHistory().size(), 1);
    QCOMPARE(containers->memHistory().size(), 1);
    containers->unsubscribeStats();

    QSignalSpy filesSpy(containers, &PodStack::ContainerController::filesChanged);
    containers->listFiles(QStringLiteral("abc123"), QStringLiteral("/"));
    QTRY_VERIFY_WITH_TIMEOUT(filesSpy.count() > 0, 5000);
    QCOMPARE(containers->files().size(), 2);
}

void GuiTest::backendContainerDetail()
{
    PodStack::Backend backend;
    auto *detail = backend.containerDetail();
    QSignalSpy detailSpy(detail, &PodStack::ContainerDetailController::detailChanged);

    detail->load(QStringLiteral("abc123"));
    QVERIFY(detail->loading());
    QTRY_VERIFY_WITH_TIMEOUT(!detail->loading(), 5000);
    QVERIFY2(detail->error().isEmpty(), qPrintable(detail->error()));
    QVERIFY(detailSpy.count() > 0);

    const QVariantMap data = detail->detail();
    QCOMPARE(data.value(QStringLiteral("Id")).toString(), QStringLiteral("abc123"));
    const QVariantMap config = data.value(QStringLiteral("Config")).toMap();
    const QVariantList env = config.value(QStringLiteral("Env")).toList();
    QCOMPARE(env.size(), 3);
    QCOMPARE(env.first().toString(), QStringLiteral("A=B"));
    QCOMPARE(env.at(1).toString(), QStringLiteral("TOKEN=super-secret"));

    const QVariantList mounts = data.value(QStringLiteral("Mounts")).toList();
    QCOMPARE(mounts.size(), 2);
    QCOMPARE(mounts.first().toMap().value(QStringLiteral("Destination")).toString(),
             QStringLiteral("/mnt"));
    QCOMPARE(mounts.at(1).toMap().value(QStringLiteral("Name")).toString(), QStringLiteral("data"));

    const QVariantMap networks = data.value(QStringLiteral("NetworkSettings"))
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
    detail->copyInspect();

    detail->load(QStringLiteral("missing123"));
    QTRY_VERIFY_WITH_TIMEOUT(!detail->loading(), 5000);
    QVERIFY(!detail->error().isEmpty());

    detail->startStats(QStringLiteral("abc123"));
    QTRY_VERIFY_WITH_TIMEOUT(detail->stats()
                                 .value(QStringLiteral("cpu_percent"))
                                 .toString()
                                 .contains(QStringLiteral("1.5")),
                             5000);
    QCOMPARE(detail->cpuHistory().size(), 1);
    QCOMPARE(detail->memHistory().size(), 1);
    QCOMPARE(detail->netRxHistory().size(), 1);
    QCOMPARE(detail->netTxHistory().size(), 1);
    QCOMPARE(detail->diskReadHistory().size(), 1);
    QCOMPARE(detail->diskWriteHistory().size(), 1);
    QVERIFY(detail->stats().value(QStringLiteral("netRxBytes")).toLongLong() > 0);
    QVERIFY(detail->statsError().isEmpty());
    detail->stopStats();

    QSignalSpy logsSpy(detail, &PodStack::ContainerDetailController::logsChanged);
    detail->startLogs(QStringLiteral("abc123"), 10, false);
    QTRY_VERIFY_WITH_TIMEOUT(detail->logLines().size() >= 2, 5000);
    QVERIFY(logsSpy.count() > 0);
    QVERIFY(detail->logsError().isEmpty());
    detail->clearLogs();
    QVERIFY(detail->logLines().isEmpty());
    detail->stopLogs();

    detail->lifecycle(QStringLiteral("invalid"), QStringLiteral("abc123"));
    QVERIFY(!detail->lastError().isEmpty());
}

void GuiTest::backendContainerDetailRace()
{
    PodStack::Backend backend;
    auto *detail = backend.containerDetail();

    // Dois cliques rápidos: só o último alvo pode aparecer. "first123" não
    // existe (volta erro); "abc123" existe. Sem a guarda de corrida, a
    // resposta atrasada do primeiro inspect sobrescreve o segundo.
    detail->load(QStringLiteral("first123"));
    detail->load(QStringLiteral("abc123"));

    QTRY_VERIFY_WITH_TIMEOUT(!detail->loading(), 5000);
    QTRY_COMPARE_WITH_TIMEOUT(detail->detail().value(QStringLiteral("Id")).toString(),
                              QStringLiteral("abc123"), 5000);
    QVERIFY2(detail->error().isEmpty(), qPrintable(detail->error()));

    // O mesmo alvo em sequência não deve trocar o conteúdo exibido.
    detail->load(QStringLiteral("abc123"));
    QTRY_VERIFY_WITH_TIMEOUT(!detail->loading(), 5000);
    QCOMPARE(detail->detail().value(QStringLiteral("Id")).toString(),
             QStringLiteral("abc123"));
}

void GuiTest::backendKubernetes()
{
    PodStack::Backend backend;
    auto *k8s = backend.k8s();
    QSignalSpy spy(k8s, &PodStack::K8sController::changed);

    k8s->refresh();
    QTRY_VERIFY_WITH_TIMEOUT(spy.count() > 0, 5000);
    QTRY_VERIFY_WITH_TIMEOUT(k8s->state() == QStringLiteral("disabled"), 5000);
    QVERIFY2(!k8s->enabled(), qPrintable(k8s->state() + QStringLiteral(" | ") + k8s->error()));
    QVERIFY(k8s->pods().isEmpty());
    QVERIFY(k8s->services().isEmpty());

    k8s->startWatch();
    k8s->stopWatch();

    k8s->startPortForward(QStringLiteral("prod"), QStringLiteral("web"), 8080, 80);
    QTRY_VERIFY_WITH_TIMEOUT(!k8s->portForwardError().isEmpty(), 5000);
    QVERIFY(!k8s->portForwardActive());
    k8s->stopPortForward();
    k8s->deletePod(QStringLiteral("prod"), QStringLiteral("web"));
    k8s->deleteService(QStringLiteral("prod"), QStringLiteral("web"));
}

void GuiTest::backendControllerEdges()
{
    PodStack::Backend backend;

    // Validações e early-returns dos controllers (não batem no daemon).
    backend.machines()->cancelCreate();
    backend.machines()->resetCreateState();
    backend.machines()->create({});
    backend.machines()->archivePath(QString());
    backend.machines()->exportTo(QString(), QString());
    backend.machines()->importFrom(QString());
    backend.machines()->setPorts(QString(), {});
    backend.machines()->setAudio(QString(), true);
    backend.machines()->refreshSnapshots(QString());
    backend.machines()->createSnapshot(QString(), QString());
    backend.machines()->restoreSnapshot(QString(), QString());
    backend.machines()->removeSnapshot(QString(), QString());
    backend.machines()->openSsh(QString());
    backend.machines()->attachSerialPort(QString(), QString());
    backend.machines()->detachSerialPort(QString(), QString());
    backend.machines()->refreshSerialPorts(QString());
    backend.machines()->attachUsbDevice(QString(), 0, 0);
    backend.machines()->detachUsbDevice(QString(), 0, 0);
    backend.machines()->refreshUsbDevices(QString());
    backend.machines()->lifecycle(QString(), QString());
    backend.machines()->remove(QString(), false);
    backend.machines()->rename(QString(), QString());
    backend.machines()->clone(QString(), QString());
    backend.machines()->batch({}, QStringLiteral("start"));

    backend.containers()->listFiles(QString(), QStringLiteral("/"));
    backend.containers()->downloadFile(QString(), QString());
    backend.containers()->uploadFile(QString(), QString(), QString());
    backend.containers()->clearLogs();
    backend.containers()->unsubscribeLogs();
    backend.containers()->unsubscribeStats();
    backend.containers()->subscribeLogs(QString(), 0, false);
    backend.containers()->subscribeStats(QString());
    backend.containers()->batchIds({}, QStringLiteral("start"));

    backend.containerDetail()->startLogs(QString(), 0, false);
    backend.containerDetail()->startStats(QString());
    backend.containerDetail()->remove(QString(), false);
    backend.containerDetail()->copyInspect();

    backend.images()->cancelPull();
    backend.images()->refresh();
    backend.images()->batchImages({}, false);

    backend.compose()->unsubscribeComposeLogs();
    backend.compose()->batchCompose({}, QStringLiteral("bogus"));

    backend.builds()->cancelBuild();
    backend.builds()->batchBuilds({});
    backend.builds()->refresh();

    backend.volumes()->batchVolumes({}, false);
    backend.networks()->batchNetworks({}, false);
    backend.pods()->batchPods({}, QStringLiteral("bogus"));

    backend.k8s()->startLogs(QString(), QString());
    backend.k8s()->stopLogs();
    backend.k8s()->clearLogs();
    backend.k8s()->deletePod(QString(), QString());
    backend.k8s()->deleteService(QString(), QString());
    backend.k8s()->stopPortForward();

    backend.settings()->resetSetting(QString());
    backend.settings()->refresh();

    backend.system()->loadLogs(QString(), QString(), 0);
    backend.system()->clearLogs();

    backend.updates()->loadState();
    backend.updates()->clearState();
    backend.updates()->check(false);

    QTest::qWait(100);
}

void GuiTest::backendKubernetesEnabledError()
{
    PodStack::K8sStore store(m_testDataDir + QStringLiteral("/data/podstack/podstack.db"));
    PodStack::Error error;
    QVERIFY2(store.open(&error), qPrintable(error.message));
    PodStack::K8sCluster cluster;
    cluster.id = QStringLiteral("k8s-default");
    cluster.name = QStringLiteral("default");
    cluster.state = QStringLiteral("ready");
    cluster.vmMachineId = QStringLiteral("missing-vm");
    cluster.createdAt = QStringLiteral("2026-01-01T00:00:00.000Z");
    cluster.updatedAt = cluster.createdAt;
    QVERIFY2(store.upsert(cluster, &error), qPrintable(error.message));

    PodStack::Backend backend;
    auto *k8s = backend.k8s();
    QSignalSpy spy(k8s, &PodStack::K8sController::changed);
    k8s->refresh();
    QTRY_VERIFY_WITH_TIMEOUT(spy.count() > 0, 5000);
    QTRY_VERIFY_WITH_TIMEOUT(k8s->enabled(), 5000);

    k8s->refresh();
    QTRY_VERIFY_WITH_TIMEOUT(!k8s->enabled(), 5000);
    QVERIFY(!k8s->error().isEmpty());

    QVERIFY(store.remove(QStringLiteral("default"), &error));
}

void GuiTest::backendKubernetesDisableJob()
{
    PodStack::Backend backend;
    auto *k8s = backend.k8s();
    QSignalSpy jobSpy(k8s, &PodStack::K8sController::jobChanged);

    k8s->disable(true);
    QTRY_VERIFY_WITH_TIMEOUT(!k8s->jobActive(), 5000);
    QTRY_VERIFY_WITH_TIMEOUT(k8s->state() == QStringLiteral("disabled"), 5000);
    QVERIFY(k8s->jobError().isEmpty());
    QVERIFY(jobSpy.count() > 0);

    k8s->reset();
    QTRY_VERIFY_WITH_TIMEOUT(!k8s->jobActive(), 5000);
    QCOMPARE(k8s->state(), QStringLiteral("disabled"));
}

void GuiTest::backendRefreshUpdatesCatalog()
{
    PodStack::Backend backend;
    auto *compose = backend.compose();
    QSignalSpy composeSpy(compose, &PodStack::ComposeController::itemsChanged);
    QSignalSpy imagesSpy(backend.images(), &PodStack::ImageController::itemsChanged);

    // Espera o boot terminar, então mede só o refresh explícito.
    QTRY_VERIFY_WITH_TIMEOUT(compose->items().size() >= 1, 5000);
    composeSpy.clear();
    imagesSpy.clear();

    // Um evento/refresh de containers precisa atualizar o catálogo: subir uma
    // stack por fora deixava o Compose vazio até sair e voltar para a página.
    backend.refresh();

    QTRY_VERIFY_WITH_TIMEOUT(composeSpy.count() > 0, 5000);
    QTRY_VERIFY_WITH_TIMEOUT(imagesSpy.count() > 0, 5000);
    QCOMPARE(compose->items().size(), 1);
}

void GuiTest::backendCatalog()
{
    PodStack::Backend backend;
    QSignalSpy imagesSpy(backend.images(), &PodStack::ImageController::itemsChanged);
    QSignalSpy volumesSpy(backend.volumes(), &PodStack::VolumeController::itemsChanged);
    QSignalSpy networksSpy(backend.networks(), &PodStack::NetworkController::itemsChanged);
    QSignalSpy podsSpy(backend.pods(), &PodStack::PodController::itemsChanged);
    auto *containers = backend.containers();
    QSignalSpy pullSpy(backend.images(), &PodStack::ImageController::pullStateChanged);
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
    QCOMPARE(preview.value(QStringLiteral("images")).toMap().value(QStringLiteral("count")).toInt(),
             1);
    QCOMPARE(
        preview.value(QStringLiteral("volumes")).toMap().value(QStringLiteral("count")).toInt(), 1);
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

    QCOMPARE(backend.images()->items().size(), 2);
    QCOMPARE(backend.volumes()->items().size(), 1);
    QCOMPARE(backend.networks()->items().size(), 1);
    QCOMPARE(backend.pods()->items().size(), 1);
    QCOMPARE(backend.images()->items().first().toMap().value(QStringLiteral("name")).toString(),
             QStringLiteral("docker.io/library/alpine:3.20"));

    backend.images()->pullImage(QStringLiteral("alpine"), {});
    QTRY_VERIFY_WITH_TIMEOUT(!backend.images()->pullActive() && pullSpy.count() > 0, 5000);
    QVERIFY(backend.images()->pullStatus().contains(QStringLiteral("concluído")));

    backend.images()->tagImage(QStringLiteral("img1234567890"), QStringLiteral("repo:v1"));
    QTRY_VERIFY_WITH_TIMEOUT(
        !backend.images()->actionActive() && backend.images()->lastError().isEmpty(), 5000);

    backend.images()->untagImage(QStringLiteral("img1234567890"), QStringLiteral("repo:v1"));
    QTRY_VERIFY_WITH_TIMEOUT(
        !backend.images()->actionActive() && backend.images()->lastError().isEmpty(), 5000);

    backend.images()->removeImage(QStringLiteral("img1234567890"), false);
    QTRY_VERIFY_WITH_TIMEOUT(
        !backend.images()->actionActive() && backend.images()->lastError().isEmpty(), 5000);

    backend.images()->pruneImages(false);
    QTRY_VERIFY_WITH_TIMEOUT(
        !backend.images()->actionActive() && backend.images()->lastError().isEmpty(), 5000);

    backend.volumes()->createVolume(QStringLiteral("vol2"), QStringLiteral("local"));
    QTRY_VERIFY_WITH_TIMEOUT(
        !backend.volumes()->actionActive() && backend.volumes()->lastError().isEmpty(), 5000);

    backend.volumes()->removeVolume(QStringLiteral("vol1"), false);
    QTRY_VERIFY_WITH_TIMEOUT(
        !backend.volumes()->actionActive() && backend.volumes()->lastError().isEmpty(), 5000);

    backend.volumes()->pruneVolumes();
    QTRY_VERIFY_WITH_TIMEOUT(
        !backend.volumes()->actionActive() && backend.volumes()->lastError().isEmpty(), 5000);

    backend.networks()->createNetwork(QStringLiteral("mock-net"), QStringLiteral("10.90.0.0/24"),
                                      {});
    QTRY_VERIFY_WITH_TIMEOUT(
        !backend.networks()->actionActive() && backend.networks()->lastError().isEmpty(), 5000);

    backend.networks()->connectNetwork(QStringLiteral("mock-net"), QStringLiteral("abc123"));
    QTRY_VERIFY_WITH_TIMEOUT(
        !backend.networks()->actionActive() && backend.networks()->lastError().isEmpty(), 5000);

    backend.networks()->disconnectNetwork(QStringLiteral("mock-net"), QStringLiteral("abc123"));
    QTRY_VERIFY_WITH_TIMEOUT(
        !backend.networks()->actionActive() && backend.networks()->lastError().isEmpty(), 5000);

    backend.networks()->removeNetwork(QStringLiteral("mock-net"), false);
    QTRY_VERIFY_WITH_TIMEOUT(
        !backend.networks()->actionActive() && backend.networks()->lastError().isEmpty(), 5000);

    backend.pods()->createPod(QStringLiteral("demo-pod"));
    QTRY_VERIFY_WITH_TIMEOUT(
        !backend.pods()->actionActive() && backend.pods()->lastError().isEmpty(), 5000);

    backend.pods()->podAction(QStringLiteral("start"), QStringLiteral("pod123"));
    QTRY_VERIFY_WITH_TIMEOUT(
        !backend.pods()->actionActive() && backend.pods()->lastError().isEmpty(), 5000);

    backend.pods()->removePod(QStringLiteral("pod123"), true);
    QTRY_VERIFY_WITH_TIMEOUT(
        !backend.pods()->actionActive() && backend.pods()->lastError().isEmpty(), 5000);

    const QString uploadPath =
        QDir::tempPath() +
        QStringLiteral("/podstack-gui-upload-%1.txt").arg(QCoreApplication::applicationPid());
    QFile uploadFile(uploadPath);
    QVERIFY(uploadFile.open(QIODevice::WriteOnly));
    uploadFile.write("gui-upload");
    uploadFile.close();
    backend.containers()->uploadFile(QStringLiteral("abc123"), QStringLiteral("/tmp"), uploadPath);
    QTRY_VERIFY_WITH_TIMEOUT(
        !backend.containers()->actionActive() && backend.containers()->lastError().isEmpty(), 5000);
    QFile::remove(uploadPath);

    const QString hostDir = QStringLiteral("/tmp/podstack-host-src");
    QDir(hostDir).removeRecursively();
    QDir().mkpath(hostDir);
    backend.volumes()->importHostPath(QStringLiteral("vol2"), hostDir);
    QTRY_VERIFY_WITH_TIMEOUT(
        !backend.volumes()->actionActive() && backend.volumes()->lastError().isEmpty(), 5000);
    QDir(hostDir).removeRecursively();
}

void GuiTest::backendMachines()
{
    PodStack::Backend backend;
    QSignalSpy machinesSpy(backend.machines(), &PodStack::MachineController::itemsChanged);

    QTRY_VERIFY_WITH_TIMEOUT(machinesSpy.count() > 0, 5000);
    QCOMPARE(backend.machines()->items().size(), 0);

    QSignalSpy distrosSpy(backend.machines(), &PodStack::MachineController::distrosChanged);
    backend.machines()->refreshDistros();
    QTRY_VERIFY_WITH_TIMEOUT(distrosSpy.count() > 0, 5000);
    QVERIFY(backend.machines()->distros().size() >= 3);
    QVERIFY(backend.machines()->distros().first().toMap().contains(QStringLiteral("label")));

    backend.machines()->loadInfo(QStringLiteral("missing-machine"));
    QTRY_VERIFY_WITH_TIMEOUT(!backend.machines()->lastError().isEmpty(), 5000);

    QSignalSpy detailErrorSpy(backend.machineDetail(),
                              &PodStack::MachineDetailController::lastErrorChanged);
    int errorsBefore = detailErrorSpy.count();
    backend.machineDetail()->loadLogs(QStringLiteral("missing-machine"), 10);
    QTRY_VERIFY_WITH_TIMEOUT(detailErrorSpy.count() > errorsBefore, 5000);

    QSignalSpy machineErrorSpy(backend.machines(), &PodStack::MachineController::lastErrorChanged);
    backend.machines()->lifecycle(QStringLiteral("start"), QStringLiteral("missing-machine"));
    QTRY_VERIFY_WITH_TIMEOUT(
        !backend.machines()->actionActive() && !backend.machines()->lastError().isEmpty(), 5000);

    backend.machines()->remove(QStringLiteral("missing-machine"), true);
    QTRY_VERIFY_WITH_TIMEOUT(
        !backend.machines()->actionActive() && !backend.machines()->lastError().isEmpty(), 5000);

    errorsBefore = machineErrorSpy.count();
    backend.machines()->refreshSerialPorts(QStringLiteral("missing-machine"));
    QTRY_VERIFY_WITH_TIMEOUT(machineErrorSpy.count() > errorsBefore, 5000);

    backend.machines()->attachSerialPort(QStringLiteral("missing-machine"),
                                         QStringLiteral("/dev/null"));
    QTRY_VERIFY_WITH_TIMEOUT(
        !backend.machines()->actionActive() && !backend.machines()->lastError().isEmpty(), 5000);
    backend.machines()->detachSerialPort(QStringLiteral("missing-machine"),
                                         QStringLiteral("/dev/null"));
    QTRY_VERIFY_WITH_TIMEOUT(
        !backend.machines()->actionActive() && !backend.machines()->lastError().isEmpty(), 5000);

    errorsBefore = machineErrorSpy.count();
    backend.machines()->refreshUsbDevices(QStringLiteral("missing-machine"));
    QTRY_VERIFY_WITH_TIMEOUT(machineErrorSpy.count() > errorsBefore, 5000);
    backend.machines()->attachUsbDevice(QStringLiteral("missing-machine"), 1, 4);
    QTRY_VERIFY_WITH_TIMEOUT(
        !backend.machines()->actionActive() && !backend.machines()->lastError().isEmpty(), 5000);
    backend.machines()->detachUsbDevice(QStringLiteral("missing-machine"), 1, 4);
    QTRY_VERIFY_WITH_TIMEOUT(
        !backend.machines()->actionActive() && !backend.machines()->lastError().isEmpty(), 5000);
    backend.machines()->setAudio(QStringLiteral("missing-machine"), true);
    QTRY_VERIFY_WITH_TIMEOUT(
        !backend.machines()->actionActive() && !backend.machines()->lastError().isEmpty(), 5000);

    backend.machines()->openSsh(QString());
    QCOMPARE(backend.machines()->lastError(), QStringLiteral("Machine inválida."));
    backend.machines()->openSsh(QStringLiteral("missing-machine"));
    QTRY_VERIFY_WITH_TIMEOUT(
        backend.machines()->lastError().contains(QStringLiteral("não encontrada")), 5000);

    backend.machines()->create({});
    QVERIFY(!backend.machines()->createActive());
    QVERIFY(!backend.machines()->createError().isEmpty());

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

    const QString machineDir = m_testDataDir + QStringLiteral("/data/podstack/machines/") + meta.id;
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

    backend.machines()->refresh();
    QTRY_COMPARE_WITH_TIMEOUT(backend.machines()->items().size(), 1, 5000);
    const QVariantMap item = backend.machines()->items().first().toMap();
    QCOMPARE(item.value(QStringLiteral("name")).toString(), QStringLiteral("gui-logs"));
    QCOMPARE(item.value(QStringLiteral("distroLabel")).toString(), QStringLiteral("debian:12"));
    QCOMPARE(item.value(QStringLiteral("ssh")).toString(),
             QStringLiteral("podstack@127.0.0.1:32222"));

    backend.machines()->loadInfo(meta.id);
    QTRY_COMPARE_WITH_TIMEOUT(backend.machines()->info().value(QStringLiteral("id")).toString(),
                              meta.id, 5000);
    QCOMPARE(backend.machines()->info().value(QStringLiteral("state")).toString(),
             QStringLiteral("stopped"));

    backend.machineDetail()->loadLogs(meta.id, 2);
    QTRY_COMPARE_WITH_TIMEOUT(backend.machineDetail()->logs().size(), 2, 5000);
    QCOMPARE(backend.machineDetail()->logs().first(), QStringLiteral("gui boot 2"));
    QCOMPARE(backend.machineDetail()->logs().last(), QStringLiteral("gui boot 3"));

    // Compatibilidade: o Backend continua emitindo `terminalRequested` (dock/
    // TerminalWindow); a GUI usa `MainWindow::openMachineDetail`, coberto em
    // `test_terminal::machineDetailDialog*`.
    QSignalSpy terminalSpy(&backend, &PodStack::Backend::terminalRequested);
    backend.machines()->openSsh(meta.id);
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
    backend.machines()->update(
        QVariantMap{{QStringLiteral("id"), meta.id}, {QStringLiteral("forwardSshAgent"), false}});
    QTRY_VERIFY_WITH_TIMEOUT(
        !backend.machines()->actionActive() && backend.machines()->lastError().isEmpty(), 5000);
    backend.machines()->loadInfo(meta.id);
    QTRY_VERIFY_WITH_TIMEOUT(
        !backend.machines()->info().value(QStringLiteral("forwardSshAgent")).toBool(), 5000);

    QVERIFY(store.remove(meta.id));
    QDir(machineDir).removeRecursively();
}

void GuiTest::backendMachineStats()
{
    PodStack::Backend backend;
    QSignalSpy statsSpy(backend.machineDetail(), &PodStack::MachineDetailController::statsChanged);
    QSignalSpy errorSpy(backend.machineDetail(),
                        &PodStack::MachineDetailController::lastErrorChanged);

    backend.machineDetail()->startStats(QString());
    QVERIFY(!backend.machineDetail()->statsError().isEmpty());
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

    const QString machineDir = m_testDataDir + QStringLiteral("/data/podstack/machines/") + meta.id;
    QVERIFY(QDir().mkpath(machineDir));
    meta.sshKeyPath = machineDir + QStringLiteral("/id_ed25519");
    QFile keyFile(meta.sshKeyPath);
    QVERIFY(keyFile.open(QIODevice::WriteOnly | QIODevice::Truncate));
    keyFile.write("test-key\n");
    keyFile.close();
    QVERIFY(keyFile.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner));
    QVERIFY2(store.upsert(meta, &storeError), qPrintable(storeError.message));

    backend.machineDetail()->startStats(meta.id);
    QTRY_VERIFY_WITH_TIMEOUT(
        backend.machineDetail()->stats().value(QStringLiteral("memTotal")).toLongLong() > 0, 7000);
    QVERIFY(statsSpy.count() > 0);
    QCOMPARE(backend.machineDetail()->stats().value(QStringLiteral("memTotal")).toLongLong(),
             2147483648LL);
    QCOMPARE(backend.machineDetail()->stats().value(QStringLiteral("diskPercent")).toDouble(),
             25.0);
    QCOMPARE(backend.machineDetail()->cpuHistory().size(), 1);
    QCOMPARE(backend.machineDetail()->memHistory().size(), 1);
    QCOMPARE(backend.machineDetail()->netRxHistory().size(), 1);
    QCOMPARE(backend.machineDetail()->netTxHistory().size(), 1);
    QVERIFY(backend.machineDetail()->statsError().isEmpty());

    backend.machineDetail()->stopStats();
    const int samplesAfterStop = backend.machineDetail()->cpuHistory().size();
    QTest::qWait(2300);
    QCOMPARE(backend.machineDetail()->cpuHistory().size(), samplesAfterStop);

    // Erros do stream de stats da machine não vazam para o lastError global
    // (o inspector lateral de containers exibe backend.lastError).
    PodStack::Backend statsOnly;
    QSignalSpy statsOnlyErrorSpy(statsOnly.machineDetail(),
                                 &PodStack::MachineDetailController::lastErrorChanged);
    statsOnly.machineDetail()->startStats(QStringLiteral("missing-machine-stats"));
    QTRY_VERIFY_WITH_TIMEOUT(!statsOnly.machineDetail()->statsError().isEmpty(), 5000);
    QVERIFY(statsOnly.lastError().isEmpty());
    QCOMPARE(statsOnlyErrorSpy.count(), 0);
    statsOnly.machineDetail()->stopStats();
    QVERIFY(statsOnly.machineDetail()->statsError().isEmpty());

    QVERIFY(store.remove(meta.id));
    QDir(machineDir).removeRecursively();
}

void GuiTest::backendMachineConsole()
{
    PodStack::Backend backend;
    QSignalSpy consoleSpy(backend.machineDetail(),
                          &PodStack::MachineDetailController::consoleChanged);
    QSignalSpy errorSpy(&backend, &PodStack::Backend::lastErrorChanged);

    backend.machineDetail()->followConsole(QString(), 1024);
    QVERIFY(!backend.machineDetail()->consoleActive());
    QVERIFY(!backend.machineDetail()->consoleError().isEmpty());

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

    const QString machineDir = m_testDataDir + QStringLiteral("/data/podstack/machines/") + meta.id;
    QVERIFY(QDir().mkpath(machineDir));
    QVERIFY2(store.upsert(meta, &storeError), qPrintable(storeError.message));

    const QString consolePath = machineDir + QStringLiteral("/console.log");
    QFile console(consolePath);
    QVERIFY(console.open(QIODevice::WriteOnly | QIODevice::Truncate));
    console.write("serial 1\nserial 2\n");
    console.close();

    backend.machineDetail()->followConsole(meta.id, 1024);
    QVERIFY(backend.machineDetail()->consoleActive());
    QTRY_COMPARE_WITH_TIMEOUT(backend.machineDetail()->consoleText(),
                              QStringLiteral("serial 1\nserial 2\n"), 5000);
    QVERIFY(backend.machineDetail()->consoleError().isEmpty());
    QVERIFY(consoleSpy.count() > 0);

    QFile appendConsole(consolePath);
    QVERIFY(appendConsole.open(QIODevice::Append));
    appendConsole.write("serial 3\n");
    appendConsole.close();
    QTRY_VERIFY_WITH_TIMEOUT(
        backend.machineDetail()->consoleText().contains(QStringLiteral("serial 3")), 5000);

    backend.machineDetail()->stopConsole();
    QVERIFY(!backend.machineDetail()->consoleActive());
    QVERIFY(backend.machineDetail()->consoleText().isEmpty());
    QVERIFY(backend.machineDetail()->consoleError().isEmpty());

    QFile afterStop(consolePath);
    QVERIFY(afterStop.open(QIODevice::Append));
    afterStop.write("serial 4\n");
    afterStop.close();
    QTest::qWait(700);
    QVERIFY(backend.machineDetail()->consoleText().isEmpty());

    backend.machineDetail()->followConsole(QStringLiteral("missing-machine-console"), 1024);
    QTRY_VERIFY_WITH_TIMEOUT(!backend.machineDetail()->consoleError().isEmpty(), 5000);
    QVERIFY(!backend.machineDetail()->consoleActive());
    QVERIFY(backend.lastError().isEmpty());
    QCOMPARE(errorSpy.count(), 0);
    backend.machineDetail()->stopConsole();

    QVERIFY(store.remove(meta.id));
    QDir(machineDir).removeRecursively();
}

void GuiTest::backendMachineFiles()
{
    PodStack::Backend backend;
    QSignalSpy filesSpy(backend.machineDetail(), &PodStack::MachineDetailController::filesChanged);

    backend.machineDetail()->refreshFiles(QString(), QStringLiteral("/"));
    QVERIFY(!backend.machineDetail()->filesError().isEmpty());
    QVERIFY(!backend.machineDetail()->filesBusy());

    backend.machineDetail()->refreshFiles(QStringLiteral("missing-machine"), QStringLiteral("/"));
    QTRY_VERIFY_WITH_TIMEOUT(!backend.machineDetail()->filesBusy(), 5000);
    QVERIFY(backend.machineDetail()->filesError().contains(QStringLiteral("não encontrada")));

    backend.machineDetail()->downloadFile(QStringLiteral("missing-machine"),
                                          QStringLiteral("/tmp/report.txt"),
                                          QStringLiteral("report.txt"));
    QTRY_VERIFY_WITH_TIMEOUT(!backend.machineDetail()->filesBusy(), 5000);
    QVERIFY(backend.machineDetail()->filesError().contains(QStringLiteral("não encontrada")));

    const QString uploadSource = m_testDataDir + QStringLiteral("/gui-upload.txt");
    QFile upload(uploadSource);
    QVERIFY(upload.open(QIODevice::WriteOnly | QIODevice::Truncate));
    upload.write("gui upload");
    upload.close();

    backend.machineDetail()->uploadFile(QStringLiteral("missing-machine"), QStringLiteral("/tmp"),
                                        uploadSource);
    QTRY_VERIFY_WITH_TIMEOUT(!backend.machineDetail()->filesBusy(), 5000);
    QVERIFY(backend.machineDetail()->filesError().contains(QStringLiteral("não encontrada")));

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

    const QString machineDir = m_testDataDir + QStringLiteral("/data/podstack/machines/") + meta.id;
    QVERIFY(QDir().mkpath(machineDir));
    meta.sshKeyPath = machineDir + QStringLiteral("/id_ed25519");
    QFile keyFile(meta.sshKeyPath);
    QVERIFY(keyFile.open(QIODevice::WriteOnly | QIODevice::Truncate));
    keyFile.write("test-key\n");
    keyFile.close();
    QVERIFY(keyFile.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner));
    QVERIFY2(store.upsert(meta, &storeError), qPrintable(storeError.message));

    backend.machineDetail()->refreshFiles(meta.id, QStringLiteral("/home"));
    QTRY_COMPARE_WITH_TIMEOUT(backend.machineDetail()->files().size(), 2, 5000);
    QVERIFY(!backend.machineDetail()->filesBusy());
    QVERIFY2(backend.machineDetail()->filesError().isEmpty(),
             qPrintable(backend.machineDetail()->filesError()));
    QCOMPARE(backend.machineDetail()->filesPath(), QStringLiteral("/home"));
    QVERIFY(filesSpy.count() > 0);

    QStringList names;
    for (const QVariant &value : backend.machineDetail()->files()) {
        const QVariantMap item = value.toMap();
        names.append(item.value(QStringLiteral("name")).toString());
        QVERIFY(item.contains(QStringLiteral("path")));
        QVERIFY(item.contains(QStringLiteral("directory")));
        QVERIFY(item.contains(QStringLiteral("size")));
        QVERIFY(item.contains(QStringLiteral("modified")));
    }
    QVERIFY(names.contains(QStringLiteral("docs")));
    QVERIFY(names.contains(QStringLiteral("gui.txt")));

    backend.machineDetail()->downloadFile(meta.id, QStringLiteral("/home/podstack/gui.txt"),
                                          QStringLiteral("gui.txt"));
    QTRY_VERIFY_WITH_TIMEOUT(!backend.machineDetail()->filesBusy(), 5000);
    QVERIFY2(backend.machineDetail()->filesError().isEmpty(),
             qPrintable(backend.machineDetail()->filesError()));
    const QString downloadDir = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    QVERIFY2(downloadDir.startsWith(m_testDataDir), qPrintable(downloadDir));
    const QString downloaded = downloadDir + QStringLiteral("/gui.txt");
    QVERIFY(QFileInfo::exists(downloaded));

    backend.machineDetail()->downloadFile(meta.id, QStringLiteral("/home/podstack/gui.txt"),
                                          QStringLiteral("../escape.txt"));
    QVERIFY(backend.machineDetail()->filesError().contains(QStringLiteral("inválido")));
    QVERIFY(!backend.machineDetail()->filesBusy());

    backend.machineDetail()->uploadFile(meta.id, QStringLiteral("/home/podstack"), uploadSource);
    QTRY_VERIFY_WITH_TIMEOUT(!backend.machineDetail()->filesBusy(), 5000);
    QVERIFY2(backend.machineDetail()->filesError().isEmpty(),
             qPrintable(backend.machineDetail()->filesError()));

    QVERIFY(store.remove(meta.id));
    QDir(machineDir).removeRecursively();
    QFile::remove(uploadSource);
    QFile::remove(downloaded);
}

void GuiTest::backendTerminal()
{
    PodStack::Backend backend;
    QSignalSpy terminalSpy(&backend, &PodStack::Backend::terminalRequested);
    QSignalSpy errorSpy(backend.machines(), &PodStack::MachineController::lastErrorChanged);
    QSignalSpy containerErrorSpy(backend.containers(),
                                 &PodStack::ContainerController::lastErrorChanged);

    backend.containers()->openTerminal(QStringLiteral("abc123"));
    QCOMPARE(terminalSpy.count(), 1);
    const QList<QVariant> request = terminalSpy.takeFirst();
    QCOMPARE(request.at(0).toString(), QStringLiteral("abc123"));
    QCOMPARE(request.at(1).toString(), QStringLiteral("podman"));
    QCOMPARE(request.at(2).toStringList(),
             QStringList({QStringLiteral("exec"), QStringLiteral("-it"), QStringLiteral("abc123"),
                          QStringLiteral("/bin/sh")}));

    backend.containers()->openTerminal(QString());
    QCOMPARE(terminalSpy.count(), 0);
    QCOMPARE(backend.containers()->lastError(), QStringLiteral("Container inválido."));
    QVERIFY(containerErrorSpy.count() > 0);

    QSignalSpy machineErrorSpy(backend.machines(), &PodStack::MachineController::lastErrorChanged);
    const int errorsBefore = machineErrorSpy.count();
    backend.machines()->openSsh(QStringLiteral("missing-machine"));
    QTRY_VERIFY_WITH_TIMEOUT(machineErrorSpy.count() > errorsBefore, 5000);
    QVERIFY(backend.machines()->lastError().contains(QStringLiteral("não encontrada")));
    // O caminho de erro não abre sessão; o sucesso visual do QTermWidget é manual.
    QCOMPARE(terminalSpy.count(), 0);
}

void GuiTest::backendBuildCancelIdle()
{
    PodStack::Backend backend;
    auto *builds = backend.builds();

    // Sem build em andamento, cancelar é no-op: não pode mentir "Cancelando"
    // nem deixar o botão preso (regressão do botão que ficava travado).
    QVERIFY(!builds->buildActive());
    builds->cancelBuild();
    QVERIFY(!builds->buildActive());
    QVERIFY(!builds->buildStatus().contains(QStringLiteral("Cancelando")));
}

void GuiTest::backendM3()
{
    PodStack::Backend backend;
    QSignalSpy composeSpy(backend.compose(), &PodStack::ComposeController::itemsChanged);
    QSignalSpy composeLogsSpy(backend.compose(), &PodStack::ComposeController::logLinesChanged);
    QSignalSpy buildSpy(backend.builds(), &PodStack::BuildController::buildStateChanged);
    QSignalSpy buildsSpy(backend.builds(), &PodStack::BuildController::itemsChanged);
    QSignalSpy eventsSpy(&backend, &PodStack::Backend::eventsChanged);
    auto *containers = backend.containers();

    backend.compose()->refresh();
    QTRY_VERIFY_WITH_TIMEOUT(composeSpy.count() > 0, 5000);
    QCOMPARE(backend.compose()->items().size(), 1);

    backend.compose()->subscribeComposeLogs(QStringLiteral("mock-project"), QStringLiteral("web"));
    QTRY_VERIFY_WITH_TIMEOUT(backend.compose()->logLines().size() >= 2, 5000);
    backend.compose()->unsubscribeComposeLogs();

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
    backend.builds()->startBuild(spec);
    QTRY_VERIFY_WITH_TIMEOUT(!backend.builds()->buildActive() && buildSpy.count() > 0, 5000);
    QVERIFY(backend.builds()->logLines().size() >= 2);
    QVERIFY(!backend.builds()->activeBuildId().isEmpty());

    QTRY_VERIFY_WITH_TIMEOUT(buildsSpy.count() > 0, 5000);
    QTRY_VERIFY_WITH_TIMEOUT(backend.builds()->items().size() >= 1, 5000);

    QFile::remove(m_testDataDir + QStringLiteral("/podman-args.log"));
    QVariantMap multiSpec = spec;
    multiSpec.insert(QStringLiteral("tag"), QStringLiteral("localhost/mock:multi"));
    multiSpec.insert(QStringLiteral("platforms"), QStringLiteral("linux/amd64,linux/arm64"));
    backend.builds()->startBuild(multiSpec);
    QTRY_VERIFY_WITH_TIMEOUT(
        !backend.builds()->buildActive() && backend.builds()->items().size() >= 2, 5000);
    bool foundPlatforms = false;
    for (const QVariant &value : backend.builds()->items()) {
        const QVariantMap build = value.toMap();
        if (build.value(QStringLiteral("imageRef")).toString() ==
            QStringLiteral("localhost/mock:multi")) {
            foundPlatforms = build.value(QStringLiteral("platforms")).toString() ==
                             QStringLiteral("linux/amd64, linux/arm64");
            break;
        }
    }
    QVERIFY(foundPlatforms);
    QFile argsFile(m_testDataDir + QStringLiteral("/podman-args.log"));
    QVERIFY(argsFile.open(QIODevice::ReadOnly));
    const QString args = QString::fromUtf8(argsFile.readAll());
    argsFile.close();
    QVERIFY2(args.contains(QStringLiteral("--platform linux/amd64,linux/arm64")), qPrintable(args));

    QTRY_VERIFY_WITH_TIMEOUT(eventsSpy.count() > 0, 5000);
    QVERIFY(backend.eventMessage().contains(QStringLiteral("start")));

    backend.compose()->composeUp(projectDir);
    QTRY_VERIFY_WITH_TIMEOUT(
        !backend.compose()->actionActive() && backend.compose()->lastError().isEmpty(), 5000);

    backend.compose()->composeDown(projectDir, false);
    QTRY_VERIFY_WITH_TIMEOUT(
        !backend.compose()->actionActive() && backend.compose()->lastError().isEmpty(), 5000);

    backend.compose()->composeRestart(projectDir, QString());
    QTRY_VERIFY_WITH_TIMEOUT(
        !backend.compose()->actionActive() && backend.compose()->lastError().isEmpty(), 5000);

    backend.compose()->composeStop(projectDir, QString());
    QTRY_VERIFY_WITH_TIMEOUT(
        !backend.compose()->actionActive() && backend.compose()->lastError().isEmpty(), 5000);

    backend.compose()->composeStart(projectDir, QString());
    QTRY_VERIFY_WITH_TIMEOUT(
        !backend.compose()->actionActive() && backend.compose()->lastError().isEmpty(), 5000);

    backend.builds()->removeBuild(backend.builds()->activeBuildId());
    QTest::qWait(100);
}

void GuiTest::backendErrors()
{
    PodStack::Backend backend;
    QSignalSpy errorSpy(&backend, &PodStack::Backend::lastErrorChanged);
    QSignalSpy imageErrorSpy(backend.images(), &PodStack::ImageController::lastErrorChanged);
    auto *containers = backend.containers();

    backend.images()->pullImage(QString());
    QVERIFY(imageErrorSpy.count() > 0);

    backend.builds()->startBuild({});
    QVERIFY(!backend.builds()->lastError().isEmpty());

    backend.containers()->uploadFile(QString(), QString(), QString());
    QVERIFY(!backend.containers()->lastError().isEmpty());

    backend.containers()->renameContainer(QString(), QString());
    QVERIFY(!backend.containers()->lastError().isEmpty());

    backend.containers()->duplicateContainer(QString());
    QVERIFY(!backend.containers()->lastError().isEmpty());

    backend.containers()->openTerminal(QString());
    QVERIFY(!backend.containers()->lastError().isEmpty());

    backend.settings()->saveEngineMode(QStringLiteral("bogus"));
    QVERIFY(!backend.settings()->lastError().isEmpty());
}

void GuiTest::backendInvalidArguments()
{
    PodStack::Backend backend;
    QSignalSpy errorSpy(&backend, &PodStack::Backend::lastErrorChanged);
    QSignalSpy actionSpy(&backend, &PodStack::Backend::actionFinished);

    // Entradas vazias devem ser recusadas no Backend, sem chegar ao daemon.
    backend.images()->removeImage(QString(), false);
    backend.images()->tagImage(QString(), QString());
    backend.images()->untagImage(QString(), QString());
    backend.images()->exportImage(QString(), QString());
    backend.images()->loadImage(QString());
    backend.volumes()->createVolume(QString(), QString());
    backend.volumes()->removeVolume(QString(), false);
    backend.volumes()->cloneVolume(QString(), QString());
    backend.volumes()->importHostPath(QString(), QString());
    backend.networks()->createNetwork(QString(), QString(), QString());
    backend.networks()->removeNetwork(QString(), false);
    backend.networks()->connectNetwork(QString(), QString());
    backend.networks()->disconnectNetwork(QString(), QString());
    backend.pods()->createPod(QString());
    backend.pods()->podAction(QString(), QString());
    backend.pods()->removePod(QString(), false);
    backend.compose()->composeUp(QString());
    backend.compose()->composeDown(QString(), false);
    backend.compose()->composeRestart(QString(), QString());
    backend.compose()->composeStop(QString(), QString());
    backend.compose()->composeStart(QString(), QString());
    backend.compose()->subscribeComposeLogs(QString(), QString());
    backend.builds()->removeBuild(QString());
    backend.machineDetail()->loadLogs(QString(), 0);
    backend.machineDetail()->startStats(QString());
    backend.machineDetail()->followConsole(QString(), 0);
    backend.machineDetail()->refreshFiles(QString(), QString());
    backend.machines()->refreshSnapshots(QString());
    backend.machines()->createSnapshot(QString(), QString());
    backend.machines()->restoreSnapshot(QString(), QString());
    backend.machines()->removeSnapshot(QString(), QString());
    backend.machineDetail()->downloadFile(QString(), QString(), QString());
    backend.machineDetail()->uploadFile(QString(), QString(), QString());
    backend.machines()->rename(QString(), QString());
    backend.machines()->clone(QString(), QString());
    backend.machines()->update({});
    backend.machines()->setPorts(QString(), {});
    backend.machines()->exportTo(QString(), QString());
    backend.machines()->importFrom(QString());
    backend.containerDetail()->load(QString());
    backend.containerDetail()->startStats(QString());
    backend.containerDetail()->startLogs(QString(), 0, true);
    backend.containerDetail()->lifecycle(QString(), QString());
    backend.containerDetail()->remove(QString(), false);
    backend.containers()->createContainer({}, false);
    backend.containers()->exposeContainerLan(QString(), true);
    backend.containers()->batchAll(QString());
    backend.containers()->batchIds({}, QString());
    backend.containers()->listFiles(QString(), QString());
    backend.containers()->downloadFile(QString(), QString());
    backend.containers()->uploadFile(QString(), QString(), QString());
    backend.settings()->setSetting(QString(), {});
    backend.settings()->setSetting(QStringLiteral("does.not.exist"), 1);
    backend.settings()->setSetting(QStringLiteral("app.theme"), QStringLiteral("bogus"));
    backend.settings()->resetSetting(QString());
    backend.settings()->resetSetting(QStringLiteral("does.not.exist"));
    backend.setup()->apply(QStringLiteral("bogus"));

    QTRY_VERIFY_WITH_TIMEOUT(!backend.machines()->lastError().isEmpty() ||
                                 !backend.machineDetail()->lastError().isEmpty() ||
                                 !backend.settings()->lastError().isEmpty(),
                             5000);
    QVERIFY(errorSpy.count() > 0 || actionSpy.count() > 0 ||
            !backend.machines()->lastError().isEmpty());
}

void GuiTest::backendOperations()
{
    PodStack::Backend backend;
    const QString tempFile = m_testDataDir + QStringLiteral("/image.tar");
    QFile file(tempFile);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("tar");
    file.close();

    backend.images()->refresh();
    backend.volumes()->refresh();
    backend.networks()->refresh();
    backend.pods()->refresh();
    backend.refreshDiskUsage();
    backend.compose()->refresh();
    backend.builds()->refresh();

    backend.volumes()->createVolume(QStringLiteral("vol2"), QStringLiteral("local"));
    backend.volumes()->cloneVolume(QStringLiteral("vol1"), QStringLiteral("vol2"));
    backend.volumes()->importHostPath(QStringLiteral("vol3"), m_testDataDir);
    backend.volumes()->removeVolume(QStringLiteral("vol1"), false);
    backend.volumes()->pruneVolumes();

    backend.networks()->createNetwork(QStringLiteral("net2"), QStringLiteral("10.90.0.0/24"),
                                      QStringLiteral("10.90.0.1"));
    backend.networks()->connectNetwork(QStringLiteral("mock-net"), QStringLiteral("abc123"));
    backend.networks()->disconnectNetwork(QStringLiteral("mock-net"), QStringLiteral("abc123"));
    backend.networks()->removeNetwork(QStringLiteral("net2"), false);

    backend.pods()->createPod(QStringLiteral("pod2"));
    backend.pods()->podAction(QStringLiteral("start"), QStringLiteral("pod1"));
    backend.pods()->removePod(QStringLiteral("pod1"), false);

    backend.images()->tagImage(QStringLiteral("img1234567890"), QStringLiteral("repo:tag"));
    backend.images()->untagImage(QStringLiteral("img1234567890"), QStringLiteral("repo:tag"));
    backend.images()->exportImage(QStringLiteral("img1234567890"), tempFile);
    backend.images()->loadImage(tempFile);
    backend.images()->pruneImages(false);
    backend.images()->removeImage(QStringLiteral("img1234567890"), false);

    backend.compose()->composeUp(QStringLiteral("/tmp/mock-project"));
    backend.compose()->composeRestart(QStringLiteral("/tmp/mock-project"), QStringLiteral("web"));
    backend.compose()->composeStop(QStringLiteral("/tmp/mock-project"), QStringLiteral("web"));
    backend.compose()->composeStart(QStringLiteral("/tmp/mock-project"), QStringLiteral("web"));
    backend.compose()->composeDown(QStringLiteral("/tmp/mock-project"), false);
    backend.compose()->subscribeComposeLogs(QStringLiteral("mock-project"), QStringLiteral("web"));
    backend.compose()->unsubscribeComposeLogs();
    backend.builds()->removeBuild(QStringLiteral("missing"));

    backend.system()->startActivityMonitor();
    backend.system()->stopActivityMonitor();

    // Dá tempo para os jobs assíncronos concluírem antes de destruir o Backend.
    QTest::qWait(1500);
}

void GuiTest::backendBatchActions()
{
    PodStack::Backend backend;
    QSignalSpy actionSpy(&backend, &PodStack::Backend::actionFinished);

    backend.pods()->batchPods({QStringLiteral("missing")}, QStringLiteral("bogus"));
    QTRY_VERIFY_WITH_TIMEOUT(
        !backend.pods()->actionActive() && !backend.pods()->lastError().isEmpty(), 5000);

    backend.images()->batchImages({QStringLiteral("missing-image")}, false);
    QTRY_VERIFY_WITH_TIMEOUT(
        !backend.images()->actionActive() && !backend.images()->lastError().isEmpty(), 5000);

    backend.volumes()->batchVolumes({QStringLiteral("missing-volume")}, false);
    QTRY_VERIFY_WITH_TIMEOUT(
        !backend.volumes()->actionActive() && !backend.volumes()->lastError().isEmpty(), 5000);

    backend.networks()->batchNetworks({QStringLiteral("missing-network")}, false);
    QTRY_VERIFY_WITH_TIMEOUT(
        !backend.networks()->actionActive() && !backend.networks()->lastError().isEmpty(), 5000);

    backend.pods()->batchPods({QStringLiteral("missing-pod")}, QStringLiteral("start"));
    QTRY_VERIFY_WITH_TIMEOUT(
        !backend.pods()->actionActive() && !backend.pods()->lastError().isEmpty(), 5000);

    backend.machines()->batch({QStringLiteral("missing-machine")}, QStringLiteral("start"));
    QTRY_VERIFY_WITH_TIMEOUT(
        !backend.machines()->actionActive() && !backend.machines()->lastError().isEmpty(), 5000);

    backend.builds()->batchBuilds({QStringLiteral("missing-build")});
    QTRY_VERIFY_WITH_TIMEOUT(
        !backend.builds()->actionActive() && !backend.builds()->lastError().isEmpty(), 5000);

    backend.compose()->batchCompose({QStringLiteral("/tmp/mock-project")}, QStringLiteral("up"));
    QTRY_VERIFY_WITH_TIMEOUT(!backend.compose()->actionActive(), 10000);
    QVERIFY2(backend.compose()->lastError().isEmpty(), qPrintable(backend.compose()->lastError()));
}

void GuiTest::backendBatchPartial()
{
    PodStack::Backend backend;
    auto *volumes = backend.volumes();
    QSignalSpy itemsSpy(volumes, &PodStack::VolumeController::itemsChanged);
    QSignalSpy settledSpy(volumes, &PodStack::ControllerBase::actionSettled);

    QTRY_VERIFY_WITH_TIMEOUT(volumes->items().size() >= 1, 5000);
    itemsSpy.clear();

    // Remover um volume inexistente falha; mesmo assim a lista precisa
    // recarregar (antes o lote abortava o refresh e o usuário via "nada
    // aconteceu" até clicar em Atualizar) e o resumo precisa sair.
    volumes->batchVolumes({QStringLiteral("missing-volume")}, false);

    QTRY_VERIFY_WITH_TIMEOUT(!volumes->actionActive(), 5000);
    QTRY_VERIFY_WITH_TIMEOUT(itemsSpy.count() > 0, 5000);
    QVERIFY2(volumes->lastError().contains(QStringLiteral("1 de 1")),
             qPrintable(volumes->lastError()));
    QCOMPARE(settledSpy.count(), 1);
    QCOMPARE(settledSpy.first().at(1).toBool(), false);

    // O banner da página usa o invokable para dispensar o erro.
    volumes->dismissError();
    QVERIFY(volumes->lastError().isEmpty());
}

void GuiTest::backendComposeErrorDetail()
{
    PodStack::Backend backend;
    auto *compose = backend.compose();

    // O provider do Podman escreve um banner de emulação no stderr; usar o
    // stderr inteiro como detalhe escondia o erro real por trás do banner.
    const QString projectDir = m_testDataDir + QStringLiteral("/fail-project");
    QVERIFY(QDir().mkpath(projectDir));
    compose->composeUp(projectDir);

    QTRY_VERIFY_WITH_TIMEOUT(!compose->actionActive(), 10000);
    const QString error = compose->lastError();
    QVERIFY2(error.contains(QStringLiteral("bind: address already in use")), qPrintable(error));
    QVERIFY2(!error.contains(QStringLiteral("Emulate Docker CLI")), qPrintable(error));
}

void GuiTest::backendRejectDuplicateAction()
{
    PodStack::Backend backend;
    auto *containers = backend.containers();
    const QString path = QStringLiteral("/libpod/containers/abc123/start");
    const int before = m_mock.requestCount(path);

    // Duplo clique no mesmo item: a segunda chamada não pode empilhar outra
    // ação igual (antes ela rodava de novo depois).
    containers->lifecycle(QStringLiteral("start"), QStringLiteral("abc123"));
    containers->lifecycle(QStringLiteral("start"), QStringLiteral("abc123"));

    QTRY_VERIFY_WITH_TIMEOUT(!containers->actionActive(), 5000);
    QCOMPARE(m_mock.requestCount(path), before + 1);
    QVERIFY(containers->runningTargets().isEmpty());
}

void GuiTest::feedbackQueue()
{
    PodStack::Backend backend;
    auto *feedback = backend.feedback();
    QSignalSpy spy(feedback, &PodStack::Feedback::currentChanged);

    feedback->show(QStringLiteral("primeiro"), QStringLiteral("success"), 1000);
    QCOMPARE(feedback->message(), QStringLiteral("primeiro"));
    QCOMPARE(feedback->severity(), QStringLiteral("success"));
    QVERIFY(feedback->active());

    // Rajada repete o mesmo texto: não empilha nem rouba a vez.
    feedback->show(QStringLiteral("primeiro"), QStringLiteral("success"), 1000);
    feedback->show(QStringLiteral("segundo"), QStringLiteral("error"), 1000);
    QCOMPARE(feedback->message(), QStringLiteral("primeiro"));

    QTRY_COMPARE_WITH_TIMEOUT(feedback->message(), QStringLiteral("segundo"), 3000);
    QCOMPARE(feedback->severity(), QStringLiteral("error"));
    QVERIFY(spy.count() >= 2);

    feedback->dismiss();
    QVERIFY(!feedback->active());
    QCOMPARE(feedback->message(), QString());

    // Ação concluída com sucesso vira toast (mapa central em `Backend`).
    const QString projectDir = m_testDataDir + QStringLiteral("/toast-project");
    QVERIFY(QDir().mkpath(projectDir));
    backend.compose()->composeUp(projectDir);
    QTRY_COMPARE_WITH_TIMEOUT(feedback->message(), QStringLiteral("Projeto no ar"), 8000);
}

void GuiTest::backendTheme()
{
    PodStack::Backend backend;
    QSignalSpy errorSpy(backend.settings(), &PodStack::SettingsController::lastErrorChanged);

    backend.settings()->saveTheme(QStringLiteral("light"));
    QTRY_COMPARE(backend.settings()->settings().value(QStringLiteral("theme")).toString(),
                 QStringLiteral("light"));

    backend.settings()->saveTheme(QStringLiteral("system"));
    QTRY_COMPARE(backend.settings()->settings().value(QStringLiteral("theme")).toString(),
                 QStringLiteral("system"));

    backend.settings()->saveTheme(QStringLiteral("bogus"));
    QVERIFY(!backend.settings()->lastError().isEmpty());
    QVERIFY(errorSpy.count() > 0);
    QCOMPARE(backend.settings()->settings().value(QStringLiteral("theme")).toString(),
             QStringLiteral("system"));

    backend.settings()->setSetting(QStringLiteral("app.fontScale"), 120);
    QTRY_COMPARE(backend.settings()->values().value(QStringLiteral("app.fontScale")).toInt(), 120);
    backend.settings()->setSetting(QStringLiteral("app.fontScale"), 200);
    QTRY_VERIFY(!backend.settings()->error().isEmpty());
    QCOMPARE(backend.settings()->values().value(QStringLiteral("app.fontScale")).toInt(), 120);
}

void GuiTest::backendSettings()
{
    PodStack::Backend backend;
    QSignalSpy schemaSpy(backend.settings(), &PodStack::SettingsController::schemaChanged);
    QSignalSpy valuesSpy(backend.settings(), &PodStack::SettingsController::valuesChanged);
    QSignalSpy finishedSpy(backend.settings(), &PodStack::SettingsController::settingFinished);
    QSignalSpy restartSpy(backend.settings(),
                          &PodStack::SettingsController::pendingRestartKeysChanged);

    backend.settings()->refresh();
    QTRY_COMPARE(backend.settings()->schema().size(), 88);
    QVERIFY(schemaSpy.count() > 0);
    QVERIFY(backend.settings()->values().contains(QStringLiteral("engine.mode")));
    QVERIFY(valuesSpy.count() > 0);

    backend.settings()->setSetting(QStringLiteral("app.theme"), QStringLiteral("light"));
    QTRY_COMPARE(backend.settings()->values().value(QStringLiteral("app.theme")).toString(),
                 QStringLiteral("light"));
    QTRY_VERIFY(finishedSpy.count() > 0);
    bool themeSaved = false;
    for (const QList<QVariant> &entry : finishedSpy) {
        if (entry.at(0).toString() == QStringLiteral("app.theme")) {
            themeSaved = entry.at(1).toBool();
        }
    }
    QVERIFY(themeSaved);
    // app.theme não exige reinício: não deve entrar na lista de pendências.
    QVERIFY(!backend.settings()->pendingRestartKeys().contains(QStringLiteral("app.theme")));

    backend.settings()->setSetting(QStringLiteral("terminal.fontSize"), 99);
    QTRY_VERIFY(!backend.settings()->error().isEmpty());
    QCOMPARE(backend.settings()->values().value(QStringLiteral("terminal.fontSize")).toInt(), 12);

    backend.settings()->setSetting(QStringLiteral("engine.mode"), QStringLiteral("rootful"));
    QTRY_COMPARE(backend.settings()->values().value(QStringLiteral("engine.mode")).toString(),
                 QStringLiteral("rootful"));
    QTRY_VERIFY(backend.settings()->error().isEmpty());
    // O daemon devolve restartRequired=true para engine.mode; o Backend precisa
    // expor isso para o diálogo avisar o usuário.
    QTRY_VERIFY(backend.settings()->pendingRestartKeys().contains(QStringLiteral("engine.mode")));
    QVERIFY(restartSpy.count() > 0);

    backend.settings()->resetSetting(QStringLiteral("app.theme"));
    QTRY_COMPARE(backend.settings()->values().value(QStringLiteral("app.theme")).toString(),
                 QStringLiteral("system"));
    backend.settings()->resetSetting(QStringLiteral("engine.mode"));
    QTRY_COMPARE(backend.settings()->values().value(QStringLiteral("engine.mode")).toString(),
                 QStringLiteral("rootless"));

    backend.settings()->setSetting(QStringLiteral("app.startAtLogin"), true);
    QTRY_VERIFY(QFile::exists(PodStack::Autostart::filePath()));
    backend.settings()->setSetting(QStringLiteral("app.startAtLogin"), false);
    QTRY_VERIFY(!QFile::exists(PodStack::Autostart::filePath()));
}

void GuiTest::backendTray()
{
    PodStack::Backend backend;
    QSignalSpy containersSpy(backend.containers(), &PodStack::ContainerController::itemsChanged);
    QTRY_VERIFY_WITH_TIMEOUT(containersSpy.count() > 0, 5000);
    QVERIFY(containersSpy.count() > 0);
    QCOMPARE(backend.containers()->items().size(), 1);

    backend.containers()->lifecycle(QStringLiteral("pause"), QStringLiteral("abc123"));
    QVERIFY(backend.containers()->actionActive());
    backend.containers()->lifecycle(QStringLiteral("unpause"), QStringLiteral("abc123"));
    QVERIFY(backend.containers()->actionActive());
    QTRY_VERIFY_WITH_TIMEOUT(!backend.containers()->actionActive(), 5000);

    backend.settings()->saveTraySettings(false, false, true, false);
    QTRY_COMPARE(backend.settings()->settings().value(QStringLiteral("keepRunningInTray")).toBool(),
                 false);
    QTRY_COMPARE(backend.settings()->settings().value(QStringLiteral("showInTray")).toBool(),
                 false);
    QTRY_COMPARE(backend.settings()->settings().value(QStringLiteral("startAtLogin")).toBool(),
                 true);
    QTRY_COMPARE(backend.settings()->settings().value(QStringLiteral("notifications")).toBool(),
                 false);
    QTRY_VERIFY(QFile::exists(PodStack::Autostart::filePath()));

    backend.settings()->saveTraySettings(true, true, false, true);
    QTRY_COMPARE(backend.settings()->settings().value(QStringLiteral("keepRunningInTray")).toBool(),
                 true);
    QTRY_COMPARE(backend.settings()->settings().value(QStringLiteral("showInTray")).toBool(), true);
    QTRY_COMPARE(backend.settings()->settings().value(QStringLiteral("startAtLogin")).toBool(),
                 false);
    QTRY_COMPARE(backend.settings()->settings().value(QStringLiteral("notifications")).toBool(),
                 true);
    QTRY_VERIFY(!QFile::exists(PodStack::Autostart::filePath()));
}

void GuiTest::uiStateRoundTrip()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("ui-state.json"));

    PodStack::UiState state;
    state.windowGeometry = QByteArrayLiteral("geometry");
    state.section = QStringLiteral("machines");
    state.containerTab = 5;
    state.machineTab = QStringLiteral("stats");
    QVERIFY(state.save(path));

    const QFileInfo info(path);
    QVERIFY(info.exists());
    QCOMPARE(info.permissions() & QFileDevice::ReadOther, QFileDevice::Permissions());
    QCOMPARE(info.permissions() & QFileDevice::WriteOther, QFileDevice::Permissions());

    PodStack::UiState loaded;
    QVERIFY(loaded.load(path));
    QCOMPARE(loaded.windowGeometry, QByteArrayLiteral("geometry"));
    QCOMPARE(loaded.section, QStringLiteral("machines"));
    QCOMPARE(loaded.containerTab, 5);
    QCOMPARE(loaded.machineTab, QStringLiteral("stats"));
}

void GuiTest::uiStateInvalidFallsBack()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("ui-state.json"));

    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("not json");
    file.close();

    PodStack::UiState state;
    QVERIFY(!state.load(path));
    QCOMPARE(state.section, QStringLiteral("containers"));
    QCOMPARE(state.containerTab, 0);
    QCOMPARE(state.machineTab, QStringLiteral("info"));

    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    file.write(R"({"version":2,"section":"machines"})");
    file.close();
    QVERIFY(!state.load(path));
    QCOMPARE(state.section, QStringLiteral("containers"));
}

void GuiTest::uiStateRejectsInvalidValues()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("ui-state.json"));

    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(R"({"version":1,"section":"bogus","detail":{"containerTab":99,"machineTab":"x"}})");
    file.close();

    PodStack::UiState state;
    QVERIFY(state.load(path));
    QCOMPARE(state.section, QStringLiteral("containers"));
    QCOMPARE(state.containerTab, 0);
    QCOMPARE(state.machineTab, QStringLiteral("info"));
    QVERIFY(PodStack::UiState::isValidSection(QStringLiteral("kubernetes")));
    QVERIFY(!PodStack::UiState::isValidSection(QStringLiteral("bogus")));
    QVERIFY(!PodStack::UiState::isValidContainerTab(-1));
    QVERIFY(!PodStack::UiState::isValidMachineTab(QStringLiteral("bogus")));
}

void GuiTest::systemLogs()
{
    const QString logDir = m_testDataDir + QStringLiteral("/state/podstack/logs");
    QVERIFY(QDir().mkpath(logDir));
    QFile file(logDir + QStringLiteral("/podstackd.jsonl"));
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    file.write(
        QByteArrayLiteral(
            R"({"ts":"2026-11-13T00:00:00Z","level":"error","category":"podstack.setup","process":"podstackd","message":"Authorization: Bearer segredo"})") +
        '\n');
    file.close();

    PodStack::Backend backend;
    backend.system()->loadLogs(QString(), QStringLiteral("setup"), 7);
    QTRY_VERIFY_WITH_TIMEOUT(!backend.system()->logsBusy(), 3000);
    QVERIFY2(backend.system()->logsError().isEmpty(), qPrintable(backend.system()->logsError()));
    QCOMPARE(backend.system()->logs().size(), 1);
    QVERIFY(!backend.system()
                 ->logs()
                 .first()
                 .toMap()
                 .value(QStringLiteral("message"))
                 .toString()
                 .contains(QStringLiteral("segredo")));

    const QString exportPath = m_testDataDir + QStringLiteral("/logs-export.jsonl");
    QVERIFY(backend.system()->exportLogs(exportPath));
    QFile exported(exportPath);
    QVERIFY(exported.open(QIODevice::ReadOnly));
    QVERIFY(!exported.readAll().contains("segredo"));

    backend.system()->clearLogs();
    QVERIFY(!QFileInfo::exists(logDir + QStringLiteral("/podstackd.jsonl")));
}

void GuiTest::loadUpdateState()
{
    const QString updatesDir = m_testDataDir + QStringLiteral("/state/podstack/updates");
    QVERIFY(QDir().mkpath(updatesDir));
    QFile state(updatesDir + QStringLiteral("/state.json"));
    QVERIFY(state.open(QIODevice::WriteOnly | QIODevice::Truncate));
    state.write(QByteArrayLiteral(R"({"phase":"done","message":"ok"})"));
    state.close();

    PodStack::Backend backend;
    QTRY_COMPARE_WITH_TIMEOUT(
        backend.updates()->status().value(QStringLiteral("status")).toString(),
        QStringLiteral("installed"), 3000);
}

void GuiTest::diagnosticReport()
{
    PodStack::Backend backend;
    backend.system()->generateDiagnosticReport();
    QTRY_VERIFY_WITH_TIMEOUT(!backend.system()->diagnosticReportPath().isEmpty(), 5000);
    QVERIFY(QFileInfo::exists(backend.system()->diagnosticReportPath()));
}

void GuiTest::clearUpdateState()
{
    const QString stateDir = m_testDataDir + QStringLiteral("/state/podstack/updates");
    QVERIFY(QDir().mkpath(stateDir));
    const QString path = stateDir + QStringLiteral("/state.json");
    QFile state(path);
    QVERIFY(state.open(QIODevice::WriteOnly | QIODevice::Truncate));
    state.write(QByteArrayLiteral(R"({"phase":"failed","message":"erro"})"));
    state.close();

    PodStack::Backend backend;
    backend.updates()->clearState();
    QVERIFY(!QFileInfo::exists(path));
}

void GuiTest::installUpdateFlatpakGuard()
{
    PodStack::Backend backend;
    QSignalSpy failed(backend.updates(), &PodStack::UpdateController::checkFailed);
    qputenv("FLATPAK_ID", "dev.podstack.PodStack");
    const auto restore = qScopeGuard([] { qunsetenv("FLATPAK_ID"); });
    backend.updates()->install();
    QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 1, 3000);
}

void GuiTest::installUpdateSystemdGuard()
{
    PodStack::Backend backend;
    QSignalSpy failed(backend.updates(), &PodStack::UpdateController::checkFailed);
    const QByteArray previousPath = qgetenv("PATH");
    qputenv("PATH", QByteArrayLiteral("/nonexistent-podstack"));
    const auto restore = qScopeGuard([previousPath] { qputenv("PATH", previousPath); });
    backend.updates()->install();
    QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 1, 3000);
}

void GuiTest::unixTerminationSignal()
{
    int received = 0;
    QString error;
    QVERIFY2(PodStack::installTerminationHandler(
                 this, [&received]() { ++received; }, &error),
             qPrintable(error));

    ::raise(SIGTERM);
    QTRY_COMPARE_WITH_TIMEOUT(received, 1, 3000);

    ::raise(SIGINT);
    QTRY_COMPARE_WITH_TIMEOUT(received, 2, 3000);
}

void GuiTest::guiStartsAndQuits()
{
    PodStack::Config config = PodStack::Config::load();
    config.set(QStringLiteral("app.showInTray"), false);
    QString error;
    QVERIFY2(config.save(&error), qPrintable(error));

    const QString runtimeDir = m_testDataDir + QStringLiteral("/runtime");
    QDir().mkpath(runtimeDir);
    QFile::setPermissions(runtimeDir,
                          QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
    const QString guiPath =
        QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("podstack-gui"));

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
    QSignalSpy activitySpy(backend.system(), &PodStack::SystemController::activityChanged);

    backend.system()->startActivityMonitor();
    QVERIFY(backend.system()->activityActive());
    QTRY_VERIFY_WITH_TIMEOUT(activitySpy.count() > 1, 7000);
    QTRY_COMPARE(
        backend.system()->activityStats().value(QStringLiteral("containers")).toList().size(), 2);
    QVERIFY(backend.system()
                ->activityStats()
                .value(QStringLiteral("host"))
                .toMap()
                .value(QStringLiteral("memTotal"))
                .toLongLong() > 0);

    backend.system()->stopActivityMonitor();
    QVERIFY(!backend.system()->activityActive());
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

    QSignalSpy k8sLogSpy(&client, &PodStack::StreamClient::k8sLogLine);
    QSignalSpy k8sLogDoneSpy(&client, &PodStack::StreamClient::k8sLogFinished);
    client.subscribeK8sLogs(QStringLiteral("prod"), QStringLiteral("web"), QStringLiteral("web"));
    socket = accept(QByteArrayLiteral("k8s.logs.subscribe"));
    QVERIFY(socket);
    socket->write(QByteArrayLiteral(
        "{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":{\"subscriptionId\":\"sub-10\"}}\n"
        "{\"jsonrpc\":\"2.0\",\"method\":\"k8s.logs.delta\",\"params\":{"
        "\"namespace\":\"prod\",\"pod\":\"web\",\"container\":\"web\","
        "\"line\":\"linha k8s\"}}\n"
        "{\"jsonrpc\":\"2.0\",\"method\":\"k8s.logs.done\",\"params\":{"
        "\"namespace\":\"prod\",\"pod\":\"web\",\"container\":\"web\"}}\n"));
    socket->flush();
    QTRY_VERIFY_WITH_TIMEOUT(k8sLogSpy.count() > 0, 5000);
    QTRY_VERIFY_WITH_TIMEOUT(k8sLogDoneSpy.count() > 0, 5000);
    QCOMPARE(k8sLogDoneSpy.last().at(0).toBool(), true);

    QSignalSpy k8sJobEventSpy(&client, &PodStack::StreamClient::k8sJobEvent);
    QSignalSpy k8sJobDoneSpy(&client, &PodStack::StreamClient::k8sJobFinished);
    client.startK8sJob(QStringLiteral("k8s.disable"), QJsonObject{});
    socket = accept(QByteArrayLiteral("k8s.disable"));
    QVERIFY(socket);
    socket->write(QByteArrayLiteral(
        "{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":{\"subscriptionId\":\"sub-11\"}}\n"
        "{\"jsonrpc\":\"2.0\",\"method\":\"k8s.delta\",\"params\":{"
        "\"stage\":\"removing\",\"progress\":60}}\n"
        "{\"jsonrpc\":\"2.0\",\"method\":\"k8s.done\",\"params\":{"
        "\"cluster\":{\"state\":\"disabled\"}}}\n"));
    socket->flush();
    QTRY_VERIFY_WITH_TIMEOUT(k8sJobEventSpy.count() > 0, 5000);
    QTRY_VERIFY_WITH_TIMEOUT(k8sJobDoneSpy.count() > 0, 5000);

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
    auto *settings = backend.settings();
    QSignalSpy errorSpy(settings, &PodStack::SettingsController::lastErrorChanged);
    QSignalSpy settingsSpy(settings, &PodStack::SettingsController::valuesChanged);

    settings->saveProxySettings(QStringLiteral("http://127.0.0.1:3128"),
                                QStringLiteral("localhost, .internal"));
    QTRY_COMPARE(settings->settings().value(QStringLiteral("proxy")).toString(),
                 QStringLiteral("http://127.0.0.1:3128"));
    QTRY_COMPARE(settings->settings().value(QStringLiteral("proxyExclude")).toStringList(),
                 QStringList({QStringLiteral("localhost"), QStringLiteral(".internal")}));
    QVERIFY(settings->lastError().isEmpty());
    QVERIFY(settingsSpy.count() > 0);

    settings->saveProxySettings(QStringLiteral("ftp://127.0.0.1:21"), QStringLiteral("localhost"));
    QVERIFY(!settings->lastError().isEmpty());
    QCOMPARE(settings->settings().value(QStringLiteral("proxy")).toString(),
             QStringLiteral("http://127.0.0.1:3128"));
    QCOMPARE(settings->settings().value(QStringLiteral("proxyExclude")).toStringList(),
             QStringList({QStringLiteral("localhost"), QStringLiteral(".internal")}));

    settings->saveProxySettings(QStringLiteral("http://127.0.0.1:3128"),
                                QStringLiteral("bad host"));
    QVERIFY(!settings->lastError().isEmpty());
    QCOMPARE(settings->settings().value(QStringLiteral("proxyExclude")).toStringList(),
             QStringList({QStringLiteral("localhost"), QStringLiteral(".internal")}));

    settings->saveProxySettings(QStringLiteral("auto"), QStringLiteral("localhost, .internal"));
    QTRY_COMPARE(settings->settings().value(QStringLiteral("proxy")).toString(),
                 QStringLiteral("auto"));
}

void GuiTest::backendSetup()
{
    PodStack::Backend backend;
    QSignalSpy setupSpy(backend.setup(), &PodStack::SetupController::changed);

    QVERIFY(!backend.setup()->loaded());
    backend.setup()->refreshStatus();
    QTRY_VERIFY_WITH_TIMEOUT(backend.setup()->loaded(), 15000);
    QVERIFY(setupSpy.count() > 0);
    QVERIFY(backend.setup()->checks().size() >= 8);
    QVERIFY(backend.setup()->completed() == false);
    QVERIFY(backend.setup()->pending());

    backend.setup()->skip();
    QTRY_VERIFY_WITH_TIMEOUT(!backend.setup()->pending(), 10000);
    QVERIFY(!backend.setup()->completed());

    backend.setup()->complete();
    if (!backend.setup()->mandatoryOk()) {
        // O host (ex.: container do CI) não tem QEMU/OVMF/virtiofsd. A recusa é
        // parte do contrato; o caminho de sucesso é coberto onde os checks passam.
        QVERIFY(!backend.setup()->error().isEmpty());
        QVERIFY(!backend.setup()->completed());
        QSKIP("Checks obrigatórios indisponíveis no host; conclusão bloqueada.");
    }
    QTRY_VERIFY_WITH_TIMEOUT(backend.setup()->completed(), 10000);
    QVERIFY(!backend.setup()->pending());
}

void GuiTest::backendSetupJob()
{
    PodStack::Backend backend;
    QSignalSpy jobSpy(backend.setup(), &PodStack::SetupController::jobChanged);

    backend.setup()->apply(QStringLiteral("bogus"));
    QTRY_VERIFY_WITH_TIMEOUT(!backend.setup()->jobActive(), 10000);
    QVERIFY(!backend.setup()->error().isEmpty());

    backend.setup()->apply(QStringLiteral("enable-socket"));
    QTRY_VERIFY_WITH_TIMEOUT(!backend.setup()->jobActive(), 20000);
    QVERIFY(jobSpy.count() > 0);
    QVERIFY(!backend.setup()->log().isEmpty());

    backend.setup()->cancel();
    backend.setup()->complete();
    QVERIFY(!backend.setup()->error().isEmpty());
}

} // namespace

QTEST_GUILESS_MAIN(GuiTest)
#include "test_gui.moc"
