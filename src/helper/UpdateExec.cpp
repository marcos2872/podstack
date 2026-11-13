// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/Redaction.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QSaveFile>
#include <QStringList>

#include <cstdio>

namespace {

QString argumentValue(const QStringList &arguments, const QString &name)
{
    const int index = arguments.indexOf(name);
    return index >= 0 && index + 1 < arguments.size() ? arguments.at(index + 1) : QString();
}

bool writeState(const QString &stateDir, const QString &phase, const QString &message,
                int percent, int exitCode, const QString &errorCode, const QString &jobId,
                const QString &uid)
{
    QDir().mkpath(stateDir);
    QFile::setPermissions(stateDir,
                          QFileDevice::ReadOwner | QFileDevice::WriteOwner
                              | QFileDevice::ExeOwner);
    QJsonObject state;
    state.insert(QStringLiteral("jobId"), jobId);
    state.insert(QStringLiteral("uid"), static_cast<qint64>(uid.toUInt()));
    state.insert(QStringLiteral("phase"), phase);
    state.insert(QStringLiteral("percent"), percent);
    state.insert(QStringLiteral("message"), message);
    state.insert(QStringLiteral("exitCode"), exitCode);
    state.insert(QStringLiteral("errorCode"), errorCode);
    state.insert(QStringLiteral("timestamp"),
                 QDateTime::currentDateTimeUtc().toString(Qt::ISODate));

    QSaveFile file(stateDir + QStringLiteral("/state.json"));
    if (!file.open(QIODevice::WriteOnly)) {
        return false;
    }
    file.write(QJsonDocument(state).toJson(QJsonDocument::Compact));
    if (!file.commit()) {
        return false;
    }
    QFile::setPermissions(stateDir + QStringLiteral("/state.json"),
                          QFileDevice::ReadOwner | QFileDevice::WriteOwner
                              | QFileDevice::ReadGroup);
    return true;
}

void appendLog(const QString &logPath, const QString &jobId, const QString &uid,
               const QString &level, const QString &message)
{
    const QByteArray redacted = PodStack::Redaction::text(message).toUtf8();
    if (logPath.isEmpty()) {
        return;
    }
    QDir().mkpath(QFileInfo(logPath).absolutePath());
    QFile::setPermissions(QFileInfo(logPath).absolutePath(),
                          QFileDevice::ReadOwner | QFileDevice::WriteOwner
                              | QFileDevice::ExeOwner | QFileDevice::ReadGroup
                              | QFileDevice::ExeGroup);
    QFile file(logPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Append)) {
        return;
    }
    QJsonObject entry;
    entry.insert(QStringLiteral("ts"),
                 QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    entry.insert(QStringLiteral("level"), level);
    entry.insert(QStringLiteral("category"), QStringLiteral("podstack.update"));
    entry.insert(QStringLiteral("jobId"), jobId);
    entry.insert(QStringLiteral("uid"), static_cast<qint64>(uid.toUInt()));
    entry.insert(QStringLiteral("message"), QString::fromUtf8(redacted));
    file.write(QJsonDocument(entry).toJson(QJsonDocument::Compact) + '\n');
    file.close();
    QFile::setPermissions(logPath, QFileDevice::ReadOwner | QFileDevice::WriteOwner
                                       | QFileDevice::ReadGroup);
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    const QStringList arguments = app.arguments();
    const QString jobId = argumentValue(arguments, QStringLiteral("--job-id"));
    const QString uid = argumentValue(arguments, QStringLiteral("--uid"));
    const QString stateDir = argumentValue(arguments, QStringLiteral("--state-dir"));
    const QString logPath = argumentValue(arguments, QStringLiteral("--log-file"));
    const QString artifact = argumentValue(arguments, QStringLiteral("--artifact"));
    const QString manager = argumentValue(arguments, QStringLiteral("--manager"));

    if (jobId.isEmpty() || uid.isEmpty() || stateDir.isEmpty() || artifact.isEmpty()
        || (manager != QStringLiteral("apt-get") && manager != QStringLiteral("dnf"))) {
        std::fprintf(stderr, "podstack-update-exec: argumentos inválidos\n");
        return 1;
    }

    writeState(stateDir, QStringLiteral("installing"),
               QStringLiteral("Executando o gerenciador de pacotes."), 10, 0, QString(), jobId,
               uid);
    appendLog(logPath, jobId, uid, QStringLiteral("info"),
              QStringLiteral("Executando %1 install %2").arg(manager, artifact));

    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("PODSTACK_UPDATE_JOB_ID"), jobId);
    environment.insert(QStringLiteral("PODSTACK_UPDATE_UID"), uid);

    QProcess process;
    process.setProcessEnvironment(environment);
    process.setProcessChannelMode(QProcess::SeparateChannels);
    process.start(manager, {QStringLiteral("install"), QStringLiteral("-y"), artifact});
    if (!process.waitForStarted(10000)) {
        appendLog(logPath, jobId, uid, QStringLiteral("error"),
                  QStringLiteral("Não foi possível iniciar %1: %2")
                      .arg(manager, process.errorString()));
        writeState(stateDir, QStringLiteral("failed"),
                   QStringLiteral("Não foi possível iniciar o gerenciador."), 0, 1,
                   QStringLiteral("PODSTACK_UPDATE_INSTALL_FAILED"), jobId, uid);
        return 1;
    }

    const auto drain = [&](const QByteArray &payload) {
        for (const QByteArray &line : payload.split('\n')) {
            const QString text = QString::fromUtf8(line).trimmed();
            if (text.isEmpty()) {
                continue;
            }
            appendLog(logPath, jobId, uid, QStringLiteral("info"), text);
            std::fprintf(stdout, "%s\n",
                         PodStack::Redaction::text(text).toUtf8().constData());
        }
    };

    while (process.state() != QProcess::NotRunning) {
        process.waitForFinished(1000);
        drain(process.readAllStandardOutput());
        drain(process.readAllStandardError());
    }
    drain(process.readAllStandardOutput());
    drain(process.readAllStandardError());
    std::fflush(stdout);

    const bool ok = process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0;
    appendLog(logPath, jobId, uid, ok ? QStringLiteral("info") : QStringLiteral("error"),
              ok ? QStringLiteral("Gerenciador concluiu com sucesso.")
                 : QStringLiteral("Gerenciador falhou com código %1.").arg(process.exitCode()));
    writeState(stateDir, ok ? QStringLiteral("done") : QStringLiteral("failed"),
               ok ? QStringLiteral("Update instalado.") : QStringLiteral("Falha na instalação."),
               ok ? 100 : 0, process.exitCode(),
               ok ? QString() : QStringLiteral("PODSTACK_UPDATE_INSTALL_FAILED"), jobId, uid);
    return ok ? 0 : 1;
}
