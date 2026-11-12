# CLI `podstack` — referência (0.1.3)

O pacote instala o CLI `podstack`, o daemon `podstackd` e a GUI `podstack-gui`. O CLI conversa com o
daemon pelo socket Unix do usuário; o daemon sobe sob demanda quando `podstack.socket` está ativo.
O binário do build de desenvolvimento fica em `build/dev/podstack`.

```bash
podstack help
podstack version
```

## Convenções

- `<id|nome>` aceita o ID ou o nome do recurso.
- O CLI 0.1.3 **não** tem flags globais como `--machine` ou `--format`; cada comando tem as suas.
- Saída humana é o padrão. Onde há `--json`, a saída é JSON; alguns comandos incluem `"api": 1` e
  outros devolvem o objeto do RPC. A lista de comandos com JSON está no fim deste documento. Os
  exemplos que usam `jq` exigem o `jq` instalado.
- `--force`/`--yes` só existem nos comandos listados; eles tornam a operação não interativa ou
  ignoram erros, conforme o comando. Não são flags globais.
- O CLI não cria, inicia, para nem remove containers nesta versão: use a GUI. As operações de
  containers disponíveis no CLI são listadas abaixo.

## Diagnóstico e monitoramento

| Comando | O que faz |
|---|---|
| `podstack version` | Mostra `PodStack 0.1.3`. |
| `podstack status` | Estado do daemon e do Podman. Exit 7 quando o daemon não responde; exit 1 quando o status contém erro (por exemplo, engine indisponível). |
| `podstack doctor` | Diagnóstico rápido: socket do daemon e socket do Podman. Exit 1 se algum check falhar. |
| `podstack report [--include-logs] [--json]` | Gera um relatório redigido em `~/.local/state/podstack/reports/` (0600), com caminho e SHA-256. Revise antes de compartilhar. |
| `podstack top [--once] [--interval S] [--json] [--plain]` | Monitora CPU/memória/containers. `--once` espera duas amostras para calcular taxas; sem `--once`, segue até `Ctrl+C`. |
| `podstack update --check [--json] [--channel stable|beta]` | Consulta o manifesto assinado. Não instala nada. Exit 3 se houver versão nova; exit 1 se a checagem estiver indisponível. |

```bash
podstack status
podstack doctor
podstack report --include-logs
podstack top --once --plain
podstack update --check --channel stable
```

## Containers

| Comando | Flags | Notas |
|---|---|---|
| `podstack ps` | `--all`/`-a`, `--running`/`-r`, `--stopped`/`-s`, `--format table\|json`, `--quiet`/`-q` | Por padrão lista containers em execução; `--all` inclui parados. `--quiet` imprime só os nomes. |
| `podstack inspect <id\|nome>` | — | JSON bruto do container. |
| `podstack exec <id\|nome> -- <cmd...>` | — | Execução one-shot; imprime stdout e devolve o exit code do comando do container. Para shell interativo, use a aba **Terminal** da GUI. |
| `podstack pause <id\|nome>` | — | Pausa. |
| `podstack unpause <id\|nome>` | — | Despausa. |
| `podstack kill <id\|nome>` | `--signal S` | Envia um sinal (ex.: `SIGTERM`). |
| `podstack checkpoint <id\|nome>` | `--leave-running`, `--tcp-established`, `--json` | Checkpoint CRIU; depende do host. |
| `podstack restore <id\|nome>` | `--tcp-established`, `--json` | Restaura o checkpoint CRIU. |

```bash
podstack ps --all
podstack ps --format json | jq -r '.items[].name'
podstack inspect web
podstack exec web -- sh -c 'echo olá'
podstack kill web --signal SIGTERM
```

## Imagens

`podstack images` sem subcomando é o mesmo que `images ls`.

