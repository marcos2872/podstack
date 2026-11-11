// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/UpdateManifest.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QStringList>

#include <openssl/evp.h>

#include <algorithm>
#include <memory>

namespace PodStack {
namespace {

constexpr int kMaxManifestBytes = 512 * 1024;

struct Version {
    QVector<int> numbers;
    QStringList prerelease;
};

bool parseVersion(const QString &text, Version *out)
{
    QString value = text.trimmed();
    if (value.startsWith(QLatin1Char('v')) || value.startsWith(QLatin1Char('V'))) {
        value.remove(0, 1);
    }
    const int buildIndex = value.indexOf(QLatin1Char('+'));
    if (buildIndex >= 0) {
        value = value.left(buildIndex);
    }
    const int dashIndex = value.indexOf(QLatin1Char('-'));
    if (dashIndex >= 0) {
        out->prerelease = value.mid(dashIndex + 1).split(QLatin1Char('.'));
        value = value.left(dashIndex);
    }
    if (value.isEmpty()) {
        return false;
    }
    for (const QString &part : value.split(QLatin1Char('.'))) {
        bool ok = false;
        const int number = part.toInt(&ok);
        if (!ok || number < 0) {
            return false;
        }
        out->numbers.append(number);
    }
    return !out->numbers.isEmpty();
}

int comparePrerelease(const QStringList &a, const QStringList &b)
{
    if (a.isEmpty() && b.isEmpty()) {
        return 0;
    }
    if (a.isEmpty()) {
        return 1;
    }
    if (b.isEmpty()) {
        return -1;
    }
    const int count = std::min(a.size(), b.size());
    for (int index = 0; index < count; ++index) {
        bool aNumeric = false;
        bool bNumeric = false;
        const int aNumber = a.at(index).toInt(&aNumeric);
        const int bNumber = b.at(index).toInt(&bNumeric);
        if (aNumeric && bNumeric) {
            if (aNumber != bNumber) {
                return aNumber < bNumber ? -1 : 1;
            }
        } else if (aNumeric != bNumeric) {
            return aNumeric ? -1 : 1;
        } else {
            const int order = QString::compare(a.at(index), b.at(index), Qt::CaseSensitive);
            if (order != 0) {
                return order < 0 ? -1 : 1;
            }
        }
    }
    if (a.size() == b.size()) {
        return 0;
    }
    return a.size() < b.size() ? -1 : 1;
}

UpdateArtifact parseArtifact(const QJsonObject &object)
{
    UpdateArtifact artifact;
    artifact.arch = object.value(QStringLiteral("arch")).toString();
    artifact.format = object.value(QStringLiteral("format")).toString();
    artifact.url = object.value(QStringLiteral("url")).toString();
    artifact.sha256 = object.value(QStringLiteral("sha256")).toString();
    return artifact;
}

bool verifyEd25519(const QByteArray &message, const QByteArray &signature,
                   const QByteArray &publicKey, QString *error)
{
    if (publicKey.size() != 32) {
        if (error) {
            *error = QStringLiteral("Chave pública Ed25519 com tamanho inválido.");
        }
        return false;
    }
    if (signature.size() != 64) {
        if (error) {
            *error = QStringLiteral("Assinatura Ed25519 com tamanho inválido.");
        }
        return false;
    }

    EVP_PKEY *rawKey = EVP_PKEY_new_raw_public_key(
        EVP_PKEY_ED25519, nullptr, reinterpret_cast<const unsigned char *>(publicKey.constData()),
        static_cast<size_t>(publicKey.size()));
    if (!rawKey) {
        if (error) {
            *error = QStringLiteral("Não foi possível carregar a chave pública Ed25519.");
        }
        return false;
    }
    std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)> key(rawKey, EVP_PKEY_free);
    std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> context(EVP_MD_CTX_new(),
                                                                    EVP_MD_CTX_free);
    if (!context ||
        EVP_DigestVerifyInit(context.get(), nullptr, nullptr, nullptr, key.get()) != 1) {
        if (error) {
            *error = QStringLiteral("Não foi possível inicializar a verificação Ed25519.");
        }
        return false;
    }
    const int result = EVP_DigestVerify(
        context.get(), reinterpret_cast<const unsigned char *>(signature.constData()),
        static_cast<size_t>(signature.size()),
        reinterpret_cast<const unsigned char *>(message.constData()),
        static_cast<size_t>(message.size()));
    if (result != 1 && error) {
        *error = QStringLiteral("Assinatura Ed25519 inválida.");
    }
    return result == 1;
}

} // namespace

