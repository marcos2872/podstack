// SPDX-License-Identifier: GPL-3.0-or-later

#include "daemon/DnsServer.h"

#include "core/Logging.h"

#include <QHostAddress>
#include <QStringList>
#include <QTcpServer>
#include <QTcpSocket>
#include <QUdpSocket>

#include <memory>

namespace PodStack {
namespace {

constexpr quint16 kTypeA = 1;
constexpr quint16 kTypeAAAA = 28;
constexpr quint16 kClassIn = 1;
constexpr quint32 kTtlSeconds = 60;

struct ParsedQuery {
    quint16 id = 0;
    quint16 flags = 0;
    QByteArray name;
    QByteArray question;
    quint16 type = 0;
    quint16 qclass = 0;
};

bool parseQuery(const QByteArray &packet, ParsedQuery *query)
{
    if (packet.size() < 12) {
        return false;
    }
    const auto byteAt = [&packet](int index) { return static_cast<quint8>(packet.at(index)); };

    query->id = static_cast<quint16>((byteAt(0) << 8) | byteAt(1));
    query->flags = static_cast<quint16>((byteAt(2) << 8) | byteAt(3));
    if (query->flags & 0x8000) {
        return false; // resposta, não consulta
    }
    const int questionCount = (byteAt(4) << 8) | byteAt(5);
    if (questionCount != 1) {
        return false;
    }

    int position = 12;
    QStringList labels;
    while (true) {
        if (position >= packet.size()) {
            return false;
        }
        const quint8 length = byteAt(position);
        if (length == 0) {
            ++position;
            break;
        }
        if ((length & 0xC0) != 0 || length > 63 || position + 1 + length > packet.size()) {
            return false;
        }
        labels.append(QString::fromLatin1(packet.mid(position + 1, length)).toLower());
        position += 1 + length;
    }
    if (labels.isEmpty() || position + 4 > packet.size()) {
        return false;
    }

    query->name = labels.join(QLatin1Char('.')).toLatin1();
    query->type = static_cast<quint16>((byteAt(position) << 8) | byteAt(position + 1));
    query->qclass = static_cast<quint16>((byteAt(position + 2) << 8) | byteAt(position + 3));
    query->question = packet.mid(12, position + 4 - 12);
    return true;
}

void appendUint16(QByteArray &output, quint16 value)
{
    output.append(static_cast<char>((value >> 8) & 0xFF));
    output.append(static_cast<char>(value & 0xFF));
}

void appendUint32(QByteArray &output, quint32 value)
{
    output.append(static_cast<char>((value >> 24) & 0xFF));
    output.append(static_cast<char>((value >> 16) & 0xFF));
    output.append(static_cast<char>((value >> 8) & 0xFF));
    output.append(static_cast<char>(value & 0xFF));
}

bool inZone(const QByteArray &name)
{
    return name == "podstack.local" || name.endsWith(".podstack.local");
}

QByteArray addressRecord(quint16 type)
{
    if (type == kTypeA) {
        return QByteArray::fromHex("7f000001"); // 127.0.0.1
    }
    QByteArray address(16, '\0');
    address[15] = 1; // ::1
    return address;
}

} // namespace

DnsServer::DnsServer(QObject *parent) : QObject(parent) {}

bool DnsServer::listen(quint16 port, Error *error)
{
    if (error) {
        *error = {};
    }
    close();

    auto *udp = new QUdpSocket(this);
    // ShareAddress permite conviver com mDNS/Avahi, que costuma ocupar 0.0.0.0:5353;
    // o bind específico em 127.0.0.1 recebe as consultas encaminhadas pelo resolved.
    if (!udp->bind(QHostAddress::LocalHost, port, QUdpSocket::ShareAddress)) {
        const QString detail = udp->errorString();
        delete udp;
        if (error) {
            *error = Error::make(
                QStringLiteral("PODSTACK_IO_ERROR"),
                QStringLiteral("Não foi possível escutar DNS em 127.0.0.1:%1.").arg(port), detail);
        }
        return false;
    }
    m_udp = udp;
    connect(m_udp, &QUdpSocket::readyRead, this, &DnsServer::readDatagrams);

    auto *tcp = new QTcpServer(this);
    if (tcp->listen(QHostAddress::LocalHost, m_udp->localPort())) {
        m_tcp = tcp;
        connect(m_tcp, &QTcpServer::newConnection, this, &DnsServer::acceptTcp);
    } else {
        qCDebug(lcRpc) << "DNS sem listener TCP:" << tcp->errorString();
        delete tcp;
    }

    qCInfo(lcRpc) << "DNS de *.podstack.local em 127.0.0.1:" << m_udp->localPort();
    return true;
}

void DnsServer::close()
{
    const QList<QObject *> objects{m_udp, m_tcp};
    for (QObject *object : objects) {
        if (!object) {
            continue;
        }
        if (auto *udp = qobject_cast<QUdpSocket *>(object)) {
            udp->close();
        } else if (auto *tcp = qobject_cast<QTcpServer *>(object)) {
            tcp->close();
        }
        object->deleteLater();
    }
    m_udp = nullptr;
    m_tcp = nullptr;
}

quint16 DnsServer::port() const
{
    return m_udp ? m_udp->localPort() : 0;
}

QByteArray DnsServer::answer(const QByteArray &query)
{
    ParsedQuery parsed;
    if (!parseQuery(query, &parsed)) {
        return {};
    }

    const bool zoneMatch = inZone(parsed.name);
    const bool addressAnswer = zoneMatch && parsed.qclass == kClassIn &&
                               (parsed.type == kTypeA || parsed.type == kTypeAAAA);

    QByteArray response;
    response.reserve(12 + parsed.question.size() + 16);
    appendUint16(response, parsed.id);
    quint16 flags = static_cast<quint16>(0x8000 | (parsed.flags & 0x0100) | 0x0080);
    if (!zoneMatch) {
        flags |= 3; // NXDOMAIN
    }
    appendUint16(response, flags);
    appendUint16(response, 1);
    appendUint16(response, addressAnswer ? 1 : 0);
    appendUint16(response, 0);
    appendUint16(response, 0);
    response.append(parsed.question);

    if (!addressAnswer) {
        return response;
    }

    response.append(static_cast<char>(0xC0));
    response.append(static_cast<char>(0x0C));
    appendUint16(response, parsed.type);
    appendUint16(response, kClassIn);
    appendUint32(response, kTtlSeconds);
    const QByteArray address = addressRecord(parsed.type);
    appendUint16(response, static_cast<quint16>(address.size()));
    response.append(address);
    return response;
}

void DnsServer::readDatagrams()
{
    while (m_udp->hasPendingDatagrams()) {
        const qint64 pending = m_udp->pendingDatagramSize();
        if (pending <= 0) {
            continue;
        }
        QByteArray datagram;
        datagram.resize(static_cast<int>(pending));
        QHostAddress sender;
        quint16 senderPort = 0;
        const qint64 read =
            m_udp->readDatagram(datagram.data(), datagram.size(), &sender, &senderPort);
        if (read < 0) {
            continue;
        }
        datagram.resize(static_cast<int>(read));
        const QByteArray response = answer(datagram);
        if (!response.isEmpty()) {
            m_udp->writeDatagram(response, sender, senderPort);
        }
    }
}

void DnsServer::acceptTcp()
{
    while (m_tcp->hasPendingConnections()) {
        QTcpSocket *socket = m_tcp->nextPendingConnection();
        if (!socket) {
            break;
        }
        auto buffer = std::make_shared<QByteArray>();
        auto answered = std::make_shared<bool>(false);
        connect(socket, &QTcpSocket::readyRead, this, [socket, buffer, answered] {
            if (*answered) {
                return;
            }
            buffer->append(socket->readAll());
            if (buffer->size() < 2) {
                return;
            }
            const int length =
                (static_cast<quint8>(buffer->at(0)) << 8) | static_cast<quint8>(buffer->at(1));
            if (buffer->size() < 2 + length) {
                return;
            }
            *answered = true;
            const QByteArray response = DnsServer::answer(buffer->mid(2, length));
            if (response.isEmpty()) {
                socket->disconnectFromHost();
                return;
            }
            QByteArray framed;
            framed.append(static_cast<char>((response.size() >> 8) & 0xFF));
            framed.append(static_cast<char>(response.size() & 0xFF));
            framed.append(response);
            socket->write(framed);
            socket->disconnectFromHost();
        });
        connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
    }
}

} // namespace PodStack
