// SPDX-License-Identifier: GPL-3.0-or-later
#include "gui/controllers/ControllerBase.h"

#include "core/error/Error.h"
#include "core/rpc/RpcClient.h"

#include <QFutureWatcher>
#include <QtConcurrent>

namespace PodStack {

namespace {
int g_logBufferLines = 10000;

QJsonObject errorPayload(const Error &error)
{
    QJsonObject payload;
    payload.insert(QStringLiteral("__error"), error.toJson());
    return payload;
}

} // namespace

ControllerBase::ControllerBase(QObject *parent) : QObject(parent) {}

void ControllerBase::clearLastError()
{
    if (m_lastError.isEmpty()) {
        return;
    }
    m_lastError.clear();
    emit lastErrorChanged();
}

QString ControllerBase::errorMessage(const QJsonObject &result)
{
    const QJsonObject error = result.value(QStringLiteral("__error")).isObject()
                                  ? result.value(QStringLiteral("__error")).toObject()
                                  : result.value(QStringLiteral("error")).toObject();
    QString message = error.value(QStringLiteral("message")).toString();
    const QString detail = error.value(QStringLiteral("detail")).toString();
    if (!detail.isEmpty() && !message.contains(detail)) {
        message += QLatin1Char(' ') + detail;
    }
    const QString remediation = error.value(QStringLiteral("remediation")).toString();
    if (!remediation.isEmpty()) {
        message += QLatin1Char(' ') + remediation;
    }
    return message;
}

bool ControllerBase::isErrorResult(const QJsonObject &result)
{
    return result.value(QStringLiteral("__error")).isObject() ||
           result.value(QStringLiteral("error")).isObject();
}

// O QFutureWatcher é deletado com deleteLater no handler; o analyzer não
// acompanha o ownership através do lambda.
// NOLINTBEGIN(clang-analyzer-cplusplus.NewDeleteLeaks)
void ControllerBase::callRpc(const QString &method, const QJsonObject &params, Callback done,
                             int timeoutMs)
{
    // NOLINTNEXTLINE(clang-analyzer-cplusplus.NewDeleteLeaks)
    auto *watcher = new QFutureWatcher<QJsonObject>(this);
    connect(watcher, &QFutureWatcher<QJsonObject>::finished, this,
            [watcher, done = std::move(done)]() {
                const QJsonObject result = watcher->result();
                watcher->deleteLater();
                if (done) {
                    done(result);
                }
            });
    watcher->setFuture(QtConcurrent::run([method, params, timeoutMs] {
        RpcClient client;
        Error error;
        const QJsonObject result = client.call(method, params, &error, timeoutMs);
        if (!error.isEmpty()) {
            return errorPayload(error);
        }
        return result;
    }));
}
// NOLINTEND(clang-analyzer-cplusplus.NewDeleteLeaks)

void ControllerBase::runAction(const QString &method, const QJsonObject &params, Callback done,
                               int timeoutMs)
{
    PendingAction action;
    action.method = method;
    action.params = params;
    action.timeoutMs = timeoutMs;
    if (m_actionActive) {
        m_actionQueue.enqueue(action);
        return;
    }
    m_actionActive = true;
    emit actionStateChanged();
    startAction(action, std::move(done));
}

void ControllerBase::runJob(const QString &label, const QJsonObject &params, Job job, Callback done,
                            int timeoutMs)
{
    PendingAction action;
    action.method = label;
    action.params = params;
    action.timeoutMs = timeoutMs;
    action.job = std::move(job);
    if (m_actionActive) {
        m_actionQueue.enqueue(action);
        return;
    }
    m_actionActive = true;
    emit actionStateChanged();
    startAction(action, std::move(done));
}

// NOLINTBEGIN(clang-analyzer-cplusplus.NewDeleteLeaks)
void ControllerBase::startAction(const PendingAction &action, Callback done)
{
    m_currentAction = action;
    auto finish = [this, action, done = std::move(done)](const QJsonObject &result) {
        onActionFinished(action.method, action.params, result);
        if (done) {
            done(result);
        }
        if (!m_actionQueue.isEmpty()) {
            const PendingAction next = m_actionQueue.dequeue();
            startAction(next, {});
            return;
        }
        m_actionActive = false;
        m_currentAction = {};
        emit actionStateChanged();
    };

    if (action.job) {
        // NOLINTNEXTLINE(clang-analyzer-cplusplus.NewDeleteLeaks)
        auto *watcher = new QFutureWatcher<QJsonObject>(this);
        connect(watcher, &QFutureWatcher<QJsonObject>::finished, this,
                [watcher, finish = std::move(finish)]() {
                    const QJsonObject result = watcher->result();
                    watcher->deleteLater();
                    finish(result);
                });
        watcher->setFuture(QtConcurrent::run(action.job));
        return;
    }

    callRpc(action.method, action.params, finish, action.timeoutMs);
}
// NOLINTEND(clang-analyzer-cplusplus.NewDeleteLeaks)

void ControllerBase::enqueueAction(const PendingAction &action)
{
    if (m_actionActive) {
        m_actionQueue.enqueue(action);
        return;
    }
    m_actionActive = true;
    emit actionStateChanged();
    startAction(action, {});
}

void ControllerBase::onActionFinished(const QString &, const QJsonObject &,
                                      const QJsonObject &result)
{
    if (isErrorResult(result)) {
        setLastError(errorMessage(result));
    } else {
        clearLastError();
    }
}

int ControllerBase::logBufferLines()
{
    return g_logBufferLines;
}

void ControllerBase::setLogBufferLines(int lines)
{
    if (lines >= 100 && lines <= 1000000) {
        g_logBufferLines = lines;
    }
}

void ControllerBase::setLastError(const QString &message)
{
    if (m_lastError == message) {
        return;
    }
    m_lastError = message;
    emit lastErrorChanged();
}

QJsonObject ControllerBase::runBatchCalls(const QString &prefix, const QString &idKey,
                                          const QString &action, const QStringList &ids, bool force,
                                          const QJsonObject &extra)
{
    RpcClient client;
    int failed = 0;
    QJsonObject lastError;

    for (const QString &id : ids) {
        Error error;
        QJsonObject params = extra;
        params.insert(idKey, id);
        if (force) {
            params.insert(QStringLiteral("force"), true);
        }
        client.call(prefix + action, params, &error);
        if (!error.isEmpty()) {
            ++failed;
            lastError = error.toJson();
        }
    }

    QJsonObject result;
    result.insert(QStringLiteral("count"), ids.size());
    result.insert(QStringLiteral("failed"), failed);
    if (failed > 0) {
        result.insert(QStringLiteral("error"), lastError);
    } else {
        result.insert(QStringLiteral("ok"), true);
    }
    return result;
}

} // namespace PodStack
