// SPDX-License-Identifier: GPL-3.0-or-later

#include "machines/MachineFiles.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QJsonObject>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QStringList>

namespace PodStack {
namespace {

constexpr int kListTimeoutMs = 15000;
constexpr int kTransferTimeoutMs = 600000;
constexpr qint64 kMaxListBytes = 2 * 1024 * 1024;
constexpr int kMaxErrorDetail = 4096;

const QRegularExpression &listingPattern()
{
    static const QRegularExpression pattern(
        QStringLiteral(R"(^([bcdlps-])([rwxStTs-]{9})[.+@]?\s+\d+\s+\S+\s+\S+\s+(\d+)\s+(\d{4}-\d{2}-\d{2})\s+(\d{2}:\d{2})\s+(.+)$)"));
    return pattern;
}

bool fails(Error *error, const Error &value)
{
    if (error) {
        *error = value;
    }
    return false;
}

Error invalidParams(const QString &message)
{
    return Error::make(QStringLiteral("PODSTACK_INVALID_PARAMS"), message);
}

QString resolveBinary(const char *overrideVariable, const QString &name)
{
    const QString override = qEnvironmentVariable(overrideVariable).trimmed();
    if (!override.isEmpty()) {
        const QString resolved = QStandardPaths::findExecutable(override);
        if (!resolved.isEmpty()) {
            return resolved;
        }
        const QFileInfo info(override);
        if (info.isFile() && info.isExecutable()) {
            return info.absoluteFilePath();
        }
        return {};
    }
    return QStandardPaths::findExecutable(name);
}

bool containsControlCharacter(const QString &value)
{
    for (const QChar character : value) {
        if (character == QChar::Null || character.unicode() < 0x20
            || character.unicode() == 0x7f) {
            return true;
        }
    }
    return false;
}

bool hasParentComponent(const QString &path)
{
    const QStringList parts = path.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    return parts.contains(QStringLiteral(".."));
}

bool validateSshInfo(const SshInfo &ssh, Error *error)
{
    if (ssh.host.isEmpty() || ssh.user.isEmpty() || ssh.identityFile.isEmpty()
        || ssh.port <= 0 || ssh.port > 65535) {
        return fails(error, invalidParams(QStringLiteral("O destino SSH da machine é inválido.")));
    }
    return true;
}

bool validateRemotePath(const QString &path, Error *error)
{
    if (path.isEmpty()) {
        return fails(error, invalidParams(QStringLiteral("Informe o caminho remoto.")));
    }
    if (containsControlCharacter(path)) {
        return fails(
            error,
            invalidParams(QStringLiteral("O caminho remoto contém caracteres inválidos.")));
    }
    return true;
}

bool validateLocalDestination(const QString &path, Error *error)
{
    if (path.isEmpty()) {
        return fails(error, invalidParams(QStringLiteral("Informe o caminho local de destino.")));
    }
    if (!QDir::isAbsolutePath(path)) {
        return fails(error,
                     invalidParams(QStringLiteral("O destino local precisa ser um caminho "
                                                  "absoluto.")));
    }
    if (containsControlCharacter(path) || hasParentComponent(path)) {
        return fails(error,
                     invalidParams(QStringLiteral("O destino local contém um caminho inválido.")));
    }
    if (path.endsWith(QLatin1Char('/')) || QFileInfo(path).fileName().isEmpty()) {
        return fails(error,
                     invalidParams(QStringLiteral("Informe um arquivo de destino local.")));
    }
    return true;
}

bool validateLocalSource(const QString &path, Error *error)
{
    if (path.isEmpty()) {
        return fails(error, invalidParams(QStringLiteral("Informe o caminho local de origem.")));
    }
    if (!QDir::isAbsolutePath(path)) {
        return fails(error,
                     invalidParams(QStringLiteral("A origem local precisa ser um caminho "
                                                  "absoluto.")));
    }
    if (containsControlCharacter(path) || hasParentComponent(path)) {
        return fails(error,
                     invalidParams(QStringLiteral("A origem local contém um caminho inválido.")));
    }
    const QFileInfo info(path);
    if (!info.exists()) {
        return fails(error,
                     Error::make(QStringLiteral("PODSTACK_IO_ERROR"),
                                 QStringLiteral("O arquivo local de origem não existe."),
                                 path));
    }
    return true;
}

/// Escapa o caminho para o shell remoto: `it's` vira `'it'\''s'`.
QString shellQuote(const QString &value)
{
    QString quoted = value;
    quoted.replace(QLatin1Char('\''), QStringLiteral("'\\''"));
    return QLatin1Char('\'') + quoted + QLatin1Char('\'');
}

QString remoteSpec(const SshInfo &ssh, const QString &path)
{
    return QStringLiteral("%1@%2:%3").arg(ssh.user, ssh.host, path);
}

QString processDetail(const QByteArray &output)
{
    QString detail = QString::fromUtf8(output).trimmed();
    if (detail.size() > kMaxErrorDetail) {
        detail = detail.left(kMaxErrorDetail);
    }
    return detail;
}

QStringList sshOptions(const QString &identityFile)
{
    return {
        QStringLiteral("-o"),
        QStringLiteral("IdentitiesOnly=yes"),
        QStringLiteral("-o"),
        QStringLiteral("StrictHostKeyChecking=accept-new"),
        // known_hosts próprio da machine: a porta SSH pode ser reutilizada por
        // outra VM e a chave no known_hosts global faria o SSH recusar.
        QStringLiteral("-o"),
        QStringLiteral("UserKnownHostsFile=%1/known_hosts")
            .arg(QFileInfo(identityFile).absolutePath()),
        QStringLiteral("-o"),
        QStringLiteral("BatchMode=yes"),
        QStringLiteral("-o"),
        QStringLiteral("ConnectTimeout=5"),
    };
}

bool runProcess(const QString &binary, const QStringList &arguments, int timeoutMs,
                const QString &program, qint64 maxOutputBytes, QByteArray *output,
                Error *error)
{
    QProcess process;
    process.setProcessChannelMode(QProcess::SeparateChannels);

    QElapsedTimer timer;
    timer.start();
    process.start(binary, arguments);
    const int startTimeout = timeoutMs - static_cast<int>(timer.elapsed());
    if (startTimeout <= 0 || !process.waitForStarted(startTimeout)) {
        fails(error,
              Error::make(QStringLiteral("PODSTACK_IO_ERROR"),
                          QStringLiteral("Não foi possível executar o %1.").arg(program),
                          process.errorString(),
                          QStringLiteral("Verifique o OpenSSH e as permissões da chave.")));
        return false;
    }

    const int finishTimeout = timeoutMs - static_cast<int>(timer.elapsed());
    if (finishTimeout <= 0 || !process.waitForFinished(finishTimeout)) {
        process.kill();
        process.waitForFinished(1000);
        fails(error,
              Error::make(QStringLiteral("PODSTACK_TIMEOUT"),
                          QStringLiteral("A operação de arquivos da machine excedeu o tempo "
                                         "limite."),
                          program,
                          QStringLiteral("Verifique a rede/SSH da machine e tente novamente.")));
        return false;
    }

    const QByteArray stdoutData = process.readAllStandardOutput();
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        QString detail = processDetail(process.readAllStandardError());
        if (detail.isEmpty()) {
            detail = processDetail(stdoutData);
        }
        fails(error,
              Error::make(QStringLiteral("PODSTACK_IO_ERROR"),
                          QStringLiteral("A operação de arquivos da machine falhou."), detail,
                          QStringLiteral("Confirme que a machine está em execução e que o "
                                         "SSH responde.")));
        return false;
    }

