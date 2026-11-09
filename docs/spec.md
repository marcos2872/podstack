# Spec funcional — PodStack

> Especificação derivada do [`PRD.md`](PRD.md). Define comportamento observável, estados, fluxos e critérios de aceite. Onde o PRD diz "o quê", esta spec diz "como se comporta".
> Baseline de paridade: OrbStack 2.2.5 (ver [`reference/`](reference/)).

---

## 1. Escopo e atores

### 1.1 Atores

| Ator | Permissões |
|---|---|
| **Usuário local** | Dono do daemon e dos recursos do Podman rootless. Pode tudo no escopo do próprio UID. |
| **Usuário rootful (opt-in)** | Acesso ao socket rootful após consentimento; ações marcadas com aviso. |
| **Helper privilegiado** | Executa apenas operações declarativas (DNS, portas baixas, CA, udev). Nunca executa comandos arbitrários. |
| **Containers** | Não acessam o socket do PodStack. Domínios/CA são opt-in. |
| **CLI** | Mesmas permissões do usuário; fala com o daemon pelo socket. |
| **CI/automação** | CLI em modo não interativo, sem TTY. |

### 1.2 Modos de operação

| Modo | Descrição | Default |
|---|---|---|
| **Rootless** | Podman do usuário, socket em `$XDG_RUNTIME_DIR/podman/podman.sock` | ✅ |
| **Rootful** | Socket `/run/podman/podman.sock`; suportado no v1 como opt-in explícito | opt-in |
| **Sem engine** | Podman ausente/parado; GUI mostra estado e diagnóstico | — |

---

## 2. Comportamentos globais

### 2.1 Início e conexão

**Fluxo:**

1. `podstackd` inicia via `podstack.socket` (systemd --user) ou manualmente.
2. Descobre o socket do Podman e chama `GET /_ping`; lê `Libpod-Api-Version` e `Api-Version`.
3. Abre o socket de controle `$XDG_RUNTIME_DIR/podstack/control.sock` (0600).
4. Assina `/libpod/events?stream=true` e reconcilia o estado inicial.
5. GUI conecta e pede `system.status` + listas iniciais.

**Critérios de aceite:**

- [ ] Sem Podman: `system.status` retorna `engine: "unavailable"` com `remediation`.
- [ ] Podman parado: a GUI oferece "Iniciar Podman" (habilitar `podman.socket`) com consentimento.
- [ ] Reconexão automática com backoff exponencial (1s→30s) e indicador visual.
- [ ] Nenhuma chamada bloqueante no thread da UI.

### 2.2 Eventos e reconciliação

- O daemon mantém um cache em memória do estado (containers, imagens, volumes, redes, pods, machines).
- Eventos do Libpod atualizam o cache e emitem notificações JSON-RPC (`event`).
- A GUI aplica updates incrementais; em caso de dúvida, refaz a lista.
- Perda de eventos → reconciliação completa a cada reconexão e a cada 60 s (fallback).

**Critérios:**

- [ ] Container criado por CLI externa aparece na GUI em ≤ 1 s.
- [ ] Container removido externamente desaparece sem erro.
- [ ] Nenhuma duplicação de itens após reconexão.

### 2.3 Confirmações e operações destrutivas

- Toda remoção exige confirmação, exceto com `--force`/`--yes` explícito.
- A confirmação lista nome(s), tipo e impacto (ex.: "3 containers, 2 volumes").
- Ações em lote mostram resumo e permitem revisar a seleção.
- Operações irreversíveis (reset) exigem digitar o nome do recurso ou usar `--yes`.

### 2.4 Erros

Formato único:

```json
{
  "code": "PODSTACK_CONTAINER_NOT_FOUND",
  "message": "Container 'api' não encontrado.",
  "detail": "libpod: no such container",
  "remediation": "Atualize a lista ou verifique o nome.",
  "retryable": false
}
```

- Códigos estáveis documentados em [`api-rpc.md`](api-rpc.md).
- A GUI mostra mensagem amigável + detalhe técnico expansível + ação sugerida.
- Nunca exibir stack trace cru; log completo com correlação.

### 2.5 Cancelamento e timeouts

- Toda operação longa (pull, build, export, create) é cancelável.
- Timeout padrão de RPC: 30 s; operações longas usam job + notificações.
- Cancelamento não deixa recurso parcial sem registro; limpeza é idempotente.

### 2.6 Primeira execução e setup

**Entrada:** primeiro uso (setup incompleto), `podstack setup` ou Settings → “Executar configuração novamente”.

**Fluxo (7 etapas):**

