// SPDX-License-Identifier: GPL-3.0-or-later
#include "daemon/rpc/handlers/Common.h"

using namespace PodStack::DaemonInternal;

namespace PodStack {

QJsonObject Daemon::k8sCancel(const QJsonObject &params, Error *error)
{
    const QString jobId = params.value(QStringLiteral("jobId")).toString().trimmed();
    if (jobId.isEmpty()) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_INVALID_PARAMS"),
                                 QStringLiteral("Informe o job do cluster."));
        }
        return {};
    }

    QString subscriptionId;
    {
        std::lock_guard<std::mutex> lock(m_k8sJobsMutex);
        subscriptionId = m_k8sSubscriptions.value(jobId);
    }
    if (subscriptionId.isEmpty()) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_NOT_FOUND"),
                                 QStringLiteral("Job de cluster não está em execução."), jobId);
        }
        return {};
    }

    std::shared_ptr<Subscription> subscription;
    {
        std::lock_guard<std::mutex> lock(m_subscriptionsMutex);
        subscription = m_subscriptions.value(subscriptionId);
    }
    if (!subscription) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_NOT_FOUND"),
                                 QStringLiteral("Job de cluster não está em execução."), jobId);
        }
        return {};
    }

    subscription->cancel.store(true);
    QJsonObject result;
    result.insert(QStringLiteral("jobId"), jobId);
    result.insert(QStringLiteral("cancelled"), true);
    return result;
}

QJsonObject Daemon::k8sContextUse(const QJsonObject &params, Error *error)
{
    const QString context = params.value(QStringLiteral("context")).toString().trimmed();
    if (!m_k8s.useContext(context, error)) {
        return {};
    }
    return QJsonObject{{QStringLiteral("context"), context}};
}

QJsonObject Daemon::k8sDisable(const QJsonObject &params, const Notify &notify, Error *error)
{
    const bool force = params.value(QStringLiteral("force")).toBool(false);
    QString jobId;
    {
        std::lock_guard<std::mutex> lock(m_k8sJobsMutex);
        jobId =
            QStringLiteral("k8s-%1-%2").arg(QDateTime::currentSecsSinceEpoch()).arg(m_nextK8sJob++);
    }
    auto subscription = std::make_shared<Subscription>();
    const QString subscriptionId = createSubscription(subscription);
    {
        std::lock_guard<std::mutex> lock(m_k8sJobsMutex);
        m_k8sSubscriptions.insert(jobId, subscriptionId);
    }

    (void) QtConcurrent::run([this, jobId, subscriptionId, subscription, force, notify] {
        const auto progress = [jobId, notify](const QString &stage, int value) {
            if (!notify) {
                return;
            }
            QJsonObject payload;
            payload.insert(QStringLiteral("jobId"), jobId);
            payload.insert(QStringLiteral("stage"), stage);
            payload.insert(QStringLiteral("progress"), value);
            notify(QStringLiteral("k8s.delta"), payload);
        };
        const auto cancelled = [subscription] { return subscription->cancel.load(); };
        Error disableError;
        const K8sCluster cluster = m_k8s.disable(force, progress, cancelled, &disableError);

        QJsonObject payload;
        payload.insert(QStringLiteral("jobId"), jobId);
        if (disableError.isEmpty()) {
            payload.insert(QStringLiteral("cluster"), cluster.toJson());
        } else {
            payload.insert(QStringLiteral("error"), disableError.toJson());
        }
        if (notify) {
            notify(QStringLiteral("k8s.done"), payload);
        }
        {
            std::lock_guard<std::mutex> lock(m_k8sJobsMutex);
            m_k8sSubscriptions.remove(jobId);
        }
        removeSubscription(subscriptionId);
    });

    return QJsonObject{{QStringLiteral("jobId"), jobId},
                       {QStringLiteral("subscriptionId"), subscriptionId}};
}