| Comando | Flags |
|---|---|
| `podstack images ls` / `list` | — |
| `podstack images pull <imagem>` | `--platform p` |
| `podstack images push <id>` | `--destination <repo>` (obrigatório) |
| `podstack images rm <id>` | `--force` |
| `podstack images prune` | `--all` |
| `podstack images history <id>` | — |
| `podstack images tag <id> <repo:tag>` / `untag <id> <repo:tag>` | — |
| `podstack images inspect <id>` | — |
| `podstack images export <id> <arquivo>` | — |
| `podstack images load <arquivo>` | — |

```bash
podstack images pull nginx:alpine
podstack images
podstack images tag 1b75f92cc711 local/nginx:dev
podstack images export 1b75f92cc711 /tmp/nginx.tar
podstack images load /tmp/nginx.tar
```

## Volumes

| Comando | Flags |
|---|---|
| `podstack volumes ls` / `list` | — |
| `podstack volumes create <nome>` | `--driver driver` |
| `podstack volumes rm <nome>` | `--force` |
| `podstack volumes prune` | — |
| `podstack volumes clone <origem> <destino>` | — |
| `podstack volumes export <nome> <arquivo>` | `--force` |
| `podstack volumes import <nome> <arquivo>` | `--force` |
| `podstack volumes import-host <nome> <diretório>` | — |

`export`/`import` usam `.tar.zst`; `import-host` copia um diretório do host para um volume novo.
Em `export`/`import`, o primeiro argumento é o nome do volume e o segundo é o arquivo.

```bash
podstack volumes create dados
podstack volumes export dados ~/dados.tar.zst
podstack volumes import dados-restaurado ~/dados.tar.zst
```

## Redes

| Comando | Flags |
|---|---|
| `podstack networks ls` / `list` | — |
| `podstack networks create <nome>` | `--subnet CIDR`, `--gateway IP`, `--driver driver`, `--ipv6` |
| `podstack networks rm <nome>` | `--force` |
| `podstack networks connect <rede> <container>` | `--alias a` (repetível) |
| `podstack networks disconnect <rede> <container>` | `--alias a` |

```bash
podstack networks create backend --subnet 10.89.0.0/24
podstack networks connect backend web --alias api
podstack networks ls
```

## Pods

| Comando | Flags |
|---|---|
| `podstack pods ls` / `list` | — |
| `podstack pods create <nome>` | — |
| `podstack pods start <id\|nome>` | — |
| `podstack pods stop <id\|nome>` | — |
| `podstack pods restart <id\|nome>` | — |
| `podstack pods rm <id\|nome>` | `--force` |

```bash
podstack pods create web
podstack pods start web
```

## Compose

O provider é o Docker Compose (`docker-compose`, `podman compose` ou `docker compose`), apontando
para o socket do Podman. `PODSTACK_COMPOSE_PROVIDER` força um binário.

| Comando | Flags | Notas |
|---|---|---|
| `podstack compose ls` / `list` | — | Projetos agrupados por labels. |
| `podstack compose up <diretório> [serviço]` | — | Sempre destaca (`detach`); não há `-d` nem `--file` no CLI 0.1.3. |
| `podstack compose down <diretório> [serviço]` | `--volumes` | Sem `--volumes`, os volumes são preservados. |
| `podstack compose restart <diretório> [serviço]` | — | |
| `podstack compose stop <diretório> [serviço]` | — | |
| `podstack compose start <diretório> [serviço]` | — | |
| `podstack compose logs <projeto> [serviço]` | — | Stream agregado ou por serviço; `Ctrl+C` encerra. |

```bash
podstack compose up ~/projetos/app
podstack compose ls
podstack compose logs app web
podstack compose down --volumes ~/projetos/app
```

## Builds

| Comando | Flags |
|---|---|
| `podstack build <contexto> -t <tag>` | `-f Dockerfile`, `--build-arg K=V` (repetível), `--platform p1,p2` |
| `podstack builds ls` / `list` | — |
| `podstack builds rm <id>` | — |