1. **Boas-vindas** — explica o que será feito; nada é alterado.
2. **Diagnóstico** — checks read-only: Docker, Podman, QEMU/KVM, `/dev/kvm`, virtiofsd, systemd de usuário, SQLite, Caddy e k3s.
3. **Podman** — instala/atualiza o Podman e ativa `podman.socket` rootless. **O Docker não é removido por padrão**; a remoção é uma ação separada, opt-in, com confirmação e aviso de perda.
4. **QEMU/KVM** — instala QEMU, OVMF, virtiofsd e libvirt opcional; sem `/dev/kvm`, avisa e permite TCG.
5. **Componentes** — socket/rootless/SQLite obrigatórios; Caddy e k3s opcionais (k3s desmarcado).
6. **Validação** — checklist final; obrigatórios vs. opcionais; a tela principal fica bloqueada enquanto os obrigatórios não estiverem OK.
7. **Pronto** — resumo e abertura do app; persiste `setup.completed`.

**Regras:**

- A detecção é **read-only**; nenhuma alteração sem confirmação explícita.
- Instalar/remover pacotes e remover Docker passam pelo **helper allowlisted**; habilitar `podman.socket` é uma operação do systemd do usuário; a GUI nunca executa `sudo`/shell.
- Docker e Podman **coexistem** (sockets diferentes); o PodStack usa Podman e não exige a remoção do Docker.
- A remoção do Docker exige `confirm: true`, lista o impacto e não migra dados automaticamente; pode ser ignorada.
- Jobs de setup têm progresso, log, cancelamento e retry; o estado é persistido (`setup.completed`, `dockerPolicy`).
- O setup é reexecutável; avisos em itens não obrigatórios (KVM/TCG, Docker, Caddy, k3s) não bloqueiam. Os checks obrigatórios — Podman com socket, systemd de usuário, SQLite e QEMU/OVMF/virtiofsd — precisam estar `ok` para liberar a tela principal.

**Critérios de aceite:**

- [ ] Com Docker presente e Podman ausente, o setup instala o Podman e mantém o Docker.
- [ ] A matriz Docker (presente/ausente) × Podman (ausente/desatualizado/atualizado) exibe a ação correta.
- [ ] Remover o Docker só ocorre após confirmação explícita; cancelar não altera o sistema.
- [ ] Sem `/dev/kvm`, o setup avisa e permite TCG; machines ficam marcadas como lentas.
- [ ] A validação distingue obrigatórios de opcionais e não libera a tela principal enquanto os obrigatórios não passarem.
- [ ] O setup pode ser reexecutado por Settings/CLI e não repete ações já aplicadas.

---

## 3. Containers

### 3.1 Criar container

**Entrada:** imagem, nome (opcional), comando/entrypoint, portas, env, volumes, workdir, usuário, policy de restart, CPU/memória, flags (`--rm`, `--init`, `--read-only`, `--privileged`), labels, rede.

**Fluxo:**

1. Validar imagem (local ou pull), nome (RFC 1123 + conflito), portas (1–65535, sem duplicata), mounts (caminho existe, sem traversal).
2. Resolver imagem/plataforma; se ausente, oferecer pull.
3. `POST /libpod/containers/create`.
4. `POST /libpod/containers/{id}/start` (se "criar e iniciar").
5. Retornar container; GUI seleciona e abre Info.

**Critérios:**

- [ ] Conflito de nome retorna `PODSTACK_CONFLICT` com sugestão de nome.
- [ ] Porta em uso retorna erro antes de criar (pré-checagem) ou erro claro do engine.
- [ ] Mount inexistente é bloqueado com mensagem.
- [ ] Criação + start ≤ 2 s para imagem local.
- [ ] Pull mostra progresso por camada e é cancelável.

### 3.2 Ciclo de vida

| Ação | API | Pré-condição | Pós-condição |
|---|---|---|---|
| start | `POST /containers/{id}/start` | existe, não running | running |
| stop | `POST /containers/{id}/stop?t=<s>` | running/paused | stopped |
| restart | `POST /containers/{id}/restart` | existe | running |
| pause | `POST /containers/{id}/pause` | running | paused |
| unpause | `POST /containers/{id}/unpause` | paused | running |
| kill | `POST /containers/{id}/kill?signal=` | running | stopped/killed |
| remove | `DELETE /containers/{id}?force=&volumes=` | existe | removido |

**Critérios:**

- [ ] Ações otimistas: UI muda para "iniciando…" e reconcilia com o evento.
- [ ] Stop com timeout configurável (default 10 s) e fallback kill com confirmação.
- [ ] Remover container running exige `force` ou confirmação explícita.
- [ ] Erros de permissão (rootless) explicam a limitação.

### 3.3 Logs

**Fluxo:**

