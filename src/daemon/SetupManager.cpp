// SPDX-License-Identifier: GPL-3.0-or-later

#include "daemon/SetupManager.h"

#include "core/Config.h"
#include "core/Paths.h"
#include "core/SetupPolicy.h"

#include <QFileInfo>
#include <QJsonArray>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QtConcurrent>

#include <sqlite3.h>

namespace PodStack {
namespace {

constexpr auto kHelperService = "dev.podstack.Helper1";

QString runCommand(const QString &program, const QStringList &arguments, int timeoutMs = 2000,
                   int *exitCode = nullptr, QString *output = nullptr)
{
    if (exitCode) {
        *exitCode = -1;
    }

    QProcess process;
    process.setProcessChannelMode(QProcess::SeparateChannels);
    process.start(program, arguments);
    if (!process.waitForStarted(timeoutMs)) {
        return {};
    }
    if (!process.waitForFinished(timeoutMs)) {
        process.kill();
        process.waitForFinished(500);
        return {};
    }

    if (exitCode) {
        *exitCode = process.exitCode();
    }
    const QString text = QString::fromUtf8(process.readAllStandardOutput()).trimmed();
    if (output) {
        *output = text;
    }
    return text;
}

QString firstLine(const QString &text)
{
    return text.section(QLatin1Char('\n'), 0, 0).trimmed();
}

QString findBinary(const QString &name, const QStringList &fallbacks = {})
{
    const QString resolved = QStandardPaths::findExecutable(name);
    if (!resolved.isEmpty()) {
        return resolved;
    }
    for (const QString &candidate : fallbacks) {
        if (QFileInfo::exists(candidate) && QFileInfo(candidate).isExecutable()) {
            return candidate;
        }
    }
    return {};
}

bool fileExists(const QString &path)
{
    return QFileInfo::exists(path);
}

QString versionFromText(const QString &text, const QString &prefix)
{
    const QRegularExpression expression(
        QStringLiteral("%1\\s+([0-9]+(?:\\.[0-9]+)+)").arg(prefix));
    const QRegularExpressionMatch match = expression.match(text);
    return match.hasMatch() ? match.captured(1) : QString();
}

QJsonObject checkJson(const SetupCheck &check)
{
    return check.toJson();
}

} // namespace

QJsonObject SetupCheck::toJson() const
{
    QJsonObject object;
    object.insert(QStringLiteral("id"), id);
    object.insert(QStringLiteral("status"), status);
    object.insert(QStringLiteral("version"), version);
    object.insert(QStringLiteral("detail"), detail);
    object.insert(QStringLiteral("remediation"), remediation);
    return object;
}

SetupManager::SetupManager() = default;

QString SetupManager::podmanVersion()
{
    const QString binary = findBinary(QStringLiteral("podman"));
    if (binary.isEmpty()) {
        return {};
    }
    return versionFromText(runCommand(binary, {QStringLiteral("--version")}), QStringLiteral("version"));
}

QString SetupManager::dockerVersion()
{
    const QString binary = findBinary(QStringLiteral("docker"));
    if (binary.isEmpty()) {
        return {};
    }
    const QString text = runCommand(binary, {QStringLiteral("--version")});
    if (text.contains(QStringLiteral("podman"), Qt::CaseInsensitive)) {
        return {};
    }
    return versionFromText(text, QStringLiteral("version"));
}

bool SetupManager::dockerInstalled()
{
    if (firstLine(runCommand(QStringLiteral("systemctl"),
                             {QStringLiteral("is-active"), QStringLiteral("docker.socket")}))
        == QStringLiteral("active")) {
        return true;
    }
    return fileExists(QStringLiteral("/var/run/docker.sock")) || !dockerVersion().isEmpty();
}

bool SetupManager::kvmAvailable()
{
    const QFileInfo info(QStringLiteral("/dev/kvm"));
    return info.exists() && info.isReadable();
}

bool SetupManager::kvmWritable()
{
    const QFileInfo info(QStringLiteral("/dev/kvm"));
    return info.exists() && info.isWritable();
}

bool SetupManager::commandSucceeds(const QString &program, const QStringList &arguments,
                                   int timeoutMs)
{
    int exitCode = -1;
    runCommand(program, arguments, timeoutMs, &exitCode);
    return exitCode == 0;
}

QList<SetupCheck> SetupManager::check(bool deep)
{
    QList<SetupCheck> checks;

    const QString podman = findBinary(QStringLiteral("podman"));
    const QString podmanVer = podman.isEmpty() ? QString() : podmanVersion();
    const bool socketExists = fileExists(Paths::podmanSocket());
    const QString socketState = firstLine(runCommand(
        QStringLiteral("systemctl"),
        {QStringLiteral("--user"), QStringLiteral("is-active"), QStringLiteral("podman.socket")}));

    SetupCheck podmanCheck;
    podmanCheck.id = QStringLiteral("podman");
    if (podman.isEmpty()) {
        podmanCheck.status = QStringLiteral("missing");
        podmanCheck.detail = QStringLiteral("binário do Podman não encontrado");
        podmanCheck.remediation = QStringLiteral("Instale o Podman para o PodStack funcionar.");
    } else if (!socketExists) {
        podmanCheck.status = QStringLiteral("warn");
        podmanCheck.version = podmanVer;
        podmanCheck.detail = QStringLiteral("Podman instalado, mas podman.socket não está ativo");
        podmanCheck.remediation =
            QStringLiteral("Ative o socket: systemctl --user enable --now podman.socket");
    } else {
        podmanCheck.status = QStringLiteral("ok");
        podmanCheck.version = podmanVer;
        podmanCheck.detail = QStringLiteral("Podman %1 · rootless · socket ativo")
                                 .arg(podmanVer.isEmpty() ? QStringLiteral("desconhecido")
                                                          : podmanVer);
    }
    if (podmanCheck.status != QStringLiteral("missing") && deep) {
        const QString rootless =
            runCommand(podman,
                       {QStringLiteral("info"), QStringLiteral("--format"),
                        QStringLiteral("{{.Host.Security.Rootless}}")},
                       5000);
        if (!rootless.isEmpty() && rootless != QStringLiteral("true")) {
            podmanCheck.status = QStringLiteral("warn");
            podmanCheck.detail += QStringLiteral(" · rootless indisponível");
        }
    }
    if (podmanCheck.status == QStringLiteral("ok") && socketState != QStringLiteral("active")) {
        podmanCheck.detail += QStringLiteral(" · systemd: %1").arg(socketState.isEmpty()
                                                                      ? QStringLiteral("desconhecido")
                                                                      : socketState);
    }
    checks.append(podmanCheck);

    SetupCheck dockerCheck;
    dockerCheck.id = QStringLiteral("docker");
    if (dockerInstalled()) {
        const QString version = dockerVersion();
        dockerCheck.status = QStringLiteral("optional");
        dockerCheck.version = version;
        dockerCheck.detail = version.isEmpty()
                                 ? QStringLiteral("Docker presente · coexistindo com o Podman")
                                 : QStringLiteral("Docker %1 · coexistindo com o Podman")
                                       .arg(version);
    } else {
        dockerCheck.status = QStringLiteral("optional");
        dockerCheck.detail = QStringLiteral("Docker não instalado · nenhum conflito");
    }
    checks.append(dockerCheck);

    SetupCheck kvmCheck;
    kvmCheck.id = QStringLiteral("kvm");
    if (kvmAvailable()) {
        kvmCheck.status = kvmWritable() ? QStringLiteral("ok") : QStringLiteral("warn");
        kvmCheck.detail = kvmWritable()
                              ? QStringLiteral("/dev/kvm disponível")
                              : QStringLiteral("/dev/kvm existe, mas sem permissão de escrita");
        if (!kvmWritable()) {
            kvmCheck.remediation =
                QStringLiteral("Adicione seu usuário ao grupo kvm e encerre a sessão.");
        }
    } else {
        kvmCheck.status = QStringLiteral("warn");
        kvmCheck.detail = QStringLiteral("/dev/kvm indisponível · machines usarão TCG (lento)");
        kvmCheck.remediation =
            QStringLiteral("Ative a virtualização no firmware ou use TCG com aviso de lentidão.");
    }
    checks.append(kvmCheck);

    SetupCheck qemuCheck;
    qemuCheck.id = QStringLiteral("qemu");
    const QString qemu = findBinary(QStringLiteral("qemu-system-x86_64"));
    const QString qemuArm = findBinary(QStringLiteral("qemu-system-aarch64"));
    if (qemu.isEmpty() && qemuArm.isEmpty()) {
        qemuCheck.status = QStringLiteral("missing");
        qemuCheck.detail = QStringLiteral("QEMU não encontrado");
        qemuCheck.remediation = QStringLiteral("Instale qemu-kvm e qemu-system-x86_64.");
    } else {
        const QString version = versionFromText(
            runCommand(qemu.isEmpty() ? qemuArm : qemu, {QStringLiteral("--version")}),
            QStringLiteral("QEMU emulator version"));
        qemuCheck.status = QStringLiteral("ok");
        qemuCheck.version = version;
        qemuCheck.detail = QStringLiteral("QEMU %1").arg(version.isEmpty()
                                                             ? QStringLiteral("instalado")
                                                             : version);
    }
    checks.append(qemuCheck);

    SetupCheck ovmfCheck;
    ovmfCheck.id = QStringLiteral("ovmf");
    const QStringList ovmfPaths = {
        QStringLiteral("/usr/share/edk2/ovmf/OVMF_CODE.fd"),
        QStringLiteral("/usr/share/OVMF/OVMF_CODE.fd"),
        QStringLiteral("/usr/share/qemu/edk2-x86_64-code.fd"),
    };
    bool hasOvmf = false;
    for (const QString &path : ovmfPaths) {
        if (fileExists(path)) {
            hasOvmf = true;
            break;
        }
    }
    ovmfCheck.status = hasOvmf ? QStringLiteral("ok") : QStringLiteral("missing");
    ovmfCheck.detail = hasOvmf ? QStringLiteral("OVMF presente")
                               : QStringLiteral("OVMF não encontrado");
    if (!hasOvmf) {
        ovmfCheck.remediation = QStringLiteral("Instale edk2-ovmf.");
    }
    checks.append(ovmfCheck);

    SetupCheck virtiofsdCheck;
    virtiofsdCheck.id = QStringLiteral("virtiofsd");
    const QString virtiofsd = findBinary(QStringLiteral("virtiofsd"),
                                         {QStringLiteral("/usr/libexec/virtiofsd")});
    virtiofsdCheck.status = virtiofsd.isEmpty() ? QStringLiteral("missing") : QStringLiteral("ok");
    virtiofsdCheck.detail = virtiofsd.isEmpty() ? QStringLiteral("virtiofsd não encontrado")
                                                : QStringLiteral("virtiofsd instalado");
    if (virtiofsd.isEmpty()) {
        virtiofsdCheck.remediation = QStringLiteral("Instale virtiofsd para mounts de machines.");
    }
    checks.append(virtiofsdCheck);

    SetupCheck systemdCheck;
    systemdCheck.id = QStringLiteral("systemd-user");
    const QString systemdState = firstLine(runCommand(
        QStringLiteral("systemctl"), {QStringLiteral("--user"), QStringLiteral("is-system-running")}));
    if (systemdState == QStringLiteral("running") || systemdState == QStringLiteral("degraded")) {
        systemdCheck.status = QStringLiteral("ok");
        systemdCheck.detail = QStringLiteral("systemd de usuário ativo");
    } else {
        systemdCheck.status = QStringLiteral("warn");
        systemdCheck.detail = QStringLiteral("systemd de usuário indisponível");
        systemdCheck.remediation = QStringLiteral("Execute o PodStack em uma sessão de usuário.");
    }
    checks.append(systemdCheck);

    SetupCheck sqliteCheck;
    sqliteCheck.id = QStringLiteral("sqlite");
    sqliteCheck.status = QStringLiteral("ok");
    sqliteCheck.version = QString::fromLatin1(sqlite3_libversion());
    sqliteCheck.detail = QStringLiteral("SQLite %1").arg(sqliteCheck.version);
    checks.append(sqliteCheck);

    SetupCheck caddyCheck;
    caddyCheck.id = QStringLiteral("caddy");
    const QString caddy = findBinary(QStringLiteral("caddy"));
    caddyCheck.status = caddy.isEmpty() ? QStringLiteral("optional") : QStringLiteral("ok");
    caddyCheck.detail = caddy.isEmpty() ? QStringLiteral("Caddy não instalado · domínios HTTPS opcionais")
                                        : QStringLiteral("Caddy instalado · domínios HTTPS disponíveis");
    checks.append(caddyCheck);

    SetupCheck k3sCheck;
    k3sCheck.id = QStringLiteral("k3s");
    const QString k3s = findBinary(QStringLiteral("k3s"), {QStringLiteral("/usr/local/bin/k3s")});
    k3sCheck.status = k3s.isEmpty() ? QStringLiteral("optional") : QStringLiteral("ok");
    k3sCheck.detail = k3s.isEmpty() ? QStringLiteral("k3s não instalado · Kubernetes opcional")
                                    : QStringLiteral("k3s instalado · Kubernetes disponível");
    checks.append(k3sCheck);

    return checks;
}

QJsonObject SetupManager::status()
{
    const Config config = Config::load();
    const QList<SetupCheck> checks = check(false);

    int ok = 0;
    int warn = 0;
    int missing = 0;
    int optional = 0;
    QJsonArray checkArray;
    for (const SetupCheck &check : checks) {
        if (check.status == QStringLiteral("ok")) {
            ++ok;
        } else if (check.status == QStringLiteral("warn")) {
            ++warn;
        } else if (check.status == QStringLiteral("missing")) {
            ++missing;
        } else {
            ++optional;
        }
        checkArray.append(checkJson(check));
    }

    bool ready = true;
    for (const SetupCheck &check : checks) {
        if (!SetupPolicy::isCheckSatisfied(check.id, check.status)) {
            ready = false;
            break;
        }
    }

    QJsonObject summary;
    summary.insert(QStringLiteral("ok"), ok);
    summary.insert(QStringLiteral("warnings"), warn);
    summary.insert(QStringLiteral("missing"), missing);
    summary.insert(QStringLiteral("optional"), optional);
    summary.insert(QStringLiteral("ready"), ready);

    QJsonObject result;
    result.insert(QStringLiteral("completed"),
                  config.get(QStringLiteral("setup.completed")).toBool());
    result.insert(QStringLiteral("completedAt"),
                  config.get(QStringLiteral("setup.completedAt")));
    result.insert(QStringLiteral("dockerPolicy"),
                  config.get(QStringLiteral("setup.dockerPolicy")).toString());
    result.insert(QStringLiteral("showOnboarding"),
                  config.get(QStringLiteral("app.showOnboarding")).toBool(true));
    result.insert(QStringLiteral("checks"), checkArray);
    result.insert(QStringLiteral("summary"), summary);
    result.insert(QStringLiteral("activeJob"), activeJob());
    return result;
}

QJsonObject SetupManager::validate()
{
    const QList<SetupCheck> checks = check(true);
    bool ok = true;
    QJsonArray warnings;
    QJsonArray checkArray;
    for (const SetupCheck &check : checks) {
        if (!SetupPolicy::isCheckSatisfied(check.id, check.status)) {
            ok = false;
        }
        if (check.status == QStringLiteral("warn")) {
            QJsonObject warning;
            warning.insert(QStringLiteral("id"), check.id);
            warning.insert(QStringLiteral("detail"), check.detail);
            warning.insert(QStringLiteral("remediation"), check.remediation);
            warnings.append(warning);
        }
        checkArray.append(checkJson(check));
    }

    QJsonObject result;
    result.insert(QStringLiteral("ok"), ok);
    result.insert(QStringLiteral("checks"), checkArray);
    result.insert(QStringLiteral("warnings"), warnings);
    return result;
}

std::shared_ptr<SetupManager::Job> SetupManager::createJob(const QString &action,
                                                           const Notify &notify)
{
    auto job = std::make_shared<Job>();
    job->action = action;
    job->state = QStringLiteral("running");
    job->startedAt = QDateTime::currentDateTimeUtc();
    if (notify) {
        job->watchers.append(notify);
    }

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        job->id = QStringLiteral("setup-%1-%2")
                      .arg(QDateTime::currentSecsSinceEpoch())
                      .arg(m_nextJob++);
        m_jobs.insert(job->id, job);
        m_activeJobId = job->id;
    }
    return job;
}

