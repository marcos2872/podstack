#include "MockPodmanServer.h"

#include "podman/PodmanClient.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QStringList>
#include <QTest>

namespace {

class ContractTest : public QObject {
    Q_OBJECT

private slots:
    void containerInfoJson();
    void pingListAndLifecycle();
    void createContainer();
    void execLogsAndEvents();
    void files();
    void catalog();
    void catalogErrors();
    void buildContext();
    void errorMapping();
    void pingFailure();
};

void ContractTest::containerInfoJson()
{
    PodStack::ContainerInfo info;
    info.id = QStringLiteral("abc123");
    info.name = QStringLiteral("demo");
    info.image = QStringLiteral("alpine:3.20");
    info.state = QStringLiteral("running");
    info.status = QStringLiteral("Up");
    info.created = QStringLiteral("2026-01-01T00:00:00Z");
    info.ports = {QStringLiteral("8080->80/tcp")};
    info.labels = {{QStringLiteral("dev.podstack.test"), QStringLiteral("1")}};

    const QJsonObject json = info.toJson();
    QCOMPARE(json.value(QStringLiteral("id")).toString(), QStringLiteral("abc123"));
    QCOMPARE(json.value(QStringLiteral("name")).toString(), QStringLiteral("demo"));
    QCOMPARE(json.value(QStringLiteral("ports")).toArray().size(), 1);
    QCOMPARE(json.value(QStringLiteral("labels")).toObject().size(), 1);
}

void ContractTest::pingListAndLifecycle()
{
    MockPodmanServer server;
    QVERIFY(server.isListening());

    PodStack::PodmanClient client(server.socketPath());
    PodStack::Error error;

    QVERIFY2(client.ping(&error), qPrintable(error.message));
    QCOMPARE(client.apiVersion(), QStringLiteral("v5.8.9"));

    const QVector<PodStack::ContainerInfo> containers = client.listContainers(true, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QCOMPARE(containers.size(), 1);
    QCOMPARE(containers.first().name, QStringLiteral("demo"));
    QCOMPARE(containers.first().health, QStringLiteral("healthy"));
    QCOMPARE(containers.first().ports.size(), 1);

    QVERIFY2(client.startContainer(QStringLiteral("abc123"), &error),
             qPrintable(error.message));
    QVERIFY2(client.stopContainer(QStringLiteral("abc123"), 5, &error),
             qPrintable(error.message));
    QVERIFY2(client.restartContainer(QStringLiteral("abc123"), 5, &error),
             qPrintable(error.message));
    QVERIFY2(client.removeContainer(QStringLiteral("abc123"), true, true, &error),
             qPrintable(error.message));
    QVERIFY2(client.pauseContainer(QStringLiteral("abc123"), &error), qPrintable(error.message));
    QVERIFY2(client.unpauseContainer(QStringLiteral("abc123"), &error), qPrintable(error.message));
    QVERIFY2(client.killContainer(QStringLiteral("abc123"), QStringLiteral("SIGTERM"), &error),
             qPrintable(error.message));
    QVERIFY2(client.renameContainer(QStringLiteral("abc123"), QStringLiteral("renamed"), &error),
             qPrintable(error.message));
    QVERIFY(!client.renameContainer(QStringLiteral("abc123"), {}, &error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_INVALID_PARAMS"));

    const QJsonObject inspected = client.inspectContainer(QStringLiteral("abc123"), &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QCOMPARE(inspected.value(QStringLiteral("Id")).toString(), QStringLiteral("abc123"));
}

void ContractTest::createContainer()
{
    MockPodmanServer server;
    PodStack::PodmanClient client(server.socketPath());
    PodStack::Error error;

    PodStack::ContainerCreateSpec spec;
    spec.image = QStringLiteral("docker.io/library/alpine:3.20");
    spec.name = QStringLiteral("created-demo");
    spec.command = QStringLiteral("sleep 10");
    spec.env.insert(QStringLiteral("A"), QStringLiteral("B"));
    spec.ports.append({18080, 80, QStringLiteral("tcp"), QStringLiteral("0.0.0.0")});
    spec.mounts.append({QStringLiteral("/tmp"), QStringLiteral("/mnt/host"), false});
    spec.workDir = QStringLiteral("/tmp");
    spec.user = QStringLiteral("0:0");
    spec.remove = true;
    spec.init = true;
    spec.privileged = true;

    const QString id = client.createContainer(spec, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QCOMPARE(id, QStringLiteral("created123"));

    const QJsonObject body = QJsonDocument::fromJson(server.lastBody()).object();
    QCOMPARE(body.value(QStringLiteral("image")).toString(), spec.image);
    QCOMPARE(body.value(QStringLiteral("name")).toString(), spec.name);
    QCOMPARE(body.value(QStringLiteral("command")).toArray().size(), 2);
    QCOMPARE(body.value(QStringLiteral("portmappings")).toArray().size(), 1);
    QCOMPARE(body.value(QStringLiteral("mounts")).toArray().size(), 1);
    QCOMPARE(body.value(QStringLiteral("work_dir")).toString(), spec.workDir);
    QCOMPARE(body.value(QStringLiteral("user")).toString(), spec.user);
    QVERIFY(body.value(QStringLiteral("remove")).toBool());
    QVERIFY(body.value(QStringLiteral("init")).toBool());
    QVERIFY(body.value(QStringLiteral("privileged")).toBool());
}

void ContractTest::execLogsAndEvents()
{
    MockPodmanServer server;
    PodStack::PodmanClient client(server.socketPath());
    PodStack::Error error;

    PodStack::ExecResult exec;
    QVERIFY2(client.execOnce(QStringLiteral("abc123"),
                             {QStringLiteral("/bin/echo"), QStringLiteral("hello")}, &exec,
                             &error),
             qPrintable(error.message));
    QVERIFY(exec.output.contains("mock-exec-output"));
    QCOMPARE(exec.exitCode, 0);

    QStringList logLines;
    QVERIFY2(client.streamLogs(
                 QStringLiteral("abc123"), 10, false,
                 [&logLines](const QByteArray &data) {
                     const QString line = QString::fromUtf8(data).trimmed();
                     if (!line.isEmpty()) {
                         logLines.append(line);
                     }
                     return true;
                 },
                 nullptr, &error),
             qPrintable(error.message));
    QVERIFY(logLines.contains(QStringLiteral("line-one")));
    QVERIFY(logLines.contains(QStringLiteral("line-two")));

    QStringList statsLines;
    QVERIFY2(client.streamStats(
                 QStringLiteral("abc123"), 1,
                 [&statsLines](const QByteArray &line) {
                     statsLines.append(QString::fromUtf8(line));
                     return true;
                 },
                 nullptr, &error),
             qPrintable(error.message));
    QVERIFY(!statsLines.isEmpty());
    QVERIFY(statsLines.first().contains(QStringLiteral("cpu_percent")));

    QStringList rawStatsLines;
    QVERIFY2(client.streamStats(
                 QStringLiteral("rawstats"), 1,
                 [&rawStatsLines](const QByteArray &line) {
                     rawStatsLines.append(QString::fromUtf8(line));
                     return true;
                 },
                 nullptr, &error),
             qPrintable(error.message));
    QVERIFY(!rawStatsLines.isEmpty());
    QVERIFY(rawStatsLines.first().contains(QStringLiteral("mem_usage")));
    QVERIFY(rawStatsLines.first().contains(QStringLiteral("net_io")));
    QVERIFY(rawStatsLines.first().contains(QStringLiteral("block_io")));
    QVERIFY(rawStatsLines.first().contains(QStringLiteral("pids")));

    QStringList cgroupLines;
    QVERIFY2(client.streamStats(
                 QStringLiteral("rawcgroup"), 1,
                 [&cgroupLines](const QByteArray &line) {
                     cgroupLines.append(QString::fromUtf8(line));
                     return true;
                 },
                 nullptr, &error),
             qPrintable(error.message));
    QVERIFY(!cgroupLines.isEmpty());
    QVERIFY(cgroupLines.first().contains(QStringLiteral("mem_usage")));
    QVERIFY(cgroupLines.first().contains(QStringLiteral("net_io")));
    QVERIFY(cgroupLines.first().contains(QStringLiteral("block_io")));
    QVERIFY(cgroupLines.first().contains(QStringLiteral("pids")));
    QVERIFY(cgroupLines.first().contains(QStringLiteral("cpu_percent")));

    QStringList events;
    QVERIFY2(client.streamEvents(
                 [&events](const QByteArray &data) {
                     events.append(QString::fromUtf8(data));
                     return true;
                 },
                 nullptr, &error),
             qPrintable(error.message));
    QVERIFY(!events.isEmpty());
    QVERIFY(events.join(QString()).contains(QStringLiteral("container")));
}

void ContractTest::files()
{
    MockPodmanServer server;
    PodStack::PodmanClient client(server.socketPath());
    PodStack::Error error;

    const QVector<PodStack::FileEntry> entries =
        client.listFiles(QStringLiteral("abc123"), QStringLiteral("/"), &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QCOMPARE(entries.size(), 2);

    bool foundFile = false;
    bool foundDirectory = false;
    for (const PodStack::FileEntry &entry : entries) {
        if (entry.name == QStringLiteral("file.txt")) {
            foundFile = true;
            QCOMPARE(entry.size, 5);
            QVERIFY(!entry.directory);
        }
        if (entry.name == QStringLiteral("dir")) {
            foundDirectory = true;
            QVERIFY(entry.directory);
        }
    }
    QVERIFY(foundFile);
    QVERIFY(foundDirectory);

    const QString destination = QDir::tempPath()
                                + QStringLiteral("/podstack-file-%1.txt")
                                      .arg(QCoreApplication::applicationPid());
    QFile::remove(destination);
    QVERIFY2(client.downloadFile(QStringLiteral("abc123"), QStringLiteral("/file.txt"),
                                 destination, &error),
             qPrintable(error.message));

    QFile file(destination);
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(file.readAll(), QByteArrayLiteral("hello"));
    file.close();
    QFile::remove(destination);

    QVERIFY(!client.downloadFile(QStringLiteral("abc123"), QStringLiteral("/missing.txt"),
                                 destination, &error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_NOT_FOUND"));

    const QString uploadSource = QDir::tempPath()
                                 + QStringLiteral("/podstack-upload-%1.txt")
                                       .arg(QCoreApplication::applicationPid());
    QFile uploadFile(uploadSource);
    QVERIFY(uploadFile.open(QIODevice::WriteOnly));
    uploadFile.write("upload-content");
    uploadFile.close();

    QVERIFY2(client.uploadFile(QStringLiteral("abc123"), QStringLiteral("/tmp"),
                               uploadSource, &error),
             qPrintable(error.message));
    QVERIFY(server.lastBody().contains("upload-content"));
    QFile::remove(uploadSource);

    QVERIFY(!client.uploadFile(QStringLiteral("abc123"), QStringLiteral("/tmp"),
                               QStringLiteral("/tmp/does-not-exist.txt"), &error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_IO_ERROR"));
}

void ContractTest::catalog()
{
    MockPodmanServer server;
    PodStack::PodmanClient client(server.socketPath());
    PodStack::Error error;

    const QVector<PodStack::ImageInfo> images = client.listImages(false, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QCOMPARE(images.size(), 2);
    QCOMPARE(images.first().repoTags.first(),
             QStringLiteral("docker.io/library/alpine:3.20"));
    QVERIFY(!images.first().dangling);
    QVERIFY(images.at(1).dangling);
    QCOMPARE(images.first().size, 7000000);

    const QJsonObject image = client.inspectImage(QStringLiteral("img1234567890"), &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QCOMPARE(image.value(QStringLiteral("Digest")).toString(),
             QStringLiteral("sha256:deadbeef"));

    const QVector<PodStack::ImageHistoryEntry> history =
        client.imageHistory(QStringLiteral("img1234567890"), &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QCOMPARE(history.size(), 1);
    QCOMPARE(history.first().size, 123);

    QStringList pushLines;
    QVERIFY2(client.pushImage(
                 QStringLiteral("img1234567890"), QStringLiteral("repo"),
                 [&pushLines](const QByteArray &line) {
                     pushLines.append(QString::fromUtf8(line));
                     return true;
                 },
                 nullptr, &error),
             qPrintable(error.message));
    QVERIFY(pushLines.size() >= 2);

    QStringList pullLines;
    QVERIFY2(client.pullImage(
                 QStringLiteral("alpine:3.20"), {},
                 [&pullLines](const QByteArray &line) {
                     pullLines.append(QString::fromUtf8(line));
                     return true;
                 },
                 nullptr, &error),
             qPrintable(error.message));
    QVERIFY(pullLines.size() >= 3);

    QVERIFY(!client.pullImage(
        QStringLiteral("fail"), {},
        [](const QByteArray &) { return true; }, nullptr, &error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_ENGINE_ERROR"));

    QVERIFY(client.imageHistory(QStringLiteral("missing"), &error).isEmpty());
    QCOMPARE(error.code, QStringLiteral("PODSTACK_NOT_FOUND"));

    PodStack::ExecResult execResult;
    QVERIFY(!client.execOnce(QStringLiteral("missing"), {QStringLiteral("true")},
                             &execResult, &error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_NOT_FOUND"));

    QVERIFY2(client.tagImage(QStringLiteral("img1234567890"), QStringLiteral("repo"),
                             QStringLiteral("v1"), &error),
             qPrintable(error.message));
    QVERIFY2(client.untagImage(QStringLiteral("img1234567890"), QStringLiteral("repo"),
                               QStringLiteral("v1"), &error),
             qPrintable(error.message));
    QVERIFY(!client.untagImage(QStringLiteral("img1234567890"), QString(), QString(), &error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_INVALID_PARAMS"));
    QVERIFY2(client.removeImage(QStringLiteral("img1234567890"), false, &error),
             qPrintable(error.message));

    qint64 reclaimed = 0;
    QVERIFY2(client.pruneImages(false, &reclaimed, &error), qPrintable(error.message));
    QCOMPARE(reclaimed, 12345);

    const QVector<PodStack::VolumeInfo> volumes = client.listVolumes(&error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QCOMPARE(volumes.size(), 1);
    QCOMPARE(volumes.first().name, QStringLiteral("vol1"));
    QCOMPARE(volumes.first().size, 2048);
    QCOMPARE(volumes.first().links, 1);

    const QJsonObject createdVolume =
        client.createVolume(QStringLiteral("vol2"), QStringLiteral("local"), {}, {}, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QCOMPARE(createdVolume.value(QStringLiteral("Name")).toString(), QStringLiteral("vol2"));

    QVERIFY2(client.removeVolume(QStringLiteral("vol1"), false, &error),
             qPrintable(error.message));
    qint64 volumeReclaimed = 0;
    QVERIFY2(client.pruneVolumes(&volumeReclaimed, &error), qPrintable(error.message));
    QCOMPARE(volumeReclaimed, 456);

    const QString sourceDir = QStringLiteral("/tmp/podstack-mock-volume");
    const QString destinationDir = QStringLiteral("/tmp/podstack-mock-volume2");
    QDir(sourceDir).removeRecursively();
    QDir(destinationDir).removeRecursively();
    QDir().mkpath(sourceDir);
    QFile sourceFile(sourceDir + QStringLiteral("/a.txt"));
    QVERIFY(sourceFile.open(QIODevice::WriteOnly));
    sourceFile.write("hello");
    sourceFile.close();

    QVERIFY2(client.cloneVolume(QStringLiteral("vol1"), QStringLiteral("vol2"), &error),
             qPrintable(error.message));
    QFile cloned(destinationDir + QStringLiteral("/a.txt"));
    QVERIFY(cloned.open(QIODevice::ReadOnly));
    QCOMPARE(cloned.readAll(), QByteArrayLiteral("hello"));
    cloned.close();

    const QString archivePath = QDir::tempPath()
                                + QStringLiteral("/podstack-volume-%1.tar.zst")
                                      .arg(QCoreApplication::applicationPid());
    QFile::remove(archivePath);
    QVERIFY2(client.exportVolume(QStringLiteral("vol1"), archivePath, &error),
             qPrintable(error.message));
    QVERIFY(QFileInfo(archivePath).size() > 0);

    QDir(destinationDir).removeRecursively();
    QDir().mkpath(destinationDir);
    QVERIFY2(client.importVolume(QStringLiteral("vol2"), archivePath, true, &error),
             qPrintable(error.message));
    QFile restored(destinationDir + QStringLiteral("/a.txt"));
    QVERIFY(restored.open(QIODevice::ReadOnly));
    QCOMPARE(restored.readAll(), QByteArrayLiteral("hello"));
    restored.close();
    QFile::remove(archivePath);

    const QString hostDir = QStringLiteral("/tmp/podstack-host-src");
    QDir(hostDir).removeRecursively();
    QDir().mkpath(hostDir);
    QFile hostFile(hostDir + QStringLiteral("/b.txt"));
    QVERIFY(hostFile.open(QIODevice::WriteOnly));
    hostFile.write("host-content");
    hostFile.close();
    QVERIFY2(client.importHostPath(QStringLiteral("vol2"), hostDir, &error),
             qPrintable(error.message));
    QFile imported(destinationDir + QStringLiteral("/b.txt"));
    QVERIFY(imported.open(QIODevice::ReadOnly));
    QCOMPARE(imported.readAll(), QByteArrayLiteral("host-content"));
    imported.close();
    QDir(hostDir).removeRecursively();

    const QVector<PodStack::NetworkInfo> networks = client.listNetworks(&error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QCOMPARE(networks.size(), 1);
    QCOMPARE(networks.first().name, QStringLiteral("mock-net"));
    QCOMPARE(networks.first().subnet, QStringLiteral("10.89.0.0/24"));
    QCOMPARE(networks.first().containers, 1);

    const QJsonObject network =
        client.createNetwork(QStringLiteral("mock-net"), QStringLiteral("bridge"),
                             QStringLiteral("10.90.0.0/24"), QStringLiteral("10.90.0.1"),
                             false, {}, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QCOMPARE(network.value(QStringLiteral("name")).toString(), QStringLiteral("mock-net"));

    QVERIFY2(client.connectNetwork(QStringLiteral("mock-net"), QStringLiteral("abc123"),
                                   {QStringLiteral("api")}, &error),
             qPrintable(error.message));
    QVERIFY2(client.disconnectNetwork(QStringLiteral("mock-net"),
                                      QStringLiteral("abc123"), &error),
             qPrintable(error.message));
    QVERIFY2(client.removeNetwork(QStringLiteral("mock-net"), false, &error),
             qPrintable(error.message));

    const QVector<PodStack::PodInfo> pods = client.listPods(&error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QCOMPARE(pods.size(), 1);
    QCOMPARE(pods.first().name, QStringLiteral("demo-pod"));
    QCOMPARE(pods.first().containers, 2);

    const QJsonObject pod =
        client.createPod(QStringLiteral("demo-pod"), {}, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QCOMPARE(pod.value(QStringLiteral("Id")).toString(), QStringLiteral("pod123"));
    QVERIFY2(client.podAction(QStringLiteral("pod123"), QStringLiteral("start"), &error),
             qPrintable(error.message));
    QVERIFY2(client.removePod(QStringLiteral("pod123"), true, &error),
             qPrintable(error.message));

    QVERIFY(!client.podAction(QStringLiteral("pod123"), QStringLiteral("invalid"), &error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_INVALID_PARAMS"));

    const QJsonObject df = client.systemDf(&error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QCOMPARE(static_cast<qint64>(df.value(QStringLiteral("ImagesSize")).toDouble()),
             7001000);

    QDir(sourceDir).removeRecursively();
    QDir(destinationDir).removeRecursively();
}

void ContractTest::buildContext()
{
    MockPodmanServer server;
    PodStack::PodmanClient client(server.socketPath());
    PodStack::Error error;

    QTemporaryDir context;
    QVERIFY(context.isValid());
    QFile dockerfile(context.path() + QStringLiteral("/Dockerfile"));
    QVERIFY(dockerfile.open(QIODevice::WriteOnly));
    dockerfile.write("FROM alpine:3.20\n");
    dockerfile.close();

    QStringList lines;
    QString imageId;
    QVERIFY2(client.buildImage(
                 context.path(), QStringLiteral("Dockerfile"),
                 QStringLiteral("localhost/podstack-test:latest"), {}, QStringLiteral("linux/amd64"),
                 [&lines](const QByteArray &line) {
                     lines.append(QString::fromUtf8(line));
                     return true;
                 },
                 nullptr, &imageId, &error),
             qPrintable(error.message));
    QCOMPARE(imageId, QStringLiteral("sha256:built123"));
    QVERIFY(lines.size() >= 2);

    QVERIFY(!client.buildImage(QStringLiteral("/nonexistent-context"),
                               QStringLiteral("Dockerfile"),
                               QStringLiteral("localhost/podstack-test:latest"), {}, {},
                               [](const QByteArray &) { return true; }, nullptr, &imageId,
                               &error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_INVALID_PARAMS"));

    QVERIFY(!client.buildImage(context.path(), QStringLiteral("Missing.Dockerfile"),
                               QStringLiteral("localhost/podstack-test:latest"), {}, {},
                               [](const QByteArray &) { return true; }, nullptr, &imageId,
                               &error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_INVALID_PARAMS"));

    QVERIFY(!client.buildImage(context.path(), QStringLiteral("Dockerfile"), {}, {}, {},
                               [](const QByteArray &) { return true; }, nullptr, &imageId,
                               &error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_INVALID_PARAMS"));
}

void ContractTest::catalogErrors()
{
    MockPodmanServer server;
    PodStack::PodmanClient client(server.socketPath());
    PodStack::Error error;

    QVERIFY(client.inspectImage(QStringLiteral("missing"), &error).isEmpty());
    QCOMPARE(error.code, QStringLiteral("PODSTACK_NOT_FOUND"));

    QVERIFY(!client.pullImage(QString(), {}, [](const QByteArray &) { return true; },
                              nullptr, &error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_INVALID_PARAMS"));

    QVERIFY(!client.pushImage(QStringLiteral("img"), QString(),
                              [](const QByteArray &) { return true; }, nullptr, &error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_INVALID_PARAMS"));

    QVERIFY(!client.exportImage(QStringLiteral("img"), QString(), &error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_INVALID_PARAMS"));

    QVERIFY(!client.exportImage(QStringLiteral("img1234567890"),
                                QStringLiteral("/nonexistent/dir/image.tar"), &error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_IO_ERROR"));

    QVERIFY(!client.loadImage(QStringLiteral("/nonexistent/file.tar"), &error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_IO_ERROR"));

    QVERIFY(client.inspectVolume(QStringLiteral("missing"), &error).isEmpty());
    QCOMPARE(error.code, QStringLiteral("PODSTACK_NOT_FOUND"));

    QVERIFY(!client.cloneVolume(QString(), QString(), &error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_INVALID_PARAMS"));

    QVERIFY(!client.cloneVolume(QStringLiteral("missing"), QStringLiteral("x"), &error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_NOT_FOUND"));

    const QString archive = QDir::tempPath()
                            + QStringLiteral("/podstack-err-volume.tar.zst");
    QVERIFY(!client.exportVolume(QStringLiteral("missing"), archive, &error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_NOT_FOUND"));

    QVERIFY(!client.exportVolume(QStringLiteral("vol1"),
                                 QStringLiteral("/nonexistent/dir/x.tar.zst"), &error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_ARCHIVE_ERROR"));

    QVERIFY(!client.importVolume(QString(), QString(), false, &error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_INVALID_PARAMS"));

    QVERIFY(!client.importVolume(QStringLiteral("vol1"),
                                 QStringLiteral("/nonexistent/file.tar.zst"), false, &error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_CONFLICT"));

    QVERIFY(!client.importHostPath(QString(), QString(), &error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_INVALID_PARAMS"));

    QVERIFY(!client.importHostPath(QStringLiteral("vol1"),
                                   QStringLiteral("/nonexistent/dir"), &error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_IO_ERROR"));

    QVERIFY(client.createNetwork(QString(), QStringLiteral("bridge"), QString(), QString(),
                                 false, {}, &error)
                .isEmpty());
    QCOMPARE(error.code, QStringLiteral("PODSTACK_INVALID_PARAMS"));

    QVERIFY(!client.removeNetwork(QStringLiteral("missing"), false, &error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_NOT_FOUND"));

    QVERIFY(client.createPod(QString(), {}, &error).isEmpty());
    QCOMPARE(error.code, QStringLiteral("PODSTACK_INVALID_PARAMS"));

    QVERIFY(!client.removePod(QStringLiteral("missing"), true, &error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_NOT_FOUND"));
}

void ContractTest::errorMapping()
{
    MockPodmanServer server;
    PodStack::PodmanClient client(server.socketPath());
    PodStack::Error error;

    QVERIFY(!client.startContainer(QStringLiteral("missing"), &error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_NOT_FOUND"));

    QVERIFY(!client.startContainer(QStringLiteral("fail"), &error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_ENGINE_ERROR"));

    PodStack::ExecResult exec;
    QVERIFY(!client.execOnce(QStringLiteral("abc123"), {}, &exec, &error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_INVALID_PARAMS"));

    QVERIFY(client.inspectContainer(QStringLiteral("missing"), &error).isEmpty());
    QCOMPARE(error.code, QStringLiteral("PODSTACK_NOT_FOUND"));
}

void ContractTest::pingFailure()
{
    QString socketPath;
    {
        MockPodmanServer server;
        socketPath = server.socketPath();
    }

    PodStack::PodmanClient client(socketPath);
    PodStack::Error error;
    QVERIFY(!client.ping(&error));
    QVERIFY(!error.isEmpty());
}

} // namespace

QTEST_GUILESS_MAIN(ContractTest)
#include "test_contract.moc"
