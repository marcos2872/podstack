// SPDX-License-Identifier: GPL-3.0-or-later

#include "daemon/UpdateChecker.h"

#include "core/Config.h"
#include "core/UpdateManifest.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>

#include <curl/curl.h>

#include <memory>
#include <mutex>

namespace PodStack {
namespace {

constexpr int kMaxFeedBytes = 512 * 1024;
constexpr int kFetchTimeoutMs = 8000;
constexpr auto kDefaultFeedUrl =
    "https://github.com/marcos2872/podstack/releases/latest/download/podstack-update.json";

void ensureCurlInit()
{
    static std::once_flag flag;
    std::call_once(flag, [] { curl_global_init(CURL_GLOBAL_DEFAULT); });
}

struct WriteContext {
    QByteArray *body = nullptr;
    int maxBytes = 0;
    bool overflow = false;
};

size_t writeCallback(char *ptr, size_t size, size_t nmemb, void *userdata)
{
    const size_t bytes = size * nmemb;
    auto *context = static_cast<WriteContext *>(userdata);
    if (!context->body) {
        return 0;
    }
    if (context->body->size() + static_cast<int>(bytes) > context->maxBytes) {
        context->overflow = true;
        return 0;
    }
    context->body->append(ptr, static_cast<int>(bytes));
    return bytes;
}

QJsonObject baseResult(const QString &status, const QString &channel, const QString &currentVersion)
{
    QJsonObject result;
    result.insert(QStringLiteral("status"), status);
    result.insert(QStringLiteral("checkedAt"),
                  QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    result.insert(QStringLiteral("channel"), channel);
    result.insert(QStringLiteral("currentVersion"), currentVersion);
    result.insert(QStringLiteral("latestVersion"), QString());
    result.insert(QStringLiteral("updateAvailable"), false);
    result.insert(QStringLiteral("releaseUrl"), QString());
    result.insert(QStringLiteral("notes"), QString());
    result.insert(QStringLiteral("artifacts"), QJsonArray());
    result.insert(QStringLiteral("cached"), false);
    result.insert(QStringLiteral("reason"), QString());
    return result;
}

QJsonArray artifactsToJson(const QVector<UpdateArtifact> &artifacts)
{
    QJsonArray array;
    for (const UpdateArtifact &artifact : artifacts) {
        QJsonObject object;
        object.insert(QStringLiteral("arch"), artifact.arch);
        object.insert(QStringLiteral("format"), artifact.format);
        object.insert(QStringLiteral("url"), artifact.url);
        object.insert(QStringLiteral("sha256"), artifact.sha256);
        array.append(object);
    }
    return array;
}

} // namespace

UpdateChecker::FetchResult UpdateChecker::fetch(const QString &url, int timeoutMs, int maxBytes)
{
    FetchResult result;
    ensureCurlInit();

    CURL *curl = curl_easy_init();
    if (!curl) {
        result.error = QStringLiteral("Falha ao inicializar o libcurl.");
        return result;
    }

    WriteContext context;
    context.body = &result.body;
    context.maxBytes = maxBytes;

    const QByteArray urlBytes = url.toUtf8();
    const QByteArray userAgent =
        QByteArrayLiteral("podstack/") + QByteArrayLiteral(PODSTACK_VERSION);

    curl_easy_setopt(curl, CURLOPT_URL, urlBytes.constData());
    curl_easy_setopt(curl, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_1_1);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, static_cast<long>(timeoutMs));
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 5L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &context);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, userAgent.constData());

    const CURLcode code = curl_easy_perform(curl);
    if (code != CURLE_OK) {
        result.error = context.overflow
                           ? QStringLiteral("Resposta do feed maior que o limite permitido.")
                           : QString::fromUtf8(curl_easy_strerror(code));
    } else {
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &result.status);
    }
    curl_easy_cleanup(curl);
    return result;
}

