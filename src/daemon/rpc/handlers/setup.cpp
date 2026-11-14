// SPDX-License-Identifier: GPL-3.0-or-later
#include "daemon/rpc/handlers/Common.h"

using namespace PodStack::DaemonInternal;

namespace PodStack {

QJsonObject Daemon::setupApply(const QJsonObject &params, const Notify &notify, Error *error)
{
    return m_setup.apply(params, notify, error);
}

QJsonObject Daemon::setupCancel(const QJsonObject &params)
{
    return m_setup.cancel(params);
}

QJsonObject Daemon::setupCheck(const QJsonObject &params)
{
    const bool deep = params.value(QStringLiteral("deep")).toBool(false);
    QJsonArray checks;
    for (const SetupCheck &check : m_setup.check(deep)) {
        checks.append(check.toJson());
    }

    QJsonObject result;
    result.insert(QStringLiteral("checks"), checks);
    return result;
}

QJsonObject Daemon::setupComplete(const QJsonObject &params, Error *error)
{
    const bool skipped = params.value(QStringLiteral("skipped")).toBool(false);
    if (!m_setup.complete(skipped, error)) {
        return {};
    }

    QJsonObject result;
    result.insert(QStringLiteral("completed"), !skipped);
    result.insert(QStringLiteral("skipped"), skipped);
    return result;
}

QJsonObject Daemon::setupDockerImpact()
{
    return m_setup.dockerImpact();
}

QJsonObject Daemon::setupRemoveDocker(const QJsonObject &params, const Notify &notify,
                                      Error *error)
{
    return m_setup.removeDocker(params, notify, error);
}

QJsonObject Daemon::setupStatus()
{
    return m_setup.status();
}

QJsonObject Daemon::setupSubscribe(const QJsonObject &params, const Notify &notify, Error *error)
{
    const QString jobId = params.value(QStringLiteral("jobId")).toString();
    if (jobId.isEmpty()) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_INVALID_PARAMS"),
                                 QStringLiteral("Informe o job de setup."));
        }
        return {};
    }

    const QJsonObject job = m_setup.jobStatus(jobId);
    if (job.isEmpty()) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_NOT_FOUND"),
                                 QStringLiteral("Job de setup não encontrado."));
        }
        return {};
    }

    m_setup.subscribe(jobId, notify);
    return QJsonObject{{QStringLiteral("jobId"), jobId}};
}

QJsonObject Daemon::setupValidate()
{
    return m_setup.validate();
}

} // namespace PodStack