QJsonObject Daemon::k8sEnable(const QJsonObject &params, const Notify &notify, Error *error)
{
    K8sManager::EnableRequest request;

    const auto readInt = [&](const QString &key, std::optional<int> *target) {
        const QJsonValue value = params.value(key);
        if (value.isUndefined() || value.isNull()) {
            return true;
        }
        if (!value.isDouble()) {
            if (error) {
                *error = Error::make(QStringLiteral("PODSTACK_INVALID_PARAMS"),
                                     QStringLiteral("%1 deve ser um número inteiro.").arg(key));
            }
            return false;
        }
        const double raw = value.toDouble();
        if (std::trunc(raw) != raw || raw < 1 || raw > 256) {
            if (error) {
                *error = Error::make(QStringLiteral("PODSTACK_INVALID_PARAMS"),
                                     QStringLiteral("%1 fora do intervalo 1..256.").arg(key),
                                     QString::number(raw));
            }
            return false;
        }
        *target = static_cast<int>(raw);
        return true;
    };

    const auto readBytes = [&](const QString &key, std::optional<qint64> *target) {
        const QJsonValue value = params.value(key);
        if (value.isUndefined() || value.isNull()) {
            return true;
        }
        if (!value.isDouble()) {
            if (error) {
                *error = Error::make(QStringLiteral("PODSTACK_INVALID_PARAMS"),
                                     QStringLiteral("%1 deve ser um número inteiro.").arg(key));
            }
            return false;
        }
        const double raw = value.toDouble();
        if (std::trunc(raw) != raw || raw < 1) {
            if (error) {
                *error = Error::make(QStringLiteral("PODSTACK_INVALID_PARAMS"),
                                     QStringLiteral("%1 inválido.").arg(key), QString::number(raw));
            }
            return false;
        }
        *target = static_cast<qint64>(raw);
        return true;
    };

    if (!readInt(QStringLiteral("cpus"), &request.cpus) ||
        !readBytes(QStringLiteral("memoryBytes"), &request.memoryBytes) ||
        !readBytes(QStringLiteral("diskBytes"), &request.diskBytes)) {
        return {};
    }

    QString jobId;
    {
        std::lock_guard<std::mutex> lock(m_k8sJobsMutex);
        jobId =
            QStringLiteral("k8s-%1-%2").arg(QDateTime::currentSecsSinceEpoch()).arg(m_nextK8sJob++);
    }

    auto subscription = std::make_shared<Subscription>();
    const QString subscriptionId = createSubscription(subscription);
    {
        std::lock_guard<std::mutex> lock(m_k8sJobsMutex);
        m_k8sSubscriptions.insert(jobId, subscriptionId);
    }

    (void) QtConcurrent::run([this, jobId, subscriptionId, subscription, request, notify] {
        const auto progress = [jobId, notify](const QString &stage, int value) {
            if (!notify) {
                return;
            }
            QJsonObject payload;
            payload.insert(QStringLiteral("jobId"), jobId);
            payload.insert(QStringLiteral("stage"), stage);
            payload.insert(QStringLiteral("progress"), value);
            notify(QStringLiteral("k8s.delta"), payload);
        };
        const auto cancelled = [subscription] { return subscription->cancel.load(); };

        Error enableError;
        const K8sCluster cluster = m_k8s.enable(request, progress, cancelled, &enableError);
        if (enableError.isEmpty() && cluster.id.isEmpty()) {
            enableError = Error::make(QStringLiteral("PODSTACK_INTERNAL"),
                                      QStringLiteral("Falha ao habilitar o cluster."));
        }

        QJsonObject payload;
        payload.insert(QStringLiteral("jobId"), jobId);
        if (enableError.isEmpty()) {
            payload.insert(QStringLiteral("cluster"), cluster.toJson());
        } else {
            payload.insert(QStringLiteral("error"), enableError.toJson());
        }
        if (notify) {
            notify(QStringLiteral("k8s.done"), payload);
        }

        {
            std::lock_guard<std::mutex> lock(m_k8sJobsMutex);
            m_k8sSubscriptions.remove(jobId);
        }
        removeSubscription(subscriptionId);
    });

    QJsonObject result;
    result.insert(QStringLiteral("jobId"), jobId);
    result.insert(QStringLiteral("subscriptionId"), subscriptionId);
    return result;
}

