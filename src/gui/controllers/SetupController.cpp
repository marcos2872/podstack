// SPDX-License-Identifier: GPL-3.0-or-later
#include "gui/controllers/SetupController.h"

#include "core/error/Error.h"
#include "core/rpc/RpcClient.h"
#include "core/setup/SetupActions.h"
#include "core/setup/SetupPolicy.h"

#include <QJsonArray>
#include <QtConcurrent>

namespace PodStack {

SetupController::SetupController(QObject *parent) : ControllerBase(parent)
{
    connect(&m_watcher, &QFutureWatcher<QJsonObject>::finished, this, &SetupController::onFinished);
    connect(&m_stream, &StreamClient::setupEvent, this,
            [this](const QJsonObject &event) { applyJob(event); });
    connect(&m_stream, &StreamClient::setupFinished, this, [this](const QJsonObject &event) {
        applyJob(event);
        m_jobActive = false;
        m_status = event.value(QStringLiteral("state")).toString();
        emit jobChanged();
        refreshStatus();
    });
    connect(&m_stream, &StreamClient::error, this, [this](const QString &message) {
        m_jobActive = false;
        setError(message);
        m_busy = false;
        emit jobChanged();
    });
}

QString SetupController::podmanAction() const
{
    for (const QVariant &value : m_checks) {
        const QVariantMap check = value.toMap();
        if (check.value(QStringLiteral("id")).toString() == QStringLiteral("podman")) {
            return SetupActions::podmanActionForStatus(
                check.value(QStringLiteral("status")).toString());
        }
    }
    return {};
}

bool SetupController::mandatoryOk() const
{
    if (m_checks.isEmpty()) {
        return false;
    }
    for (const QVariant &value : m_checks) {
        const QVariantMap check = value.toMap();
        if (!SetupPolicy::isCheckSatisfied(check.value(QStringLiteral("id")).toString(),
                                           check.value(QStringLiteral("status")).toString())) {
            return false;
        }
    }
    return true;
}

void SetupController::setError(const QString &error)
{
    m_error = error;
    if (error.isEmpty()) {
        clearLastError();
    } else {
        setLastError(error);
    }
    emit changed();
}

void SetupController::runRequest(const QString &method, const QJsonObject &params)
{
    if (m_watcher.isRunning()) {
        return;
    }
    m_pendingMethod = method;
    m_busy = true;
    m_error.clear();
    emit changed();
    m_watcher.setFuture(QtConcurrent::run([method, params] {
        RpcClient client;
        Error error;
        QJsonObject object = client.call(method, params, &error);
        if (!error.isEmpty()) {
            object.insert(QStringLiteral("__error"), error.message);
        }
        return object;
    }));
}

void SetupController::onFinished()
{
    const QJsonObject result = m_watcher.result();
    m_busy = false;

    if (result.contains(QStringLiteral("__error"))) {
        m_error = result.value(QStringLiteral("__error")).toString();
        if (m_pendingMethod == QStringLiteral("setup.status")) {
            m_loaded = true;
        }
        setLastError(m_error);
        emit changed();
        return;
    }

    m_error.clear();
    if (m_pendingMethod == QStringLiteral("setup.status")) {
        applyStatus(result);
    } else if (m_pendingMethod == QStringLiteral("setup.check")) {
        m_checks = result.value(QStringLiteral("checks")).toArray().toVariantList();
        emit changed();
    } else if (m_pendingMethod == QStringLiteral("setup.validate")) {
        m_checks = result.value(QStringLiteral("checks")).toArray().toVariantList();
        bool ready = true;
        for (const QVariant &checkValue : m_checks) {
            const QVariantMap check = checkValue.toMap();
            if (!SetupPolicy::isCheckSatisfied(check.value(QStringLiteral("id")).toString(),
                                               check.value(QStringLiteral("status")).toString())) {
                ready = false;
                break;
            }
        }
        m_summary.insert(QStringLiteral("ready"), ready);
        m_status = ready ? QStringLiteral("Tudo pronto.")
                         : QStringLiteral("Alguns itens precisam de atenção.");
        emit changed();
    } else if (m_pendingMethod == QStringLiteral("setup.dockerImpact")) {
        m_dockerImpact = result.toVariantMap();
        emit changed();
    } else if (m_pendingMethod == QStringLiteral("setup.complete")) {
        m_completed = result.value(QStringLiteral("completed")).toBool();
        if (result.value(QStringLiteral("skipped")).toBool()) {
            m_showOnboarding = false;
        }
        emit changed();
    }
    m_pendingMethod.clear();
}

void SetupController::applyStatus(const QJsonObject &status)
{
    m_loaded = true;
    m_completed = status.value(QStringLiteral("completed")).toBool();
    m_showOnboarding = status.value(QStringLiteral("showOnboarding")).toBool(true);
    m_dockerPolicy = status.value(QStringLiteral("dockerPolicy")).toString();
    m_checks = status.value(QStringLiteral("checks")).toArray().toVariantList();
    m_summary = status.value(QStringLiteral("summary")).toObject().toVariantMap();
    const QJsonObject active = status.value(QStringLiteral("activeJob")).toObject();
    applyJob(active);
    if (!active.isEmpty() && !m_jobActive) {
        const QString jobId = active.value(QStringLiteral("jobId")).toString();
        if (!jobId.isEmpty()) {
            m_stream.subscribeSetup(jobId);
        }
    }
    emit changed();
}

void SetupController::applyJob(const QJsonObject &job)
{
    if (job.isEmpty()) {
        return;
    }
    const QString state = job.value(QStringLiteral("state")).toString();
    m_jobId = job.value(QStringLiteral("jobId")).toString();
    m_progress = job.value(QStringLiteral("progress")).toInt();
    m_jobActive = state == QStringLiteral("running") || state == QStringLiteral("queued") ||
                  state == QStringLiteral("cancelling");
    if (!state.isEmpty()) {
        m_status = state;
    }
    const QString error = job.value(QStringLiteral("error")).toString();
    const bool errorChanged = !error.isEmpty() && error != m_error;
    if (errorChanged) {
        m_error = error;
    }
    m_log.clear();
    for (const QJsonValue &line : job.value(QStringLiteral("lines")).toArray()) {
        m_log.append(line.toString());
    }
    emit jobChanged();
    if (errorChanged) {
        emit changed();
    }
}

void SetupController::refreshStatus()
{
    runRequest(QStringLiteral("setup.status"), {});
}

void SetupController::check()
{
    runRequest(QStringLiteral("setup.check"), {});
}

void SetupController::validate()
{
    runRequest(QStringLiteral("setup.validate"), {});
}

void SetupController::apply(const QString &action, const QVariantList &components)
{
    if (m_jobActive) {
        return;
    }
    QJsonObject params;
    params.insert(QStringLiteral("action"), action);
    if (!components.isEmpty()) {
        params.insert(QStringLiteral("components"), QJsonArray::fromVariantList(components));
    }
    m_error.clear();
    m_log.clear();
    m_progress = 0;
    m_jobActive = true;
    m_status = QStringLiteral("Iniciando…");
    emit jobChanged();
    m_stream.startSetup(params);
}

void SetupController::loadDockerImpact()
{
    if (m_watcher.isRunning()) {
        return;
    }
    m_dockerImpact.clear();
    emit changed();
    runRequest(QStringLiteral("setup.dockerImpact"), {});
}

void SetupController::removeDocker()
{
    if (m_jobActive) {
        return;
    }
    m_error.clear();
    m_log.clear();
    m_progress = 0;
    m_jobActive = true;
    m_status = QStringLiteral("Removendo o Docker…");
    emit jobChanged();
    m_stream.startSetupRemoveDocker(QJsonObject{{QStringLiteral("confirm"), true}});
}

void SetupController::cancel()
{
    if (m_jobId.isEmpty()) {
        return;
    }
    runRequest(QStringLiteral("setup.cancel"), QJsonObject{{QStringLiteral("jobId"), m_jobId}});
}

void SetupController::complete()
{
    if (!mandatoryOk()) {
        m_error = QStringLiteral("Conclua os itens obrigatórios antes de abrir o PodStack.");
        emit changed();
        return;
    }
    m_error.clear();
    emit changed();
    runRequest(QStringLiteral("setup.complete"), QJsonObject{{QStringLiteral("skipped"), false}});
}

void SetupController::skip()
{
    runRequest(QStringLiteral("setup.complete"), QJsonObject{{QStringLiteral("skipped"), true}});
}

} // namespace PodStack
