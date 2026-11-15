// SPDX-License-Identifier: GPL-3.0-or-later
#include "daemon/rpc/handlers/Common.h"

using namespace PodStack::DaemonInternal;

namespace PodStack {

QJsonObject Daemon::machineAction(const QString &method, const QJsonObject &params, Error *error)
{
    const QString id = requireMachineId(params, error);
    if (id.isEmpty()) {
        return {};
    }

    const bool force = params.value(QStringLiteral("force")).toBool();
    MachineMeta machine;
    if (method == QStringLiteral("machines.start")) {
        machine = m_machines.start(id, error);
    } else if (method == QStringLiteral("machines.stop")) {
        machine = m_machines.stop(id, force, error);
    } else if (force) {
        machine = m_machines.stop(id, true, error);
        if (error && !error->isEmpty()) {
            return {};
        }
        machine = m_machines.start(id, error);
    } else {
        machine = m_machines.restart(id, error);
    }

    if (machine.id.isEmpty() || (error && !error->isEmpty())) {
        return {};
    }
    return machine.toJson();
}

QJsonObject Daemon::machineAudioSet(const QJsonObject &params, Error *error)
{
    const QString id = requireMachineId(params, error);
    if (id.isEmpty()) {
        return {};
    }
    const QJsonValue enabledValue = params.value(QStringLiteral("enabled"));
    if (!enabledValue.isBool()) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_INVALID_PARAMS"),
                                 QStringLiteral("O campo enabled deve ser booleano."),
                                 QStringLiteral("enabled"));
        }
        return {};
    }
    const MachineMeta machine = m_machines.setAudioEnabled(id, enabledValue.toBool(), error);
    if (machine.id.isEmpty() || (error && !error->isEmpty())) {
        return {};
    }
    return machine.toJson();
}

QJsonObject Daemon::machineCancel(const QJsonObject &params, Error *error)
{
    const QString jobId = params.value(QStringLiteral("jobId")).toString().trimmed();
    if (jobId.isEmpty()) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_INVALID_PARAMS"),
                                 QStringLiteral("Informe o job da machine."));
        }
        return {};
    }

    QString subscriptionId;
    {
        std::lock_guard<std::mutex> lock(m_machineJobsMutex);
        subscriptionId = m_machineSubscriptions.value(jobId);
    }
    if (subscriptionId.isEmpty()) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_NOT_FOUND"),
                                 QStringLiteral("Job de machine não está em execução."), jobId);
        }
        return {};
    }

    std::shared_ptr<Subscription> subscription;
    {
        std::lock_guard<std::mutex> lock(m_subscriptionsMutex);
        subscription = m_subscriptions.value(subscriptionId);
    }
    if (!subscription) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_NOT_FOUND"),
                                 QStringLiteral("Job de machine não está em execução."), jobId);
        }
        return {};
    }

    subscription->cancel.store(true);
    QJsonObject result;
    result.insert(QStringLiteral("jobId"), jobId);
    result.insert(QStringLiteral("cancelled"), true);
    return result;
}

QJsonObject Daemon::machineClone(const QJsonObject &params, const Notify &notify, Error *error)
{
    const QString id = requireMachineId(params, error);
    if (id.isEmpty()) {
        return {};
    }

    const QString name = params.value(QStringLiteral("name")).toString().trimmed();
    if (name.isEmpty()) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_INVALID_PARAMS"),
                                 QStringLiteral("Informe o nome da machine clonada."));
        }
        return {};
    }

    QString jobId;
    {
        std::lock_guard<std::mutex> lock(m_machineJobsMutex);
        jobId = QStringLiteral("machine-clone-%1-%2")
                    .arg(QDateTime::currentSecsSinceEpoch())
                    .arg(m_nextMachineJob++);
    }

    auto subscription = std::make_shared<Subscription>();
    // Job: continua mesmo se o cliente que acompanha o progresso cair.
    subscription->cancelOnDisconnect = false;
    const QString subscriptionId = createSubscription(subscription);
    {
        std::lock_guard<std::mutex> lock(m_machineJobsMutex);
        m_machineSubscriptions.insert(jobId, subscriptionId);
    }

    (void) QtConcurrent::run([this, jobId, subscriptionId, subscription, id, name, notify] {
        const auto progress = [jobId, notify](const QString &stage, int value) {
            if (!notify) {
                return;
            }
            QJsonObject payload;
            payload.insert(QStringLiteral("jobId"), jobId);
            payload.insert(QStringLiteral("stage"), stage);
            payload.insert(QStringLiteral("progress"), value);
            notify(QStringLiteral("machines.delta"), payload);
        };
        const auto cancelled = [subscription] { return subscription->cancel.load(); };

        Error cloneError;
        MachineMeta machine = m_machines.clone(id, name, progress, cancelled, &cloneError);
        if (cloneError.isEmpty() && machine.id.isEmpty()) {
            cloneError = Error::make(QStringLiteral("PODSTACK_INTERNAL"),
                                     QStringLiteral("Falha ao clonar a machine."));
        }

        QJsonObject payload;
        payload.insert(QStringLiteral("jobId"), jobId);
        payload.insert(QStringLiteral("name"), name);
        if (cloneError.isEmpty()) {
            payload.insert(QStringLiteral("machine"), machine.toJson());
        } else {
            payload.insert(QStringLiteral("error"), cloneError.toJson());
        }
        if (notify) {
            notify(QStringLiteral("machines.done"), payload);
        }

        {
            std::lock_guard<std::mutex> lock(m_machineJobsMutex);
            m_machineSubscriptions.remove(jobId);
        }
        removeSubscription(subscriptionId);
    });

    QJsonObject result;
    result.insert(QStringLiteral("jobId"), jobId);
    result.insert(QStringLiteral("subscriptionId"), subscriptionId);
    return result;
}

