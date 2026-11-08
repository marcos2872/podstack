#include "daemon/Daemon.h"

#include "core/Config.h"
#include "core/Logging.h"
#include "core/Paths.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStringList>
#include <QtConcurrent>

namespace PodStack {
namespace {

QString selectedPodmanSocket()
{
    const Config config = Config::load();
    if (config.get(QStringLiteral("engine.mode")).toString() == QStringLiteral("rootful")) {
        return QStringLiteral("/run/podman/podman.sock");
    }
    return Paths::podmanSocket();
}

QString engineMode()
{
    const Config config = Config::load();
    return config.get(QStringLiteral("engine.mode")).toString(QStringLiteral("rootless"));
}

} // namespace

Daemon::Daemon()
    : m_podman(selectedPodmanSocket())
{
    Error error;
    if (!m_podman.ping(&error)) {
        qCWarning(lcPodman) << "Podman indisponível:" << error.message << error.detail;
    } else {
        qCInfo(lcPodman) << "Podman conectado; API" << m_podman.apiVersion() << "modo"
                         << engineMode();
    }
}

QJsonObject Daemon::handle(const QString &method, const QJsonObject &params,
                           const Notify &notify, Error *error)
{
    if (error) {
        *error = {};
    }

    if (method == QStringLiteral("system.hello")) {
        return systemHello();
    }
    if (method == QStringLiteral("system.status")) {
        return systemStatus();
    }
    if (method == QStringLiteral("system.doctor")) {
        return systemDoctor();
    }
    if (method == QStringLiteral("containers.list")) {
        return containersList(params, error);
    }
    if (method == QStringLiteral("containers.start") || method == QStringLiteral("containers.stop")
        || method == QStringLiteral("containers.restart")
        || method == QStringLiteral("containers.remove")
        || method == QStringLiteral("containers.pause")
        || method == QStringLiteral("containers.unpause")
        || method == QStringLiteral("containers.kill")) {
        return containerAction(method, params, error);
    }
    if (method == QStringLiteral("containers.exec")) {
        return containerExec(params, error);
    }
    if (method == QStringLiteral("containers.inspect")) {
        return containerInspect(params, error);
    }
    if (method == QStringLiteral("containers.logs.subscribe")) {
        return subscribeLogs(params, notify, error);
    }
    if (method == QStringLiteral("containers.logs.unsubscribe")
        || method == QStringLiteral("events.unsubscribe")) {
        return unsubscribe(params);
    }
    if (method == QStringLiteral("events.subscribe")) {
        return subscribeEvents(notify, error);
    }

    if (error) {
        *error = Error::make(QStringLiteral("PODSTACK_METHOD_NOT_FOUND"),
                             QStringLiteral("Método RPC desconhecido: %1").arg(method));
    }
    return {};
}

QJsonObject Daemon::systemHello() const
{
    QJsonObject podman;
    podman.insert(QStringLiteral("apiVersion"), m_podman.apiVersion());
    podman.insert(QStringLiteral("socket"), m_podman.socketPath());
    podman.insert(QStringLiteral("mode"), engineMode());

    QJsonObject result;
    result.insert(QStringLiteral("apiVersion"), 1);
    result.insert(QStringLiteral("daemonVersion"), QStringLiteral(PODSTACK_VERSION));
    result.insert(QStringLiteral("podman"), podman);
    return result;
}

QJsonObject Daemon::systemStatus()
{
    Error error;
    const bool ok = m_podman.ping(&error);

    QJsonObject podman;
    podman.insert(QStringLiteral("apiVersion"), m_podman.apiVersion());
    podman.insert(QStringLiteral("socket"), m_podman.socketPath());
    podman.insert(QStringLiteral("mode"), engineMode());

    QJsonObject result;
    result.insert(QStringLiteral("state"), ok ? QStringLiteral("ok") : QStringLiteral("error"));
    result.insert(QStringLiteral("engine"),
                  ok ? QStringLiteral("running") : QStringLiteral("unavailable"));
    result.insert(QStringLiteral("daemonVersion"), QStringLiteral(PODSTACK_VERSION));
    result.insert(QStringLiteral("podman"), podman);
    if (!ok) {
        result.insert(QStringLiteral("error"), error.toJson());
    }
    return result;
}

QJsonObject Daemon::systemDoctor()
{
    QJsonArray checks;

    const bool socketExists = QFile::exists(Paths::controlSocket());
    QJsonObject socketCheck;
    socketCheck.insert(QStringLiteral("id"), QStringLiteral("daemon.socket"));
    socketCheck.insert(QStringLiteral("status"),
                       socketExists ? QStringLiteral("ok") : QStringLiteral("fail"));
    socketCheck.insert(QStringLiteral("detail"), Paths::controlSocket());
    checks.append(socketCheck);

    Error error;
    const bool podmanOk = m_podman.ping(&error);
    QJsonObject podmanCheck;
    podmanCheck.insert(QStringLiteral("id"), QStringLiteral("podman.socket"));
    podmanCheck.insert(QStringLiteral("status"),
                       podmanOk ? QStringLiteral("ok") : QStringLiteral("fail"));
    podmanCheck.insert(QStringLiteral("detail"),
                       podmanOk ? m_podman.socketPath() : error.message);
    if (!podmanOk && !error.remediation.isEmpty()) {
        podmanCheck.insert(QStringLiteral("remediation"), error.remediation);
    }
    checks.append(podmanCheck);

    QJsonObject result;
    result.insert(QStringLiteral("checks"), checks);
    return result;
}

QJsonObject Daemon::containersList(const QJsonObject &params, Error *error)
{
    const bool all = params.value(QStringLiteral("all")).toBool(false);
    const QString state = params.value(QStringLiteral("state")).toString();
    const QVector<ContainerInfo> containers = m_podman.listContainers(all, error);
    if (error && !error->isEmpty()) {
        return {};
    }

    QJsonArray items;
    for (const ContainerInfo &container : containers) {
        if (!state.isEmpty() && state != QStringLiteral("all")) {
            if (state == QStringLiteral("running") && container.state != QStringLiteral("running")) {
                continue;
            }
            if (state == QStringLiteral("paused") && container.state != QStringLiteral("paused")) {
                continue;
            }
            if (state == QStringLiteral("stopped")
                && container.state != QStringLiteral("exited")
                && container.state != QStringLiteral("created")
                && container.state != QStringLiteral("dead")
                && container.state != QStringLiteral("stopped")) {
                continue;
            }
        }
        items.append(container.toJson());
    }

    QJsonObject result;
    result.insert(QStringLiteral("items"), items);
    result.insert(QStringLiteral("total"), items.size());
    return result;
}

QJsonObject Daemon::containerAction(const QString &method, const QJsonObject &params,
                                    Error *error)
{
    const QString id = requireId(params, error);
    if (id.isEmpty()) {
        return {};
    }

    bool ok = false;
    if (method == QStringLiteral("containers.start")) {
        ok = m_podman.startContainer(id, error);
    } else if (method == QStringLiteral("containers.stop")) {
        ok = m_podman.stopContainer(
            id, params.value(QStringLiteral("timeoutSeconds")).toInt(10), error);
    } else if (method == QStringLiteral("containers.restart")) {
        ok = m_podman.restartContainer(
            id, params.value(QStringLiteral("timeoutSeconds")).toInt(10), error);
    } else if (method == QStringLiteral("containers.remove")) {
        ok = m_podman.removeContainer(id, params.value(QStringLiteral("force")).toBool(false),
                                      params.value(QStringLiteral("volumes")).toBool(false), error);
    } else if (method == QStringLiteral("containers.pause")) {
        ok = m_podman.pauseContainer(id, error);
    } else if (method == QStringLiteral("containers.unpause")) {
        ok = m_podman.unpauseContainer(id, error);
    } else if (method == QStringLiteral("containers.kill")) {
        ok = m_podman.killContainer(id, params.value(QStringLiteral("signal")).toString(), error);
    }

    if (!ok) {
        return {};
    }

    QJsonObject result;
    result.insert(QStringLiteral("id"), id);
    result.insert(QStringLiteral("ok"), true);
    return result;
}

QJsonObject Daemon::containerExec(const QJsonObject &params, Error *error)
{
    const QString id = requireId(params, error);
    if (id.isEmpty()) {
        return {};
    }

    const QJsonArray commandArray = params.value(QStringLiteral("cmd")).toArray();
    if (commandArray.isEmpty()) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_INVALID_PARAMS"),
                                 QStringLiteral("Informe o comando a executar."));
        }
        return {};
    }

    QStringList command;
    for (const QJsonValue &value : commandArray) {
        command.append(value.toString());
    }

    ExecResult execResult;
    if (!m_podman.execOnce(id, command, &execResult, error)) {
        return {};
    }

    QJsonObject result;
    result.insert(QStringLiteral("id"), id);
    result.insert(QStringLiteral("exitCode"), execResult.exitCode);
    result.insert(QStringLiteral("stdout"), QString::fromUtf8(execResult.output));
    return result;
}