    if (output) {
        if (maxOutputBytes > 0 && stdoutData.size() > maxOutputBytes) {
            fails(error,
                  Error::make(QStringLiteral("PODSTACK_IO_ERROR"),
                              QStringLiteral("A listagem da machine é grande demais.")));
            return false;
        }
        *output = stdoutData;
    }
    return true;
}

} // namespace

bool MachineFiles::list(const SshInfo &ssh, const QString &path, QJsonArray *items,
                        Error *error)
{
    if (error) {
        *error = {};
    }
    if (!items) {
        return fails(error, invalidParams(QStringLiteral("Destino da listagem inválido.")));
    }
    *items = QJsonArray();

    if (!validateSshInfo(ssh, error) || !validateRemotePath(path, error)) {
        return false;
    }

    const QString binary = resolveBinary("PODSTACK_SSH_BINARY", QStringLiteral("ssh"));
    if (binary.isEmpty()) {
        return fails(
            error,
            Error::make(
                QStringLiteral("PODSTACK_UNSUPPORTED"),
                QStringLiteral("O cliente SSH não foi encontrado."), QStringLiteral("ssh"),
                QStringLiteral("Instale o OpenSSH client (openssh-clients no Fedora ou "
                               "openssh-client no Debian/Ubuntu).")));
    }

    QStringList arguments{
        QStringLiteral("-i"),
        ssh.identityFile,
        QStringLiteral("-p"),
        QString::number(ssh.port),
    };
    arguments += sshOptions(ssh.identityFile);
    arguments.append(QStringLiteral("%1@%2").arg(ssh.user, ssh.host));
    arguments.append(QStringLiteral("ls -la --time-style=long-iso -- %1")
                         .arg(shellQuote(path)));

    QByteArray output;
    if (!runProcess(binary, arguments, kListTimeoutMs, QStringLiteral("SSH"), kMaxListBytes,
                    &output, error)) {
        return false;
    }

    const QString listing = QString::fromUtf8(output);
    const QStringList lines = listing.split(QLatin1Char('\n'));
    const QString base = path.endsWith(QLatin1Char('/')) ? path : path + QLatin1Char('/');
    for (const QString &line : lines) {
        const QRegularExpressionMatch match = listingPattern().match(line);
        if (!match.hasMatch()) {
            continue;
        }
        const bool directory = match.captured(1) == QStringLiteral("d");
        QString name = match.captured(6);
        if (match.captured(1) == QStringLiteral("l")) {
            const int arrow = name.indexOf(QStringLiteral(" -> "));
            if (arrow >= 0) {
                name.truncate(arrow);
            }
        }
        if (name == QStringLiteral(".") || name == QStringLiteral("..")) {
            continue;
        }

        QJsonObject item;
        item.insert(QStringLiteral("name"), name);
        item.insert(QStringLiteral("path"), base + name);
        item.insert(QStringLiteral("directory"), directory);
        item.insert(QStringLiteral("size"), match.captured(3).toDouble());
        item.insert(QStringLiteral("modified"),
                    QStringLiteral("%1 %2").arg(match.captured(4), match.captured(5)));
        items->append(item);
    }
    return true;
}

