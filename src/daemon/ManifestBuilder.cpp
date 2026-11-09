// SPDX-License-Identifier: GPL-3.0-or-later
#include "daemon/ManifestBuilder.h"

#include <QByteArray>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>

namespace PodStack {
namespace {

enum class RunResult {
    Completed,
    Cancelled,
    TimedOut,
    StartFailed,
};

void setError(Error *error, const QString &code, const QString &message,
              const QString &detail = {}, const QString &remediation = {})
{
    if (error) {
        *error = Error::make(code, message, detail, remediation);
    }
}

QString podmanCli()
{
    const QString override = qEnvironmentVariable("PODSTACK_PODMAN_CLI").trimmed();
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
    return QStandardPaths::findExecutable(QStringLiteral("podman"));
}

RunResult runProcess(const QString &program, const QStringList &arguments,
                     const QString &workingDirectory, int timeoutMs,
                     const ManifestBuilder::Line &onLine,
                     const ManifestBuilder::Cancelled &cancelled, int *exitCode,
                     QProcess::ExitStatus *exitStatus, QString *detail)
{
    QProcess process;
    process.setWorkingDirectory(workingDirectory);
    process.setProcessChannelMode(QProcess::MergedChannels);
    process.start(program, arguments);

    if (!process.waitForStarted(30000)) {
        if (detail) {
            *detail = process.errorString();
        }
        return RunResult::StartFailed;
    }

    QElapsedTimer timer;
    timer.start();

    QByteArray buffer;
    const auto dispatch = [&](bool final) {
        buffer += process.readAllStandardOutput();
        int newline = -1;
        while ((newline = buffer.indexOf('\n')) >= 0) {
            QByteArray raw = buffer.left(newline);
            buffer.remove(0, newline + 1);
            if (raw.endsWith('\r')) {
                raw.chop(1);
            }
            if (!raw.isEmpty() && onLine) {
                onLine(QString::fromUtf8(raw));
            }
        }
        if (final && !buffer.isEmpty()) {
            QByteArray raw = buffer;
            buffer.clear();
            if (raw.endsWith('\r')) {
                raw.chop(1);
            }
            if (!raw.isEmpty() && onLine) {
                onLine(QString::fromUtf8(raw));
            }
        }
    };

    while (process.state() != QProcess::NotRunning) {
        if (cancelled && cancelled()) {
            process.kill();
            process.waitForFinished(5000);
            return RunResult::Cancelled;
        }
        if (timer.elapsed() >= timeoutMs) {
            process.kill();
            process.waitForFinished(5000);
            return RunResult::TimedOut;
        }
        if (process.waitForReadyRead(200)) {
            dispatch(false);
        }
    }
    dispatch(true);

    if (exitCode) {
        *exitCode = process.exitCode();
    }
    if (exitStatus) {
        *exitStatus = process.exitStatus();
    }
    return RunResult::Completed;
}

} // namespace

void ManifestBuilder::setTimeoutMs(int timeoutMs)
{
    m_timeoutMs = qMax(1000, timeoutMs);
}

bool ManifestBuilder::validatePlatforms(const QStringList &platforms, Error *error)
{
    if (error) {
        *error = {};
    }
    if (platforms.isEmpty()) {
        setError(error, QStringLiteral("PODSTACK_INVALID_PARAMS"),
                 QStringLiteral("Informe ao menos uma plataforma."));
        return false;
    }
    if (platforms.size() > 8) {
        setError(error, QStringLiteral("PODSTACK_INVALID_PARAMS"),
                 QStringLiteral("São permitidas no máximo 8 plataformas."),
                 QString::number(platforms.size()));
        return false;
    }

    static const QRegularExpression expression(
        QStringLiteral("^[A-Za-z0-9][A-Za-z0-9._-]*/"
                       "[A-Za-z0-9][A-Za-z0-9._-]*"
                       "(?:/[A-Za-z0-9][A-Za-z0-9._-]*)?$"));
    for (const QString &platform : platforms) {
        if (!expression.match(platform).hasMatch()) {
            setError(error, QStringLiteral("PODSTACK_INVALID_PARAMS"),
                     QStringLiteral("Plataforma inválida."), platform,
                     QStringLiteral("Use o formato os/arch[/variant], por exemplo "
                                    "linux/amd64 ou linux/arm64/v8."));
            return false;
        }
    }
    return true;
}

bool ManifestBuilder::build(const QString &contextPath, const QString &dockerfile,
                            const QString &tag, const QMap<QString, QString> &buildArgs,
                            const QStringList &platforms, const Line &onLine,
                            const Cancelled &cancelled, Error *error)
{
    if (error) {
        *error = {};
    }

    if (cancelled && cancelled()) {
        setError(error, QStringLiteral("PODSTACK_CANCELLED"),
                 QStringLiteral("Build cancelado."));
        return false;
    }

    if (!validatePlatforms(platforms, error)) {
        return false;
    }

    const QFileInfo contextInfo(contextPath);
    if (contextPath.trimmed().isEmpty() || !contextInfo.isDir()) {
        setError(error, QStringLiteral("PODSTACK_INVALID_PARAMS"),
                 QStringLiteral("Diretório de contexto inválido."), contextPath);
        return false;
    }

    const QString dockerfileName =
        dockerfile.trimmed().isEmpty() ? QStringLiteral("Dockerfile") : dockerfile.trimmed();
    const QDir contextDir(contextInfo.absoluteFilePath());
    const QFileInfo dockerfileInfo(contextDir.absoluteFilePath(dockerfileName));
    if (!dockerfileInfo.isFile()) {
        setError(error, QStringLiteral("PODSTACK_INVALID_PARAMS"),
                 QStringLiteral("Dockerfile não encontrado."), dockerfileInfo.absoluteFilePath());
        return false;
    }

    static const QRegularExpression tagExpression(
        QStringLiteral("^[A-Za-z0-9][A-Za-z0-9._:/@-]{0,254}$"));
    if (!tagExpression.match(tag).hasMatch()) {
        setError(error, QStringLiteral("PODSTACK_INVALID_PARAMS"),
                 QStringLiteral("Tag de imagem inválida."), tag);
        return false;
    }

    for (auto it = buildArgs.constBegin(); it != buildArgs.constEnd(); ++it) {
        const QString &key = it.key();
        bool validKey = !key.isEmpty() && !key.startsWith(QLatin1Char('-'));
        for (const QChar character : key) {
            if (character.isSpace() || character == QLatin1Char('=')
                || character == QChar::Null) {
                validKey = false;
                break;
            }
        }
        if (!validKey || it.value().contains(QChar::Null)) {
            setError(error, QStringLiteral("PODSTACK_INVALID_PARAMS"),
                     QStringLiteral("Argumento de build inválido."), key);
            return false;
        }
    }

    const QString cli = podmanCli();
    if (cli.isEmpty()) {
        setError(error, QStringLiteral("PODSTACK_UNSUPPORTED"),
                 QStringLiteral("Build multi-plataforma requer o CLI do Podman."),
                 QStringLiteral("PODSTACK_PODMAN_CLI vazio ou Podman não encontrado."),
                 QStringLiteral("Instale o Podman (≥ 5.8) e tente novamente."));
        return false;
    }

    // Rebuild determinístico: remove o manifest antigo (falha é ignorada).
    QString removeDetail;
    const int removeTimeout = qMin(m_timeoutMs, 30000);
    const RunResult removeResult =
        runProcess(cli, {QStringLiteral("manifest"), QStringLiteral("rm"), tag},
                   contextInfo.absoluteFilePath(), removeTimeout, {}, cancelled, nullptr,
                   nullptr, &removeDetail);
    if (removeResult == RunResult::Cancelled) {
        setError(error, QStringLiteral("PODSTACK_CANCELLED"),
                 QStringLiteral("Build cancelado."));
        return false;
    }
    if (removeResult == RunResult::StartFailed) {
        setError(error, QStringLiteral("PODSTACK_IO_ERROR"),
                 QStringLiteral("Falha ao iniciar o CLI do Podman."), removeDetail,
                 QStringLiteral("Verifique a instalação do Podman."));
        return false;
    }

    QStringList arguments{QStringLiteral("build"),
                          QStringLiteral("--platform"),
                          platforms.join(QLatin1Char(',')),
                          QStringLiteral("--manifest"),
                          tag,
                          QStringLiteral("-f"),
                          dockerfileName};
    for (auto it = buildArgs.constBegin(); it != buildArgs.constEnd(); ++it) {
        arguments.append(QStringLiteral("--build-arg"));
        arguments.append(it.key() + QLatin1Char('=') + it.value());
    }
    arguments.append(contextInfo.absoluteFilePath());

    QStringList tail;
    const Line sink = [&tail, &onLine](const QString &line) {
        tail.append(line);
        if (tail.size() > 40) {
            tail.removeFirst();
        }
        if (onLine) {
            onLine(line);
        }
    };

    int exitCode = 0;
    QProcess::ExitStatus exitStatus = QProcess::NormalExit;
    QString detail;
    const RunResult result =
        runProcess(cli, arguments, contextInfo.absoluteFilePath(), m_timeoutMs, sink,
                   cancelled, &exitCode, &exitStatus, &detail);
    const QString output = tail.join(QLatin1Char('\n')).trimmed();

    if (result == RunResult::Cancelled) {
        setError(error, QStringLiteral("PODSTACK_CANCELLED"),
                 QStringLiteral("Build cancelado."));
        return false;
    }
    if (result == RunResult::TimedOut) {
        setError(error, QStringLiteral("PODSTACK_TIMEOUT"),
                 QStringLiteral("O build multi-plataforma excedeu o tempo limite."), output);
        return false;
    }
    if (result == RunResult::StartFailed) {
        setError(error, QStringLiteral("PODSTACK_IO_ERROR"),
                 QStringLiteral("Falha ao iniciar o CLI do Podman."), detail,
                 QStringLiteral("Verifique a instalação do Podman."));
        return false;
    }
    if (exitStatus != QProcess::NormalExit) {
        setError(error, QStringLiteral("PODSTACK_IO_ERROR"),
                 QStringLiteral("O processo do Podman encerrou inesperadamente."),
                 detail.isEmpty() ? output : detail);
        return false;
    }
    if (exitCode != 0) {
        setError(error, QStringLiteral("PODSTACK_IO_ERROR"),
                 QStringLiteral("O build multi-plataforma falhou."), output);
        return false;
    }
    return true;
}

} // namespace PodStack
