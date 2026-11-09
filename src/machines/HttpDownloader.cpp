// SPDX-License-Identifier: GPL-3.0-or-later

#include "machines/HttpDownloader.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <curl/curl.h>

#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <mutex>

namespace PodStack {
namespace {

constexpr long kConnectTimeoutMs = 15000;
constexpr long kLowSpeedLimit = 1024;
constexpr long kLowSpeedTimeSeconds = 60;
constexpr long kMaxRedirects = 5;

void ensureCurlInit()
{
    static std::once_flag flag;
    std::call_once(flag, [] { curl_global_init(CURL_GLOBAL_DEFAULT); });
}

struct DownloadContext {
    QFile *file = nullptr;
    const DownloadProgress *progress = nullptr;
    const DownloadCancelled *cancelled = nullptr;
};

size_t writeCallback(char *ptr, size_t size, size_t nmemb, void *userdata)
{
    const size_t bytes = size * nmemb;
    auto *context = static_cast<DownloadContext *>(userdata);
    if (!context->file) {
        return 0;
    }
    const qint64 written = context->file->write(ptr, static_cast<qint64>(bytes));
    return written == static_cast<qint64>(bytes) ? bytes : 0;
}

int progressCallback(void *clientp, curl_off_t dltotal, curl_off_t dlnow, curl_off_t,
                     curl_off_t)
{
    auto *context = static_cast<DownloadContext *>(clientp);
    if (context->cancelled && *context->cancelled && (*context->cancelled)()) {
        return 1;
    }
    if (context->progress && *context->progress) {
        (*context->progress)(static_cast<qint64>(dlnow), static_cast<qint64>(dltotal));
    }
    return 0;
}

Error downloadError(const QString &message, const QString &detail = {})
{
    return Error::make(QStringLiteral("PODSTACK_DOWNLOAD_FAILED"), message, detail, {}, true);
}

Error cancelledError()
{
    return Error::make(QStringLiteral("PODSTACK_CANCELLED"), QStringLiteral("Download cancelado."));
}

} // namespace

bool HttpDownloader::download(const QString &url, const QString &destination,
                              const DownloadProgress &progress,
                              const DownloadCancelled &cancelled, Error *error)
{
    if (error) {
        *error = {};
    }

    if (url.trimmed().isEmpty() || destination.trimmed().isEmpty()) {
        if (error) {
            *error = downloadError(QStringLiteral("URL ou destino de download vazio."));
        }
        return false;
    }

    if (cancelled && cancelled()) {
        if (error) {
            *error = cancelledError();
        }
        return false;
    }

    const QString directory = QFileInfo(destination).absolutePath();
    if (!QDir().mkpath(directory)) {
        if (error) {
            *error = downloadError(QStringLiteral("Não foi possível criar o diretório de destino."),
                                   directory);
        }
        return false;
    }

    const QString partPath = destination + QStringLiteral(".part");
    QFile::remove(partPath);

    QFile part(partPath);
    if (!part.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (error) {
            *error = downloadError(QStringLiteral("Não foi possível abrir o arquivo temporário."),
                                   part.errorString());
        }
        return false;
    }
    QFile::setPermissions(partPath, QFileDevice::ReadOwner | QFileDevice::WriteOwner);

    ensureCurlInit();
    CURL *curl = curl_easy_init();
    if (!curl) {
        part.close();
        QFile::remove(partPath);
        if (error) {
            *error = downloadError(QStringLiteral("Falha ao inicializar o libcurl."));
        }
        return false;
    }

    DownloadContext context;
    context.file = &part;
    context.progress = &progress;
    context.cancelled = &cancelled;

    const QByteArray urlBytes = url.toUtf8();
    const QByteArray userAgent =
        QStringLiteral("podstack/%1").arg(QStringLiteral(PODSTACK_VERSION)).toUtf8();

    curl_easy_setopt(curl, CURLOPT_URL, urlBytes.constData());
    curl_easy_setopt(curl, CURLOPT_USERAGENT, userAgent.constData());
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, kMaxRedirects);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, kConnectTimeoutMs);
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, kLowSpeedLimit);
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, kLowSpeedTimeSeconds);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
    curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, "http,https");
    curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS_STR, "http,https");
    curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &context);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, progressCallback);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &context);

    const CURLcode result = curl_easy_perform(curl);
    long status = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    curl_easy_cleanup(curl);

    const bool wasCancelled = cancelled && cancelled();

    const bool flushed = part.flush();
    const int handle = part.handle();
    const bool synced = handle >= 0 && ::fsync(handle) == 0;
    part.close();

    auto cleanup = [&partPath] { QFile::remove(partPath); };

    if (wasCancelled) {
        cleanup();
        if (error) {
            *error = cancelledError();
        }
        return false;
    }

    if (result == CURLE_OK && (!flushed || !synced)) {
        cleanup();
        if (error) {
            *error = downloadError(QStringLiteral("Falha ao gravar o arquivo baixado."),
                                   part.errorString());
        }
        return false;
    }

    if (status >= 400) {
        cleanup();
        if (error) {
            *error = downloadError(QStringLiteral("O servidor respondeu HTTP %1.").arg(status),
                                   url);
        }
        return false;
    }

    if (result != CURLE_OK) {
        cleanup();
        if (error) {
            *error = downloadError(QStringLiteral("Falha no download: %1")
                                       .arg(QString::fromUtf8(curl_easy_strerror(result))),
                                   url);
        }
        return false;
    }

    const QByteArray destinationBytes = QFile::encodeName(destination);
    const QByteArray partBytes = QFile::encodeName(partPath);
    if (std::rename(partBytes.constData(), destinationBytes.constData()) != 0) {
        cleanup();
        if (error) {
            *error = downloadError(QStringLiteral("Não foi possível finalizar o download."),
                                   QString::fromLocal8Bit(std::strerror(errno)));
        }
        return false;
    }

    QFile::setPermissions(destination, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    return true;
}

} // namespace PodStack