QJsonObject Daemon::containerInspect(const QJsonObject &params, Error *error)
{
    const QString id = requireId(params, error);
    if (id.isEmpty()) {
        return {};
    }

    const QJsonObject json = m_podman.inspectContainer(id, error);
    if (error && !error->isEmpty()) {
        return {};
    }

    QJsonObject result;
    result.insert(QStringLiteral("id"), id);
    result.insert(QStringLiteral("json"), json);
    return result;
}

QJsonObject Daemon::subscribeLogs(const QJsonObject &params, const Notify &notify, Error *error)
{
    const QString id = requireId(params, error);
    if (id.isEmpty()) {
        return {};
    }

    const int tail = params.value(QStringLiteral("tail")).toInt(200);
    const bool follow = params.value(QStringLiteral("follow")).toBool(true);

    auto subscription = std::make_shared<Subscription>();
    const QString subscriptionId = createSubscription(subscription);

    (void)QtConcurrent::run([this, id, tail, follow, subscriptionId, subscription, notify] {
        QByteArray buffer;
        Error streamError;

        const auto onData = [&](const QByteArray &data) {
            buffer.append(data);
            int newline = -1;
            while ((newline = buffer.indexOf('\n')) >= 0) {
                QByteArray line = buffer.left(newline);
                buffer.remove(0, newline + 1);
                if (line.endsWith('\r')) {
                    line.chop(1);
                }
                QJsonObject payload;
                payload.insert(QStringLiteral("subscriptionId"), subscriptionId);
                payload.insert(QStringLiteral("line"), QString::fromUtf8(line));
                notify(QStringLiteral("containers.logs.delta"), payload);
                if (subscription->cancel.load()) {
                    return false;
                }
            }
            return !subscription->cancel.load();
        };

        m_podman.streamLogs(id, tail, follow, onData, &subscription->cancel, &streamError);
        if (!streamError.isEmpty() && !subscription->cancel.load()) {
            QJsonObject payload;
            payload.insert(QStringLiteral("subscriptionId"), subscriptionId);
            payload.insert(QStringLiteral("error"), streamError.toJson());
            notify(QStringLiteral("containers.logs.error"), payload);
        }
        removeSubscription(subscriptionId);
    });

    QJsonObject result;
    result.insert(QStringLiteral("subscriptionId"), subscriptionId);
    return result;
}

