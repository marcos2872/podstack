// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QByteArray>
#include <QString>

#include <openssl/evp.h>

namespace UpdateTestUtils {

struct KeyPair {
    QByteArray publicKey;
    QByteArray privateKey;
};

inline KeyPair generateKeyPair()
{
    EVP_PKEY_CTX *context = EVP_PKEY_CTX_new_id(EVP_PKEY_ED25519, nullptr);
    EVP_PKEY *key = nullptr;
    if (!context || EVP_PKEY_keygen_init(context) <= 0 || EVP_PKEY_keygen(context, &key) <= 0) {
        EVP_PKEY_CTX_free(context);
        return {};
    }

    KeyPair pair;
    pair.publicKey.resize(32);
    pair.privateKey.resize(32);
    size_t length = 32;
    EVP_PKEY_get_raw_public_key(key, reinterpret_cast<unsigned char *>(pair.publicKey.data()),
                                &length);
    length = 32;
    EVP_PKEY_get_raw_private_key(key, reinterpret_cast<unsigned char *>(pair.privateKey.data()),
                                 &length);
    EVP_PKEY_free(key);
    EVP_PKEY_CTX_free(context);
    return pair;
}

inline QByteArray sign(const QByteArray &message, const QByteArray &privateKey,
                       const QByteArray &publicKey, bool prehashed)
{
    QByteArray signedData = message;
    if (prehashed) {
        unsigned char digest[EVP_MAX_MD_SIZE];
        unsigned int digestSize = 0;
        if (EVP_Digest(message.constData(), static_cast<size_t>(message.size()), digest,
                       &digestSize, EVP_blake2b512(), nullptr) != 1) {
            return {};
        }
        signedData =
            QByteArray(reinterpret_cast<const char *>(digest), static_cast<int>(digestSize));
    }

    EVP_PKEY *key = EVP_PKEY_new_raw_private_key(
        EVP_PKEY_ED25519, nullptr, reinterpret_cast<const unsigned char *>(privateKey.constData()),
        static_cast<size_t>(privateKey.size()));
    EVP_MD_CTX *context = EVP_MD_CTX_new();
    if (!key || !context || EVP_DigestSignInit(context, nullptr, nullptr, nullptr, key) != 1) {
        EVP_MD_CTX_free(context);
        EVP_PKEY_free(key);
        return {};
    }

    size_t signatureSize = 64;
    QByteArray signature(64, '\0');
    const int result =
        EVP_DigestSign(context, reinterpret_cast<unsigned char *>(signature.data()), &signatureSize,
                       reinterpret_cast<const unsigned char *>(signedData.constData()),
                       static_cast<size_t>(signedData.size()));
    EVP_MD_CTX_free(context);
    EVP_PKEY_free(key);
    if (result != 1) {
        return {};
    }
    signature.resize(static_cast<int>(signatureSize));
    Q_UNUSED(publicKey)
    return signature;
}

inline QByteArray minisignFile(const QByteArray &message, const KeyPair &key, bool prehashed)
{
    QByteArray blob("Ed");
    blob.append(QByteArray(8, '\0'));
    blob.append(sign(message, key.privateKey, key.publicKey, prehashed));
    if (prehashed) {
        blob[0] = 'E';
        blob[1] = 'D';
    }
    const QByteArray encoded = blob.toBase64();
    return QByteArrayLiteral("untrusted comment: test signature\n") + encoded + '\n' +
           QByteArrayLiteral("trusted comment: test\n");
}

inline QString publicKeyBase64(const KeyPair &key)
{
    return QString::fromLatin1(key.publicKey.toBase64());
}

inline QByteArray manifestForVersion(const QByteArray &version, const QByteArray &channel)
{
    return QByteArrayLiteral(R"({"schemaVersion":1,"channels":{")") + channel +
           QByteArrayLiteral(R"(":{"version":")") + version +
           QByteArrayLiteral(
               R"(","releaseUrl":"https://example.test/release","notes":"notas","artifacts":[]}}})");
}

} // namespace UpdateTestUtils
