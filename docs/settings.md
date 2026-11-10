# Settings — PodStack

> Fonte única de configuração no daemon; GUI e CLI leem/escrevem as mesmas chaves via RPC
> (`settings.*`). Arquivo: `~/.config/podstack/config.json` (0600), escrita atômica, schema
> versionado. O schema central com as 82 chaves, defaults, enums e faixas vive em
> `src/core/Settings.cpp`; `Config::defaults()` é gerado a partir dele.

---

## 1. Precedência

1. Flag de CLI (`--engine-socket`, etc.) — apenas para a invocação.
2. Variável de ambiente (`PODSTACK_*`) — para diagnóstico/CI.
3. `config.json` do usuário.
4. Defaults compilados.

Campos desconhecidos são preservados na escrita. Config inválida → defaults em memória + aviso no
log (o arquivo original não é apagado; um save posterior o substitui).

---

## 2. Chaves

### 2.1 App

| Chave | Tipo | Default | Reinício | Descrição |
|---|---|---|---|---|
| `app.startAtLogin` | bool | `false` | não | Cria/remove `~/.config/autostart/dev.podstack.PodStack.desktop` com `--hidden` |
| `app.waitForContainerStop` | bool | `true` | não | Ao sair, aguarda containers pararem |
| `app.keepRunningInTray` | bool | `true` | não | Fechar a janela esconde para a bandeja em vez de encerrar a GUI |
| `app.showInTray` | bool | `true` | não | Mostra o ícone na bandeja; `false` faz fechar encerrar a GUI |
| `app.notifications` | bool | `true` | não | Notifica falha/OOM/unhealthy de containers |
| `app.theme` | enum `system\|dark\|light` | `system` | não | Tema da UI |
| `app.language` | string | `pt-BR` | não | Idioma (`pt-BR`, `en-US`) |
| `app.confirmDestructive` | bool | `true` | não | Confirmação para ações destrutivas |
| `app.showOnboarding` | bool | `true` | não | Exibe onboarding se incompleto |

### 2.2 Engine

| Chave | Tipo | Default | Reinício | Descrição |
|---|---|---|---|---|
| `engine.mode` | enum `rootless\|rootful` | `rootless` | sim | Modo de conexão com Podman |
| `engine.socketPath` | string? | `null` | sim | Override do socket (null = autodetectar) |
| `engine.autoStart` | bool | `true` | não | Habilita `podman.socket` se necessário (com consentimento) |
| `engine.restartOnConfigChange` | bool | `true` | não | Reinicia engine ao mudar config do Docker |

### 2.3 Docker/Engine config

| Chave | Tipo | Default | Reinício | Descrição |
|---|---|---|---|---|
| `docker.exposePortsToLan` | bool | `true` | não | Publica portas em `0.0.0.0` |
| `docker.enableContainerdImageStore` | bool | `false` | sim | Usa image store do containerd (se suportado) |
| `docker.daemonJsonPath` | string | `~/.config/containers/containers.conf` | sim | Arquivo de config do engine |
| `docker.defaultNetwork` | string | `podman` | não | Rede default para novos containers |

### 2.4 Rede

| Chave | Tipo | Default | Reinício | Descrição |
|---|---|---|---|---|
| `network.https` | bool | `false` | sim | HTTPS automático para domínios |
| `network.proxy` | enum/URL | `auto` | não | `none`, `auto`, `http://`, `https://`, `socks5://`, `socks5h://` |
| `network.proxyExclude` | string[] | `[]` | não | Hosts/CIDRs fora do proxy |
| `network.subnet4` | CIDR | `192.168.127.0/24` | sim | Subnet da bridge PodStack |
| `network.bridge` | bool | `true` | sim | Permite acesso a domínios/IPs de containers |
| `network.ipv6` | bool | `false` | sim | Habilita IPv6 em containers |
| `network.exposeLan` | bool | `false` | não | Expõe portas/domínios na LAN |
| `network.domainsMode` | enum `localhost\|podstack-local` | `localhost` | sim | Mecanismo de domínio |
| `network.httpsPort` | int | `8443` | sim | Porta HTTPS do proxy |
| `network.httpPort` | int | `8080` | sim | Porta HTTP do proxy |