1. Assinar `GET /libpod/containers/{id}/logs?follow=1&stdout=1&stderr=1&tail=N&timestamps=1`.
2. Stream contínuo; o daemon reempaqueta em notificações `container.logs.delta` com `stream` (stdout/stderr), `line`, `ts`.
3. Buffer circular por sessão (ex.: 10.000 linhas) + persistência opcional em arquivo.
4. Busca/filtro no cliente; "seguir" pausa ao rolar para cima e retoma no fim.

**Critérios:**

- [ ] Primeira linha em ≤ 500 ms.
- [ ] Follow não perde linhas ao reconectar (usa `since`).
- [ ] Busca não bloqueia a UI para 100k linhas (worker thread).
- [ ] Pretty JSON detecta JSON por linha e formata sob demanda.
- [ ] Filtro por serviço Compose usa label `com.docker.compose.service`.

### 3.4 Terminal (exec)

**Fluxo:**

1. `POST /libpod/containers/{id}/exec` com `Cmd`, `AttachStdin/Stdout/Stderr`, `Tty`, `Env`, `WorkingDir`, `User`.
2. `POST /libpod/exec/{id}/start` com hijack (`Upgrade: tcp`), não WebSocket.
3. `POST /libpod/exec/{id}/resize?h=&w=` ao redimensionar.
4. Encerrar com EOF/Ctrl+D; propagar exit code.

**Critérios:**

- [ ] TTY alocado quando o shell é interativo; sem TTY para comandos.
- [ ] Ctrl+C envia SIGINT; Ctrl+D encerra; resize funciona em Wayland com escala fracionária.
- [ ] Shell padrão: `/bin/sh` se existir; fallback `/bin/bash`; erro claro se não houver.
- [ ] Sessão sobrevive à troca de aba (buffer preservado).
- [ ] Fechar a aba encerra o processo remoto (sem sessões órfãs).

### 3.5 Stats

- Stream `GET /libpod/containers/stats?stream=true&interval=2&containers=...`.
- Modelo: CPU %, memória usada/limite, rede RX/TX, bloco I/O, PIDs.
- Histórico em memória: 5 min a 2 s (150 amostras); gráficos no QML.
- Parar o stream quando a aba não está visível (economia).

**Critérios:**

- [ ] Números consistentes com `podman stats`.
- [ ] Stream para automaticamente ao fechar a aba/janela.
- [ ] Sem picos de CPU > 2% por container monitorado.

### 3.6 Files

- Listar via `GET /libpod/containers/{id}/archive?path=`.
- Download via stream tar; upload via `PUT .../archive`.
- Paths canônicos; bloquear `..` e symlink escapes.
- Editor: abrir arquivo em editor externo (cópia temporária + save-back opcional).

**Critérios:**

- [ ] Listagem de 1.000 entradas em ≤ 1 s.
- [ ] Upload de 100 MB com progresso e cancelamento.
- [ ] Permissões e ownership exibidos.

### 3.7 Inspecionar

- `GET /libpod/containers/{id}/json` + `top` + `changes`.
- Exibir JSON com busca; copiar; exportar.
- **Nota:** o OrbStack não tem aba Inspect; no PodStack é um diferencial (P1).

---

## 4. Imagens

### 4.1 Pull

1. `POST /libpod/images/pull?reference=&platform=` (stream NDJSON).
2. Progresso agregado por camada; estados: resolving, downloading, extracting, done.
3. Auth: `POST /libpod/images/pull` usa credenciais do `containers-auth.json`/keyring.
4. Cancelar fecha o stream e marca como cancelado.

**Critérios:**

- [ ] Pull de imagem de 500 MB mostra progresso e ETA aproximado.
- [ ] Auth inválida retorna `PODSTACK_AUTH_REQUIRED` com opção "Fazer login".
- [ ] Imagem já presente: pull verifica digest e informa "já atualizada".

### 4.2 Push/tag/remove/prune

- Push requer registry e credenciais; nunca loga token.
- Tag/untag validam formato `repo:tag`.
- Remove bloqueia se em uso, exceto `force`.
- Prune com prévia (dangling/all) e confirmação.

### 4.3 Import/export

- `podman save/load` via API (`/images/load`, `/images/export`).
- Validar tar; progresso; checksum opcional.

---

## 5. Volumes

### 5.1 CRUD e uso

- Listar com driver, mountpoint, labels, created, size (via `system/df` ou mount).
- Criar com driver/opts/labels.
- Remover: bloquear se em uso; `force` explícito.
- Prune com prévia.

### 5.2 Clone

- Estratégia 1: `cp --reflink=auto` no mountpoint (btrfs/XFS).
- Estratégia 2: tar pipe (fallback).
- Volume de destino novo; validar espaço livre.

### 5.3 Export/import `.tar.zst`

