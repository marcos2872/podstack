# API JSON-RPC — `podstackd`

> Contrato entre GUI/CLI e o daemon. Transporte: **Unix domain socket** `$XDG_RUNTIME_DIR/podstack/control.sock` (0600). Protocolo: **JSON-RPC 2.0**, mensagens delimitadas por `\n` (NDJSON). Sem TCP, sem TLS (o socket já é local e autenticado pelo kernel).

---

## 1. Transporte e autenticação

| Item | Regra |
|---|---|
| Socket | `$XDG_RUNTIME_DIR/podstack/control.sock` |
| Diretório | `0700`; socket `0600` |
| Auth | `SO_PEERCRED` — `uid` do cliente deve ser igual ao dono do daemon |
| Framing | uma mensagem JSON por linha (`\n`) |
| Tamanho máximo | 8 MiB por mensagem (configurável) |
| Backpressure | notificações podem ser descartadas; `logs.dropped` informa |
| Versão | `system.hello` retorna `apiVersion` (inteiro) |

Cliente que falhar no handshake é desconectado.

---

## 2. Formato

### 2.1 Requisição

```json
{ "jsonrpc": "2.0", "id": "1", "method": "containers.list", "params": { "all": true } }
```

### 2.2 Resposta

```json
{ "jsonrpc": "2.0", "id": "1", "result": { "items": [] } }
```

### 2.3 Erro

```json
{
  "jsonrpc": "2.0",
  "id": "1",
  "error": {
    "code": -32004,
    "message": "Container 'api' não encontrado.",
    "data": {
      "code": "PODSTACK_CONTAINER_NOT_FOUND",
      "detail": "libpod: no such container",
      "remediation": "Atualize a lista ou verifique o nome.",
      "retryable": false,
      "traceId": "c0a8012e-..."
    }
  }
}
```

### 2.4 Notificação (server → client)

```json
{ "jsonrpc": "2.0", "method": "event", "params": { "type": "container.start", "id": "...", "at": "..." } }
```

### 2.5 Handshake

```json
→ { "jsonrpc": "2.0", "id": 0, "method": "system.hello", "params": { "client": "gui", "version": "0.1.0", "api": 1 } }
← { "jsonrpc": "2.0", "id": 0, "result": { "apiVersion": 1, "daemonVersion": "0.1.0", "podman": { "version": "5.8.9", "api": "1.44", "socket": "..." } } }
```

`api` diferente do suportado → erro `PODSTACK_API_MISMATCH` (fatal para o cliente).

---

## 3. Tipos comuns

```ts
type Timestamp = string;      // RFC 3339 UTC
type ID = string;             // UUID ou ID do engine
type State = "created"|"running"|"paused"|"stopped"|"exited"|"dead";

interface Page { offset?: number; limit?: number } // default 0/100, max 1000

interface ErrorData {
  code: string;               // estável, ex.: PODSTACK_CONFLICT
  detail?: string;
  remediation?: string;
  retryable: boolean;
  traceId: string;
}

interface JobRef { jobId: string }

interface Subscription { subscriptionId: string }
```

---

## 4. Métodos

> Todos os métodos retornam `result` objeto (nunca `null` cru). Listas usam `{ items, total? }`.
> `params` é sempre objeto. Campos opcionais são omitidos quando não usados.

### 4.1 `system.*`

| Método | Params | Result |
|---|---|---|
| `system.hello` | `{client, version, api}` | `{apiVersion, daemonVersion, podman:{version,api,socket,rootless}}` |
| `system.status` | `{}` | `{state, engine, mode, version, uptimeSeconds, counts, warnings[]}` |
| `system.doctor` | `{deep?:bool}` | `{checks:[{id,status,detail,remediation}]}` |
| `system.report` | `{includeLogs?:bool}` | `{path, bytes, sha256}` |
| `system.df` | `{detailed?:bool}` | `{json, detailed, sections, totalReclaimableBytes}` |
| `system.stats.subscribe` | `{intervalSeconds?}` | `Subscription`; notifica `system.stats.delta` |
| `system.stats.unsubscribe` | `{subscriptionId}` | `{subscriptionId, cancelled}` |
| `system.shutdown` | `{stopContainers?:bool}` | `{}` |

#### `system.df`

`params`: `{detailed?: bool}` — padrão `true` para compatibilidade. Com `detailed:false`,
cada seção mantém `count`, `sizeBytes` e `reclaimableBytes`, mas omite `items`.

O resultado preserva o payload bruto do Libpod em `json` (compatibilidade) e adiciona um bloco
normalizado:

```json
{
  "json": { "ImagesSize": 0, "Images": [], "Containers": [], "Volumes": [] },
  "detailed": true,
  "sections": {
    "images": {
      "count": 0,
      "sizeBytes": 0,
      "reclaimableBytes": 0,
      "items": [
        { "id": "", "repository": "", "tag": "", "sizeBytes": 0,
          "uniqueSizeBytes": 0, "containers": 0, "dangling": false, "unused": true }
      ]
    },
    "containers": {
      "count": 0,
      "sizeBytes": 0,
      "reclaimableBytes": 0,
      "items": [
        { "id": "", "name": "", "status": "", "sizeBytes": 0, "running": false }
      ]
    },
    "volumes": {
      "count": 0,
      "sizeBytes": 0,
      "reclaimableBytes": 0,
      "items": [
        { "name": "", "sizeBytes": 0, "reclaimableBytes": 0, "inUse": false }
      ]
    },
    "buildCache": { "count": 0, "sizeBytes": 0, "reclaimableBytes": 0, "items": [] }
  },
  "totalReclaimableBytes": 0
}
```

