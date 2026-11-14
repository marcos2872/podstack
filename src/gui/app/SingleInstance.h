// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QObject>
#include <QString>

class QLocalServer;

namespace PodStack {

/// Garante uma única instância da GUI e encaminha pedidos de "abrir" para a
/// instância primária. O socket fica em $XDG_RUNTIME_DIR/podstack/gui.sock,
/// com diretório 0700, socket 0600 e checagem de UID via SO_PEERCRED.
class SingleInstance : public QObject {
    Q_OBJECT

public:
    explicit SingleInstance(QObject *parent = nullptr);
    ~SingleInstance() override;

    /// Caminho do socket de controle da GUI.
    static QString socketPath();

    /// Tenta se tornar a instância primária. Se já houver outra, envia
    /// `command` ("show" por padrão; vazio não envia nada) e retorna false.
    /// Em falha ao escutar, preenche `error` e retorna false.
    bool start(const QString &command = QStringLiteral("show"), QString *error = nullptr);

    bool isPrimary() const { return m_primary; }
    void stop();

signals:
    void showRequested();
    void quitRequested();

private:
    void handleNewConnection();

    QLocalServer *m_server = nullptr;
    bool m_primary = false;
};

} // namespace PodStack
