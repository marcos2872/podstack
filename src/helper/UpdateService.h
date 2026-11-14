// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "core/Error.h"

#include <QByteArray>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

#include <sys/types.h>

namespace PodStack {

/// Operações root do update assistido. Separado do HelperService para ser
/// testável com raízes temporárias e ferramentas falsas.
class UpdateService {
public:
    struct Options {
        QString stagingRoot = QStringLiteral("/run/podstack/update");
        QString stateRoot = QStringLiteral("/run/podstack/update");
        QString admissionLock = QStringLiteral("/run/podstack/update.admission.lock");
        QString updateLock = QStringLiteral("/run/podstack/update.lock");
        QString releaseKeyPath = QStringLiteral("/etc/podstack/update.pub");
        QString embeddedKey;
        QString systemdRun = QStringLiteral("systemd-run");
        QString systemctl = QStringLiteral("systemctl");
        QString packageTool; // vazio = autodetectar dpkg-deb/rpm
        QString wrapperPath = QStringLiteral("/usr/libexec/podstack/podstack-update-exec");
        QString logRoot = QStringLiteral("/var/log/podstack/jobs");
        int installTimeoutSeconds = 1800;
        qint64 maxArtifactBytes = 300LL * 1024 * 1024;
        bool includeJournal = true;
    };

    UpdateService();
    explicit UpdateService(Options options);

    bool stage(uid_t uid, const QByteArray &manifest, const QByteArray &signature,
               const QString &artifactPath, const QString &channel, QVariantMap *result,
               Error *error);
    bool install(uid_t uid, const QString &jobId, QVariantMap *result, Error *error);
    bool status(uid_t uid, const QString &jobId, QVariantMap *result, Error *error);
    bool logs(uid_t uid, const QString &jobId, QVariantList *lines, bool *truncated,
              Error *error) const;

    const Options &options() const { return m_options; }

private:
    bool stageArtifact(uid_t uid, const QByteArray &manifest, const QByteArray &signature,
                       const QString &artifactPath, const QString &channel, QVariantMap *result,
                       Error *error);
    bool readReleaseKey(QString *key, Error *error) const;
    bool writeState(const QString &stateDir, const QVariantMap &state, Error *error) const;
    QVariantMap readState(const QString &stateDir, Error *error) const;
    QString statePath(const QString &jobId) const;
    bool validatePackage(const QString &artifactPath, const QString &expectedVersion,
                         Error *error) const;
    bool activeUpdateUnit(QString *unit, Error *error) const;
    QString packageManagerFor(const QString &artifactPath) const;

    Options m_options;
};

} // namespace PodStack
