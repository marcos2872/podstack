// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/config/Paths.h"
#include "core/rpc/DaemonBootstrap.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLocalServer>
#include <QLocalSocket>
#include <QStringList>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>

namespace {

/// Servidor de controle falso. Responde ao `system.hello` como o daemon real
/// — a menos que `startOnMarker` ainda não tenha visto o marcador, simulando
/// um daemon que aceita conexão mas não processa nada (zumbi).
class FakeControlServer : public QThread {
public:
    FakeControlServer(QString socketPath, QString marker, bool startOnMarker, bool respond,
                      bool gateOnMarker)
        : m_socketPath(std::move(socketPath)), m_marker(std::move(marker)),
          m_startOnMarker(startOnMarker), m_respond(respond), m_gateOnMarker(gateOnMarker)
    {
    }

    void stop()
    {
        requestInterruption();
        wait(3000);
    }

protected:
    void run() override
    {
        if (m_startOnMarker) {
            while (!isInterruptionRequested() && !QFileInfo::exists(m_marker)) {
                QThread::msleep(5);
            }
        }
        if (isInterruptionRequested()) {
            return;
        }
        QLocalServer::removeServer(m_socketPath);
        QLocalServer server;
        if (!server.listen(m_socketPath)) {
            return;
        }

        while (!isInterruptionRequested()) {
            if (!server.waitForNewConnection(50)) {
                continue;
            }
            while (QLocalSocket *socket = server.nextPendingConnection()) {
                socket->waitForReadyRead(500);
                socket->readAll();
                const bool gated = m_gateOnMarker && !QFileInfo::exists(m_marker);
                if (m_respond && !gated) {
                    socket->write("{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":{\"apiVersion\":1}}\n");
                    socket->waitForBytesWritten(200);
                }
                socket->disconnectFromServer();
                delete socket;
            }
        }
        server.close();
        QLocalServer::removeServer(m_socketPath);
    }

private:
    QString m_socketPath;
    QString m_marker;
    bool m_startOnMarker = false;
    bool m_respond = true;
    bool m_gateOnMarker = false;
};

} // namespace

class DaemonBootstrapTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void unitInstalledFollowsSearchPaths();
    void startsStoppedSocket();
    void reportsHungDaemon();
    void restartRecoversHungDaemon();
    void retriesAndReportsFailure();
    void skipsWhenUnitIsMissing();
    void skipsWithCustomSocket();
    void throttlesRepeatedAttempts();
    void startsEngineSocket();

private:
    QString writeScript(const QString &name, const QString &body);
    void writeUnit();
    void removeUnit();
    QStringList invocations(const QString &logPath) const;
    int countInvocations(const QStringList &lines, const QString &needle) const;

    QTemporaryDir m_runtimeDir;
    QTemporaryDir m_unitDir;
    QString m_socketPath;
    QString m_unitPath;
    QString m_logPath;
    QString m_markerPath;
};

QString DaemonBootstrapTest::writeScript(const QString &name, const QString &body)
{
    const QString path = m_unitDir.path() + QLatin1Char('/') + name;
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return {};
    }
    file.write(body.toUtf8());
    file.close();
    file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
    return path;
}

void DaemonBootstrapTest::writeUnit()
{
    QFile unit(m_unitPath);
    if (!unit.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return;
    }
    unit.write("[Socket]\nListenStream=%t/podstack/control.sock\n");
    unit.close();
}

void DaemonBootstrapTest::removeUnit()
{
    QFile::remove(m_unitPath);
}

QStringList DaemonBootstrapTest::invocations(const QString &logPath) const
{
    QFile log(logPath);
    if (!log.open(QIODevice::ReadOnly)) {
        return {};
    }
    const QString payload = QString::fromUtf8(log.readAll());
    QStringList lines;
    for (const QString &line : payload.split(QLatin1Char('\n'))) {
        if (!line.isEmpty()) {
            lines.append(line);
        }
    }
    return lines;
}

int DaemonBootstrapTest::countInvocations(const QStringList &lines, const QString &needle) const
{
    int count = 0;
    for (const QString &line : lines) {
        if (line.contains(needle)) {
            ++count;
        }
    }
    return count;
}