std::shared_ptr<SetupManager::Job> SetupManager::findJob(const QString &jobId) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_jobs.value(jobId);
}

QList<SetupManager::Notify> SetupManager::watchersFor(const QString &jobId) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    const auto job = m_jobs.value(jobId);
    return job ? job->watchers : QList<Notify>{};
}

void SetupManager::publish(const std::shared_ptr<Job> &job, const QString &method)
{
    QJsonObject payload;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        payload = jobToJsonLocked(*job);
    }
    for (const Notify &notify : watchersFor(job->id)) {
        if (notify) {
            notify(method, payload);
        }
    }
}

void SetupManager::appendLog(const std::shared_ptr<Job> &job, const QString &line)
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        job->log.append(line);
        if (job->log.size() > 2000) {
            job->log = job->log.mid(500);
        }
    }
    publish(job, QStringLiteral("setup.delta"));
}

void SetupManager::setProgress(const std::shared_ptr<Job> &job, int progress)
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        job->progress = qBound(0, progress, 100);
    }
    publish(job, QStringLiteral("setup.delta"));
}

QJsonObject SetupManager::jobToJsonLocked(const Job &job) const
{
    QJsonObject object;
    object.insert(QStringLiteral("jobId"), job.id);
    object.insert(QStringLiteral("action"), job.action);
    object.insert(QStringLiteral("state"), job.state);
    object.insert(QStringLiteral("progress"), job.progress);
    object.insert(QStringLiteral("error"), job.error);
    object.insert(QStringLiteral("lines"), QJsonArray::fromStringList(job.log));
    object.insert(QStringLiteral("startedAt"), job.startedAt.toString(Qt::ISODateWithMs));
    if (!job.finishedAt.isNull()) {
        object.insert(QStringLiteral("finishedAt"),
                      job.finishedAt.toString(Qt::ISODateWithMs));
    }
    return object;
}

