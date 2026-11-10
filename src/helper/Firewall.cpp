// SPDX-License-Identifier: GPL-3.0-or-later

#include "helper/Firewall.h"

#include <QByteArray>
#include <QElapsedTimer>
#include <QProcess>
#include <QSet>
#include <QStandardPaths>
#include <QStringList>

#include <optional>

namespace PodStack::Firewall {
namespace {

constexpr int kMaxPorts = 32;
constexpr int kTimeoutMs = 5000;

struct Backend {
    QString id;
    QString program;
};

std::optional<Backend> detect()
{
    const QString firewalld = QStandardPaths::findExecutable(QStringLiteral("firewall-cmd"));
    if (!firewalld.isEmpty()) {
        return Backend{QStringLiteral("firewalld"), firewalld};
    }
    const QString ufw = QStandardPaths::findExecutable(QStringLiteral("ufw"));
    if (!ufw.isEmpty()) {
        return Backend{QStringLiteral("ufw"), ufw};
    }
    return std::nullopt;
}

QString summarize(const QString &text)
{
    const QStringList lines = text.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    QString summary = lines.mid(qMax(0, lines.size() - 3)).join(QStringLiteral(" | ")).trimmed();
    if (summary.size() > 200) {
        summary = summary.left(200) + QStringLiteral("…");
    }
    return summary;
}

Error ioError(const QString &backend, const QString &port, const QString &detail)
{
    Error error = Error::make(
        QStringLiteral("PODSTACK_IO_ERROR"),
        QStringLiteral("O firewall (%1) recusou a operação para %2.").arg(backend, port),
        summarize(detail),
        QStringLiteral("Verifique se o firewalld/ufw está ativo e tente novamente."));
    return error;
}

bool validate(const QStringList &ports, const QString &protocol, QStringList *normalized,
              Error *error)
{
    if (normalized) {
        normalized->clear();
    }
    if (error) {
        *error = {};
    }

    const QString normalizedProtocol = protocol.trimmed().toLower();
    if (normalizedProtocol != QStringLiteral("tcp")
        && normalizedProtocol != QStringLiteral("udp")) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_INVALID_PARAMS"),
                                 QStringLiteral("Protocolo inválido: use tcp ou udp."));
        }
        return false;
    }

    if (ports.isEmpty()) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_INVALID_PARAMS"),
                                 QStringLiteral("Informe ao menos uma porta."));
        }
        return false;
    }
    if (ports.size() > kMaxPorts) {
        if (error) {
            *error = Error::make(
                QStringLiteral("PODSTACK_INVALID_PARAMS"),
                QStringLiteral("No máximo %1 portas podem ser alteradas por vez.").arg(kMaxPorts));
        }
        return false;
    }

    QSet<int> seen;
    QStringList result;
    for (const QString &raw : ports) {
        bool ok = false;
        const int port = raw.trimmed().toInt(&ok);
        if (!ok || port < 1 || port > 65535) {
            if (error) {
                *error = Error::make(
                    QStringLiteral("PODSTACK_INVALID_PARAMS"),
                    QStringLiteral("Porta inválida: %1 (use 1..65535).").arg(raw.trimmed()));
            }
            return false;
        }
        if (seen.contains(port)) {
            continue;
        }
        seen.insert(port);
        result.append(QString::number(port));
    }

    if (normalized) {
        *normalized = result;
    }
    return true;
}

bool run(const Backend &backend, const QStringList &arguments, const QString &port, Error *error)
{
    QProcess process;
    process.setProcessChannelMode(QProcess::MergedChannels);
    QElapsedTimer timer;
    timer.start();
    process.start(backend.program, arguments);
    if (!process.waitForStarted(kTimeoutMs)) {
        if (error) {
            *error = ioError(backend.id, port,
                             QStringLiteral("não foi possível iniciar o processo"));
        }
        return false;
    }
    const int remaining = qMax(1, kTimeoutMs - static_cast<int>(timer.elapsed()));
    if (!process.waitForFinished(remaining)) {
        process.kill();
        process.waitForFinished(1000);
        if (error) {
            *error = ioError(backend.id, port, QStringLiteral("tempo limite de 5 s excedido"));
        }
        return false;
    }

    const QString output = QString::fromUtf8(process.readAll()).trimmed();
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        if (error) {
            *error = ioError(backend.id, port,
                             output.isEmpty()
                                 ? QStringLiteral("código %1").arg(process.exitCode())
                                 : output);
        }
        return false;
    }
    return true;
}

QStringList openArguments(const Backend &backend, const QString &port, const QString &protocol)
{
    if (backend.id == QStringLiteral("firewalld")) {
        return {QStringLiteral("--add-port=%1/%2").arg(port, protocol)};
    }
    return {QStringLiteral("allow"), QStringLiteral("%1/%2").arg(port, protocol)};
}

QStringList closeArguments(const Backend &backend, const QString &port, const QString &protocol)
{
    if (backend.id == QStringLiteral("firewalld")) {
        return {QStringLiteral("--remove-port=%1/%2").arg(port, protocol)};
    }
    return {QStringLiteral("delete"), QStringLiteral("allow"),
            QStringLiteral("%1/%2").arg(port, protocol)};
}

} // namespace

bool available(QString *backend)
{
    const std::optional<Backend> found = detect();
    if (backend) {
        *backend = found ? found->id : QString();
    }
    return found.has_value();
}

bool validatePorts(const QStringList &ports, const QString &protocol, QStringList *normalized,
                   Error *error)
{
    return validate(ports, protocol, normalized, error);
}

bool openPorts(const QStringList &ports, const QString &protocol, Error *error)
{
    QStringList normalized;
    if (!validate(ports, protocol, &normalized, error)) {
        return false;
    }

    const std::optional<Backend> backend = detect();
    if (!backend) {
        if (error) {
            *error = Error::make(
                QStringLiteral("PODSTACK_HELPER_REQUIRED"),
                QStringLiteral("Nenhum firewall suportado (firewalld/ufw) foi encontrado."),
                QStringLiteral("Instale o firewalld ou o ufw para expor portas na LAN."));
        }
        return false;
    }

    const QString normalizedProtocol = protocol.trimmed().toLower();
    QStringList opened;
    for (const QString &port : normalized) {
        if (!run(*backend, openArguments(*backend, port, normalizedProtocol), port, error)) {
            for (const QString &done : opened) {
                Error ignored;
                (void)run(*backend, closeArguments(*backend, done, normalizedProtocol), done,
                          &ignored);
            }
            return false;
        }
        opened.append(port);
    }
    return true;
}

bool closePorts(const QStringList &ports, const QString &protocol, Error *error)
{
    QStringList normalized;
    if (!validate(ports, protocol, &normalized, error)) {
        return false;
    }

    const std::optional<Backend> backend = detect();
    if (!backend) {
        if (error) {
            *error = Error::make(
                QStringLiteral("PODSTACK_HELPER_REQUIRED"),
                QStringLiteral("Nenhum firewall suportado (firewalld/ufw) foi encontrado."),
                QStringLiteral("Instale o firewalld ou o ufw para gerenciar portas na LAN."));
        }
        return false;
    }

    const QString normalizedProtocol = protocol.trimmed().toLower();
    for (const QString &port : normalized) {
        if (!run(*backend, closeArguments(*backend, port, normalizedProtocol), port, error)) {
            return false;
        }
    }
    return true;
}

} // namespace PodStack::Firewall
