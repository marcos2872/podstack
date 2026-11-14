// SPDX-License-Identifier: GPL-3.0-or-later
#include "podman/client/Common.h"

using namespace PodStack::PodmanInternal;

namespace PodStack {

bool PodmanClient::execOnce(const QString &id, const QStringList &cmd, ExecResult *result,
                            Error *error)
{
    if (error) {
        *error = {};
    }
    if (!ensureVersion(error)) {
        return false;
    }
    if (cmd.isEmpty()) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_INVALID_PARAMS"),
                                 QStringLiteral("Informe o comando para exec."));
        }
        return false;
    }

    QJsonObject spec;
    spec.insert(QStringLiteral("AttachStdin"), false);
    spec.insert(QStringLiteral("AttachStdout"), true);
    spec.insert(QStringLiteral("AttachStderr"), true);
    spec.insert(QStringLiteral("Tty"), true);

    QJsonArray command;
    for (const QString &part : cmd) {
        command.append(part);
    }
    spec.insert(QStringLiteral("Cmd"), command);

    const QByteArray createBody = QJsonDocument(spec).toJson(QJsonDocument::Compact);
    const HttpResponse create = m_http.request(
        QByteArrayLiteral("POST"), path(QStringLiteral("/containers/%1/exec").arg(encodeId(id))),
        createBody, QStringLiteral("application/json"));
    if (create.status != 201 && create.status != 200) {
        if (error) {
            *error = errorFromResponse(create, QStringLiteral("Falha ao criar o exec."));
        }
        return false;
    }

    const QJsonObject created = QJsonDocument::fromJson(create.body).object();
    const QString execId = created.value(QStringLiteral("Id")).toString();
    if (execId.isEmpty()) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_INVALID_RESPONSE"),
                                 QStringLiteral("O Podman não retornou o ID do exec."));
        }
        return false;
    }

    QJsonObject startSpec;
    startSpec.insert(QStringLiteral("Detach"), false);
    startSpec.insert(QStringLiteral("Tty"), true);
    const QByteArray startBody = QJsonDocument(startSpec).toJson(QJsonDocument::Compact);

    ExecResult local;
    QString hijackError;
    const bool hijacked = m_http.hijack(
        path(QStringLiteral("/exec/%1/start").arg(encodeId(execId))), startBody,
        [&local](const QByteArray &data) {
            local.output.append(data);
            return true;
        },
        nullptr, &hijackError);

    if (!hijacked) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_EXEC_FAILED"),
                                 QStringLiteral("Falha no exec do container."), hijackError);
        }
        return false;
    }

    const HttpResponse inspect =
        m_http.request(QByteArrayLiteral("GET"), path(QStringLiteral("/exec/%1/json").arg(execId)));
    if (inspect.status == 200) {
        const QJsonObject object = QJsonDocument::fromJson(inspect.body).object();
        local.exitCode = object.value(QStringLiteral("ExitCode")).toInt(-1);
    }

    if (result) {
        *result = local;
    }
    return true;
}

} // namespace PodStack