Regras de normalização:

- `images.dangling`: `Repository` vazio ou `<none>`; `images.unused`: `Containers == 0`.
- `images.reclaimableBytes`: soma de `UniqueSize` (fallback `Size`) apenas das imagens
  *dangling*.
- `containers.running`: `Status`/`State` em `running` ou `paused`;
  `containers.reclaimableBytes`: soma de `Size` dos containers parados.
- `volumes.inUse`: `Links > 0`; `volumes.reclaimableBytes`: soma de `ReclaimableSize`
  (fallback `0`) apenas dos volumes **sem uso** — volumes em uso contribuem com `0`.
- `totalReclaimableBytes`: imagens + containers + volumes. É uma **estimativa**: camadas
  compartilhadas podem reduzir o espaço realmente liberado.
- Entradas malformadas ou sem identificação são ignoradas.

### 4.2 `containers.*`

| Método | Params | Result |
|---|---|---|
| `containers.list` | `{all?, state?: "running"\|"stopped"\|"paused", filter?, limit?, offset?}` | `{items: Container[], total}` |
| `containers.get` | `{id}` | `Container` |
| `containers.create` | `{image, name?, command?, env?, ports?, mounts?, workDir?, user?, remove?, init?, privileged?, exposeLan?, start?}` | `{id, started, exposeLan}` |
| `containers.exposeLan` | `{id, expose:bool}` | `{id, expose, ports:[string]}` |
| `containers.duplicate` | `{id, name?}` | `{id, source, started}` |
| `containers.start` | `{id}` | `{id, ok:true}` |
| `containers.stop` | `{id, timeoutSeconds?}` | `{id, ok:true}` |
| `containers.restart` | `{id, timeoutSeconds?}` | `{id, ok:true}` |
| `containers.pause` / `unpause` | `{id}` | `{id, ok:true}` |
| `containers.kill` | `{id, signal?}` | `{id, ok:true}` |
| `containers.remove` | `{id, force?, volumes?}` | `{id, ok:true}` |
| `containers.rename` | `{id, name}` | `{id, ok:true}` |
| `containers.inspect` | `{id}` | `{id, json: object}` |
| `containers.top` | `{id}` | `{titles:[], processes:[][]}` |
| `containers.changes` | `{id}` | `{changes:[{path,kind}]}` |
| `containers.logs.subscribe` | `{id, tail?, follow?, timestamps?, since?}` | `Subscription` |
| `containers.logs.unsubscribe` | `{subscriptionId}` | `{}` |
| `containers.stats.subscribe` | `{ids?, intervalSeconds?}` | `Subscription` |
| `containers.stats.unsubscribe` | `{subscriptionId}` | `{}` |
| `containers.exec` | `{id, cmd: string[]}` | `{id, exitCode, stdout}` (one-shot; TTY/sessão via `exec.*` no M1) |
| `containers.exec.create` | `{id, cmd, tty?, user?, workdir?, env?}` | `{sessionId, execId}` |
| `containers.exec.write` | `{sessionId, dataBase64}` | `{}` |
| `containers.exec.resize` | `{sessionId, rows, cols}` | `{}` |
| `containers.exec.close` | `{sessionId}` | `{exitCode?}` |
| `containers.files.list` | `{id, path}` | `{path, entries:[{name,size,directory,mode,modified}]}` |
| `containers.files.download` | `{id, path, destination}` | `{path, destination}` |
| `containers.files.upload` | `{id, path, sourcePath}` | `{path, sourcePath, ok:true}` |
| `containers.export` | `{id, path}` | `JobRef` |
| `containers.commit` | `{id, repo, tag?, message?}` | `{imageId}` |

> `containers.create` e `containers.duplicate` aplicam `network.proxy`/`network.proxyExclude`
> ao `env` do novo container, sem sobrescrever chaves definidas explicitamente pelo usuário
> (ou herdadas do container duplicado). A configuração é lida na criação; containers
> existentes exigem recriação. Ver [`settings.md`](settings.md) e [`spec.md`](spec.md) §6.3.

> **Portas e LAN:** sem `exposeLan`, cada `ports[]` sem `hostIp` explícito usa
> `hostIp=127.0.0.1`; com `exposeLan:true`, o daemon força `hostIp=0.0.0.0` e chama
> `OpenPorts` no helper para as host ports (`tcp`/`udp`). A ausência do helper retorna
> `PODSTACK_HELPER_REQUIRED` **antes** de criar o container; falha ao abrir o firewall
> remove o container recém-criado e retorna `PODSTACK_IO_ERROR`. `containers.exposeLan`
> só opera em containers já publicados em `0.0.0.0`; binds em `127.0.0.1`/IP específico
> retornam `PODSTACK_UNSUPPORTED` com a remediação de recriar o container. No firewalld as
> regras são runtime (sem `--permanent`) e não sobrevivem a reload/restart; no ufw são
> persistentes até `ClosePorts`/`ufw delete allow`. Ver
> [`spec.md`](spec.md) §6.2 e [`security.md`](security.md) §4.2.

### 4.3 `images.*`

| Método | Params | Result |
|---|---|---|
| `images.list` | `{all?}` | `{items: Image[], total}` |
| `images.get` / `inspect` | `{id}` | `{id, json}` |
| `images.pull` | `{reference, platform?}` | `{subscriptionId, reference}` + `images.pull.delta/done/error` |
| `images.push` | `{id, destination}` | `{subscriptionId, destination}` + `images.push.delta/done/error` |
| `images.tag` / `untag` | `{id, repo, tag?}` | `{id, repo, ok:true}` |
| `images.remove` | `{id, force?}` | `{id, removed:true}` |
| `images.prune` | `{all?}` | `{reclaimedBytes}` |
| `images.history` | `{id}` | `{id, items:[{id,created,createdBy,size,comment}]}` |
| `images.export` | `{id, path}` | `{id, path}` |
| `images.load` | `{path}` | `{path, ok:true}` |

