# Spec funcional — PodStack

> Especificação derivada do [`PRD.md`](PRD.md). Define comportamento observável, estados, fluxos e critérios de aceite. Onde o PRD diz "o quê", esta spec diz "como se comporta".
> Baseline de paridade: OrbStack 2.2.5 (ver [`reference/`](../reference)).

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

- Códigos estáveis documentados em [`api-rpc.md`](../engineering/api-rpc.md).
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
5. **Componentes** — socket/rootless/SQLite obrigatórios; Caddy opcional. O k3s **não** é instalado no host pelo setup: o cluster roda em VM dedicada e é habilitado depois na página Kubernetes.
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

**Idioma:** `app.language` (`pt-BR` fonte, `en-US` catálogo) é aplicado no boot da GUI via
`QTranslator` (`translations/podstack_en_US.qm`); trocar o idioma exige reiniciar o app.

**Fluxo:**

1. `POST /libpod/containers/{id}/exec` com `Cmd`, `AttachStdin/Stdout/Stderr`, `Tty`, `Env`, `WorkingDir`, `User`.
2. `POST /libpod/exec/{id}/start` com hijack (`Upgrade: tcp`), não WebSocket.
3. `POST /libpod/exec/{id}/resize?h=&w=` ao redimensionar.
4. Encerrar com EOF/Ctrl+D; propagar exit code.

**Critérios:**

- [ ] TTY alocado quando o shell é interativo; sem TTY para comandos.
- [ ] Ctrl+C envia SIGINT; Ctrl+D encerra; resize funciona em Wayland com escala fracionária.
- [ ] Shell padrão: `/bin/sh` se existir; fallback `/bin/bash`; erro claro se não houver.
- [ ] Copiar/colar: `Ctrl+Shift+C`/`Ctrl+Shift+V`, `Ctrl+Insert`/`Shift+Insert` e menu de contexto (Copiar/Colar) funcionam no terminal embutido.
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

### 7.2.1 Console serial (T4-15)

- O QEMU grava o serial em `machines/<id>/console.log` (`-serial file:`). Portas seriais extras
  anexadas à machine entram como `-serial <path>` adicionais (T8-02); a primeira porta continua
  sendo o console. Com áudio ativado (T8-03), entram `-audiodev pipewire,id=audio0`,
  `intel-hda` e `hda-duplex,audiodev=audio0`. O daemon expõe:
  - `machines.console.read {id, tailBytes?}` one-shot, default 64 KiB e máximo 512 KiB,
    devolvendo `{id, data}`; arquivo ausente → `{data:""}`.
  - `machines.console {id, tailBytes?}` subscription com tail inicial (`reset:true`) e
    polling de 300 ms dos bytes novos (`reset:false`); truncate/rotação/reaparecimento
    reinicia do offset 0 com `reset:true`; symlink/arquivo não regular encerra com
    `machines.console.error`.
  - `machines.console.unsubscribe {subscriptionId}` cancela o loop e libera a subscription.
- As validações são as mesmas de `machines.logs`: arquivo regular, sem symlink, caminho
  canônico confinado ao diretório da machine e path nunca exposto na resposta.
- Os bytes são convertidos com `QString::fromUtf8` (lossy); o console é tratado como texto
  UTF-8 aproximado, não como terminal interativo. Não há VNC nesta tarefa.
- CLI: `machine console <id|nome> [--tail BYTES] [--no-follow]`; sem `--no-follow` usa o
  stream até Ctrl+C/erro, com `--no-follow` imprime o tail one-shot. GUI: a aba **Logs** do
  inspetor tem **Seguir console**, que concatena os deltas em um buffer de até ~512 KiB,
  rola para o fim e para ao sair da aba/fechar o inspetor.

**Critérios:**

- [x] Console serial acessível quando o SSH falha (read one-shot e follow).
- [x] Truncate/rotação reinicia o stream com `reset:true`; symlink → erro e fim.
- [x] Follow para ao trocar de aba/fechar o inspetor.

### 7.3 SSH

