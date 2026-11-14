// SPDX-License-Identifier: GPL-3.0-or-later
#include "machines/manager/Common.h"

using namespace PodStack::MachineInternal;

namespace PodStack {

QVector<MachineSnapshot> MachineManager::snapshots(const QString &idOrName, Error *error)
{
    if (error) {
        *error = {};
    }

    MachineMeta meta = load(idOrName, error);
    if (meta.id.isEmpty()) {
        return {};
    }

    std::shared_ptr<std::recursive_mutex> lockHolder;
    std::unique_lock<std::recursive_mutex> machineGuard;
    if (!tryLockMachine(meta.id, &lockHolder, &machineGuard)) {
        fails(error, Error::make(QStringLiteral("PODSTACK_CONFLICT"),
                                 QStringLiteral("A machine está em outra operação."), meta.id,
                                 QStringLiteral("Aguarde a operação atual terminar.")));
        return {};
    }

    return parseSnapshots(meta, error);
}

QVector<MachineSnapshot> MachineManager::parseSnapshots(const MachineMeta &meta, Error *error)
{
    if (meta.diskPath.isEmpty() || !QFileInfo(meta.diskPath).isFile()) {
        fails(error, Error::make(QStringLiteral("PODSTACK_IO_ERROR"),
                                 QStringLiteral("O disco da machine não existe."), meta.diskPath,
                                 QStringLiteral("Recrie a machine.")));
        return {};
    }

    QByteArray output;
    // `-U` (force-share) permite ler os snapshots com a machine rodando; o
    // QEMU mantém o lock de escrita no qcow2. A operação é somente leitura.
    if (!qemuImgSnapshot({QStringLiteral("info"), QStringLiteral("-U"),
                          QStringLiteral("--output=json"), meta.diskPath},
                         &output, error)) {
        return {};
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(output, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        fails(error, Error::make(QStringLiteral("PODSTACK_IO_ERROR"),
                                 QStringLiteral("Não foi possível interpretar as informações do "
                                                "disco."),
                                 parseError.errorString()));
        return {};
    }

    QVector<MachineSnapshot> items;
    const QJsonArray array = document.object().value(QStringLiteral("snapshots")).toArray();
    items.reserve(array.size());
    for (const QJsonValue &value : array) {
        const QJsonObject object = value.toObject();
        const QString name = object.value(QStringLiteral("name")).toString();
        if (name.isEmpty()) {
            continue;
        }

        MachineSnapshot snapshot;
        snapshot.id = object.value(QStringLiteral("id")).toString();
        snapshot.name = name;
        snapshot.sizeBytes = object.value(QStringLiteral("vm-state-size")).toInteger(0);

        const QJsonValue secondsValue = object.value(QStringLiteral("date-sec"));
        if (secondsValue.isDouble()) {
            qint64 milliseconds = 0;
            const QJsonValue nanosecondsValue = object.value(QStringLiteral("date-nsec"));
            if (nanosecondsValue.isDouble()) {
                milliseconds = nanosecondsValue.toInteger() / 1000000;
            }
            snapshot.date = QDateTime::fromSecsSinceEpoch(secondsValue.toInteger(), utcTimeZone())
                                .addMSecs(milliseconds)
                                .toString(Qt::ISODateWithMs);
        }
        items.append(snapshot);
    }
    return items;
}

bool MachineManager::removeSnapshot(const QString &idOrName, const QString &name, Error *error)
{
    if (error) {
        *error = {};
    }
    if (!validSnapshotName(name)) {
        fails(error, invalidSnapshotName(name));
        return false;
    }

    MachineMeta meta = load(idOrName, error);
    if (meta.id.isEmpty()) {
        return false;
    }

    std::shared_ptr<std::recursive_mutex> lockHolder;
    std::unique_lock<std::recursive_mutex> machineGuard;
    if (!tryLockMachine(meta.id, &lockHolder, &machineGuard)) {
        fails(error, Error::make(QStringLiteral("PODSTACK_CONFLICT"),
                                 QStringLiteral("A machine está em outra operação."), meta.id,
                                 QStringLiteral("Aguarde a operação atual terminar.")));
        return false;
    }

    const QVector<MachineSnapshot> existing = parseSnapshots(meta, error);
    if (error && !error->isEmpty()) {
        return false;
    }
    findSnapshot(existing, name, error);
    if (error && !error->isEmpty()) {
        return false;
    }

    const QemuSpec spec = specFor(meta, false, error);
    if (spec.machineId.isEmpty()) {
        return false;
    }
    const bool wasRunning = meta.state == QStringLiteral("running") ||
                            meta.state == QStringLiteral("starting") ||
                            m_runtime->isRunning(spec, nullptr);
    if (wasRunning) {
        Error stopError;
        meta = stop(meta.id, false, &stopError);
        if (!stopError.isEmpty()) {
            if (error) {
                *error = stopError;
            }
            return false;
        }
    }

    const QString machineId = meta.id;
    const auto resume = [this, wasRunning, machineId]() -> Error {
        if (!wasRunning) {
            return {};
        }
        Error resumeError;
        start(machineId, &resumeError);
        return resumeError;
    };

    Error removeError;
    if (!qemuImgSnapshot({QStringLiteral("snapshot"), QStringLiteral("-d"), name, meta.diskPath},
                         nullptr, &removeError)) {
        const Error resumeError = resume();
        if (!resumeError.isEmpty()) {
            qCWarning(lcCore) << "Falha ao reiniciar a machine" << machineId
                              << "após falha ao remover snapshot:" << resumeError.code;
        }
        if (error) {
            *error = removeError;
        }
        return false;
    }

    const Error resumeError = resume();
    if (!resumeError.isEmpty()) {
        qCWarning(lcCore) << "Falha ao reiniciar a machine" << machineId
                          << "após remover o snapshot:" << resumeError.code;
        fails(error, Error::make(QStringLiteral("PODSTACK_CONFLICT"),
                                 QStringLiteral("O snapshot foi removido, mas a machine não "
                                                "reiniciou."),
                                 errorText(resumeError),
                                 QStringLiteral("Inicie a machine manualmente; o snapshot já foi "
                                                "removido do disco.")));
        return false;
    }
    return true;
}

MachineMeta MachineManager::restoreSnapshot(const QString &idOrName, const QString &name,
                                            Error *error)
{
    if (error) {
        *error = {};
    }
    if (!validSnapshotName(name)) {
        fails(error, invalidSnapshotName(name));
        return {};
    }

    MachineMeta meta = load(idOrName, error);
    if (meta.id.isEmpty()) {
        return {};
    }

    std::shared_ptr<std::recursive_mutex> lockHolder;
    std::unique_lock<std::recursive_mutex> machineGuard;
    if (!tryLockMachine(meta.id, &lockHolder, &machineGuard)) {
        fails(error, Error::make(QStringLiteral("PODSTACK_CONFLICT"),
                                 QStringLiteral("A machine está em outra operação."), meta.id,
                                 QStringLiteral("Aguarde a operação atual terminar.")));
        return {};
    }

    const QVector<MachineSnapshot> existing = parseSnapshots(meta, error);
    if (error && !error->isEmpty()) {
        return {};
    }
    findSnapshot(existing, name, error);
    if (error && !error->isEmpty()) {
        return {};
    }

    const QemuSpec spec = specFor(meta, false, error);
    if (spec.machineId.isEmpty()) {
        return {};
    }
    const bool wasRunning = meta.state == QStringLiteral("running") ||
                            meta.state == QStringLiteral("starting") ||
                            m_runtime->isRunning(spec, nullptr);
    if (wasRunning) {
        Error stopError;
        meta = stop(meta.id, false, &stopError);
        if (!stopError.isEmpty()) {
            if (error) {
                *error = stopError;
            }
            return {};
        }
    }

    const QString machineId = meta.id;
    const auto resume = [this, wasRunning, machineId]() -> Error {
        if (!wasRunning) {
            return {};
        }
        Error resumeError;
        start(machineId, &resumeError);
        return resumeError;
    };

    Error restoreError;
    if (!qemuImgSnapshot({QStringLiteral("snapshot"), QStringLiteral("-a"), name, meta.diskPath},
                         nullptr, &restoreError)) {
        const Error resumeError = resume();
        if (!resumeError.isEmpty()) {
            qCWarning(lcCore) << "Falha ao reiniciar a machine" << machineId
                              << "após falha no restore:" << resumeError.code;
        }
        if (error) {
            *error = restoreError;
        }
        return {};
    }

    if (!wasRunning) {
        return meta;
    }

    Error resumeError;
    MachineMeta restarted = start(machineId, &resumeError);
    if (!resumeError.isEmpty()) {
        qCWarning(lcCore) << "Falha ao reiniciar a machine" << machineId
                          << "após restaurar o snapshot:" << resumeError.code;
        if (restarted.id.isEmpty()) {
            restarted = meta;
        }
        fails(error, Error::make(QStringLiteral("PODSTACK_CONFLICT"),
                                 QStringLiteral("O snapshot foi restaurado, mas a machine não "
                                                "reiniciou."),
                                 errorText(resumeError),
                                 QStringLiteral("Inicie a machine manualmente; o disco já foi "
                                                "restaurado.")));
    }
    return restarted;
}

bool MachineManager::qemuImgSnapshot(const QStringList &args, QByteArray *output, Error *error)
{
    if (output) {
        output->clear();
    }

    const QString binary = qemuImgBinary(error);
    if (binary.isEmpty()) {
        return false;
    }

    QProcess process;
    process.setProcessChannelMode(QProcess::SeparateChannels);
    process.start(binary, args);
    if (!process.waitForStarted(kQemuImgStartTimeoutMs)) {
        return fails(error, Error::make(QStringLiteral("PODSTACK_IO_ERROR"),
                                        QStringLiteral("Não foi possível executar o qemu-img."),
                                        process.errorString()));
    }
    if (!process.waitForFinished(kQemuImgTimeoutMs)) {
        process.kill();
        process.waitForFinished(1000);
        return fails(error, Error::make(QStringLiteral("PODSTACK_TIMEOUT"),
                                        QStringLiteral("O qemu-img excedeu o tempo limite ao "
                                                       "gerenciar snapshots.")));
    }
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        return fails(error,
                     Error::make(QStringLiteral("PODSTACK_IO_ERROR"),
                                 QStringLiteral("O qemu-img falhou ao gerenciar o snapshot."),
                                 processDetail(process.readAllStandardError())));
    }

    if (output) {
        *output = process.readAllStandardOutput();
    }
    return true;
}

MachineSnapshot MachineManager::createSnapshot(const QString &idOrName, const QString &name,
                                               Error *error)
{
    if (error) {
        *error = {};
    }
    if (!validSnapshotName(name)) {
        fails(error, invalidSnapshotName(name));
        return {};
    }

    MachineMeta meta = load(idOrName, error);
    if (meta.id.isEmpty()) {
        return {};
    }

    std::shared_ptr<std::recursive_mutex> lockHolder;
    std::unique_lock<std::recursive_mutex> machineGuard;
    if (!tryLockMachine(meta.id, &lockHolder, &machineGuard)) {
        fails(error, Error::make(QStringLiteral("PODSTACK_CONFLICT"),
                                 QStringLiteral("A machine está em outra operação."), meta.id,
                                 QStringLiteral("Aguarde a operação atual terminar.")));
        return {};
    }

    const QVector<MachineSnapshot> existing = parseSnapshots(meta, error);
    if (error && !error->isEmpty()) {
        return {};
    }
    for (const MachineSnapshot &snapshot : existing) {
        if (snapshot.name == name) {
            fails(error, Error::make(QStringLiteral("PODSTACK_CONFLICT"),
                                     QStringLiteral("Já existe um snapshot com esse nome."), name,
                                     QStringLiteral("Escolha outro nome ou remova o snapshot "
                                                    "existente.")));
            return {};
        }
    }

    const QemuSpec spec = specFor(meta, false, error);
    if (spec.machineId.isEmpty()) {
        return {};
    }
    const bool wasRunning = meta.state == QStringLiteral("running") ||
                            meta.state == QStringLiteral("starting") ||
                            m_runtime->isRunning(spec, nullptr);
    if (wasRunning) {
        Error stopError;
        meta = stop(meta.id, false, &stopError);
        if (!stopError.isEmpty()) {
            if (error) {
                *error = stopError;
            }
            return {};
        }
    }

    const QString machineId = meta.id;
    const auto resume = [this, wasRunning, machineId]() -> Error {
        if (!wasRunning) {
            return {};
        }
        Error resumeError;
        start(machineId, &resumeError);
        return resumeError;
    };

    Error snapshotError;
    if (!qemuImgSnapshot({QStringLiteral("snapshot"), QStringLiteral("-c"), name, meta.diskPath},
                         nullptr, &snapshotError)) {
        const Error resumeError = resume();
        if (!resumeError.isEmpty()) {
            qCWarning(lcCore) << "Falha ao reiniciar a machine" << machineId
                              << "após falha no snapshot:" << resumeError.code;
        }
        if (error) {
            *error = snapshotError;
        }
        return {};
    }

    MachineSnapshot created;
    Error listError;
    const QVector<MachineSnapshot> after = parseSnapshots(meta, &listError);
    if (listError.isEmpty()) {
        for (const MachineSnapshot &snapshot : after) {
            if (snapshot.name == name) {
                created = snapshot;
                break;
            }
        }
    } else {
        qCWarning(lcCore) << "Falha ao relistar snapshots da machine" << machineId << ":"
                          << listError.code;
    }
    if (created.name.isEmpty()) {
        // qemu-img pode não listar o snapshot em versões antigas; o comando
        // retornou sucesso, então devolvemos ao menos nome/data.
        created.name = name;
        created.date = nowIso();
    }

    const Error resumeError = resume();
    if (!resumeError.isEmpty()) {
        qCWarning(lcCore) << "Falha ao reiniciar a machine" << machineId
                          << "após criar o snapshot:" << resumeError.code;
        fails(error, Error::make(QStringLiteral("PODSTACK_CONFLICT"),
                                 QStringLiteral("O snapshot foi criado, mas a machine não "
                                                "reiniciou."),
                                 errorText(resumeError),
                                 QStringLiteral("Inicie a machine manualmente; o snapshot já foi "
                                                "gravado no disco.")));
    }
    return created;
}

MachineSnapshot MachineManager::findSnapshot(const QVector<MachineSnapshot> &items,
                                             const QString &name, Error *error)
{
    for (const MachineSnapshot &snapshot : items) {
        if (snapshot.name == name) {
            return snapshot;
        }
    }
    fails(error, Error::make(QStringLiteral("PODSTACK_NOT_FOUND"),
                             QStringLiteral("Snapshot não encontrado."), name,
                             QStringLiteral("Liste os snapshots com `machine snapshot ls`.")));
    return {};
}

} // namespace PodStack
