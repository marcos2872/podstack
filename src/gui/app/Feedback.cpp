// SPDX-License-Identifier: GPL-3.0-or-later
#include "gui/app/Feedback.h"

namespace PodStack {

namespace {
constexpr int kMaxQueue = 8;
} // namespace

Feedback::Feedback(QObject *parent) : QObject(parent)
{
    m_timer.setSingleShot(true);
    connect(&m_timer, &QTimer::timeout, this, [this] { finish(); });
}

void Feedback::show(const QString &message, const QString &severity, int durationMs,
                    const QString &actionText)
{
    if (message.trimmed().isEmpty()) {
        return;
    }
    // Rajada de eventos repete o mesmo texto: não empilha.
    if (message == m_message || (!m_queue.isEmpty() && message == m_queue.last().message)) {
        return;
    }

    Notice notice;
    notice.message = message;
    notice.severity = severity;
    notice.actionText = actionText;
    notice.durationMs = qBound(1000, durationMs, 15000);

    if (m_message.isEmpty()) {
        present(notice);
        return;
    }
    if (m_queue.size() >= kMaxQueue) {
        m_queue.dequeue();
    }
    m_queue.enqueue(notice);
}

void Feedback::dismiss()
{
    m_timer.stop();
    finish();
}

void Feedback::triggerAction()
{
    const QString message = m_message;
    if (message.isEmpty() || m_actionText.isEmpty()) {
        return;
    }
    emit actionTriggered(message);
    dismiss();
}

void Feedback::present(const Notice &notice)
{
    m_message = notice.message;
    m_severity = notice.severity;
    m_actionText = notice.actionText;
    m_timer.start(notice.durationMs);
    emit currentChanged();
}

void Feedback::finish()
{
    m_message.clear();
    m_severity.clear();
    m_actionText.clear();
    emit currentChanged();
    if (m_queue.isEmpty()) {
        return;
    }
    present(m_queue.dequeue());
}

} // namespace PodStack
