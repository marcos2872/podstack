# Modelo de dados — PodStack UI Concept

> Shapes usados pela POC e como eles se conectam ao daemon/RPC no app.
> A POC tem **dados mock**; o app não lê JSON: ele fala JSON-RPC com o `podstackd` e o Podman.

---

## 1. Contrato da POC

- Fonte: `poc/ui/app.js` → objeto `data` (containers, images, volumes, networks, compose, builds, pods, machines, devices) e `state` (navegação, seleção, setup).
- Nada é persistido; recarregar a página restaura o mock.
- Os campos marcados **“app”** existem no RPC/`PodmanClient`; os marcados **“mock”** são apenas para a demo (métricas de exemplo, logs/files estáticos, IPs, uptime).
- Em caso de divergência, o contrato real é [`docs/api-rpc.md`](../../docs/api-rpc.md) + [`src/podman/PodmanClient.h`](../../src/podman/PodmanClient.h).

---

## 2. Container

| Campo | Tipo | Exemplo | Origem | Notas |
|---|---|---|---|---|
| `id` | string | `api` / `7d30b5…` | app | nome/ID; a POC usa nomes como IDs |
| `name` | string | `api` | app | `Names[0]` sem `/` |
| `image` | string | `ghcr.io/acme/api:1.18.2` | app | `Image` |
| `state` | enum | `running`, `stopped`, `paused`, `exited` | app | `State`; a POC simplifica para running/stopped |
| `status` | string | `Up 2 hours`, `Exited (0) 12 min` | app | `Status`; no Libpod pode vir vazio → fallback `state` |
| `health` | enum | `healthy`, `starting`, `unhealthy`, `""` | app | derivado de `Status` (`(healthy)` etc.) |
| `ports` | string (POC) / lista (app) | `8080→3000, 443→443` | app | app monta `HostPort→ContainerPort/proto` |
| `project` | string | `acme` | app | label `com.docker.compose.project` |
| `ip` | string | `10.89.0.4` | mock/app | app via inspect/NetworkSettings |
| `created` | string | `há 2 h` / RFC3339 | app | `Created` |
| `command` | string | `node dist/main.js` | mock/app | inspect `Config.Cmd` |
| `env` | string[] | `["NODE_ENV=production"]` | mock/app | inspect `Config.Env` |
| `mounts` | string[] | `["/home/dev/acme/api → /app"]` | mock/app | inspect `Mounts` |
| `cpu` | string | `2.4%` | mock | stats (`cpu_percent`) |
| `mem` | string | `148 MB` | mock | stats (`mem_usage`) |
| `net` | string | `1,2 MB / 840 KB` | mock | stats (`net_io`) |
| `disk` | string | `0 B / 12 MB` | mock | stats (`block_io`) |
| `pids` | int | `24` | mock | stats (`pids`) |
| `files` | `FileEntry[]` | `[{name,size,directory,mode}]` | mock/app | `containers.files.list` |
| `logs` | `[ts,level,msg][]` | `["12:31:04","info","…"]` | mock/app | `containers.logs.subscribe` |

### 2.1 DTO do app (`ContainerInfo`)

```cpp
struct ContainerInfo {
    QString id, name, image, state, status, created, health;
    QStringList ports;
    QMap<QString, QString> labels;
    QJsonObject toJson() const;
};
```

O `Backend` acrescenta `status` ao `ContainerListModel` (role) e mantém os gráficos a partir do stream de stats.

---

## 3. Image

| Campo | Tipo | Exemplo | Origem |
|---|---|---|---|
| `repo` | string | `ghcr.io/acme/api` | app (`RepoTags[0]` sem tag) |
| `tag` | string | `1.18.2` | app |
| `id` | string | `img1234567890` | app (`Id`) |
| `digest` | string | `sha256:9f2c…a1b7` | app (`Digest`) |
| `size` | int (bytes) / string | `182 MB` | app (`Size`) |
| `created` | epoch / string | `há 3 dias` | app (`Created`) |
| `used` | int | `1` | app (`Containers`) |
| `dangling` | bool | `true` | app (`RepoTags` vazio) |
| `labels` | map | `{}` | app |

---

## 4. Volume

| Campo | Tipo | Exemplo | Origem |
|---|---|---|---|
| `name` | string | `acme_pgdata` | app |
| `driver` | string | `local` | app |
| `mountpoint` | string | `/var/lib/containers/storage/volumes/…/_data` | app |
| `size` | int (bytes) | `312 MB` | app (`system.df` → `Size`) |
| `reclaimable` | int | `0` | app (`ReclaimableSize`) |
| `links` | int | `1` | app (`Links`) |
| `created` | string | `2026-…` | app (`CreatedAt`) |
| `labels` | map | `{}` | app |

---

## 5. Network