**Proxy na prática:** `auto` (default) herda do ambiente do daemon as variáveis de proxy já
definidas (`HTTP_PROXY`, `HTTPS_PROXY`, `ALL_PROXY`, `NO_PROXY` e minúsculas), sem inventar
valores. Com uma URL (`http`, `https`, `socks5` ou `socks5h`), o daemon define
`HTTP_PROXY`/`HTTPS_PROXY`/`ALL_PROXY` (e minúsculas) e monta `NO_PROXY` com os defaults
(`localhost,127.0.0.1,::1,*.localhost`), as exclusões de `network.proxyExclude` e o `NO_PROXY`
já existente, sem duplicatas. `proxyExclude` aceita host, domínio, `*.domínio` ou CIDR e só é
aplicado no modo URL.

A configuração é lida na **criação**: containers e machines novos recebem as variáveis;
containers, machines e stacks Compose existentes precisam ser recriados. Machines recebem um
`write_files` em `/etc/environment` via cloud-init; se o usuário informar `user-data` próprio,
o conteúdo não é mesclado.

### 2.5 Máquinas

| Chave | Tipo | Default | Reinício | Descrição |
|---|---|---|---|---|
| `machines.default` | string? | `null` | não | Máquina default do CLI (T4-10: get/set via `machine default`; limpa ao remover a machine) |
| `machines.forwardPorts` | bool | `true` | não | Encaminha portas da máquina ao host |
| `machines.exposePortsToLan` | bool | `false` | não | Expõe portas da máquina na LAN |
| `machines.cpus` | int | `2` | não | Default de CPUs (1..N) |
| `machines.memoryBytes` | int64 | `4294967296` | não | Default de memória (≥ 1 GiB) |
| `machines.diskBytes` | int64 | `21474836480` | não | Default de disco (≥ 1 GiB) |
| `machines.sshPortStart` | int | `32222` | sim | Primeira porta SSH |
| `machines.forwardSshAgent` | bool | `false` | não | Default de agent forwarding |
| `machines.isolated` | bool | `false` | não | Default de isolamento |
| `machines.autoSuspend` | bool | `false` | não | Suspende VMs ociosas (P2) |

### 2.6 Kubernetes

| Chave | Tipo | Default | Reinício | Descrição |
|---|---|---|---|---|
| `kubernetes.enabled` | bool | `false` | sim | Cluster local habilitado |
| `kubernetes.exposeServices` | bool | `false` | sim | Port-forward de Service em `0.0.0.0` (LAN); `false` usa loopback |
| `kubernetes.kubeconfigUseDomain` | bool | `true` | sim | Usa domínio no kubeconfig |
| `kubernetes.tlsSan` | string[] | `[]` | sim | SANs extras do API server |
| `kubernetes.cpus` | int | `2` | sim | CPUs da VM do cluster |
| `kubernetes.memoryBytes` | int64 | `4294967296` | sim | Memória da VM |
| `kubernetes.diskBytes` | int64 | `21474836480` | sim | Disco da VM |
| `kubernetes.kubeconfigPath` | string | `~/.local/share/podstack/kubeconfigs/` | não | Diretório de kubeconfigs (usado pelo `k8s.enable`/T5-02; `~` é expandido) |
| `kubernetes.contextPrefix` | string | `podstack-` | não | Prefixo de contexto (aplicado no kubeconfig; T5-02) |

### 2.7 SSH

| Chave | Tipo | Default | Reinício | Descrição |
|---|---|---|---|---|
| `ssh.exposePort` | bool | `false` | sim | Expõe SSH na LAN |
| `ssh.forwardAgent` | bool | `false` | não | Encaminha `SSH_AUTH_SOCK` |
| `ssh.manageConfig` | bool | `true` | não | Escreve blocos em `~/.ssh/config` |
| `ssh.configPath` | string | `~/.ssh/config` | não | Caminho do config |
| `ssh.keyType` | enum | `ed25519` | não | Tipo de chave por máquina |

### 2.8 Terminal e logs

| Chave | Tipo | Default | Reinício | Descrição |
|---|---|---|---|---|
| `terminal.fontFamily` | string | `monospace` | não | Fonte do terminal |
| `terminal.fontSize` | int | `12` | não | Tamanho (8..32) |
| `terminal.fontBold` | bool | `true` | não | Negrito |
| `terminal.scrollback` | int | `10000` | não | Linhas de histórico (1k..100k) |
| `logs.fontSize` | int | `12` | não | Fonte dos logs |
| `logs.wrapLines` | bool | `false` | não | Quebra de linha |
| `logs.searchMode` | enum `filter\|highlight` | `filter` | não | Comportamento da busca |
| `logs.prettyJson` | bool | `true` | não | Destaca/format JSON |
| `logs.bufferLines` | int | `10000` | não | Buffer por sessão |

### 2.9 Updates e diagnóstico

