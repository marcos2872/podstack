// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "core/Error.h"

#include <QObject>
#include <QString>

class QProcess;

namespace PodStack {

/// Caddy filho do daemon para HTTPS com CA interna.
///
/// Termina TLS em `httpsPort` para `*.localhost`/`*.podstack.local` com
/// certificados on-demand da CA local e encaminha o HTTP resultante para o
/// `HttpProxy` em loopback, que resolve o container pelo `Host`.
class CaddyProxy : public QObject {
    Q_OBJECT
public:
    explicit CaddyProxy(QString directory = QString(), QObject *parent = nullptr);
    ~CaddyProxy() override;

    bool start(quint16 httpsPort, quint16 httpPort, Error *error = nullptr);
    void stop();

    bool running() const;
    quint16 httpsPort() const;
    QString directory() const;
    QString configPath() const;
    QString rootCertPath() const;

    /// Conteúdo do Caddyfile; exposto para teste.
    static QString configText(quint16 httpsPort, quint16 httpPort, const QString &storageDir);

private:
    QString m_directory;
    QProcess *m_process = nullptr;
    quint16 m_httpsPort = 0;
};

} // namespace PodStack