QJsonObject Daemon::machineConsoleRead(const QJsonObject &params, Error *error)
{
    const QString id = requireMachineId(params, error);
    if (id.isEmpty()) {
        return {};
    }

    const MachineMeta machine = m_machines.get(id, error);
    if (machine.id.isEmpty()) {
        return {};
    }

    const QString directory = Paths::machineDir(machine.id);
    if (directory.isEmpty()) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_INTERNAL"),
                                 QStringLiteral("Identificador de machine inválido."));
        }
        return {};
    }

    bool consoleExists = false;
    if (!checkMachineConsoleFile(directory, &consoleExists, error)) {
        return {};
    }

    QByteArray data;
    if (consoleExists) {
        const QString path = directory + QStringLiteral("/console.log");
        if (!readMachineConsoleTail(path, consoleTailBytes(params), &data, nullptr, error)) {
            return {};
        }
    }

    QJsonObject result;
    result.insert(QStringLiteral("id"), machine.id);
    // O serial é binário; o stream expõe uma aproximação textual em UTF-8
    // (bytes inválidos viram U+FFFD).
    result.insert(QStringLiteral("data"), QString::fromUtf8(data));
    return result;
}

QJsonObject Daemon::machineCreate(const QJsonObject &params, const Notify &notify, Error *error)
{
    // Defaults de criação vêm de Configurações (machines.*) quando o cliente
    // não envia o parâmetro (ex.: CLI sem flags).
    const Config config = Config::load();

    CreateMachineRequest request;
    request.name = params.value(QStringLiteral("name")).toString().trimmed();
    request.distro = params.value(QStringLiteral("distro")).toString().trimmed();
    request.version = params.value(QStringLiteral("version")).toString().trimmed();
    request.arch = params.value(QStringLiteral("arch")).toString().trimmed();
    request.user = params.value(QStringLiteral("user")).toString();
    request.password = params.value(QStringLiteral("password")).toString();
    request.userData = params.value(QStringLiteral("userData")).toString();
    request.cpus = params.value(QStringLiteral("cpus"))
                       .toInt(config.get(QStringLiteral("machines.cpus")).toInt(request.cpus));
    request.memoryBytes =
        params.value(QStringLiteral("memoryBytes"))
            .toInteger(
                config.get(QStringLiteral("machines.memoryBytes")).toInteger(request.memoryBytes));
    request.diskBytes =
        params.value(QStringLiteral("diskBytes"))
            .toInteger(
                config.get(QStringLiteral("machines.diskBytes")).toInteger(request.diskBytes));
    request.isolateNetwork =
        params.value(QStringLiteral("isolateNetwork")).toBool(request.isolateNetwork);
    request.forwardSshAgent = params.value(QStringLiteral("forwardSshAgent"))
                                  .toBool(config.get(QStringLiteral("machines.forwardSshAgent"))
                                              .toBool(request.forwardSshAgent));
    request.isolated =
        params.value(QStringLiteral("isolated"))
            .toBool(config.get(QStringLiteral("machines.isolated")).toBool(request.isolated));

    const QJsonValue mountsValue = params.value(QStringLiteral("mounts"));
    if (!mountsValue.isUndefined() && !mountsValue.isNull()) {
        if (!mountsValue.isArray()) {
            if (error) {
                *error = Error::make(QStringLiteral("PODSTACK_INVALID_PARAMS"),
                                     QStringLiteral("mounts deve ser um array de objetos."));
            }
            return {};
        }
        const QJsonArray mounts = mountsValue.toArray();
        for (const QJsonValue &value : mounts) {
            if (!value.isObject()) {
                if (error) {
                    *error = Error::make(QStringLiteral("PODSTACK_INVALID_PARAMS"),
                                         QStringLiteral("Cada mount deve ser um objeto."));
                }
                return {};
            }
            const QJsonObject object = value.toObject();
            MachineMount mount;
            mount.source = object.value(QStringLiteral("source")).toString().trimmed();
            mount.target = object.value(QStringLiteral("target")).toString().trimmed();
            mount.readOnly = object.value(QStringLiteral("readOnly")).toBool(false);
            mount.tag = object.value(QStringLiteral("tag")).toString().trimmed();
            request.mounts.append(mount);
        }
    }

    if (!m_machines.validate(request, error)) {
        return {};
    }

    QString jobId;
    {
        std::lock_guard<std::mutex> lock(m_machineJobsMutex);
        jobId = QStringLiteral("machine-%1-%2")
                    .arg(QDateTime::currentSecsSinceEpoch())
                    .arg(m_nextMachineJob++);
    }

    auto subscription = std::make_shared<Subscription>();
    // Job: continua mesmo se o cliente que acompanha o progresso cair.
    subscription->cancelOnDisconnect = false;
    const QString subscriptionId = createSubscription(subscription);
    {
        std::lock_guard<std::mutex> lock(m_machineJobsMutex);
        m_machineSubscriptions.insert(jobId, subscriptionId);
    }

    (void) QtConcurrent::run([this, jobId, subscriptionId, subscription, request, notify] {
        const auto progress = [jobId, notify](const QString &stage, int value) {
            if (!notify) {
                return;
            }
            QJsonObject payload;
            payload.insert(QStringLiteral("jobId"), jobId);
            payload.insert(QStringLiteral("stage"), stage);
            payload.insert(QStringLiteral("progress"), value);
            notify(QStringLiteral("machines.delta"), payload);
        };
        const auto cancelled = [subscription] { return subscription->cancel.load(); };

        Error createError;
        MachineMeta machine = m_machines.create(request, progress, cancelled, &createError);
        if (createError.isEmpty() && machine.id.isEmpty()) {
            createError = Error::make(QStringLiteral("PODSTACK_INTERNAL"),
                                      QStringLiteral("Falha ao criar a machine."));
        }

        QJsonObject payload;
        payload.insert(QStringLiteral("jobId"), jobId);
        if (createError.isEmpty()) {
            payload.insert(QStringLiteral("machine"), machine.toJson());
        } else {
            payload.insert(QStringLiteral("error"), createError.toJson());
        }
        if (notify) {
            notify(QStringLiteral("machines.done"), payload);
        }

        {
            std::lock_guard<std::mutex> lock(m_machineJobsMutex);
            m_machineSubscriptions.remove(jobId);
        }
        removeSubscription(subscriptionId);
    });

    QJsonObject result;
    result.insert(QStringLiteral("jobId"), jobId);
    result.insert(QStringLiteral("subscriptionId"), subscriptionId);
    return result;
}