QJsonObject Daemon::subscribeEvents(const Notify &notify, Error *error)
{
    if (error) {
        *error = {};
    }

    auto subscription = std::make_shared<Subscription>();
    const QString subscriptionId = createSubscription(subscription);

    (void)QtConcurrent::run([this, subscriptionId, subscription, notify] {
        QByteArray buffer;
        Error streamError;

        const auto onData = [&](const QByteArray &data) {
            buffer.append(data);
            int newline = -1;
            while ((newline = buffer.indexOf('\n')) >= 0) {
                const QByteArray line = buffer.left(newline);
                buffer.remove(0, newline + 1);
                const QJsonDocument document = QJsonDocument::fromJson(line);
                if (document.isObject()) {
                    notify(QStringLiteral("event"), document.object());
                }
                if (subscription->cancel.load()) {
                    return false;
                }
            }
            return !subscription->cancel.load();
        };

        m_podman.streamEvents(onData, &subscription->cancel, &streamError);
        removeSubscription(subscriptionId);
    });

    QJsonObject result;
    result.insert(QStringLiteral("subscriptionId"), subscriptionId);
    return result;
}

QJsonObject Daemon::unsubscribe(const QJsonObject &params)
{
    const QString subscriptionId = params.value(QStringLiteral("subscriptionId")).toString();

    std::shared_ptr<Subscription> subscription;
    {
        std::lock_guard<std::mutex> lock(m_subscriptionsMutex);
        subscription = m_subscriptions.take(subscriptionId);
    }
    if (subscription) {
        subscription->cancel.store(true);
    }

    QJsonObject result;
    result.insert(QStringLiteral("subscriptionId"), subscriptionId);
    result.insert(QStringLiteral("cancelled"), static_cast<bool>(subscription));
    return result;
}

QString Daemon::requireId(const QJsonObject &params, Error *error) const
{
    const QString id = params.value(QStringLiteral("id")).toString();
    if (id.isEmpty() && error) {
        *error = Error::make(QStringLiteral("PODSTACK_INVALID_PARAMS"),
                             QStringLiteral("Informe o ID ou nome do container."));
    }
    return id;
}

QString Daemon::createSubscription(const std::shared_ptr<Subscription> &subscription)
{
    std::lock_guard<std::mutex> lock(m_subscriptionsMutex);
    const QString id = QStringLiteral("sub-%1").arg(m_nextSubscription++);
    m_subscriptions.insert(id, subscription);
    return id;
}

void Daemon::removeSubscription(const QString &id)
{
    std::lock_guard<std::mutex> lock(m_subscriptionsMutex);
    m_subscriptions.remove(id);
}

} // namespace PodStack
