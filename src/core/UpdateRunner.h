// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "core/Error.h"
#include "core/UpdateManifest.h"

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

namespace PodStack {

/// Abstração do helper privilegiado usada pelo runner (permite test double).
class UpdateHelper {
public:
    virtual ~UpdateHelper() = default;
    virtual bool call(const QString &method, const QVariantList &arguments, QVariantMap *reply,
                      Error *error) = 0;
};

struct UpdateRunnerOptions {
    QString cacheDir;
    QString stateDir;
    QString channel = QStringLiteral("stable");
    QString feedUrl;
    QString publicKey;
    QString systemctlBinary = QStringLiteral("systemctl");
    bool restartServices = true;
    int downloadTimeoutMs = 15 * 60 * 1000;
    int pollIntervalMs = 1000;
    int installTimeoutMs = 30 * 60 * 1000;
    qint64 maxArtifactBytes = 300LL * 1024 * 1024;
};

/// Prepara (download/assinatura/cache) e instala via helper. O download e a
/// verificação acontecem no processo do usuário; o helper revalida tudo.
class UpdateRunner {
public:
    UpdateRunner(UpdateHelper *helper, UpdateRunnerOptions options);

    bool prepare(Error *error);
    bool prepareFromManifest(const QString &manifestPath, Error *error);
    bool install(Error *error);

    QString version() const { return m_version; }
    QString channel() const { return m_channel; }
    QString jobId() const { return m_jobId; }
    QString unit() const { return m_unit; }
    QString phase() const { return m_phase; }
    QString statePath() const;
    QString artifactPath() const { return m_artifactPath; }

    static int exitCodeFor(const Error &error);

private:
    bool download(const QString &url, int maxBytes, QByteArray *body, Error *error);
    bool loadManifest(const QByteArray &manifest, const QByteArray &signature, Error *error);
    bool fetchArtifact(Error *error);
    bool writeState(const QString &phase, const QString &message, Error *error);
    bool runSystemctl(const QStringList &arguments, Error *error);
    bool pollStatus(Error *error);
    bool checkCustomKey(Error *error);
    QString hostArch() const;
    QString hostFormat() const;
    QString cachePath(const QString &name) const;
    QString statePathValue() const;

    UpdateHelper *m_helper = nullptr;
    UpdateRunnerOptions m_options;
    UpdateManifest m_manifest;
    UpdateRelease m_release;
    QByteArray m_manifestBytes;
    QByteArray m_signatureBytes;
    QString m_artifactPath;
    QString m_artifactUrl;
    QString m_expectedSha256;
    QString m_version;
    QString m_channel;
    QString m_jobId;
    QString m_unit;
    QString m_stateDir;
    QString m_phase = QStringLiteral("idle");
};

} // namespace PodStack
