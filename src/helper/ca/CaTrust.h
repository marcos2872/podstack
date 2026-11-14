// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QByteArray>
#include <QString>

namespace PodStack {

/// Instalação da CA local do PodStack no trust store do host.
///
/// O helper grava o certificado em `.../ca-trust/source/anchors` (Fedora) ou
/// `.../ca-certificates` (Debian) e roda `update-ca-trust`/`update-ca-certificates`.
/// O certificado precisa ser um CA autoassinado e o fingerprint SHA-256 precisa
/// bater com o esperado; nada é confiado sem essa verificação.
namespace CaTrust {

inline constexpr auto kAnchorName = "podstack-local-ca.crt";

/// Remove `:`/espaços e baixa a caixa do fingerprint.
QString normalizeFingerprint(const QString &fingerprint);

/// Valida o formato de um SHA-256 em hexadecimal (64 dígitos).
bool validateFingerprint(const QString &fingerprint, QString *error = nullptr);

/// Fingerprint SHA-256 (hex minúsculo) de um certificado PEM.
QString fingerprintOf(const QByteArray &pem);

/// `true` quando o PEM contém exatamente um certificado autoassinado.
bool isSelfSigned(const QByteArray &pem, QString *error = nullptr);

/// Copia o PEM para `<anchorsDir>/podstack-local-ca.crt` (0644).
bool trust(const QString &anchorsDir, const QByteArray &pem, const QString &fingerprint,
           QString *error = nullptr);

/// Remove o anchor somente se o fingerprint bater. Idempotente.
bool untrust(const QString &anchorsDir, const QString &fingerprint, QString *error = nullptr);

} // namespace CaTrust

} // namespace PodStack
