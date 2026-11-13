# Modelo de dados — PodStack

> Entidades de domínio, esquema SQLite e formatos de arquivo. A fonte de verdade de containers/imagens/volumes/redes/pods é o **Podman**; o daemon persiste apenas o que o Podman não conhece (machines, builds, clusters, jobs, settings, domínios).

---

## 1. Convenções

- IDs persistentes: UUIDv7 em texto (ordenáveis por tempo).
- Timestamps: RFC 3339 UTC (`2026-11-08T12:00:00Z`).
- Nomes: RFC 1123 (`[a-z0-9-]`, 1–63 chars), sem `_`/maiúsculas.
- Dinheiro/licença não se aplica; tamanhos em **bytes** (inteiros).
- Enums persistidos como strings estáveis (nunca ordinais).
- `schemaVersion` inteiro no config e `PRAGMA user_version` no SQLite.

---

## 2. Entidades

### 2.1 Container (efêmero, cache)

| Campo | Tipo | Origem |
|---|---|---|
| `id` | string (hex) | Libpod |
| `name` | string | Libpod |
| `image` / `imageId` | string | Libpod |
| `state` | enum | `created`, `running`, `paused`, `stopped`, `exited`, `dead` |
| `status` | string | texto do engine |
| `created` / `startedAt` / `finishedAt` | timestamp | Libpod |
| `ports` | array | Libpod (`Ports`) |
| `labels` | map | Libpod |
| `composeProject` / `composeService` | string? | labels `com.docker.compose.*` |
| `podId` / `podName` | string? | Libpod |
| `networks` | array | Libpod |
| `mounts` | array | Libpod |
| `health` | enum? | healthcheck |
| `exitCode` | int? | Libpod |
| `arch` | string? | imagem/labels |

Não persistido. Cache em memória reconstruído por eventos.

### 2.2 Image

| Campo | Tipo |
|---|---|
| `id` | string |
| `repoTags` / `repoDigests` | array |
| `size` | int64 |
| `created` | timestamp |
| `labels` | map |
| `architecture` / `os` | string |
| `dangling` | bool |
| `inUse` | bool (calculado) |

### 2.3 Volume

| Campo | Tipo |
|---|---|
| `name` | string |
| `driver` | string |
| `mountpoint` | string |
| `createdAt` | timestamp |
| `labels` | map |
| `options` | map |
| `sizeBytes` | int64? (calculado) |
| `inUse` | bool |

### 2.4 Network

| Campo | Tipo |
|---|---|
| `id` / `name` | string |
| `driver` | string (`bridge`) |
| `subnet4` / `gateway4` | string? |
| `subnet6` / `gateway6` | string? |
| `ipv6Enabled` | bool |
| `labels` | map |
| `containers` | array de `{id,name,ipv4,ipv6}` |

### 2.5 Pod

| Campo | Tipo |
|---|---|
| `id` / `name` | string |
| `status` | enum |
| `created` | timestamp |
| `labels` | map |
| `containers` | array de IDs |
| `infraId` | string |

### 2.6 Machine (persistida)

| Campo | Tipo | Observação |
|---|---|---|
| `id` | UUIDv7 | PK |
| `name` | string | único |
| `distro` | string | `ubuntu`, `debian`, ...; `windows` no provisioner Windows |
| `distroVersion` | string? | `24.04`, `noble`, ... |
| `provisioner` | enum | `linux` (default), `windows` |
| `windowsSsh` | bool | instala/habilita OpenSSH no Windows |
| `windowsEdition` | string? | imagem do ISO (default `Windows 11 Pro`) |
| `installPending` | bool | anexa as ISOs de instalação enquanto true |
| `installerIso` / `virtioWinIso` | string? | ISOs durante a instalação (Windows local; virtio-win local ou do cache; limpos no stop) |
| `arch` | enum | `amd64`, `arm64` |
| `cpus` | int | 1..N |
| `memoryBytes` | int64 | ≥ 1 GiB |
| `diskBytes` | int64 | ≥ 1 GiB |
| `diskPath` | string | qcow2 |
| `seedPath` | string? | seed ISO |
| `sshPort` | int | hostfwd |
| `sshUser` | string | default = usuário do host |
| `sshKeyPath` | string | chave privada 0600 |
| `state` | enum | ver §3.2 |
| `isolated` | bool | sem mounts/host |
| `isolateNetwork` | bool | sem rede host |
| `forwardSshAgent` | bool | |
| `httpPort` / `httpsPort` | int? | portas anunciadas |
| `lastBootAt` | timestamp? | |
| `createdAt` / `updatedAt` | timestamp | RFC 3339 UTC com ms |
| `error` | string? | último erro |
| `errorReason` | string? | classificação estável (`guest_unreachable`, `provisioning_timeout`) |
| `guestReady` | bool | canal primário já respondeu |
| `guestOs` | object | `family`, `version`, `edition`, `productName` |
| `guestChannels` | array | `{channel, role, version, lastSeenAt}` |