- Export: montar/ler o volume; tar + zstd; progresso; pausar containers que usam o volume com aviso.
- Import: criar volume; extrair com validação de paths; negar entries com `..`/absolutos.
- Checksums e retomada não no v1.

**Critérios:**

- [ ] Export de 10 GB com progresso e cancelamento.
- [ ] Import não sobrescreve volume existente sem `--force`.
- [ ] Arquivo corrompido falha antes de alterar o volume.

---

## 6. Redes

### 6.1 Criar rede

- Nome único; driver bridge (v1); subnet IPv4 opcional (CIDR válido, sem sobreposição); IPv6 opt-in; labels.
- `POST /libpod/networks/create`.

**Critérios:**

- [ ] Conflito de subnet retorna erro claro.
- [ ] Rede criada aparece em ≤ 1 s e aceita containers.
- [ ] DNS interno resolve nome/alias.

### 6.2 Exposição LAN

- **Padrão seguro:** portas publicadas em containers novos usam `hostIp=127.0.0.1`
  (acessíveis só pelo host). O checkbox **“Expor portas na LAN”** do diálogo de criação troca
  o bind para `0.0.0.0` e pede ao helper a abertura das portas no firewall.
- **Firewall runtime:** o helper (`OpenPorts`/`ClosePorts`, allowlist + polkit) usa
  `firewall-cmd --add-port=<porta>/<proto>` ou `ufw allow <porta>/<proto>` **sem**
  `--permanent`; fechar remove a mesma regra (`--remove-port` / `ufw delete allow`). Só
  `tcp`/`udp`, 1..65535, no máximo 32 portas por chamada. No firewalld as regras são
  runtime e não sobrevivem a um reload/restart; no ufw a regra é persistente até
  `ClosePorts`/`ufw delete allow`.
- **Container já criado:** `containers.exposeLan {id, expose}` inspeciona o bind. Se as portas
  estiverem em `0.0.0.0`, abre/fecha o firewall e devolve `{id, expose, ports}`. Se estiverem
  em `127.0.0.1` (ou IP específico), retorna `PODSTACK_UNSUPPORTED` com a remediação de
  **recriar o container com “Expor na LAN”** — o bind não é alterado em runtime.
- **Sem helper:** criar com `exposeLan:true` ou alternar a exposição retorna
  `PODSTACK_HELPER_REQUIRED` antes de criar/alterar o container. Se a abertura falhar depois
  da criação, o container novo é removido e o erro é `PODSTACK_IO_ERROR`.
- **Aviso de segurança:** obrigatório no diálogo de criação e no inspector, informando que só
  as portas publicadas são afetadas e que containers antigos em `127.0.0.1` exigem recriação.
- Mostrar IPs da LAN e URL de acesso (o inspector já exibe as portas publicadas).

**Critérios:**

- [ ] Acesso de outro dispositivo funciona após toggle (validação manual em outra máquina).
- [ ] Desligar remove a exposição (regra runtime removida; bind permanece `0.0.0.0`).
- [ ] Aviso de segurança exibido na primeira ativação.

### 6.3 Proxy e VPN

- Proxy global: `none`, `auto` (env do sistema), URL (`http://`, `https://`, `socks5://`, `socks5h://`).
- Exclusões por host/domínio/`*.domínio`/CIDR, separadas por vírgula na UI.
- **Containers novos:** o daemon insere as variáveis de proxy em `env` na criação
  (`containers.create`) e na duplicação (`containers.duplicate`). Variáveis definidas
  explicitamente pelo usuário ou herdadas do container original têm precedência e nunca são
  sobrescritas. A URL do proxy não é registrada em log (pode conter credenciais).
- **Machines novas:** cloud-init grava as variáveis em `/etc/environment` (`write_files` com
  `append: true`). Se o usuário forneceu `user-data` próprio, o PodStack não mescla o proxy —
  o arquivo do usuário é usado verbatim.
- **Limitações:** a configuração vale para novos recursos; containers, machines e projetos
  Compose existentes exigem recriação. Projetos Compose sobem pelo provider externo e não são
  alterados pelo PodStack nesta versão. A VPN do host é herdada pelo Podman rootless.

**Critérios:**

- [x] `none` não define variáveis; `auto` copia somente chaves já presentes; URL define
  `HTTP_PROXY`/`HTTPS_PROXY`/`ALL_PROXY` (e minúsculas).
- [x] `NO_PROXY` contém defaults + exclusões + valor existente, sem duplicatas.
- [x] Exclusão inválida ou URL sem host/esquema permitido retorna `PODSTACK_VALIDATION` e não
  é salva.
- [x] A UI indica que containers, machines e projetos Compose existentes exigem recriação
  (não há fluxo automático de recriação).

---

## 7. Machines (VMs Linux)

### 7.1 Criar máquina