- `podstack machine ssh --x11` adiciona `-X` (X11 forwarding) e `--x11-trusted` adiciona `-Y`;
  o host precisa de `DISPLAY`/`XAUTHORITY` e o guest de `xauth`/cliente X11 (T8-04). Wayland
  nativo depende do compositor/cliente do guest e não é encaminhado pelo SSH.
- Novas VMs recebem via cloud-init `xauth`, `x11-apps` (Debian/Ubuntu) ou `xorg-x11-apps`
  (Fedora), o drop-in `/etc/ssh/sshd_config.d/10-podstack-x11.conf` com `X11Forwarding yes`
  e o stack de áudio (PulseAudio no Debian/Ubuntu; PipeWire no Fedora). Em Debian o kernel
  cloud sem módulos de som é substituído pelo genérico e o guest reinicia no primeiro boot;
  em Ubuntu instala-se `linux-modules-extra`. VMs antigas precisam do passo manual.

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
- Snapshots internos do qcow2 não são preservados: o `qemu-img convert` produz um disco
  autocontido sem os snapshots, e o import/export `.tar.zst` também não os leva (§7.8).

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

- `--isolated` (CLI) / `isolated:true` (RPC/GUI): sem mounts do host, sem SSH agent e
  sem rede para o host ou outras máquinas. Implica `isolateNetwork=true`,
  `forwardSshAgent=false` e `mounts` vazio; o backend força essa combinação.
- Rejeitar com `PODSTACK_INVALID_PARAMS` e remediação quando `isolated=true` vier junto
  com mounts ou com `forwardSshAgent=true`; `isolateNetwork` é aceito (redundante).
- A porta SSH local continua ativa: o `hostfwd` da administração fica em `127.0.0.1`
  e não é afetado pelo isolamento. Nenhuma porta pode ter `exposeLan=true`;
  `machines.ports.set`/`machine port add` recusam essa combinação na machine isolada.
- Clone preserva `isolated`/`isolateNetwork` e zera o agent/mounts; export/import leva o
  campo `isolated` no manifesto `podstack.machine/1` (opcional, default `false`) e o
  import força `isolateNetwork=true`, `forwardSshAgent=false` e mounts vazio.
- Aviso: não é fronteira de segurança contra malware (kernel do guest é separado; QEMU é o limite).

### 7.8 Snapshots

- Snapshots são **internos do qcow2** e não têm tabela no SQLite; nome, data e tamanho vêm do
  `qemu-img info --output=json`. Não há snapshot de memória/VM state: o disco é sempre
  fotografado com a máquina parada.
- Listar (`machine snapshot ls`/`machines.snapshots.list`) funciona com a máquina parada ou
  rodando; criar/restaurar/remover param a máquina quando ela está rodando e a reiniciam ao
  final, inclusive quando a operação falha.
- Nome do snapshot: `^[A-Za-z0-9][A-Za-z0-9._-]{0,63}$`, único na máquina; duplicado retorna
  `PODSTACK_CONFLICT`, inválido `PODSTACK_INVALID_PARAMS` e inexistente `PODSTACK_NOT_FOUND`.
- Restaurar **substitui o estado atual do disco** com o conteúdo do snapshot; alterações
  posteriores são descartadas. A GUI pede confirmação explícita e a CLI imprime um aviso
  antes de executar (o comando `restore` já é a confirmação; não existe `--force`).
- Remover apaga o snapshot do qcow2; a operação é destrutiva e a GUI confirma.
- Limitações: `qemu-img convert` (export e clone) não preserva snapshots internos, e o
  export/import `.tar.zst` também não os leva. O snapshot referencia o disco overlay (com
  backing da imagem cloud); manter o backing disponível é responsabilidade do cache de imagens.

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
- `HttpProxy` (daemon) escuta **somente em loopback** (`127.0.0.1` e, quando
  disponível, `::1`) em `network.httpPort` (default 18080) e encaminha por `Host`.
- Configs do schema 1 com `network.httpPort=8080`/`network.httpsPort=8443` são migradas
  automaticamente para os defaults atuais ao carregar (`Config::load`, `schemaVersion` 2);
  valores customizados são preservados.
