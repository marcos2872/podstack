// SPDX-License-Identifier: GPL-3.0-or-later
#include "machines/manager/Internal.h"
#include "machines/manager/Common.h"

namespace PodStack::MachineInternal {

QTimeZone utcTimeZone()
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    return QTimeZone::UTC;
#else
    return QTimeZone(Qt::UTC);
#endif
}

Error invalidParams(const QString &message, const QString &detail, const QString &remediation)
{
    return Error::make(QStringLiteral("PODSTACK_INVALID_PARAMS"), message, detail, remediation);
}

bool validMachineName(const QString &name)
{
    static const QRegularExpression pattern(
        QStringLiteral("^[a-z0-9](?:[a-z0-9-]{0,61}[a-z0-9])?$"));
    return !name.isEmpty() && name.size() <= 63 && pattern.match(name).hasMatch();
}

Error invalidMachineName(const QString &name)
{
    return invalidParams(QStringLiteral("Nome de machine inválido."), name,
                         QStringLiteral("Use 1..63 caracteres [a-z0-9-], começando e "
                                        "terminando por letra ou número."));
}

bool validSnapshotName(const QString &name)
{
    static const QRegularExpression pattern(QStringLiteral("^[A-Za-z0-9][A-Za-z0-9._-]{0,63}$"));
    return pattern.match(name).hasMatch();
}

Error invalidSnapshotName(const QString &name)
{
    return invalidParams(QStringLiteral("Nome de snapshot inválido."), name,
                         QStringLiteral("Use 1..64 caracteres [A-Za-z0-9._-], começando por "
                                        "letra ou número."));
}

bool fails(Error *error, const Error &value)
{
    if (error) {
        *error = value;
    }
    return false;
}

QString processDetail(const QByteArray &output)
{
    QString detail = QString::fromUtf8(output).trimmed();
    if (detail.size() > kMaxProcessOutput) {
        detail = detail.left(kMaxProcessOutput);
    }
    return detail;
}

QString errorText(const Error &error)
{
    if (error.detail.isEmpty()) {
        return error.message;
    }
    return QStringLiteral("%1: %2").arg(error.message, error.detail);
}

QString normalizedPortProtocol(const QString &protocol)
{
    const QString normalized = protocol.trimmed().toLower();
    return normalized.isEmpty() ? QStringLiteral("tcp") : normalized;
}

bool isSshPort(const MachinePort &port)
{
    return port.guestPort == 22 && normalizedPortProtocol(port.protocol) == QStringLiteral("tcp");
}

bool samePort(const MachinePort &left, const MachinePort &right)
{
    return left.id == right.id && left.hostPort == right.hostPort &&
           left.guestPort == right.guestPort &&
           normalizedPortProtocol(left.protocol) == normalizedPortProtocol(right.protocol) &&
           left.exposeLan == right.exposeLan && left.label == right.label;
}

QStringList proxyExcludesFromConfig(const Config &config)
{
    QStringList excludes;
    const QJsonArray array = config.get(QStringLiteral("network.proxyExclude")).toArray();
    for (const QJsonValue &value : array) {
        const QString entry = value.toString();
        if (!entry.isEmpty()) {
            excludes.append(entry);
        }
    }
    return excludes;
}

QMap<QString, QString> proxyEnvironmentFromConfig()
{
    const Config config = Config::load();
    const QString proxy =
        config.get(QStringLiteral("network.proxy")).toString(QStringLiteral("auto"));
    const QStringList excludes = proxyExcludesFromConfig(config);

    Error error;
    if (!Proxy::validate(proxy, excludes, &error)) {
        qCWarning(lcCore) << "Configuração de proxy inválida; não aplicada às machines.";
        return {};
    }
    return Proxy::environment(proxy, excludes);
}

void setOwnerOnly(const QString &path, bool directory)
{
    QFileDevice::Permissions permissions = QFileDevice::ReadOwner | QFileDevice::WriteOwner;
    if (directory) {
        permissions |= QFileDevice::ExeOwner;
    }
    QFile::setPermissions(path, permissions);
}

void cleanupDirectory(const QString &directory)
{
    if (!directory.isEmpty()) {
        QDir(directory).removeRecursively();
    }
}

void cleanupRuntimeFiles(const QString &directory, const MachineMeta &meta)
{
    QFile::remove(meta.diskPath);
    if (!meta.seedPath.isEmpty()) {
        QFile::remove(meta.seedPath);
    }
    if (!directory.isEmpty()) {
        QFile::remove(directory + QStringLiteral("/OVMF_VARS.fd"));
    }
}

/// Um órfão pertence a esta instalação quando o cmdline referencia um caminho
/// dentro do machinesRoot (disco, seed, sockets). Isso permite limpar até um
/// processo cujo diretório já foi apagado, sem tocar em outra instalação.
bool processReferencesPath(qint64 pid, const QString &root)
{
    if (pid <= 0 || root.isEmpty()) {
        return false;
    }

    QFile cmdline(QStringLiteral("/proc/%1/cmdline").arg(pid));
    if (!cmdline.open(QIODevice::ReadOnly)) {
        return false;
    }
    const QByteArray payload = cmdline.read(1024LL * 1024 + 1);
    if (payload.isEmpty() || payload.size() > 1024LL * 1024) {
        return false;
    }

    const QByteArray prefix = QFile::encodeName(root + QLatin1Char('/'));
    const QList<QByteArray> tokens = payload.split('\0');
    for (const QByteArray &token : tokens) {
        if (token.contains(prefix)) {
            return true;
        }
    }
    return false;
}

} // namespace PodStack::MachineInternal
