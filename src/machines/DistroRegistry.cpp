// SPDX-License-Identifier: GPL-3.0-or-later

#include "machines/DistroRegistry.h"

#include <QSysInfo>

#include <utility>

namespace PodStack {
namespace {

DistroImage makeImage(QString distro, QString version, QString url, QString checksum,
                      QCryptographicHash::Algorithm algorithm, qint64 size)
{
    DistroImage image;
    image.distro = std::move(distro);
    image.version = std::move(version);
    image.arch = QStringLiteral("amd64");
    image.url = std::move(url);
    image.checksum = std::move(checksum);
    image.algorithm = algorithm;
    image.size = size;
    image.isDefault = true;
    return image;
}

QVector<DistroImage> registry()
{
    return {
        makeImage(QStringLiteral("ubuntu"), QStringLiteral("24.04"),
                  QStringLiteral("https://cloud-images.ubuntu.com/releases/24.04/release/"
                                 "ubuntu-24.04-server-cloudimg-amd64.img"),
                  QStringLiteral("04e8f2461a16a44e78a370284471f29616cdd9baf1893df25807b71f985b6c"
                                 "94"),
                  QCryptographicHash::Sha256, 0),
        makeImage(QStringLiteral("debian"), QStringLiteral("12"),
                  QStringLiteral("https://cloud.debian.org/images/cloud/bookworm/latest/"
                                 "debian-12-genericcloud-amd64.qcow2"),
                  QStringLiteral("0464b5d4ae3aa4f658468d88da83af7857b71548b30d46dcb6282770955a747"
                                 "fb95a983476db90d25afd0653af607224"
                                 "c7ebd05122ab71faa106fd712aaad652"),
                  QCryptographicHash::Sha512, 0),
        makeImage(QStringLiteral("fedora"), QStringLiteral("44"),
                  QStringLiteral("https://download.fedoraproject.org/pub/fedora/linux/releases/"
                                 "44/Cloud/x86_64/images/Fedora-Cloud-Base-Generic-44-1.7."
                                 "x86_64.qcow2"),
                  QStringLiteral("28680fe5b371a5a82ebf43a31926e086a168e59949d03969c5093e7071f90b7"
                                 "f"),
                  QCryptographicHash::Sha256, 583729152),
    };
}

} // namespace

QVector<DistroImage> DistroRegistry::images()
{
    return registry();
}

QStringList DistroRegistry::distros()
{
    QStringList result;
    const QVector<DistroImage> all = registry();
    for (const DistroImage &image : all) {
        if (!result.contains(image.distro)) {
            result.append(image.distro);
        }
    }
    return result;
}

DistroImage DistroRegistry::resolve(const QString &spec, const QString &arch, Error *error)
{
    if (error) {
        *error = {};
    }

    const QString normalized = spec.trimmed().toLower();
    if (normalized.isEmpty()) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_INVALID_PARAMS"),
                                 QStringLiteral("Informe a distro (ex.: ubuntu:24.04)."));
        }
        return {};
    }

    const QStringList parts = normalized.split(QLatin1Char(':'));
    if (parts.size() > 2 || parts.first().isEmpty() || parts.last().isEmpty()) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_INVALID_PARAMS"),
                                 QStringLiteral("Especificação de distro inválida: %1").arg(spec));
        }
        return {};
    }

    const QString distro = parts.first();
    const QString requestedVersion = parts.size() == 2 ? parts.at(1) : QString();
    const QString requestedArch = arch.trimmed().toLower();

    QString resolvedArch = requestedArch;
    if (resolvedArch.isEmpty()) {
        resolvedArch = hostArch();
    } else if (resolvedArch == QStringLiteral("x86_64")
               || resolvedArch == QStringLiteral("x64")) {
        resolvedArch = QStringLiteral("amd64");
    } else if (resolvedArch == QStringLiteral("aarch64")) {
        resolvedArch = QStringLiteral("arm64");
    }

    if (resolvedArch != QStringLiteral("amd64")) {
        if (error) {
            *error = Error::make(
                QStringLiteral("PODSTACK_UNSUPPORTED"),
                QStringLiteral("Não há imagem cloud pinada para a arquitetura %1.")
                    .arg(resolvedArch),
                QStringLiteral("Imagens pinadas disponíveis: amd64 (ubuntu 24.04, debian 12, "
                               "fedora 44)."));
        }
        return {};
    }

    const QVector<DistroImage> all = registry();
    for (const DistroImage &image : all) {
        if (image.distro != distro) {
            continue;
        }
        if (requestedVersion.isEmpty() ? image.isDefault : image.version == requestedVersion) {
            return image;
        }
    }

    const bool knownDistro = distros().contains(distro);
    if (!knownDistro) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_INVALID_PARAMS"),
                                 QStringLiteral("Distro não suportada: %1").arg(distro),
                                 QStringLiteral("Distros disponíveis: %1")
                                     .arg(distros().join(QStringLiteral(", "))));
        }
        return {};
    }

    if (error) {
        *error = Error::make(QStringLiteral("PODSTACK_NOT_FOUND"),
                             QStringLiteral("Versão %1 não disponível para %2.")
                                 .arg(requestedVersion, distro),
                             QStringLiteral("Consulte `podstack machine distros`."));
    }
    return {};
}

QString DistroRegistry::algorithmName(QCryptographicHash::Algorithm algorithm)
{
    switch (algorithm) {
    case QCryptographicHash::Sha512:
        return QStringLiteral("sha512");
    case QCryptographicHash::Sha256:
    default:
        return QStringLiteral("sha256");
    }
}

QString DistroRegistry::hostArch()
{
    const QString arch = QSysInfo::currentCpuArchitecture().toLower();
    if (arch == QStringLiteral("x86_64") || arch == QStringLiteral("amd64")
        || arch == QStringLiteral("x64")) {
        return QStringLiteral("amd64");
    }
    if (arch == QStringLiteral("aarch64") || arch == QStringLiteral("arm64")) {
        return QStringLiteral("arm64");
    }
    return arch;
}

} // namespace PodStack
