// SPDX-License-Identifier: GPL-3.0-or-later
#include "daemon/StatsCollector.h"
#include "podman/PodmanClient.h"

#include <QJsonArray>
#include <QTest>

namespace {

class StatsTest : public QObject {
    Q_OBJECT

private slots:
    void parsesProcStat();
    void parsesMemInfo();
    void parsesNetDevIgnoringLoopback();
    void parsesDiskStats();
    void malformedInputsReturnZero();
    void samplesContainersAndTotals();
};

void StatsTest::parsesProcStat()
{
    quint64 total = 0;
    quint64 idle = 0;
    PodStack::StatsCollector::parseProcStat(
        QByteArrayLiteral("cpu  100 0 50 850 0 0 0 0\ncpu0 1 2 3 4\n"), &total, &idle);
    QCOMPARE(total, quint64(1000));
    QCOMPARE(idle, quint64(850));
}

void StatsTest::parsesMemInfo()
{
    qint64 used = 0;
    qint64 total = 0;
    PodStack::StatsCollector::parseMemInfo(
        QByteArrayLiteral("MemTotal:       1000 kB\nMemAvailable:    400 kB\n"), &used,
        &total);
    QCOMPARE(total, qint64(1024000));
    QCOMPARE(used, qint64(614400));
}

void StatsTest::parsesNetDevIgnoringLoopback()
{
    const QByteArray data =
        "Inter-|   Receive                                                |  Transmit\n"
        " face |bytes    packets errs drop fifo frame compressed multicast|bytes    "
        "packets errs drop fifo colls carrier compressed\n"
        "    lo: 9999 0 0 0 0 0 0 0 9999 0 0 0 0 0 0 0\n"
        "  eth0: 1000 0 0 0 0 0 0 0 2000 0 0 0 0 0 0 0\n";

    quint64 rx = 0;
    quint64 tx = 0;
    PodStack::StatsCollector::parseNetDev(data, &rx, &tx);
    QCOMPARE(rx, quint64(1000));
    QCOMPARE(tx, quint64(2000));
}

void StatsTest::parsesDiskStats()
{
    const QByteArray data = "   8       0 sda 1 0 100 0 2 0 200 0 0 0 0\n";
    quint64 readBytes = 0;
    quint64 writeBytes = 0;
    PodStack::StatsCollector::parseDiskStats(data, &readBytes, &writeBytes);
    QCOMPARE(readBytes, quint64(100 * 512));
    QCOMPARE(writeBytes, quint64(200 * 512));
}

void StatsTest::malformedInputsReturnZero()
{
    quint64 total = 1;
    quint64 idle = 1;
    PodStack::StatsCollector::parseProcStat("not a stat line", &total, &idle);
    QCOMPARE(total, quint64(0));
    QCOMPARE(idle, quint64(0));
}

void StatsTest::samplesContainersAndTotals()
{
    PodStack::PodmanClient podman;
    PodStack::StatsCollector collector;
    collector.setStatsProvider([](QJsonArray *containers, PodStack::Error *) {
        const QJsonObject network{
            {QStringLiteral("eth0"),
             QJsonObject{{QStringLiteral("RxBytes"), 1000},
                         {QStringLiteral("TxBytes"), 2000}}}};
        const QJsonObject first{
            {QStringLiteral("ContainerID"), QStringLiteral("id-a")},
            {QStringLiteral("Name"), QStringLiteral("web")},
            {QStringLiteral("CPU"), 12.5},
            {QStringLiteral("MemUsage"), 1048576},
            {QStringLiteral("MemLimit"), 10485760},
            {QStringLiteral("MemPerc"), 10.0},
            {QStringLiteral("PIDs"), 3},
            {QStringLiteral("BlockInput"), 100},
            {QStringLiteral("BlockOutput"), 200},
            {QStringLiteral("Network"), network},
        };
        const QJsonObject second{
            {QStringLiteral("ContainerID"), QStringLiteral("id-b")},
            {QStringLiteral("Name"), QStringLiteral("db")},
            {QStringLiteral("CPU"), 2.0},
            {QStringLiteral("MemUsage"), 2097152},
            {QStringLiteral("MemLimit"), 10485760},
            {QStringLiteral("MemPerc"), 20.0},
            {QStringLiteral("PIDs"), 1},
        };
        *containers = QJsonArray{first, second};
        return true;
    });

    const QJsonObject sample = collector.sample(podman);
    QVERIFY(!sample.isEmpty());
    QCOMPARE(sample.value(QStringLiteral("containers")).toArray().size(), 2);
    QCOMPARE(sample.value(QStringLiteral("totals"))
                 .toObject()
                 .value(QStringLiteral("pids"))
                 .toInt(),
             4);
    QVERIFY(sample.value(QStringLiteral("host"))
                .toObject()
                .value(QStringLiteral("memTotal"))
                .toDouble()
            > 0);

    const QJsonObject secondSample = collector.sample(podman);
    QVERIFY(secondSample.value(QStringLiteral("ts")).toDouble()
            >= sample.value(QStringLiteral("ts")).toDouble());
}

} // namespace

QTEST_GUILESS_MAIN(StatsTest)
#include "test_stats.moc"