```bash
podstack build ./app -t local/app:dev --build-arg VERSION=0.1.3
podstack builds
podstack builds rm 01a1c24e-44d9-76df-8db5-3185e34087b7
```

Com uma plataforma, o build usa o Libpod; com duas ou mais, o daemon cria uma manifest list pelo CLI
do Podman. Arquiteturas estrangeiras exigem `binfmt_misc` no host.

## Machines

### Ciclo de vida e informação

| Comando | Flags |
|---|---|
| `podstack machine list` | `--json` |
| `podstack machine info <id\|nome>` | `--json` |
| `podstack machine logs <id\|nome>` | `--tail N`, `--json` |
| `podstack machine console <id\|nome>` | `--tail BYTES`, `--no-follow`, `--json` (só com `--no-follow`) |
| `podstack machine create DISTRO[:VER] NOME` | `--cpus N`, `--memory 4G`, `--disk 20G`, `--user U`, `--user-data ARQUIVO`, `--isolate-network`, `--forward-ssh-agent`, `--isolated` |
| `podstack machine start <id\|nome>` | `--json` |
| `podstack machine stop <id\|nome>` | `--force`, `--json` |
| `podstack machine restart <id\|nome>` | `--json` |
| `podstack machine delete <id\|nome>` | `--force`, `--json` |
| `podstack machine rename <id\|nome> <novo>` | `--json` |
| `podstack machine clone <id\|nome> <novo>` | `--json` |
| `podstack machine update <id\|nome>` | `--cpus N`, `--memory 4G`, `--disk 40G`, `--forward-agent`, `--no-forward-agent`, `--json` |
| `podstack machine default [<id\|nome>]` | `--json` |
| `podstack machine distros` | `--json` |

- Distros disponíveis em 0.1.3: `ubuntu:24.04`, `debian:12` e `fedora:44` (amd64).
- Defaults: 2 CPUs, 4 GiB de memória, 20 GiB de disco, usuário `podstack`.
- `machine create` não tem flags de mount no CLI (T4-07); mounts são configurados na GUI/RPC.
- `machine update` exige ao menos um recurso ou o agent. CPUs/memória valem no próximo boot; o disco
  só cresce e a partição/filesystem do guest não é expandida automaticamente.
- `machine delete` remove a machine e o disco sem confirmação interativa no CLI.
- `machine console` lê o console serial em texto; sem `--no-follow` segue até `Ctrl+C`. Não há VNC.
- `machine export`/`import` e `clone` não preservam snapshots internos do qcow2.

```bash
podstack machine distros
podstack machine create ubuntu:24.04 dev --cpus 2 --memory 4G --disk 20G
podstack machine list --json
podstack machine info dev
podstack machine stop dev
podstack machine start dev
podstack machine update dev --cpus 4 --memory 8G
```

### SSH, arquivos e portas

| Comando | Flags |
|---|---|
| `podstack machine ssh [<id\|nome>]` | `--x11`, `--x11-trusted`, `-- <cmd...>` |
| `podstack ssh [<id\|nome>]` | `--x11`, `--x11-trusted`, `-- <cmd...>` |
| `podstack machine files ls <id\|nome>` | `--path P`, `--json` |
| `podstack machine pull <id\|nome> <remoto> <local>` | — |
| `podstack machine push <id\|nome> <local> <remoto>` | — |
| `podstack machine port ls <id\|nome>` | `--json` |
| `podstack machine port add <id\|nome>` | `--host N`, `--guest N`, `--udp`, `--lan`, `--label L`, `--json` |
| `podstack machine port rm <id\|nome> <portId\|hostPort>` | `--json` |

- `machine pull` copia guest → host; `machine push` copia host → guest (via SSH/SCP).
- `machine port add` reinicia a machine se ela estiver rodando. `--lan` publica em `0.0.0.0` e exige
  liberação do firewall; o padrão é `127.0.0.1`.
- A porta SSH é gerenciada automaticamente e não pode ser removida.

