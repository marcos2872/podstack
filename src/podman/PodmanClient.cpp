#include "podman/PodmanClient.h"

#include "core/Paths.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUrl>
#include <QtEndian>

#include <utility>

namespace PodStack {
namespace {

QString encodeId(const QString &id)
{
    return QString::fromLatin1(QUrl::toPercentEncoding(id));
}

} // namespace

QJsonObject ContainerInfo::toJson() const
{
    QJsonObject obj;
    obj.insert(QStringLiteral("id"), id);
    obj.insert(QStringLiteral("name"), name);
    obj.insert(QStringLiteral("image"), image);
    obj.insert(QStringLiteral("state"), state);
    obj.insert(QStringLiteral("status"), status);
    obj.insert(QStringLiteral("created"), created);

    QJsonArray portArray;
    for (const QString &port : ports) {
        portArray.append(port);
    }
    obj.insert(QStringLiteral("ports"), portArray);

    QJsonObject labelObject;
    for (auto it = labels.constBegin(); it != labels.constEnd(); ++it) {
        labelObject.insert(it.key(), it.value());
    }
    obj.insert(QStringLiteral("labels"), labelObject);
    return obj;
}

PodmanClient::PodmanClient(QString socketPath)
    : m_http(socketPath.isEmpty() ? Paths::podmanSocket() : std::move(socketPath))
{
}

QString PodmanClient::path(const QString &suffix) const
{
    std::lock_guard<std::recursive_mutex> lock(m_versionMutex);
    if (m_apiVersion.isEmpty()) {
        return QStringLiteral("/libpod") + suffix;
    }
    return QStringLiteral("/") + m_apiVersion + QStringLiteral("/libpod") + suffix;
}

QString PodmanClient::apiVersion() const
{
    std::lock_guard<std::recursive_mutex> lock(m_versionMutex);
    return m_apiVersion;
}

Error PodmanClient::errorFromResponse(const HttpResponse &response, const QString &fallback)
{
    QString message = fallback;
    QString detail = response.error;

    const QJsonDocument document = QJsonDocument::fromJson(response.body);
    if (document.isObject()) {
        const QJsonObject object = document.object();
        const QString engineMessage = object.value(QStringLiteral("message")).toString();
        if (!engineMessage.isEmpty()) {
            message = engineMessage;
        }
        const QString cause = object.value(QStringLiteral("cause")).toString();
        if (!cause.isEmpty()) {
            detail = cause;
        } else if (detail.isEmpty()) {
            detail = QString::fromUtf8(response.body.left(512));
        }
    } else if (detail.isEmpty()) {
        detail = QString::fromUtf8(response.body.left(512));
    }

    QString code = QStringLiteral("PODSTACK_ENGINE_ERROR");
    if (response.status == 404) {
        code = QStringLiteral("PODSTACK_NOT_FOUND");
    } else if (response.status == 409) {
        code = QStringLiteral("PODSTACK_CONFLICT");
    } else if (response.status == 403) {
        code = QStringLiteral("PODSTACK_PERMISSION_DENIED");
    }

    return Error::make(code, message, detail);
}

bool PodmanClient::ping(Error *error)
{
    std::lock_guard<std::recursive_mutex> lock(m_versionMutex);
    if (error) {
        *error = {};
    }
    const HttpResponse response = m_http.request(QByteArrayLiteral("GET"), QStringLiteral("/_ping"));
    if (response.status != 200) {
        if (error) {
            *error = errorFromResponse(response,
                                       QStringLiteral("O Podman não respondeu ao ping."));
        }
        return false;
    }

    QString version = response.headers.value(QStringLiteral("libpod-api-version"));
    if (version.isEmpty()) {
        version = response.headers.value(QStringLiteral("api-version"));
    }
    if (!version.isEmpty() && !version.startsWith(QLatin1Char('v'))) {
        version.prepend(QLatin1Char('v'));
    }
    m_apiVersion = version;
    return true;
}

bool PodmanClient::ensureVersion(Error *error)
{
    std::lock_guard<std::recursive_mutex> lock(m_versionMutex);
    if (!m_apiVersion.isEmpty()) {
        return true;
    }
    return ping(error);
}

QVector<ContainerInfo> PodmanClient::listContainers(bool all, Error *error)
{
    if (error) {
        *error = {};
    }
    if (!ensureVersion(error)) {
        return {};
    }

    const QString suffix = all ? QStringLiteral("/containers/json?all=true")
                               : QStringLiteral("/containers/json?all=false");
    const HttpResponse response = m_http.request(QByteArrayLiteral("GET"), path(suffix));
    if (response.status != 200) {
        if (error) {
            *error = errorFromResponse(response,
                                       QStringLiteral("Falha ao listar containers."));
        }
        return {};
    }

    QJsonParseError parseError{};
    const QJsonDocument document = QJsonDocument::fromJson(response.body, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isArray()) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_INVALID_RESPONSE"),
                                 QStringLiteral("Resposta inválida ao listar containers."),
                                 parseError.errorString());
        }
        return {};
    }

    QVector<ContainerInfo> result;
    const QJsonArray array = document.array();
    result.reserve(array.size());

    for (const QJsonValue &value : array) {
        const QJsonObject object = value.toObject();
        ContainerInfo info;
        info.id = object.value(QStringLiteral("Id")).toString();
        if (info.id.isEmpty()) {
            info.id = object.value(QStringLiteral("ID")).toString();
        }

        const QJsonArray names = object.value(QStringLiteral("Names")).toArray();
        if (!names.isEmpty()) {
            info.name = names.first().toString();
            if (info.name.startsWith(QLatin1Char('/'))) {
                info.name.remove(0, 1);
            }
        }
        if (info.name.isEmpty()) {
            info.name = info.id.left(12);
        }

        info.image = object.value(QStringLiteral("Image")).toString();
        info.state = object.value(QStringLiteral("State")).toString();
        info.status = object.value(QStringLiteral("Status")).toString();
        info.created = object.value(QStringLiteral("Created")).toString();

        const QJsonArray ports = object.value(QStringLiteral("Ports")).toArray();
        for (const QJsonValue &portValue : ports) {
            const QJsonObject port = portValue.toObject();
            const QString hostIp = port.value(QStringLiteral("HostIp")).toString();
            const int hostPort = port.value(QStringLiteral("HostPort")).toInt();
            const int containerPort = port.value(QStringLiteral("ContainerPort")).toInt();
            const QString protocol = port.value(QStringLiteral("Protocol")).toString();

            QString text;
            if (hostPort > 0) {
                if (!hostIp.isEmpty() && hostIp != QStringLiteral("0.0.0.0")) {
                    text = QStringLiteral("%1:%2->%3/%4")
                               .arg(hostIp)
                               .arg(hostPort)
                               .arg(containerPort)
                               .arg(protocol);
                } else {
                    text = QStringLiteral("%1->%2/%3").arg(hostPort).arg(containerPort).arg(protocol);
                }
            } else if (containerPort > 0) {
                text = QStringLiteral("%1/%2").arg(containerPort).arg(protocol);
            }
            if (!text.isEmpty()) {
                info.ports.append(text);
            }
        }

        const QJsonObject labels = object.value(QStringLiteral("Labels")).toObject();
        for (auto it = labels.constBegin(); it != labels.constEnd(); ++it) {
            info.labels.insert(it.key(), it.value().toString());
        }

        result.append(info);
    }

    return result;
}

