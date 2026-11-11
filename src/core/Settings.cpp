// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/Settings.h"

#include "core/Proxy.h"

#include <QDateTime>
#include <QHostAddress>
#include <QJsonArray>
#include <QUrl>

#include <cmath>

namespace PodStack::Settings {
namespace {

constexpr double kGiB = 1024.0 * 1024.0 * 1024.0;
constexpr double kTiB = 1024.0 * kGiB;
constexpr double kMaxSafeInteger = 4503599627370496.0; // 2^52

SettingDef makeBool(const QString &key, bool value, const QString &category,
                    const QString &description, bool restart = false)
{
    SettingDef definition;
    definition.key = key;
    definition.type = QStringLiteral("bool");
    definition.defaultValue = value;
    definition.category = category;
    definition.description = description;
    definition.restartRequired = restart;
    return definition;
}

SettingDef makeInt(const QString &key, int value, double min, double max,
                   const QString &category, const QString &description,
                   bool restart = false)
{
    SettingDef definition;
    definition.key = key;
    definition.type = QStringLiteral("int");
    definition.defaultValue = value;
    definition.min = min;
    definition.max = max;
    definition.category = category;
    definition.description = description;
    definition.restartRequired = restart;
    return definition;
}

SettingDef makeInt64(const QString &key, qint64 value, double min, double max,
                     const QString &category, const QString &description,
                     bool restart = false)
{
    SettingDef definition;
    definition.key = key;
    definition.type = QStringLiteral("int64");
    definition.defaultValue = value;
    definition.min = min;
    definition.max = max;
    definition.category = category;
    definition.description = description;
    definition.restartRequired = restart;
    return definition;
}

SettingDef makeString(const QString &key, const QJsonValue &value, const QString &category,
                      const QString &description, bool restart = false)
{
    SettingDef definition;
    definition.key = key;
    definition.type = QStringLiteral("string");
    definition.defaultValue = value;
    definition.category = category;
    definition.description = description;
    definition.restartRequired = restart;
    return definition;
}

SettingDef makeEnum(const QString &key, const QString &value, const QStringList &choices,
                    const QString &category, const QString &description,
                    bool restart = false)
{
    SettingDef definition;
    definition.key = key;
    definition.type = QStringLiteral("enum");
    definition.defaultValue = value;
    definition.enumValues = choices;
    definition.category = category;
    definition.description = description;
    definition.restartRequired = restart;
    return definition;
}

SettingDef makeStringList(const QString &key, const QString &category,
                          const QString &description, bool restart = false)
{
    SettingDef definition;
    definition.key = key;
    definition.type = QStringLiteral("stringList");
    definition.defaultValue = QJsonArray{};
    definition.category = category;
    definition.description = description;
    definition.restartRequired = restart;
    return definition;
}

SettingDef makeTimestamp(const QString &key, const QString &category,
                         const QString &description)
{
    SettingDef definition;
    definition.key = key;
    definition.type = QStringLiteral("timestamp");
    definition.defaultValue = QJsonValue::Null;
    definition.category = category;
    definition.description = description;
    return definition;
}

bool isRfc1918(const QString &value)
{
    const QStringList parts = value.split(QLatin1Char('/'));
    if (parts.size() != 2 || parts.at(0).isEmpty() || parts.at(1).isEmpty()) {
        return false;
    }
    bool prefixOk = false;
    const int prefix = parts.at(1).toInt(&prefixOk);
    if (!prefixOk || prefix < 0 || prefix > 32) {
        return false;
    }

    QHostAddress address;
    if (!address.setAddress(parts.at(0))
        || address.protocol() != QAbstractSocket::IPv4Protocol) {
        return false;
    }

    const quint32 raw = address.toIPv4Address();
    if ((raw & 0xff000000U) == 0x0a000000U) { // 10.0.0.0/8
        return prefix >= 8;
    }
    if ((raw & 0xfff00000U) == 0xac100000U) { // 172.16.0.0/12
        return prefix >= 12;
    }
    if ((raw & 0xffff0000U) == 0xc0a80000U) { // 192.168.0.0/16
        return prefix >= 16;
    }
    return false;
}

bool parseTimestamp(const QString &value)
{
    const QDateTime withMs = QDateTime::fromString(value, Qt::ISODateWithMs);
    if (withMs.isValid()) {
        return true;
    }
    const QDateTime dateOnly = QDateTime::fromString(value, Qt::ISODate);
    if (dateOnly.isValid()) {
        return true;
    }
    const QDate date = QDate::fromString(value, Qt::ISODate);
    return date.isValid();
}

Error validationError(const SettingDef &definition, const QString &reason,
                      const QString &remediation = {})
{
    const QString action =
        remediation.isEmpty()
            ? QStringLiteral("Consulte `podstack config schema` para tipo, faixa e valores "
                             "aceitos.")
            : remediation;
    return Error::make(QStringLiteral("PODSTACK_VALIDATION"),
                       QStringLiteral("Valor inválido para %1: %2")
                           .arg(definition.key, reason),
                       definition.key, action);
}

void insertNested(QJsonObject &object, const QStringList &parts, int index,
                  const QJsonValue &value)
{
    if (index == parts.size() - 1) {
        object.insert(parts.at(index), value);
        return;
    }
    QJsonObject child = object.value(parts.at(index)).toObject();
    insertNested(child, parts, index + 1, value);
    object.insert(parts.at(index), child);
}

} // namespace

const QVector<SettingDef> &definitions()
{
    static const QVector<SettingDef> defs = [] {
        QVector<SettingDef> list;
        list.reserve(86);

        // Geral
        list.append(makeBool(QStringLiteral("app.startAtLogin"), false,
                             QStringLiteral("Geral"),
                             QStringLiteral("Cria o autostart com --hidden no login.")));
        list.append(makeBool(QStringLiteral("app.waitForContainerStop"), true,
                             QStringLiteral("Geral"),
                             QStringLiteral("Aguarda containers pararem ao sair.")));
        list.append(makeBool(QStringLiteral("app.keepRunningInTray"), true,
                             QStringLiteral("Geral"),
                             QStringLiteral("Fechar a janela esconde para a bandeja.")));
        list.append(makeBool(QStringLiteral("app.showInTray"), true, QStringLiteral("Geral"),
                             QStringLiteral("Mostra o ícone na bandeja.")));
        list.append(makeBool(QStringLiteral("app.notifications"), true,
                             QStringLiteral("Geral"),
                             QStringLiteral("Notifica falha, OOM e unhealthy.")));
        list.append(makeInt(QStringLiteral("notifications.diskThresholdPercent"), 90, 50, 99,
                            QStringLiteral("Notificações"),
                            QStringLiteral("Percentual de uso que dispara o aviso de disco "
                                           "baixo.")));
        list.append(makeEnum(QStringLiteral("app.theme"), QStringLiteral("system"),
                             {QStringLiteral("system"), QStringLiteral("dark"),
                              QStringLiteral("light")},
                             QStringLiteral("Geral"), QStringLiteral("Tema da UI.")));
        list.append(makeInt(QStringLiteral("app.fontScale"), 100, 80, 150, QStringLiteral("Geral"),
                            QStringLiteral("Escala da fonte da interface (%).")));
        list.append(makeEnum(QStringLiteral("app.language"), QStringLiteral("pt-BR"),
                             {QStringLiteral("pt-BR"), QStringLiteral("en-US")},
                             QStringLiteral("Geral"),
                             QStringLiteral("Idioma da interface (pt-BR ou en-US)."), true));
        list.append(makeBool(QStringLiteral("app.confirmDestructive"), true,
                             QStringLiteral("Geral"),
                             QStringLiteral("Confirma ações destrutivas.")));
        list.append(makeBool(QStringLiteral("app.showOnboarding"), true,
                             QStringLiteral("Geral"),
                             QStringLiteral("Exibe onboarding se incompleto.")));

        // Engine
        list.append(makeEnum(QStringLiteral("engine.mode"), QStringLiteral("rootless"),
                             {QStringLiteral("rootless"), QStringLiteral("rootful")},
                             QStringLiteral("Engine"),
                             QStringLiteral("Modo de conexão com o Podman."), true));
        list.append(makeString(QStringLiteral("engine.socketPath"), QJsonValue::Null,
                               QStringLiteral("Engine"),
                               QStringLiteral("Override do socket (null = autodetectar)."),
                               true));
        list.append(makeBool(QStringLiteral("engine.autoStart"), true,
                             QStringLiteral("Engine"),
                             QStringLiteral("Habilita podman.socket se necessário.")));
        list.append(makeBool(QStringLiteral("engine.restartOnConfigChange"), true,
                             QStringLiteral("Engine"),
                             QStringLiteral("Reinicia o engine ao mudar config do Docker.")));

        // Docker
        list.append(makeBool(QStringLiteral("docker.exposePortsToLan"), true,
                             QStringLiteral("Docker"),
                             QStringLiteral("Publica portas em 0.0.0.0.")));
        list.append(makeBool(QStringLiteral("docker.enableContainerdImageStore"), false,
                             QStringLiteral("Docker"),
                             QStringLiteral("Usa o image store do containerd."), true));
        list.append(makeString(QStringLiteral("docker.daemonJsonPath"),
                               QStringLiteral("~/.config/containers/containers.conf"),
                               QStringLiteral("Docker"),
                               QStringLiteral("Arquivo de configuração do engine."), true));
        list.append(makeString(QStringLiteral("docker.defaultNetwork"),
                               QStringLiteral("podman"), QStringLiteral("Docker"),
                               QStringLiteral("Rede default de novos containers.")));

        // Rede
        list.append(makeBool(QStringLiteral("network.https"), false, QStringLiteral("Rede"),
                             QStringLiteral("HTTPS automático para domínios."), true));
        SettingDef proxy = makeString(QStringLiteral("network.proxy"),
                                      QStringLiteral("auto"), QStringLiteral("Rede"),
                                      QStringLiteral("none, auto ou URL de proxy."));
        proxy.secret = true;
        list.append(proxy);
        list.append(makeStringList(QStringLiteral("network.proxyExclude"),
                                   QStringLiteral("Rede"),
                                   QStringLiteral("Hosts/CIDRs fora do proxy.")));
        list.append(makeString(QStringLiteral("network.subnet4"),
                               QStringLiteral("192.168.127.0/24"), QStringLiteral("Rede"),
                               QStringLiteral("Subnet RFC 1918 da bridge PodStack."), true));
        list.append(makeBool(QStringLiteral("network.bridge"), true, QStringLiteral("Rede"),
                             QStringLiteral("Permite acesso a domínios/IPs dos containers."),
                             true));
        list.append(makeBool(QStringLiteral("network.ipv6"), false, QStringLiteral("Rede"),
                             QStringLiteral("Habilita IPv6 nos containers."), true));
        list.append(makeBool(QStringLiteral("network.exposeLan"), false,
                             QStringLiteral("Rede"),
                             QStringLiteral("Expõe portas e domínios na LAN."), true));
        list.append(makeEnum(QStringLiteral("network.domainsMode"),
                             QStringLiteral("localhost"),
                             {QStringLiteral("localhost"),
                              QStringLiteral("podstack-local")},
                             QStringLiteral("Rede"),
                             QStringLiteral("Mecanismo de domínios locais."), true));
        list.append(makeInt(QStringLiteral("network.httpsPort"), 8443, 1, 65535,
                            QStringLiteral("Rede"),
                            QStringLiteral("Porta HTTPS do proxy."), true));
        list.append(makeInt(QStringLiteral("network.httpPort"), 8080, 1, 65535,
                            QStringLiteral("Rede"),
                            QStringLiteral("Porta HTTP do proxy."), true));

        // Máquinas
        list.append(makeString(QStringLiteral("machines.default"), QJsonValue::Null,
                               QStringLiteral("Máquinas"),
                               QStringLiteral("Machine default do CLI (null = nenhuma).")));
        list.append(makeBool(QStringLiteral("machines.forwardPorts"), true,
                             QStringLiteral("Máquinas"),
                             QStringLiteral("Encaminha portas da machine ao host.")));
        list.append(makeBool(QStringLiteral("machines.exposePortsToLan"), false,
                             QStringLiteral("Máquinas"),
                             QStringLiteral("Expõe portas da machine na LAN.")));
        list.append(makeInt(QStringLiteral("machines.cpus"), 2, 1, 1024,
                            QStringLiteral("Máquinas"),
                            QStringLiteral("CPUs default de novas machines.")));
        list.append(makeInt64(QStringLiteral("machines.memoryBytes"), 4294967296LL, kGiB,
                              kTiB, QStringLiteral("Máquinas"),
                              QStringLiteral("Memória default (mínimo 1 GiB).")));
        list.append(makeInt64(QStringLiteral("machines.diskBytes"), 21474836480LL, kGiB,
                              kTiB, QStringLiteral("Máquinas"),
                              QStringLiteral("Disco default (mínimo 1 GiB).")));
        list.append(makeInt(QStringLiteral("machines.sshPortStart"), 32222, 1, 65535,
                            QStringLiteral("Máquinas"),
                            QStringLiteral("Primeira porta SSH encaminhada."), true));
        list.append(makeBool(QStringLiteral("machines.forwardSshAgent"), false,
                             QStringLiteral("Máquinas"),
                             QStringLiteral("Default de agent forwarding.")));
        list.append(makeBool(QStringLiteral("machines.isolated"), false,
                             QStringLiteral("Máquinas"),
                             QStringLiteral("Default de isolamento de rede.")));
        list.append(makeBool(QStringLiteral("machines.autoSuspend"), false,
                             QStringLiteral("Máquinas"),
                             QStringLiteral("Suspende VMs ociosas.")));
        list.append(makeBool(QStringLiteral("ssh.exposePort"), false,
                             QStringLiteral("Máquinas"),
                             QStringLiteral("Expõe SSH na LAN."), true));
        list.append(makeBool(QStringLiteral("ssh.forwardAgent"), false,
                             QStringLiteral("Máquinas"),
                             QStringLiteral("Encaminha SSH_AUTH_SOCK.")));
        list.append(makeBool(QStringLiteral("ssh.manageConfig"), true,
                             QStringLiteral("Máquinas"),
                             QStringLiteral("Escreve blocos em ~/.ssh/config.")));
        list.append(makeString(QStringLiteral("ssh.configPath"),
                               QStringLiteral("~/.ssh/config"), QStringLiteral("Máquinas"),
                               QStringLiteral("Caminho do config SSH.")));
        list.append(makeEnum(QStringLiteral("ssh.keyType"), QStringLiteral("ed25519"),
                             {QStringLiteral("ed25519"), QStringLiteral("rsa"),
                              QStringLiteral("ecdsa")},
                             QStringLiteral("Máquinas"),
                             QStringLiteral("Tipo de chave por machine.")));

        // Kubernetes
        list.append(makeBool(QStringLiteral("kubernetes.enabled"), false,
                             QStringLiteral("Kubernetes"),
                             QStringLiteral("Cluster local habilitado."), true));
        list.append(makeBool(QStringLiteral("kubernetes.exposeServices"), false,
                             QStringLiteral("Kubernetes"),
                             QStringLiteral("Expõe Services na LAN.")));
        list.append(makeBool(QStringLiteral("kubernetes.kubeconfigUseDomain"), true,
                             QStringLiteral("Kubernetes"),
                             QStringLiteral("Usa domínio no kubeconfig."), true));
        list.append(makeStringList(QStringLiteral("kubernetes.tlsSan"),
                                   QStringLiteral("Kubernetes"),
                                   QStringLiteral("SANs extras do API server."), true));
        list.append(makeInt(QStringLiteral("kubernetes.cpus"), 2, 1, 1024,
                            QStringLiteral("Kubernetes"),
                            QStringLiteral("CPUs da VM do cluster."), true));
        list.append(makeInt64(QStringLiteral("kubernetes.memoryBytes"), 4294967296LL, kGiB,
                              kTiB, QStringLiteral("Kubernetes"),
                              QStringLiteral("Memória da VM do cluster."), true));
        list.append(makeInt64(QStringLiteral("kubernetes.diskBytes"), 21474836480LL, kGiB,
                              kTiB, QStringLiteral("Kubernetes"),
                              QStringLiteral("Disco da VM do cluster."), true));
        list.append(makeString(QStringLiteral("kubernetes.kubeconfigPath"),
                               QStringLiteral("~/.local/share/podstack/kubeconfigs/"),
                               QStringLiteral("Kubernetes"),
                               QStringLiteral("Diretório de kubeconfigs.")));
        list.append(makeString(QStringLiteral("kubernetes.contextPrefix"),
                               QStringLiteral("podstack-"), QStringLiteral("Kubernetes"),
                               QStringLiteral("Prefixo de contexto.")));

        // Terminal e logs
        list.append(makeString(QStringLiteral("terminal.fontFamily"),
                               QStringLiteral("monospace"),
                               QStringLiteral("Terminal/Logs"),
                               QStringLiteral("Fonte do terminal.")));
        list.append(makeInt(QStringLiteral("terminal.fontSize"), 12, 8, 32,
                            QStringLiteral("Terminal/Logs"),
                            QStringLiteral("Tamanho da fonte do terminal.")));
        list.append(makeBool(QStringLiteral("terminal.fontBold"), true,
                             QStringLiteral("Terminal/Logs"),
                             QStringLiteral("Negrito no terminal.")));
        list.append(makeInt(QStringLiteral("terminal.scrollback"), 10000, 1000, 100000,
                            QStringLiteral("Terminal/Logs"),
                            QStringLiteral("Linhas de histórico do terminal.")));
        list.append(makeInt(QStringLiteral("logs.fontSize"), 12, 8, 32,
                            QStringLiteral("Terminal/Logs"),
                            QStringLiteral("Tamanho da fonte dos logs.")));
        list.append(makeBool(QStringLiteral("logs.wrapLines"), false,
                             QStringLiteral("Terminal/Logs"),
                             QStringLiteral("Quebra de linha nos logs.")));
        list.append(makeEnum(QStringLiteral("logs.searchMode"), QStringLiteral("filter"),
                             {QStringLiteral("filter"), QStringLiteral("highlight")},
                             QStringLiteral("Terminal/Logs"),
                             QStringLiteral("Comportamento da busca nos logs.")));
        list.append(makeBool(QStringLiteral("logs.prettyJson"), true,
                             QStringLiteral("Terminal/Logs"),
                             QStringLiteral("Destaca e formata JSON.")));
        list.append(makeInt(QStringLiteral("logs.bufferLines"), 10000, 100, 1000000,
                            QStringLiteral("Terminal/Logs"),
                            QStringLiteral("Buffer de linhas por sessão.")));

        // Updates e diagnóstico
        list.append(makeEnum(QStringLiteral("updates.mode"), QStringLiteral("check"),
                             {QStringLiteral("off"), QStringLiteral("check"),
                              QStringLiteral("auto")},
                             QStringLiteral("Updates"),
                             QStringLiteral("Política de atualização.")));
        list.append(makeEnum(QStringLiteral("updates.channel"), QStringLiteral("stable"),
                             {QStringLiteral("stable"), QStringLiteral("beta")},
                             QStringLiteral("Updates"), QStringLiteral("Canal de updates.")));
        list.append(makeTimestamp(QStringLiteral("updates.lastCheck"), QStringLiteral("Updates"),
                                  QStringLiteral("Última verificação de updates.")));
        list.append(makeString(
            QStringLiteral("updates.feedUrl"),
            QStringLiteral("https://github.com/marcos2872/podstack/releases/latest/download/"
                           "podstack-update.json"),
            QStringLiteral("Updates"),
            QStringLiteral("URL do manifesto assinado de updates (https; http só em loopback).")));
        list.append(makeString(
            QStringLiteral("updates.publicKey"), QJsonValue(), QStringLiteral("Updates"),
            QStringLiteral("Chave pública Ed25519 (base64/minisign) usada para validar o feed; "
                           "null usa a chave embutida.")));
        list.append(makeBool(QStringLiteral("diagnostics.includeLogs"), true,
                             QStringLiteral("Updates"),
                             QStringLiteral("Inclui logs no report.")));
        list.append(makeBool(QStringLiteral("diagnostics.redact"), true,
                             QStringLiteral("Updates"),
                             QStringLiteral("Redige segredos no report.")));
        list.append(makeBool(QStringLiteral("telemetry.enabled"), false,
                             QStringLiteral("Updates"),
                             QStringLiteral("Master switch da telemetria (opt-in).")));
        list.append(makeBool(QStringLiteral("telemetry.crashReports"), false,
                             QStringLiteral("Updates"),
                             QStringLiteral("Envia relatórios de crash anônimos.")));
        list.append(makeBool(QStringLiteral("telemetry.usage"), false,
                             QStringLiteral("Updates"),
                             QStringLiteral("Envia uso agregado de features.")));
        list.append(makeBool(QStringLiteral("telemetry.performance"), false,
                             QStringLiteral("Updates"),
                             QStringLiteral("Envia métricas de performance.")));
        list.append(makeBool(QStringLiteral("telemetry.errors"), false,
                             QStringLiteral("Updates"),
                             QStringLiteral("Envia erros não fatais.")));

        // Avançado
        list.append(makeBool(QStringLiteral("advanced.helper.enabled"), false,
                             QStringLiteral("Avançado"),
                             QStringLiteral("Permite instalar o helper privilegiado."), true));
        list.append(makeBool(QStringLiteral("advanced.dns.install"), false,
                             QStringLiteral("Avançado"),
                             QStringLiteral("Instala drop-in do resolved."), true));
        list.append(makeBool(QStringLiteral("advanced.ca.install"), false,
                             QStringLiteral("Avançado"),
                             QStringLiteral("Confia na CA local."), true));
        list.append(makeEnum(QStringLiteral("advanced.logLevel"), QStringLiteral("info"),
                             {QStringLiteral("error"), QStringLiteral("warn"),
                              QStringLiteral("info"), QStringLiteral("debug"),
                              QStringLiteral("trace")},
                             QStringLiteral("Avançado"),
                             QStringLiteral("Nível de log do daemon."), true));
        list.append(makeInt(QStringLiteral("advanced.jobRetentionDays"), 7, 1, 3650,
                            QStringLiteral("Avançado"),
                            QStringLiteral("Retenção de jobs em dias.")));
        list.append(makeInt(QStringLiteral("advanced.buildLogRetentionDays"), 30, 1, 3650,
                            QStringLiteral("Avançado"),
                            QStringLiteral("Retenção de logs de build em dias.")));
        list.append(makeInt64(QStringLiteral("advanced.cloudImageCacheBytes"), 21474836480LL,
                              0, kMaxSafeInteger, QStringLiteral("Avançado"),
                              QStringLiteral("Cache de imagens cloud em bytes.")));

        // Setup
        list.append(makeBool(QStringLiteral("setup.completed"), false,
                             QStringLiteral("Setup"),
                             QStringLiteral("Onboarding/setup concluído.")));
        list.append(makeTimestamp(QStringLiteral("setup.completedAt"), QStringLiteral("Setup"),
                                  QStringLiteral("Quando o setup foi concluído.")));
        list.append(makeEnum(QStringLiteral("setup.dockerPolicy"), QStringLiteral("keep"),
                             {QStringLiteral("keep"), QStringLiteral("removed")},
                             QStringLiteral("Setup"),
                             QStringLiteral("Coexistência com Docker.")));
        list.append(makeStringList(QStringLiteral("setup.components"),
                                   QStringLiteral("Setup"),
                                   QStringLiteral("Componentes opcionais instalados.")));
        list.append(makeTimestamp(QStringLiteral("setup.lastValidation"),
                                  QStringLiteral("Setup"),
                                  QStringLiteral("Última validação do ambiente.")));

        // Chaves que ainda não têm efeito no app. Continuam no schema/CLI, mas o
        // diálogo de Configurações as esconde para não prometer o que não faz.
        static const QStringList kHiddenKeys = {
            QStringLiteral("app.waitForContainerStop"),
            QStringLiteral("app.confirmDestructive"),
            QStringLiteral("engine.socketPath"),
            QStringLiteral("engine.autoStart"),
            QStringLiteral("engine.restartOnConfigChange"),
            QStringLiteral("docker.exposePortsToLan"),
            QStringLiteral("docker.enableContainerdImageStore"),
            QStringLiteral("docker.daemonJsonPath"),
            QStringLiteral("docker.defaultNetwork"),
            QStringLiteral("network.subnet4"),
            QStringLiteral("network.ipv6"),
            QStringLiteral("network.domainsMode"),
            QStringLiteral("machines.forwardPorts"),
            QStringLiteral("machines.exposePortsToLan"),
            QStringLiteral("machines.sshPortStart"),
            QStringLiteral("machines.autoSuspend"),
            QStringLiteral("ssh.exposePort"),
            QStringLiteral("ssh.forwardAgent"),
            QStringLiteral("ssh.manageConfig"),
            QStringLiteral("ssh.configPath"),
            QStringLiteral("ssh.keyType"),
            QStringLiteral("kubernetes.enabled"),
            QStringLiteral("kubernetes.kubeconfigUseDomain"),
            QStringLiteral("terminal.fontBold"),
            QStringLiteral("logs.searchMode"),
            QStringLiteral("updates.lastCheck"),
            QStringLiteral("telemetry.enabled"),
            QStringLiteral("telemetry.crashReports"),
            QStringLiteral("telemetry.usage"),
            QStringLiteral("telemetry.performance"),
            QStringLiteral("telemetry.errors"),
            QStringLiteral("advanced.helper.enabled"),
            QStringLiteral("advanced.dns.install"),
            QStringLiteral("advanced.ca.install"),
            QStringLiteral("advanced.logLevel"),
            QStringLiteral("advanced.jobRetentionDays"),
            QStringLiteral("advanced.buildLogRetentionDays"),
            QStringLiteral("advanced.cloudImageCacheBytes"),
            QStringLiteral("setup.components"),
            QStringLiteral("setup.lastValidation"),
        };
        for (SettingDef &definition : list) {
            if (kHiddenKeys.contains(definition.key)) {
                definition.hidden = true;
            }
        }

        return list;
    }();
    return defs;
}

const SettingDef *find(const QString &key)
{
    for (const SettingDef &definition : definitions()) {
        if (definition.key == key) {
            return &definition;
        }
    }
    return nullptr;
}

QJsonObject defaults()
{
    QJsonObject root;
    for (const SettingDef &definition : definitions()) {
        const QStringList parts = definition.key.split(QLatin1Char('.'));
        insertNested(root, parts, 0, definition.defaultValue);
    }
    return root;
}

QJsonObject defaultValues()
{
    QJsonObject values;
    for (const SettingDef &definition : definitions()) {
        values.insert(definition.key, definition.defaultValue);
    }
    return values;
}

QJsonObject schemaJson()
{
    QJsonArray schema;
    for (const SettingDef &definition : definitions()) {
        QJsonObject item;
        item.insert(QStringLiteral("key"), definition.key);
        item.insert(QStringLiteral("type"), definition.type);
        item.insert(QStringLiteral("default"), definition.defaultValue);
        item.insert(QStringLiteral("category"), definition.category);
        item.insert(QStringLiteral("description"), definition.description);
        item.insert(QStringLiteral("restartRequired"), definition.restartRequired);
        item.insert(QStringLiteral("secret"), definition.secret);
        item.insert(QStringLiteral("hidden"), definition.hidden);
        if (!definition.enumValues.isEmpty()) {
            QJsonArray choices;
            for (const QString &choice : definition.enumValues) {
                choices.append(choice);
            }
            item.insert(QStringLiteral("enum"), choices);
        }
        if (definition.type == QStringLiteral("int")
            || definition.type == QStringLiteral("int64")) {
            item.insert(QStringLiteral("min"), definition.min);
            item.insert(QStringLiteral("max"), definition.max);
        }
        item.insert(QStringLiteral("nullable"), definition.defaultValue.isNull());
        schema.append(item);
    }
    return QJsonObject{{QStringLiteral("schema"), schema}};
}

bool validate(const SettingDef &definition, const QJsonValue &value, Error *error)
{
    if (error) {
        *error = {};
    }

    const auto fail = [&](const QString &reason, const QString &remediation = {}) {
        if (error) {
            *error = validationError(definition, reason, remediation);
        }
        return false;
    };

    if (definition.type == QStringLiteral("bool")) {
        if (!value.isBool()) {
            return fail(QStringLiteral("esperado true ou false."));
        }
        return true;
    }

    if (definition.type == QStringLiteral("int") || definition.type == QStringLiteral("int64")) {
        if (!value.isDouble()) {
            return fail(QStringLiteral("esperado um número inteiro."));
        }
        const double raw = value.toDouble();
        if (!std::isfinite(raw) || std::floor(raw) != raw) {
            return fail(QStringLiteral("esperado um número inteiro."));
        }
        if (raw < definition.min || raw > definition.max) {
            return fail(QStringLiteral("%1 está fora da faixa %2..%3.")
                            .arg(QString::number(raw, 'f', 0),
                                 QString::number(definition.min, 'f', 0),
                                 QString::number(definition.max, 'f', 0)),
                        QStringLiteral("Informe um valor entre %1 e %2.")
                            .arg(QString::number(definition.min, 'f', 0),
                                 QString::number(definition.max, 'f', 0)));
        }
        return true;
    }

    if (definition.type == QStringLiteral("enum")) {
        if (!value.isString()) {
            return fail(QStringLiteral("esperado um dos valores: %1.")
                            .arg(definition.enumValues.join(QStringLiteral(", "))));
        }
        if (!definition.enumValues.contains(value.toString())) {
            return fail(QStringLiteral("valor fora do enum."),
                        QStringLiteral("Use um destes valores: %1.")
                            .arg(definition.enumValues.join(QStringLiteral(", "))));
        }
        return true;
    }

    if (definition.type == QStringLiteral("stringList")) {
        if (!value.isArray()) {
            return fail(QStringLiteral("esperado uma lista de strings."));
        }
        QStringList entries;
        const QJsonArray array = value.toArray();
        for (const QJsonValue &entry : array) {
            if (!entry.isString()) {
                return fail(QStringLiteral("cada item deve ser uma string."));
            }
            const QString text = entry.toString();
            if (text.trimmed().isEmpty()) {
                return fail(QStringLiteral("itens vazios não são permitidos."));
            }
            if (entries.contains(text)) {
                return fail(QStringLiteral("itens duplicados não são permitidos."),
                            QStringLiteral("Remova o item repetido: %1.").arg(text));
            }
            entries.append(text);
        }
        if (definition.key == QStringLiteral("network.proxyExclude")) {
            Error proxyError;
            if (!Proxy::validate(QStringLiteral("none"), entries, &proxyError)) {
                return fail(proxyError.message, proxyError.remediation);
            }
        }
        return true;
    }

    if (definition.type == QStringLiteral("timestamp")) {
        if (value.isNull()) {
            return true;
        }
        if (!value.isString()) {
            return fail(QStringLiteral("esperado null ou uma data ISO 8601."));
        }
        if (!parseTimestamp(value.toString())) {
            return fail(QStringLiteral("data ISO 8601 inválida."),
                        QStringLiteral("Use null ou um instante como 2026-11-09T12:00:00Z."));
        }
        return true;
    }

    if (definition.type == QStringLiteral("string")) {
        if (value.isNull()) {
            if (definition.defaultValue.isNull()) {
                return true;
            }
            return fail(QStringLiteral("valor nulo não é permitido."),
                        QStringLiteral("Use null apenas para limpar chaves opcionais."));
        }
        if (!value.isString()) {
            return fail(QStringLiteral("esperado uma string."));
        }
        const QString text = value.toString();
        if (text.isEmpty()) {
            return fail(QStringLiteral("a string não pode ser vazia."),
                        definition.defaultValue.isNull()
                            ? QStringLiteral("Use null para limpar o valor.")
                            : QStringLiteral("Informe um valor não vazio."));
        }
        if (definition.key == QStringLiteral("network.proxy")) {
            Error proxyError;
            if (!Proxy::validate(text, {}, &proxyError)) {
                return fail(proxyError.message, proxyError.remediation);
            }
        } else if (definition.key == QStringLiteral("network.subnet4")) {
            if (!isRfc1918(text)) {
                return fail(QStringLiteral("esperado um CIDR IPv4 RFC 1918."),
                            QStringLiteral("Use uma faixa 10.0.0.0/8, 172.16.0.0/12 ou "
                                           "192.168.0.0/16."));
            }
        } else if (definition.key == QStringLiteral("updates.feedUrl")) {
            const QUrl url(text);
            const bool loopback = url.host() == QStringLiteral("127.0.0.1") ||
                                  url.host() == QStringLiteral("localhost") ||
                                  url.host() == QStringLiteral("::1");
            if (!url.isValid() || (url.scheme() != QStringLiteral("https") &&
                                   !(url.scheme() == QStringLiteral("http") && loopback))) {
                return fail(QStringLiteral("esperado uma URL https (http só em loopback)."),
                            QStringLiteral("Use https://… ou http://127.0.0.1/… em testes."));
            }
        }
        return true;
    }

    return fail(QStringLiteral("tipo de schema desconhecido (%1).").arg(definition.type));
}

} // namespace PodStack::Settings