> `httpPort`/`httpsPort` são colunas reservadas no esquema SQLite; o `meta.json` atual não as grava.
>
> O `provisioner` foi adicionado no M9. Bancos anteriores recebem a coluna por migração
> aditiva (`PRAGMA table_info` + `ALTER TABLE ... DEFAULT 'linux'`), o mesmo valendo para as
> colunas Windows (`windows_ssh`, `windows_edition`, `install_pending`, `installer_iso`,
> `virtio_win_iso`) e de guest (`error_reason`, `guest_ready`, `guest_os`, `guest_channels`;
> `guest_os`/`guest_channels` são JSON compacto). Machines Windows também têm `nvram.fd`
> (NVRAM Secure Boot, `0600`), o diretório `tpm/` (`0700`, arquivos `0600`) e, durante a
> instalação, `autounattend.xml`/`autounattend.iso` (`0600`); todos ficam fora do export
> `.tar.zst`.

### 2.7 MachinePort / MachineMount

`machine_ports`: `id`, `machineId`, `hostPort`, `guestPort`, `protocol`, `exposeLan`, `label`.
`machine_mounts`: `id`, `machineId`, `source`, `target`, `readOnly`, `tag` (virtiofs).
`machine_serial_ports`: `machineId`, `path` (porta serial do host anexada; PK composta).
`machine_usb_devices`: `machineId`, `bus`, `address`, `vendorId`, `productId`, `description`
(PK composta por `machineId`, `bus`, `address`).
`machine_settings`: `machineId`, `audioEnabled` (1/0; default 0).

### 2.8 Snapshot (efêmero, derivado do qcow2)

Não há tabela SQLite nem arquivo de metadados: cada snapshot é interno ao disco qcow2 da
machine. A listagem usa `qemu-img info --output=json` e expõe:

| Campo | Tipo |
|---|---|
| `id` | string (id interno do qcow2) |
| `name` | string (único na machine) |
| `sizeBytes` | int (`vm-state-size`; 0 em snapshot offline) |
| `date` | timestamp RFC 3339 UTC com ms (`date-sec`/`date-nsec`) |

Remover a machine apaga o disco e, junto, todos os snapshots. Export/clone convertem o disco
para um qcow2 autocontido e não preservam snapshots internos.

### 2.9 ComposeProject (efêmero, derivado)

| Campo | Tipo |
|---|---|
| `name` | string (label `com.docker.compose.project`) |
| `workingDir` | string? (label) |
| `configFiles` | array (label) |
| `services` | array de `{name, containerIds, status}` |
| `status` | enum (`running`, `partial`, `stopped`) |

Derivado dos containers; não persistido (ou cache).

### 2.10 Build / BuildStep (persistidos)

`builds`: `id`, `imageRef`, `status` (`queued`, `running`, `success`, `failed`, `cancelled`), `startedAt`, `finishedAt`, `durationMs`, `trigger` (`gui`, `cli`, `compose`), `contextPath`, `dockerfile`, `logPath`, `error`.
`build_steps`: `id`, `buildId`, `index`, `name`, `status`, `startedAt`, `finishedAt`, `logPath`.

### 2.11 Cluster (persistido)

`clusters` (implementado na T5-01): `id`, `name`, `provider` (`k3s-vm`), `state`, `apiPort`, `kubeconfigPath`, `contextName`, `vmMachineId`, `createdAt`, `updatedAt`, `error`.

### 2.12 Domain (persistido para custom domains)