QJsonObject Daemon::machineDefault(const QJsonObject &params, Error *error)
{
    const QJsonValue nameValue = params.value(QStringLiteral("name"));
    if (!nameValue.isUndefined() && !nameValue.isNull()) {
        if (!nameValue.isString() || nameValue.toString().trimmed().isEmpty()) {
            if (error) {
                *error = Error::make(QStringLiteral("PODSTACK_INVALID_PARAMS"),
                                     QStringLiteral("O nome da machine default é inválido."));
            }
            return {};
        }
        if (!m_machines.setDefaultMachine(nameValue.toString().trimmed(), error)) {
            return {};
        }
    }

    const QString name = m_machines.defaultMachine(error);
    if (error && !error->isEmpty()) {
        return {};
    }
    return QJsonObject{{QStringLiteral("name"), name}};
}

QJsonObject Daemon::machineDelete(const QJsonObject &params, Error *error)
{
    const QString id = requireMachineId(params, error);
    if (id.isEmpty()) {
        return {};
    }

    const bool force = params.value(QStringLiteral("force")).toBool();
    if (!m_machines.remove(id, force, error)) {
        return {};
    }
    return QJsonObject{{QStringLiteral("deleted"), true}};
}

QJsonObject Daemon::machineExport(const QJsonObject &params, const Notify &notify, Error *error)
{
    const QString id = requireMachineId(params, error);
    if (id.isEmpty()) {
        return {};
    }

    const QString path = params.value(QStringLiteral("path")).toString().trimmed();
    if (path.isEmpty()) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_INVALID_PARAMS"),
                                 QStringLiteral("Informe o arquivo de destino do export."));
        }
        return {};
    }
    if (!QFileInfo(path).isAbsolute()) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_INVALID_PARAMS"),
                                 QStringLiteral("O arquivo de export precisa ser absoluto."), path);
        }
        return {};
    }
    const bool force = params.value(QStringLiteral("force")).toBool();

    QString jobId;
    {
        std::lock_guard<std::mutex> lock(m_machineJobsMutex);
        jobId = QStringLiteral("machine-export-%1-%2")
                    .arg(QDateTime::currentSecsSinceEpoch())
                    .arg(m_nextMachineJob++);
    }

    auto subscription = std::make_shared<Subscription>();
    // Job: continua mesmo se o cliente que acompanha o progresso cair.
    subscription->cancelOnDisconnect = false;
    const QString subscriptionId = createSubscription(subscription);
    {
        std::lock_guard<std::mutex> lock(m_machineJobsMutex);
        m_machineSubscriptions.insert(jobId, subscriptionId);
    }

    (void) QtConcurrent::run([this, jobId, subscriptionId, subscription, id, path, force, notify] {
        const auto progress = [jobId, notify](const QString &stage, int value) {
            if (!notify) {
                return;
            }
            QJsonObject payload;
            payload.insert(QStringLiteral("jobId"), jobId);
            payload.insert(QStringLiteral("stage"), stage);
            payload.insert(QStringLiteral("progress"), value);
            notify(QStringLiteral("machines.delta"), payload);
        };
        const auto cancelled = [subscription] { return subscription->cancel.load(); };

        Error exportError;
        MachineMeta machine =
            m_machines.exportMachine(id, path, force, progress, cancelled, &exportError);
        if (exportError.isEmpty() && machine.id.isEmpty()) {
            exportError = Error::make(QStringLiteral("PODSTACK_INTERNAL"),
                                      QStringLiteral("Falha ao exportar a machine."));
        }

        QJsonObject payload;
        payload.insert(QStringLiteral("jobId"), jobId);
        payload.insert(QStringLiteral("path"), path);
        if (exportError.isEmpty()) {
            payload.insert(QStringLiteral("machine"), machine.toJson());
        } else {
            payload.insert(QStringLiteral("error"), exportError.toJson());
        }
        if (notify) {
            notify(QStringLiteral("machines.done"), payload);
        }

        {
            std::lock_guard<std::mutex> lock(m_machineJobsMutex);
            m_machineSubscriptions.remove(jobId);
        }
        removeSubscription(subscriptionId);
    });

    QJsonObject result;
    result.insert(QStringLiteral("jobId"), jobId);
    result.insert(QStringLiteral("subscriptionId"), subscriptionId);
    return result;
}

QJsonObject Daemon::machineFilesDownload(const QJsonObject &params, Error *error)
{
    const QString id = requireMachineId(params, error);
    if (id.isEmpty()) {
        return {};
    }

    const MachineMeta machine = m_machines.get(id, error);
    if (machine.id.isEmpty()) {
        return {};
    }

    SshInfo info;
    if (!m_machines.sshInfo(machine.id, &info, error)) {
        return {};
    }

    const QString path = params.value(QStringLiteral("path")).toString();
    const QString destination = params.value(QStringLiteral("destination")).toString();
    if (!MachineFiles::download(info, path, destination, error)) {
        return {};
    }

    QJsonObject result;
    result.insert(QStringLiteral("path"), path);
    result.insert(QStringLiteral("destination"), destination);
    return result;
}

QJsonObject Daemon::machineFilesList(const QJsonObject &params, Error *error)
{
    const QString id = requireMachineId(params, error);
    if (id.isEmpty()) {
        return {};
    }

    const MachineMeta machine = m_machines.get(id, error);
    if (machine.id.isEmpty()) {
        return {};
    }

    SshInfo info;
    if (!m_machines.sshInfo(machine.id, &info, error)) {
        return {};
    }

    const QString path = params.value(QStringLiteral("path")).toString(QStringLiteral("/"));
    QJsonArray items;
    if (!MachineFiles::list(info, path, &items, error)) {
        return {};
    }

    QJsonObject result;
    result.insert(QStringLiteral("path"), path);
    result.insert(QStringLiteral("items"), items);
    return result;
}