### 4.4 `volumes.*`

| Método | Params | Result |
|---|---|---|
| `volumes.list` | `{}` | `{items: Volume[], total}` |
| `volumes.create` | `{name?, driver?, options?, labels?}` | `Volume` |
| `volumes.remove` | `{name, force?}` | `{name, removed:true}` |
| `volumes.prune` | `{}` | `{reclaimedBytes}` |
| `volumes.clone` | `{source, destination}` | `{source, destination, ok:true}` |
| `volumes.export` | `{name, path}` | `{name, path}` (tar.zst) |
| `volumes.import` | `{name, path, force?}` | `{name, path, ok:true}` |
| `volumes.importHostPath` | `{name, sourcePath}` | `{name, sourcePath, ok:true}` |

### 4.5 `networks.*` e `pods.*`

| Método | Params | Result |
|---|---|---|
| `networks.list` | `{}` | `{items: Network[], total}` |
| `networks.create` | `{name, driver?, subnet?, gateway?, ipv6?, labels?}` | `Network` |
| `networks.remove` | `{name, force?}` | `{name, removed:true}` |
| `networks.connect` | `{name, containerId, aliases?}` | `{name, containerId, ok:true}` |
| `networks.disconnect` | `{name, containerId}` | `{name, containerId, ok:true}` |
| `pods.list` | `{}` | `{items: Pod[], total}` |
| `pods.create` | `{name, labels?}` | `Pod` |
| `pods.start` / `stop` / `restart` | `{id}` | `{id, action, ok:true}` |
| `pods.remove` | `{id, force?}` | `{id, removed:true}` |

### 4.6 `machines.*`

Implementados na T4-03 (M4), T4-06, T4-07, T4-08, T4-10, T4-11 e T4-12: registry,
listagem, inspeção, criação assíncrona, logs do console, ciclo de vida, mounts virtiofs,
arquivos, clone/rename, limites de CPU/memória/disco, port forwarding e a machine default do
CLI. A T4-04 adiciona `machines.ssh` (chave ed25519 e bloco gerenciado no `~/.ssh/config`). Os
demais métodos continuam planejados para as tarefas seguintes.

| Método | Params | Result |
|---|---|---|
| `machines.distros` | `{}` | `{items: DistroImage[]}` |
| `machines.list` | `{}` | `{items: Machine[]}` |
| `machines.get` | `{id}` | `Machine` (aceita ID ou nome) |
| `machines.create` | `{name, distro, version?, arch?, cpus?, memoryBytes?, diskBytes?, user?, password?, userData?, isolateNetwork?, forwardSshAgent?, mounts?, isolated?}` | `{jobId, subscriptionId}` (`JobRef` + `Subscription`) |
| `machines.cancel` | `{jobId}` | `{jobId, cancelled:true}` |
| `machines.start` | `{id}` | `Machine` |
| `machines.stop` | `{id, force?}` | `Machine` |
| `machines.restart` | `{id, force?}` | `Machine` |
| `machines.delete` | `{id, force?}` | `{deleted:true}` |
| `machines.export` | `{id, path, force?}` | `JobRef` + `Subscription` |
| `machines.import` | `{path, name?, force?}` | `JobRef` + `Subscription` |
| `machines.rename` | `{id, name}` | `Machine` |
| `machines.clone` | `{id, name}` | `JobRef` + `Subscription` |
| `machines.update` | `{id, cpus?, memoryBytes?, diskBytes?}` | `Machine` |
| `machines.ports.list` | `{id}` | `{items: MachinePort[]}` |
| `machines.ports.set` | `{id, ports}` | `Machine` |
| `machines.default` | `{name?}` | `{name}` (sem `name` = get; com `name` = set) |
| `machines.logs` | `{id, tail?}` | `{lines: string[]}` |
| `machines.ssh` | `{id}` | `{host, port, user, identityFile}` |
| `machines.files.list` | `{id, path?}` | `{path, items: MachineFile[]}` |
| `machines.files.download` | `{id, path, destination}` | `{path, destination}` |
| `machines.files.upload` | `{id, path, sourcePath}` | `{path, sourcePath, ok:true}` |
| `machines.stats` | `{id}` | `MachineStats` (one-shot via SSH) |
| `machines.stats.subscribe` | `{id, intervalSeconds?}` | `{subscriptionId, id}` |
| `machines.stats.unsubscribe` | `{subscriptionId}` | `{subscriptionId, cancelled}` |

**Planejados (não implementados ainda):**

| Método | Params | Result | Tarefa |
|---|---|---|---|
| `machines.console` | `{id}` | `Subscription` (stream serial) | T4-15 |

```ts
interface Machine {
  id: string; name: string;
  distro: string; distroVersion: string; arch: "amd64"|"arm64";
  cpus: number; memoryBytes: number; diskBytes: number;
  diskPath: string; seedPath: string; sshKeyPath: string;
  sshPort: number; sshUser: string;
  state: "provisioning"|"starting"|"running"|"stopping"|"stopped"|"error"|"deleting"|"deleted";
  isolated: boolean; isolateNetwork: boolean; forwardSshAgent: boolean;
  lastBootAt: string; createdAt: string; updatedAt: string; error: string;
  ports: MachinePort[]; mounts: MachineMount[];
}
interface MachinePort { id: string; hostPort: number; guestPort: number; protocol: string; exposeLan: boolean; label: string }
interface MachineMount { id: string; source: string; target: string; readOnly: boolean; tag: string }
```