`domains`: `id`, `hostname`, `targetType` (`container`, `service`, `machine`), `targetId`, `port`, `https`, `source` (`label`, `manual`), `createdAt`.

### 2.13 Job / JobEvent

`jobs`: `id`, `type` (`pull`, `build`, `export`, `import`, `machine-create`, `k8s-enable`, `setup`), `state`, `progress` (0..100), `message`, `payload` (JSON), `createdAt`, `updatedAt`, `error`.
`job_events`: `id`, `jobId`, `seq`, `at`, `level`, `message`, `data` (JSON).

### 2.14 Setting

`settings`: `key` (PK), `value` (JSON), `updatedAt`, `schemaVersion`.

### 2.15 Setup (persistido em `config.json`; checks transientes)

| Campo | Tipo | Default | Notas |
|---|---|---|---|
| `completed` | bool | `false` | onboarding concluído |
| `completedAt` | timestamp? | `null` | |
| `dockerPolicy` | enum `keep\|removed` | `keep` | `removed` só após confirmação explícita |
| `components` | string[] | `[]` | `caddy`, `k3s` |
| `lastValidation` | timestamp? | `null` | |

- `SetupCheck` é transiente: `id`, `status` (`ok|warn|missing|optional`), `version`, `detail`, `remediation`.
- Versões e disponibilidade (Podman, QEMU, KVM etc.) vêm dos checks a cada execução; não são cacheadas no `config.json`.
- Jobs de setup são mantidos em memória pelo `SetupManager` com progresso, log e cancelamento; o log não é persistido no v1.
- A remoção do Docker exige `confirm: true` e grava `dockerPolicy = "removed"` apenas após a ação allowlisted.
- `app.showOnboarding=false` registra que o usuário optou por continuar sem concluir o setup.

---

## 3. Máquinas de estado

### 3.1 Container

`created → running → paused → running → stopped → running`; `running → stopped` (stop/kill); qualquer → `removed`.

### 3.2 Machine

`provisioning → running → stopping → stopped → starting → running`; `provisioning → error`; `error → provisioning` (retry); `running|stopped|error → deleting → deleted`.

### 3.3 Cluster

`disabled → provisioning → running → stopping → disabled`; `provisioning → error`; `running → error` (health).

### 3.4 Job

`queued → running → success|failed|cancelled`; `running → cancelling → cancelled`.

---

## 4. Esquema SQLite

