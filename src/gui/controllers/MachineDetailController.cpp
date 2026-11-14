// SPDX-License-Identifier: GPL-3.0-or-later
#include "gui/controllers/MachineDetailController.h"

#include "core/config/Paths.h"
#include "core/error/Error.h"
#include "core/rpc/RpcClient.h"

#include <QClipboard>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QStandardPaths>
#include <QtConcurrent>

namespace PodStack {

MachineDetailController::MachineDetailController(QObject *parent) : ControllerBase(parent)
{
    connect(&m_machineLogsWatcher, &QFutureWatcher<QJsonObject>::finished, this,
            &MachineDetailController::onMachineLogsFinished);
    connect(&m_machineFilesWatcher, &QFutureWatcher<QJsonObject>::finished, this,
            &MachineDetailController::onMachineFilesFinished);
    connect(&m_machineStatsStream, &StreamClient::machineStats, this,
            [this](const QJsonObject &sample) {
                m_machineStats = sample.toVariantMap();
                const auto append = [](QVariantList &list, double value) {
                    list.append(value);
                    if (list.size() > 60) {
                        list.removeFirst();
                    }
                };
                append(m_machineCpuHistory, sample.value(QStringLiteral("cpuPercent")).toDouble());
                append(m_machineMemHistory, sample.value(QStringLiteral("memPercent")).toDouble());
                append(m_machineNetRxHistory, sample.value(QStringLiteral("netRx")).toDouble());
                append(m_machineNetTxHistory, sample.value(QStringLiteral("netTx")).toDouble());
                m_machineStatsError.clear();
                emit statsChanged();
            });
    connect(&m_machineStatsStream, &StreamClient::error, this, [this](const QString &message) {
        m_machineStatsError = message;
        emit statsChanged();
    });

    connect(&m_machineConsoleStream, &StreamClient::machineConsoleEvent, this,
            [this](const QJsonObject &event) {
                const QString data = event.value(QStringLiteral("data")).toString();
                if (event.value(QStringLiteral("reset")).toBool()) {
                    m_machineConsoleText = data;
                } else {
                    m_machineConsoleText += data;
                }
                constexpr int kMaxConsoleChars = 512 * 1024;
                if (m_machineConsoleText.size() > kMaxConsoleChars) {
                    m_machineConsoleText.remove(0, m_machineConsoleText.size() - kMaxConsoleChars);
                }
                m_machineConsoleActive = true;
                m_machineConsoleError.clear();
                emit consoleChanged();
            });
    connect(&m_machineConsoleStream, &StreamClient::error, this, [this](const QString &message) {
        m_machineConsoleActive = false;
        m_machineConsoleError = message;
        emit consoleChanged();
    });
}

void MachineDetailController::startStats(const QString &id)
{
    m_machineStats.clear();
    m_machineCpuHistory.clear();
    m_machineMemHistory.clear();
    m_machineNetRxHistory.clear();
    m_machineNetTxHistory.clear();
    m_machineStatsError.clear();
    emit statsChanged();

    if (id.isEmpty()) {
        m_machineStatsError = QStringLiteral("Machine inválida.");
        m_lastError = m_machineStatsError;
        emit lastErrorChanged();
        emit statsChanged();
        return;
    }

    m_machineStatsStream.subscribeMachineStats(id, 2);
}

void MachineDetailController::stopStats()
{
    m_machineStatsStream.unsubscribe();

    const bool clearGlobalError =
        !m_machineStatsError.isEmpty() && m_lastError == m_machineStatsError;
    m_machineStatsError.clear();
    emit statsChanged();
    if (clearGlobalError) {
        m_lastError.clear();
        emit lastErrorChanged();
    }
}

void MachineDetailController::followConsole(const QString &id, int tailBytes)
{
    if (id.isEmpty()) {
        m_machineConsoleActive = false;
        m_machineConsoleError = QStringLiteral("Machine inválida.");
        m_machineConsoleText.clear();
        emit consoleChanged();
        return;
    }

    if (tailBytes <= 0) {
        tailBytes = 64 * 1024;
    }
    tailBytes = qBound(1, tailBytes, 512 * 1024);

    m_machineConsoleText.clear();
    m_machineConsoleError.clear();
    m_machineConsoleActive = true;
    emit consoleChanged();
    m_machineConsoleStream.startMachineConsole(id, tailBytes);
}

void MachineDetailController::stopConsole()
{
    m_machineConsoleStream.stopMachineConsole();
    m_machineConsoleActive = false;
    m_machineConsoleText.clear();
    m_machineConsoleError.clear();
    emit consoleChanged();
}

void MachineDetailController::loadLogs(const QString &id, int tail)
{
    if (id.isEmpty()) {
        m_lastError = QStringLiteral("Machine inválida.");
        emit lastErrorChanged();
        return;
    }

    if (tail <= 0) {
        tail = 200;
    }
    tail = qBound(1, tail, 2000);

    if (m_machineLogsWatcher.isRunning()) {
        m_pendingMachineLogsId = id;
        m_pendingMachineLogsTail = tail;
        return;
    }

    m_machineLogsWatcher.setFuture(QtConcurrent::run([id, tail] {
        RpcClient client;
        Error error;
        const QJsonObject result = client.call(
            QStringLiteral("machines.logs"),
            QJsonObject{{QStringLiteral("id"), id}, {QStringLiteral("tail"), tail}}, &error);
        if (!error.isEmpty()) {
            return QJsonObject{{QStringLiteral("error"), error.toJson()}};
        }
        return result;
    }));
}

void MachineDetailController::onMachineLogsFinished()
{
    const QJsonObject result = m_machineLogsWatcher.result();
    if (result.contains(QStringLiteral("error"))) {
        const QJsonObject errorObject = result.value(QStringLiteral("error")).toObject();
        m_lastError = errorObject.value(QStringLiteral("message")).toString();
        emit lastErrorChanged();
    } else {
        QStringList lines;
        const QJsonArray array = result.value(QStringLiteral("lines")).toArray();
        lines.reserve(array.size());
        for (const QJsonValue &value : array) {
            lines.append(value.toString());
        }
        m_machineLogLines = lines;
        m_machineLogsCapturedAt =
            QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
        emit logsChanged();
    }

    if (!m_pendingMachineLogsId.isEmpty()) {
        const QString next = m_pendingMachineLogsId;
        const int tail = m_pendingMachineLogsTail;
        m_pendingMachineLogsId.clear();
        loadLogs(next, tail);
    }
}

void MachineDetailController::refreshFiles(const QString &id, const QString &path)
{
    if (id.isEmpty()) {
        m_machineFilesError = QStringLiteral("Machine inválida.");
        emit filesChanged();
        return;
    }

    const QString target = path.isEmpty() ? QStringLiteral("/") : path;
    m_machineFilesPath = target;
    if (!m_machineFilesWatcher.isRunning()) {
        m_machineFiles.clear();
    }
    startMachineFilesRequest(
        QStringLiteral("list"), id, QStringLiteral("machines.files.list"),
        QJsonObject{{QStringLiteral("id"), id}, {QStringLiteral("path"), target}}, 30000);
}

void MachineDetailController::downloadFile(const QString &id, const QString &path,
                                           const QString &fileName)
{
    if (id.isEmpty() || path.isEmpty()) {
        m_machineFilesError = QStringLiteral("Informe a machine e o arquivo remoto.");
        emit filesChanged();
        return;
    }

    QString name = fileName;
    if (name.isEmpty()) {
        name = QFileInfo(path).fileName();
    }
    if (name.isEmpty() || name == QStringLiteral(".") || name == QStringLiteral("..") ||
        name.contains(QLatin1Char('/')) || name.contains(QLatin1Char('\\'))) {
        m_machineFilesError = QStringLiteral("Nome de arquivo remoto inválido.");
        emit filesChanged();
        return;
    }

    QString directory = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    if (directory.isEmpty()) {
        directory = QDir::homePath();
    }
    if (!QDir().mkpath(directory)) {
        m_machineFilesError = QStringLiteral("Não foi possível criar a pasta de downloads.");
        emit filesChanged();
        return;
    }

    const QString destination = directory + QLatin1Char('/') + name;
    startMachineFilesRequest(QStringLiteral("download"), id,
                             QStringLiteral("machines.files.download"),
                             QJsonObject{{QStringLiteral("id"), id},
                                         {QStringLiteral("path"), path},
                                         {QStringLiteral("destination"), destination}},
                             600000);
}

void MachineDetailController::uploadFile(const QString &id, const QString &remoteDir,
                                         const QString &localPath)
{
    if (id.isEmpty() || remoteDir.isEmpty() || localPath.isEmpty()) {
        m_machineFilesError = QStringLiteral("Informe a machine, o destino e o arquivo local.");
        emit filesChanged();
        return;
    }

    const QFileInfo source(localPath);
    const QString name = source.fileName();
    if (name.isEmpty()) {
        m_machineFilesError = QStringLiteral("Arquivo local inválido.");
        emit filesChanged();
        return;
    }

    const QString base =
        remoteDir.endsWith(QLatin1Char('/')) ? remoteDir : remoteDir + QLatin1Char('/');
    startMachineFilesRequest(QStringLiteral("upload"), id, QStringLiteral("machines.files.upload"),
                             QJsonObject{{QStringLiteral("id"), id},
                                         {QStringLiteral("path"), base + name},
                                         {QStringLiteral("sourcePath"), localPath}},
                             600000);
}

void MachineDetailController::startMachineFilesRequest(const QString &operation, const QString &id,
                                                       const QString &method,
                                                       const QJsonObject &params, int timeoutMs)
{
    if (m_machineFilesWatcher.isRunning()) {
        m_machineFilesError = QStringLiteral("Aguarde a operação de arquivos atual terminar.");
        emit filesChanged();
        return;
    }

    m_machineFilesMachineId = id;
    m_machineFilesOperation = operation;
    m_machineFilesBusy = true;
    m_machineFilesError.clear();
    emit filesChanged();

    m_machineFilesWatcher.setFuture(QtConcurrent::run([method, params, timeoutMs] {
        RpcClient client;
        Error error;
        const QJsonObject result = client.call(method, params, &error, timeoutMs);
        if (!error.isEmpty()) {
            return QJsonObject{{QStringLiteral("error"), error.toJson()}};
        }
        return result;
    }));
}

void MachineDetailController::onMachineFilesFinished()
{
    const QJsonObject result = m_machineFilesWatcher.result();
    const QString operation = m_machineFilesOperation;
    m_machineFilesOperation.clear();
    m_machineFilesBusy = false;

    if (result.value(QStringLiteral("error")).isObject()) {
        m_machineFilesError = result.value(QStringLiteral("error"))
                                  .toObject()
                                  .value(QStringLiteral("message"))
                                  .toString();
        if (m_machineFilesError.isEmpty()) {
            m_machineFilesError = QStringLiteral("A operação de arquivos da machine falhou.");
        }
        if (operation == QStringLiteral("list")) {
            m_machineFiles.clear();
        }
        emit filesChanged();
        return;
    }

    m_machineFilesError.clear();
    if (operation == QStringLiteral("list")) {
        m_machineFilesPath = result.value(QStringLiteral("path")).toString(m_machineFilesPath);
        m_machineFiles.clear();
        const QJsonArray entries = result.value(QStringLiteral("items")).toArray();
        for (const QJsonValue &value : entries) {
            const QJsonObject entry = value.toObject();
            QVariantMap item;
            item.insert(QStringLiteral("name"), entry.value(QStringLiteral("name")).toString());
            item.insert(QStringLiteral("path"), entry.value(QStringLiteral("path")).toString());
            item.insert(QStringLiteral("directory"),
                        entry.value(QStringLiteral("directory")).toBool());
            item.insert(QStringLiteral("size"), entry.value(QStringLiteral("size")).toDouble());
            item.insert(QStringLiteral("modified"),
                        entry.value(QStringLiteral("modified")).toString());
            m_machineFiles.append(item);
        }
        emit filesChanged();
        return;
    }

    emit filesChanged();
    if (operation == QStringLiteral("upload") && !m_machineFilesMachineId.isEmpty()) {
        const QString machineId = m_machineFilesMachineId;
        const QString path = m_machineFilesPath;
        QTimer::singleShot(0, this, [this, machineId, path] { refreshFiles(machineId, path); });
    }
}

void MachineDetailController::copyLogs()
{
    if (m_machineLogLines.isEmpty()) {
        return;
    }
    if (QClipboard *clipboard = QGuiApplication::clipboard()) {
        clipboard->setText(m_machineLogLines.join(QLatin1Char('\n')));
    }
}

} // namespace PodStack