**Criação assíncrona.** `machines.create` valida os parâmetros de forma síncrona (nome RFC
1123, CPUs 1..256, memória/disco ≥ 1 GiB, arquitetura e distro) e só então inicia o job.
Progresso e resultado chegam como notificações:

- `machines.delta {jobId, stage, progress}` — `stage` é `image`, `disk`, `seed`, `starting`
  ou `running`; `progress` vai de 0 a 100.
- `machines.done {jobId, machine?, error?}` — `machine` é o objeto `Machine` em caso de
  sucesso; `error` é um `ErrorData` (mesmo formato de `data` dos erros) em caso de falha,
  inclusive `PODSTACK_CANCELLED`.

`machines.cancel` apenas sinaliza o job; o cancelamento é observado no próximo checkpoint
(download, disco, seed ou início) e o job termina com `machines.done`/`PODSTACK_CANCELLED`.
A subscription é removida quando o job termina; cancelar um job inexistente retorna
`PODSTACK_NOT_FOUND`. `isolated:true` ainda retorna `PODSTACK_UNSUPPORTED` (T4-14);
`isolateNetwork` e `forwardSshAgent` são persistidos no metadado.

**Mounts virtiofs (T4-07).** `mounts` é um array de até **8** objetos
`{source, target, readOnly?, tag?}`. `source` precisa ser um diretório absoluto existente no
host e é canonicalizado no metadado; `target` precisa ser absoluto e não pode conter `..`;
`readOnly` default `false`; `tag` default `host<N>` e deve ser única e casar com
`[a-z0-9-]{1,32}`. A validação é síncrona: qualquer mount inválido retorna
`PODSTACK_INVALID_PARAMS` sem iniciar o job. O daemon inicia um `virtiofsd` por mount antes do
QEMU (`virtiofs-<tag>.sock` dentro do diretório da machine) e encerra os processos no
stop/remove; falha em um mount desfaz os anteriores e o job termina em erro. Sem o binário
`virtiofsd` (e sem `PODSTACK_VIRTIOFSD_BINARY`), o job falha com `PODSTACK_UNSUPPORTED` e
remediação.

**Export/import (T4-09).** `machines.export` para a machine (se estiver rodando),
converte o disco para um qcow2 autocontido (`qemu-img convert`), grava
`disk.qcow2`, `seed.iso`, `id_ed25519`, `id_ed25519.pub` e `manifest.json` em
`<path>` (`.tar.zst`, filtro zstd) com checksum SHA-256 por artefato e reinicia a
machine ao terminar. O arquivo é gravado em `<path>.part` e renomeado só no
sucesso; o destino precisa ser absoluto e não pode existir sem `force`. Progresso:
`machines.delta {jobId, stage, progress}` com `stage` em `stopping`, `disk`,
`archive`, `starting` ou `done`; o resultado chega em
`machines.done {jobId, path, machine?, error?}`. `machines.import` extrai em
diretório temporário, valida formato `podstack.machine/1`, nomes de entrada,
tamanhos e os checksums do manifesto, cria uma **nova** machine com ID, porta e
bloco SSH próprios, preservando a identidade/chave SSH do arquivo (o pacote é
uma credencial), grava `meta.json`/SQLite e o bloco SSH, e deixa a machine
`stopped`. `name` vazio usa o nome do manifesto;
nome existente retorna `PODSTACK_CONFLICT` sem `force` (com `force`, a machine
existente é removida e substituída). Mounts do host não são exportados. Progresso:
`verify`, `metadata`, `done`. Cancelamento em ambos usa os checkpoints e termina em
`PODSTACK_CANCELLED`, removendo o `.part`/diretório temporário; `machines.cancel`
aceita o `jobId` dos dois jobs. Export/import são serializados entre si e uma
segunda operação na mesma machine (start/stop/restart/delete/export) retorna
`PODSTACK_CONFLICT` enquanto o job estiver em andamento. O arquivo final fica
`0600` (contém a chave privada SSH da machine).

**Clone/rename/default (T4-10).** `machines.rename {id, name}` valida o nome RFC 1123 e
recusa machines em `provisioning`/`deleting` ou com o nome já usado por outra machine
(`PODSTACK_CONFLICT`). O método remove o bloco SSH antigo, troca o nome em `meta.json`/SQLite,
recria o bloco SSH e atualiza `machines.default` quando ele apontava para o nome antigo.
`machines.clone {id, name}` cria uma machine independente: para a origem se estiver rodando,
gera um novo ID/porta, converte o disco para um qcow2 autocontido com `qemu-img convert`,
copia `seed.iso`, `id_ed25519` e `id_ed25519.pub`, grava metadados/bloco SSH e deixa a cópia
`stopped` com apenas a porta SSH e sem mounts; a origem volta a rodar ao final (sucesso ou
falha). O job emite `machines.delta {jobId, stage, progress}` com `stopping`, `disk`, `copy`,
`metadata`, `starting` e `done` e termina em `machines.done {jobId, name, machine?, error?}`.
Cancelamento usa `machines.cancel` e termina em `PODSTACK_CANCELLED`, apagando diretório,
store e bloco SSH parciais. `machines.default {name?}` lê ou grava a chave
`machines.default` do Config (aceita ID ou nome e persiste o nome canônico); machine
inexistente retorna `PODSTACK_NOT_FOUND`. Remover a machine default limpa a chave.

