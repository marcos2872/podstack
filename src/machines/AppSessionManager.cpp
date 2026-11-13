// SPDX-License-Identifier: GPL-3.0-or-later

#include "machines/AppSessionManager.h"

#include "core/Ids.h"
#include "core/Paths.h"

#include <QFileInfo>
#include <QJsonArray>

#include <chrono>
#include <utility>

namespace PodStack {
namespace {

constexpr int kTtlTickMs = 1000;

Error sessionError(const QString &message, const QString &reason, const QString &detail = {})
{
    Error error = Error::make(QStringLiteral("PODSTACK_SESSION_NOT_FOUND"), message, detail);
    error.reason = reason;
    return error;
}

} // namespace

QJsonObject AppSession::toJson() const
{
    QJsonObject object;
    object.insert(QStringLiteral("sessionId"), sessionId);
    object.insert(QStringLiteral("machineId"), machineId);
    object.insert(QStringLiteral("appId"), appId);
    object.insert(QStringLiteral("backend"), backend);
    object.insert(QStringLiteral("state"), state);
    object.insert(QStringLiteral("createdAt"), createdAt.toString(Qt::ISODateWithMs));
    return object;
}

QJsonObject LaunchSpec::toJson() const
{
    QJsonObject object;
    object.insert(QStringLiteral("backend"), backend);
    object.insert(QStringLiteral("program"), program);
    QJsonArray argumentArray;
    for (const QString &argument : args) {
        argumentArray.append(argument);
    }
    object.insert(QStringLiteral("args"), argumentArray);
    QJsonObject envObject;
    for (auto it = env.constBegin(); it != env.constEnd(); ++it) {
        envObject.insert(it.key(), it.value());
    }
    object.insert(QStringLiteral("env"), envObject);
    if (!knownHostsPath.isEmpty()) {
        object.insert(QStringLiteral("knownHostsPath"), knownHostsPath);
    }
    if (sshPort > 0) {
        object.insert(QStringLiteral("sshPort"), sshPort);
    }
    if (!display.isEmpty()) {
        object.insert(QStringLiteral("display"), display);
    }
    if (!xpraVersion.isEmpty()) {
        object.insert(QStringLiteral("xpraVersion"), xpraVersion);
    }
    if (!rdpFile.isEmpty()) {
        object.insert(QStringLiteral("rdpFile"), rdpFile);
    }
    if (!rdpSecretPath.isEmpty()) {
        object.insert(QStringLiteral("rdpSecretPath"), rdpSecretPath);
    }
    if (!certFingerprint.isEmpty()) {
        object.insert(QStringLiteral("certFingerprint"), certFingerprint);
    }
    return object;
}

AppSessionManager::AppSessionManager(int ttlMs)
    : m_ttlMs(qMax(1000, ttlMs))
{
    m_ttlThread = std::thread([this] { ttlLoop(); });
}

AppSessionManager::~AppSessionManager()
{
    m_stop.store(true);
    if (m_ttlThread.joinable()) {
        m_ttlThread.join();
    }
}

void AppSessionManager::setEventCallback(EventCallback callback)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_callback = std::move(callback);
}

void AppSessionManager::emitEvent(const QString &method, const QJsonObject &payload)
{
    EventCallback callback;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        callback = m_callback;
    }
    if (callback) {
        callback(method, payload);
    }
}

QString AppSessionManager::resolveBackend(const MachineMeta &machine, const QString &requested)
{
    const QString value = requested.trimmed().toLower();
    if (value.isEmpty() || value == QStringLiteral("auto")) {
        return machine.provisioner == QStringLiteral("windows") ? QStringLiteral("rail")
                                                                 : QStringLiteral("x11");
    }
    return value;
}