**Entrada:** distro[:versão], nome, arch, cpus, memória, disco, usuário, senha, user-data, mounts, isolamento, SSH agent.

**Fluxo:**

1. Resolver imagem cloud (URL + checksum) e cache local.
2. Criar disco qcow2 (mínimo 1 GiB; default 20 GiB).
3. Gerar seed ISO (cloud-init NoCloud) com label `cidata`: `meta-data` (`instance-id`,
   `local-hostname`), `user-data` (`#cloud-config`, usuário com sudo NOPASSWD, chaves SSH e senha
   apenas como hash SHA-512 crypt) e `network-config` v2 (DHCP em `en*`). O arquivo é `0600` e os
   três nomes ficam na raiz da ISO.
4. Iniciar QEMU (KVM se disponível) com:
   - `-M q35 -cpu host -smp N -m M`
   - disco virtio, seed ISO, virtiofs (se mounts), rede user/passt com `hostfwd` de cada
     porta configurada (SSH 22/tcp sempre presente; extras em §11.5).
   - QMP socket para controle.
5. Aguardar SSH (timeout 120 s); validar cloud-init (`cloud-init status --wait`).
6. Registrar em SQLite; retornar.

**Critérios:**

- [ ] Criação ≤ 60 s para Ubuntu/Debian/Fedora em KVM.
- [ ] Sem `/dev/kvm`: modo TCG com aviso de lentidão (ou bloquear, configurável).
- [ ] Falha no cloud-init → máquina em estado `error` com log e opção "recriar".
- [ ] Disco não é alocado inteiro (qcow2 esparso).
- [ ] Nome único; conflito sugere sufixo.

### 7.2 Ciclo de vida

Estados: `provisioning → running → stopping → stopped → starting → running`; `error`; `deleting → deleted`.

- Start: QEMU + QMP; aguardar SSH.
- Stop: ACPI shutdown (`system_powerdown`) com timeout; fallback `quit`.
- Delete: parar, remover disco/seed/definição; `--force` remove mesmo em erro.
- PID: o `qemu.pid` é a fonte primária; se ele sumir ou ficar inválido, o runtime localiza o
  processo pelo `cmdline` (`/proc/<pid>/cmdline`) com o marcador `podstack:<id>`, sempre no
  mesmo uid. Sem pidfile, `isRunning`, `status`, `stop` e o `start` (para não duplicar QEMU)
  usam esse fallback.
- Reconciliação no start do daemon: para cada machine `running`/`starting`/`stopping`,
  adota o processo encontrado como `running` (preenchendo `lastBootAt` quando vazio) ou marca
  `stopped` quando não há processo. Processos com `podstack:<id>` sem machine no store e que
  pertencem ao diretório de machines da instalação são encerrados (SIGTERM, espera curta,
  SIGKILL) e logados; prefixos diferentes e processos de outro uid nunca são tocados.
- Nenhum processo QEMU órfão após crash (limpeza por PID/arquivo de lock e reconciliação por
  `cmdline` no start do daemon).

### 7.3 SSH

- Chave ed25519 por máquina em `~/.local/share/podstack/machines/<id>/id_ed25519` (0600),
  gerada por `ssh-keygen` na criação; a pública (`.pub`, 0644) vai para o seed cloud-init.
- Bloco gerenciado em `~/.ssh/config` (0600; diretório `~/.ssh` 0700):

```
# >>> podstack: <name>
Host <name>.podstack <name>
  HostName 127.0.0.1
  Port <porta>
  User <user>
  IdentityFile <path>
  IdentitiesOnly yes
# <<< podstack: <name>
```

- Comando `podstack ssh <name> [-- cmd]` (alias `machine ssh`), com
  `StrictHostKeyChecking=accept-new` e `IdentitiesOnly=yes`.
- Agent forwarding opcional (`forwardSshAgent`, desligado por padrão; grava
  `ForwardAgent yes` no bloco). O toggle fica no **Info** do inspetor e pode ser
  alterado depois da criação sem parar/reiniciar a machine; o CLI expõe
  `--forward-ssh-agent` no `create` e `--forward-agent`/`--no-forward-agent` no
  `update`. O próprio `podstack ssh`/`machine ssh` acrescenta `-A` quando o flag
  está ativo. Aviso: com forwarding, processos no guest podem usar as chaves do
  SSH agent do host — ative só em machines confiáveis.
- Blocos de terceiros e o restante do arquivo são preservados; a escrita é atômica
  (`QSaveFile`). O bloco é substituído pelo nome, sem duplicação.
- `PODSTACK_SSH_CONFIG` sobrescreve o caminho do config (dev/teste).

**Critérios:**

