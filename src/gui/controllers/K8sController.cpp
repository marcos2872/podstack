// SPDX-License-Identifier: GPL-3.0-or-later
#include "gui/controllers/K8sController.h"

#include "core/error/Error.h"
#include "core/rpc/RpcClient.h"

#include <QJsonArray>
#include <QtConcurrent>

namespace PodStack {

namespace {

QJsonObject withError(QJsonObject result, const Error &error)
{
    if (!error.isEmpty()) {
        result.insert(QStringLiteral("error"), error.toJson());
    }
    return result;
}

} // namespace

K8sController::K8sController(QObject *parent) : ControllerBase(parent)
{
    connect(&m_jobStream, &StreamClient::k8sJobEvent, this, [this](const QJsonObject &event) {
        m_jobStage = event.value(QStringLiteral("stage")).toString();
        m_jobProgress = event.value(QStringLiteral("progress")).toInt();
        emit jobChanged();
    });
    connect(&m_jobStream, &StreamClient::subscriptionReady, this,
            [this](const QJsonObject &result) {
                const QString jobId = result.value(QStringLiteral("jobId")).toString();
                if (!jobId.isEmpty()) {
                    m_jobId = jobId;
                    emit jobChanged();
                }
            });
    connect(&m_jobStream, &StreamClient::k8sJobFinished, this, [this](const QJsonObject &result) {
        m_jobActive = false;
        m_jobId.clear();
        const QJsonObject errorObject = result.value(QStringLiteral("error")).toObject();
        if (!errorObject.isEmpty()) {
            m_jobError = errorObject.value(QStringLiteral("message")).toString();
        } else {
            const QJsonObject cluster = result.value(QStringLiteral("cluster")).toObject();
            m_state = cluster.value(QStringLiteral("state")).toString();
            m_enabled = !m_state.isEmpty() && m_state != QStringLiteral("disabled");
            m_jobError.clear();
        }
        emit jobChanged();
        emit changed();
        refreshStatus();
        if (m_enabled) {
            refresh();
        }
    });
    connect(&m_jobStream, &StreamClient::error, this, [this](const QString &message) {
        m_jobActive = false;
        m_jobId.clear();
        m_jobError = message;
        emit jobChanged();
    });
    connect(&m_logsStream, &StreamClient::k8sLogLine, this, [this](const QString &line) {
        m_logLines.append(line);
        if (m_logLines.size() > 10000) {
            m_logLines = m_logLines.mid(2000);
        }
        emit logsChanged();
    });
    connect(&m_logsStream, &StreamClient::k8sLogFinished, this,
            [this](bool ok, const QString &error) {
                m_logActive = false;
                if (!ok) {
                    m_logError = error;
                }
                emit logsChanged();
            });
    connect(&m_logsStream, &StreamClient::error, this, [this](const QString &message) {
        m_logActive = false;
        m_logError = message;
        emit logsChanged();
    });

    connect(&m_statusWatcher, &QFutureWatcher<QJsonObject>::finished, this, [this] {
        const QJsonObject result = m_statusWatcher.result();
        if (result.value(QStringLiteral("error")).isObject()) {
            m_error = result.value(QStringLiteral("error"))
                          .toObject()
                          .value(QStringLiteral("message"))
                          .toString();
        } else {
            m_state = result.value(QStringLiteral("state")).toString();
            m_enabled = !m_state.isEmpty() && m_state != QStringLiteral("disabled");
            m_error.clear();
        }
        emit changed();
        if (m_enabled && m_timer.isActive()) {
            refresh();
        }
    });
    connect(&m_podsWatcher, &QFutureWatcher<QJsonObject>::finished, this, [this] {
        const QJsonObject result = m_podsWatcher.result();
        if (result.contains(QStringLiteral("error"))) {
            const QJsonObject errorObject = result.value(QStringLiteral("error")).toObject();
            m_error = errorObject.value(QStringLiteral("message")).toString();
            if (errorObject.value(QStringLiteral("code")).toString() ==
                QStringLiteral("PODSTACK_NOT_FOUND")) {
                m_state = QStringLiteral("disabled");
                m_enabled = false;
                m_timer.stop();
            }
        } else {
            m_pods = result.value(QStringLiteral("items")).toArray().toVariantList();
            if (!m_servicesWatcher.isRunning()) {
                m_error.clear();
            }
        }
        m_loading = m_podsWatcher.isRunning() || m_servicesWatcher.isRunning();
        emit changed();
    });
    connect(&m_servicesWatcher, &QFutureWatcher<QJsonObject>::finished, this, [this] {
        const QJsonObject result = m_servicesWatcher.result();
        if (result.contains(QStringLiteral("error"))) {
            const QJsonObject errorObject = result.value(QStringLiteral("error")).toObject();
            m_error = errorObject.value(QStringLiteral("message")).toString();
            if (errorObject.value(QStringLiteral("code")).toString() ==
                QStringLiteral("PODSTACK_NOT_FOUND")) {
                m_state = QStringLiteral("disabled");
                m_enabled = false;
                m_timer.stop();
            }
        } else {
            m_services = result.value(QStringLiteral("items")).toArray().toVariantList();
            if (!m_podsWatcher.isRunning()) {
                m_error.clear();
            }
        }
        m_loading = m_podsWatcher.isRunning() || m_servicesWatcher.isRunning();
        emit changed();
    });
    connect(&m_forwardWatcher, &QFutureWatcher<QJsonObject>::finished, this, [this] {
        const QJsonObject result = m_forwardWatcher.result();
        if (result.value(QStringLiteral("error")).isObject()) {
            m_portForwardError = result.value(QStringLiteral("error"))
                                     .toObject()
                                     .value(QStringLiteral("message"))
                                     .toString();
            m_portForwardActive = false;
            m_portForwardId.clear();
            m_portForwardSummary.clear();
        } else {
            m_portForwardId = result.value(QStringLiteral("id")).toString();
            m_portForwardActive = !m_portForwardId.isEmpty();
            m_portForwardError.clear();
            m_portForwardSummary = m_portForwardActive ? QStringLiteral("127.0.0.1:%1 → %2")
                                                             .arg(m_forwardLocalRequested)
                                                             .arg(m_forwardRemoteRequested)
                                                       : QString();
        }
        emit forwardChanged();
    });

    m_timer.setInterval(2000);
    connect(&m_timer, &QTimer::timeout, this, &K8sController::refresh);
}

void K8sController::cancelJob()
{
    if (!m_jobActive) {
        return;
    }
    if (m_jobId.isEmpty()) {
        m_jobError = QStringLiteral("Aguarde o job começar para cancelar.");
        emit jobChanged();
        return;
    }
    const QString jobId = m_jobId;
    callRpc(QStringLiteral("k8s.cancel"), QJsonObject{{QStringLiteral("jobId"), jobId}},
            [this](const QJsonObject &result) {
                if (isErrorResult(result)) {
                    m_jobError = errorMessage(result);
                } else {
                    m_jobStage = QStringLiteral("cancelling");
                    m_jobError.clear();
                }
                emit jobChanged();
            });
}

void K8sController::refreshStatus()
{
    if (m_statusWatcher.isRunning()) {
        return;
    }
    m_statusWatcher.setFuture(QtConcurrent::run([] {
        RpcClient client;
        Error error;
        const QJsonObject result = client.call(QStringLiteral("k8s.status"), QJsonObject{}, &error);
        return withError(result, error);
    }));
}

void K8sController::refresh()
{
    if (!m_enabled) {
        refreshStatus();
        return;
    }
    if (m_podsWatcher.isRunning() || m_servicesWatcher.isRunning()) {
        return;
    }
    m_loading = true;
    emit changed();

    m_podsWatcher.setFuture(QtConcurrent::run([] {
        RpcClient client;
        Error error;
        const QJsonObject result =
            client.call(QStringLiteral("k8s.pods.list"), QJsonObject{}, &error);
        return withError(result, error);
    }));
    m_servicesWatcher.setFuture(QtConcurrent::run([] {
        RpcClient client;
        Error error;
        const QJsonObject result =
            client.call(QStringLiteral("k8s.services.list"), QJsonObject{}, &error);
        return withError(result, error);
    }));
}

void K8sController::startWatch()
{
    if (!m_enabled) {
        refreshStatus();
        return;
    }
    if (!m_timer.isActive()) {
        m_timer.start();
    }
    refresh();
}

void K8sController::stopWatch()
{
    m_timer.stop();
}

void K8sController::startLogs(const QString &namespaceName, const QString &pod,
                              const QString &container)
{
    m_logLines.clear();
    m_logError.clear();
    m_logActive = true;
    emit logsChanged();
    m_logsStream.subscribeK8sLogs(namespaceName, pod, container);
}

void K8sController::stopLogs()
{
    m_logsStream.unsubscribe();
    m_logActive = false;
    emit logsChanged();
}

void K8sController::clearLogs()
{
    m_logLines.clear();
    emit logsChanged();
}

void K8sController::deletePod(const QString &namespaceName, const QString &name)
{
    runAction(
        QStringLiteral("k8s.pod.delete"),
        QJsonObject{{QStringLiteral("namespace"), namespaceName}, {QStringLiteral("name"), name}});
}

void K8sController::deleteService(const QString &namespaceName, const QString &name)
{
    runAction(
        QStringLiteral("k8s.service.delete"),
        QJsonObject{{QStringLiteral("namespace"), namespaceName}, {QStringLiteral("name"), name}});
}

void K8sController::startPortForward(const QString &namespaceName, const QString &name,
                                     int localPort, int remotePort, const QString &resource)
{
    if (m_forwardWatcher.isRunning()) {
        m_portForwardError =
            QStringLiteral("Aguarde a operação de port-forward anterior terminar.");
        emit forwardChanged();
        return;
    }
    m_portForwardError.clear();
    m_forwardLocalRequested = localPort;
    m_forwardRemoteRequested = remotePort;
    m_forwardWatcher.setFuture(
        QtConcurrent::run([namespaceName, name, localPort, remotePort, resource] {
            RpcClient client;
            Error error;
            const QJsonObject result =
                client.call(QStringLiteral("k8s.portforward.start"),
                            QJsonObject{{QStringLiteral("resource"), resource},
                                        {QStringLiteral("namespace"), namespaceName},
                                        {QStringLiteral("pod"), name},
                                        {QStringLiteral("localPort"), localPort},
                                        {QStringLiteral("remotePort"), remotePort}},
                            &error);
            return withError(result, error);
        }));
}

void K8sController::stopPortForward()
{
    const QString id = m_portForwardId;
    m_portForwardActive = false;
    m_portForwardId.clear();
    m_portForwardSummary.clear();
    emit forwardChanged();
    if (id.isEmpty()) {
        return;
    }
    (void) QtConcurrent::run([this, id] {
        RpcClient client;
        Error error;
        client.call(QStringLiteral("k8s.portforward.stop"), QJsonObject{{QStringLiteral("id"), id}},
                    &error);
        if (error.isEmpty()) {
            return;
        }
        // Um forward órfão no daemon ficava sem aviso; reporta na thread da UI.
        const QString message = error.message;
        QMetaObject::invokeMethod(
            this,
            [this, message] {
                m_portForwardError = message;
                emit forwardChanged();
            },
            Qt::QueuedConnection);
    });
}

void K8sController::enable()
{
    m_jobActive = true;
    m_jobStage = QStringLiteral("starting");
    m_jobProgress = 0;
    m_jobError.clear();
    m_jobId.clear();
    emit jobChanged();
    m_jobStream.startK8sJob(QStringLiteral("k8s.enable"), QJsonObject{});
}

void K8sController::disable(bool force)
{
    m_jobActive = true;
    m_jobStage = QStringLiteral("disabling");
    m_jobProgress = 0;
    m_jobError.clear();
    m_jobId.clear();
    emit jobChanged();
    m_jobStream.startK8sJob(QStringLiteral("k8s.disable"),
                            QJsonObject{{QStringLiteral("force"), force}});
}

void K8sController::reset()
{
    m_jobActive = true;
    m_jobStage = QStringLiteral("resetting");
    m_jobProgress = 0;
    m_jobError.clear();
    m_jobId.clear();
    emit jobChanged();
    m_jobStream.startK8sJob(QStringLiteral("k8s.reset"), QJsonObject{});
}

void K8sController::onActionFinished(const QString &method, const QJsonObject &params,
                                     const QJsonObject &result)
{
    ControllerBase::onActionFinished(method, params, result);
    if (method.startsWith(QStringLiteral("k8s."))) {
        refresh();
    }
}

} // namespace PodStack