QJsonObject Daemon::machineFilesUpload(const QJsonObject &params, Error *error)
{
    const QString id = requireMachineId(params, error);
    if (id.isEmpty()) {
        return {};
    }

    const MachineMeta machine = m_machines.get(id, error);
    if (machine.id.isEmpty()) {
        return {};
    }

    SshInfo info;
    if (!m_machines.sshInfo(machine.id, &info, error)) {
        return {};
    }

    const QString path = params.value(QStringLiteral("path")).toString();
    const QString sourcePath = params.value(QStringLiteral("sourcePath")).toString();
    if (!MachineFiles::upload(info, sourcePath, path, error)) {
        return {};
    }

    QJsonObject result;
    result.insert(QStringLiteral("path"), path);
    result.insert(QStringLiteral("sourcePath"), sourcePath);
    result.insert(QStringLiteral("ok"), true);
    return result;
}

QJsonObject Daemon::machineGet(const QJsonObject &params, Error *error)
{
    const QString id = requireMachineId(params, error);
    if (id.isEmpty()) {
        return {};
    }

    const MachineMeta machine = m_machines.get(id, error);
    if (machine.id.isEmpty()) {
        return {};
    }
    return machine.toJson();
}

QJsonObject Daemon::machineImport(const QJsonObject &params, const Notify &notify, Error *error)
{
    const QString path = params.value(QStringLiteral("path")).toString().trimmed();
    if (path.isEmpty()) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_INVALID_PARAMS"),
                                 QStringLiteral("Informe o arquivo de machine para importar."));
        }
        return {};
    }
    if (!QFileInfo(path).isAbsolute()) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_INVALID_PARAMS"),
                                 QStringLiteral("O arquivo de import precisa ser absoluto."), path);
        }
        return {};
    }
    const QString name = params.value(QStringLiteral("name")).toString().trimmed();
    const bool force = params.value(QStringLiteral("force")).toBool();

    QString jobId;
    {
        std::lock_guard<std::mutex> lock(m_machineJobsMutex);
        jobId = QStringLiteral("machine-import-%1-%2")
                    .arg(QDateTime::currentSecsSinceEpoch())
                    .arg(m_nextMachineJob++);
    }

    auto subscription = std::make_shared<Subscription>();
    // Job: continua mesmo se o cliente que acompanha o progresso cair.
    subscription->cancelOnDisconnect = false;
    const QString subscriptionId = createSubscription(subscription);
    {
        std::lock_guard<std::mutex> lock(m_machineJobsMutex);
        m_machineSubscriptions.insert(jobId, subscriptionId);
    }

    (void) QtConcurrent::run(
        [this, jobId, subscriptionId, subscription, path, name, force, notify] {
            const auto progress = [jobId, notify](const QString &stage, int value) {
                if (!notify) {
                    return;
                }
                QJsonObject payload;
                payload.insert(QStringLiteral("jobId"), jobId);
                payload.insert(QStringLiteral("stage"), stage);
                payload.insert(QStringLiteral("progress"), value);
                notify(QStringLiteral("machines.delta"), payload);
            };
            const auto cancelled = [subscription] { return subscription->cancel.load(); };

            Error importError;
            MachineMeta machine =
                m_machines.importMachine(path, name, force, progress, cancelled, &importError);
            if (importError.isEmpty() && machine.id.isEmpty()) {
                importError = Error::make(QStringLiteral("PODSTACK_INTERNAL"),
                                          QStringLiteral("Falha ao importar a machine."));
            }

            QJsonObject payload;
            payload.insert(QStringLiteral("jobId"), jobId);
            payload.insert(QStringLiteral("path"), path);
            if (importError.isEmpty()) {
                payload.insert(QStringLiteral("machine"), machine.toJson());
            } else {
                payload.insert(QStringLiteral("error"), importError.toJson());
            }
            if (notify) {
                notify(QStringLiteral("machines.done"), payload);
            }

            {
                std::lock_guard<std::mutex> lock(m_machineJobsMutex);
                m_machineSubscriptions.remove(jobId);
            }
            removeSubscription(subscriptionId);
        });

    QJsonObject result;
    result.insert(QStringLiteral("jobId"), jobId);
    result.insert(QStringLiteral("subscriptionId"), subscriptionId);
    return result;
}

QJsonObject Daemon::machineLogs(const QJsonObject &params, Error *error)
{
    const QString id = requireMachineId(params, error);
    if (id.isEmpty()) {
        return {};
    }

    const MachineMeta machine = m_machines.get(id, error);
    if (machine.id.isEmpty()) {
        return {};
    }

    int tail = params.value(QStringLiteral("tail")).toInt(200);
    if (tail <= 0) {
        tail = 200;
    }
    tail = qBound(1, tail, 2000);

    const QString directory = Paths::machineDir(machine.id);
    if (directory.isEmpty()) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_INTERNAL"),
                                 QStringLiteral("Identificador de machine inválido."));
        }
        return {};
    }

    bool consoleExists = false;
    if (!checkMachineConsoleFile(directory, &consoleExists, error)) {
        return {};
    }
    if (!consoleExists) {
        return QJsonObject{{QStringLiteral("lines"), QJsonArray{}}};
    }

    const QString path = directory + QStringLiteral("/console.log");
    QByteArray consoleData;
    qint64 start = 0;
    if (!readMachineConsoleTail(path, kMachineConsoleMaxBytes, &consoleData, &start, error)) {
        return {};
    }
    start = qMax<qint64>(0, start - consoleData.size());

    QString text = QString::fromUtf8(consoleData);
    if (start > 0) {
        const int newline = text.indexOf(QLatin1Char('\n'));
        if (newline >= 0) {
            text.remove(0, newline + 1);
        } else {
            text.clear();
        }
    }

    QStringList lines = text.split(QLatin1Char('\n'));
    if (!lines.isEmpty() && lines.last().isEmpty()) {
        lines.removeLast();
    }
    for (QString &line : lines) {
        if (line.endsWith(QLatin1Char('\r'))) {
            line.chop(1);
        }
    }
    if (lines.size() > tail) {
        lines = lines.mid(lines.size() - tail);
    }

    QJsonArray array;
    for (const QString &line : lines) {
        array.append(line);
    }
    return QJsonObject{{QStringLiteral("lines"), array}};
}