QJsonObject SetupManager::jobStatus(const QString &jobId) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    const auto job = m_jobs.value(jobId);
    return job ? jobToJsonLocked(*job) : QJsonObject{};
}

QJsonObject SetupManager::activeJob() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_activeJobId.isEmpty()) {
        return {};
    }
    const auto job = m_jobs.value(m_activeJobId);
    return job ? jobToJsonLocked(*job) : QJsonObject{};
}

QJsonObject SetupManager::apply(const QJsonObject &params, const Notify &notify, Error *error)
{
    QString action = params.value(QStringLiteral("action")).toString();
    const QJsonArray actions = params.value(QStringLiteral("actions")).toArray();
    if (action.isEmpty() && !actions.isEmpty()) {
        action = actions.first().toObject().value(QStringLiteral("type")).toString();
    }

    const QStringList allowed = {
        QStringLiteral("install-podman"),
        QStringLiteral("upgrade-podman"),
        QStringLiteral("install-qemu"),
        QStringLiteral("install-components"),
        QStringLiteral("enable-socket"),
    };
    if (!allowed.contains(action)) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_INVALID_PARAMS"),
                                 QStringLiteral("Ação de setup inválida: %1").arg(action));
        }
        return {};
    }

    const bool needsHelper = action != QStringLiteral("enable-socket");
    if (needsHelper && !helperAvailable()) {
            if (error) {
                *error = Error::make(
                    QStringLiteral("PODSTACK_HELPER_UNAVAILABLE"),
                    QStringLiteral("O helper privilegiado do PodStack não está disponível."),
                    QStringLiteral("Serviço %1 não encontrado no barramento de sistema.")
                        .arg(QString::fromLatin1(kHelperService)),
                    QStringLiteral("Instale o podstack-helper e tente novamente."));
            }
        return {};
    }

    auto job = createJob(action, notify);
    const QJsonObject options = params;
    (void)QtConcurrent::run([this, job, options] { runJob(job, options); });

    QJsonObject result;
    result.insert(QStringLiteral("jobId"), job->id);
    result.insert(QStringLiteral("state"), job->state);
    return result;
}