**Limites de CPU/memória/disco (T4-11).** `machines.update {id, cpus?, memoryBytes?,
diskBytes?}` aceita ao menos um campo e é síncrono. CPUs precisam ficar em 1..256 e
memória/disco em ≥ 1 GiB (`PODSTACK_INVALID_PARAMS`); o disco nunca encolhe — um valor
menor que `diskBytes` atual é recusado. A máquina é parada se estiver `running`/`starting`
(e reiniciada ao final, mesmo quando a atualização falha), o `resize` do qcow2 é feito
offline com `qemu-img resize` e só então os metadados são persistidos; se a persistência
falhar depois do resize, o erro informa que o disco já cresceu. Growpart/filesystem dentro
do guest é manual. Progresso não é emitido (operação síncrona).

**Port forwarding (T4-12).** `machines.ports.list {id}` devolve
`{items: MachinePort[]}`; `machines.ports.set {id, ports}` é síncrono e devolve o `Machine`
atualizado. `ports` é a lista **completa das portas não-SSH** — a porta SSH (guest 22/tcp) é
gerenciada pelo daemon, sempre preservada na resposta e não pode ser removida nem alterada;
um item com `guestPort: 22` e `protocol: "tcp"` só é aceito se repetir o `hostPort` atual da
SSH (e é ignorado/deduplicado). Cada porta aceita `{id?, hostPort, guestPort, protocol?,
exposeLan?, label?}`: `hostPort`/`guestPort` em 1..65535, `protocol` `tcp` (default) ou
`udp`, `exposeLan` default `false` (bind `127.0.0.1`; `true` usa `0.0.0.0`), `label` até 64
caracteres sem controles e `id` gerado (UUIDv7) quando ausente. `hostPort` é único por
protocolo entre **todas** as machines (`PODSTACK_CONFLICT`), inclusive contra a SSH de cada
uma; ids repetidos, protocolo/porta/label inválidos retornam `PODSTACK_INVALID_PARAMS`. Se a
machine estiver rodando, ela é parada e reiniciada para aplicar os novos `hostfwd` (a linha de
comando do QEMU é imutável); machine parada apenas persiste `meta.json`+SQLite. Metadados
antigos sem `ports` continuam válidos: o daemon sintetiza a porta SSH a partir de `sshPort`.
A validação ocorre antes de parar a machine; o `meta.json`+SQLite é persistido após o stop e
antes do restart. Erro de restart devolve `PODSTACK_CONFLICT` informando que as portas já
foram salvas.

**Logs do console.** `machines.logs` resolve `id` ou nome e lê apenas
`machines/<id>/console.log`, sem expor o caminho na resposta. O arquivo precisa ser regular
e não pode ser symlink; a leitura usa no máximo os últimos 512 KiB e devolve as últimas
`tail` linhas (`tail` default 200, limitado a 1..2000). Se a machine ainda não bootou e o
arquivo não existe, a resposta é `{lines:[]}`. A leitura é one-shot; não há follow/stream
neste método (console serial é a T4-15).

**SSH.** `machines.ssh` resolve ID ou nome, exige que a chave privada da machine exista
(`0600`, reparada se necessário), garante o bloco gerenciado em `~/.ssh/config` e devolve
`{host:"127.0.0.1", port, user, identityFile}`. O bloco é idempotente e não expõe o
conteúdo da chave. `PODSTACK_SSH_CONFIG` sobrescreve o caminho do config em dev/teste.
Erros: `PODSTACK_NOT_FOUND` (machine inexistente), `PODSTACK_IO_ERROR` (chave/config
inacessível) e `PODSTACK_PERMISSION_DENIED` (permissões não corrigíveis).

**Arquivos (T4-08).** O file manager da machine usa SSH/SCP (não depende de mounts
virtiofs) e resolve `id` ou nome antes de qualquer transferência. `machines.files.list`
executa `ssh -i <chave> -p <porta> ... "ls -la --time-style=long-iso -- '<path>'"`, com o
caminho remoto escapado para o shell (`'` → `'\''`) e sem shell local; `.`/`..` e a linha
`total` são ignorados, symlinks usam o nome do link e o resultado é ordenado pelo `ls`.
`path` default é `/`. `machines.files.download` e `machines.files.upload` usam `scp -r`
com `-P <porta>`, `-i <chave>`, `IdentitiesOnly=yes`, `StrictHostKeyChecking=accept-new`,
`BatchMode=yes` e `ConnectTimeout=5`; a origem/destino local precisa ser absoluto, sem
`..` e sem caracteres de controle, e o destino não pode ser um diretório. As operações são
síncronas (sem job/progresso nesta versão) e não aceitam caminhos internos da instalação.
`PODSTACK_SSH_BINARY`/`PODSTACK_SCP_BINARY` sobrescrevem os binários em dev/teste.

```ts
interface MachineFile {
  name: string; path: string; directory: boolean;
  size: number; modified: string; // "YYYY-MM-DD HH:MM" (ls long-iso)
}
```

Erros de arquivos: `PODSTACK_INVALID_PARAMS` (path/destino/origem inválidos),
`PODSTACK_IO_ERROR` (origem local ausente, SSH/SCP falhou ou saída grande demais),
`PODSTACK_TIMEOUT` (listagem 15 s, transferência 10 min) e `PODSTACK_UNSUPPORTED`
(`ssh`/`scp` ausente).

