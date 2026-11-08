#include "MockPodmanServer.h"

#include "gui/Backend.h"
#include "gui/ContainerListModel.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QProcessEnvironment>
#include <QScopeGuard>
#include <QSignalSpy>
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

private:
    QProcessEnvironment daemonEnvironment() const;

    MockPodmanServer m_mock;
    QProcess m_daemon;
    QString m_controlSocket;
};

QProcessEnvironment GuiTest::daemonEnvironment() const
{
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("PODSTACK_CONTROL_SOCKET"), m_controlSocket);
    env.insert(QStringLiteral("PODSTACK_PODMAN_SOCKET"), m_mock.socketPath());
    return env;
}

void GuiTest::initTestCase()
{
    QVERIFY(m_mock.isListening());

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

    model.setContainers({item});
    QCOMPARE(model.count(), 1);
    QCOMPARE(countSpy.count(), 1);
    QCOMPARE(model.rowCount(), 1);
    QCOMPARE(model.rowCount(model.index(0, 0)), 0);
    QCOMPARE(model.roleNames().size(), 5);

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

} // namespace

QTEST_GUILESS_MAIN(GuiTest)
#include "test_gui.moc"