- [x] `ssh <name>` funciona após criação.
- [x] Remover máquina remove o bloco e a chave.
- [x] Bloco idempotente (sem duplicação).
- [x] Toggle do SSH agent pós-criação atualiza metadado/config sem reiniciar e o
  SSH do PodStack usa `-A` quando ativo.

### 7.4 Arquivos

- Virtiofs (T4-07): cada mount tem `source` (diretório absoluto do host,
  canonicalizado), `target` (caminho absoluto no guest, sem `..`, por exemplo
  `/mnt/podstack/<nome>`), `readOnly` e `tag` (`[a-z0-9-]{1,32}`, única,
  default `host<N>`). Limite de **8 mounts** por machine.
- O daemon inicia um `virtiofsd` por mount **antes** do QEMU:
  `virtiofsd --shared-dir <source> --socket-path <machineDir>/virtiofs-<tag>.sock
  --tag <tag> --cache=auto --xattr --sandbox=<chroot|namespace> [--readonly]`.
  O virtiofsd 1.14 só aceita `--sandbox=chroot` como root; no modo rootless
  (padrão do PodStack) usa-se `--sandbox=namespace`. O binário vem de
  `Options.virtiofsdBinary`, `PODSTACK_VIRTIOFSD_BINARY`, PATH ou do fallback
  `/usr/libexec/virtiofsd`; ausente → `PODSTACK_UNSUPPORTED` com remediação.
- QEMU recebe, por mount, `-chardev socket,id=vfs<N>,path=<socket>` e
  `-device vhost-user-fs-pci,chardev=vfs<N>,tag=<tag>`.
- Ciclo de vida: o virtiofsd sobe antes do QEMU; `stop`/`remove` encerram os
  processos (SIGTERM, espera e SIGKILL) e removem os sockets. Falha em qualquer
  mount para os já iniciados, limpa os sockets e marca a machine como `error`.
- pull/push via `scp` com SSH/SCP one-shot (T4-08); a operação é síncrona e não tem barra
  de progresso nesta versão.
- File manager lista o lado remoto via SSH e baixa/envia com `scp -r` (T4-08); o lado local
  é o filesystem do host (`~/Downloads` para downloads na GUI).

### 7.5 Export/import

- Export: parar a máquina (se estiver rodando), converter o disco para um qcow2
  autocontido e gravar `.tar.zst` (zstd) com `disk.qcow2`, `seed.iso`, o par de
  chaves SSH e `manifest.json`; checksum SHA-256 por artefato no manifesto.
  Destino absoluto, gravação em `.part` e rename atômico; ao terminar, reiniciar
  a máquina se ela estava rodando. Mounts do host não entram no arquivo.
- Import: extrair em diretório temporário dentro de `machinesRoot`, validar o
  formato `podstack.machine/1`, os nomes de entrada, os tamanhos e os checksums
  antes de mover para o diretório definitivo; criar uma nova máquina com ID,
  porta SSH e bloco SSH próprios e estado `stopped`. Nome vazio usa o do
  manifesto; nome existente retorna `PODSTACK_CONFLICT` sem `force`.
- Progresso (`machines.delta`/`machines.done`) e cancelamento por checkpoints;
  em falha/cancelamento, remover `.part`/diretório temporário e restaurar o
  estado anterior da máquina exportada.

### 7.6 Limites e recursos

- `--cpus` (1..256), `--memory` (≥ 1 GiB), `--disk` (≥ 1 GiB).
- `machine update`/`machines.update` alteram recursos de uma máquina existente:
  CPUs e memória valem no próximo boot; se a máquina estiver rodando, ela é parada
  e reiniciada ao final (inclusive quando a atualização falha). O disco só cresce:
  o qcow2 é redimensionado offline com `qemu-img resize` e não há shrink.
- Crescer o disco não expande a partição/filesystem do guest; `growpart`/`resize2fs`
  (ou equivalente) continuam manuais dentro da máquina.
- Mostrar uso real (QMP/cgroup do processo QEMU) no Activity Monitor.

### 7.7 Isolamento

- `--isolated`: sem mounts do host, sem SSH agent, sem rede para host/outras máquinas (opcional `--isolate-network`).
- Aviso: não é fronteira de segurança contra malware (kernel do guest é separado; QEMU é o limite).

---

## 8. Kubernetes

### 8.1 Habilitar

1. Verificar recursos (CPU/mem/disco) e KVM.
2. Criar **VM dedicada com k3s** (ADR-0003); kind não é suportado no v1.
3. Instalar k3s server; aguardar `kubectl get nodes` Ready.
4. Escrever kubeconfig em `~/.local/share/podstack/kubeconfigs/<cluster>.yaml` (0600).
5. Criar contexto `podstack-<cluster>`; não alterar `~/.kube/config` sem opt-in.
6. Expor API em `127.0.0.1:<porta>` e domínio `k8s.podstack.local` (se habilitado).