void DaemonBootstrapTest::initTestCase()
{
    QVERIFY(m_runtimeDir.isValid());
    QVERIFY(m_unitDir.isValid());
    qputenv("XDG_RUNTIME_DIR", m_runtimeDir.path().toUtf8());
    qputenv("PODSTACK_USER_UNIT_DIR", m_unitDir.path().toUtf8());
    qunsetenv("PODSTACK_CONTROL_SOCKET");

    m_socketPath = PodStack::Paths::controlSocket();
    QCOMPARE(m_socketPath, m_runtimeDir.path() + QStringLiteral("/podstack/control.sock"));
    QVERIFY(QDir().mkpath(QFileInfo(m_socketPath).absolutePath()));

    m_unitPath = m_unitDir.path() + QStringLiteral("/podstack.socket");
    m_logPath = m_unitDir.path() + QStringLiteral("/systemctl.log");
    m_markerPath = m_unitDir.path() + QStringLiteral("/start.marker");
}

void DaemonBootstrapTest::unitInstalledFollowsSearchPaths()
{
    removeUnit();
    QVERIFY(!PodStack::DaemonBootstrap::unitInstalled());
    QVERIFY(PodStack::DaemonBootstrap::unitSearchPaths().first().startsWith(m_unitDir.path()));

    writeUnit();
    QVERIFY(PodStack::DaemonBootstrap::unitInstalled());
}

void DaemonBootstrapTest::startsStoppedSocket()
{
    QFile::remove(m_socketPath);
    QFile::remove(m_logPath);
    QFile::remove(m_markerPath);
    writeUnit();
    const QString script =
        writeScript(QStringLiteral("systemctl-ok.sh"),
                    QStringLiteral("#!/bin/sh\n"
                                   "echo \"$@\" >> '%1'\n"
                                   "case \"$*\" in\n"
                                   "  *\"--user start podstack.socket\"*) : > '%2' ;;\n"
                                   "esac\n"
                                   "exit 0\n")
                        .arg(m_logPath, m_markerPath));
    QVERIFY(!script.isEmpty());
    qputenv("PODSTACK_SYSTEMCTL", script.toUtf8());

    FakeControlServer listener(m_socketPath, m_markerPath, true, true, false);
    listener.start();

    QString detail;
    const auto status = PodStack::DaemonBootstrap::ensureRunning({}, &detail);
    listener.stop();

    QCOMPARE(static_cast<int>(status),
             static_cast<int>(PodStack::DaemonBootstrap::Status::Started));
    QVERIFY2(detail.isEmpty(), qPrintable(detail));

    const QStringList lines = invocations(m_logPath);
    QCOMPARE(countInvocations(lines, QStringLiteral("--user daemon-reload")), 1);
    QCOMPARE(countInvocations(lines, QStringLiteral("--user start podstack.socket")), 1);
}

void DaemonBootstrapTest::reportsHungDaemon()
{
    QFile::remove(m_socketPath);
    QFile::remove(m_logPath);
    QFile::remove(m_markerPath);
    writeUnit();
    const QString script =
        writeScript(QStringLiteral("systemctl-hung.sh"), QStringLiteral("#!/bin/sh\n"
                                                                        "echo \"$@\" >> '%1'\n"
                                                                        "exit 0\n")
                                                             .arg(m_logPath));
    QVERIFY(!script.isEmpty());
    qputenv("PODSTACK_SYSTEMCTL", script.toUtf8());

    // Aceita conexões e nunca responde: é o daemon travado que fazia a GUI
    // mostrar "0 containers" com o engine cheio.
    FakeControlServer listener(m_socketPath, {}, false, false, false);
    listener.start();
    QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(m_socketPath), 3000);

    const auto probe = PodStack::DaemonBootstrap::probe({}, 300);
    QVERIFY2(probe.connectable, "o socket deveria aceitar conexão");
    QVERIFY2(!probe.responds, "socket mudo não pode ser reportado como saudável");

    QString detail;
    const auto status = PodStack::DaemonBootstrap::ensureRunning({}, &detail);
    listener.stop();

    QCOMPARE(static_cast<int>(status), static_cast<int>(PodStack::DaemonBootstrap::Status::Failed));
    QVERIFY(detail.contains(QStringLiteral("não respondeu")));
    QVERIFY(PodStack::DaemonBootstrap::manualRemedy().contains(
        QStringLiteral("systemctl --user restart podstack.service")));
}

