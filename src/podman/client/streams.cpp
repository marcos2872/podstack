// SPDX-License-Identifier: GPL-3.0-or-later
#include "podman/client/Common.h"

using namespace PodStack::PodmanInternal;

namespace PodStack {

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

bool PodmanClient::streamStats(const QString &id, int intervalSeconds,
                               const std::function<bool(const QByteArray &)> &onLine,
                               std::atomic_bool *cancel, Error *error)
{
    if (error) {
        *error = {};
    }
    if (!ensureVersion(error)) {
        return false;
    }

    const QString suffix = QStringLiteral("/containers/stats?stream=false&containers=%1")
                               .arg(encodeId(id));
    const int safeInterval = qBound(1, intervalSeconds, 60);

    while (true) {
        if (cancel && cancel->load()) {
            return true;
        }

        const HttpResponse response =
            m_http.request(QByteArrayLiteral("GET"), path(suffix), {}, {}, 20000);
        if (response.status != 200) {
            if (error) {
                *error = errorFromResponse(
                    response, QStringLiteral("Falha ao ler as estatísticas do container."));
            }
            return false;
        }

        const QJsonObject raw = QJsonDocument::fromJson(response.body).object();
        const QJsonObject formatted = formatContainerStats(raw);
        if (!formatted.isEmpty()) {
            if (!onLine(QJsonDocument(formatted).toJson(QJsonDocument::Compact))) {
                return true;
            }
        } else if (!onLine(response.body)) {
            return true;
        }

        if (!cancel) {
            return true;
        }

        for (int tick = 0; tick < safeInterval * 10; ++tick) {
            if (cancel->load()) {
                return true;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
}

bool PodmanClient::statsSnapshot(QJsonArray *containers, Error *error)
{
    if (error) {
        *error = {};
    }
    if (containers) {
        *containers = QJsonArray();
    }
    if (!ensureVersion(error)) {
        return false;
    }

    const HttpResponse response =
        m_http.request(QByteArrayLiteral("GET"),
                       path(QStringLiteral("/containers/stats?stream=false&all=true")), {}, {},
                       20000);
    if (response.status != 200) {
        if (error) {
            *error = errorFromResponse(
                response, QStringLiteral("Falha ao ler as estatísticas dos containers."));
        }
        return false;
    }

    const QJsonDocument document = QJsonDocument::fromJson(response.body);
    QJsonArray array;
    if (document.isArray()) {
        array = document.array();
    } else if (document.isObject()) {
        array = document.object().value(QStringLiteral("Stats")).toArray();
    }
    if (containers) {
        *containers = array;
    }
    return true;
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