bool PodmanClient::startContainer(const QString &id, Error *error)
{
    if (error) {
        *error = {};
    }
    if (!ensureVersion(error)) {
        return false;
    }
    const HttpResponse response = m_http.request(
        QByteArrayLiteral("POST"), path(QStringLiteral("/containers/%1/start").arg(encodeId(id))));
    if (response.status == 204 || response.status == 304) {
        return true;
    }
    if (error) {
        *error = errorFromResponse(response, QStringLiteral("Falha ao iniciar o container."));
    }
    return false;
}

bool PodmanClient::stopContainer(const QString &id, int timeoutSeconds, Error *error)
{
    if (error) {
        *error = {};
    }
    if (!ensureVersion(error)) {
        return false;
    }
    const QString suffix =
        QStringLiteral("/containers/%1/stop?t=%2").arg(encodeId(id)).arg(timeoutSeconds);
    const HttpResponse response = m_http.request(QByteArrayLiteral("POST"), path(suffix));
    if (response.status == 204 || response.status == 304) {
        return true;
    }
    if (error) {
        *error = errorFromResponse(response, QStringLiteral("Falha ao parar o container."));
    }
    return false;
}

bool PodmanClient::restartContainer(const QString &id, int timeoutSeconds, Error *error)
{
    if (error) {
        *error = {};
    }
    if (!ensureVersion(error)) {
        return false;
    }
    const QString suffix =
        QStringLiteral("/containers/%1/restart?t=%2").arg(encodeId(id)).arg(timeoutSeconds);
    const HttpResponse response = m_http.request(QByteArrayLiteral("POST"), path(suffix));
    if (response.status == 204) {
        return true;
    }
    if (error) {
        *error = errorFromResponse(response, QStringLiteral("Falha ao reiniciar o container."));
    }
    return false;
}