- Mapeamento: `nome.localhost` → container `nome` na **primeira porta TCP
  publicada** (`[ip:]porta->porta/tcp`); sem porta publicada → 404.
- `network.bridge=false` desabilita o proxy. `network.exposeLan=true` (opt-in,
  restart) faz o proxy escutar em `0.0.0.0`/`::`; o firewall das portas
  18080/18443 precisa ser liberado explicitamente.
- A conexão é fechada após a resposta (`Connection: close`) e o proxy adiciona
  `X-Forwarded-For`, `X-Forwarded-Proto` e `X-Forwarded-Host`.
- Fallback: porta publicada + `localhost:<porta>`.
- O mesmo mapeamento vale para `<nome>.podstack.local` (usado pelo DNS
  userspace e pelo HTTPS); `localhost` e `podstack.local` servem o índice.
- `http://localhost:<network.httpPort>/` serve a **página de índice** com os
  containers que publicam porta TCP: nome, estado, porta publicada e URL
  `http://<nome>.localhost:<porta>/`. Acesso por `GET`/`HEAD`; os demais
  métodos recebem 405.

**Critérios:**

- [x] `curl http://nome.localhost:18080` retorna o serviço.
- [x] Nome inexistente retorna 404 com página de índice.
- [x] Funciona sem root e sem alterar `/etc/hosts`.

Detecção automática de porta + cache ficam na T6-03; HTTPS em T6-06.

### 11.2 `*.podstack.local` (P1, opt-in com helper)

- Helper instala drop-in no `systemd-resolved`:

```
[Resolve]
DNS=127.0.0.1:5353
Domains=~podstack.local
```

- DNS userspace (`DnsServer`) escuta em `127.0.0.1:5353` (UDP e TCP quando
  disponível) e responde A/AAAA para `*.podstack.local` com 127.0.0.1/::1.
  O bind UDP usa `SO_REUSEADDR` para conviver com mDNS/Avahi que ocupe
  `0.0.0.0:5353`; se ainda assim falhar, o daemon loga o aviso e segue.
- Remoção idempotente; backup (`podstack.conf.bak`) e validação de
  zona/upstream contra allowlist (`podstack.local`, `127.0.0.1:5353`).
- Conflito com Avahi: detectar e avisar no resultado do helper.

**Critérios:**

- [x] Drop-in instalado/removido de forma idempotente, com backup.
- [x] DNS responde A/AAAA para `*.podstack.local` (UDP e TCP).
- [x] Avahi ativo gera aviso (não bloqueia).
- [ ] Toggle de settings/RPC `proxy.dns.*` e validação E2E com polkit ficam
  para a fiação do setup (T7).

### 11.3 HTTPS

- Caddy filho do daemon, ativado por `network.https` (default `false`), com CA
  interna e certificados on-demand para `*.localhost` e `*.podstack.local` na
  `network.httpsPort` (default 18443).
- O Caddyfile é gerado pelo daemon (`<data>/podstack/caddy/Caddyfile`), usa
  storage próprio (`<data>/podstack/caddy/storage`), `skip_install_trust` e
  encaminha o HTTP para o `HttpProxy` em `127.0.0.1:<network.httpPort>`; o
  `Host` original é preservado para a resolução do container.
- Confiar na CA via helper (`update-ca-trust`) apenas com consentimento: o
  helper exige CA autoassinada, fingerprint SHA-256 correspondente e arquivo
  pertencente ao chamador, grava o anchor e roda o update do trust store.
- CA montada read-only nos containers criados/duplicados pelo PodStack quando
  o arquivo existe; `dev.podstack.ca=false` faz opt-out. Containers de Compose
  ficam pendentes (o provider externo monta os volumes).
- Renovação automática; chave e CA com permissões restritas (root `0600`).
- Com `network.exposeLan=true`, o Caddyfile usa `bind 0.0.0.0` para expor o
  HTTPS na LAN (mesmo aviso de firewall).

**Critérios:**

