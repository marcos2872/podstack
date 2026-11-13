// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QByteArray>
#include <QHash>
#include <QJsonObject>
#include <QMutex>
#include <QPair>
#include <QString>
#include <QVector>

#include <atomic>

class QLocalSocket;
class QThread;

/// Servidor QMP local mínimo para testes, sem QEMU real.
///
/// Envia a saudação `{"QMP": ...}` ao aceitar a conexão, responde a
/// `qmp_capabilities`, `query-status` (status configurável), `system_powerdown`
/// e comandos desconhecidos. `quit` responde com o evento SHUTDOWN e fecha o
/// socket. Falhas, silêncio e eventos intercalados são configuráveis.
class MockQmpServer {
public:
    MockQmpServer();
    ~MockQmpServer();

    MockQmpServer(const MockQmpServer &) = delete;
    MockQmpServer &operator=(const MockQmpServer &) = delete;

    QString socketPath() const { return m_path; }
    bool isListening() const { return m_listening; }

    /// Status retornado por `query-status` (default: running).
    void setStatus(const QString &status);

    /// Faz o comando responder `{"error": {"class": "GenericError", ...}}`.
    void setFailCommand(const QString &command);

    /// Faz o comando não responder (força timeout no cliente).
    void setSilentCommand(const QString &command);

    /// Envia um evento assíncrono antes da resposta do comando indicado.
    void setInterleavedEvent(const QString &command);

    /// Omite a saudação inicial (handshake inválido).
    void setNoGreeting(bool enabled);

    /// Faz `quit` responder `{"return": {}}` antes de fechar o socket.
    void setQuitSendsReturn(bool enabled);

    int commandCount(const QString &command) const;
    int connectionCount() const;
    /// Comandos recebidos, na ordem, com os argumentos (para validar sequências QMP).
    QVector<QPair<QString, QJsonObject>> commands() const;
    void clearCommands();

private:
    struct Behavior {
        QString status;
        bool fail = false;
        bool silent = false;
        bool interleave = false;
        bool quitReturn = false;
    };

    Behavior behaviorFor(const QString &command);
    bool noGreeting() const;
    void handleLine(QLocalSocket *socket, const QByteArray &line);

    QString m_path;
    bool m_listening = false;

    mutable QMutex m_mutex;
    QString m_status = QStringLiteral("running");
    QString m_failCommand;
    QString m_silentCommand;
    QString m_interleavedEvent;
    bool m_noGreeting = false;
    bool m_quitSendsReturn = false;
    QHash<QString, int> m_commandCounts;
    QVector<QPair<QString, QJsonObject>> m_commands;
    int m_connections = 0;

    std::atomic_bool m_stop{false};
    QThread *m_thread = nullptr;
};