```sql
PRAGMA journal_mode = WAL;
PRAGMA foreign_keys = ON;
PRAGMA user_version = 1;

CREATE TABLE machines (
  id TEXT PRIMARY KEY,
  name TEXT NOT NULL UNIQUE,
  distro TEXT NOT NULL,
  distro_version TEXT,
  provisioner TEXT NOT NULL DEFAULT 'linux',
  arch TEXT NOT NULL,
  cpus INTEGER NOT NULL,
  memory_bytes INTEGER NOT NULL,
  disk_bytes INTEGER NOT NULL,
  disk_path TEXT NOT NULL,
  seed_path TEXT,
  ssh_port INTEGER NOT NULL,
  ssh_user TEXT NOT NULL,
  ssh_key_path TEXT NOT NULL,
  state TEXT NOT NULL,
  isolated INTEGER NOT NULL DEFAULT 0,
  isolate_network INTEGER NOT NULL DEFAULT 0,
  forward_ssh_agent INTEGER NOT NULL DEFAULT 0,
  http_port INTEGER,
  https_port INTEGER,
  last_boot_at TEXT,
  created_at TEXT NOT NULL,
  updated_at TEXT NOT NULL,
  error TEXT,
  windows_ssh INTEGER NOT NULL DEFAULT 0,
  windows_edition TEXT,
  install_pending INTEGER NOT NULL DEFAULT 0,
  installer_iso TEXT,
  virtio_win_iso TEXT,
  error_reason TEXT,
  guest_ready INTEGER NOT NULL DEFAULT 0,
  guest_os TEXT,
  guest_channels TEXT
);

CREATE TABLE machine_ports (
  id TEXT PRIMARY KEY,
  machine_id TEXT NOT NULL REFERENCES machines(id) ON DELETE CASCADE,
  host_port INTEGER NOT NULL,
  guest_port INTEGER NOT NULL,
  protocol TEXT NOT NULL DEFAULT 'tcp',
  expose_lan INTEGER NOT NULL DEFAULT 0,
  label TEXT
);

CREATE TABLE machine_mounts (
  id TEXT PRIMARY KEY,
  machine_id TEXT NOT NULL REFERENCES machines(id) ON DELETE CASCADE,
  source TEXT NOT NULL,
  target TEXT NOT NULL,
  read_only INTEGER NOT NULL DEFAULT 0,
  tag TEXT NOT NULL
);

CREATE TABLE machine_serial_ports (
  machine_id TEXT NOT NULL REFERENCES machines(id) ON DELETE CASCADE,
  path TEXT NOT NULL,
  PRIMARY KEY (machine_id, path)
);

CREATE TABLE machine_usb_devices (
  machine_id TEXT NOT NULL REFERENCES machines(id) ON DELETE CASCADE,
  bus INTEGER NOT NULL,
  address INTEGER NOT NULL,
  vendor_id TEXT NOT NULL,
  product_id TEXT NOT NULL,
  description TEXT,
  PRIMARY KEY (machine_id, bus, address)
);

CREATE TABLE machine_settings (
  machine_id TEXT PRIMARY KEY REFERENCES machines(id) ON DELETE CASCADE,
  audio_enabled INTEGER NOT NULL DEFAULT 0
);

CREATE TABLE builds (
  id TEXT PRIMARY KEY,
  image_ref TEXT NOT NULL,
  status TEXT NOT NULL,
  started_at TEXT,
  finished_at TEXT,
  duration_ms INTEGER,
  trigger TEXT NOT NULL,
  context_path TEXT,
  dockerfile TEXT,
  log_path TEXT,
  error TEXT
);

CREATE TABLE build_steps (
  id TEXT PRIMARY KEY,
  build_id TEXT NOT NULL REFERENCES builds(id) ON DELETE CASCADE,
  idx INTEGER NOT NULL,
  name TEXT,
  status TEXT NOT NULL,
  started_at TEXT,
  finished_at TEXT,
  log_path TEXT
);

CREATE TABLE clusters (
  id TEXT PRIMARY KEY,
  name TEXT NOT NULL UNIQUE,
  provider TEXT NOT NULL,
  state TEXT NOT NULL,
  api_port INTEGER,
  kubeconfig_path TEXT,
  context_name TEXT,
  vm_machine_id TEXT, -- referência lógica a machines(id)
  created_at TEXT NOT NULL,
  updated_at TEXT NOT NULL,
  error TEXT
);

CREATE TABLE domains (
  id TEXT PRIMARY KEY,
  hostname TEXT NOT NULL UNIQUE,
  target_type TEXT NOT NULL,
  target_id TEXT NOT NULL,
  port INTEGER,
  https INTEGER NOT NULL DEFAULT 0,
  source TEXT NOT NULL,
  created_at TEXT NOT NULL
);

CREATE TABLE jobs (
  id TEXT PRIMARY KEY,
  type TEXT NOT NULL,
  state TEXT NOT NULL,
  progress INTEGER NOT NULL DEFAULT 0,
  message TEXT,
  payload TEXT,
  created_at TEXT NOT NULL,
  updated_at TEXT NOT NULL,
  error TEXT
);

CREATE TABLE job_events (
  id TEXT PRIMARY KEY,
  job_id TEXT NOT NULL REFERENCES jobs(id) ON DELETE CASCADE,
  seq INTEGER NOT NULL,
  at TEXT NOT NULL,
  level TEXT NOT NULL,
  message TEXT,
  data TEXT
);

CREATE TABLE settings (
  key TEXT PRIMARY KEY,
  value TEXT NOT NULL,
  updated_at TEXT NOT NULL,
  schema_version INTEGER NOT NULL DEFAULT 1
);

CREATE INDEX idx_machines_state ON machines(state);
CREATE INDEX idx_builds_started ON builds(started_at DESC);
CREATE INDEX idx_jobs_state ON jobs(state);
CREATE INDEX idx_domains_hostname ON domains(hostname);
```

---

## 5. Configuração (`config.json`)