| Campo | Tipo | Exemplo | Origem |
|---|---|---|---|
| `name` | string | `acme_default` | app |
| `id` | string | `net123` | app |
| `driver` | string | `bridge` | app |
| `subnet` | string | `10.89.0.0/24` | app (`subnets[0].subnet`) |
| `gateway` | string | `10.89.0.1` | app (`subnets[0].gateway`) |
| `containers` | int | `4` | app (inspect `containers` size) |
| `ipv6` | bool | `false` | app |
| `dnsEnabled` | bool | `true` | app |
| `labels` | map | `{}` | app |

---

## 6. Compose

### 6.1 Projeto

| Campo | Tipo | Exemplo | Origem |
|---|---|---|---|
| `name` | string | `acme` | label `com.docker.compose.project` |
| `workingDir` | string | `/home/dev/acme` | label `…project.working_dir` |
| `configFiles` | string[] | `["compose.yaml"]` | label `…project.config_files` / `findConfigFile` |
| `services` | `Service[]` | `api, web, postgres, redis` | labels |
| `status` | enum | `running`, `partial`, `stopped` | agregado dos containers |

### 6.2 Serviço

| Campo | Tipo | Exemplo |
|---|---|---|
| `name` | string | `api` |
| `containerIds` | string[] | `["…"]` |
| `status` | enum | `running`, `stopped`, `partial` |

---

## 7. Build

| Campo | Tipo | Exemplo | Origem |
|---|---|---|---|
| `id` | string | `b-1042` / `build-1794…` | app (`BuildStore`) |
| `imageRef` | string | `ghcr.io/acme/api:1.18.2` | app |
| `status` | enum | `queued`, `running`, `success`, `failed`, `cancelled` | app |
| `startedAt` | ISO | `2026-…` | app |
| `finishedAt` | ISO | `2026-…` | app |
| `durationMs` | int | `42100` | app |
| `trigger` | enum | `gui`, `cli`, `compose` | app |
| `contextPath` | string | `/home/dev/acme/api` | app |
| `dockerfile` | string | `Dockerfile` | app |
| `logPath` | string | `~/.local/state/podstack/builds/…log` | app |
| `error` | string | `npm ERR!…` | app |
| `steps` | `BuildStep[]` | `STEP 1/5` | app (`build_steps`) |

---

## 8. Pod

| Campo | Tipo | Exemplo | Origem |
|---|---|---|---|
| `id` | string | `a1b2c3d4` | app (`Id`) |
| `name` | string | `acme` | app |
| `status` | string | `Running`, `Degraded`, `Created` | app |
| `containers` | int | `4` | app (`Containers` size) |
| `infraId` | string | `…` | app (`InfraId`) |
| `created` | string | `2026-…` | app |
| `ip` | string | `10.88.0.10` | mock/app (inspect) |
| `age` | string | `há 2 h` | mock |

---

## 9. Machine (alvo M4)

| Campo | Tipo | Exemplo | Notas |
|---|---|---|---|
| `id` / `name` | string | `dev` | nome único |
| `distro` | string | `Ubuntu` | Ubuntu/Debian/Fedora |
| `version` | string | `24.04` | |
| `arch` | enum | `arm64`, `amd64` | QEMU |
| `cpus` | int | `6` | |
| `memory` | string / int | `8 GB` / MiB | |
| `disk` | string / int | `64 GB` / bytes | qcow2 esparso |
| `status` | enum | `Running`, `Stopped`, `Provisioning`, `Error` | UI |
| `state` | enum | `running`, `stopped`, `provisioning`, `error` | interno |
| `ip` | string | `192.168.215.2` | user/passt |
| `ssh` | string | `ssh dev` | bloco `~/.ssh/config` |
| `sshPort` | int | `2222` | hostfwd |
| `user` | string | `dev` | cloud-init |
| `created` | string | `há 3 dias` | |
| `uptime` | string | `3d 4h` | |
| `kernel` | string | `6.8.0-45-generic` | guest |
| `qemu` | string | `QEMU 9.0.2` | |
| `cpuPct` / `memPct` / `diskPct` | int | `12`/`39`/`28` | barras de recurso |
| `mounts` | string[] | `/home/dev → /Users/dev` | virtiofs |
| `ports` | string[] | `2222→22` | |
| `logs` | `[ts,msg][]` | boot/console | |

---

## 10. Device (alvo M4)

| Campo | Tipo | Exemplo | Notas |
|---|---|---|---|
| `id` | string | `usb-1` | estável |
| `type` | enum | `USB`, `Serial` | |
| `name` | string | `YubiKey 5C`, `/dev/ttyUSB0` | |
| `vidpid` | string | `1050:0407`, `10c4:ea60` | |
| `status` | enum | `Attached`, `Available`, `Error` | |
| `machine` | string | `dev` / `—` | machine alvo |
| `bus` / `port` | string | `003` / `002` | USB |
| `error` | string? | `device busy` | |