QJsonObject UpdateChecker::check(const QString &requestedChannel, bool force, Error *error)
{
    const Config config = Config::load();
    const QString mode = config.get(QStringLiteral("updates.mode")).toString();
    const QString channel = requestedChannel.isEmpty()
                                ? config.get(QStringLiteral("updates.channel")).toString()
                                : requestedChannel;
    const QString currentVersion = QStringLiteral(PODSTACK_VERSION);

    if (channel != QStringLiteral("stable") && channel != QStringLiteral("beta")) {
        if (error) {
            *error = Error::make(
                QStringLiteral("PODSTACK_VALIDATION"), QStringLiteral("Canal de updates inválido."),
                QStringLiteral("Canal: %1").arg(channel), QStringLiteral("Use stable ou beta."));
        }
        return {};
    }

    QJsonObject disabled = baseResult(QStringLiteral("disabled"), channel, currentVersion);
    if (mode == QStringLiteral("off")) {
        disabled.insert(QStringLiteral("reason"),
                        QStringLiteral("Verificação de updates desativada."));
        return disabled;
    }

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!force && !m_cached.isEmpty() &&
            QDateTime::currentMSecsSinceEpoch() - m_cachedAtMs < kCacheTtlMs) {
            QJsonObject cached = m_cached;
            cached.insert(QStringLiteral("cached"), true);
            return cached;
        }
    }

    QString feedUrl = config.get(QStringLiteral("updates.feedUrl")).toString();
    if (feedUrl.isEmpty()) {
        feedUrl = QString::fromLatin1(kDefaultFeedUrl);
    }
    const QString publicKey = config.get(QStringLiteral("updates.publicKey")).toString();
    if (publicKey.isEmpty()) {
        QJsonObject unconfigured =
            baseResult(QStringLiteral("unconfigured"), channel, currentVersion);
        unconfigured.insert(QStringLiteral("reason"),
                            QStringLiteral("Chave pública de updates não configurada."));
        return unconfigured;
    }

    const FetchResult feed = fetch(feedUrl, kFetchTimeoutMs, kMaxFeedBytes);
    if (feed.status == 404 || feed.status == 410) {
        QJsonObject noFeed = baseResult(QStringLiteral("no-feed"), channel, currentVersion);
        noFeed.insert(QStringLiteral("reason"),
                      QStringLiteral("Nenhum feed de updates publicado."));
        return noFeed;
    }
    if (!feed.error.isEmpty() || feed.status != 200) {
        QJsonObject unavailable =
            baseResult(QStringLiteral("unavailable"), channel, currentVersion);
        unavailable.insert(QStringLiteral("reason"),
                           feed.error.isEmpty()
                               ? QStringLiteral("Feed respondeu HTTP %1.").arg(feed.status)
                               : feed.error);
        return unavailable;
    }

    const FetchResult signature =
        fetch(feedUrl + QStringLiteral(".minisig"), kFetchTimeoutMs, kMaxFeedBytes);
    if (signature.status != 200 || !signature.error.isEmpty()) {
        if (error) {
            *error = Error::make(
                QStringLiteral("PODSTACK_UPDATE_SIGNATURE"),
                QStringLiteral("Não foi possível obter a assinatura do manifesto de updates."),
                signature.error,
                QStringLiteral("Publique o manifesto assinado (.minisig) junto da release."));
        }
        return {};
    }

    QString signatureError;
    if (!verifyUpdateSignature(feed.body, signature.body, publicKey, &signatureError)) {
        if (error) {
            *error = Error::make(
                QStringLiteral("PODSTACK_UPDATE_SIGNATURE"),
                QStringLiteral("A assinatura do manifesto de updates é inválida."), signatureError,
                QStringLiteral("Rejeite o feed e verifique a chave pública configurada."));
        }
        return {};
    }

    UpdateManifest manifest;
    QString manifestError;
    if (!parseUpdateManifest(feed.body, &manifest, &manifestError)) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_UPDATE_MANIFEST"),
                                 QStringLiteral("Manifesto de updates inválido."), manifestError,
                                 QStringLiteral("Corrija o manifesto publicado na release."));
        }
        return {};
    }

    if (!manifest.channels.contains(channel)) {
        QJsonObject noFeed = baseResult(QStringLiteral("no-feed"), channel, currentVersion);
        noFeed.insert(QStringLiteral("reason"),
                      QStringLiteral("O manifesto não publica o canal %1.").arg(channel));
        return noFeed;
    }

    const UpdateRelease release = manifest.channels.value(channel);
    QJsonObject result = baseResult(QStringLiteral("ok"), channel, currentVersion);
    result.insert(QStringLiteral("latestVersion"), release.version);
    result.insert(QStringLiteral("updateAvailable"),
                  compareVersions(currentVersion, release.version) < 0);
    result.insert(QStringLiteral("releaseUrl"), release.releaseUrl);
    result.insert(QStringLiteral("notes"), release.notes);
    result.insert(QStringLiteral("artifacts"), artifactsToJson(release.artifacts));
    result.insert(QStringLiteral("publishedAt"), release.publishedAt);

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_cached = result;
        m_cachedAtMs = QDateTime::currentMSecsSinceEpoch();
    }
    return result;
}

void UpdateChecker::clearCache()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_cached = {};
    m_cachedAtMs = 0;
}

} // namespace PodStack
