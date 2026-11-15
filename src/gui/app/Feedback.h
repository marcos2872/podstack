// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QObject>
#include <QQueue>
#include <QString>
#include <QTimer>

namespace PodStack {

/// Fila de avisos transitórios (toasts) da GUI.
///
/// Um aviso por vez, com fila curta e deduplicação: rajadas de eventos do
/// engine não empilham o mesmo texto. Erros de ação continuam nos banners das
/// páginas; aqui vão os eventos e as confirmações de sucesso.
class Feedback : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString message READ message NOTIFY currentChanged)
    Q_PROPERTY(QString severity READ severity NOTIFY currentChanged) // info|success|warning|error
    Q_PROPERTY(bool active READ active NOTIFY currentChanged)

public:
    explicit Feedback(QObject *parent = nullptr);

    QString message() const { return m_message; }
    QString severity() const { return m_severity; }
    bool active() const { return !m_message.isEmpty(); }

    /// Enfileira um aviso. `durationMs` é limitado a 1–15 s.
    Q_INVOKABLE void show(const QString &message, const QString &severity = QStringLiteral("info"),
                          int durationMs = 4000);
    Q_INVOKABLE void dismiss();

signals:
    void currentChanged();

private:
    struct Notice {
        QString message;
        QString severity;
        int durationMs = 4000;
    };

    void present(const Notice &notice);
    void finish();

    QQueue<Notice> m_queue;
    QTimer m_timer;
    QString m_message;
    QString m_severity;
};

} // namespace PodStack
