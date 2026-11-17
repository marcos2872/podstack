// SPDX-License-Identifier: GPL-3.0-or-later
#include "daemon/Pools.h"

#include <QElapsedTimer>
#include <QThread>
#include <QtConcurrent>
#include <QtTest>

#include <atomic>

/// Regressão do esgotamento do pool global: streams longos ficam no
/// backgroundPool e não podem impedir um handler RPC de rodar.
class TestPools : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void poolsAreSeparate();
    void rpcRespondsWhileBackgroundIsSaturated();
};

void TestPools::initTestCase()
{
    // Precisa ser definido antes da primeira chamada a backgroundPool().
    qputenv("PODSTACK_BACKGROUND_POOL_SIZE", "1");
}

void TestPools::poolsAreSeparate()
{
    QVERIFY(PodStack::rpcPool() != nullptr);
    QVERIFY(PodStack::backgroundPool() != nullptr);
    QVERIFY(PodStack::rpcPool() != PodStack::backgroundPool());
    QCOMPARE(PodStack::backgroundPool()->maxThreadCount(), 1);
    QVERIFY(PodStack::rpcPool()->maxThreadCount() >= 4);
}

void TestPools::rpcRespondsWhileBackgroundIsSaturated()
{
    std::atomic_bool release{false};
    std::atomic_bool started{false};

    QFuture<int> blocked = QtConcurrent::run(PodStack::backgroundPool(), [&release, &started] {
        started.store(true);
        while (!release.load(std::memory_order_acquire)) {
            QThread::msleep(5);
        }
        return 1;
    });
    QTRY_VERIFY_WITH_TIMEOUT(started.load(std::memory_order_acquire), 2000);

    std::atomic_bool rpcDone{false};
    QFuture<int> rpc = QtConcurrent::run(PodStack::rpcPool(), [&rpcDone] {
        rpcDone.store(true, std::memory_order_release);
        return 2;
    });

    QTRY_VERIFY_WITH_TIMEOUT(rpcDone.load(std::memory_order_acquire), 2000);
    QCOMPARE(rpc.result(), 2);

    release.store(true, std::memory_order_release);
    blocked.waitForFinished();
}

QTEST_GUILESS_MAIN(TestPools)
#include "test_pools.moc"
