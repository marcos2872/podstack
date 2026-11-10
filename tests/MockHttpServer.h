// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QByteArray>
#include <QHash>
#include <QMutex>
#include <QString>
#include <QTcpServer>

#include <atomic>

class QThread;

/// Servidor HTTP local mínimo para testes de download, sem rede externa.
class MockHttpServer {
public:
    MockHttpServer();
    ~MockHttpServer();

    MockHttpServer(const MockHttpServer &) = delete;
    MockHttpServer &operator=(const MockHttpServer &) = delete;

    void setResponse(const QString &path, int status, const QByteArray &body);
    QString url(const QString &path = QStringLiteral("/")) const;
    int requestCount(const QString &path) const;
    int totalRequests() const;
    bool isListening() const { return m_port != 0; }

private:
    struct Response {
        int status = 200;
        QByteArray body;
    };

    Response responseFor(const QString &path) const;
    void countRequest(const QString &path);

    mutable QMutex m_mutex;
    QHash<QString, Response> m_responses;
    QHash<QString, int> m_requests;
    int m_totalRequests = 0;
    quint16 m_port = 0;
    std::atomic_bool m_stop{false};
    QThread *m_thread = nullptr;
};