void DaemonBootstrapTest::startsEngineSocket()
{
    const QString podmanSocket = m_unitDir.path() + QStringLiteral("/podman/podman.sock");
    QDir().mkpath(QFileInfo(podmanSocket).absolutePath());
    QFile::remove(podmanSocket);
    QFile::remove(m_logPath);

    const QString script =
        writeScript(QStringLiteral("systemctl-engine.sh"),
                    QStringLiteral("#!/bin/sh\n"
                                   "echo \"$@\" >> '%1'\n"
                                   "case \"$*\" in\n"
                                   "  *\"--user start podman.socket\"*) : > '%2' ;;\n"
                                   "esac\n"
                                   "exit 0\n")
                        .arg(m_logPath, podmanSocket));
    QVERIFY(!script.isEmpty());
    qputenv("PODSTACK_SYSTEMCTL", script.toUtf8());

    QString detail;
    auto status = PodStack::DaemonBootstrap::ensureEngineRunning(podmanSocket, true, &detail);
    QCOMPARE(static_cast<int>(status),
             static_cast<int>(PodStack::DaemonBootstrap::EngineStatus::Started));
    QVERIFY(QFileInfo::exists(podmanSocket));
    QCOMPARE(countInvocations(invocations(m_logPath), QStringLiteral("--user start podman.socket")),
             1);

    status = PodStack::DaemonBootstrap::ensureEngineRunning(podmanSocket, true, &detail);
    QCOMPARE(static_cast<int>(status),
             static_cast<int>(PodStack::DaemonBootstrap::EngineStatus::AlreadyRunning));

    QFile::remove(podmanSocket);
    status = PodStack::DaemonBootstrap::ensureEngineRunning(podmanSocket, false, &detail);
    QCOMPARE(static_cast<int>(status),
             static_cast<int>(PodStack::DaemonBootstrap::EngineStatus::Disabled));

    const QString failing = writeScript(QStringLiteral("systemctl-engine-fail.sh"),
                                        QStringLiteral("#!/bin/sh\nexit 1\n"));
    QVERIFY(!failing.isEmpty());
    qputenv("PODSTACK_SYSTEMCTL", failing.toUtf8());
    status = PodStack::DaemonBootstrap::ensureEngineRunning(podmanSocket, true, &detail);
    QCOMPARE(static_cast<int>(status),
             static_cast<int>(PodStack::DaemonBootstrap::EngineStatus::Failed));
    QVERIFY(!detail.isEmpty());
}

void DaemonBootstrapTest::restartRecoversHungDaemon()
{
    QFile::remove(m_socketPath);
    QFile::remove(m_logPath);
    QFile::remove(m_markerPath);
    writeUnit();
    const QString script =
        writeScript(QStringLiteral("systemctl-restart.sh"),
                    QStringLiteral("#!/bin/sh\n"
                                   "echo \"$@\" >> '%1'\n"
                                   "case \"$*\" in\n"
                                   "  *\"--user restart podstack.service\"*) : > '%2' ;;\n"
                                   "esac\n"
                                   "exit 0\n")
                        .arg(m_logPath, m_markerPath));
    QVERIFY(!script.isEmpty());
    qputenv("PODSTACK_SYSTEMCTL", script.toUtf8());

    // Silencioso até o restart; depois responde — como um daemon que voltou.
    FakeControlServer listener(m_socketPath, m_markerPath, false, true, true);
    listener.start();
    QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(m_socketPath), 3000);

    QVERIFY(!PodStack::DaemonBootstrap::probe({}, 300).responds);

    QString detail;
    const auto status = PodStack::DaemonBootstrap::restart({}, &detail);
    listener.stop();

    QCOMPARE(static_cast<int>(status),
             static_cast<int>(PodStack::DaemonBootstrap::Status::Started));
    QVERIFY2(detail.isEmpty(), qPrintable(detail));
    QCOMPARE(
        countInvocations(invocations(m_logPath), QStringLiteral("--user restart podstack.service")),
        1);
}

