// SPDX-License-Identifier: GPL-3.0-or-later

#include "machines/ProcessScope.h"

#include "core/Logging.h"

#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>

namespace PodStack {
namespace {

bool envFlagEnabled(const char *name)
{
    const QString value = qEnvironmentVariable(name).trimmed().toLower();
    return value == QStringLiteral("1") || value == QStringLiteral("true")
           || value == QStringLiteral("yes") || value == QStringLiteral("on");
}

} // namespace

QString ProcessScope::systemdRunBinary()
{
    const QString requested = qEnvironmentVariable("PODSTACK_SYSTEMD_RUN_BINARY").trimmed();
    if (!requested.isEmpty()) {
        if (QFileInfo(requested).isAbsolute()) {
            return requested;
        }
        return QStandardPaths::findExecutable(requested);
    }
    return QStandardPaths::findExecutable(QStringLiteral("systemd-run"));
}

bool ProcessScope::available()
{
    if (envFlagEnabled("PODSTACK_DISABLE_SCOPE")) {
        return false;
    }

    const QString runner = systemdRunBinary();
    if (runner.isEmpty()) {
        return false;
    }
    const QFileInfo runnerInfo(runner);
    if (!runnerInfo.isFile() || !runnerInfo.isExecutable()) {
        return false;
    }

    // Um runner explícito (testes) dispensa o user manager real; o próprio
    // processo decide se consegue executar o comando.
    if (!qEnvironmentVariable("PODSTACK_SYSTEMD_RUN_BINARY").trimmed().isEmpty()) {
        return true;
    }

    const QString runtime = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
    if (runtime.isEmpty()) {
        return false;
    }
    return QFileInfo::exists(runtime + QStringLiteral("/systemd/private"));
}

QString ProcessScope::unitName(const QString &prefix, const QString &id)
{
    static const QRegularExpression invalid(QStringLiteral("[^A-Za-z0-9:_.-]"));
    QString name = prefix + QLatin1Char('-') + (id.isEmpty() ? QStringLiteral("default") : id);
    name.replace(invalid, QStringLiteral("-"));
    if (name.size() > 190) {
        name.truncate(190);
    }
    if (!name.endsWith(QStringLiteral(".scope"))) {
        name += QStringLiteral(".scope");
    }
    return name;
}

QStringList ProcessScope::arguments(const QString &unit, const QString &program,
                                    const QStringList &programArguments)
{
    QStringList result;
    result << QStringLiteral("--user") << QStringLiteral("--scope") << QStringLiteral("--collect")
           << QStringLiteral("--quiet");
    if (!unit.isEmpty()) {
        result << QStringLiteral("--unit=") + unit;
    }
    result << QStringLiteral("--") << program;
    result.append(programArguments);
    return result;
}

bool ProcessScope::startDetached(const QString &unit, const QString &program,
                                 const QStringList &programArguments, qint64 *pid,
                                 QString *failure, bool *scoped)
{
    if (pid) {
        *pid = 0;
    }
    if (failure) {
        failure->clear();
    }
    if (scoped) {
        *scoped = false;
    }

    QString scopeFailure;
    if (available()) {
        const QString runner = systemdRunBinary();
        const QStringList scopeArguments = arguments(unit, program, programArguments);
        qint64 launched = 0;
        if (QProcess::startDetached(runner, scopeArguments, QString(), &launched)
            && launched > 0) {
            if (pid) {
                *pid = launched;
            }
            if (scoped) {
                *scoped = true;
            }
            qCDebug(lcCore) << "processo iniciado em scope" << unit << "pid" << launched
                            << "programa" << program;
            return true;
        }
        scopeFailure = QStringLiteral("systemd-run não iniciou o processo");
        qCWarning(lcCore) << "Falha ao iniciar o scope" << unit
                          << "; tentando execução direta";
    }

    qint64 launched = 0;
    if (!QProcess::startDetached(program, programArguments, QString(), &launched)
        || launched <= 0) {
        if (failure) {
            *failure = scopeFailure.isEmpty()
                           ? QStringLiteral("não foi possível iniciar %1").arg(program)
                           : scopeFailure
                                 + QStringLiteral("; execução direta de %1 também falhou")
                                       .arg(program);
        }
        return false;
    }

    if (pid) {
        *pid = launched;
    }
    if (!scopeFailure.isEmpty()) {
        qCWarning(lcCore) << "processo iniciado sem scope" << program << "pid" << launched;
    }
    return true;
}

} // namespace PodStack
