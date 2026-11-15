// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/rpc/StreamClient.h"
#include "gui/controllers/ControllerBase.h"

#include <QFutureWatcher>
#include <QJsonObject>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

namespace PodStack {

class SetupController : public ControllerBase {
    Q_OBJECT
    Q_PROPERTY(bool completed READ completed NOTIFY changed)
    Q_PROPERTY(bool pending READ pending NOTIFY changed)
    Q_PROPERTY(QString podmanAction READ podmanAction NOTIFY changed)
    Q_PROPERTY(bool mandatoryOk READ mandatoryOk NOTIFY changed)
    Q_PROPERTY(QVariantList checks READ checks NOTIFY changed)
    Q_PROPERTY(QVariantMap summary READ summary NOTIFY changed)
    Q_PROPERTY(QVariantMap dockerImpact READ dockerImpact NOTIFY changed)
    Q_PROPERTY(QString dockerPolicy READ dockerPolicy NOTIFY changed)
    Q_PROPERTY(QString jobId READ jobId NOTIFY jobChanged)
    Q_PROPERTY(bool jobActive READ jobActive NOTIFY jobChanged)
    Q_PROPERTY(int progress READ progress NOTIFY jobChanged)
    Q_PROPERTY(QStringList log READ log NOTIFY jobChanged)
    Q_PROPERTY(QString status READ status NOTIFY jobChanged)
    Q_PROPERTY(QString error READ error NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(bool loaded READ loaded NOTIFY changed)

public:
    explicit SetupController(QObject *parent = nullptr);

    bool completed() const { return m_completed; }
    bool pending() const { return !m_completed && m_showOnboarding; }
    QString podmanAction() const;
    bool mandatoryOk() const;
    QVariantList checks() const { return m_checks; }
    QVariantMap summary() const { return m_summary; }
    QVariantMap dockerImpact() const { return m_dockerImpact; }
    QString dockerPolicy() const { return m_dockerPolicy; }
    QString jobId() const { return m_jobId; }
    bool jobActive() const { return m_jobActive; }
    int progress() const { return m_progress; }
    QStringList log() const { return m_log; }
    QString status() const { return m_status; }
    QString error() const { return m_error; }
    bool busy() const { return m_busy; }
    bool loaded() const { return m_loaded; }

    Q_INVOKABLE void refreshStatus();
    Q_INVOKABLE void check();
    Q_INVOKABLE void validate();
    Q_INVOKABLE void apply(const QString &action, const QVariantList &components = {});
    Q_INVOKABLE void loadDockerImpact();
    Q_INVOKABLE void removeDocker();
    Q_INVOKABLE void cancel();
    Q_INVOKABLE void complete();
    Q_INVOKABLE void skip();

signals:
    void changed();
    void jobChanged();

private:
    void runRequest(const QString &method, const QJsonObject &params);
    void onFinished();
    void applyStatus(const QJsonObject &status);
    void applyJob(const QJsonObject &job);
    void setError(const QString &error);

    bool m_completed = false;
    bool m_showOnboarding = true;
    QString m_dockerPolicy = QStringLiteral("keep");
    QVariantList m_checks;
    QVariantMap m_summary;
    QVariantMap m_dockerImpact;
    QString m_jobId;
    bool m_jobActive = false;
    int m_progress = 0;
    QStringList m_log;
    QString m_status;
    QString m_error;
    bool m_busy = false;
    bool m_loaded = false;
    QString m_pendingMethod;
    QFutureWatcher<QJsonObject> m_watcher;
    StreamClient m_stream;
};

} // namespace PodStack