**Critérios:**

- [ ] Cluster Ready ≤ 90 s em KVM.
- [ ] `podstack kube env` imprime `export KUBECONFIG=...`.
- [ ] `kubectl get pods -A` funciona.
- [ ] Desabilitar remove cluster, kubeconfig e contexto de forma idempotente.

### 8.2 Pods/Services

- Watch via `kubectl get --watch -o json` (ou client HTTP no API server).
- Modelos: namespace, nome, status, labels, containers, IP, node, idade.
- Logs por container do pod (selector).
- Delete com confirmação; port-forward com porta local e indicador.

---

## 9. Compose

### 9.1 Detecção

- Containers com label `com.docker.compose.project` e `com.docker.compose.service`.
- Agrupar por projeto; status agregado (running/partial/stopped).
- Nome do projeto = label (não a pasta).

### 9.2 Up/down

- Preferir `docker compose` (provider) com `DOCKER_HOST=unix://$XDG_RUNTIME_DIR/podman/podman.sock`.
- Fallback `podman-compose` com aviso de limitações.
- `up -d`, `down`, `restart`, `stop`, `start`.
- Detectar `compose.yaml`/`docker-compose.yml` no diretório do projeto.

**Critérios:**

- [ ] `up` cria containers e a GUI agrupa em ≤ 2 s após o evento.
- [ ] `down` remove containers/rede do projeto; volumes preservados por padrão.
- [ ] Logs por serviço não misturam saídas.

---

## 10. Builds

- Build via `POST /libpod/build` (tar do contexto) com `dockerfile`, `t`, `buildargs`, `platform`, `cache`.
- Progresso NDJSON; steps e logs.
- Persistir metadados em SQLite: id, imagem, status, início/fim, duração, trigger, log path, plataformas.
- Histórico com filtro; apagar registros (não imagens).
- **Multi-plataforma (≥ 2):** manifest list via CLI do Podman
  (`podman build --platform p1,p2 --manifest <tag> -f <dockerfile> [--build-arg K=V] <contexto>`),
  com `podman manifest rm <tag>` antes para rebuild determinístico; progresso linha a linha em
  `builds.delta` e sem steps. O CLI é executado sem shell, com timeout total (padrão 30 min) e
  cancelamento que mata o processo.
- **Uma ou nenhuma plataforma:** o fluxo continua via Libpod, sem manifest list.
- Arquiteturas estrangeiras exigem `binfmt_misc` (`qemu-user-static`) no host; sem ele o Podman
  falha com a mensagem do próprio runtime no log do build.

---

## 11. Domínios e port forwarding

### 11.1 `*.localhost` (v1, sem privilégio)

- Qualquer nome sob `.localhost` resolve para 127.0.0.1/::1 via systemd-resolved ou glibc.
- Proxy do daemon escuta em portas altas (ex.: 8080/8443) e encaminha por `Host`.
- Mapeamento: `nome.localhost` → container `nome` na porta detectada.
- Fallback: porta publicada + `localhost:<porta>`.

**Critérios:**

- [ ] `curl http://nome.localhost:8080` retorna o serviço.
- [ ] Nome inexistente retorna 404 com página de índice.
- [ ] Funciona sem root e sem alterar `/etc/hosts`.

### 11.2 `*.podstack.local` (P1, opt-in com helper)

- Helper instala drop-in no `systemd-resolved`:

```
[Resolve]
DNS=127.0.0.1:5353
Domains=~podstack.local
```

- DNS userspace responde A/AAAA para 127.0.0.1/::1.
- Remoção idempotente; backup e validação.
- Conflito com Avahi: detectar e avisar.

### 11.3 HTTPS

- Caddy embutido com CA interna; certificados on-the-fly para `*.podstack.local`/`*.localhost`.
- Confiar na CA via helper (`update-ca-trust`) apenas com consentimento.
- CA montada nos containers via label opt-out.
- Renovação automática; chave 0600.

### 11.4 Detecção de porta

- Na primeira requisição, testar portas expostas (80, 8080, 3000, 5000, 8000, 8443...) com timeout curto.
- Cachear por container + digest da config; invalidar ao recriar.
- Override por label `dev.podstack.http-port`/`https-port`.

### 11.5 Port forwarding de máquinas

- Cada porta configurada vira um `hostfwd` do `-netdev user` do QEMU:
  `hostfwd=<tcp|udp>:<bind>:<hostPort>-:<guestPort>`. A porta SSH (22/tcp) é gerenciada
  automaticamente e sempre existe; as demais são adicionadas/removidas pelo usuário.