QJsonObject SetupManager::removeDocker(const QJsonObject &params, const Notify &notify,
                                       Error *error)
{
    if (!params.value(QStringLiteral("confirm")).toBool(false)) {
        if (error) {
            *error = Error::make(
                QStringLiteral("PODSTACK_CONFIRMATION_REQUIRED"),
                QStringLiteral("A remoção do Docker exige confirmação explícita."),
                QStringLiteral("Nenhuma alteração foi feita."),
                QStringLiteral("Envie confirm=true após avisar sobre a perda de dados."));
        }
        return {};
    }

    if (!helperAvailable()) {
        if (error) {
            *error = Error::make(
                QStringLiteral("PODSTACK_HELPER_UNAVAILABLE"),
                QStringLiteral("O helper privilegiado do PodStack não está disponível."),
                QStringLiteral("Serviço %1 não encontrado no barramento de sistema.")
                    .arg(QString::fromLatin1(kHelperService)),
                QStringLiteral("Instale o podstack-helper e tente novamente."));
        }
        return {};
    }

    auto job = createJob(QStringLiteral("remove-docker"), notify);
    const QJsonObject options = params;
    (void)QtConcurrent::run([this, job, options] { runJob(job, options); });

    QJsonObject result;
    result.insert(QStringLiteral("jobId"), job->id);
    result.insert(QStringLiteral("state"), job->state);
    return result;
}