| Chave | Tipo | Default | Reinício | Descrição |
|---|---|---|---|---|
| `updates.mode` | enum `off\|check\|auto` | `check` | não | Política de update |
| `updates.channel` | enum `stable\|beta` | `stable` | não | Canal |
| `updates.lastCheck` | timestamp? | `null` | não | Última verificação |
| `diagnostics.includeLogs` | bool | `true` | não | Inclui logs no report |
| `diagnostics.redact` | bool | `true` | não | Redige segredos |
| `telemetry.enabled` | bool | `false` | não | Master switch da telemetria (opt-in) |
| `telemetry.crashReports` | bool | `false` | não | Enviar relatórios de crash (anônimos) |
| `telemetry.usage` | bool | `false` | não | Enviar uso agregado de features |
| `telemetry.performance` | bool | `false` | não | Enviar métricas de performance |
| `telemetry.errors` | bool | `false` | não | Enviar erros não fatais (sem dados sensíveis) |

### 2.10 Avançado

| Chave | Tipo | Default | Reinício | Descrição |
|---|---|---|---|---|
| `advanced.helper.enabled` | bool | `false` | sim | Permite instalar helper privilegiado |
| `advanced.dns.install` | bool | `false` | sim | Instala drop-in do resolved (`*.podstack.local`) |
| `advanced.ca.install` | bool | `false` | sim | Confia na CA local |
| `advanced.logLevel` | enum `error\|warn\|info\|debug\|trace` | `info` | sim | Nível de log |
| `advanced.jobRetentionDays` | int | `7` | não | Retenção de jobs |
| `advanced.buildLogRetentionDays` | int | `30` | não | Retenção de logs de build |
| `advanced.cloudImageCacheBytes` | int64 | `21474836480` | não | Cache de imagens cloud |

### 2.11 Setup

| Chave | Tipo | Default | Reinício | Descrição |
|---|---|---|---|---|
| `setup.completed` | bool | `false` | não | Onboarding/setup concluído |
| `setup.completedAt` | timestamp? | `null` | não | Quando o setup foi concluído |
| `setup.dockerPolicy` | enum `keep\|removed` | `keep` | não | `keep` = coexistência com Docker; `removed` só após ação opt-in confirmada |
| `setup.components` | string[] | `[]` | não | Componentes opcionais instalados (`caddy`, `k3s`) |
| `setup.lastValidation` | timestamp? | `null` | não | Última validação do ambiente |

> O Docker **não** é removido por padrão. `setup.dockerPolicy=removed` só é gravado depois de confirmação explícita e da ação allowlisted do helper; não há migração automática de dados do Docker.

> O onboarding abre quando `setup.completed=false` **e** `app.showOnboarding=true` (default). Pular o assistente grava `app.showOnboarding=false`, mantém `setup.completed=false` e deixa a Main com o aviso de configuração pendente; Settings → “Executar configuração novamente” reabre o fluxo.

> **Bandeja (2026-11-08):** `app.keepRunningInTray`, `app.showInTray`, `app.startAtLogin` e
> `app.notifications` são editáveis em Configurações → Geral. Se o host SNI não estiver disponível,
> `app.keepRunningInTray` é ignorado e fechar a janela encerra a GUI (nunca fica sem janela).

---

## 3. Mapeamento GUI ↔ CLI ↔ OrbStack

| GUI | CLI (`podstack config`) | OrbStack |
|---|---|---|
| Iniciar no login | `app.startAtLogin` | `app.start_at_login` |
| Esperar containers ao sair | `app.waitForContainerStop` | `app.wait_for_container_stop` |
| Expor portas Docker na LAN | `docker.exposePortsToLan` | `docker.expose_ports_to_lan` |
| Expor portas de máquinas na LAN | `machines.exposePortsToLan` | `machines.expose_ports_to_lan` |
| Encaminhar portas de máquinas | `machines.forwardPorts` | `machines.forward_ports` |
| HTTPS para domínios | `network.https` | `network.https` |
| Proxy | `network.proxy` | `network.proxy` |
| Exclusões do proxy | `network.proxyExclude` | `network.proxy.exclude` |
| Subnet IPv4 | `network.subnet4` | `network.subnet4` |
| Kubernetes habilitado | `kubernetes.enabled` | `k8s.enable` |
| Expor Services | `kubernetes.exposeServices` | `k8s.expose_services` |
| kubeconfig por domínio | `kubernetes.kubeconfigUseDomain` | `k8s.kubeconfig_use_domain` |
| SANs do API server | `kubernetes.tlsSan` | `k8s.tls_san` |
| SSH na LAN | `ssh.exposePort` | `ssh.expose_port` |
| Setup concluído | `setup.completed` | — |
| Política do Docker | `setup.dockerPolicy` | — |

