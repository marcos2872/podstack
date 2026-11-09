# Paridade de CLI — `orb` → `podstack`

> Objetivo: mesma árvore, mesma semântica e mesmos fluxos do CLI do OrbStack, adaptados ao Podman/Linux. Onde o comportamento não faz sentido no Linux, o comando é omitido ou substituído — sempre documentado.

---

## 1. Nomes e semântica

| OrbStack | PodStack | Observação |
|---|---|---|
| `orb` | `podstack` | Comando principal |
| `orbctl` | `podstackctl` | Forma explícita; exige `shell`/`run` |
| `orb <cmd>` | `podstack <cmd>` | Atalho para executar na máquina default |
| `orb` (sem args) | `podstack` (sem args) | Shell na máquina default |
| `orbctl shell` | `podstackctl shell` | Shell explícito |
| `orbctl run` | `podstackctl run` | Execução explícita |
| `orb version` | `podstack version` | |
| `orb help` | `podstack help` | |

**Flags globais:**

| Flag | OrbStack | PodStack | Descrição |
|---|---|---|---|
| `-m`, `--machine` | ✅ | ✅ | Máquina alvo |
| `-u`, `--user` | ✅ | ✅ | Usuário no guest |
| `-w`, `--workdir` | ✅ | ✅ | Diretório de trabalho |
| `-c`, `--context` | ✅ (debug) | ✅ (debug) | Contexto Docker |
| `--format` | parcial | ✅ | `table`, `json`, `quiet` |
| `--quiet` | parcial | ✅ | Só IDs/nomes |
| `--yes` | parcial | ✅ | Não interativo |
| `--version` / `--help` | ✅ | ✅ | |

---

## 2. Tabela de comandos

### 2.1 Máquinas

| `orb` | `podstack` | Flags | Notas Linux |
|---|---|---|---|
| `create DISTRO[:VER] [NAME]` | `machine create DISTRO[:VER] [NAME]` **e** alias `create` | `--arch`, `--cpus`, `--memory`, `--disk`, `--user`, `--set-password`, `-c/--user-data`, `--mount`, `--isolated`, `--isolate-network`, `--forward-ssh-agent` | QEMU/KVM + cloud-init; sem Rosetta |
| `start [MACHINE...]` | `machine start [MACHINE...]` | `--all` | Sem args: máquinas que estavam rodando |
| `stop [MACHINE...]` | `machine stop [MACHINE...]` | `--all`, `--force` | Sem args: para tudo (engine + machines) |
| `restart [MACHINE...]` | `machine restart [MACHINE...]` | `--all` | |
| `delete [MACHINE...]` | `machine delete [MACHINE...]` | `--all`, `--force` | Dados perdidos; confirmação |
| `list` | `machine list` | `--format`, `--quiet`, `--running` | |
| `info M` | `machine info M` | `--format` | |
| `logs [M]` | `machine logs [M]` | `--all` | Console/boot |
| `ssh` | `machine ssh` / `ssh M` | `-u`, `-m` | Escreve `~/.ssh/config` |
| `run [-m M] CMD` | `machine run [-m M] CMD` | `--user`, `--workdir`, `--shell`, `--path` | |
| `pull [-m M] SRC [DST]` | `machine pull` | `-m` | Host→guest? Ver §3 |
| `push [-m M] SRC [DST]` | `machine push` | `-m` | Guest→host? Ver §3 |
| `export M FILE.tar.zst` | `machine export M FILE` | — | Pausa a máquina |
| `import FILE [-n NAME]` | `machine import FILE` | `--name` | Valida manifest/checksum |
| `clone SRC DST` | `machine clone SRC DST` | — | CoW quando possível |
| `rename M NEW` | `machine rename M NEW` | — | Atualiza SSH config |
| `default [M]` | `machine default [M]` | — | Get/set |
| — | `machine distros` | `--json` | Lista imagens cloud pinadas (ubuntu/debian/fedora), checksum e cache local; não baixa |
| `serial list` | `device serial list` | `--format` | |
| `usb list\|info\|attach\|detach` | `device usb ...` | `--machine` | QEMU usb-host |

### 2.2 Serviços e sistema

