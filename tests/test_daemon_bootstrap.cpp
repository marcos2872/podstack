// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/config/Paths.h"
#include "core/rpc/DaemonBootstrap.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLocalServer>
#include <QStringList>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>

namespace {

/// Cria o socket de controle só depois que o `systemctl` fake sinaliza o
/// start. Sem isso o probe acharia o socket ativo e o start nunca rodaria.
class DeferredSocketListener : public QThread {
public:
    DeferredSocketListener(QString socketPath, QString marker)
        : m_socketPath(std::move(socketPath)), m_marker(std::move(marker))
    {
    }

    void stop()
    {
        requestInterruption();
        quit();
        wait(3000);
    }

protected:
    void run() override
    {
        while (!isInterruptionRequested() && !QFileInfo::exists(m_marker)) {
            QThread::msleep(5);
        }
        if (isInterruptionRequested()) {
            return;
        }
        QLocalServer::removeServer(m_socketPath);
        QLocalServer server;
        if (!server.listen(m_socketPath)) {
            return;
        }
        exec();
    }

private:
    QString m_socketPath;
    QString m_marker;
};

} // namespace

class DaemonBootstrapTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void unitInstalledFollowsSearchPaths();
    void startsStoppedSocket();
    void retriesAndReportsFailure();
    void skipsWhenUnitIsMissing();
    void skipsWithCustomSocket();
    void throttlesRepeatedAttempts();

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

    DeferredSocketListener listener(m_socketPath, m_markerPath);
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
        QStringLiteral("systemctl --user start podstack.socket")));

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