void DaemonBootstrapTest::retriesAndReportsFailure()
{
    QFile::remove(m_socketPath);
    QFile::remove(m_logPath);
    writeUnit();
    const QString script = writeScript(
        QStringLiteral("systemctl-fail.sh"),
        QStringLiteral("#!/bin/sh\n"
                       "echo \"$@\" >> '%1'\n"
                       "case \"$*\" in\n"
                       "  *\"--user start podstack.socket\"*)\n"
                       "    echo 'Failed to start podstack.socket: Unit not found.' >&2\n"
                       "    exit 5 ;;\n"
                       "esac\n"
                       "exit 0\n")
            .arg(m_logPath));
    QVERIFY(!script.isEmpty());
    qputenv("PODSTACK_SYSTEMCTL", script.toUtf8());

    QString detail;
    const auto status = PodStack::DaemonBootstrap::ensureRunning({}, &detail);

    QCOMPARE(static_cast<int>(status), static_cast<int>(PodStack::DaemonBootstrap::Status::Failed));
    QVERIFY(detail.contains(QStringLiteral("Unit not found")));
    QVERIFY(PodStack::DaemonBootstrap::manualRemedy().contains(
        QStringLiteral("systemctl --user restart podstack.service")));

    // Duas tentativas: o retry com backoff precisa aparecer no log do fake.
    const QStringList lines = invocations(m_logPath);
    QCOMPARE(countInvocations(lines, QStringLiteral("--user start podstack.socket")), 2);
    QCOMPARE(countInvocations(lines, QStringLiteral("--user daemon-reload")), 2);
}

void DaemonBootstrapTest::skipsWhenUnitIsMissing()
{
    QFile::remove(m_socketPath);
    QFile::remove(m_logPath);
    removeUnit();
    qputenv("PODSTACK_SYSTEMCTL",
            writeScript(QStringLiteral("systemctl-unused.sh"),
                        QStringLiteral("#!/bin/sh\necho \"$@\" >> '%1'\nexit 0\n").arg(m_logPath))
                .toUtf8());

    const auto status = PodStack::DaemonBootstrap::ensureRunning({});

    QCOMPARE(static_cast<int>(status),
             static_cast<int>(PodStack::DaemonBootstrap::Status::NotInstalled));
    QVERIFY(invocations(m_logPath).isEmpty());
}

void DaemonBootstrapTest::skipsWithCustomSocket()
{
    QFile::remove(m_logPath);
    writeUnit();
    qputenv("PODSTACK_SYSTEMCTL",
            writeScript(QStringLiteral("systemctl-custom.sh"),
                        QStringLiteral("#!/bin/sh\necho \"$@\" >> '%1'\nexit 0\n").arg(m_logPath))
                .toUtf8());
    qputenv("PODSTACK_CONTROL_SOCKET", (m_unitDir.path() + QStringLiteral("/dev.sock")).toUtf8());

    const auto status = PodStack::DaemonBootstrap::ensureRunning({});

    QCOMPARE(static_cast<int>(status),
             static_cast<int>(PodStack::DaemonBootstrap::Status::CustomSocket));
    QVERIFY(invocations(m_logPath).isEmpty());
    qunsetenv("PODSTACK_CONTROL_SOCKET");
}

void DaemonBootstrapTest::throttlesRepeatedAttempts()
{
    QFile::remove(m_socketPath);
    QFile::remove(m_logPath);
    writeUnit();
    qputenv("PODSTACK_SYSTEMCTL",
            writeScript(QStringLiteral("systemctl-throttle.sh"),
                        QStringLiteral("#!/bin/sh\necho \"$@\" >> '%1'\nexit 0\n").arg(m_logPath))
                .toUtf8());

    // Sem listener o start "dá certo" mas o socket não responde: a primeira
    // chamada roda o systemctl; a segunda cai no throttle de 5 s.
    const auto first = PodStack::DaemonBootstrap::ensureRunningThrottled({});
    const auto second = PodStack::DaemonBootstrap::ensureRunningThrottled({});

    QCOMPARE(static_cast<int>(first), static_cast<int>(PodStack::DaemonBootstrap::Status::Failed));
    QCOMPARE(static_cast<int>(second),
             static_cast<int>(PodStack::DaemonBootstrap::Status::Throttled));
    QCOMPARE(
        countInvocations(invocations(m_logPath), QStringLiteral("--user start podstack.socket")),
        2);
}

QTEST_GUILESS_MAIN(DaemonBootstrapTest)

#include "test_daemon_bootstrap.moc"