- [x] Certificado válido para os domínios, assinado pela CA local.
- [x] `network.https=false` não sobe o Caddy; ausência do binário gera aviso e
  não derruba o daemon.
- [x] Chave/CA sem leitura por grupo/outros.
- [x] Confiar/remover a CA no host via helper, com fingerprint e consentimento.
- [x] Montar a CA nos containers de `containers.create`/`duplicate`, com
  opt-out `dev.podstack.ca=false` (Compose pendente).

### 11.4 Detecção de porta

- Na primeira requisição, testar as portas TCP **publicadas** (as únicas
  alcançáveis no modelo rootless) em paralelo, com timeout de 300 ms e
  `HEAD /` com `User-Agent: PodStack-Server-Detection`; a resposta que começa
  com `HTTP/` vence.
- Preferência por portas de container comuns: 80, 8080, 3000, 5000, 8000,
  8081, 8888, 443 e 8443; depois as demais em ordem crescente.
- Uma única porta publicada é usada direto, sem probe. Se nenhuma porta
  responde HTTP, o proxy usa a primeira publicada (comportamento anterior).
- Cachear por container (id) + digest de `image` + portas publicadas; recriar
  o container troca o id e invalida naturalmente. Cache limitado a 64 entradas
  e 10 minutos.
- Overrides por label: `dev.podstack.http-port`/`https-port` forçam a porta
  publicada (por porta do container ou do host; HTTPS conecta com TLS sem
  verificação de peer) e `dev.podstack.domains` adiciona domínios customizados
  ao roteamento por `Host` e ao índice. Label inválido cai na detecção normal.

**Critérios:**

- [x] Container com uma porta publicada funciona sem probe.
- [x] Com múltiplas portas, escolhe a que responde HTTP e prefere as comuns.
- [x] Cache evita novo probe; mudança de id/digest invalida.
- [ ] Override por label (T6-08).
- [ ] HTTPS de backend (T6-06).

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

### 11.6 Portas 80/443 (socket activation)

- O helper instala unidades systemd de sistema `podstack-http.socket` e/ou
  `podstack-https.socket` (`ListenStream=80`/`443`) mais serviços
  `systemd-socket-proxyd` que encaminham para `127.0.0.1:<httpPort|httpsPort>`
  (default 18080/18443). Com isso o browser acessa `http://nome.localhost/` sem
  porta.
- Portas e alvos são allowlisted: apenas 80/443 e apenas `127.0.0.1:<porta>`.
- `BindPorts`/`UnbindPorts` exigem polkit; a remoção é idempotente e apaga
  socket + service + backup.

**Critérios:**

- [x] Unidades geradas com `ListenStream` correto e alvo loopback validado.
- [x] Porta/alvo fora da allowlist é recusado antes da autorização.
- [x] `BindPorts`/`UnbindPorts` idempotentes (teste com runner fake).
- [ ] Bind real em :80/:443 com polkit e acesso `http://nome.localhost/` sem
  porta (validação manual com root; fiação pelo setup em T7).

### 11.7 Portas não publicadas (limitação)

No modo rootless o Podman não publica portas depois que o container foi
criado. Port forwarding de portas não publicadas (T6-10) fica adiado: as
alternativas são publicar no `podman run`/Compose ou usar um container
roteador; o PodStack não altera a rede do usuário nem usa root para isso.

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

Ver [`cli-parity.md`](../engineering/cli-parity.md) para a tabela completa `orb` → `podstack`.

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

## 15. Atualização segura e logs do app

> Requisitos: `FR-UPD-006…016`, `FR-LOG-001…009`, `NFR-UPD-001…004`, `NFR-LOG-001…003`
> ([`PRD.md`](PRD.md)). Decisão: **ADR-0012**.

### 15.1 Update manual `apt`/`dnf`

O pacote novo resolve o incidente 0.1.2→0.1.3 antes do unpack:

1. O `preinst` (Debian) ou `%pre` (RPM) percorre os usuários logados (`loginctl`) e, para cada um,
   aplica `mask --runtime podstack.socket`, para `podstack.socket`/`podstack.service` e encerra a
   GUI com SIGTERM (SIGKILL após 3 s). O daemon é parado via systemd; sem systemd, ou se o user
   manager não responder a `systemctl --machine user@<uid>.service --user`, usa
   `pkill -TERM -x podstackd` e valida com `pgrep`.
2. O helper é parado e conferido com `systemctl is-active`; se qualquer processo não encerrar, um
   **trap de limpeza** desfaz a máscara runtime, religa o helper dos uids já tocados e aborta o
   upgrade. O runner `podstack update --install --runner` nunca é encerrado.
3. O `postinst`/`%post` faz `daemon-reload`, `systemctl --global enable podstack.socket`,
   `unmask --runtime` por usuário (com retry e remediação) e **`restart podstack-helper.service`**.
   A GUI/daemon de usuário não são iniciados pelo root; o usuário reabre ou o runner religa.
4. No Debian, `postrm` trata `abort-upgrade`/`abort-install` restaurando helper e socket. No RPM, a
   ordem é `%pre` novo → `%preun` antigo → remoção → `%post` novo → `%posttrans` novo; o
   `%posttrans` restaura socket/helper por usuário, faz health check (`is-active` + versão) e só
   retorna não-zero se não conseguir.
5. O `preinst` não toca em Podman, containers, machines nem no runner. O `prerm` de remoção encerra
   os processos do app com o mesmo cuidado.

### 15.2 Update assistido (GUI/CLI)

1. `updates.check` devolve versão, canal, notas e metadados do manifesto. O runner baixa o
   manifesto assinado de `updates.feedUrl` para `~/.cache/podstack/updates/manifest.json`
   (`0600`, TTL 6 h) e o artefato (`0700`/`0600`, `.part` + rename atômico, limite 300 MB,
   preflight de espaço, timeout de 15 min), validando canal, arquitetura, formato e versão. Se
   `updates.publicKey` estiver customizada, o install assistido é recusado
   (`PODSTACK_UPDATE_UNSUPPORTED`, `reason=custom_key`).
2. A GUI grava `~/.local/state/podstack/updates/gui-was-open`, inicia
   `podstack update --install --runner --manifest <path>` em novo session/process group (`setsid`)
   e sai. `--runner` e `--manifest` são flags internas.
3. O runner grava `state.json` atomicamente (`QSaveFile`/rename) com `pid`, `boot_id` e timestamp,
   para `podstack.socket`/`podstack.service` do usuário e chama `StageUpdate`, que:
   autentica por polkit (`dev.podstack.helper.stage-update`, `auth_admin`), obtém o uid por
   `GetConnectionUnixUser`, confere arquivo regular/sem symlink (`O_NOFOLLOW`)/dono e copia para
   staging root-owned `/run/podstack/update/<uuid>/` (`0700`, timeout 5 min), valida a assinatura
   com a chave de release embutida ou `/etc/podstack/update.pub`, compara o SHA-256 ao manifesto e
   confere nome/versão/arch (`dpkg-deb -f`/`rpm -qp`).
4. `InstallUpdate(jobId)` serializa a admissão com `flock /run/podstack/update.admission.lock`,
   recusa unidade `podstack-update-*` ativa e inicia a unidade transiente de sistema
   `systemd-run --unit=podstack-update-<uuid> --collect --property=RuntimeMaxSec=1800`, cujo
   `ExecStart` é `flock /run/podstack/update.lock` + `podstack-update-exec`. O gerenciador roda
   fora do cgroup do helper/sessão; o restart do helper no meio do `apt` não o mata.
5. O wrapper executa `apt-get`/`dnf` com o pacote em staging, filtra stdout/stderr pela
   `Redaction`, grava `/var/log/podstack/jobs/<uid>/<jobId>.jsonl` (`0640`), espelha no journal e
   grava `/run/podstack/update/<uuid>/state.json` (`0640`) com fase/percentual/exit/error.
