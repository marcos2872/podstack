#include "daemon/ComposeManager.h"

#include "core/Paths.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QMap>
#include <QProcess>
#include <QProcessEnvironment>
#include <QStandardPaths>

namespace PodStack {
namespace {

QString projectStatus(bool anyRunning, bool anyStopped)
{
    if (anyRunning && anyStopped) {
        return QStringLiteral("partial");
    }
    return anyRunning ? QStringLiteral("running") : QStringLiteral("stopped");
}

} // namespace

QJsonObject ComposeServiceInfo::toJson() const
{
    QJsonObject object;
    object.insert(QStringLiteral("name"), name);
    QJsonArray ids;
    for (const QString &id : containerIds) {
        ids.append(id);
    }
    object.insert(QStringLiteral("containerIds"), ids);
    object.insert(QStringLiteral("status"), status);
    return object;
}

QJsonObject ComposeProjectInfo::toJson() const
{
    QJsonObject object;
    object.insert(QStringLiteral("name"), name);
    object.insert(QStringLiteral("workingDir"), workingDir);
    QJsonArray files;
    for (const QString &file : configFiles) {
        files.append(file);
    }
    object.insert(QStringLiteral("configFiles"), files);
    QJsonArray serviceArray;
    for (const ComposeServiceInfo &service : services) {
        serviceArray.append(service.toJson());
    }
    object.insert(QStringLiteral("services"), serviceArray);
    object.insert(QStringLiteral("status"), status);
    return object;
}

ComposeManager::ComposeManager(PodmanClient *podman)
    : m_podman(podman)
{
}

QString ComposeManager::findConfigFile(const QString &projectPath)
{
    const QDir dir(projectPath);
    const QStringList candidates{QStringLiteral("compose.yaml"),
                                 QStringLiteral("compose.yml"),
                                 QStringLiteral("docker-compose.yaml"),
                                 QStringLiteral("docker-compose.yml")};
    for (const QString &candidate : candidates) {
        const QString path = dir.absoluteFilePath(candidate);
        if (QFileInfo::exists(path)) {
            return path;
        }
    }
    return {};
}

QVector<ComposeProjectInfo> ComposeManager::listProjects(Error *error)
{
    if (error) {
        *error = {};
    }
    if (!m_podman) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_INTERNAL_ERROR"),
                                 QStringLiteral("ComposeManager sem PodmanClient."));
        }
        return {};
    }

    const QVector<ContainerInfo> containers = m_podman->listContainers(true, error);
    if (error && !error->isEmpty()) {
        return {};
    }

    QMap<QString, ComposeProjectInfo> projects;
    QMap<QString, QMap<QString, ComposeServiceInfo>> services;

    for (const ContainerInfo &container : containers) {
        const QString project = container.labels.value(
            QStringLiteral("com.docker.compose.project"));
        if (project.isEmpty()) {
            continue;
        }
        const QString service = container.labels.value(
            QStringLiteral("com.docker.compose.service"), QStringLiteral("default"));
        const QString workingDir = container.labels.value(
            QStringLiteral("com.docker.compose.project.working_dir"));
        const QString configFiles = container.labels.value(
            QStringLiteral("com.docker.compose.project.config_files"));

        ComposeProjectInfo &info = projects[project];
        info.name = project;
        if (!workingDir.isEmpty()) {
            info.workingDir = workingDir;
        }
        if (info.configFiles.isEmpty() && !configFiles.isEmpty()) {
            info.configFiles = configFiles.split(QLatin1Char(','), Qt::SkipEmptyParts);
        }

        ComposeServiceInfo &serviceInfo = services[project][service];
        serviceInfo.name = service;
        serviceInfo.containerIds.append(container.id);
        const bool running = container.state == QStringLiteral("running");
        if (serviceInfo.status.isEmpty()) {
            serviceInfo.status = running ? QStringLiteral("running")
                                         : QStringLiteral("stopped");
        } else if (serviceInfo.status != (running ? QStringLiteral("running")
                                                  : QStringLiteral("stopped"))) {
            serviceInfo.status = QStringLiteral("partial");
        }
    }

    QVector<ComposeProjectInfo> result;
    for (auto it = projects.begin(); it != projects.end(); ++it) {
        ComposeProjectInfo info = it.value();
        const QMap<QString, ComposeServiceInfo> projectServices = services.value(it.key());
        bool anyRunning = false;
        bool anyStopped = false;
        for (const ComposeServiceInfo &service : projectServices) {
            info.services.append(service);
            if (service.status == QStringLiteral("running")) {
                anyRunning = true;
            } else if (service.status == QStringLiteral("stopped")) {
                anyStopped = true;
            } else {
                anyRunning = true;
                anyStopped = true;
            }
        }
        info.status = projectStatus(anyRunning, anyStopped);
        if (info.configFiles.isEmpty() && !info.workingDir.isEmpty()) {
            const QString config = findConfigFile(info.workingDir);
            if (!config.isEmpty()) {
                info.configFiles.append(config);
            }
        }
        result.append(info);
    }
    return result;
}

