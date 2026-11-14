// SPDX-License-Identifier: GPL-3.0-or-later
#include "gui/controllers/UpdateController.h"

#include "core/config/Paths.h"
#include "core/error/Error.h"
#include "core/rpc/RpcClient.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QProcess>
#include <QStandardPaths>
#include <QTimer>
#include <QtConcurrent>

namespace PodStack {

UpdateController::UpdateController(QObject *parent) : ControllerBase(parent)
{
    connect(&m_watcher, &QFutureWatcher<QJsonObject>::finished, this,
            &UpdateController::onFinished);
    QTimer::singleShot(5000, this, [this] { check(false); });
    QTimer::singleShot(1000, this, &UpdateController::loadState);
    auto *timer = new QTimer(this);
    timer->setInterval(6 * 60 * 60 * 1000);
    connect(timer, &QTimer::timeout, this, [this] { check(false); });
    timer->start();
}

void UpdateController::check(bool interactive)
{
    if (m_running) {
        return;
    }
    if (!interactive && !m_enabled) {
        return;
    }
    m_running = true;
    m_interactive = interactive;
    m_watcher.setFuture(QtConcurrent::run([interactive] {
        RpcClient client;
        Error error;
        const QJsonObject result =
            client.call(QStringLiteral("updates.check"),
                        QJsonObject{{QStringLiteral("force"), interactive}}, &error);
        if (!error.isEmpty()) {
            return QJsonObject{{QStringLiteral("__error"), error.message}};
        }
        return result;
    }));
}

void UpdateController::onFinished()
{
    m_running = false;
    const QJsonObject result = m_watcher.result();
    if (result.contains(QStringLiteral("__error"))) {
        const QString reason = result.value(QStringLiteral("__error")).toString();
        m_status = QVariantMap{{QStringLiteral("status"), QStringLiteral("unavailable")},
                               {QStringLiteral("reason"), reason}};
        emit statusChanged();
        emit checkFailed(reason);
        if (m_interactive) {
            emit userMessage(QStringLiteral("error"),
                             QStringLiteral("Falha ao verificar atualizações: %1").arg(reason));
        }
        return;
    }

    m_status = result.toVariantMap();
    emit statusChanged();
    const bool availableUpdate = result.value(QStringLiteral("updateAvailable")).toBool();
    if (availableUpdate) {
        emit available(m_status);
    }
    if (m_interactive) {
        emit userMessage(QStringLiteral("info"),
                         availableUpdate
                             ? QStringLiteral("PodStack %1 está disponível.")
                                   .arg(result.value(QStringLiteral("latestVersion")).toString())
                             : QStringLiteral("Nenhuma atualização disponível."));
    }
}

void UpdateController::loadState()
{
    QFile file(Paths::stateDir() + QStringLiteral("/updates/state.json"));
    if (!file.open(QIODevice::ReadOnly)) {
        return;
    }
    const QJsonObject object = QJsonDocument::fromJson(file.readAll()).object();
    const QString phase = object.value(QStringLiteral("phase")).toString();
    if (phase != QStringLiteral("done") && phase != QStringLiteral("failed")) {
        return;
    }
    QVariantMap state = object.toVariantMap();
    if (phase == QStringLiteral("done")) {
        state.insert(QStringLiteral("status"), QStringLiteral("installed"));
        state.insert(QStringLiteral("reason"), QStringLiteral("Atualização instalada."));
    } else {
        state.insert(QStringLiteral("status"), QStringLiteral("failed"));
        state.insert(QStringLiteral("reason"), object.value(QStringLiteral("message")).toString());
    }
    m_status = state;
    emit statusChanged();
}

void UpdateController::install()
{
    startRunner(false);
}

void UpdateController::resume()
{
    startRunner(true);
}

void UpdateController::startRunner(bool resume)
{
    if (qEnvironmentVariableIsSet("FLATPAK_ID") ||
        QFileInfo::exists(QStringLiteral("/.flatpak-info"))) {
        emit checkFailed(QStringLiteral(
            "O update assistido não está disponível no Flatpak; use o pacote do host."));
        return;
    }
    if (QStandardPaths::findExecutable(QStringLiteral("systemctl")).isEmpty()) {
        emit checkFailed(
            QStringLiteral("O update assistido exige systemd de usuário; use o caminho manual."));
        return;
    }
    const QString stateDir = Paths::stateDir() + QStringLiteral("/updates");
    if (!QDir().mkpath(stateDir)) {
        emit checkFailed(QStringLiteral("Não foi possível preparar o estado do update."));
        return;
    }
    QFile::setPermissions(stateDir,
                          QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
    QFile marker(stateDir + QStringLiteral("/gui-was-open"));
    if (!marker.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        emit checkFailed(QStringLiteral("Não foi possível gravar o marcador do update."));
        return;
    }
    marker.close();
    QFile::setPermissions(marker.fileName(), QFileDevice::ReadOwner | QFileDevice::WriteOwner);

    const QString sibling = QCoreApplication::applicationDirPath() + QStringLiteral("/podstack");
    QString program = QFileInfo(sibling).isExecutable()
                          ? sibling
                          : QStandardPaths::findExecutable(QStringLiteral("podstack"));
    QStringList arguments{QStringLiteral("update"), QStringLiteral("--install"),
                          QStringLiteral("--runner"), QStringLiteral("--yes"),
                          QStringLiteral("--json")};
    if (resume) {
        arguments.append(QStringLiteral("--resume"));
    }
    if (!QProcess::startDetached(program, arguments)) {
        QFile::remove(marker.fileName());
        emit checkFailed(QStringLiteral("Não foi possível iniciar o runner do update."));
        return;
    }
    QCoreApplication::quit();
}

void UpdateController::clearState()
{
    QFile::remove(Paths::stateDir() + QStringLiteral("/updates/state.json"));
    m_status.clear();
    emit statusChanged();
}

} // namespace PodStack
