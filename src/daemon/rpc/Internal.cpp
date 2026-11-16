// SPDX-License-Identifier: GPL-3.0-or-later
#include "daemon/rpc/Internal.h"
#include "daemon/rpc/handlers/Common.h"

#include "core/system/ContainerTrust.h"

namespace PodStack::DaemonInternal {

QString selectedPodmanSocket()
{
    const Config config = Config::load();
    if (config.get(QStringLiteral("engine.mode")).toString() == QStringLiteral("rootful")) {
        return QStringLiteral("/run/podman/podman.sock");
    }
    return Paths::podmanSocket();
}

QString engineMode()
{
    const Config config = Config::load();
    return config.get(QStringLiteral("engine.mode")).toString(QStringLiteral("rootless"));
}

bool portNumberFromJson(const QJsonValue &value, int *number)
{
    if (!value.isDouble()) {
        return false;
    }
    const double raw = value.toDouble();
    if (raw < 1.0 || raw > 65535.0 || raw != static_cast<double>(static_cast<int>(raw))) {
        return false;
    }
    *number = static_cast<int>(raw);
    return true;
}

QMap<QString, QString> labelsFromParams(const QJsonObject &params)
{
    QMap<QString, QString> labels;
    const QJsonObject object = params.value(QStringLiteral("labels")).toObject();
    for (auto it = object.constBegin(); it != object.constEnd(); ++it) {
        labels.insert(it.key(), it.value().toString());
    }
    return labels;
}

QStringList aliasesFromParams(const QJsonObject &params)
{
    QStringList aliases;
    const QJsonArray array = params.value(QStringLiteral("aliases")).toArray();
    for (const QJsonValue &value : array) {
        const QString alias = value.toString();
        if (!alias.isEmpty()) {
            aliases.append(alias);
        }
    }
    return aliases;
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

void applyProxyEnvironment(ContainerCreateSpec &spec)
{
    const Config config = Config::load();
    const QString proxy =
        config.get(QStringLiteral("network.proxy")).toString(QStringLiteral("auto"));
    const QStringList excludes = proxyExcludesFromConfig(config);

    Error proxyError;
    if (!Proxy::validate(proxy, excludes, &proxyError)) {
        qCWarning(lcRpc) << "Configuração de proxy inválida; não aplicada aos containers.";
        return;
    }

    const QMap<QString, QString> values = Proxy::environment(proxy, excludes);
    for (auto it = values.constBegin(); it != values.constEnd(); ++it) {
        if (!spec.env.contains(it.key())) {
            spec.env.insert(it.key(), it.value());
        }
    }
}

void applyCaTrustMount(ContainerCreateSpec &spec)
{
    if (spec.labels.value(QStringLiteral("dev.podstack.ca"))
            .compare(QStringLiteral("false"), Qt::CaseInsensitive) == 0) {
        return;
    }
    const QString cert =
        Paths::dataDir() + QStringLiteral("/caddy/storage/pki/authorities/local/root.crt");
    if (!QFileInfo::exists(cert)) {
        return;
    }
    // Fedora/RHEL e Debian/Ubuntu usam caminhos de âncora diferentes. Monta
    // nos dois para a imagem enxergar a CA independentemente da distro base;
    // a imagem ainda precisa rodar update-ca-trust/update-ca-certificates para
    // regenerar o bundle do sistema (documentado).
    const QStringList destinations = [&] {
        QStringList paths;
        for (const QString &anchor : ContainerTrust::caAnchorPaths()) {
            paths.append(anchor + QStringLiteral("/podstack-local-ca.crt"));
        }
        return paths;
    }();
    for (const QString &destination : destinations) {
        bool present = false;
        for (const MountMapping &mount : spec.mounts) {
            if (mount.destination == destination) {
                present = true;
                break;
            }
        }
        if (!present) {
            spec.mounts.append({cert, destination, true});
        }
    }
}

bool isWildcardHostIp(const QString &hostIp)
{
    return hostIp.isEmpty() || hostIp == QStringLiteral("0.0.0.0") ||
           hostIp == QStringLiteral("::");
}

PublishedPorts publishedPorts(const QJsonObject &inspect)
{
    PublishedPorts result;
    const QJsonObject bindings = inspect.value(QStringLiteral("HostConfig"))
                                     .toObject()
                                     .value(QStringLiteral("PortBindings"))
                                     .toObject();
    for (auto it = bindings.constBegin(); it != bindings.constEnd(); ++it) {
        const QStringList keyParts = it.key().split(QLatin1Char('/'));
        if (keyParts.size() != 2) {
            continue;
        }
        const QString protocol = keyParts.at(1).trimmed().toLower();
        if (protocol != QStringLiteral("tcp") && protocol != QStringLiteral("udp")) {
            result.unsupportedProtocol = true;
            continue;
        }
        for (const QJsonValue &hostValue : it.value().toArray()) {
            const QJsonObject host = hostValue.toObject();
            const int hostPort = host.value(QStringLiteral("HostPort")).toString().toInt();
            if (hostPort <= 0) {
                continue;
            }
            result.any = true;
            if (!isWildcardHostIp(host.value(QStringLiteral("HostIp")).toString())) {
                result.wildcardOnly = false;
            }
            result.byProtocol[protocol].append(QString::number(hostPort));
        }
    }
    return result;
}

qint64 consoleTailBytes(const QJsonObject &params)
{
    qint64 tail = params.value(QStringLiteral("tailBytes")).toInteger(kMachineConsoleDefaultTail);
    if (tail <= 0) {
        tail = kMachineConsoleDefaultTail;
    }
    return qBound<qint64>(1, tail, kMachineConsoleMaxBytes);
}

bool checkMachineConsoleFile(const QString &directory, bool *exists, Error *error)
{
    if (exists) {
        *exists = false;
    }

    const QString path = directory + QStringLiteral("/console.log");
    const QFileInfo info(path);
    if (info.isSymLink()) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_IO_ERROR"),
                                 QStringLiteral("O console da machine não pode ser lido."),
                                 QStringLiteral("symlink rejeitado"));
        }
        return false;
    }
    if (!info.exists()) {
        return true;
    }
    if (!info.isFile()) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_IO_ERROR"),
                                 QStringLiteral("O console da machine não é um arquivo regular."));
        }
        return false;
    }

    const QString canonicalDir = QFileInfo(directory).canonicalFilePath();
    const QString canonicalFile = info.canonicalFilePath();
    if (canonicalDir.isEmpty() || canonicalFile.isEmpty() ||
        !canonicalFile.startsWith(canonicalDir + QLatin1Char('/'))) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_IO_ERROR"),
                                 QStringLiteral("O console da machine está fora do diretório "
                                                "esperado."));
        }
        return false;
    }

    if (exists) {
        *exists = true;
    }
    return true;
}

