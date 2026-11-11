#pragma once

#include <QString>
#include <QHash>
#include <QPair>

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
    /// Corpo do último POST /containers/create (o start seguinte não o apaga).
    QByteArray lastCreateBody() const;
    /// Quantidade de POST /containers/create recebidos.
    int createCount() const;

    /// Usa o formato nativo do Libpod (`Created` epoch e portas minúsculas)
    /// na listagem de containers.
    void setNativeContainers(bool enabled);

    /// Responde `path` (sem query) com status/corpo fixos, antes do roteamento padrão.
    void setResponse(const QString &path, int status, const QByteArray &body);

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
    QByteArray m_lastCreateBody;
    int m_createCount = 0;
    std::atomic_bool m_nativeContainers{false};
    QHash<QString, QPair<int, QByteArray>> m_responses;
};