| `orb` | `podstack` | Flags | Notas |
|---|---|---|---|
| `start docker` / `stop docker` / `restart docker` | `engine start/stop/restart` (alias `docker`) | — | Podman socket/engine |
| `start k8s` / `stop k8s` / `restart k8s` / `delete k8s` | `kube start/stop/restart/disable` | — | Cluster local |
| `status` | `status` | `--format` | Exit 0/1/2 |
| `doctor` | `doctor` | `--fix`, `--quiet` | Checks Linux |
| `reset` | `reset` | `--yes` | Dados Docker + machines |
| `update` | `update` | `--check` | Pacote/assinatura |
| `version` | `version` | `--format` | |
| `report` | `report` | `--include-logs` | Redaction |
| `top` | `top` | `--interval` | TUI |
| `completion` | `completion` | `--shell` | bash/zsh/fish |

### 2.3 Docker/Compose/Builds

| `orb` | `podstack` | Flags | Notas |
|---|---|---|---|
| `docker` | `docker` | — | Encaminha para `podman`/shim |
| `docker migrate` | `docker migrate` | `--all`, `--containers`, `--images`, `--volumes`, `--force` | Importa de Docker/Podman existente |
| `docker volume clone SRC DST` | `volume clone SRC DST` | `--name` | Reflink/fallback |
| `docker volume export V FILE` | `volume export V FILE` | — | `.tar.zst` |
| `docker volume import FILE` | `volume import FILE` | `--name` | |
| — | `volume export/import` (top-level) | — | Conveniência PodStack |
| — | `compose up/down/restart` | `--file`, `--detach`, `--volumes` | Provider Docker Compose |
| — | `build` | `--file`, `--tag`, `--platform`, `--build-arg` | Libpod build |
| `debug CONTAINER` | `debug CONTAINER` | `--context`, `--workdir`, `--fallback`, `--reset` | Debug shell (P2) |

### 2.3.1 Containers (extensões PodStack)

| Docker/OrbStack | `podstack` | Flags | Notas |
|---|---|---|---|
| `docker ps` | `ps` | `--all`, `--running`, `--stopped`, `--format`, `--quiet` | Lista e filtra por estado |
| `docker pause` / `unpause` | `pause` / `unpause` | — | Ciclo de vida |
| `docker kill` | `kill` | `--signal` | Sinal opcional (ex.: `SIGTERM`) |
| `docker inspect` | `inspect` | — | JSON bruto do container |
| `docker exec` | `exec` | `-- <cmd...>` | Exec one-shot via hijack |

> No M1, create/duplicate/rename/batch, logs/stats/files e terminal estão disponíveis na GUI; os equivalentes CLI entram no M2.

### 2.3.2 Catálogo (extensões PodStack)

Comandos próprios para imagens, volumes, redes e pods (o `orb` delega ao Docker CLI; aqui o caminho é nativo):

| Comando | Flags | Notas |
|---|---|---|
| `podstack images [ls]` | `--format` | repo:tag, ID, tamanho, uso |
| `podstack images pull <ref>` | `--platform` | progresso por camada; `Ctrl+C` cancela |
| `podstack images push <id>` | `--destination` | stream de push |
| `podstack images rm <id>` | `--force` | |
| `podstack images prune` | `--all` | |
| `podstack images history <id>` | | |
| `podstack images tag|untag <id> <repo:tag>` | | |
| `podstack images inspect <id>` | | JSON |
| `podstack images export <id> <arquivo>` / `load <arquivo>` | | tar |
| `podstack volumes [ls]` | | nome, driver, tamanho, links |
| `podstack volumes create <nome>` | `--driver` | |
| `podstack volumes rm <nome>` | `--force` | |
| `podstack volumes prune` | | |
| `podstack volumes clone <origem> <destino>` | | reflink/fallback |
| `podstack volumes export|import <nome> <arquivo>` | `--force` | `.tar.zst` |
| `podstack volumes import-host <nome> <dir>` | | copia do host |
| `podstack networks [ls]` | | nome, driver, subnet, containers |
| `podstack networks create <nome>` | `--subnet`, `--gateway`, `--driver`, `--ipv6` | |
| `podstack networks rm <nome>` | `--force` | |
| `podstack networks connect|disconnect <rede> <container>` | `--alias` | |
| `podstack pods [ls]` | | nome, status, containers, ID |
| `podstack pods create <nome>` | | |
| `podstack pods start|stop|restart <id>` | | |
| `podstack pods rm <id>` | `--force` | |