```json
{
  "schemaVersion": 1,
  "app": {
    "startAtLogin": false,
    "waitForContainerStop": true,
    "keepRunningInTray": true,
    "theme": "system",
    "language": "pt-BR"
  },
  "engine": {
    "mode": "rootless",
    "socketPath": null
  },
  "docker": {
    "exposePortsToLan": true,
    "enableContainerdImageStore": false
  },
  "network": {
    "https": false,
    "proxy": "auto",
    "proxyExclude": [],
    "subnet4": "192.168.127.0/24",
    "bridge": true
  },
  "machines": {
    "exposePortsToLan": false,
    "forwardPorts": true,
    "default": null
  },
  "kubernetes": {
    "enabled": false,
    "exposeServices": false,
    "kubeconfigUseDomain": true,
    "tlsSan": []
  },
  "ssh": {
    "exposePort": false,
    "forwardAgent": false
  },
  "terminal": {
    "fontFamily": "monospace",
    "fontSize": 12,
    "fontBold": true
  },
  "logs": {
    "fontSize": 12,
    "wrapLines": false,
    "searchMode": "filter",
    "prettyJson": true
  },
  "updates": {
    "mode": "check",
    "channel": "stable"
  },
  "telemetry": {
    "enabled": false,
    "crashReports": false,
    "usage": false,
    "performance": false,
    "errors": false
  }
}
```

- Local: `~/.config/podstack/config.json` (0600).
- Escrita atômica (tmp + rename); backup `.bak` antes de migrar.
- Campos desconhecidos preservados.

---

## 6. `meta.json` por máquina

```json
{
  "id": "0193f2c4-...",
  "name": "dev-ubuntu",
  "distro": "ubuntu",
  "distroVersion": "24.04",
  "arch": "amd64",
  "cpus": 2,
  "memoryBytes": 4294967296,
  "diskBytes": 21474836480,
  "diskPath": "/home/marcos/.local/share/podstack/machines/0193f2c4-.../disk.qcow2",
  "seedPath": "/home/marcos/.local/share/podstack/machines/0193f2c4-.../seed.iso",
  "sshKeyPath": "/home/marcos/.local/share/podstack/machines/0193f2c4-.../id_ed25519",
  "sshPort": 32222,
  "sshUser": "marcos",
  "state": "running",
  "isolated": false,
  "isolateNetwork": false,
  "forwardSshAgent": false,
  "lastBootAt": "2026-11-08T12:05:00.000Z",
  "createdAt": "2026-11-08T12:00:00.000Z",
  "updatedAt": "2026-11-08T12:05:00.000Z",
  "error": "",
  "ports": [
    { "id": "0193f2c5-...", "hostPort": 32222, "guestPort": 22, "protocol": "tcp",
      "exposeLan": false, "label": "ssh" }
  ],
  "mounts": [
    { "id": "0193f2c6-...", "source": "/home/marcos/Projetos", "target": "/mnt/projetos",
      "readOnly": false, "tag": "host" }
  ],
  "serialPorts": ["/dev/ttyUSB0"],
  "usbDevices": [
    { "bus": 1, "address": 4, "vendorId": "0781", "productId": "5567",
      "description": "SanDisk Cruzer Blade" }
  ],
  "audioEnabled": false
}
```

- Arquivo `0600` em diretório `0700`, escrito por `QSaveFile` (rename atômico).
- Timestamps em RFC 3339 UTC com milissegundos.
- Sem senha ou outro segredo: a senha existe apenas no seed cloud-init como hash SHA-512 crypt.
- `meta.json` é a fonte de verdade recuperável; SQLite é índice/cache.

---

## 7. Retenção e limpeza

| Dado | Retenção |
|---|---|
| Logs do daemon | 10 MB × 5 arquivos |
| Logs de build | 30 dias ou 100 builds |
| Jobs concluídos | 7 dias |
| `job_events` | 30 dias |
| Snapshots | até o usuário remover |
| Imagens cloud em cache | LRU, limite configurável (default 20 GB) |

Limpeza executada no start e a cada 24 h; nunca remove dados de máquinas em uso.

---

## 8. Migração

- `schemaVersion`/`user_version` incrementados por migration idempotente.
- Antes de migrar: `cp podstack.db podstack.db.bak-<timestamp>`.
- Falha → restaurar backup e abortar com erro claro.
- Config: merge de defaults + preservação de desconhecidos.
