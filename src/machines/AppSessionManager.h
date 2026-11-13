// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "core/Error.h"
#include "machines/GuestAppCatalog.h"
#include "machines/MachineMeta.h"

#include <QDateTime>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

#include <atomic>
#include <functional>
#include <mutex>
#include <thread>

namespace PodStack {

/// Sessão de app mantida pelo daemon (não persiste entre reinícios).
struct AppSession {
    QString sessionId;
    QString machineId;
    QString appId;
    QString backend; // x11|xpra|rail
    QString state;   // starting|running|stopping|failed
    int sshPort = 0;
    qint64 guestPid = -1;
    QString xpraDisplay;
    QDateTime createdAt;
    QDateTime lastSeenAt;

    QJsonObject toJson() const;
};

/// Spec entregue à GUI para lançar o cliente local do app.
struct LaunchSpec {
    QString backend;
    QString program;
    QStringList args;
    QHash<QString, QString> env;
    QString knownHostsPath;
    int sshPort = 0;
    QString display;
    QString xpraVersion;
    QString rdpFile;
    QString rdpSecretPath;
    QString certFingerprint;

    QJsonObject toJson() const;
};

/// Dono das sessões de apps: idempotência por (machineId, appId), TTL e eventos.
class AppSessionManager {
public:
    using EventCallback = std::function<void(const QString &method, const QJsonObject &payload)>;

    explicit AppSessionManager(int ttlMs = 60 * 1000);
    ~AppSessionManager();

    AppSessionManager(const AppSessionManager &) = delete;
    AppSessionManager &operator=(const AppSessionManager &) = delete;

    void setEventCallback(EventCallback callback);

    AppSession launch(const MachineMeta &machine, const GuestApp &app, const QString &backend,
                      bool consent, LaunchSpec *spec, Error *error = nullptr);
    bool stop(const QString &machineId, const QString &sessionId, bool force, Error *error);
    bool detach(const QString &machineId, const QString &sessionId, Error *error);
    QList<AppSession> sessions(const QString &machineId);
    QJsonArray sessionsJson(const QString &machineId);
    void reconcileMachine(const QString &machineId);

    void trust(const QString &machineId, const QString &fingerprint);
    QString trustedFingerprint(const QString &machineId) const;

private:
    void emitEvent(const QString &method, const QJsonObject &payload);
    void ttlLoop();
    static QString resolveBackend(const MachineMeta &machine, const QString &requested);
    static bool buildLaunchSpec(const MachineMeta &machine, const GuestApp &app,
                                const QString &backend, LaunchSpec *spec, Error *error);

    mutable std::mutex m_mutex;
    QHash<QString, AppSession> m_sessions; // sessionId -> sessão
    QHash<QString, QString> m_trustedFingerprints;
    EventCallback m_callback;
    int m_ttlMs = 60 * 1000;
    std::atomic_bool m_stop{false};
    std::thread m_ttlThread;
};

} // namespace PodStack