QJsonObject SetupManager::cancel(const QJsonObject &params)
{
    const QString jobId = params.value(QStringLiteral("jobId")).toString();
    const auto job = findJob(jobId);
    if (!job) {
        return QJsonObject{{QStringLiteral("cancelled"), false}};
    }

    job->cancel.store(true);
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (job->state == QStringLiteral("running") || job->state == QStringLiteral("queued")) {
            job->state = QStringLiteral("cancelling");
        }
    }
    publish(job, QStringLiteral("setup.delta"));
    return QJsonObject{{QStringLiteral("cancelled"), true}};
}

void SetupManager::subscribe(const QString &jobId, const Notify &notify)
{
    const auto job = findJob(jobId);
    if (!job) {
        return;
    }
    QJsonObject payload;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        job->watchers.append(notify);
        payload = jobToJsonLocked(*job);
    }
    if (notify) {
        notify(QStringLiteral("setup.delta"), payload);
    }
}

QStringList SetupManager::actionPackages(const QString &action,
                                         const QJsonObject &options) const
{
    if (action == QStringLiteral("install-podman")
        || action == QStringLiteral("upgrade-podman")) {
        return {QStringLiteral("podman")};
    }
    if (action == QStringLiteral("install-qemu")) {
        return {
            QStringLiteral("qemu-kvm"),
            QStringLiteral("qemu-system-x86_64"),
            QStringLiteral("qemu-system-aarch64"),
            QStringLiteral("virtiofsd"),
            QStringLiteral("edk2-ovmf"),
        };
    }
    if (action == QStringLiteral("install-components")) {
        QStringList packages;
        const QJsonArray components = options.value(QStringLiteral("components")).toArray();
        for (const QJsonValue &value : components) {
            if (value.toString() == QStringLiteral("caddy")) {
                packages.append(QStringLiteral("caddy"));
            }
        }
        return packages;
    }
    return {};
}

