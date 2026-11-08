#pragma once

#include <QString>

#include <atomic>
#include <mutex>
#include <thread>

/// Servidor HTTP mínimo sobre Unix socket para testes de contrato do PodmanClient.
/// Roda em uma thread POSIX própria, sem depender do event loop do Qt.
class MockPodmanServer {
public:
    MockPodmanServer();
    ~MockPodmanServer();

    MockPodmanServer(const MockPodmanServer &) = delete;
    MockPodmanServer &operator=(const MockPodmanServer &) = delete;

    QString socketPath() const { return m_path; }
    bool isListening() const { return m_listenFd >= 0; }

    QString lastMethod() const;
    QString lastPath() const;
    QByteArray lastBody() const;

private:
    void run();
    void handleClient(int clientFd);

    int m_listenFd = -1;
    int m_stopPipe[2] = {-1, -1};
    QString m_path;
    std::thread m_thread;
    std::atomic_bool m_stopping{false};
    mutable std::mutex m_mutex;
    QString m_lastMethod;
    QString m_lastPath;
    QByteArray m_lastBody;
};
