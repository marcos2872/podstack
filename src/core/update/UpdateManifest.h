// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QByteArray>
#include <QHash>
#include <QString>
#include <QVector>

namespace PodStack {

struct UpdateArtifact {
    QString arch;
    QString format;
    QString url;
    QString sha256;
};

struct UpdateRelease {
    QString channel;
    QString version;
    QString publishedAt;
    QString releaseUrl;
    QString notes;
    QVector<UpdateArtifact> artifacts;
};

struct UpdateManifest {
    int schemaVersion = 0;
    QHash<QString, UpdateRelease> channels;
};

/// Lê o manifesto JSON de updates. Rejeita schema desconhecido ou canal sem versão.
bool parseUpdateManifest(const QByteArray &json, UpdateManifest *out, QString *error);

/// Compara versões no formato `[v]MAJOR.MINOR.PATCH[-pre][+build]`.
/// Retorna -1, 0 ou 1. Versões com pré-release são anteriores à release.
int compareVersions(const QString &a, const QString &b);

/// Decodifica uma chave pública Ed25519 em base64 cru (32 bytes) ou no
/// formato de chave pública minisign (42 bytes: "Ed" + keyId + chave).
QByteArray decodeUpdatePublicKey(const QString &text, QString *error = nullptr);

/// Verifica uma assinatura destacada no formato minisign (`Ed` ou `ED`,
/// este último com BLAKE2b-512) usando a chave pública informada.
bool verifyUpdateSignature(const QByteArray &message, const QByteArray &signatureFile,
                           const QString &publicKey, QString *error = nullptr);

} // namespace PodStack
