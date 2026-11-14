// SPDX-License-Identifier: GPL-3.0-or-later
#include "cli/Internal.h"

namespace PodStack::CliInternal {



QTextStream &out()
{
    static QTextStream stream(stdout);
    stream.setEncoding(QStringConverter::Utf8);
    return stream;
}



QTextStream &err()
{
    static QTextStream stream(stderr);
    stream.setEncoding(QStringConverter::Utf8);
    return stream;
}



void printUsage()
{
    out() << "Uso: podstack <comando> [opções]\n\n"
          << "Comandos:\n"
          << "  version                 Mostra a versão\n"
          << "  status                  Estado do daemon/Podman\n"
          << "  ps [--all|--running|--stopped] [--format json] [--quiet]\n"
          << "                          Lista containers\n"
          << "  pause <id|nome>         Pausa um container\n"
          << "  unpause <id|nome>       Despausa um container\n"
          << "  kill [--signal S] <id>  Mata um container\n"
          << "  checkpoint <id> [--leave-running] [--tcp-established] [--json]\n"
          << "                          Cria um checkpoint CRIU do container\n"
          << "  restore <id> [--tcp-established] [--json]\n"
          << "                          Restaura o checkpoint CRIU do container\n"
          << "  inspect <id|nome>       Mostra o JSON do container\n"
          << "  exec <id|nome> -- <cmd...>\n"
          << "                          Executa um comando no container (hijack)\n"
          << "  images [ls|pull|push|rm|prune|history|tag|inspect|export|load]\n"
          << "                          Gerencia imagens\n"
          << "  volumes [ls|create|rm|prune|clone|export|import]\n"
          << "                          Gerencia volumes\n"
          << "  networks [ls|create|rm|connect|disconnect]\n"
          << "                          Gerencia redes\n"
          << "  pods [ls|create|start|stop|restart|rm]\n"
          << "                          Gerencia pods\n"
          << "  compose [ls|up|down|restart|stop|start|logs]\n"
          << "                          Projetos Docker Compose\n"
          << "  build <contexto> -t <tag> [-f Dockerfile] [--build-arg K=V]\n"
          << "        [--platform p1,p2]\n"
          << "                          Build de imagem\n"
          << "  builds [ls|rm]        Histórico de builds\n"
          << "  machine "
             "<list|info|logs|console|create|start|stop|restart|delete|rename|clone|default|"
             "distros|ssh|files|pull|push|port|snapshot|update>\n"
          << "                          Gerencia machines QEMU/KVM\n"
          << "  kube <info|env> [--cluster NOME]\n"
          << "                          Cluster Kubernetes e KUBECONFIG\n"
          << "  ssh [id|nome] [--x11|--x11-trusted] [-- cmd...]\n"
          << "                          Abre SSH (sem id usa a machine default)\n"
          << "  top [--once] [--interval S] [--json] [--plain]\n"
          << "                          Monitora CPU/memória/containers\n"
          << "  system df [--detailed] [--json]\n"
          << "                          Uso de disco e prévia de prune\n"
          << "  system disk [--json]    Espaço livre e aviso de disco baixo\n"
          << "  system emulation [--json]\n"
          << "  system logs [--follow|--since 7d|--level|--component|--json|--export|--clear]\n"
          << "                          Arquitetura do host e handlers binfmt/FEX\n"
          << "  setup [--check|--status|--validate]\n"
          << "        [--apply podman|qemu|components|socket]\n"
          << "        [--docker-impact [--json]]\n"
          << "        [--remove-docker --yes]\n"
          << "                          Setup/onboarding da máquina\n"
          << "  config <show|get|set|add|remove|reset|schema> [args]\n"
          << "                          Lê e altera configurações\n"
          << "  doctor                  Diagnóstico rápido\n"
          << "  update --check [--json] [--channel stable|beta]\n"
          << "  update --install [--yes] [--json] [--channel stable|beta]\n"
          << "                          Verifica updates assinados (exit 3 se houver)\n"
          << "  report [--include-logs] [--json]\n"
          << "                          Relatório redigido para suporte\n"
          << "  help                    Esta ajuda\n";
    out().flush();
}



int fail(const PodStack::Error &error)
{
    err() << error.message << '\n';
    if (!error.detail.isEmpty()) {
        err() << "  detalhe: " << error.detail << '\n';
    }
    if (!error.remediation.isEmpty()) {
        err() << "  ação: " << error.remediation << '\n';
    }
    err().flush();

    if (error.code == QStringLiteral("PODSTACK_DAEMON_UNAVAILABLE")
        || error.code == QStringLiteral("PODSTACK_ENGINE_UNAVAILABLE")) {
        return kExitEngineUnavailable;
    }
    return kExitError;
}



QString humanBytes(qint64 bytes)
{
    static const char *units[] = {"B", "KiB", "MiB", "GiB", "TiB"};
    double value = static_cast<double>(bytes);
    int unit = 0;
    while (value >= 1024.0 && unit < 4) {
        value /= 1024.0;
        ++unit;
    }
    return QString::number(value, 'f', unit == 0 ? 0 : 1) + QLatin1Char(' ')
           + QString::fromLatin1(units[unit]);
}



QString epochDate(qint64 epoch)
{
    return QDateTime::fromSecsSinceEpoch(epoch).toString(QStringLiteral("yyyy-MM-dd"));
}



void printMachineUsage(QTextStream &stream)
{
    stream << "Uso: podstack machine <comando> [opções]\n\n"
           << "Comandos:\n"
           << "  list [--json]            Lista machines\n"
           << "  info <id|nome> [--json]  Mostra os dados de uma machine\n"
           << "  logs <id|nome> [--tail N] [--json]\n"
           << "                          Mostra as últimas linhas do console.log\n"
           << "  console <id|nome> [--tail BYTES] [--no-follow] [--json]\n"
           << "                          Lê o console serial (follow por padrão; --json\n"
           << "                          só com --no-follow)\n"
           << "  create DISTRO[:VER] NOME [--cpus N] [--memory 4G] [--disk 20G]\n"
           << "         [--user U] [--user-data ARQUIVO] [--isolate-network]\n"
           << "         [--forward-ssh-agent] [--isolated]\n"
           << "                          Cria uma machine (a senha entra na T4-04)\n"
           << "  start <id|nome> [--json] Inicia uma machine\n"
           << "  stop <id|nome> [--force] [--json]\n"
           << "                          Para uma machine\n"
           << "  restart <id|nome> [--json]\n"
           << "                          Reinicia uma machine\n"
           << "  delete <id|nome> [--force] [--json]\n"
           << "                          Remove uma machine e seus dados\n"
           << "  rename <id|nome> <novo> [--json]\n"
           << "                          Renomeia uma machine (atualiza o SSH)\n"
           << "  clone <id|nome> <novo> [--json]\n"
           << "                          Cria uma cópia independente da machine\n"
           << "  update <id|nome> [--cpus N] [--memory 4G] [--disk 40G]\n"
           << "         [--forward-agent|--no-forward-agent] [--json]\n"
           << "                          Atualiza recursos ou o SSH agent\n"
           << "  port ls <id|nome> [--json]\n"
           << "                          Lista as portas encaminhadas da machine\n"
           << "  port add <id|nome> --host N --guest N [--udp] [--lan] [--label L]\n"
           << "                          Adiciona uma porta (reinicia se rodando)\n"
           << "  port rm <id|nome> <portId|hostPort> [--json]\n"
           << "                          Remove uma porta (a SSH é gerenciada)\n"
           << "  serial list <id|nome> [--json]\n"
           << "                          Lista portas seriais do host e as anexadas\n"
           << "  serial attach <id|nome> <porta> [--json]\n"
           << "                          Anexa uma porta serial (reinicia se rodando)\n"
           << "  serial detach <id|nome> <porta> [--json]\n"
           << "                          Desanexa uma porta serial\n"
           << "  usb list <id|nome> [--json]\n"
           << "                          Lista dispositivos USB do host e os anexados\n"
           << "  usb attach <id|nome> BUS:ADDR [--json]\n"
           << "                          Anexa um dispositivo USB (reinicia se rodando)\n"
           << "  usb detach <id|nome> BUS:ADDR [--json]\n"
           << "                          Desanexa um dispositivo USB\n"
           << "  audio <id|nome> on|off [--json]\n"
           << "                          Liga/desliga áudio PipeWire (reinicia se rodando)\n"
           << "  snapshot ls <id|nome> [--json]\n"
           << "                          Lista os snapshots internos do disco\n"
           << "  snapshot create <id|nome> <nome> [--json]\n"
           << "                          Cria um snapshot (para/reinicia se rodando)\n"
           << "  snapshot restore <id|nome> <nome> [--json]\n"
           << "                          Restaura o disco (substitui o estado atual)\n"
           << "  snapshot rm <id|nome> <nome> [--json]\n"
           << "                          Remove um snapshot do disco\n"
           << "  default [<id|nome>] [--json]\n"
           << "                          Mostra ou define a machine default\n"
           << "  ssh [id|nome] [--x11|--x11-trusted] [-- cmd...]\n"
           << "                          Abre SSH (sem id usa a machine default; -A se\n"
           << "                          o SSH agent estiver encaminhado; -X/-Y para X11)\n"
           << "  files ls <id|nome> [--path P] [--json]\n"
           << "                          Lista arquivos da machine via SSH\n"
           << "  pull <id|nome> <remoto> <local>\n"
           << "                          Baixa um arquivo/diretório da machine (scp)\n"
           << "  push <id|nome> <local> <remoto>\n"
           << "                          Envia um arquivo/diretório para a machine (scp)\n"
           << "  export <id|nome> <arquivo> [--force] [--json]\n"
           << "                          Exporta a machine para um .tar.zst (pausa)\n"
           << "  import <arquivo> [--name N] [--force] [--json]\n"
           << "                          Restaura uma machine de um .tar.zst\n"
           << "  distros [--json]         Lista imagens cloud e cache local\n";
    stream.flush();
}



bool parseSize(const QString &text, qint64 *bytes)
{
    if (!bytes) {
        return false;
    }

    QString value = text.trimmed();
    if (value.isEmpty()) {
        return false;
    }

    qint64 multiplier = 1;
    const QChar suffix = value.back().toUpper();
    if (suffix == QLatin1Char('K') || suffix == QLatin1Char('M')
        || suffix == QLatin1Char('G') || suffix == QLatin1Char('T')) {
        if (suffix == QLatin1Char('K')) {
            multiplier = 1024LL;
        } else if (suffix == QLatin1Char('M')) {
            multiplier = 1024LL * 1024LL;
        } else if (suffix == QLatin1Char('G')) {
            multiplier = 1024LL * 1024LL * 1024LL;
        } else {
            multiplier = 1024LL * 1024LL * 1024LL * 1024LL;
        }
        value.chop(1);
    }

    bool ok = false;
    const qint64 number = value.toLongLong(&ok);
    if (!ok || number <= 0
        || number > std::numeric_limits<qint64>::max() / multiplier) {
        return false;
    }
    *bytes = number * multiplier;
    return true;
}



PodStack::Error errorFromJson(const QJsonObject &object)
{
    PodStack::Error error;
    error.code = object.value(QStringLiteral("code")).toString();
    error.message = object.value(QStringLiteral("message")).toString();
    error.detail = object.value(QStringLiteral("detail")).toString();
    error.remediation = object.value(QStringLiteral("remediation")).toString();
    error.retryable = object.value(QStringLiteral("retryable")).toBool();
    if (error.message.isEmpty()) {
        error.message = QStringLiteral("Falha ao criar a machine.");
    }
    if (error.code.isEmpty()) {
        error.code = QStringLiteral("PODSTACK_INTERNAL");
    }
    return error;
}



QString formatSize(qint64 bytes)
{
    constexpr double kGiB = 1024.0 * 1024.0 * 1024.0;
    if (bytes <= 0) {
        return QStringLiteral("-");
    }
    return QStringLiteral("%1 GiB").arg(QString::number(bytes / kGiB, 'f', 1));
}



QString machineSsh(const QJsonObject &machine)
{
    const int port = machine.value(QStringLiteral("sshPort")).toInt();
    if (port <= 0) {
        return QStringLiteral("-");
    }
    QString user = machine.value(QStringLiteral("sshUser")).toString();
    if (user.isEmpty()) {
        user = QStringLiteral("podstack");
    }
    return QStringLiteral("%1@127.0.0.1:%2").arg(user).arg(port);
}



QString machineDistroLabel(const QJsonObject &machine)
{
    QString distro = machine.value(QStringLiteral("distro")).toString();
    const QString version = machine.value(QStringLiteral("distroVersion")).toString();
    if (!version.isEmpty() && !distro.contains(QLatin1Char(':'))) {
        distro += QLatin1Char(':') + version;
    }
    return distro;
}



bool machinePortIsSsh(const QJsonObject &port)
{
    if (port.value(QStringLiteral("guestPort")).toInt() != 22) {
        return false;
    }
    const QString protocol = port.value(QStringLiteral("protocol")).toString();
    return protocol.isEmpty() || protocol.compare(QStringLiteral("tcp"), Qt::CaseInsensitive) == 0;
}



QJsonArray machinePortsWithoutSsh(const QJsonArray &ports)
{
    QJsonArray result;
    for (const QJsonValue &value : ports) {
        const QJsonObject port = value.toObject();
        if (!machinePortIsSsh(port)) {
            result.append(port);
        }
    }
    return result;
}



QString machinePortLabel(const QJsonObject &machine)
{
    const QString name = machine.value(QStringLiteral("name")).toString();
    return name.isEmpty() ? machine.value(QStringLiteral("id")).toString() : name;
}



void printTopSample(const QJsonObject &sample)
{
    const QJsonObject host = sample.value(QStringLiteral("host")).toObject();
    const QJsonObject totals = sample.value(QStringLiteral("totals")).toObject();

    out() << "HOST   cpu " << QString::number(host.value(QStringLiteral("cpuPercent")).toDouble(),
                                             'f', 1)
          << "%  mem " << humanBytes(host.value(QStringLiteral("memUsed")).toInteger()) << " / "
          << humanBytes(host.value(QStringLiteral("memTotal")).toInteger()) << "  net "
          << humanBytes(host.value(QStringLiteral("netRx")).toInteger()) << "/s rx, "
          << humanBytes(host.value(QStringLiteral("netTx")).toInteger()) << "/s tx\n";
    out() << "TOTAL  cpu "
          << QString::number(totals.value(QStringLiteral("cpuPercent")).toDouble(), 'f', 1)
          << "%  mem " << humanBytes(totals.value(QStringLiteral("memUsed")).toInteger())
          << " / " << humanBytes(totals.value(QStringLiteral("memLimit")).toInteger())
          << "  pids " << totals.value(QStringLiteral("pids")).toInt() << '\n';

    struct Row {
        QString name;
        double cpu = 0.0;
        qint64 mem = 0;
        int pids = 0;
        qint64 rx = 0;
        qint64 tx = 0;
    };
    QVector<Row> rows;
    int nameWidth = 4;
    for (const QJsonValue &value : sample.value(QStringLiteral("containers")).toArray()) {
        const QJsonObject container = value.toObject();
        Row row;
        row.name = container.value(QStringLiteral("name")).toString();
        if (row.name.isEmpty()) {
            row.name = container.value(QStringLiteral("id")).toString().left(12);
        }
        row.cpu = container.value(QStringLiteral("cpuPercent")).toDouble();
        row.mem = container.value(QStringLiteral("memUsed")).toInteger();
        row.pids = container.value(QStringLiteral("pids")).toInt();
        row.rx = container.value(QStringLiteral("netRx")).toInteger();
        row.tx = container.value(QStringLiteral("netTx")).toInteger();
        nameWidth = qMax(nameWidth, row.name.size());
        rows.append(row);
    }

    out() << QStringLiteral("%1  %2  %3  %4  %5\n")
                 .arg(QStringLiteral("NOME").leftJustified(nameWidth))
                 .arg(QStringLiteral("CPU%").rightJustified(6))
                 .arg(QStringLiteral("MEM").rightJustified(10))
                 .arg(QStringLiteral("PIDS").rightJustified(5))
                 .arg(QStringLiteral("NET RX/TX"));
    for (const Row &row : rows) {
        out() << QStringLiteral("%1  %2  %3  %4  %5/%6\n")
                     .arg(row.name.leftJustified(nameWidth))
                     .arg(QString::number(row.cpu, 'f', 1).rightJustified(6))
                     .arg(humanBytes(row.mem).rightJustified(10))
                     .arg(QString::number(row.pids).rightJustified(5))
                     .arg(humanBytes(row.rx), humanBytes(row.tx));
    }
    out().flush();
}



QString imageLabel(const QJsonObject &item)
{
    const QString repository = item.value(QStringLiteral("repository")).toString();
    const QString tag = item.value(QStringLiteral("tag")).toString();
    if (repository.isEmpty() || repository == QStringLiteral("<none>")) {
        return QStringLiteral("<none>:<none>");
    }
    return tag.isEmpty() ? repository : repository + QLatin1Char(':') + tag;
}



int levelRank(const QString &level)
{
    if (level == QStringLiteral("error")) {
        return 0;
    }
    if (level == QStringLiteral("warn")) {
        return 1;
    }
    if (level == QStringLiteral("debug")) {
        return 3;
    }
    if (level == QStringLiteral("trace")) {
        return 4;
    }
    return 2;
}



bool validLogLevel(const QString &level)
{
    return level == QStringLiteral("error") || level == QStringLiteral("warn") ||
           level == QStringLiteral("info") || level == QStringLiteral("debug") ||
           level == QStringLiteral("trace");
}



bool validLogComponent(const QString &component)
{
    static const QStringList components{QStringLiteral("daemon"), QStringLiteral("gui"),
                                        QStringLiteral("cli"),    QStringLiteral("qml"),
                                        QStringLiteral("setup"),  QStringLiteral("update"),
                                        QStringLiteral("helper"), QStringLiteral("package")};
    return components.contains(component);
}



QDate parseSince(const QString &text)
{
    const QString value = text.trimmed().toLower();
    if (value.endsWith(QLatin1Char('d'))) {
        bool ok = false;
        const int days = value.chopped(1).toInt(&ok);
        if (ok && days > 0 && days <= 30) {
            return QDate::currentDate().addDays(-(days - 1));
        }
        return {};
    }
    return QDate::fromString(value.left(10), Qt::ISODate);
}



void printKubeUsage(QTextStream &stream)
{
    stream << "Uso: podstack kube <comando> [opções]\n\n"
           << "Comandos:\n"
           << "  info [--cluster NOME] [--json]  Estado do cluster\n"
           << "  env [--cluster NOME]            Imprime export KUBECONFIG=...\n"
           << "  start                           Cria a VM e instala o k3s\n"
           << "  stop|disable [--force]          Remove a VM e o kubeconfig\n"
           << "  context [use <nome>]            Mostra ou troca o contexto\n";
    stream.flush();
}



int kubeStatus(const QString &cluster, QJsonObject *result, PodStack::Error *error)
{
    PodStack::RpcClient client;
    *result = client.call(QStringLiteral("k8s.status"),
                          QJsonObject{{QStringLiteral("cluster"), cluster}}, error);
    return error->isEmpty() ? kExitOk : fail(*error);
}



void printConfigUsage(QTextStream &stream)
{
    stream << "Uso: podstack config <comando> [opções]\n\n"
           << "Comandos:\n"
           << "  show [--json]           Lista chaves e valores\n"
           << "  get <chave> [--json]    Mostra uma chave\n"
           << "  set <chave> <valor>     Define uma chave (valida tipo/faixa/enum)\n"
           << "  add <chave> <valor>     Adiciona item a uma lista\n"
           << "  remove <chave> <valor>  Remove item de uma lista\n"
           << "  reset [chave] [--yes]   Restaura uma chave ou todas\n"
           << "  schema [--json]         Mostra o schema (86 chaves)\n";
    stream.flush();
}



QString formatConfigValue(const QJsonValue &value)
{
    if (value.isNull() || value.isUndefined()) {
        return QStringLiteral("null");
    }
    if (value.isBool()) {
        return value.toBool() ? QStringLiteral("true") : QStringLiteral("false");
    }
    if (value.isDouble()) {
        const double raw = value.toDouble();
        if (std::floor(raw) == raw) {
            return QString::number(static_cast<qint64>(raw));
        }
        return QString::number(raw);
    }
    if (value.isArray()) {
        QStringList entries;
        for (const QJsonValue &entry : value.toArray()) {
            entries.append(entry.toString());
        }
        return entries.join(QLatin1Char(','));
    }
    if (value.isObject()) {
        return QString::fromUtf8(
            QJsonDocument(value.toObject()).toJson(QJsonDocument::Compact));
    }
    return value.toString();
}



bool fetchConfigSchema(QHash<QString, QJsonObject> *schema, PodStack::Error *error)
{
    PodStack::RpcClient client;
    const QJsonObject result =
        client.call(QStringLiteral("settings.schema"), {}, error);
    if (error && !error->isEmpty()) {
        return false;
    }
    schema->clear();
    for (const QJsonValue &value : result.value(QStringLiteral("schema")).toArray()) {
        const QJsonObject definition = value.toObject();
        const QString key = definition.value(QStringLiteral("key")).toString();
        if (!key.isEmpty()) {
            schema->insert(key, definition);
        }
    }
    return true;
}



bool parseConfigValue(const QJsonObject &definition, const QString &raw,
                      QJsonValue *value, QString *error)
{
    const QString type = definition.value(QStringLiteral("type")).toString();
    const QString trimmed = raw.trimmed();

    if (type == QStringLiteral("bool")) {
        const QString normalized = trimmed.toLower();
        if (normalized == QStringLiteral("true") || normalized == QStringLiteral("1")
            || normalized == QStringLiteral("sim") || normalized == QStringLiteral("yes")) {
            *value = true;
            return true;
        }
        if (normalized == QStringLiteral("false") || normalized == QStringLiteral("0")
            || normalized == QStringLiteral("não") || normalized == QStringLiteral("nao")
            || normalized == QStringLiteral("no")) {
            *value = false;
            return true;
        }
        *error = QStringLiteral("esperado true ou false.");
        return false;
    }

    if (type == QStringLiteral("int") || type == QStringLiteral("int64")) {
        bool ok = false;
        const qint64 number = trimmed.toLongLong(&ok);
        if (!ok) {
            *error = QStringLiteral("esperado um número inteiro.");
            return false;
        }
        *value = static_cast<double>(number);
        return true;
    }

    if (type == QStringLiteral("stringList")) {
        if (trimmed.startsWith(QLatin1Char('['))) {
            QJsonParseError parseError{};
            const QJsonDocument document =
                QJsonDocument::fromJson(trimmed.toUtf8(), &parseError);
            if (parseError.error != QJsonParseError::NoError || !document.isArray()) {
                *error = QStringLiteral("lista JSON inválida.");
                return false;
            }
            *value = document.array();
            return true;
        }
        QJsonArray entries;
        const QStringList parts = trimmed.split(QLatin1Char(','), Qt::SkipEmptyParts);
        for (const QString &part : parts) {
            const QString entry = part.trimmed();
            if (!entry.isEmpty()) {
                entries.append(entry);
            }
        }
        *value = entries;
        return true;
    }

    if (type == QStringLiteral("timestamp")) {
        if (trimmed.isEmpty() || trimmed.compare(QStringLiteral("null"),
                                                 Qt::CaseInsensitive)
                                     == 0) {
            *value = QJsonValue::Null;
            return true;
        }
        *value = raw;
        return true;
    }

    if (type == QStringLiteral("string") || type == QStringLiteral("enum")) {
        const bool nullable = definition.value(QStringLiteral("nullable")).toBool();
        if (nullable && (trimmed.isEmpty()
                         || trimmed.compare(QStringLiteral("null"), Qt::CaseInsensitive)
                                == 0)) {
            *value = QJsonValue::Null;
            return true;
        }
        *value = raw;
        return true;
    }

    *error = QStringLiteral("tipo de schema desconhecido: %1.").arg(type);
    return false;
}

} // namespace PodStack::CliInternal
