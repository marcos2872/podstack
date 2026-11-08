#include "podman/PodmanClient.h"

#include <QCoreApplication>
#include <QProcess>
#include <QScopeGuard>
#include <QTest>

namespace {

class PodmanTest : public QObject {
    Q_OBJECT

private slots:
    void pingAndList();
    void execEcho();
};

void PodmanTest::pingAndList()
{
    PodStack::PodmanClient client;
    PodStack::Error error;

    if (!client.ping(&error)) {
        QSKIP("Podman indisponível; teste de integração ignorado.");
    }

    QVERIFY(!client.apiVersion().isEmpty());

    const QVector<PodStack::ContainerInfo> containers = client.listContainers(true, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    Q_UNUSED(containers);
}

void PodmanTest::execEcho()
{
    const QString image = QStringLiteral("docker.io/library/debian:bookworm-slim");
    if (QProcess::execute(QStringLiteral("podman"),
                          {QStringLiteral("image"), QStringLiteral("exists"), image})
        != 0) {
        QSKIP("imagem debian:bookworm-slim indisponível; teste de exec ignorado.");
    }

    const QString name =
        QStringLiteral("pstest-exec-%1").arg(QCoreApplication::applicationPid());
    QProcess::execute(QStringLiteral("podman"),
                      {QStringLiteral("rm"), QStringLiteral("-f"), name});
    const auto cleanup = qScopeGuard([&] {
        QProcess::execute(QStringLiteral("podman"),
                          {QStringLiteral("rm"), QStringLiteral("-f"), name});
    });

    const int runResult = QProcess::execute(
        QStringLiteral("podman"),
        {QStringLiteral("run"), QStringLiteral("-d"), QStringLiteral("--rm"),
         QStringLiteral("--name"), name, image, QStringLiteral("sleep"), QStringLiteral("60")});
    if (runResult != 0) {
        QSKIP("não foi possível criar o container de teste.");
    }

    PodStack::PodmanClient client;
    PodStack::Error error;
    PodStack::ExecResult result;
    const bool ok = client.execOnce(name, {QStringLiteral("/bin/echo"), QStringLiteral("hello")},
                                    &result, &error);

    QVERIFY2(ok, qPrintable(error.message));
    QVERIFY(result.output.contains("hello"));
    QCOMPARE(result.exitCode, 0);
}

} // namespace

QTEST_GUILESS_MAIN(PodmanTest)
#include "test_podman.moc"