QJsonObject Daemon::k8sPodDelete(const QJsonObject &params, Error *error)
{
    const QString ns = params.value(QStringLiteral("namespace")).toString().trimmed();
    const QString name = params.value(QStringLiteral("name")).toString().trimmed();
    if (!m_k8s.deleteResource(QStringLiteral("pod"), ns, name, error)) {
        return {};
    }
    return QJsonObject{{QStringLiteral("deleted"), true}};
}

QJsonObject Daemon::k8sPodsList(const QJsonObject &params, Error *error)
{
    const QJsonValue namespaceValue = params.value(QStringLiteral("namespace"));
    if (!namespaceValue.isUndefined() && !namespaceValue.isNull() && !namespaceValue.isString()) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_INVALID_PARAMS"),
                                 QStringLiteral("namespace deve ser uma string."));
        }
        return {};
    }
    const QJsonArray items = m_k8s.pods(namespaceValue.toString().trimmed(), error);
    if (error && !error->isEmpty()) {
        return {};
    }
    return QJsonObject{{QStringLiteral("items"), items}};
}

QJsonObject Daemon::k8sPortForwardStart(const QJsonObject &params, const Notify &notify,
                                        Error *error)
{
    const QString resource =
        params.value(QStringLiteral("resource")).toString(QStringLiteral("pod")).trimmed();
    const QString ns = params.value(QStringLiteral("namespace")).toString().trimmed();
    const QString pod = params.value(QStringLiteral("pod")).toString().trimmed();
    const int localPort = params.value(QStringLiteral("localPort")).toInt();
    const int remotePort = params.value(QStringLiteral("remotePort")).toInt();
    if (!K8sManager::validatePortForward(resource, ns, pod, localPort, remotePort, error) ||
        !m_k8s.checkClusterReady(error)) {
        return {};
    }
    const bool exposeLan =
        Config::load().get(QStringLiteral("kubernetes.exposeServices")).toBool(false);

    QString id;
    {
        std::lock_guard<std::mutex> lock(m_k8sJobsMutex);
        id = QStringLiteral("fwd-%1").arg(m_nextK8sForward++);
    }
    auto subscription = std::make_shared<Subscription>();
    {
        std::lock_guard<std::mutex> lock(m_k8sJobsMutex);
        m_k8sForwards.insert(id, subscription);
    }

    (void) QtConcurrent::run([this, id, resource, ns, pod, localPort, remotePort, exposeLan,
                              subscription, notify] {
        const auto onLine = [&notify, &id](const QString &line) {
            QJsonObject payload;
            payload.insert(QStringLiteral("id"), id);
            payload.insert(QStringLiteral("line"), line);
            notify(QStringLiteral("k8s.portforward.delta"), payload);
        };
        const auto cancelled = [subscription] { return subscription->cancel.load(); };
        Error forwardError;
        m_k8s.portForward(resource, ns, pod, localPort, remotePort, exposeLan, onLine, cancelled,
                          &forwardError);

        QJsonObject payload;
        payload.insert(QStringLiteral("id"), id);
        if (!forwardError.isEmpty()) {
            payload.insert(QStringLiteral("error"), forwardError.toJson());
        }
        notify(QStringLiteral("k8s.portforward.done"), payload);
        {
            std::lock_guard<std::mutex> lock(m_k8sJobsMutex);
            m_k8sForwards.remove(id);
        }
    });

    return QJsonObject{{QStringLiteral("id"), id},
                       {QStringLiteral("resource"), resource},
                       {QStringLiteral("namespace"), ns},
                       {QStringLiteral("pod"), pod},
                       {QStringLiteral("localPort"), localPort},
                       {QStringLiteral("remotePort"), remotePort}};
}

QJsonObject Daemon::k8sPortForwardStop(const QJsonObject &params, Error *error)
{
    const QString id = params.value(QStringLiteral("id")).toString().trimmed();
    std::shared_ptr<Subscription> subscription;
    {
        std::lock_guard<std::mutex> lock(m_k8sJobsMutex);
        subscription = m_k8sForwards.value(id);
    }
    if (!subscription) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_NOT_FOUND"),
                                 QStringLiteral("Port-forward não está ativo."), id);
        }
        return {};
    }
    subscription->cancel.store(true);
    return QJsonObject{{QStringLiteral("id"), id}, {QStringLiteral("stopped"), true}};
}