bool ComposeManager::resolveProvider(Error *error)
{
    if (!m_provider.isEmpty()) {
        return true;
    }

    const QString override = qEnvironmentVariable("PODSTACK_COMPOSE_PROVIDER");
    if (!override.isEmpty()) {
        const QString resolved = QStandardPaths::findExecutable(override);
        m_provider = resolved.isEmpty() ? override : resolved;
        return true;
    }

    const QString standalone = QStandardPaths::findExecutable(QStringLiteral("docker-compose"));
    if (!standalone.isEmpty()) {
        m_provider = standalone;
        return true;
    }

    QProcess podmanCompose;
    podmanCompose.setProcessEnvironment(QProcessEnvironment::systemEnvironment());
    podmanCompose.start(QStringLiteral("podman"),
                        {QStringLiteral("compose"), QStringLiteral("version")});
    if (podmanCompose.waitForFinished(15000)
        && podmanCompose.exitStatus() == QProcess::NormalExit
        && podmanCompose.exitCode() == 0) {
        m_provider = QStringLiteral("podman compose");
        return true;
    }

    QProcess dockerCompose;
    dockerCompose.setProcessEnvironment(QProcessEnvironment::systemEnvironment());
    dockerCompose.start(QStringLiteral("docker"),
                        {QStringLiteral("compose"), QStringLiteral("version")});
    if (dockerCompose.waitForFinished(15000)
        && dockerCompose.exitStatus() == QProcess::NormalExit
        && dockerCompose.exitCode() == 0) {
        m_provider = QStringLiteral("docker compose");
        return true;
    }

    if (error) {
        *error = Error::make(
            QStringLiteral("PODSTACK_COMPOSE_PROVIDER_MISSING"),
            QStringLiteral("Nenhum provider Compose encontrado."),
            QStringLiteral("Instale docker-compose ou podman-compose."),
            QStringLiteral("sudo dnf install docker-compose"));
    }
    return false;
}

QString ComposeManager::provider(Error *error)
{
    if (!resolveProvider(error)) {
        return {};
    }
    return m_provider;
}

bool ComposeManager::runCompose(const QString &projectPath, const QStringList &args,
                                int timeoutMs, QString *output, Error *error)
{
    if (error) {
        *error = {};
    }
    if (!resolveProvider(error)) {
        return false;
    }
    if (projectPath.isEmpty() || !QFileInfo(projectPath).isDir()) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_INVALID_PARAMS"),
                                 QStringLiteral("Diretório do projeto inválido."), projectPath);
        }
        return false;
    }

    QString program;
    QStringList arguments;
    if (m_provider == QStringLiteral("podman compose")) {
        program = QStringLiteral("podman");
        arguments = {QStringLiteral("compose")};
    } else if (m_provider == QStringLiteral("docker compose")) {
        program = QStringLiteral("docker");
        arguments = {QStringLiteral("compose")};
    } else {
        program = m_provider;
    }

    arguments += {QStringLiteral("--project-directory"), projectPath};
    arguments += args;

    QProcess process;
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("DOCKER_HOST"),
                       QStringLiteral("unix://") + m_podman->socketPath());
    environment.insert(QStringLiteral("COMPOSE_ANSI"), QStringLiteral("never"));
    process.setProcessEnvironment(environment);
    process.setProcessChannelMode(QProcess::SeparateChannels);
    process.start(program, arguments);

    if (!process.waitForStarted(30000)) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_COMPOSE_ERROR"),
                                 QStringLiteral("Falha ao iniciar o provider Compose."),
                                 process.errorString());
        }
        return false;
    }
    if (!process.waitForFinished(timeoutMs)) {
        process.kill();
        process.waitForFinished(3000);
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_TIMEOUT"),
                                 QStringLiteral("O Compose excedeu o tempo limite."),
                                 QString::fromUtf8(process.readAllStandardError()));
        }
        return false;
    }

    const QString stdoutText = QString::fromUtf8(process.readAllStandardOutput());
    const QString stderrText = QString::fromUtf8(process.readAllStandardError());
    if (output) {
        *output = stdoutText;
    }
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_COMPOSE_ERROR"),
                                 QStringLiteral("O Compose falhou."),
                                 stderrText.isEmpty() ? stdoutText : stderrText);
        }
        return false;
    }
    return true;
}

bool ComposeManager::up(const QString &projectPath, bool detach, Error *error)
{
    QStringList args{QStringLiteral("up")};
    if (detach) {
        args.append(QStringLiteral("-d"));
    }
    return runCompose(projectPath, args, 600000, nullptr, error);
}

bool ComposeManager::down(const QString &projectPath, bool volumes, Error *error)
{
    QStringList args{QStringLiteral("down")};
    if (volumes) {
        args.append(QStringLiteral("--volumes"));
    }
    return runCompose(projectPath, args, 600000, nullptr, error);
}

bool ComposeManager::restart(const QString &projectPath, const QString &service, Error *error)
{
    QStringList args{QStringLiteral("restart")};
    if (!service.isEmpty()) {
        args.append(service);
    }
    return runCompose(projectPath, args, 600000, nullptr, error);
}

bool ComposeManager::stop(const QString &projectPath, const QString &service, Error *error)
{
    QStringList args{QStringLiteral("stop")};
    if (!service.isEmpty()) {
        args.append(service);
    }
    return runCompose(projectPath, args, 600000, nullptr, error);
}

bool ComposeManager::start(const QString &projectPath, const QString &service, Error *error)
{
    QStringList args{QStringLiteral("start")};
    if (!service.isEmpty()) {
        args.append(service);
    }
    return runCompose(projectPath, args, 600000, nullptr, error);
}

} // namespace PodStack