bool MachineFiles::download(const SshInfo &ssh, const QString &remotePath,
                            const QString &destination, Error *error)
{
    if (error) {
        *error = {};
    }
    if (!validateSshInfo(ssh, error) || !validateRemotePath(remotePath, error)
        || !validateLocalDestination(destination, error)) {
        return false;
    }

    const QString parent = QFileInfo(destination).absolutePath();
    if (!QFileInfo(parent).isDir()) {
        return fails(error,
                     Error::make(QStringLiteral("PODSTACK_IO_ERROR"),
                                 QStringLiteral("O diretório de destino local não existe."),
                                 parent));
    }
    if (QFileInfo(destination).isDir()) {
        return fails(error,
                     invalidParams(QStringLiteral("O destino local é um diretório.")));
    }

    const QString binary = resolveBinary("PODSTACK_SCP_BINARY", QStringLiteral("scp"));
    if (binary.isEmpty()) {
        return fails(
            error,
            Error::make(
                QStringLiteral("PODSTACK_UNSUPPORTED"),
                QStringLiteral("O cliente SCP não foi encontrado."), QStringLiteral("scp"),
                QStringLiteral("Instale o OpenSSH client (openssh-clients no Fedora ou "
                               "openssh-client no Debian/Ubuntu).")));
    }

    QStringList arguments{
        QStringLiteral("-r"),
        QStringLiteral("-P"),
        QString::number(ssh.port),
        QStringLiteral("-i"),
        ssh.identityFile,
    };
    arguments += sshOptions(ssh.identityFile);
    arguments.append(remoteSpec(ssh, remotePath));
    arguments.append(destination);

    return runProcess(binary, arguments, kTransferTimeoutMs, QStringLiteral("SCP"), 0, nullptr,
                      error);
}

bool MachineFiles::upload(const SshInfo &ssh, const QString &localPath,
                          const QString &remotePath, Error *error)
{
    if (error) {
        *error = {};
    }
    if (!validateSshInfo(ssh, error) || !validateRemotePath(remotePath, error)
        || !validateLocalSource(localPath, error)) {
        return false;
    }

    const QString binary = resolveBinary("PODSTACK_SCP_BINARY", QStringLiteral("scp"));
    if (binary.isEmpty()) {
        return fails(
            error,
            Error::make(
                QStringLiteral("PODSTACK_UNSUPPORTED"),
                QStringLiteral("O cliente SCP não foi encontrado."), QStringLiteral("scp"),
                QStringLiteral("Instale o OpenSSH client (openssh-clients no Fedora ou "
                               "openssh-client no Debian/Ubuntu).")));
    }

    QStringList arguments{
        QStringLiteral("-r"),
        QStringLiteral("-P"),
        QString::number(ssh.port),
        QStringLiteral("-i"),
        ssh.identityFile,
    };
    arguments += sshOptions(ssh.identityFile);
    arguments.append(localPath);
    arguments.append(remoteSpec(ssh, remotePath));

    return runProcess(binary, arguments, kTransferTimeoutMs, QStringLiteral("SCP"), 0, nullptr,
                      error);
}

} // namespace PodStack