bool AppSessionManager::buildLaunchSpec(const MachineMeta &machine, const GuestApp &app,
                                        const QString &backend, LaunchSpec *spec, Error *error)
{
    if (!spec) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_INTERNAL"),
                                 QStringLiteral("Destino do LaunchSpec ausente."));
        }
        return false;
    }
    *spec = {};
    if (backend != QStringLiteral("x11")) {
        Error unsupported = Error::make(
            QStringLiteral("PODSTACK_UNSUPPORTED"),
            QStringLiteral("O backend solicitado ainda não está disponível."), backend,
            QStringLiteral("Use x11 nesta fase; xpra/rail chegam na T10-03."));
        unsupported.reason = QStringLiteral("backend_unsupported");
        if (error) {
            *error = unsupported;
        }
        return false;
    }
    if (machine.provisioner == QStringLiteral("windows")) {
        Error unsupported = Error::make(QStringLiteral("PODSTACK_UNSUPPORTED"),
                                        QStringLiteral("x11 não é suportado em guests Windows."),
                                        machine.id);
        unsupported.reason = QStringLiteral("backend_unsupported");
        if (error) {
            *error = unsupported;
        }
        return false;
    }
    if (machine.sshKeyPath.isEmpty() || machine.sshPort <= 0 || app.exec.isEmpty()) {
        Error unreachable = Error::make(
            QStringLiteral("PODSTACK_GUEST_UNREACHABLE"),
            QStringLiteral("A machine não tem SSH/executável para abrir o app."), machine.id);
        unreachable.reason = QStringLiteral("guest_unreachable");
        if (error) {
            *error = unreachable;
        }
        return false;
    }
    const QString directory = Paths::machineDir(machine.id);
    spec->backend = QStringLiteral("x11");
    spec->program = QStringLiteral("ssh");
    spec->knownHostsPath = directory + QStringLiteral("/known_hosts");
    spec->sshPort = machine.sshPort;
    spec->args = {
        QStringLiteral("-X"),
        QStringLiteral("-i"),
        machine.sshKeyPath,
        QStringLiteral("-p"),
        QString::number(machine.sshPort),
        QStringLiteral("-o"),
        QStringLiteral("IdentitiesOnly=yes"),
        QStringLiteral("-o"),
        QStringLiteral("StrictHostKeyChecking=accept-new"),
        QStringLiteral("-o"),
        QStringLiteral("UserKnownHostsFile=%1").arg(spec->knownHostsPath),
        QStringLiteral("%1@127.0.0.1").arg(machine.sshUser),
        app.exec,
    };
    return true;
}

AppSession AppSessionManager::launch(const MachineMeta &machine, const GuestApp &app,
                                     const QString &backend, bool consent, LaunchSpec *spec,
                                     Error *error)
{
    Q_UNUSED(consent);
    if (error) {
        *error = {};
    }
    if (machine.id.isEmpty() || app.appId.isEmpty()) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_INVALID_PARAMS"),
                                 QStringLiteral("Machine/app inválidos para o launch."));
        }
        return {};
    }
    if (machine.state != QStringLiteral("running")) {
        Error unreachable = Error::make(
            QStringLiteral("PODSTACK_GUEST_UNREACHABLE"),
            QStringLiteral("A machine precisa estar running para abrir apps."), machine.state);
        unreachable.reason = QStringLiteral("guest_unreachable");
        if (error) {
            *error = unreachable;
        }
        return {};
    }

    const QString resolved = resolveBackend(machine, backend);
    if (resolved == QStringLiteral("xpra")) {
        Error missing = Error::make(
            QStringLiteral("PODSTACK_DEPENDENCY_MISSING"),
            QStringLiteral("O backend xpra ainda não foi implementado."), resolved,
            QStringLiteral("Use x11 nesta fase; xpra chega na T10-03."));
        missing.reason = QStringLiteral("backend_missing");
        if (error) {
            *error = missing;
        }
        return {};
    }
    if (resolved == QStringLiteral("rail")) {
        Error unsupported = Error::make(
            QStringLiteral("PODSTACK_UNSUPPORTED"),
            QStringLiteral("RAIL ainda não está implementado."), machine.id);
        unsupported.reason = QStringLiteral("backend_unsupported");
        if (error) {
            *error = unsupported;
        }
        return {};
    }

    LaunchSpec localSpec;
    if (!buildLaunchSpec(machine, app, resolved, &localSpec, error)) {
        return {};
    }

    AppSession session;
    QJsonObject payload;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (const AppSession &existing : std::as_const(m_sessions)) {
            if (existing.machineId == machine.id && existing.appId == app.appId) {
                if (spec) {
                    *spec = localSpec;
                }
                return existing;
            }
        }

        session.sessionId = newUuidV7();
        session.machineId = machine.id;
        session.appId = app.appId;
        session.backend = resolved;
        session.state = QStringLiteral("running");
        session.sshPort = machine.sshPort;
        session.createdAt = QDateTime::currentDateTimeUtc();
        session.lastSeenAt = session.createdAt;
        m_sessions.insert(session.sessionId, session);
        if (spec) {
            *spec = localSpec;
        }
        payload = session.toJson();
        payload.insert(QStringLiteral("spec"), localSpec.toJson());
    }
    emitEvent(QStringLiteral("machines.apps.session.started"), payload);
    return session;
}

