// SPDX-License-Identifier: GPL-3.0-or-later
#include "gui/controllers/ContainerController.h"

#include "core/error/Error.h"
#include "core/rpc/RpcClient.h"

#include "gui/windows/TerminalWindow.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QMetaMethod>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTimer>

namespace PodStack {
namespace {

double parsePercent(const QString &text)
{
    QString copy = text.trimmed();
    copy.remove(QLatin1Char('%'));
    bool ok = false;
    const double value = copy.toDouble(&ok);
    return ok ? value : 0.0;
}

void appendHistory(QVariantList &list, double value)
{
    list.append(value);
    if (list.size() > 60) {
        list.removeFirst();
    }
}

} // namespace

ContainerController::ContainerController(QObject *parent) : CatalogController(parent)
{
    m_logsTimer = new QTimer(this);
    m_logsTimer->setSingleShot(true);
    m_logsTimer->setInterval(50);
    connect(m_logsTimer, &QTimer::timeout, this, [this] {
        m_logsPending = false;
        emit logsChanged();
    });

    connect(&m_stream, &StreamClient::logLine, this, [this](const QString &line) {
        m_logLines.append(line);
        if (m_logLines.size() > 10000) {
            m_logLines = m_logLines.mid(2000);
        }
        if (!m_logsPending) {
            m_logsPending = true;
            m_logsTimer->start();
        }
    });

    connect(&m_stream, &StreamClient::stats, this, [this](const QJsonObject &stats) {
        m_stats = stats.toVariantMap();
        appendHistory(m_cpuHistory,
                      parsePercent(stats.value(QStringLiteral("cpu_percent")).toString()));
        appendHistory(m_memHistory,
                      parsePercent(stats.value(QStringLiteral("mem_percent")).toString()));
        emit statsChanged();
    });

    connect(&m_stream, &StreamClient::error, this,
            [this](const QString &message) { setLastError(message); });
}

void ContainerController::refresh()
{
    if (!m_loading) {
        m_loading = true;
        emit loadingChanged();
    }
    const quint64 generation = ++m_refreshGeneration;
    // Timeout curto: um daemon travado precisa aparecer como erro em segundos,
    // não deixar a lista vazia por 30 s como se não houvesse containers.
    callRpc(
        listMethod(), listParams(),
        [this, generation](const QJsonObject &result) {
            if (generation != m_refreshGeneration) {
                // Leitura atrasada: a mais nova é quem fecha o loading.
                return;
            }
            m_loading = false;
            emit loadingChanged();
            if (isErrorResult(result)) {
                // Mantém o último retrato: um erro transitório não pode
                // apagar a lista e fingir que o engine está vazio.
                emit connectionChanged(false, errorMessage(result));
                return;
            }
            setItems(parseItems(result.value(QStringLiteral("items")).toArray()));
            emit connectionChanged(true, {});
        },
        8000);
}

QVariantList ContainerController::parseItems(const QJsonArray &items) const
{
    QVariantList parsed;
    parsed.reserve(items.size());
    for (const QJsonValue &value : items) {
        const QJsonObject object = value.toObject();
        QVariantMap item;
        item.insert(QStringLiteral("id"), object.value(QStringLiteral("id")).toString());
        item.insert(QStringLiteral("name"), object.value(QStringLiteral("name")).toString());
        item.insert(QStringLiteral("state"), object.value(QStringLiteral("state")).toString());
        item.insert(QStringLiteral("image"), object.value(QStringLiteral("image")).toString());
        item.insert(QStringLiteral("health"), object.value(QStringLiteral("health")).toString());
        item.insert(QStringLiteral("status"), object.value(QStringLiteral("status")).toString());
        item.insert(QStringLiteral("created"), object.value(QStringLiteral("created")).toString());

        QVariantMap labels;
        const QJsonObject labelObject = object.value(QStringLiteral("labels")).toObject();
        for (auto it = labelObject.constBegin(); it != labelObject.constEnd(); ++it) {
            labels.insert(it.key(), it.value().toString());
        }
        item.insert(QStringLiteral("labels"), labels);
        item.insert(QStringLiteral("project"),
                    labels.value(QStringLiteral("com.docker.compose.project")).toString());
        item.insert(QStringLiteral("service"),
                    labels.value(QStringLiteral("com.docker.compose.service")).toString());

        QStringList ports;
        for (const QJsonValue &port : object.value(QStringLiteral("ports")).toArray()) {
            ports.append(port.toString());
        }
        item.insert(QStringLiteral("ports"), ports.join(QStringLiteral(", ")));
        parsed.append(item);
    }
    return parsed;
}

void ContainerController::lifecycle(const QString &action, const QString &id)
{
    if (action.isEmpty() || id.isEmpty()) {
        setLastError(QStringLiteral("Ação ou container inválido."));
        return;
    }
    runAction(QStringLiteral("containers.") + action, QJsonObject{{QStringLiteral("id"), id}});
}

void ContainerController::removeContainer(const QString &id, bool force, bool volumes)
{
    if (id.isEmpty()) {
        setLastError(QStringLiteral("Container inválido."));
        return;
    }
    runAction(QStringLiteral("containers.remove"),
              QJsonObject{{QStringLiteral("id"), id},
                          {QStringLiteral("force"), force},
                          {QStringLiteral("volumes"), volumes}});
}

void ContainerController::createContainer(const QVariantMap &spec, bool start)
{
    // Validação local também precisa chegar ao diálogo: antes ela só ia para
    // `lastError` (rodapé do inspetor) e clicar em Criar com porta/env inválido
    // não mostrava nada no formulário.
    const auto fail = [this](const QString &message) {
        ControllerBase::setLastError(message);
        emit actionFailed(QStringLiteral("containers.create"),
                          QStringLiteral("PODSTACK_INVALID_PARAMS"), message);
    };
    QJsonObject params;
    params.insert(QStringLiteral("image"),
                  spec.value(QStringLiteral("image")).toString().trimmed());
    params.insert(QStringLiteral("name"), spec.value(QStringLiteral("name")).toString().trimmed());
    params.insert(QStringLiteral("command"), spec.value(QStringLiteral("command")).toString());
    params.insert(QStringLiteral("workDir"), spec.value(QStringLiteral("workDir")).toString());
    params.insert(QStringLiteral("user"), spec.value(QStringLiteral("user")).toString());
    params.insert(QStringLiteral("remove"), spec.value(QStringLiteral("remove")).toBool());
    params.insert(QStringLiteral("init"), spec.value(QStringLiteral("init")).toBool());
    params.insert(QStringLiteral("privileged"), spec.value(QStringLiteral("privileged")).toBool());
    params.insert(QStringLiteral("readOnly"), spec.value(QStringLiteral("readOnly")).toBool());
    params.insert(QStringLiteral("restartPolicy"),
                  spec.value(QStringLiteral("restartPolicy")).toString());
    params.insert(QStringLiteral("restartTries"),
                  spec.value(QStringLiteral("restartTries")).toInt());
    params.insert(QStringLiteral("cpus"), spec.value(QStringLiteral("cpus")).toDouble());
    params.insert(QStringLiteral("memoryMb"), spec.value(QStringLiteral("memoryMb")).toInt());
    params.insert(QStringLiteral("network"), spec.value(QStringLiteral("network")).toString());
    const bool exposeLan = spec.value(QStringLiteral("exposeLan")).toBool();
    params.insert(QStringLiteral("exposeLan"), exposeLan);
    params.insert(QStringLiteral("start"), start);

    QJsonObject env;
    const QString envText = spec.value(QStringLiteral("envText")).toString();
    const QStringList envLines =
        envText.split(QRegularExpression(QStringLiteral("[,\\n]")), Qt::SkipEmptyParts);
    for (const QString &line : envLines) {
        const int equals = line.indexOf(QLatin1Char('='));
        if (equals <= 0) {
            fail(QStringLiteral("Variável de ambiente inválida: “%1” (use CHAVE=valor).")
                             .arg(line.trimmed()));
            return;
        }
        env.insert(line.left(equals).trimmed(), line.mid(equals + 1).trimmed());
    }
    params.insert(QStringLiteral("env"), env);

    QJsonObject labels;
    const QString labelsText = spec.value(QStringLiteral("labelsText")).toString();
    const QStringList labelLines =
        labelsText.split(QRegularExpression(QStringLiteral("[,\\n]")), Qt::SkipEmptyParts);
    for (const QString &line : labelLines) {
        const int equals = line.indexOf(QLatin1Char('='));
        if (equals <= 0) {
            fail(
                QStringLiteral("Label inválida: “%1” (use chave=valor).").arg(line.trimmed()));
            return;
        }
        labels.insert(line.left(equals).trimmed(), line.mid(equals + 1).trimmed());
    }
    params.insert(QStringLiteral("labels"), labels);

    QJsonArray ports;
    const QString portsText = spec.value(QStringLiteral("portsText")).toString();
    const QStringList portItems =
        portsText.split(QRegularExpression(QStringLiteral("[,\\n]")), Qt::SkipEmptyParts);
    for (const QString &raw : portItems) {
        const QString item = raw.trimmed();
        const QStringList parts = item.split(QLatin1Char(':'));
        if (parts.size() < 2 || parts.size() > 3) {
            fail(QStringLiteral("Porta inválida: “%1” (use host:container).").arg(item));
            return;
        }
        QString containerPart = parts.last();
        QString protocol = QStringLiteral("tcp");
        const int slash = containerPart.indexOf(QLatin1Char('/'));
        if (slash > 0) {
            protocol = containerPart.mid(slash + 1).toLower();
            containerPart = containerPart.left(slash);
        }
        bool containerOk = false;
        bool hostOk = false;
        const int containerPort = containerPart.toInt(&containerOk);
        const int hostPort = parts.at(parts.size() == 2 ? 0 : 1).toInt(&hostOk);
        if (!containerOk || !hostOk || containerPort <= 0 || hostPort <= 0) {
            fail(QStringLiteral("Porta inválida: “%1” (use host:container).").arg(item));
            return;
        }
        QJsonObject port;
        port.insert(QStringLiteral("containerPort"), containerPort);
        port.insert(QStringLiteral("protocol"), protocol);
        port.insert(QStringLiteral("hostPort"), hostPort);
        if (parts.size() == 3) {
            const QString hostIp = parts.at(0).trimmed();
            if (hostIp.isEmpty()) {
                fail(QStringLiteral("IP inválido em “%1”.").arg(item));
                return;
            }
            port.insert(QStringLiteral("hostIp"), exposeLan ? QStringLiteral("0.0.0.0") : hostIp);
        } else {
            port.insert(QStringLiteral("hostIp"),
                        exposeLan ? QStringLiteral("0.0.0.0") : QStringLiteral("127.0.0.1"));
        }
        ports.append(port);
    }
    params.insert(QStringLiteral("ports"), ports);

    QJsonArray mounts;
    const QString mountsText = spec.value(QStringLiteral("mountsText")).toString();
    // Vírgula e quebra de linha separam mounts; o placeholder da UI usa vírgula.
    const QStringList mountItems =
        mountsText.split(QRegularExpression(QStringLiteral("[,\\n]")), Qt::SkipEmptyParts);
    for (const QString &raw : mountItems) {
        const QString item = raw.trimmed();
        const QStringList parts = item.split(QLatin1Char(':'));
        if (parts.size() < 2 || parts.size() > 3) {
            fail(
                QStringLiteral("Volume inválido: “%1” (use host:container[:ro]).").arg(item));
            return;
        }
        const QString source = parts.at(0).trimmed();
        const QString destination = parts.at(1).trimmed();
        if (source.isEmpty() || destination.isEmpty()) {
            fail(
                QStringLiteral("Volume inválido: “%1” (use host:container[:ro]).").arg(item));
            return;
        }
        QJsonObject mount;
        mount.insert(QStringLiteral("source"), source);
        mount.insert(QStringLiteral("destination"), destination);
        mount.insert(QStringLiteral("readOnly"),
                     parts.size() == 3 && parts.at(2).trimmed() == QStringLiteral("ro"));
        mounts.append(mount);
    }
    params.insert(QStringLiteral("mounts"), mounts);

    runAction(QStringLiteral("containers.create"), params);
}

void ContainerController::exposeContainerLan(const QString &id, bool expose)
{
    if (id.isEmpty()) {
        setLastError(QStringLiteral("Container inválido."));
        return;
    }
    runAction(QStringLiteral("containers.exposeLan"),
              QJsonObject{{QStringLiteral("id"), id}, {QStringLiteral("expose"), expose}});
}

void ContainerController::renameContainer(const QString &id, const QString &name)
{
    if (id.isEmpty() || name.isEmpty()) {
        setLastError(QStringLiteral("Informe o container e o novo nome."));
        return;
    }
    runAction(QStringLiteral("containers.rename"),
              QJsonObject{{QStringLiteral("id"), id}, {QStringLiteral("name"), name}});
}

void ContainerController::duplicateContainer(const QString &id)
{
    if (id.isEmpty()) {
        setLastError(QStringLiteral("Container inválido."));
        return;
    }
    runAction(QStringLiteral("containers.duplicate"), QJsonObject{{QStringLiteral("id"), id}});
}

void ContainerController::batchAll(const QString &action)
{
    QStringList ids;
    for (const QVariant &value : items()) {
        const QString id = value.toMap().value(QStringLiteral("id")).toString();
        if (!id.isEmpty()) {
            ids.append(id);
        }
    }
    batchIds(ids, action);
}

void ContainerController::batchIds(const QStringList &ids, const QString &action)
{
    if (action != QStringLiteral("start") && action != QStringLiteral("stop") &&
        action != QStringLiteral("restart") && action != QStringLiteral("remove")) {
        setLastError(QStringLiteral("Ação de lote inválida."));
        return;
    }
    if (ids.isEmpty()) {
        return;
    }
    runJob(
        QStringLiteral("containers.batch"), {},
        [ids, action] {
            RpcClient client;
            int failed = 0;
            QJsonObject lastError;
            for (const QString &id : ids) {
                Error error;
                QJsonObject params{{QStringLiteral("id"), id}};
                if (action == QStringLiteral("remove")) {
                    // Remoção em lote é confirmada na UI; sem `force` um
                    // container em execução faria o lote falhar inteiro.
                    params.insert(QStringLiteral("force"), true);
                }
                client.call(QStringLiteral("containers.") + action, params, &error);
                if (!error.isEmpty()) {
                    ++failed;
                    lastError = error.toJson();
                }
            }
            QJsonObject result;
            result.insert(QStringLiteral("count"), ids.size());
            result.insert(QStringLiteral("failed"), failed);
            result.insert(QStringLiteral("ok"), failed == 0);
            if (failed > 0) {
                result.insert(QStringLiteral("lastError"), lastError);
            }
            return result;
        },
        {}, 60000);
}

void ContainerController::listFiles(const QString &id, const QString &path)
{
    m_filesPath = path;
    callRpc(
        QStringLiteral("containers.files.list"),
        QJsonObject{{QStringLiteral("id"), id}, {QStringLiteral("path"), path}},
        [this](const QJsonObject &result) {
            if (isErrorResult(result)) {
                setLastError(errorMessage(result));
                return;
            }
            m_filesPath = result.value(QStringLiteral("path")).toString(m_filesPath);
            m_files.clear();
            for (const QJsonValue &value : result.value(QStringLiteral("entries")).toArray()) {
                const QJsonObject entry = value.toObject();
                QVariantMap item;
                item.insert(QStringLiteral("name"), entry.value(QStringLiteral("name")).toString());
                item.insert(QStringLiteral("size"), entry.value(QStringLiteral("size")).toDouble());
                item.insert(QStringLiteral("directory"),
                            entry.value(QStringLiteral("directory")).toBool());
                item.insert(QStringLiteral("mode"), entry.value(QStringLiteral("mode")).toString());
                item.insert(QStringLiteral("modified"),
                            entry.value(QStringLiteral("modified")).toString());
                m_files.append(item);
            }
            emit filesChanged();
        });
}

void ContainerController::downloadFile(const QString &id, const QString &path)
{
    const QString baseName = QFileInfo(path).fileName();
    if (baseName.isEmpty()) {
        setLastError(QStringLiteral("Caminho inválido."));
        return;
    }
    QString directory = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    if (directory.isEmpty()) {
        directory = QDir::homePath();
    }
    QDir().mkpath(directory);
    runAction(
        QStringLiteral("containers.files.download"),
        QJsonObject{{QStringLiteral("id"), id},
                    {QStringLiteral("path"), path},
                    {QStringLiteral("destination"), directory + QLatin1Char('/') + baseName}});
}

void ContainerController::uploadFile(const QString &id, const QString &path,
                                     const QString &sourcePath)
{
    if (id.isEmpty() || path.isEmpty() || sourcePath.isEmpty()) {
        setLastError(QStringLiteral("Informe o container, o destino e o arquivo local."));
        return;
    }
    runAction(QStringLiteral("containers.files.upload"),
              QJsonObject{{QStringLiteral("id"), id},
                          {QStringLiteral("path"), path},
                          {QStringLiteral("sourcePath"), sourcePath}});
}

void ContainerController::openTerminal(const QString &id)
{
    if (id.isEmpty()) {
        setLastError(QStringLiteral("Container inválido."));
        return;
    }

    QString title = id;
    for (const QVariant &value : items()) {
        const QVariantMap container = value.toMap();
        if (container.value(QStringLiteral("id")).toString() == id) {
            const QString name = container.value(QStringLiteral("name")).toString();
            if (!name.isEmpty()) {
                title = name;
            }
            break;
        }
    }

    const QStringList arguments{QStringLiteral("exec"), QStringLiteral("-it"), id,
                                QStringLiteral("/bin/sh")};
    if (isSignalConnected(QMetaMethod::fromSignal(&ContainerController::terminalRequested))) {
        emit terminalRequested(title, QStringLiteral("podman"), arguments);
        return;
    }
    auto *window = new TerminalWindow(id, QStringLiteral("podman"), arguments);
    window->show();
    window->raise();
    window->activateWindow();
}

void ContainerController::subscribeLogs(const QString &id, int tail, bool follow)
{
    m_logLines.clear();
    m_logsPending = false;
    emit logsChanged();
    m_stream.subscribeLogs(id, tail, follow);
}

void ContainerController::unsubscribeLogs()
{
    m_stream.unsubscribe();
}

void ContainerController::clearLogs()
{
    m_logLines.clear();
    emit logsChanged();
}

void ContainerController::subscribeStats(const QString &id)
{
    m_stats.clear();
    m_cpuHistory.clear();
    m_memHistory.clear();
    emit statsChanged();
    m_stream.subscribeStats(id);
}

void ContainerController::unsubscribeStats()
{
    m_stream.unsubscribe();
}

void ContainerController::onActionFinished(const QString &method, const QJsonObject &params,
                                           const QJsonObject &result)
{
    const auto setCode = [this](const QString &code) {
        if (m_lastErrorCode == code) {
            return;
        }
        m_lastErrorCode = code;
        emit lastErrorCodeChanged();
    };

    // O lote devolve `count`/`failed` (falha parcial não é falha do método):
    // trata antes do caminho genérico para emitir o código de erro por item.
    if (method == QStringLiteral("containers.batch")) {
        const int failed = result.value(QStringLiteral("failed")).toInt();
        const QString message = batchSummary(result);
        if (failed > 0) {
            setCode(QStringLiteral("PODSTACK_BATCH_PARTIAL"));
            setLastError(message);
            emit actionFailed(method, m_lastErrorCode, message);
        } else {
            setCode({});
            clearLastError();
        }
        refresh();
        emit catalogDirty();
        emit actionCompleted(method, QString(), failed == 0);
        return;
    }

    const QString targetId = params.value(QStringLiteral("id")).toString();

    const QJsonObject errorObject = result.value(QStringLiteral("__error")).isObject()
                                        ? result.value(QStringLiteral("__error")).toObject()
                                        : result.value(QStringLiteral("error")).toObject();
    setCode(errorObject.value(QStringLiteral("code")).toString());

    ControllerBase::onActionFinished(method, params, result);
    if (isErrorResult(result)) {
        emit actionFailed(method, m_lastErrorCode, errorMessage(result));
        emit actionCompleted(method, targetId, false);
        return;
    }
    refresh();
    emit catalogDirty();
    if (method == QStringLiteral("containers.create")) {
        const QString id = result.value(QStringLiteral("id")).toString();
        if (!id.isEmpty()) {
            emit containerCreated(id);
        }
    }
    if (method == QStringLiteral("containers.files.upload")) {
        // A listagem não recarregava sozinha: o arquivo enviado só aparecia no
        // próximo clique em Atualizar.
        const QString id = params.value(QStringLiteral("id")).toString();
        const QString path = params.value(QStringLiteral("path")).toString();
        if (!id.isEmpty() && !path.isEmpty()) {
            listFiles(id, path);
        }
    }
    emit actionCompleted(method, targetId, true);
}

} // namespace PodStack