namespace {

bool hasTraversal(const QString &path)
{
    const QStringList parts = path.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    return parts.contains(QStringLiteral(".."));
}

} // namespace

bool validateContainerName(const QString &name, QString *error)
{
    if (name.isEmpty()) {
        return true;
    }
    static const QRegularExpression pattern(QStringLiteral("^[a-zA-Z0-9][a-zA-Z0-9_.-]*$"));
    if (!pattern.match(name).hasMatch() || name.size() > 255) {
        if (error) {
            *error = QStringLiteral(
                "Nome inválido. Use letras, números, ponto, hífen ou sublinhado, começando por "
                "letra ou número (até 255 caracteres).");
        }
        return false;
    }
    return true;
}

QString suggestContainerName(const QString &name, const QStringList &taken)
{
    const QString base = name.isEmpty() ? QStringLiteral("container") : name;
    for (int suffix = 2; suffix < 1000; ++suffix) {
        const QString candidate = QStringLiteral("%1-%2").arg(base).arg(suffix);
        if (!taken.contains(candidate)) {
            return candidate;
        }
    }
    return {};
}

bool validatePortMappings(const QVector<PortMapping> &ports, QString *error)
{
    QSet<QString> seen;
    for (const PortMapping &port : ports) {
        if (port.hostPort < 1 || port.hostPort > 65535) {
            if (error) {
                *error =
                    QStringLiteral("Porta do host inválida: %1 (use 1–65535).").arg(port.hostPort);
            }
            return false;
        }
        if (port.containerPort < 1 || port.containerPort > 65535) {
            if (error) {
                *error = QStringLiteral("Porta do container inválida: %1 (use 1–65535).")
                             .arg(port.containerPort);
            }
            return false;
        }
        const QString key = QStringLiteral("%1/%2").arg(port.hostPort).arg(port.protocol);
        if (seen.contains(key)) {
            if (error) {
                *error = QStringLiteral("A porta %1/%2 está repetida.")
                             .arg(port.hostPort)
                             .arg(port.protocol);
            }
            return false;
        }
        seen.insert(key);
    }
    return true;
}

bool validateMounts(const QVector<MountMapping> &mounts, QString *error)
{
    for (const MountMapping &mount : mounts) {
        if (!mount.source.startsWith(QLatin1Char('/')) || hasTraversal(mount.source)) {
            if (error) {
                *error =
                    QStringLiteral("Origem do volume inválida: “%1”. Use um caminho absoluto, sem "
                                   "“..”.")
                        .arg(mount.source);
            }
            return false;
        }
        if (!mount.destination.startsWith(QLatin1Char('/')) || hasTraversal(mount.destination)) {
            if (error) {
                *error =
                    QStringLiteral("Destino do volume inválido: “%1”. Use um caminho absoluto, sem "
                                   "“..”.")
                        .arg(mount.destination);
            }
            return false;
        }
        if (!QFileInfo::exists(mount.source)) {
            if (error) {
                *error = QStringLiteral("O caminho “%1” não existe no host.").arg(mount.source);
            }
            return false;
        }
    }
    return true;
}