---

## 11. Setup (POC)

| Campo | Tipo | Inicial | Notas |
|---|---|---|---|
| `open` | bool | `false` | janela |
| `step` | int | `0` | 0..6 |
| `busy` | bool | `false` | instalação |
| `progress` | int | `0` | 0..100 |
| `logs` | string[] | `[]` | comandos |
| `docker` | bool | `true` | detectado; coexistência por padrão |
| `dockerPolicy` | `"keep"` \| `"removed"` | `"keep"` | app: decisão persistida; a POC usa só `docker` |
| `podman` | bool | `false` | |
| `podmanVersion` | string | `""` | `5.4.2` / `5.8.9` |
| `podmanLatest` | string | `5.8.9` | |
| `podmanOutdated` | bool | `false` | |
| `qemu` / `kvm` / `virtiofsd` / `libvirt` | bool | `false`/`true`/`false`/`false` | |
| `caddy` / `k3s` / `sqlite` | bool | `false`/`false`/`true` | |
| `socket` / `rootless` | bool | `false`/`false` | |
| `installK3s` | bool | `false` | opcional |
| `scanning` / `validating` / `validated` | bool | `false` | |

Persistência no app: `setup.completed`, `completedAt`, `dockerPolicy`, versões e componentes em Config/SQLite. O Docker é opcional; a remoção é opt-in e não entra no fluxo principal.

---

## 12. Históricos de gráficos

| Série | Onde | Tamanho | Atualização | Escala |
|---|---|---:|---|---|
| `cpuHistory` | Containers → Stats | 30 | 2 s | 0..max (mín. 5) |
| `memHistory` | Containers → Stats | 30 | 2 s | `min*0.94`..`max*1.04` |

No app: manter os históricos no `Backend`/`StatsModel` (por container), limpar ao trocar de container/aba e desassinar o stream ao sair.

---

## 13. Mapeamento RPC

| Tela / dado | Métodos | DTO/Result |
|---|---|---|
| Containers (lista) | `containers.list {all, state, filter}` | `ContainerInfo[]` |
| Containers (ações) | `containers.start/stop/restart/pause/unpause/kill/remove/rename/duplicate` | `{ok}` / `{id}` |
| Container (inspect) | `containers.inspect {id}` | `{id, json}` |
| Logs | `containers.logs.subscribe/unsubscribe` | `containers.logs.delta` |
| Stats | `containers.stats.subscribe/unsubscribe` | `containers.stats.delta` |
| Files | `containers.files.list/download/upload` | `FileEntry[]` / `{path}` |
| Images | `images.list/get/history/pull/push/tag/untag/remove/prune/export/load` | `ImageInfo` / subscriptions |
| Volumes | `volumes.list/create/remove/prune/clone/export/import/importHostPath` | `VolumeInfo` |
| Networks | `networks.list/create/remove/connect/disconnect` | `NetworkInfo` |
| Pods | `pods.list/create/start/stop/restart/remove` | `PodInfo` |
| Compose | `compose.projects.list/up/down/restart/stop/start/logs.subscribe` | `ComposeProject[]` / subscription |
| Builds | `builds.list/get/start/cancel/remove` | `BuildRecord` / `builds.delta/done` |
| Disco | `system.df` | `{json}` |
| Events | `events.subscribe/unsubscribe` | `event` |
| Machines (M4) | `machines.list/get/create/start/stop/restart/delete/rename/clone/export/import/ssh/files/ports/logs/console` | planejado |
| Devices (M4) | `devices.list/attach/detach/scan` | planejado |
| Setup | `setup.status/check/apply/cancel/validate` | proposto (ver ONBOARDING §15) |

---

## 14. Formatação e invariantes

- **Bytes**: `B`, `kB`, `MB`, `GB`, `TB` (decimal) na POC; no app, manter o helper existente (`humanBytes`) e não duplicar.
- **IDs**: exibir 12 caracteres; copiar o completo.
- **Digest**: `sha256:` + 12…4.
- **Portas**: `host→container/proto`; múltiplas separadas por `, `.
- **Datas**: relativas (“há 2 h”) na lista; absolutas no inspect.
- **Duração**: `42.1s`, `1m 08s`; build em ms no RPC.
- **Saúde**: `healthy`/`starting`/`unhealthy`; vazio = sem healthcheck.
- **Compose status**: qualquer running + qualquer stopped = `partial`.
- **Cores de status**: verde running/success; âmbar warning/partial/provisioning; vermelho error/failed/unhealthy; cinza stopped/cancelled.
- **Nunca** exibir segredos (env com `TOKEN`, `PASSWORD`, `SECRET`, `KEY`) sem mascarar.
