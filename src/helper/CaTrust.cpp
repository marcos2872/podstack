// SPDX-License-Identifier: GPL-3.0-or-later

#include "helper/CaTrust.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSslCertificate>

namespace PodStack {
namespace CaTrust {

QString normalizeFingerprint(const QString &fingerprint)
{
    QString normalized = fingerprint.trimmed().toLower();
    normalized.remove(QLatin1Char(':'));
    normalized.remove(QLatin1Char(' '));
    return normalized;
}

bool validateFingerprint(const QString &fingerprint, QString *error)
{
    static const QRegularExpression pattern(QStringLiteral("^[0-9a-f]{64}$"));
    if (!pattern.match(normalizeFingerprint(fingerprint)).hasMatch()) {
        if (error) {
            *error = QStringLiteral("Fingerprint SHA-256 inválido.");
        }
        return false;
    }
    if (error) {
        error->clear();
    }
    return true;
}

QString fingerprintOf(const QByteArray &pem)
{
    const QList<QSslCertificate> certificates = QSslCertificate::fromData(pem, QSsl::Pem);
    if (certificates.size() != 1) {
        return {};
    }
    return QString::fromLatin1(certificates.first().digest(QCryptographicHash::Sha256).toHex());
}

bool isSelfSigned(const QByteArray &pem, QString *error)
{
    const QList<QSslCertificate> certificates = QSslCertificate::fromData(pem, QSsl::Pem);
    if (certificates.size() != 1 || !certificates.first().isSelfSigned()) {
        if (error) {
            *error = QStringLiteral("O certificado não é um CA autoassinado.");
        }
        return false;
    }
    if (error) {
        error->clear();
    }
    return true;
}

bool trust(const QString &anchorsDir, const QByteArray &pem, const QString &fingerprint,
           QString *error)
{
    if (!validateFingerprint(fingerprint, error) || !isSelfSigned(pem, error)) {
        return false;
    }
    if (fingerprintOf(pem) != normalizeFingerprint(fingerprint)) {
        if (error) {
            *error = QStringLiteral("O fingerprint não corresponde ao certificado.");
        }
        return false;
    }

    QDir dir(anchorsDir);
    if (!dir.exists() && !QDir().mkpath(anchorsDir)) {
        if (error) {
            *error = QStringLiteral("Não foi possível criar %1.").arg(anchorsDir);
        }
        return false;
    }

    QSaveFile file(dir.filePath(QString::fromLatin1(kAnchorName)));
    if (!file.open(QIODevice::WriteOnly) || file.write(pem) != pem.size()) {
        if (error) {
            *error = QStringLiteral("Não foi possível gravar o CA em %1.").arg(anchorsDir);
        }
        file.cancelWriting();
        return false;
    }
    file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ReadGroup |
                        QFileDevice::ReadOther);
    if (!file.commit()) {
        if (error) {
            *error = QStringLiteral("Não foi possível concluir o CA em %1.").arg(anchorsDir);
        }
        return false;
    }
    if (error) {
        error->clear();
    }
    return true;
}

bool untrust(const QString &anchorsDir, const QString &fingerprint, QString *error)
{
    if (!validateFingerprint(fingerprint, error)) {
        return false;
    }
    const QString path = QDir(anchorsDir).filePath(QString::fromLatin1(kAnchorName));
    if (!QFileInfo::exists(path)) {
        if (error) {
            error->clear();
        }
        return true;
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) {
            *error = QStringLiteral("Não foi possível ler %1.").arg(path);
        }
        return false;
    }
    const QByteArray pem = file.readAll();
    file.close();
    if (fingerprintOf(pem) != normalizeFingerprint(fingerprint)) {
        if (error) {
            *error = QStringLiteral("O anchor existente não corresponde a este fingerprint.");
        }
        return false;
    }
    if (!QFile::remove(path)) {
        if (error) {
            *error = QStringLiteral("Não foi possível remover %1.").arg(path);
        }
        return false;
    }
    if (error) {
        error->clear();
    }
    return true;
}

} // namespace CaTrust
} // namespace PodStack