QJsonObject Daemon::machinePortsList(const QJsonObject &params, Error *error)
{
    const QString id = requireMachineId(params, error);
    if (id.isEmpty()) {
        return {};
    }

    const MachineMeta machine = m_machines.get(id, error);
    if (machine.id.isEmpty() || (error && !error->isEmpty())) {
        return {};
    }
    return QJsonObject{
        {QStringLiteral("items"), machine.toJson().value(QStringLiteral("ports")).toArray()}};
}

QJsonObject Daemon::machinePortsSet(const QJsonObject &params, Error *error)
{
    const QString id = requireMachineId(params, error);
    if (id.isEmpty()) {
        return {};
    }

    const QJsonValue portsValue = params.value(QStringLiteral("ports"));
    if (!portsValue.isArray()) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_INVALID_PARAMS"),
                                 QStringLiteral("O campo ports deve ser um array."),
                                 QStringLiteral("Envie a lista completa de portas não-SSH."));
        }
        return {};
    }

    QVector<MachinePort> ports;
    const QJsonArray array = portsValue.toArray();
    ports.reserve(array.size());
    for (int index = 0; index < array.size(); ++index) {
        const QJsonValue entry = array.at(index);
        if (!entry.isObject()) {
            if (error) {
                *error = Error::make(QStringLiteral("PODSTACK_INVALID_PARAMS"),
                                     QStringLiteral("Cada item de ports deve ser um objeto."),
                                     QStringLiteral("ports[%1]").arg(index));
            }
            return {};
        }
        const QJsonObject object = entry.toObject();
        const auto invalidField = [error, index](const QString &field, const QString &expected) {
            if (error) {
                *error = Error::make(
                    QStringLiteral("PODSTACK_INVALID_PARAMS"),
                    QStringLiteral("Campo inválido em ports."),
                    QStringLiteral("ports[%1].%2 (esperado %3)").arg(index).arg(field, expected));
            }
            return QJsonObject{};
        };

        MachinePort port;
        const QJsonValue idValue = object.value(QStringLiteral("id"));
        if (!idValue.isUndefined() && !idValue.isNull()) {
            if (!idValue.isString()) {
                return invalidField(QStringLiteral("id"), QStringLiteral("string"));
            }
            port.id = idValue.toString();
        }
        if (!portNumberFromJson(object.value(QStringLiteral("hostPort")), &port.hostPort)) {
            return invalidField(QStringLiteral("hostPort"), QStringLiteral("inteiro 1..65535"));
        }
        if (!portNumberFromJson(object.value(QStringLiteral("guestPort")), &port.guestPort)) {
            return invalidField(QStringLiteral("guestPort"), QStringLiteral("inteiro 1..65535"));
        }

        const QJsonValue protocolValue = object.value(QStringLiteral("protocol"));
        if (!protocolValue.isUndefined() && !protocolValue.isNull()) {
            if (!protocolValue.isString()) {
                return invalidField(QStringLiteral("protocol"), QStringLiteral("string tcp|udp"));
            }
            port.protocol = protocolValue.toString();
        }
        const QJsonValue exposeValue = object.value(QStringLiteral("exposeLan"));
        if (!exposeValue.isUndefined() && !exposeValue.isNull()) {
            if (!exposeValue.isBool()) {
                return invalidField(QStringLiteral("exposeLan"), QStringLiteral("boolean"));
            }
            port.exposeLan = exposeValue.toBool();
        }
        const QJsonValue labelValue = object.value(QStringLiteral("label"));
        if (!labelValue.isUndefined() && !labelValue.isNull()) {
            if (!labelValue.isString()) {
                return invalidField(QStringLiteral("label"), QStringLiteral("string"));
            }
            port.label = labelValue.toString();
        }
        ports.append(port);
    }

    const MachineMeta machine = m_machines.setPorts(id, ports, error);
    if (machine.id.isEmpty() || (error && !error->isEmpty())) {
        return {};
    }
    return machine.toJson();
}

QJsonObject Daemon::machineRename(const QJsonObject &params, Error *error)
{
    const QString id = requireMachineId(params, error);
    if (id.isEmpty()) {
        return {};
    }

    const QString name = params.value(QStringLiteral("name")).toString().trimmed();
    if (name.isEmpty()) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_INVALID_PARAMS"),
                                 QStringLiteral("Informe o novo nome da machine."));
        }
        return {};
    }

    const MachineMeta machine = m_machines.rename(id, name, error);
    if (machine.id.isEmpty() || (error && !error->isEmpty())) {
        return {};
    }
    return machine.toJson();
}

QJsonObject Daemon::machinesDistros()
{
    QJsonArray items;
    const QVector<DistroImage> images = DistroRegistry::images();
    for (const DistroImage &image : images) {
        QJsonObject item;
        item.insert(QStringLiteral("distro"), image.distro);
        item.insert(QStringLiteral("version"), image.version);
        item.insert(QStringLiteral("arch"), image.arch);
        item.insert(QStringLiteral("default"), image.isDefault);
        item.insert(QStringLiteral("url"), image.url);
        item.insert(QStringLiteral("checksum"), image.checksum);
        item.insert(QStringLiteral("algorithm"), DistroRegistry::algorithmName(image.algorithm));
        item.insert(QStringLiteral("size"), static_cast<double>(image.size));
        item.insert(QStringLiteral("cached"), m_cloudImages.isCached(image));
        items.append(item);
    }

    QJsonObject result;
    result.insert(QStringLiteral("items"), items);
    return result;
}

