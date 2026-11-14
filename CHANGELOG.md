# Changelog

Todas as mudanças relevantes deste projeto são documentadas neste arquivo.

O formato segue [Keep a Changelog](https://keepachangelog.com/pt-BR/1.1.0/) e o
versionamento segue [SemVer](https://semver.org/lang/pt-BR/).

## [Não lançado]

### Alterado

- **Portas padrão do proxy:** `network.httpPort`/`network.httpsPort` passam de `8080`/`8443` para
  `18080`/`18443`, fora da faixa efêmera do kernel, evitando colisões com serviços comuns de
  desenvolvimento. Configs do schema 1 com os valores antigos são migradas automaticamente para os
  novos defaults (`schemaVersion` 2); valores customizados são preservados.

## [0.1.3] - 2026-11-12

Release de correção do setup no Ubuntu.

### Corrigido

- **OVMF no Ubuntu:** o check do setup e a criação de machines reconhecem
  `/usr/share/OVMF/OVMF_CODE_4M.fd` e `OVMF_VARS_4M.fd`, além dos caminhos do
  Fedora; instalar `ovmf` manualmente deixa de ser ignorado.
- **Instalação de QEMU/OVMF:** o setup escolhe os nomes de pacote pelo
  gerenciador da distro — `ovmf`, `qemu-system-x86`, `qemu-system-arm`,
  `qemu-utils` e `qemu-efi-aarch64` no apt; `edk2-ovmf` etc. no dnf — e a
  allowlist do helper cobre os dois conjuntos.
- **Feedback de erro:** quando o helper falha ao instalar um pacote, a mensagem
  retornada (por exemplo, `Unable to locate package`) aparece no painel de log e
  no rodapé do setup, em vez de o job parecer ter terminado sem efeito.
- **`MachineManager`:** a auto-detecção de OVMF usa a primeira combinação
  code/vars existente, em vez de assumir apenas o caminho do Fedora.

### Alterado

- Site (`web/`) e documentação atualizados para a 0.1.3 e para os caminhos e
  pacotes do Ubuntu.

## [0.1.2] - 2026-11-12

Release de compatibilidade do pacote `.deb` com Ubuntu 24.04 LTS e versões
posteriores.

### Corrigido

- **Qt 6.4 (Ubuntu 24.04):** removido o requisito de Qt 6.10; o código usa
  fallbacks para APIs introduzidas no Qt 6.5 (`Qt.styleHints.colorScheme`,
  `QQuickWidget::loadFromModule`, `QTimeZone::UTC`) e o QML deixou de usar
  `MultiEffect` e cantos individuais de `Rectangle`.
- **QTermWidget:** o Ubuntu 24.04/25.10 não empacota `qtermwidget6`; a versão
  2.0.0 foi vendorizada em `third_party/qtermwidget`, compilada como
  biblioteca estática e vinculada ao `podstack-gui` (sem dependência de
  `libqtermwidget6.so.2` em runtime).
- **Pacote `.deb`:** dependências corrigidas para os nomes `t64` do Ubuntu
  24.04, inclusão dos módulos QML e do plugin de plataforma Qt, `strip` dos
  binários (30 MB → 2,5 MB) e `libcurl3t64-gnutls` declarada para CLI/helper.
- **Pipeline:** o DEB passou a ser construído em Ubuntu 24.04 (Qt 6.4) e o RPM
  continua em Fedora 44; a publicação baixa os dois artefatos antes de gerar
  `SHA256SUMS`, SBOM e manifesto.
- **Podman:** o `.deb` aceita Podman ≥ 4.9 (o do Ubuntu 24.04); Podman ≥ 5.8
  segue recomendado e é o requisito do RPM/Fedora.

## [0.1.1] - 2026-11-12

Release de correção da instalação e do daemon.

### Corrigido

- RPM: o `podstack-helper` deixa de ser um subpacote e passa a fazer parte do
  `podstack`; o pacote antigo é obsoleto no upgrade e o serviço é habilitado e
  iniciado no `%post`. O `.deb` também habilita/desabilita o helper no
  `postinst`/`prerm` e declara `polkitd`.
- systemd: o `podstackd` usa o fd entregue pelo socket unit (`LISTEN_FDS`) em
  vez de remover e recriar o socket; a unit cria os diretórios com
  `StateDirectory`/`CacheDirectory`/`ConfigurationDirectory`, evitando
  `226/NAMESPACE` no primeiro start.
- Sandbox do daemon: removidos `NoNewPrivileges` e `SystemCallFilter`, que
  impediam a transição SELinux para `container_runtime_t` e bloqueavam
  `fsopen` no Podman filho (gerava avisos do setroubleshoot). As famílias de
  socket incluem `AF_NETLINK`/`AF_PACKET`.
- Cliente: ao falhar a primeira conexão, tenta `systemctl --user
  daemon-reload` + `start podstack.socket` antes de mostrar o erro.

## [0.1.0] - 2026-11-11

Primeira release do PodStack, alternativa Linux ao OrbStack sobre o Podman Engine
nativo, com GUI Qt 6/QML, CLI `podstack` e daemon de usuário `podstackd`.

### Adicionado

**Containers**

- Ciclo de vida completo (create, start, stop, restart, pause, kill, rename,
  duplicate, remove), ações em lote, confirmações e filtros.
- Logs com follow, busca, timestamps e reconexão; terminal integrado com
  QTermWidget e sessão externa opcional; estatísticas de CPU, memória, rede e
  disco; navegação e transferência de arquivos; inspect; healthcheck; rootful
  opt-in; checkpoint/restore via CRIU quando o runtime suporta.

**Imagens, volumes, redes e pods**

- Pull/push/tag/untag, remove/prune, histórico e digest de imagens, inclusive
  pull por plataforma.
- CRUD de volumes e redes, clone, export/import `.tar.zst` e import do host.
- Pods do Podman no grupo Engine e resumo de uso em `system.df`.

**Compose, builds e eventos**

- Agrupamento de projetos Compose, up/down/restart, logs por serviço e abertura
  do YAML/diretório.
- Builds via Libpod com steps, cache, histórico em SQLite e manifest list
  multi-plataforma.
- Eventos globais do Podman refletidos na UI e em notificações.

**Machines (QEMU/KVM)**

- Criação com cloud-init para Ubuntu, Debian, Fedora, Arch, Alpine e outras,
  cache de imagens com checksum e ciclo de vida com reconciliação de órfãos.
- SSH com chave por machine, bloco gerenciado em `~/.ssh/config`, arquivos
  (list/download/upload), virtiofs, CPU/memória/disco com restart, port
  forwarding, agent forwarding e modo isolado.
- Export/import `.tar.zst`, clone, rename e machine default; snapshots;
  console serial; USB passthrough; portas seriais; áudio PipeWire; X11
  forwarding.

**Kubernetes (k3s)**

- Cluster k3s em VM dedicada com instalação, enable/disable/reset idempotentes.
- Kubeconfig isolado e contexto explícito; Pods/Services com refresh, logs por
  container, delete e port-forward de Pod/Service; TLS SANs e exposição LAN.

**Domínios e HTTPS**

- Proxy `*.localhost` em loopback com detecção automática de porta e página de
  índice; labels `dev.podstack.domains`, `http-port`, `https-port` e opt-out da
  CA.
- DNS userspace em `127.0.0.1:5353` para `*.podstack.local`, socket activation
  de :80/:443 e Caddy com CA interna e HTTPS on-demand.
- Confiança/remoção da CA local via helper com fingerprint SHA-256 e exposição
  LAN opt-in.

**Desktop**

- Bandeja SNI com menu de containers por projeto, submenus de Machines e
  Kubernetes, Activity Monitor e fechar-para-bandeja.
- Settings com 86 chaves, validação central, tema claro/escuro reativo, escala
  de fonte e reinício sinalizado quando necessário.
- Notificações de falha, OOM, unhealthy, update e disco baixo com cooldown;
  restauração de geometria, seção e abas; atalhos de teclado e navegação.
- Updates assinados (Ed25519/minisign), canais `stable`/`beta`,
  `podstack update --check` (exit 3 quando há versão nova) e report redigido.

**Setup e onboarding**

- Janela de setup em 7 etapas, reexecutável, com diagnóstico read-only,
  instalação/atualização de Podman, coexistência com Docker e remoção opt-in
  confirmada.
- `podstack-helper` com polkit e API allowlisted, sem shell nem `Exec`.

**CLI e empacotamento**

- CLI para containers (listagem, inspect, exec, pause/kill e checkpoint/restore),
  catálogo, Compose, builds, machines, kube, proxy, settings, report, update e setup;
  saída humana/JSON e exit codes estáveis cobertos por golden tests.
- Pacotes RPM (primário) e DEB, Flatpak como cliente do daemon/helper no host,
  AppStream/metainfo, ícones e units systemd.
- Módulo SELinux mínimo para o helper e o diretório de runtime.

**Internacionalização**

- UI principal traduzida para en-US (158 strings), com pt-BR como padrão e
  troca por `app.language` com reinício.

### Alterado

- Versão 0.1.0 unificada em `CMakeLists.txt`, spec RPM e metainfo AppStream.
- `scripts/build-deb.sh` passou a gerar o controle binário próprio, sem
  `${shlibs:Depends}`, com alvo Debian 12/Ubuntu 24.04 validado por
  `dpkg-deb`.
- O build RPM compila o módulo SELinux e, quando o secret existe, embute
  `PODSTACK_UPDATE_PUBLIC_KEY` via define.
- CI ganhou `appstreamcli validate`; release ganhou pipelines de RPM/DEB,
  checksums, SBOM e manifesto de updates.

### Corrigido

- Settings: carregamento das chaves com a categoria traduzida, select que
  perdia o valor ao reabrir, aplicação de terminal/logs/machines e chaves sem
  efeito ocultadas.
- Machines: restart de áudio/serial/USB sem esperar ACPI e shutdown seguro.
- Machines: criação prepara áudio e X11 nas distros Debian, Fedora e Ubuntu
  (kernel genérico no Debian, `kernel-modules`/módulos de som no Fedora,
  `xauth`/`x11-apps`/`xdg-utils`, grupo `audio`, linger e PipeWire/PulseAudio).
- Machines/Ubuntu: `~/.ssh/environment` com `XAUTHORITY` +
  `PermitUserEnvironment yes`, necessário para o Firefox snap enxergar o
  cookie X11 fora do `HOME` redirecionado.
- SSH: `known_hosts` isolado por machine, evitando falha de host key quando a
  porta SSH é reutilizada por outra VM.
- Terminal: copiar/colar e menu de contexto.
- Setup: texto esclarece que o k3s roda em VM, não no host.
- Correções acumuladas de inicialização do daemon, inspector e virtiofsd.

### Segurança

- Socket de controle com diretório `0700`, socket `0600` e validação por
  `SO_PEERCRED`.
- Helper privilegiado apenas com métodos allowlisted e autorização polkit;
  nenhum shell ou comando arbitrário.
- Redaction de tokens, senhas, chaves e `Authorization` em logs e reports;
  proteção contra path traversal em arquivos e archives.
- Instalação/remoção idempotente de CA e DNS; assinatura de update inválida é
  rejeitada.
- Release com `SHA256SUMS`, assinatura GPG opcional de RPM/checksums, SBOM
  CycloneDX e assinatura minisign opcional do manifesto de updates.

### Limitações conhecidas

- O stack Supabase (T3-09) não foi entregue.
- Mounts de host em machines (T4-07) existem na GUI e no RPC, mas
  `machine create` não tem flags de mount no CLI.
- O roteamento de domínios customizados via Compose (T6-08) não está
  disponível; labels e montagem da CA em containers de Compose ficam
  pendentes.
- T5-07 e T5-08 são parciais: não há NodePort/LoadBalancer nativos nem
  `k8s.podstack.local`; a troca de contexto é explícita, não automática por
  projeto.
- Portas não publicadas não podem ser encaminhadas no modo rootless (T6-10).
- O console serial das machines é somente texto; não há VNC nem console
  gráfico.
- `machine export`/`import` e `machine clone` não preservam snapshots internos
  do qcow2.
- No Flatpak, o aplicativo é apenas cliente: `podstackd` e `podstack-helper`
  precisam estar instalados no host.
- A ativação das portas 80/443 via helper existe na API privilegiada, mas não
  é acionada pela GUI/CLI em 0.1.0; o proxy usa `network.httpPort` e
  `network.httpsPort` (8080/8443 por padrão).
- A confiança na CA local e a instalação do drop-in de `*.podstack.local` ainda
  não têm um fluxo pronto na GUI/CLI; use `*.localhost` ou os passos manuais de
  `docs/user/troubleshooting.md`.
- A instalação limpa tem smoke automatizado em container Fedora 44; a validação
  da GUI, do KVM e do polkit reais em máquina limpa ainda é manual.
- A assinatura GPG dos RPMs e a assinatura minisign do manifesto dependem dos
  secrets do repositório; sem eles, a release é publicada sem assinatura e o
  updater retorna `unconfigured`.
- O módulo SELinux é mínimo e não foi validado em modo enforcing.
- A auditoria externa de segurança e a revisão de CVEs do SBOM ainda não foram
  concluídas.