- Bind padrão em `127.0.0.1` (acessível só pelo host). O toggle **Expor na LAN** usa
  `0.0.0.0`, com aviso de segurança; o firewall do host ainda precisa liberar a porta.
- Protocolos `tcp` e `udp`; `hostPort` único por protocolo entre todas as machines.
- Alterar as portas exige reiniciar a machine: o `hostfwd` é parte da linha de comando do
  QEMU. O daemon para e reinicia automaticamente quando a machine está rodando e persiste
  `meta.json`+SQLite antes do restart.
- Detectar serviços no guest via `ss`/`netstat` (opcional) e sugerir.

---

## 12. Settings

- Fonte única: daemon; GUI e CLI leem/escrevem via RPC.
- Arquivo `~/.config/podstack/config.json` (0600), schema versionado.
- Validação por tipo/faixa/enum; erros com código.
- Mudanças que exigem restart marcadas; aplicar o quanto antes.
- Reset por chave e total (com backup).

**Critérios:**

- [ ] `podstack config set network.https true` reflete na GUI em ≤ 1 s.
- [ ] Valor inválido não é salvo.
- [ ] Config corrompida → backup + defaults + aviso (não crash).

---

## 13. CLI

### 13.1 Semântica

- `podstack` sem subcomando = shell na máquina default (paridade `orb`).
- `podstack <cmd>` = executa na máquina default (paridade `orb <cmd>`).
- `podstackctl` = forma explícita (paridade `orbctl`), exigindo `shell`/`run`.
- Flags globais: `-m/--machine`, `-u/--user`, `-w/--workdir`, `-c/--context` (debug), `--format`, `--quiet`, `--yes`.

### 13.2 Saída e exit codes

| Código | Significado |
|---|---|
| 0 | sucesso |
| 1 | erro genérico |
| 2 | uso inválido |
| 3 | desatualizado (`update --check`) |
| 4 | não encontrado |
| 5 | conflito |
| 6 | permissão negada |
| 7 | engine indisponível |
| 8 | timeout |
| 9 | cancelado |

- `--format json` emite um objeto/array estável com `api: 1`.
- `--quiet` emite apenas IDs/nomes.
- Sem TTY: nunca prompt; usar defaults ou falhar com código 2.

### 13.3 Comandos

Ver [`cli-parity.md`](cli-parity.md) para a tabela completa `orb` → `podstack`.

---

## 14. Estados e transições

### 14.1 Container

```
created ──start──► running ──pause──► paused
   ▲                  │  ▲               │
   │                  │  └──unpause──────┘
   │                  ├──stop──► stopped ──start──► running
   │                  └──kill──► stopped
   └──────────── remove ◄───────────────┘
                 (qualquer estado)
```

### 14.2 Machine

```
provisioning ──ok──► running ──stop──► stopping ──► stopped
      │                  ▲                              │
      │                  └──────── start ◄──────────────┘
      └──fail──► error ──retry──► provisioning
running/stopped ──delete──► deleting ──► deleted
```

### 14.3 Cluster

```
disabled ──enable──► provisioning ──ok──► running
   ▲                    │                  │
   │                    └──fail──► error   │
   └──── disable ◄── stopping ◄────────────┘
```

### 14.4 Compose project

```
down ──up──► starting ──ok──► running ──stop──► stopped
                 │                 │
                 └──partial──► degraded
```

---

## 15. Casos de borda

| Caso | Comportamento |
|---|---|
| Podman sem `podman.socket` | GUI oferece habilitar; CLI retorna 7 com instrução. |
| Rootless sem permissão de porta <1024 | Explicar; sugerir porta alta ou helper. |
| Container sem shell | Terminal mostra erro; oferecer debug shell (se disponível). |
| Imagem sem `latest` | Aviso; pedir tag explícita. |
| Disco cheio | Bloquear create/export; mostrar uso; sugerir prune. |
| Nome duplicado | Sugerir `nome-2`; não criar silenciosamente. |
| Rede com subnet sobreposta | Erro antes de criar. |
| DNS `.podstack.local` já em uso | Detectar Avahi/resolved; não sobrescrever. |
| `/dev/kvm` ausente | Modo TCG com aviso; criação mais lenta. |
| QEMU crashado | Estado `error`; oferecer reiniciar/recriar; limpar lock. |
| Export interrompido | Arquivo parcial removido; máquina volta a running. |
| Config inválida | Backup + defaults; aviso na GUI. |
| Update com assinatura inválida | Rejeitar; log; notificar. |

---

## 16. Fora do escopo desta spec

- Implementação de runtime OCI, CNI ou hypervisor próprio.
- Compatibilidade com plugins/extensões do OrbStack.
- macOS/Windows.
- Orquestração multi-host.
- Detalhes visuais pixel-a-pixel (ver `ui-map.md` para o design próprio).