QJsonObject Daemon::machineSerialAttach(const QJsonObject &params, Error *error)
{
    const QString id = requireMachineId(params, error);
    if (id.isEmpty()) {
        return {};
    }
    const QString path = params.value(QStringLiteral("path")).toString();
    if (path.isEmpty()) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_INVALID_PARAMS"),
                                 QStringLiteral("Informe o caminho da porta serial."),
                                 QStringLiteral("path"));
        }
        return {};
    }

    const MachineMeta current = m_machines.get(id, error);
    if (current.id.isEmpty() || (error && !error->isEmpty())) {
        return {};
    }
    QStringList paths = current.serialPorts;
    if (!paths.contains(path)) {
        paths.append(path);
    }
    const MachineMeta machine = m_machines.setSerialPorts(id, paths, error);
    if (machine.id.isEmpty() || (error && !error->isEmpty())) {
        return {};
    }
    return machine.toJson();
}

QJsonObject Daemon::machineSerialDetach(const QJsonObject &params, Error *error)
{
    const QString id = requireMachineId(params, error);
    if (id.isEmpty()) {
        return {};
    }
    const QString path = params.value(QStringLiteral("path")).toString();
    if (path.isEmpty()) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_INVALID_PARAMS"),
                                 QStringLiteral("Informe o caminho da porta serial."),
                                 QStringLiteral("path"));
        }
        return {};
    }

    const MachineMeta current = m_machines.get(id, error);
    if (current.id.isEmpty() || (error && !error->isEmpty())) {
        return {};
    }
    if (!current.serialPorts.contains(path)) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_NOT_FOUND"),
                                 QStringLiteral("A porta serial não está anexada."), path);
        }
        return {};
    }
    QStringList paths = current.serialPorts;
    paths.removeAll(path);
    const MachineMeta machine = m_machines.setSerialPorts(id, paths, error);
    if (machine.id.isEmpty() || (error && !error->isEmpty())) {
        return {};
    }
    return machine.toJson();
}

QJsonObject Daemon::machineSerialList(const QJsonObject &params, Error *error)
{
    const QString id = requireMachineId(params, error);
    if (id.isEmpty()) {
        return {};
    }
    const MachineMeta machine = m_machines.get(id, error);
    if (machine.id.isEmpty() || (error && !error->isEmpty())) {
        return {};
    }

    QJsonArray ports;
    for (const SerialPortInfo &info : listHostSerialPorts()) {
        ports.append(info.toJson());
    }
    QJsonArray attached;
    for (const QString &path : machine.serialPorts) {
        attached.append(path);
    }
    QJsonObject machineJson;
    machineJson.insert(QStringLiteral("id"), machine.id);
    machineJson.insert(QStringLiteral("name"), machine.name);
    machineJson.insert(QStringLiteral("serialPorts"), attached);
    return QJsonObject{{QStringLiteral("ports"), ports}, {QStringLiteral("machine"), machineJson}};
}

QJsonObject Daemon::machinesList(Error *error)
{
    const QVector<MachineMeta> machines = m_machines.list(error);
    if (error && !error->isEmpty()) {
        return {};
    }

    QJsonArray items;
    for (const MachineMeta &machine : machines) {
        items.append(machine.toJson());
    }
    return QJsonObject{{QStringLiteral("items"), items}};
}

QJsonObject Daemon::machineSnapshotCreate(const QJsonObject &params, Error *error)
{
    const QString id = requireMachineId(params, error);
    if (id.isEmpty()) {
        return {};
    }

    const QString name = params.value(QStringLiteral("name")).toString().trimmed();
    if (name.isEmpty()) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_INVALID_PARAMS"),
                                 QStringLiteral("Informe o nome do snapshot."));
        }
        return {};
    }

    const MachineSnapshot snapshot = m_machines.createSnapshot(id, name, error);
    if (error && !error->isEmpty()) {
        return {};
    }
    return snapshot.toJson();
}

QJsonObject Daemon::machineSnapshotRemove(const QJsonObject &params, Error *error)
{
    const QString id = requireMachineId(params, error);
    if (id.isEmpty()) {
        return {};
    }

    const QString name = params.value(QStringLiteral("name")).toString().trimmed();
    if (name.isEmpty()) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_INVALID_PARAMS"),
                                 QStringLiteral("Informe o nome do snapshot."));
        }
        return {};
    }

    if (!m_machines.removeSnapshot(id, name, error)) {
        return {};
    }
    return QJsonObject{{QStringLiteral("removed"), true}};
}

QJsonObject Daemon::machineSnapshotRestore(const QJsonObject &params, Error *error)
{
    const QString id = requireMachineId(params, error);
    if (id.isEmpty()) {
        return {};
    }

    const QString name = params.value(QStringLiteral("name")).toString().trimmed();
    if (name.isEmpty()) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_INVALID_PARAMS"),
                                 QStringLiteral("Informe o nome do snapshot."));
        }
        return {};
    }

    const MachineMeta machine = m_machines.restoreSnapshot(id, name, error);
    if (machine.id.isEmpty() || (error && !error->isEmpty())) {
        return {};
    }
    return machine.toJson();
}

QJsonObject Daemon::machineSnapshotsList(const QJsonObject &params, Error *error)
{
    const QString id = requireMachineId(params, error);
    if (id.isEmpty()) {
        return {};
    }

    const QVector<MachineSnapshot> snapshots = m_machines.snapshots(id, error);
    if (error && !error->isEmpty()) {
        return {};
    }

    QJsonArray items;
    for (const MachineSnapshot &snapshot : snapshots) {
        items.append(snapshot.toJson());
    }
    return QJsonObject{{QStringLiteral("items"), items}};
}