6. O runner faz polling de `GetUpdateStatus {jobId}` (ação `get-update-status`, `auth_admin_keep`)
   com retry/backoff enquanto o helper reinicia; `UpdateProgress`/`UpdateFinished` são
   best-effort. No start, o helper varre unidades e `state.json` e reconstrói o estado.
7. Sucesso: `systemctl --user start podstack.socket`; se `gui-was-open` existir,
   `podstack-gui --relaunch` e o marcador é apagado; `state.json=done`; notificação nativa.
8. Falha: helper mantido/restaurado ativo; runner religa socket/GUI/daemon da versão instalada,
   grava o erro em `state.json` e no log; a GUI/CLI mostra “Ver log”. Não há rollback automático;
   a próxima execução lê `state.json` e oferece retomar ou limpar.

**CLI:** `podstack update --install [--yes] [--json]`; sem `--yes` em TTY pede confirmação, sem TTY
nunca pergunta (exit 2). `Ctrl+C` antes da instalação cancela com 9; depois que a unidade começou o
cancelamento é recusado. Timeouts: download 15 min, stage 5 min, install 30 min (`RuntimeMaxSec`).
Exit codes: `--install` **0/1/2/6/8/9** (sem 3); `--check` mantém 0/1/2/3/6/8/9. Códigos estáveis:
`PODSTACK_UPDATE_MANIFEST_INVALID`, `PODSTACK_UPDATE_SIGNATURE_INVALID`,
`PODSTACK_UPDATE_PACKAGE_INVALID`, `PODSTACK_UPDATE_DOWNLOAD_FAILED`,
`PODSTACK_UPDATE_INSTALL_FAILED`, `PODSTACK_UPDATE_CONSENT_DENIED`,
`PODSTACK_UPDATE_VERSION_EQUAL`, `PODSTACK_UPDATE_LOCKED`, `PODSTACK_UPDATE_UNSUPPORTED`.
`updates.mode=auto` verifica/baixa, nunca instala sem consentimento. Flatpak/sandbox ou ausência de
systemd de usuário desabilita o update assistido com mensagem e caminho manual.

### 15.3 Processos e cgroups

| Processo | Antes do unpack | Depois |
|---|---|---|
| `podstack-gui` | SIGTERM (SIGKILL após 3 s); handler salva `UiState` | runner/manual reabre |
| `podstackd` | `systemctl --user stop`; `pkill -TERM` como fallback | socket activation |
| `podstack.socket` | `mask --runtime` + stop | `unmask --runtime`; start no runner/next use |
| `podstack-helper` | `systemctl stop` | `systemctl restart`; varre estado no start |
| runner `podstack update` | **não matar** | termina após religar |
| job de update | — | unidade transiente + wrapper; sobrevive ao restart do helper |
| QEMU/virtiofsd | **não parar** | scope próprio; reconciliação de órfãos cobre restart |
| Podman/containers | **não parar** | intactos |

### 15.4 Logs do app

- `Logging` instala `qInstallMessageHandler` que apenas enfileira (writer thread, fila 8192,
  drop-oldest com `dropped`); nunca faz I/O no thread da UI. O stderr/journal continua.
- Arquivos correntes por processo em `~/.local/state/podstack/logs/` (`0600`, dir `0700`):
  `podstackd.jsonl`, `podstack-gui-<pid>.jsonl`, `podstack-cli-<pid>.jsonl`; JSON Lines com `ts`,
  `level`, `category`, `process`, `pid`, `thread`, `message`, `context`.
- `O_APPEND`, flush a cada 1 s e em warn/error+; flush final no encerramento. Redaction aplicada
  **antes** de gravar, inclusive em logs root.
- Rotação no start e por timer diário à meia-noite local: `<processo>-YYYYMMDD.jsonl` e
  `<processo>-YYYYMMDD-N.jsonl` por tamanho, rename atômico sob `flock`. Retenção 7 dias (1–30),
  `advanced.logMaxBytes` 10 MB, teto total 200 MB; remove o mais antigo primeiro.
- `advanced.logLevel` (default info) exposto na GUI, aplicado em runtime (`restartRequired: false`)
  e por `PODSTACK_LOG_LEVEL`; `--verbose` continua como atalho.