> O `docker`/`podman` continua disponível para paridade; os comandos acima são o caminho nativo do PodStack.

> O `docker`/`podman` continua disponível para paridade; os comandos acima são o caminho nativo do PodStack.

### 2.3.3 Compose e builds (M3)

| Comando | Flags | Notas |
|---|---|---|
| `podstack compose ls` | | Projetos agrupados por labels |
| `podstack compose up <dir>` | | `up -d` via provider; timeout 10 min |
| `podstack compose down <dir>` | `--volumes` | |
| `podstack compose restart|stop|start <dir> [serviço]` | | |
| `podstack compose logs <projeto> [serviço]` | | Stream agregado ou por serviço |
| `podstack build <contexto> -t <tag>` | `-f`, `--build-arg`, `--platform` | Via Libpod |
| `podstack builds [ls|rm <id>]` | | Histórico SQLite |

> O provider Compose é detectado em `docker-compose`, `podman compose` ou `docker compose`; `PODSTACK_COMPOSE_PROVIDER` força um binário.

### 2.3.4 Setup (implementado)

| Comando | Flags | Notas |
|---|---|---|
| `podstack setup` | `--check`, `--status`, `--validate`, `--apply podman\|qemu\|components\|socket`, `--install-*`, `--remove-docker --yes`, `--caddy` | Setup/onboarding; `--check` é read-only; Docker só é removido com `--remove-docker --yes` |

> O fluxo principal instala/atualiza o Podman e **não remove o Docker**; a GUI usa a mesma API `setup.*`.
> `--apply` inicia um job no daemon e imprime o `jobId`; o progresso aparece em `setup.status` e nos
> eventos `setup.delta`/`setup.done`. Instalações exigem o `podstack-helper` + polkit.

### 2.4 Configuração

| `orb` | `podstack` | Flags | Notas |
|---|---|---|---|
| `config show` | `config show` | `--format` | Lista chaves |
| `config get K` | `config get K` | `--format` | |
| `config set K V` | `config set K V` | — | Validação |
| `config add K V` | `config add K V` | — | Listas (proxy exclude) |
| `config remove K V` | `config remove K V` | — | |
| `config reset` | `config reset` | `--yes` | |
| `config docker` | `config engine` | — | Abre `daemon.json`/`containers.conf` |
| `login` / `logout` | `login` / `logout` | `--domain`, `--force` | Opcional (P2) |

### 2.5 Kubernetes

| `orb` | `podstack` | Flags | Notas |
|---|---|---|---|
| `k8s` | `kube info` | — | Instruções |
| `orb start k8s` | `kube start` | — | k3s em VM |
| `orb stop k8s` | `kube stop` | — | |
| `orb delete k8s` | `kube disable` | `--force` | |
| — | `kube env` | `--cluster` | `export KUBECONFIG=...` |
| — | `kube pods/services` | `--namespace` | Atalhos de leitura |

### 2.6 Domínios

| `orb` | `podstack` | Flags | Notas |
|---|---|---|---|
| (labels/settings) | `domain list` | `--format` | |
| — | `domain add HOST --target TYPE:ID --port N` | `--https` | |
| — | `domain remove HOST` | — | |
| — | `domain open HOST` | — | Abre no navegador |

> O OrbStack não documenta `orb domain`; no PodStack é um comando explícito (P1).

### 2.7 Omitidos (macOS-only)

| Comando | Motivo |
|---|---|
| `mac`, `macctl`, `mac link/unlink/notify` | Integração com macOS; sem equivalente |
| `orb ssh` baseado em `user@machine@orb` | Substituído por blocos `~/.ssh/config` + `podstack ssh` |
| `orb start docker` como VM | No Linux o engine é nativo; `engine start` gerencia o serviço |
| `orb update` auto-update Sparkle | Substituído por pacote/assinatura |
| `orb usb` com dispositivos "forwarded" | Linux usa passthrough dedicado |

---

## 3. Semântica de `pull`/`push`

No OrbStack, `orb pull` copia **de Linux para macOS** e `orb push` **de macOS para Linux**. No PodStack, para evitar ambiguidade:

- `podstack machine pull <machine>:<src> <host-dst>` — guest → host.
- `podstack machine push <host-src> <machine>:<dst>` — host → guest.
- Aliases `podstack pull`/`push` mantêm a semântica original quando `-m` é usado.

Ajuda exibe exemplos; nunca infere direção silenciosamente.

---

## 4. Saída

### 4.1 Humana (default)

```
NAME       DISTRO        STATE    CPUS  MEMORY  DISK   SSH
dev        ubuntu:24.04  running  2     4 GiB   20 GiB 127.0.0.1:32222
build      fedora:43     stopped  4     8 GiB   40 GiB -
```

### 4.2 JSON (`--format json`)

```json
{
  "api": 1,
  "items": [
    {
      "id": "0193...",
      "name": "dev",
      "distro": "ubuntu",
      "distroVersion": "24.04",
      "state": "running",
      "cpus": 2,
      "memoryBytes": 4294967296,
      "diskBytes": 21474836480,
      "ssh": { "host": "127.0.0.1", "port": 32222, "user": "marcos" }
    }
  ]
}
```

- Campos estáveis; adições são permitidas.
- Nunca incluir segredos.
- Ordem determinística (nome/ID).

### 4.3 Quiet (`--quiet`)

Apenas IDs ou nomes, um por linha.

---

## 5. Exit codes

| Código | Nome | Quando |
|---|---|---|
| 0 | `OK` | sucesso |
| 1 | `ERROR` | erro genérico |
| 2 | `USAGE` | uso inválido/flag desconhecida |
| 3 | `OUTDATED` | `update --check` com versão antiga |
| 4 | `NOT_FOUND` | recurso inexistente |
| 5 | `CONFLICT` | nome/porta/subnet em conflito |
| 6 | `PERMISSION` | permissão negada |
| 7 | `ENGINE_UNAVAILABLE` | Podman/socket indisponível |
| 8 | `TIMEOUT` | operação excedeu tempo |
| 9 | `CANCELLED` | cancelada pelo usuário |

- Em CI (`sem TTY`), nunca prompt; usar defaults ou falhar com 2.
- `--yes`/`--force` tornam operações destrutivas não interativas.

---

## 6. Exemplos

```bash
# criar máquina
podstack machine create ubuntu:24.04 dev --cpus 2 --memory 4G --disk 20G

# listar e inspecionar
podstack machine list
podstack machine list --format json | jq '.[].name'

# executar comando
podstack machine run -m dev -- uname -a
podstack ssh dev -- htop

# arquivos
podstack machine push ./app.tar dev:/home/marcos/
podstack machine pull dev:/var/log/app.log ./app.log

# backup
podstack machine export dev dev-backup.tar.zst
podstack machine import dev-backup.tar.zst --name dev-restored

# containers
podstack ps --all
podstack start web db
podstack logs -f web
podstack exec web -- sh

# compose
podstack compose up -d --file ./compose.yaml
podstack compose down --volumes

# domínios
podstack domain list
podstack domain add api.localhost --target container:web --port 8080

# diagnóstico
podstack doctor
podstack report --include-logs
```

---

## 7. Completions

- Geradas por `podstack completion bash|zsh|fish`.
- Instalação sugerida:
  - bash: `~/.local/share/bash-completion/completions/podstack`
  - zsh: `~/.zfunc/_podstack` (com `fpath`)
  - fish: `~/.config/fish/completions/podstack.fish`
- Completions dinâmicas para nomes de machines/containers (consulta o daemon com cache curto).

---

## 8. Compatibilidade com aliases

O pacote instala:

- `podstack` (principal)
- `podstackctl` (explícito)
- `pds` (alias curto)
- **Não** instala `orb`/`orbctl` por padrão (evita conflito e confusão de marca). O usuário pode criar alias próprio.

---

## 9. Testes de paridade

- Golden tests: saída humana/JSON para cada comando com fixtures.
- Testes de exit code em erros simulados.
- Testes de semântica em Podman real (rootless/rootful).
- Matriz de flags: toda flag documentada deve ter teste de parsing e efeito.
- Comandos omitidos: teste garante que não existem (evita promessa falsa).