QJsonObject Daemon::machineSsh(const QJsonObject &params, Error *error)
{
    const QString id = requireMachineId(params, error);
    if (id.isEmpty()) {
        return {};
    }

    SshInfo info;
    if (!m_machines.sshInfo(id, &info, error)) {
        return {};
    }

    QJsonObject result;
    result.insert(QStringLiteral("host"), info.host);
    result.insert(QStringLiteral("port"), info.port);
    result.insert(QStringLiteral("user"), info.user);
    result.insert(QStringLiteral("identityFile"), info.identityFile);
    result.insert(QStringLiteral("forwardAgent"), info.forwardAgent);
    return result;
}

QJsonObject Daemon::machineStats(const QJsonObject &params, Error *error)
{
    const QString id = requireMachineId(params, error);
    if (id.isEmpty()) {
        return {};
    }

    const MachineMeta machine = m_machines.get(id, error);
    if (machine.id.isEmpty()) {
        return {};
    }

    SshInfo info;
    if (!m_machines.sshInfo(machine.id, &info, error)) {
        return {};
    }
    return m_machineStats.sample(machine, info, error);
}

QJsonObject Daemon::machineUpdate(const QJsonObject &params, Error *error)
{
    const QString id = requireMachineId(params, error);
    if (id.isEmpty()) {
        return {};
    }

    UpdateMachineRequest request;
    const QJsonValue cpusValue = params.value(QStringLiteral("cpus"));
    if (!cpusValue.isUndefined() && !cpusValue.isNull()) {
        request.cpus = cpusValue.toInt();
    }
    const QJsonValue memoryValue = params.value(QStringLiteral("memoryBytes"));
    if (!memoryValue.isUndefined() && !memoryValue.isNull()) {
        request.memoryBytes = memoryValue.toInteger();
    }
    const QJsonValue diskValue = params.value(QStringLiteral("diskBytes"));
    if (!diskValue.isUndefined() && !diskValue.isNull()) {
        request.diskBytes = diskValue.toInteger();
    }
    const QJsonValue forwardValue = params.value(QStringLiteral("forwardSshAgent"));
    if (!forwardValue.isUndefined() && !forwardValue.isNull()) {
        if (!forwardValue.isBool()) {
            if (error) {
                *error = Error::make(QStringLiteral("PODSTACK_INVALID_PARAMS"),
                                     QStringLiteral("forwardSshAgent deve ser booleano."));
            }
            return {};
        }
        request.forwardSshAgent = forwardValue.toBool();
    }

    const MachineMeta machine = m_machines.update(id, request, error);
    if (machine.id.isEmpty() || (error && !error->isEmpty())) {
        return {};
    }
    return machine.toJson();
}

QJsonObject Daemon::machineUsbAttach(const QJsonObject &params, Error *error)
{
    const QString id = requireMachineId(params, error);
    if (id.isEmpty()) {
        return {};
    }
    const int bus = params.value(QStringLiteral("bus")).toInt();
    const int address = params.value(QStringLiteral("address")).toInt();
    if (bus <= 0 || address <= 0) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_INVALID_PARAMS"),
                                 QStringLiteral("Informe bus e address do dispositivo USB."),
                                 QStringLiteral("bus/address"));
        }
        return {};
    }

    const MachineMeta current = m_machines.get(id, error);
    if (current.id.isEmpty() || (error && !error->isEmpty())) {
        return {};
    }
    QVector<MachineUsbDevice> devices = current.usbDevices;
    bool exists = false;
    for (const MachineUsbDevice &device : devices) {
        if (device.bus == bus && device.address == address) {
            exists = true;
            break;
        }
    }
    if (!exists) {
        devices.append(MachineUsbDevice{bus, address, {}, {}, {}});
    }
    const MachineMeta machine = m_machines.setUsbDevices(id, devices, error);
    if (machine.id.isEmpty() || (error && !error->isEmpty())) {
        return {};
    }
    return machine.toJson();
}

QJsonObject Daemon::machineUsbDetach(const QJsonObject &params, Error *error)
{
    const QString id = requireMachineId(params, error);
    if (id.isEmpty()) {
        return {};
    }
    const int bus = params.value(QStringLiteral("bus")).toInt();
    const int address = params.value(QStringLiteral("address")).toInt();
    if (bus <= 0 || address <= 0) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_INVALID_PARAMS"),
                                 QStringLiteral("Informe bus e address do dispositivo USB."),
                                 QStringLiteral("bus/address"));
        }
        return {};
    }

    const MachineMeta current = m_machines.get(id, error);
    if (current.id.isEmpty() || (error && !error->isEmpty())) {
        return {};
    }
    QVector<MachineUsbDevice> devices;
    bool found = false;
    for (const MachineUsbDevice &device : current.usbDevices) {
        if (device.bus == bus && device.address == address) {
            found = true;
            continue;
        }
        devices.append(device);
    }
    if (!found) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_NOT_FOUND"),
                                 QStringLiteral("O dispositivo USB não está anexado."),
                                 QStringLiteral("%1:%2").arg(bus).arg(address));
        }
        return {};
    }
    const MachineMeta machine = m_machines.setUsbDevices(id, devices, error);
    if (machine.id.isEmpty() || (error && !error->isEmpty())) {
        return {};
    }
    return machine.toJson();
}

QJsonObject Daemon::machineUsbList(const QJsonObject &params, Error *error)
{
    const QString id = requireMachineId(params, error);
    if (id.isEmpty()) {
        return {};
    }
    const MachineMeta machine = m_machines.get(id, error);
    if (machine.id.isEmpty() || (error && !error->isEmpty())) {
        return {};
    }

    QJsonArray devices;
    for (const UsbDeviceInfo &info : listHostUsbDevices()) {
        devices.append(info.toJson());
    }
    QJsonArray attached;
    for (const MachineUsbDevice &device : machine.usbDevices) {
        attached.append(device.toJson());
    }
    QJsonObject machineJson;
    machineJson.insert(QStringLiteral("id"), machine.id);
    machineJson.insert(QStringLiteral("name"), machine.name);
    machineJson.insert(QStringLiteral("usbDevices"), attached);
    return QJsonObject{{QStringLiteral("devices"), devices},
                       {QStringLiteral("machine"), machineJson}};
}

