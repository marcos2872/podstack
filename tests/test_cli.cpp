#include "MockPodmanServer.h"

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
    void errorPaths();

private:
    QString cliPath() const;
    QString daemonPath() const;
    QProcessEnvironment environment(const QString &controlSocket) const;
    CliResult run(const QStringList &args, const QProcessEnvironment &env) const;

    MockPodmanServer m_mock;
    QProcess m_daemon;
    QString m_controlSocket;
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
}

} // namespace

QTEST_GUILESS_MAIN(CliTest)
#include "test_cli.moc"