**Métricas.** `machines.stats` coleta CPU, memória, rede e disco **de dentro da machine** por
um SSH one-shot (sem `qemu-guest-agent`). O comando remoto é fixo e não recebe entrada do
usuário: lê `/proc/stat`, `/proc/meminfo`, `/proc/net/dev`, `/proc/diskstats` e
`df -B1 -P /`. O `ssh` local roda sem shell, com `-i <chave> -p <porta>`,
`IdentitiesOnly=yes`, `StrictHostKeyChecking=accept-new`, `BatchMode=yes` e
`ConnectTimeout=3`, com timeout total de 6 s. A machine precisa estar `running`; a chave
privada precisa existir e ser regular (sem symlink). Taxas de rede/disco e CPU usam a
diferença entre amostras; a primeira amostra de cada machine vem com taxas zero.

```ts
interface MachineStats {
  ts: number; cpuPercent: number;
  memUsed: number; memTotal: number; memPercent: number;
  netRx: number; netTx: number;
  diskRead: number; diskWrite: number;
  diskUsed: number; diskTotal: number; diskPercent: number;
}
```

`machines.stats.subscribe` repete a coleta a cada `intervalSeconds` (1..10, default 2) em
background e devolve `{subscriptionId, id}`. Cada amostra chega como
`machines.stats.delta {subscriptionId, id, ...MachineStats}`; em falha o daemon envia
`machines.stats.error {subscriptionId, id, error}` e encerra a subscription. O cancelamento
usa `machines.stats.unsubscribe {subscriptionId}` como as demais subscriptions. Erros:
`PODSTACK_CONFLICT` (machine não está `running`), `PODSTACK_UNSUPPORTED` (cliente SSH
ausente), `PODSTACK_TIMEOUT`, `PODSTACK_IO_ERROR` (SSH/chave/resposta inválida) e os erros
de resolução da machine. `PODSTACK_SSH_BINARY` sobrescreve o binário do SSH em dev/teste.

`DistroImage`: `{distro, version, arch, default, url, checksum, algorithm, size, cached}`.
`machines.distros` lê o registry pinado no binário e consulta o cache local (sem hash e sem
download); `cached` reflete apenas arquivo + sidecar válidos.

Erros da área: `PODSTACK_INVALID_PARAMS` (parâmetros/ID ausente), `PODSTACK_NOT_FOUND`
(machine/job inexistente), `PODSTACK_CONFLICT` (nome duplicado/machine em remoção),
`PODSTACK_UNSUPPORTED`, `PODSTACK_CANCELLED`, `PODSTACK_PERMISSION_DENIED` e
`PODSTACK_IO_ERROR`. A senha nunca é devolvida, registrada em log ou exposta em notificação.

### 4.7 `compose.*` e `builds.*`

| Método | Params | Result |
|---|---|---|
| `compose.projects.list` | `{}` | `{items: ComposeProject[], total}` |
| `compose.up` | `{projectPath, detach?}` | `{projectPath, action, ok:true}` |
| `compose.down` | `{projectPath, volumes?}` | `{projectPath, action, ok:true}` |
| `compose.restart` / `stop` / `start` | `{projectPath, service?}` | `{projectPath, service, action, ok:true}` |
| `compose.logs.subscribe` | `{project, service?}` | `{subscriptionId, project}` + `compose.logs.delta {service, containerId, line}` |
| `compose.logs.unsubscribe` | `{subscriptionId}` | `{subscriptionId, cancelled}` |
| `builds.start` | `{contextPath, dockerfile?, tag, buildArgs?, platform?, platforms?:string[], trigger?}` | `{buildId, subscriptionId, logPath}` + `builds.delta {buildId, line}` + `builds.done {buildId, status, imageId, error, durationMs}` |
| `builds.cancel` | `{id}` | `{id, subscriptionId, cancelled}` |
| `builds.list` | `{limit?, offset?}` | `{items: Build[], total}` |
| `builds.get` | `{id}` | `Build` (+ `steps`) |
| `builds.remove` | `{id}` | `{id, removed:true}` |

> O provider Compose é detectado em `docker-compose`, `podman compose` ou `docker compose`
> (ou `PODSTACK_COMPOSE_PROVIDER`). `up/down/restart/stop/start` são síncronos e podem
> demorar; o cliente usa timeout de 10 min. O build empacota o contexto em tar (até 512 MB),
> envia para `POST /libpod/build` e persiste metadados em SQLite (`builds`/`build_steps`).
>
> `platforms` é um array de `os/arch[/variant]` (1..8, únicos); `platform` continua aceito por
> compatibilidade e é dividido por vírgula quando `platforms` está ausente. `Build` inclui
> `platforms:string[]` (vazio quando não informado). Com **0/1** plataforma o build usa Libpod;
> com **2+** o daemon remove o manifest anterior (`podman manifest rm <tag>`, falha ignorada) e
> executa o CLI do Podman (`podman build --platform p1,p2 --manifest <tag> -f ...`) em job
> assíncrono, sem steps. Lista inválida retorna `PODSTACK_INVALID_PARAMS` na chamada; falha do
> CLI vira `builds.done` com `status:"failed"` e a mensagem em `error`. Cancelar mata o processo
> e termina com `status:"cancelled"`. `PODSTACK_PODMAN_CLI` permite substituir o binário em
> testes/dev.

### 4.8 `k8s.*`

| Método | Params | Result |
|---|---|---|
| `k8s.status` | `{}` | `{state, context, apiPort, version}` |
| `k8s.enable` | `{cpus?, memoryBytes?, diskBytes?}` | `JobRef` |
| `k8s.disable` | `{force?}` | `JobRef` |
| `k8s.reset` | `{}` | `JobRef` |
| `k8s.pods.list` | `{namespace?}` | `{items: Pod[]}` |
| `k8s.services.list` | `{namespace?}` | `{items: Service[]}` |
| `k8s.logs.subscribe` | `{namespace, pod, container?}` | `Subscription` |
| `k8s.pod.delete` | `{namespace, name}` | `{}` |
| `k8s.service.delete` | `{namespace, name}` | `{}` |
| `k8s.portforward.start` | `{namespace, pod, localPort, remotePort}` | `{id}` |
| `k8s.portforward.stop` | `{id}` | `{}` |
| `k8s.context.use` | `{context}` | `{}` |