bool parseUpdateManifest(const QByteArray &json, UpdateManifest *out, QString *error)
{
    if (out) {
        *out = UpdateManifest();
    }
    if (json.size() > kMaxManifestBytes) {
        if (error) {
            *error = QStringLiteral("Manifesto de updates maior que 512 KiB.");
        }
        return false;
    }
    QJsonParseError parseError{};
    const QJsonDocument document = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        if (error) {
            *error =
                QStringLiteral("Manifesto de updates inválido: %1").arg(parseError.errorString());
        }
        return false;
    }
    const QJsonObject root = document.object();
    if (root.value(QStringLiteral("schemaVersion")).toInt() != 1) {
        if (error) {
            *error = QStringLiteral("Versão de schema do manifesto não suportada.");
        }
        return false;
    }
    const QJsonObject channels = root.value(QStringLiteral("channels")).toObject();
    if (channels.isEmpty()) {
        if (error) {
            *error = QStringLiteral("Manifesto de updates sem canais.");
        }
        return false;
    }

    UpdateManifest manifest;
    manifest.schemaVersion = 1;
    for (auto it = channels.begin(); it != channels.end(); ++it) {
        const QJsonObject object = it.value().toObject();
        UpdateRelease release;
        release.channel = it.key();
        release.version = object.value(QStringLiteral("version")).toString();
        if (release.version.isEmpty()) {
            if (error) {
                *error = QStringLiteral("Canal %1 sem versão.").arg(it.key());
            }
            return false;
        }
        release.publishedAt = object.value(QStringLiteral("publishedAt")).toString();
        release.releaseUrl = object.value(QStringLiteral("releaseUrl")).toString();
        release.notes = object.value(QStringLiteral("notes")).toString();
        for (const QJsonValue &artifact : object.value(QStringLiteral("artifacts")).toArray()) {
            release.artifacts.append(parseArtifact(artifact.toObject()));
        }
        manifest.channels.insert(it.key(), release);
    }

    if (out) {
        *out = manifest;
    }
    return true;
}

int compareVersions(const QString &a, const QString &b)
{
    Version left;
    Version right;
    if (!parseVersion(a, &left) || !parseVersion(b, &right)) {
        return QString::compare(a, b, Qt::CaseSensitive);
    }
    const int count = std::max(left.numbers.size(), right.numbers.size());
    for (int index = 0; index < count; ++index) {
        const int leftNumber = index < left.numbers.size() ? left.numbers.at(index) : 0;
        const int rightNumber = index < right.numbers.size() ? right.numbers.at(index) : 0;
        if (leftNumber != rightNumber) {
            return leftNumber < rightNumber ? -1 : 1;
        }
    }
    return comparePrerelease(left.prerelease, right.prerelease);
}

QByteArray decodeUpdatePublicKey(const QString &text, QString *error)
{
    QString value = text.trimmed();
    if (value.isEmpty()) {
        if (error) {
            *error = QStringLiteral("Chave pública de updates vazia.");
        }
        return {};
    }
    if (value.startsWith(QStringLiteral("untrusted comment:"))) {
        const QStringList lines = value.split(QLatin1Char('\n'));
        value = lines.size() > 1 ? lines.at(1).trimmed() : QString();
    }
    const QByteArray decoded = QByteArray::fromBase64(value.toLatin1());
    if (decoded.size() == 32) {
        return decoded;
    }
    if (decoded.size() == 42 && decoded.startsWith("Ed")) {
        return decoded.mid(10);
    }
    if (error) {
        *error = QStringLiteral("Chave pública de updates inválida (esperado Ed25519 base64).");
    }
    return {};
}

bool verifyUpdateSignature(const QByteArray &message, const QByteArray &signatureFile,
                           const QString &publicKey, QString *error)
{
    QString keyError;
    const QByteArray key = decodeUpdatePublicKey(publicKey, &keyError);
    if (key.isEmpty()) {
        if (error) {
            *error = keyError;
        }
        return false;
    }

    const QList<QByteArray> lines = signatureFile.split('\n');
    QByteArray signatureLine;
    for (const QByteArray &line : lines) {
        const QByteArray trimmed = line.trimmed();
        if (trimmed.isEmpty() || trimmed.startsWith("untrusted comment:") ||
            trimmed.startsWith("trusted comment:")) {
            continue;
        }
        signatureLine = trimmed;
        break;
    }
    if (signatureLine.isEmpty()) {
        if (error) {
            *error = QStringLiteral("Arquivo de assinatura vazio.");
        }
        return false;
    }

    const QByteArray blob = QByteArray::fromBase64(signatureLine);
    if (blob.size() != 74) {
        if (error) {
            *error = QStringLiteral("Assinatura minisign com tamanho inválido.");
        }
        return false;
    }
    const QByteArray algorithm = blob.left(2);
    const QByteArray signature = blob.mid(10);

    QByteArray signedData = message;
    if (algorithm == "ED") {
        unsigned char digest[EVP_MAX_MD_SIZE];
        unsigned int digestSize = 0;
        if (EVP_Digest(message.constData(), static_cast<size_t>(message.size()), digest,
                       &digestSize, EVP_blake2b512(), nullptr) != 1) {
            if (error) {
                *error = QStringLiteral("Não foi possível calcular o hash do manifesto.");
            }
            return false;
        }
        signedData =
            QByteArray(reinterpret_cast<const char *>(digest), static_cast<int>(digestSize));
    } else if (algorithm != "Ed") {
        if (error) {
            *error = QStringLiteral("Algoritmo de assinatura não suportado.");
        }
        return false;
    }

    return verifyEd25519(signedData, signature, key, error);
}

} // namespace PodStack