bool AppSessionManager::stop(const QString &machineId, const QString &sessionId, bool force,
                             Error *error)
{
    Q_UNUSED(force);
    if (error) {
        *error = {};
    }
    QJsonObject closed;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        const auto it = m_sessions.find(sessionId);
        if (it == m_sessions.end() || it->machineId != machineId) {
            if (error) {
                *error = sessionError(QStringLiteral("Sessão de app não encontrada."),
                                      QStringLiteral("session_not_found"), sessionId);
            }
            return false;
        }
        closed = it->toJson();
        m_sessions.erase(it);
    }
    emitEvent(QStringLiteral("machines.apps.session.closed"), closed);
    return true;
}

bool AppSessionManager::detach(const QString &machineId, const QString &sessionId, Error *error)
{
    if (error) {
        *error = {};
    }
    std::lock_guard<std::mutex> lock(m_mutex);
    const auto it = m_sessions.find(sessionId);
    if (it != m_sessions.end() && it->machineId == machineId) {
        it->lastSeenAt = QDateTime::currentDateTimeUtc();
    }
    return true;
}

QList<AppSession> AppSessionManager::sessions(const QString &machineId)
{
    QList<AppSession> result;
    std::lock_guard<std::mutex> lock(m_mutex);
    for (AppSession &session : m_sessions) {
        if (!machineId.isEmpty() && session.machineId != machineId) {
            continue;
        }
        session.lastSeenAt = QDateTime::currentDateTimeUtc();
        result.append(session);
    }
    return result;
}

QJsonArray AppSessionManager::sessionsJson(const QString &machineId)
{
    QJsonArray array;
    for (const AppSession &session : sessions(machineId)) {
        array.append(session.toJson());
    }
    return array;
}

void AppSessionManager::reconcileMachine(const QString &machineId)
{
    QList<QJsonObject> closed;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (auto it = m_sessions.begin(); it != m_sessions.end();) {
            if (it->machineId != machineId) {
                ++it;
                continue;
            }
            closed.append(it->toJson());
            it = m_sessions.erase(it);
        }
    }
    for (const QJsonObject &payload : closed) {
        emitEvent(QStringLiteral("machines.apps.session.closed"), payload);
    }
}

void AppSessionManager::trust(const QString &machineId, const QString &fingerprint)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_trustedFingerprints.insert(machineId, fingerprint);
}

QString AppSessionManager::trustedFingerprint(const QString &machineId) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_trustedFingerprints.value(machineId);
}

void AppSessionManager::ttlLoop()
{
    while (!m_stop.load()) {
        for (int i = 0; i < kTtlTickMs / 100 && !m_stop.load(); ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        if (m_stop.load()) {
            break;
        }
        const QDateTime now = QDateTime::currentDateTimeUtc();
        QList<QJsonObject> closed;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            for (auto it = m_sessions.begin(); it != m_sessions.end();) {
                if (it->lastSeenAt.msecsTo(now) < m_ttlMs) {
                    ++it;
                    continue;
                }
                closed.append(it->toJson());
                it = m_sessions.erase(it);
            }
        }
        for (const QJsonObject &payload : closed) {
            emitEvent(QStringLiteral("machines.apps.session.closed"), payload);
        }
    }
}

} // namespace PodStack