### 4.9 `domains.*` e `proxy.*`

| Método | Params | Result |
|---|---|---|
| `domains.list` | `{}` | `{items: Domain[]}` |
| `domains.add` | `{hostname, targetType, targetId, port?, https?}` | `Domain` |
| `domains.remove` | `{hostname}` | `{removed:true}` |
| `proxy.status` | `{}` | `{httpPort, httpsPort, caInstalled, dnsMode}` |
| `proxy.ca.install` | `{}` | `JobRef` |
| `proxy.ca.remove` | `{}` | `JobRef` |
| `proxy.dns.install` | `{}` | `JobRef` |
| `proxy.dns.remove` | `{}` | `JobRef` |

### 4.10 `settings.*`

| Método | Params | Result |
|---|---|---|
| `settings.get` | `{key?}` | `{values: object}` |
| `settings.set` | `{key, value}` | `{values}` |
| `settings.reset` | `{key?}` | `{values}` |
| `settings.schema` | `{}` | `{schema:[{key,type,default,enum?,min?,max?,restartRequired,description}]}` |

### 4.11 `jobs.*` e `events.*`

| Método | Params | Result |
|---|---|---|
| `jobs.get` | `{id}` | `Job` |
| `jobs.list` | `{state?}` | `{items: Job[]}` |
| `jobs.cancel` | `{id}` | `{}` |
| `events.subscribe` | `{types?}` | `Subscription` |
| `events.unsubscribe` | `{subscriptionId}` | `{}` |

### 4.12 `setup.*`

Implementado para o onboarding/setup de primeira execução. A detecção é read-only; ações
privilegiadas passam pelo helper allowlisted e nunca por shell na GUI.

| Método | Params | Result |
|---|---|---|
| `setup.status` | `{}` | `{completed, completedAt, dockerPolicy, showOnboarding, checks: SetupCheck[], summary, activeJob?}` |
| `setup.check` | `{deep?:bool}` | `{checks: SetupCheck[]}` |
| `setup.apply` | `{action:"install-podman"\|"upgrade-podman"\|"install-qemu"\|"install-components"\|"enable-socket", components?:string[]}` | `{jobId, state}` |
| `setup.removeDocker` | `{confirm:true}` | `{jobId, state}` |
| `setup.cancel` | `{jobId}` | `{cancelled:bool}` |
| `setup.complete` | `{skipped?:bool}` | `{completed, skipped}` |
| `setup.subscribe` | `{jobId}` | `{jobId}` — anexa o cliente ao job e envia `setup.delta` |
| `setup.validate` | `{}` | `{checks: SetupCheck[], ok, warnings}` |

- `SetupCheck`: `{id, status:"ok"|"warn"|"missing"|"optional", version, detail, remediation}`.
- `setup.apply` **não** remove o Docker; `setup.removeDocker` é separado, opt-in e exige `confirm:true`
  (sem isso retorna `PODSTACK_CONFIRMATION_REQUIRED`).
- `setup.complete` grava `setup.completed`/`completedAt`/`lastValidation`; com `skipped:true` grava
  apenas `app.showOnboarding=false`, mantendo o setup pendente.
- Progresso via `setup.delta`/`setup.done`; o job continua no daemon se a janela fechar e pode ser
  retomado com `setup.subscribe`. `setup.done` inclui os checks recalculados.
- Ações que instalam pacotes exigem o helper (`dev.podstack.Helper1` no barramento de sistema) e
  retornam `PODSTACK_HELPER_UNAVAILABLE` quando ele não está instalado.

---

## 5. Notificações de stream

Todas usam `method: "<canal>.delta"` e `params.subscriptionId`.

### 5.1 Logs

```json
{ "jsonrpc":"2.0", "method":"containers.logs.delta",
  "params": { "subscriptionId":"sub-1", "stream":"stdout", "ts":"2026-11-08T12:00:00.123Z", "line":"GET / 200" } }
```

- `stream`: `stdout` | `stderr`.
- Linha sem `\n` final; o cliente adiciona.
- `containers.logs.dropped`: `{subscriptionId, count}` quando backpressure.

### 5.2 Stats

```json
{ "jsonrpc":"2.0", "method":"containers.stats.delta",
  "params": { "subscriptionId":"sub-2", "items":[{
    "id":"abc","cpuPercent":1.2,"memoryBytes":104857600,"memoryLimitBytes":1073741824,
    "networkRxBytes":1000,"networkTxBytes":2000,"blockReadBytes":0,"blockWriteBytes":0,"pids":12
  }] } }
```

### 5.3 Events

```json
{ "jsonrpc":"2.0", "method":"event",
  "params": { "type":"container.start", "id":"abc", "at":"...", "attributes":{} } }
```

### 5.4 Exec

```json
{ "jsonrpc":"2.0", "method":"containers.exec.data", "params": { "sessionId":"s1", "dataBase64":"..." } }
{ "jsonrpc":"2.0", "method":"containers.exec.exit", "params": { "sessionId":"s1", "exitCode":0 } }
```

### 5.5 Stats do sistema (Activity Monitor)