> O PodStack usa **camelCase** no JSON e **dot.camelCase** no CLI para consistência com a UI; o mapeamento acima preserva a semântica do OrbStack.

---

## 4. Validação

| Chave | Regra | Erro |
|---|---|---|
| `engine.mode` | `rootless`/`rootful`; `rootful` só conecta se houver acesso ao socket no próximo start | `PODSTACK_VALIDATION` |
| `network.subnet4` | CIDR IPv4 RFC 1918 (`10/8`, `172.16/12`, `192.168/16`) | `PODSTACK_VALIDATION` |
| `network.proxy` | URL válida (`http/https/socks5/socks5h`) ou `auto`/`none` | `PODSTACK_VALIDATION` |
| `network.proxyExclude` | host, domínio, `*.domínio` ou CIDR, sem duplicatas/espaços | `PODSTACK_VALIDATION` |
| `machines.*.Bytes` | ≥ 1 GiB e ≤ 1 TiB | `PODSTACK_VALIDATION` |
| `machines.cpus` | 1..1024 | `PODSTACK_VALIDATION` |
| portas (`network.*Port`, `machines.sshPortStart`) | 1..65535 | `PODSTACK_VALIDATION` |
| `terminal.fontSize`, `logs.fontSize` | 8..32 | `PODSTACK_VALIDATION` |
| `terminal.scrollback` | 1000..100000 | `PODSTACK_VALIDATION` |
| `updates.mode`, `updates.channel` | enum | `PODSTACK_VALIDATION` |
| `advanced.logLevel` | enum | `PODSTACK_VALIDATION` |
| `telemetry.*` | bool; categorias só têm efeito com `enabled=true` | `PODSTACK_VALIDATION` |
| `updates.lastCheck`, `setup.completedAt`, `setup.lastValidation` | `null` ou ISO 8601 | `PODSTACK_VALIDATION` |

- O schema é a autoridade: `podstack config schema --json` (ou `settings.schema`) devolve
  `type`, `default`, `enum`, `min`, `max`, `restartRequired`, `secret` e `nullable`.
- Erros retornam `{code, message, detail, remediation}` no RPC (`detail` = chave; o valor não é
  ecoado para não vazar segredos). A CLI mostra mensagem, detalhe e ação.
- Valor inválido **não é salvo**.
- Alterações que exigem restart retornam `restartRequired: true` e são aplicadas no próximo start
  (ou com botão "Aplicar e reiniciar", ainda não implementado).

---

## 5. Migração e reset

- `schemaVersion` no topo; a versão atual é 1 e ainda não há migração automática. Quando houver,
  o backup `config.json.bak-<timestamp>` será criado antes de migrar.
- Reset por chave: `podstack config reset <key> --yes` (sem `--yes`, pede confirmação em TTY).
- Reset total: `podstack config reset --all --yes` — restaura as chaves de configuração e
  **preserva** `schemaVersion` e todo o bloco `setup.*` (`completed`, `completedAt`,
  `dockerPolicy`, `components`, `lastValidation`). Machines e dados não são tocados.
- Reset de dados: `podstack reset` (Docker + machines) é separado e exige confirmação.

---

## 6. Exemplos

```bash
podstack config show
podstack config show --json
podstack config get network.proxy
podstack config get app.theme --json
podstack config set network.proxy socks5://127.0.0.1:1080
podstack config set app.theme dark
podstack config add network.proxyExclude .internal
podstack config remove network.proxyExclude .internal
podstack config schema --json
podstack config reset network.https --yes
podstack config set machines.memoryBytes 8589934592
podstack config set kubernetes.enabled true   # requer restart do cluster
```

> `network.proxy` pode conter credenciais (userinfo). O valor real trafega no socket local
> (`0600`/`SO_PEERCRED`) para permitir edição; a GUI usa `echoMode: Password` e o valor nunca é
> logado. O arquivo continua `0600`.

---

## 7. Defaults

Os defaults são gerados a partir do schema central (não há mais uma cópia manual neste
documento). Para inspecionar o arquivo efetivo:

```bash
podstack config show --json
podstack config schema --json
```

O objeto persistido mantém `"schemaVersion": 1` no topo e os grupos da tabela da §2; chaves
desconhecidas presentes no arquivo são preservadas na escrita. O reset total descarta chaves
customizadas fora de `setup.*` (o bloco de setup é preservado integralmente).