bool SetupManager::helperCall(const QString &method, const QStringList &arguments, QString *error,
                              int timeoutMs) const
{
    return m_helper.call(method, QVariantList{QVariant::fromValue(arguments)}, error, timeoutMs);
}

bool SetupManager::helperAvailable() const
{
    return m_helper.available();
}

bool SetupManager::enablePodmanSocket(QString *error) const
{
    int exitCode = -1;
    runCommand(QStringLiteral("systemctl"),
               {QStringLiteral("--user"), QStringLiteral("enable"), QStringLiteral("--now"),
                QStringLiteral("podman.socket")},
               15000, &exitCode);
    if (exitCode != 0) {
        if (error) {
            *error = QStringLiteral("Não foi possível ativar o podman.socket de usuário.");
        }
        return false;
    }
    return true;
}

bool SetupManager::applyDockerPolicy(const QString &policy, QString *error)
{
    Config config = Config::load();
    config.set(QStringLiteral("setup.dockerPolicy"), policy);
    return config.save(error);
}

void SetupManager::runJob(const std::shared_ptr<Job> &job, const QJsonObject &options)
{
    appendLog(job, QStringLiteral("Iniciando %1…").arg(job->action));
    setProgress(job, 5);

    const auto cancelled = [&job] {
        return job->cancel.load();
    };

    if (job->action == QStringLiteral("remove-docker")) {
        appendLog(job, QStringLiteral("Removendo pacotes do Docker (containers, imagens, redes e "
                                      "volumes do Docker podem ser perdidos)…"));
        setProgress(job, 30);
        QString helperError;
        if (!helperCall(QStringLiteral("RemovePackages"),
                        {QStringLiteral("docker-ce"), QStringLiteral("docker-ce-cli"),
                         QStringLiteral("containerd.io"), QStringLiteral("docker.io"),
                         QStringLiteral("moby-engine")},
                        &helperError)) {
            finishJob(job, QStringLiteral("failed"), helperError);
            return;
        }
        if (cancelled()) {
            finishJob(job, QStringLiteral("cancelled"), {});
            return;
        }
        appendLog(job, QStringLiteral("Docker removido. O Podman foi mantido."));
        setProgress(job, 85);
        QString policyError;
        if (!applyDockerPolicy(QStringLiteral("removed"), &policyError)) {
            finishJob(job, QStringLiteral("failed"), policyError);
            return;
        }
        finishJob(job, QStringLiteral("success"), {});
        return;
    }

    if (job->action == QStringLiteral("enable-socket")) {
        appendLog(job, QStringLiteral("Ativando podman.socket de usuário…"));
        QString socketError;
        if (!enablePodmanSocket(&socketError)) {
            finishJob(job, QStringLiteral("failed"), socketError);
            return;
        }
        setProgress(job, 90);
        appendLog(job, QStringLiteral("podman.socket ativo."));
        finishJob(job, QStringLiteral("success"), {});
        return;
    }

    const QStringList packages = actionPackages(job->action, options);
    if (!packages.isEmpty()) {
        appendLog(job, QStringLiteral("Instalando: %1").arg(packages.join(QStringLiteral(", "))));
        setProgress(job, 20);
        QString helperError;
        if (!helperCall(QStringLiteral("InstallPackages"), packages, &helperError)) {
            finishJob(job, QStringLiteral("failed"), helperError);
            return;
        }
    }

    if (cancelled()) {
        finishJob(job, QStringLiteral("cancelled"), {});
        return;
    }

    setProgress(job, 60);
    appendLog(job, QStringLiteral("Ativando podman.socket de usuário…"));
    QString socketError;
    if (!enablePodmanSocket(&socketError)) {
        finishJob(job, QStringLiteral("failed"), socketError);
        return;
    }

    setProgress(job, 85);
    appendLog(job, QStringLiteral("Validando o Podman…"));
    const QString version = podmanVersion();
    if (version.isEmpty()) {
        finishJob(job, QStringLiteral("failed"),
                  QStringLiteral("O Podman não foi encontrado após a instalação."));
        return;
    }
    appendLog(job, QStringLiteral("Podman %1 pronto.").arg(version));
    finishJob(job, QStringLiteral("success"), {});
}