```json
{ "jsonrpc":"2.0", "method":"system.stats.delta",
  "params": {
    "subscriptionId":"sub-3",
    "ts": 1794180316689,
    "host": { "cpuPercent":5.34, "memUsed":8836042752, "memTotal":25035829248,
              "memPercent":35.29, "netRx":89, "netTx":159,
              "diskRead":0, "diskWrite":29383281 },
    "containers": [
      { "id":"abc", "name":"web", "cpuPercent":0.12, "memUsed":1744896,
        "memLimit":25035829248, "memPercent":0.01, "netRx":0, "netTx":0,
        "diskRead":0, "diskWrite":0, "pids":1 }
    ],
    "totals": { "cpuPercent":0.12, "memUsed":1744896, "memLimit":25035829248,
                "memPercent":0.01, "netRx":0, "netTx":0, "pids":1 }
  } }
```

- Taxas de rede/disco e CPU do host são diferenças entre amostras; a primeira amostra vem zerada.
- `intervalSeconds` é limitado a `1..10` (default `2`).
- `system.stats.error` segue o mesmo formato de erro das demais subscriptions.

### 5.6 Stats de machines

```json
{ "jsonrpc":"2.0", "method":"machines.stats.delta",
  "params": {
    "subscriptionId":"sub-4", "id":"machine-1", "ts":1794180316689,
    "cpuPercent":12.5, "memUsed":1073741824, "memTotal":2147483648,
    "memPercent":50.0, "netRx":2048, "netTx":1024,
    "diskRead":0, "diskWrite":0,
    "diskUsed":2500000000, "diskTotal":10000000000, "diskPercent":25.0
  } }
```

- Coleta via SSH one-shot dentro da machine; a primeira amostra de cada subscription vem
  com `cpuPercent`/`netRx`/`netTx`/`diskRead`/`diskWrite` zerados.
- `intervalSeconds` é limitado a `1..10` (default `2`).
- `machines.stats.error` encerra a subscription e carrega o mesmo `ErrorData` das demais.

### 5.7 Job

```json
{ "jsonrpc":"2.0", "method":"job.progress", "params": { "jobId":"j1", "progress":42, "message":"extraindo camada 3/5" } }
{ "jsonrpc":"2.0", "method":"job.done", "params": { "jobId":"j1", "state":"success", "result":{...} } }
```

---

## 6. Códigos de erro

| `error.code` (JSON-RPC) | `data.code` | HTTP-like | Significado |
|---|---|---|---|
| -32700 | `PODSTACK_PARSE` | 400 | JSON inválido |
| -32600 | `PODSTACK_INVALID_REQUEST` | 400 | requisição inválida |
| -32601 | `PODSTACK_METHOD_NOT_FOUND` | 404 | método desconhecido |
| -32602 | `PODSTACK_INVALID_PARAMS` | 400 | parâmetros inválidos |
| -32603 | `PODSTACK_INTERNAL` | 500 | erro interno |
| -32001 | `PODSTACK_ENGINE_UNAVAILABLE` | 503 | Podman indisponível |
| -32002 | `PODSTACK_NOT_FOUND` | 404 | recurso não encontrado |
| -32003 | `PODSTACK_CONFLICT` | 409 | nome/porta/subnet em conflito |
| -32004 | `PODSTACK_PERMISSION_DENIED` | 403 | permissão negada |
| -32005 | `PODSTACK_TIMEOUT` | 504 | timeout |
| -32006 | `PODSTACK_CANCELLED` | 499 | cancelado |
| -32007 | `PODSTACK_VALIDATION` | 422 | validação de domínio |
| -32008 | `PODSTACK_API_MISMATCH` | 426 | versão de API incompatível |
| -32009 | `PODSTACK_HELPER_REQUIRED` | 409 | operação exige helper |
| -32010 | `PODSTACK_UNSUPPORTED` | 501 | recurso não suportado no ambiente |
| -32011 | `PODSTACK_CONSENT_REQUIRED` | 409 | ação destrutiva exige confirmação explícita |

Regras:

- `message` é curta e localizável (pt-BR/en-US).
- `data.detail` é técnico e não localizado.
- `data.remediation` sugere ação.
- `retryable` indica retry seguro.
- `traceId` correlaciona com logs.

---

## 7. Limites e compatibilidade

| Limite | Valor |
|---|---|
| Mensagem | 8 MiB |
| Conexões simultâneas | 32 |
| Subscriptions por conexão | 64 |
| Taxa de requisições | 1.000/s por conexão (burst 2.000) |
| Timeout RPC padrão | 30 s |
| Operações longas | via `JobRef` (sem timeout de RPC) |

- `apiVersion` inteiro; mudanças incompatíveis incrementam.
- Campos novos são opcionais; clientes ignoram desconhecidos.
- Depreciação: mínimo de 2 releases menores.

---

## 8. Exemplos

### 8.1 CLI → daemon

```bash
# usando socat para inspecionar
socat - UNIX-CONNECT:$XDG_RUNTIME_DIR/podstack/control.sock <<'EOF'
{"jsonrpc":"2.0","id":1,"method":"containers.list","params":{"all":true}}
EOF
```

### 8.2 Criar container

```json
{
  "jsonrpc":"2.0","id":2,"method":"containers.create",
  "params": {
    "image":"docker.io/library/nginx:alpine",
    "name":"web",
    "ports":[{"host":8080,"container":80,"protocol":"tcp"}],
    "env":{"NGINX_HOST":"localhost"},
    "flags":{"rm":true}
  }
}
```

### 8.3 Assinar logs

```json
{"jsonrpc":"2.0","id":3,"method":"containers.logs.subscribe","params":{"id":"web","tail":200,"follow":true}}
```

Resposta:

```json
{"jsonrpc":"2.0","id":3,"result":{"subscriptionId":"sub-7"}}
```

Notificações seguem até `containers.logs.unsubscribe`.