```bash
podstack ssh dev
podstack ssh dev -- uname -a
podstack machine files ls dev --path /var/log
podstack machine pull dev /var/log/app.log ./app.log
podstack machine push dev ./app.tar /home/podstack/
podstack machine port add dev --host 8080 --guest 80 --label web
podstack machine port ls dev
podstack machine port rm dev 8080
```

### Export/import, snapshots e hardware

| Comando | Flags |
|---|---|
| `podstack machine export <id\|nome> <arquivo>` | `--force`, `--json` |
| `podstack machine import <arquivo>` | `--name N`, `--force`, `--json` |
| `podstack machine snapshot ls <id\|nome>` | `--json` |
| `podstack machine snapshot create <id\|nome> <nome>` | `--json` |
| `podstack machine snapshot restore <id\|nome> <nome>` | `--json` |
| `podstack machine snapshot rm <id\|nome> <nome>` | `--json` |
| `podstack machine serial list <id\|nome>` | `--json` |
| `podstack machine serial attach <id\|nome> <porta>` | `--json` |
| `podstack machine serial detach <id\|nome> <porta>` | `--json` |
| `podstack machine usb list <id\|nome>` | `--json` |
| `podstack machine usb attach <id\|nome> BUS:ADDR` | `--json` |
| `podstack machine usb detach <id\|nome> BUS:ADDR` | `--json` |
| `podstack machine audio <id\|nome> on\|off` | `--json` |

- `export` pausa a machine e grava um `.tar.zst` com disco, seed e chaves; use caminho absoluto.
- `import` valida manifesto/checksums e cria uma nova machine; `--name` define o nome e `--force`
  substitui uma machine existente de mesmo nome.
- Restaurar um snapshot **substitui o estado atual do disco**; remover um snapshot é destrutivo. Não
  existe `--force` para restore. A machine é parada e reiniciada automaticamente quando necessário.
- Serial, USB e áudio usam o hardware do host com o seu usuário; anexar reinicia a machine.

```bash
podstack machine snapshot create dev antes-upgrade
podstack machine snapshot ls dev
podstack machine snapshot restore dev antes-upgrade
podstack machine export dev ~/dev-backup.tar.zst
podstack machine import ~/dev-backup.tar.zst --name dev-restaurada
```

## Kubernetes

| Comando | Flags | Notas |
|---|---|---|
| `podstack kube info` | `--cluster NOME`, `--json` | Estado, contexto, API e kubeconfig. |
| `podstack kube env` | `--cluster NOME` | Imprime `export KUBECONFIG=...`. |
| `podstack kube start` | — | Cria a VM e instala o k3s. |
| `podstack kube stop` | `--force` | Remove a VM, o disco e o kubeconfig. |
| `podstack kube disable` | `--force` | Sinônimo de `stop` nesta versão. |
| `podstack kube context` | — | Mostra o `current-context` do kubeconfig. |
| `podstack kube context use <nome>` | — | Troca o contexto explicitamente. |

```bash
podstack kube start
podstack kube info
eval "$(podstack kube env)"
kubectl get nodes
podstack kube context
podstack kube context use podstack-default
```

Não há NodePort/LoadBalancer nativos nem `k8s.podstack.local` em 0.1.3 (T5-07 parcial). A troca de
contexto automática por projeto também não existe (T5-08 parcial).

## Sistema

| Comando | Flags |
|---|---|
| `podstack system df` | `--detailed`, `--json` |
| `podstack system disk` | `--json` |
| `podstack system emulation` | `--json` |

```bash
podstack system df --detailed
podstack system disk --json
podstack system emulation
```

`system df` mostra o uso e a prévia de prune; `system disk` mostra espaço livre e o aviso de disco
baixo; `system emulation` mostra a arquitetura do host e handlers `binfmt`/FEX.

## Configuração

