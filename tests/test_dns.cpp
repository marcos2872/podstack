// SPDX-License-Identifier: GPL-3.0-or-later

#include "daemon/DnsServer.h"

#include <QElapsedTimer>
#include <QHostAddress>
#include <QTcpSocket>
#include <QUdpSocket>
#include <QtTest>

namespace {

QByteArray buildQuery(const QByteArray &name, quint16 type = 1, quint16 id = 0x1234)
{
    QByteArray query;
    query.append(static_cast<char>((id >> 8) & 0xFF));
    query.append(static_cast<char>(id & 0xFF));
    query.append(static_cast<char>(0x01)); // RD
    query.append(static_cast<char>(0x00));
    query.append(static_cast<char>(0x00));
    query.append(static_cast<char>(0x01)); // QDCOUNT=1
    query.append(QByteArray(6, '\0'));

    for (const QByteArray &label : name.split('.')) {
        query.append(static_cast<char>(label.size()));
        query.append(label);
    }
    query.append('\0');
    query.append(static_cast<char>((type >> 8) & 0xFF));
    query.append(static_cast<char>(type & 0xFF));
    query.append(static_cast<char>(0x00));
    query.append(static_cast<char>(0x01)); // IN
    return query;
}

int answerCount(const QByteArray &response)
{
    if (response.size() < 12) {
        return -1;
    }
    return (static_cast<quint8>(response.at(6)) << 8) | static_cast<quint8>(response.at(7));
}

int responseCode(const QByteArray &response)
{
    if (response.size() < 4) {
        return -1;
    }
    return response.at(3) & 0x0F;
}

const QByteArray kARecord = QByteArray::fromHex("c00c000100010000003c00047f000001");
const QByteArray kAaaaRecord =
    QByteArray::fromHex("c00c001c00010000003c001000000000000000000000000000000001");

} // namespace

class DnsTest : public QObject {
    Q_OBJECT

private slots:
    void answersAForContainerName();
    void answersAaaaForContainerName();
    void answersZoneApexAndIsCaseInsensitive();
    void returnsNxdomainOutsideZone();
    void returnsNoDataForUnsupportedType();
    void rejectsMalformedQueries();
    void answersOverUdp();
    void answersOverTcp();
    void listenReportsPortInUse();
};

void DnsTest::answersAForContainerName()
{
    const QByteArray query = buildQuery("web.podstack.local");
    const QByteArray response = PodStack::DnsServer::answer(query);
    QCOMPARE(response.left(2), query.left(2));
    QCOMPARE(responseCode(response), 0);
    QCOMPARE(answerCount(response), 1);
    QVERIFY(response.endsWith(kARecord));
}

void DnsTest::answersAaaaForContainerName()
{
    const QByteArray query = buildQuery("web.podstack.local", 28);
    const QByteArray response = PodStack::DnsServer::answer(query);
    QCOMPARE(responseCode(response), 0);
    QCOMPARE(answerCount(response), 1);
    QVERIFY(response.endsWith(kAaaaRecord));
}

void DnsTest::answersZoneApexAndIsCaseInsensitive()
{
    const QByteArray apex = PodStack::DnsServer::answer(buildQuery("podstack.local"));
    QCOMPARE(answerCount(apex), 1);
    QVERIFY(apex.endsWith(kARecord));

    const QByteArray mixed = PodStack::DnsServer::answer(buildQuery("WEB.PodStack.Local"));
    QCOMPARE(answerCount(mixed), 1);
    QVERIFY(mixed.endsWith(kARecord));
}

void DnsTest::returnsNxdomainOutsideZone()
{
    const QByteArray response = PodStack::DnsServer::answer(buildQuery("example.com"));
    QCOMPARE(responseCode(response), 3);
    QCOMPARE(answerCount(response), 0);
}

void DnsTest::returnsNoDataForUnsupportedType()
{
    const QByteArray response = PodStack::DnsServer::answer(buildQuery("web.podstack.local", 16));
    QCOMPARE(responseCode(response), 0);
    QCOMPARE(answerCount(response), 0);
}

void DnsTest::rejectsMalformedQueries()
{
    QVERIFY(PodStack::DnsServer::answer(QByteArray()).isEmpty());
    QVERIFY(PodStack::DnsServer::answer(QByteArray(11, '\0')).isEmpty());
    QVERIFY(PodStack::DnsServer::answer(buildQuery("web.podstack.local").left(20)).isEmpty());
}

void DnsTest::answersOverUdp()
{
    PodStack::DnsServer server;
    PodStack::Error error;
    QVERIFY(server.listen(0, &error));
    QVERIFY(server.port() != 0);

    QUdpSocket socket;
    const QByteArray query = buildQuery("web.podstack.local");
    QCOMPARE(socket.writeDatagram(query, QHostAddress::LocalHost, server.port()),
             qint64(query.size()));

    QElapsedTimer timer;
    timer.start();
    while (socket.pendingDatagramSize() <= 0 && timer.elapsed() < 3000) {
        QTest::qWait(10);
    }
    QVERIFY(socket.pendingDatagramSize() > 0);

    QByteArray response;
    response.resize(static_cast<int>(socket.pendingDatagramSize()));
    QCOMPARE(socket.readDatagram(response.data(), response.size()), qint64(response.size()));
    QCOMPARE(response, PodStack::DnsServer::answer(query));
}

void DnsTest::answersOverTcp()
{
    PodStack::DnsServer server;
    PodStack::Error error;
    QVERIFY(server.listen(0, &error));

    QTcpSocket socket;
    socket.connectToHost(QHostAddress::LocalHost, server.port());

    QElapsedTimer timer;
    timer.start();
    while (socket.state() != QAbstractSocket::ConnectedState && timer.elapsed() < 3000) {
        QTest::qWait(10);
    }
    QCOMPARE(socket.state(), QAbstractSocket::ConnectedState);

    const QByteArray query = buildQuery("web.podstack.local");
    QByteArray framed;
    framed.append(static_cast<char>((query.size() >> 8) & 0xFF));
    framed.append(static_cast<char>(query.size() & 0xFF));
    framed.append(query);
    socket.write(framed);

    QByteArray response;
    timer.restart();
    while (response.size() < 2 && timer.elapsed() < 3000) {
        QTest::qWait(10);
        response.append(socket.readAll());
    }
    QVERIFY(response.size() >= 2);
    const int length =
        (static_cast<quint8>(response.at(0)) << 8) | static_cast<quint8>(response.at(1));
    while (response.size() < 2 + length && timer.elapsed() < 3000) {
        QTest::qWait(10);
        response.append(socket.readAll());
    }
    QCOMPARE(response.size(), 2 + length);
    QCOMPARE(response.mid(2), PodStack::DnsServer::answer(query));
}

void DnsTest::listenReportsPortInUse()
{
    QUdpSocket busy;
    QVERIFY(busy.bind(QHostAddress::LocalHost, 0));

    PodStack::DnsServer server;
    PodStack::Error error;
    QVERIFY(!server.listen(busy.localPort(), &error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_IO_ERROR"));
    QCOMPARE(server.port(), quint16(0));

    QVERIFY(server.listen(0, &error));
    QVERIFY(server.port() != 0);
    server.close();
    QCOMPARE(server.port(), quint16(0));
}

QTEST_GUILESS_MAIN(DnsTest)

#include "test_dns.moc"
