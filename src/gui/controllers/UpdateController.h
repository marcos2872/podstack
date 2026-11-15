// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "gui/controllers/ControllerBase.h"

#include <QFutureWatcher>
#include <QJsonObject>
#include <QVariantMap>

namespace PodStack {

class UpdateController : public ControllerBase {
    Q_OBJECT
    Q_PROPERTY(QVariantMap status READ status NOTIFY statusChanged)

public:
    explicit UpdateController(QObject *parent = nullptr);

    QVariantMap status() const { return m_status; }
    void setEnabled(bool enabled) { m_enabled = enabled; }

    Q_INVOKABLE void check(bool interactive = false);
    Q_INVOKABLE void install();
    Q_INVOKABLE void resume();
    Q_INVOKABLE void clearState();
    Q_INVOKABLE void loadState();

signals:
    void statusChanged();
    void available(const QVariantMap &release);
    void checkFailed(const QString &reason);
    /// Mensagem para o toast do app (kind: info|error).
    void userMessage(const QString &kind, const QString &text);

private:
    void startRunner(bool resume);
    void onFinished();

    QVariantMap m_status;
    QFutureWatcher<QJsonObject> m_watcher;
    bool m_running = false;
    bool m_interactive = false;
    bool m_enabled = true;
};

} // namespace PodStack