bool PodmanClient::removeContainer(const QString &id, bool force, bool volumes, Error *error)
{
    if (error) {
        *error = {};
    }
    if (!ensureVersion(error)) {
        return false;
    }
    const QString suffix = QStringLiteral("/containers/%1?force=%2&volumes=%3")
                               .arg(encodeId(id))
                               .arg(force ? QStringLiteral("true") : QStringLiteral("false"))
                               .arg(volumes ? QStringLiteral("true") : QStringLiteral("false"));
    const HttpResponse response =
        m_http.request(QByteArrayLiteral("DELETE"), path(suffix));
    if (response.status == 204 || response.status == 200) {
        return true;
    }
    if (error) {
        *error = errorFromResponse(response, QStringLiteral("Falha ao remover o container."));
    }
    return false;
}

bool PodmanClient::pauseContainer(const QString &id, Error *error)
{
    if (error) {
        *error = {};
    }
    if (!ensureVersion(error)) {
        return false;
    }
    const HttpResponse response = m_http.request(
        QByteArrayLiteral("POST"), path(QStringLiteral("/containers/%1/pause").arg(encodeId(id))));
    if (response.status == 204) {
        return true;
    }
    if (error) {
        *error = errorFromResponse(response, QStringLiteral("Falha ao pausar o container."));
    }
    return false;
}

bool PodmanClient::unpauseContainer(const QString &id, Error *error)
{
    if (error) {
        *error = {};
    }
    if (!ensureVersion(error)) {
        return false;
    }
    const HttpResponse response = m_http.request(
        QByteArrayLiteral("POST"), path(QStringLiteral("/containers/%1/unpause").arg(encodeId(id))));
    if (response.status == 204) {
        return true;
    }
    if (error) {
        *error = errorFromResponse(response, QStringLiteral("Falha ao despausar o container."));
    }
    return false;
}

bool PodmanClient::killContainer(const QString &id, const QString &signal, Error *error)
{
    if (error) {
        *error = {};
    }
    if (!ensureVersion(error)) {
        return false;
    }

    QString suffix = QStringLiteral("/containers/%1/kill").arg(encodeId(id));
    if (!signal.isEmpty()) {
        suffix += QStringLiteral("?signal=")
                  + QString::fromLatin1(QUrl::toPercentEncoding(signal));
    }

    const HttpResponse response = m_http.request(QByteArrayLiteral("POST"), path(suffix));
    if (response.status == 204) {
        return true;
    }
    if (error) {
        *error = errorFromResponse(response, QStringLiteral("Falha ao matar o container."));
    }
    return false;
}