void SetupManager::finishJob(const std::shared_ptr<Job> &job, const QString &state,
                             const QString &error)
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        job->state = state;
        job->error = error;
        job->progress = state == QStringLiteral("success") ? 100 : job->progress;
        job->finishedAt = QDateTime::currentDateTimeUtc();
        if (m_activeJobId == job->id) {
            m_activeJobId.clear();
        }
    }
    if (!error.isEmpty()) {
        appendLog(job, QStringLiteral("Falhou: %1").arg(error));
    } else {
        appendLog(job, state == QStringLiteral("cancelled") ? QStringLiteral("Cancelado.")
                                                            : QStringLiteral("Concluído."));
    }

    QJsonObject payload;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        payload = jobToJsonLocked(*job);
    }
    QJsonArray checkArray;
    for (const SetupCheck &check : check(false)) {
        checkArray.append(checkJson(check));
    }
    payload.insert(QStringLiteral("checks"), checkArray);
    for (const Notify &notify : watchersFor(job->id)) {
        if (notify) {
            notify(QStringLiteral("setup.done"), payload);
        }
    }
}

bool SetupManager::complete(bool skipped, Error *error)
{
    Config config = Config::load();
    config.set(QStringLiteral("setup.completed"), !skipped);
    config.set(QStringLiteral("setup.completedAt"),
               skipped ? QJsonValue{} : QJsonValue(QDateTime::currentDateTimeUtc().toString(
                             Qt::ISODateWithMs)));
    if (!skipped) {
        config.set(QStringLiteral("setup.lastValidation"),
                   QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    }
    if (skipped) {
        config.set(QStringLiteral("app.showOnboarding"), false);
    }

    QString saveError;
    if (!config.save(&saveError)) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_CONFIG_WRITE_FAILED"),
                                 QStringLiteral("Não foi possível salvar a configuração."),
                                 saveError);
        }
        return false;
    }
    return true;
}

} // namespace PodStack