QJsonObject Daemon::subscribeMachineConsole(const QJsonObject &params, const Notify &notify,
                                            Error *error)
{
    if (error) {
        *error = {};
    }

    const QString id = requireMachineId(params, error);
    if (id.isEmpty()) {
        return {};
    }

    const MachineMeta machine = m_machines.get(id, error);
    if (machine.id.isEmpty()) {
        return {};
    }

    const QString directory = Paths::machineDir(machine.id);
    if (directory.isEmpty()) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_INTERNAL"),
                                 QStringLiteral("Identificador de machine inválido."));
        }
        return {};
    }

    bool consoleExists = false;
    if (!checkMachineConsoleFile(directory, &consoleExists, error)) {
        return {};
    }

    const QString path = directory + QStringLiteral("/console.log");
    QByteArray initial;
    qint64 offset = 0;
    if (consoleExists &&
        !readMachineConsoleTail(path, consoleTailBytes(params), &initial, &offset, error)) {
        return {};
    }
    bool pendingReset = !consoleExists;

    auto subscription = std::make_shared<Subscription>();
    const QString subscriptionId = createSubscription(subscription);

    (void) QtConcurrent::run([this, machineId = machine.id, path, offset, pendingReset, initial,
                              subscriptionId, subscription, notify]() mutable {
        // O serial entrega bytes crus; um caractere UTF-8 pode ser dividido
        // entre dois chunks. O decoder mantém o estado entre deltas para não
        // produzir U+FFFD no meio de uma sequência.
        QStringDecoder decoder(QStringDecoder::Utf8);
        const auto sendDelta = [&](const QByteArray &bytes, bool reset) {
            if (reset) {
                decoder = QStringDecoder(QStringDecoder::Utf8);
            }
            QJsonObject payload;
            payload.insert(QStringLiteral("subscriptionId"), subscriptionId);
            payload.insert(QStringLiteral("id"), machineId);
            payload.insert(QStringLiteral("data"), QString(decoder(bytes)));
            payload.insert(QStringLiteral("reset"), reset);
            notify(QStringLiteral("machines.console.delta"), payload);
        };
        const auto sendError = [&](const Error &streamError) {
            QJsonObject payload;
            payload.insert(QStringLiteral("subscriptionId"), subscriptionId);
            payload.insert(QStringLiteral("id"), machineId);
            payload.insert(QStringLiteral("error"), streamError.toJson());
            notify(QStringLiteral("machines.console.error"), payload);
        };

        sendDelta(initial, true);

        while (!subscription->cancel.load()) {
            for (int tick = 0; tick < 3; ++tick) {
                if (subscription->cancel.load()) {
                    break;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            if (subscription->cancel.load()) {
                break;
            }

            bool exists = false;
            Error checkError;
            if (!checkMachineConsoleFile(QFileInfo(path).absolutePath(), &exists, &checkError)) {
                sendError(checkError);
                break;
            }
            if (!exists) {
                offset = 0;
                pendingReset = true;
                continue;
            }

            QByteArray chunk;
            qint64 nextOffset = offset;
            Error readError;
            if (!readMachineConsoleChunk(path, offset, kMachineConsoleMaxBytes, &chunk, &nextOffset,
                                         &readError)) {
                sendError(readError);
                break;
            }
            if (nextOffset < offset) {
                offset = 0;
                pendingReset = true;
                continue;
            }
            if (chunk.isEmpty()) {
                continue;
            }

            const bool reset = pendingReset;
            pendingReset = false;
            offset = nextOffset;
            sendDelta(chunk, reset);
        }

        removeSubscription(subscriptionId);
    });

    QJsonObject result;
    result.insert(QStringLiteral("subscriptionId"), subscriptionId);
    result.insert(QStringLiteral("id"), machine.id);
    return result;
}

QJsonObject Daemon::subscribeMachineStats(const QJsonObject &params, const Notify &notify,
                                          Error *error)
{
    if (error) {
        *error = {};
    }

    const QString id = requireMachineId(params, error);
    if (id.isEmpty()) {
        return {};
    }

    const MachineMeta machine = m_machines.get(id, error);
    if (machine.id.isEmpty()) {
        return {};
    }

    SshInfo info;
    if (!m_machines.sshInfo(machine.id, &info, error)) {
        return {};
    }

    const int interval = qBound(1, params.value(QStringLiteral("intervalSeconds")).toInt(2), 10);
    auto subscription = std::make_shared<Subscription>();
    const QString subscriptionId = createSubscription(subscription);

    (void) QtConcurrent::run([this, machine, info, interval, subscriptionId, subscription, notify] {
        MachineStatsCollector collector;
        while (!subscription->cancel.load()) {
            Error sampleError;
            QJsonObject sample = collector.sample(machine, info, &sampleError);
            if (!sample.isEmpty()) {
                sample.insert(QStringLiteral("subscriptionId"), subscriptionId);
                sample.insert(QStringLiteral("id"), machine.id);
                notify(QStringLiteral("machines.stats.delta"), sample);
            } else if (!sampleError.isEmpty()) {
                QJsonObject payload;
                payload.insert(QStringLiteral("subscriptionId"), subscriptionId);
                payload.insert(QStringLiteral("id"), machine.id);
                payload.insert(QStringLiteral("error"), sampleError.toJson());
                notify(QStringLiteral("machines.stats.error"), payload);
                break;
            }

            for (int tick = 0; tick < interval * 10; ++tick) {
                if (subscription->cancel.load()) {
                    break;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        }
        removeSubscription(subscriptionId);
    });

    QJsonObject result;
    result.insert(QStringLiteral("subscriptionId"), subscriptionId);
    result.insert(QStringLiteral("id"), machine.id);
    return result;
}

} // namespace PodStack