QJsonObject PodmanClient::inspectContainer(const QString &id, Error *error)
{
    if (error) {
        *error = {};
    }
    if (!ensureVersion(error)) {
        return {};
    }

    const HttpResponse response = m_http.request(
        QByteArrayLiteral("GET"), path(QStringLiteral("/containers/%1/json").arg(encodeId(id))));
    if (response.status != 200) {
        if (error) {
            *error = errorFromResponse(response, QStringLiteral("Falha ao inspecionar o container."));
        }
        return {};
    }

    const QJsonDocument document = QJsonDocument::fromJson(response.body);
    if (!document.isObject()) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_INVALID_RESPONSE"),
                                 QStringLiteral("Resposta inválida do inspect."));
        }
        return {};
    }
    return document.object();
}

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

bool PodmanClient::streamLogs(const QString &id, int tail, bool follow,
                              const std::function<bool(const QByteArray &)> &onData,
                              std::atomic_bool *cancel, Error *error)
{
    if (error) {
        *error = {};
    }
    if (!ensureVersion(error)) {
        return false;
    }

    const QString suffix = QStringLiteral("/containers/%1/logs?follow=%2&stdout=true&stderr=true"
                                          "&tail=%3&timestamps=true")
                               .arg(encodeId(id))
                               .arg(follow ? QStringLiteral("true") : QStringLiteral("false"))
                               .arg(tail);
    QString streamError;

    QByteArray demuxBuffer;
    bool multipart = true;
    bool detected = false;

    const auto onMultiplexedData = [&](const QByteArray &data) {
        demuxBuffer.append(data);

        if (!detected && demuxBuffer.size() >= 8) {
            const auto streamType = static_cast<quint8>(demuxBuffer.at(0));
            const bool zeroPadding = demuxBuffer.at(1) == 0 && demuxBuffer.at(2) == 0
                                     && demuxBuffer.at(3) == 0;
            multipart = streamType <= 2 && zeroPadding;
            detected = true;
        }

        if (detected && !multipart) {
            const QByteArray raw = demuxBuffer;
            demuxBuffer.clear();
            return onData(raw);
        }

        while (multipart && demuxBuffer.size() >= 8) {
            const quint32 length = qFromBigEndian<quint32>(
                reinterpret_cast<const uchar *>(demuxBuffer.constData() + 4));
            if (length > 16 * 1024 * 1024) {
                const QByteArray raw = demuxBuffer;
                demuxBuffer.clear();
                return onData(raw);
            }
            if (demuxBuffer.size() < static_cast<int>(8 + length)) {
                break;
            }
            const QByteArray payload = demuxBuffer.mid(8, static_cast<int>(length));
            demuxBuffer.remove(0, static_cast<int>(8 + length));
            if (!onData(payload)) {
                return false;
            }
        }
        return true;
    };

    const bool ok = m_http.stream(path(suffix), onMultiplexedData, cancel, &streamError);
    if (!demuxBuffer.isEmpty()) {
        onData(demuxBuffer);
    }
    if (!ok && error) {
        *error = Error::make(QStringLiteral("PODSTACK_ENGINE_ERROR"),
                             QStringLiteral("Falha ao ler os logs do container."), streamError);
    }
    return ok;
}

bool PodmanClient::streamEvents(const std::function<bool(const QByteArray &)> &onData,
                                std::atomic_bool *cancel, Error *error)
{
    if (error) {
        *error = {};
    }
    if (!ensureVersion(error)) {
        return false;
    }

    QString streamError;
    const bool ok = m_http.stream(path(QStringLiteral("/events?stream=true")), onData, cancel,
                                  &streamError);
    if (!ok && error) {
        *error = Error::make(QStringLiteral("PODSTACK_ENGINE_ERROR"),
                             QStringLiteral("Falha ao ler os eventos do Podman."), streamError);
    }
    return ok;
}

} // namespace PodStack