| Comando | Flags |
|---|---|
| `podstack config show` | `--json` |
| `podstack config get <chave>` | `--json` |
| `podstack config set <chave> <valor>` | — |
| `podstack config add <chave> <valor>` | — |
| `podstack config remove <chave> <valor>` | — |
| `podstack config reset [chave]` | `--all`, `--yes` |
| `podstack config schema` | `--json` |

- `config` sem subcomando é o mesmo que `config show`.
- O schema tem 86 chaves; `config schema --json` devolve tipo, default, enum, faixa e
  `restartRequired`.
- `config set` valida tipo/faixa/enum no daemon; valor inválido não é salvo. Se a chave exigir
  reinício, o comando avisa; reinicie o daemon com `systemctl --user restart podstack.service`.
- `config reset --all --yes` restaura as configurações e preserva o bloco `setup.*`.
- Em modo não interativo, `config reset` sem `--yes` retorna exit 2.

```bash
podstack config show
podstack config get network.httpPort
podstack config set network.https true
podstack config set network.httpPort 18080
podstack config add network.proxyExclude .internal
podstack config remove network.proxyExclude .internal
podstack config reset network.https --yes
podstack config reset --all --yes
podstack config schema --json
```

## Setup

| Comando | Flags |
|---|---|
| `podstack setup` | Mostra o status atual (igual a `--status`). |
| `podstack setup --check` | Checks somente leitura; exit 0 mesmo com avisos. |
| `podstack setup --validate` | Validação final; exit 1 se houver pendência obrigatória. |
| `podstack setup --apply podman\|qemu\|components\|socket` | Inicia um job; imprime o `jobId`. |
| `podstack setup --apply components --caddy` | Instala o Caddy como componente opcional. |
| `podstack setup --docker-impact [--json]` | Inventário read-only do Docker. |
| `podstack setup --remove-docker --yes` | Remove o Docker (opt-in, destrutivo, sem migração automática). |

`--apply socket` ativa o `podman.socket` sem helper. As demais ações de instalação/remoção exigem o
`podstack-helper` e autorização polkit. `--remove-docker` sem `--yes` retorna exit 2.
`--install-podman`, `--install-qemu`, `--install-components` e `--enable-socket` são aliases aceitos
para as ações correspondentes.

```bash
podstack setup --check
podstack setup --validate
podstack setup --apply socket
podstack setup --apply podman
podstack setup --docker-impact
```

## JSON, exit codes e automação

Comandos com saída JSON:

- `ps --format json` (ou `--format=json`)
- `checkpoint --json`, `restore --json`
- `machine ... --json` (list, info, logs, console com `--no-follow`, start/stop/restart/delete,
  rename, clone, update, default, distros, port, snapshot, serial, usb, audio, export, import)
- `kube info --json`
- `system df|disk|emulation --json`
- `config show|get|schema --json`
- `top --json`
- `update --check --json`
- `report --json`
- `setup --docker-impact --json`

`images`, `volumes`, `networks`, `pods`, `compose` e `builds` não aceitam `--json` nesta versão;
`images inspect` sempre imprime o JSON bruto.

Exit codes:

| Código | Significado |
|---|---|
| `0` | Sucesso. |
| `1` | Erro genérico; também usado por `doctor`/`setup --validate` com pendência e por `update --check` quando a checagem está indisponível. |
| `2` | Uso inválido, flag desconhecida ou confirmação ausente em modo não interativo. |
| `3` | `update --check` encontrou versão nova. |
| `7` | Daemon ou engine indisponível (`PODSTACK_DAEMON_UNAVAILABLE`/`PODSTACK_ENGINE_UNAVAILABLE`). |

`podstack exec` devolve o exit code do comando executado no container (0–255) quando o comando
chega a rodar; erros do próprio CLI seguem a tabela acima.

Os erros do daemon vêm no formato `{code, message, detail, remediation}`; o CLI imprime a mensagem,
o detalhe e a ação sugerida. Os códigos mais comuns e o que fazer estão em
[`troubleshooting.md`](troubleshooting.md#erros-podstack).
