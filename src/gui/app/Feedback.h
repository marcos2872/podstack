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
    Q_PROPERTY(QString actionText READ actionText NOTIFY currentChanged)
    Q_PROPERTY(bool active READ active NOTIFY currentChanged)

public:
    explicit Feedback(QObject *parent = nullptr);

    QString message() const { return m_message; }
    QString severity() const { return m_severity; }
    QString actionText() const { return m_actionText; }
    bool active() const { return !m_message.isEmpty(); }

    /// Enfileira um aviso. `durationMs` é limitado a 1–15 s.
    Q_INVOKABLE void show(const QString &message, const QString &severity = QStringLiteral("info"),
                          int durationMs = 4000, const QString &actionText = QString());
    Q_INVOKABLE void dismiss();
    /// Aciona o botão de ação do aviso atual (o toast chama isto).
    Q_INVOKABLE void triggerAction();

signals:
    void currentChanged();
    void actionTriggered(const QString &message);

private:
    struct Notice {
        QString message;
        QString severity;
        QString actionText;
        int durationMs = 4000;
    };

    void present(const Notice &notice);
    void finish();

    QQueue<Notice> m_queue;
    QTimer m_timer;
    QString m_message;
    QString m_severity;
    QString m_actionText;
};

} // namespace PodStack