Error helperRequiredError()
{
    return Error::make(
        QStringLiteral("PODSTACK_HELPER_REQUIRED"),
        QStringLiteral("O helper privilegiado é necessário para alterar o firewall da LAN."),
        QStringLiteral("Serviço dev.podstack.Helper1 indisponível ou sem autorização."),
        QStringLiteral("Instale o podstack-helper e autorize a operação no polkit."));
}

bool readMachineConsoleTail(const QString &path, qint64 tailBytes, QByteArray *data,
                            qint64 *endOffset, Error *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_IO_ERROR"),
                                 QStringLiteral("Não foi possível ler o console da machine."));
        }
        return false;
    }

    const qint64 size = file.size();
    const qint64 start = qMax<qint64>(0, size - tailBytes);
    if (start > 0 && !file.seek(start)) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_IO_ERROR"),
                                 QStringLiteral("Não foi possível posicionar no console da "
                                                "machine."));
        }
        return false;
    }

    if (data) {
        *data = file.readAll();
    }
    if (endOffset) {
        *endOffset = file.pos();
    }
    return true;
}

bool readMachineConsoleChunk(const QString &path, qint64 offset, qint64 maxBytes, QByteArray *data,
                             qint64 *endOffset, Error *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_IO_ERROR"),
                                 QStringLiteral("Não foi possível ler o console da machine."));
        }
        return false;
    }

    const qint64 size = file.size();
    if (size < offset) {
        if (data) {
            data->clear();
        }
        if (endOffset) {
            *endOffset = size;
        }
        return true;
    }
    if (size == offset) {
        if (data) {
            data->clear();
        }
        if (endOffset) {
            *endOffset = offset;
        }
        return true;
    }
    if (!file.seek(offset)) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_IO_ERROR"),
                                 QStringLiteral("Não foi possível posicionar no console da "
                                                "machine."));
        }
        return false;
    }

    if (data) {
        *data = file.read(qMin(maxBytes, size - offset));
    }
    if (endOffset) {
        *endOffset = file.pos();
    }
    return true;
}

QJsonArray collectReportLogs(const QString &directory, HelperClient *helper)
{
    QJsonArray logs;
    QHash<QString, int> linesPerComponent;
    qint64 bytes = 0;
    constexpr int kMaxLinesPerComponent = 10000;
    constexpr qint64 kMaxTotalBytes = 5LL * 1024 * 1024;

    const auto componentFor = [](const QString &line) {
        const QJsonObject object = QJsonDocument::fromJson(line.toUtf8()).object();
        const QString category = object.value(QStringLiteral("category")).toString();
        if (category.startsWith(QStringLiteral("podstack.setup"))) {
            return QStringLiteral("setup");
        }
        if (category.startsWith(QStringLiteral("podstack.update"))) {
            return QStringLiteral("update");
        }
        if (category == QStringLiteral("qml")) {
            return QStringLiteral("qml");
        }
        const QString process = object.value(QStringLiteral("process")).toString();
        if (process.startsWith(QStringLiteral("podstack-gui"))) {
            return QStringLiteral("gui");
        }
        if (process.startsWith(QStringLiteral("podstack-cli"))) {
            return QStringLiteral("cli");
        }
        return QStringLiteral("daemon");
    };

    const auto append = [&](const QString &line, const QString &component) {
        if (line.trimmed().isEmpty() ||
            linesPerComponent.value(component) >= kMaxLinesPerComponent) {
            return;
        }
        const QString redacted = Redaction::text(line);
        const qint64 lineBytes = redacted.toUtf8().size();
        if (bytes + lineBytes > kMaxTotalBytes) {
            return;
        }
        linesPerComponent[component] += 1;
        bytes += lineBytes;
        logs.append(redacted);
    };

    const QDir logDir(directory);
    const QStringList files =
        logDir.entryList({QStringLiteral("*.jsonl")}, QDir::Files | QDir::Readable, QDir::Name);
    const QDateTime cutoff = QDateTime::currentDateTime().addDays(-7);
    for (const QString &name : files) {
        const QFileInfo info(logDir.filePath(name));
        if (info.lastModified() < cutoff) {
            continue;
        }
        QFile file(info.filePath());
        if (!file.open(QIODevice::ReadOnly)) {
            continue;
        }
        while (!file.atEnd()) {
            const QString line = QString::fromUtf8(file.readLine()).trimmed();
            append(line, componentFor(line));
        }
    }

    if (helper) {
        QVariantMap reply;
        QString helperError;
        if (helper->callMap(QStringLiteral("GetLogs"), {}, &reply, &helperError, 60000)) {
            for (const QVariant &line : reply.value(QStringLiteral("lines")).toList()) {
                append(line.toString(), QStringLiteral("helper"));
            }
        }
    }
    return logs;
}

} // namespace PodStack::DaemonInternal