QJsonObject Daemon::k8sReset(const QJsonObject &params, const Notify &notify, Error *error)
{
    Q_UNUSED(params);
    QJsonObject forceParams{{QStringLiteral("force"), true}};
    return k8sDisable(forceParams, notify, error);
}

QJsonObject Daemon::k8sServiceDelete(const QJsonObject &params, Error *error)
{
    const QString ns = params.value(QStringLiteral("namespace")).toString().trimmed();
    const QString name = params.value(QStringLiteral("name")).toString().trimmed();
    if (!m_k8s.deleteResource(QStringLiteral("service"), ns, name, error)) {
        return {};
    }
    return QJsonObject{{QStringLiteral("deleted"), true}};
}

QJsonObject Daemon::k8sServicesList(const QJsonObject &params, Error *error)
{
    const QJsonValue namespaceValue = params.value(QStringLiteral("namespace"));
    if (!namespaceValue.isUndefined() && !namespaceValue.isNull() && !namespaceValue.isString()) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_INVALID_PARAMS"),
                                 QStringLiteral("namespace deve ser uma string."));
        }
        return {};
    }
    const QJsonArray items = m_k8s.services(namespaceValue.toString().trimmed(), error);
    if (error && !error->isEmpty()) {
        return {};
    }
    return QJsonObject{{QStringLiteral("items"), items}};
}

QJsonObject Daemon::k8sStatus(const QJsonObject &params, Error *error)
{
    const QString name = params.value(QStringLiteral("cluster")).toString().trimmed();
    K8sCluster cluster = name.isEmpty() ? m_k8s.status(error) : m_k8s.status(name, error);
    if (error && !error->isEmpty()) {
        return {};
    }

    QJsonObject result = cluster.toJson();
    result.insert(QStringLiteral("enabled"),
                  !cluster.state.isEmpty() && cluster.state != QStringLiteral("disabled"));
    result.insert(QStringLiteral("version"), QString());
    return result;
}

QJsonObject Daemon::subscribeK8sLogs(const QJsonObject &params, const Notify &notify, Error *error)
{
    const QString ns = params.value(QStringLiteral("namespace")).toString().trimmed();
    const QString pod = params.value(QStringLiteral("pod")).toString().trimmed();
    const QString container = params.value(QStringLiteral("container")).toString().trimmed();
    const int tail = params.value(QStringLiteral("tail")).toInt(500);
    if (!K8sManager::validateLogsRequest(ns, pod, container, tail, error)) {
        return {};
    }

    auto subscription = std::make_shared<Subscription>();
    const QString subscriptionId = createSubscription(subscription);
    (void) QtConcurrent::run(
        [this, ns, pod, container, tail, subscriptionId, subscription, notify] {
            const auto onLine = [&notify, &ns, &pod, &container](const QString &line) {
                QJsonObject payload;
                payload.insert(QStringLiteral("namespace"), ns);
                payload.insert(QStringLiteral("pod"), pod);
                payload.insert(QStringLiteral("container"), container);
                payload.insert(QStringLiteral("line"), line);
                notify(QStringLiteral("k8s.logs.delta"), payload);
            };
            const auto cancelled = [subscription] { return subscription->cancel.load(); };

            Error streamError;
            m_k8s.streamLogs(ns, pod, container, tail, onLine, cancelled, &streamError);

            QJsonObject payload;
            payload.insert(QStringLiteral("namespace"), ns);
            payload.insert(QStringLiteral("pod"), pod);
            payload.insert(QStringLiteral("container"), container);
            if (!streamError.isEmpty()) {
                payload.insert(QStringLiteral("error"), streamError.toJson());
            }
            notify(QStringLiteral("k8s.logs.done"), payload);
            removeSubscription(subscriptionId);
        });

    return QJsonObject{{QStringLiteral("subscriptionId"), subscriptionId}};
}

} // namespace PodStack