- Setup jobs são persistidos no log do daemon (`setup` com `jobId`, ação, progresso e erro) e
  sobrevivem ao restart.
- Logs root: `/var/log/podstack/jobs/<uid>/<jobId>.jsonl` (dir `0750`, arquivo `0640`), com
  `uid`/`jobId` por linha/índice; `/var/log/podstack` root:root `0750`, `tmpfiles.d`,
  logrotate/timer e SELinux/AppArmor. O app nunca lê esses arquivos diretamente.
- `GetLogs {jobId?}` (`dev.podstack.helper.get-logs`, `auth_admin`, rate limit 10/min, auditoria)
  retorna somente operações do uid do chamador, com cap 1 MiB e `{lines, truncated}` redigido.
- O legado `podstackd.log` é migrado para JSONL no primeiro start ou removido; a unit não
  redireciona stdout para ele.

### 15.5 Acesso: CLI, GUI e report

- `podstack system logs [--follow|--since|--level|--component|--json|--export|--clear]` lê os
  arquivos do usuário, filtra e exporta. `--since` aceita `7d`/ISO 8601; `--level` usa o enum;
  `--component` aceita `daemon|gui|cli|qml|setup|update|helper|package` (`helper`/`package` via
  `GetLogs`, com polkit). `--export` canonicaliza o destino, recusa symlink/path fora do destino e
  grava `0600`. `--json` emite `{api:1, lines:[{ts,level,component,message}], truncated, files}`;
  erro emite `{api:1, error:{code,message,detail}}`; filtro inválido retorna 2. `--clear` apaga só
  logs do usuário. O subcomando `logs` de containers continua separado.
- GUI: página/diálogo **Logs** com filtro por nível/componente, follow, abrir pasta, exportar,
  “Exportar diagnóstico” e limpar com confirmação; leitura em worker/modelo virtualizado, sem I/O
  no thread da UI; “Ver log” no erro de setup/update abre o painel filtrado; i18n/`Accessible.name`.
- `system.report --include-logs` inclui os 7 dias do usuário e os logs do helper/update via
  `GetLogs`, com caps de 10.000 linhas/componente e 5 MB no total; `diagnostics.includeLogs` passa
  a default **false**; nada é enviado.

### 15.6 Critérios de aceite

| ID | Dado | Quando | Então |
|---|---|---|---|
| AC-UPD-1 | Ubuntu 24.04, GUI+daemon+helper 0.1.3 abertos e socket ativo | `sudo apt install ./podstack_0.1.4_amd64.deb` | PIDs antigos ausentes; helper novo; socket não reativa daemon antigo; GUI nova; OVMF `ok`; containers/machines intactos. |
| AC-UPD-2 | 0.1.4 com app aberto | GUI “Atualizar” para 0.1.5 | Download/verificação; GUI fecha; unidade transiente instala; restart do helper não mata a unidade; GUI/daemon novos; log do update. |
| AC-UPD-3 | Mesmo cenário | Falha de download/assinatura/unpack/`postinst`/após `%pre` | Erro estável; helper/socket restaurados; GUI/daemon instalados voltam; log da causa; sem órfãos. |
| AC-LOG-1 | App com setup/update/erro | `system logs --json`/GUI/report | Logs do próprio uid; schema `{api:1,...}`; uid A não vê B; 7 dias; `0600`/`0640`; redaction com `diagnostics.redact=false`; “Ver log”. |
| AC-LOG-2 | 0.1.4→0.1.5 | 8º dia e limite por arquivo | Mais antigo removido; rotação `-YYYYMMDD[-N].jsonl`; timer da meia-noite local. |

---

## 16. Casos de borda

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

## 17. Fora do escopo desta spec

- Implementação de runtime OCI, CNI ou hypervisor próprio.
- Compatibilidade com plugins/extensões do OrbStack.
- macOS/Windows.
- Orquestração multi-host.
- Detalhes visuais pixel-a-pixel (ver `ui-map.md` para o design próprio).
