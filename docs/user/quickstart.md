# Primeiros passos — PodStack 0.1.3

Este guia assume o PodStack instalado e o setup concluído (veja [`install.md`](install.md)). O
objetivo é chegar a um container acessível pelo navegador, criar a primeira machine com SSH e
habilitar o Kubernetes local.

## 1. Conferir o ambiente

```bash
podstack status
podstack doctor
```

`podstack status` deve mostrar `estado: ok` e `engine: running`. `podstack doctor` deve terminar com
exit 0; se falhar, veja [`troubleshooting.md`](troubleshooting.md).

Abra a GUI pelo menu **PodStack** ou com:

```bash
podstack-gui
```

## 2. Criar o primeiro container

Primeiro baixe a imagem, se ela ainda não estiver local:

1. Abra **Images** na barra lateral.
2. Clique em **Pull**, informe `nginx:alpine` e confirme.
3. Aguarde o download terminar.

Agora crie o container:

1. Abra **Containers** na barra lateral.
2. Clique em **Novo container** (ou use `Ctrl+N`).
3. Preencha:
   - **Imagem**: `nginx:alpine`
   - **Nome**: `web`
   - **Portas**: `8080:80`
   - **Env**, **Volumes**, **Workdir** e **Usuário**: opcionais
4. Clique em **Criar e iniciar**.

A criação usa a imagem local; se ela não existir, o Podman retorna um erro. O container aparece na
lista com o estado `running`; o painel de detalhe abre as abas **Visão geral**, **Logs**, **Info**,
**Ambiente**, **Redes**, **Volumes**, **Arquivos**, **Terminal** e **Inspect**.

Pelo CLI, o pull equivalente é:

```bash
podstack images pull nginx:alpine
```

Se você já tem containers criados fora do PodStack, eles aparecem na lista assim que o daemon
reconcilia o estado com o Podman.

## 3. Acessar o container

Com o container `web` publicando `8080:80`, o proxy de domínios encaminha o nome do container para a
primeira porta TCP publicada:

- Índice de containers: `http://localhost:18080/`
- Container pelo nome: `http://web.localhost:18080/`

Teste:

```bash
curl -I http://web.localhost:18080/
```

O nome usado no domínio é o nome do container. A porta na URL é a porta do proxy
(`network.httpPort`, 18080 por padrão), não a porta interna do container. Se o container não publicar
nenhuma porta TCP, o proxy responde 404 com a lista de serviços; publique a porta na criação.

O mesmo mapeamento usa a primeira porta publicada quando há várias. Para forçar uma porta específica,
use a label `dev.podstack.http-port` no container (por exemplo, `3000` ou `8080`).

## 4. Logs, terminal e arquivos

Na GUI, selecione o container e use as abas:

- **Logs** — stream ao vivo; busca e cópia.
- **Terminal** — shell dentro do container.
- **Arquivos** — lista, envio e download de arquivos.
- **Inspect** — JSON completo do Libpod.

Pelo CLI:

```bash
podstack ps
podstack ps --all
podstack inspect web
podstack exec web -- sh -c 'echo olá'
```

O `exec` é uma execução one-shot e imprime a saída do comando; para um shell interativo, use a aba
**Terminal** da GUI. O CLI 0.1.3 não cria, inicia, para nem remove containers; essas ações estão na
GUI. Para automação, use o Podman diretamente ou o contrato RPC do daemon.

## 5. Primeira machine (VM Linux)

### Pela GUI

1. Abra **Machines** e clique em **Nova machine**.
2. Escolha a distro (0.1.3: `ubuntu:24.04`, `debian:12` ou `fedora:44`), o nome, CPUs, memória e
   disco. O usuário em branco usa `podstack`.
3. Clique em **Criar**. O progresso mostra download da imagem cloud, disco qcow2, seed cloud-init,
   QEMU e espera do SSH.

### Pelo CLI

```bash
podstack machine create ubuntu:24.04 dev --cpus 2 --memory 4G --disk 20G
podstack machine list
podstack machine info dev
```

A criação baixa a imagem cloud na primeira vez e pode demorar. O padrão é 2 CPUs, 4 GiB de memória e
20 GiB de disco. Sem `/dev/kvm`, a machine usa TCG e fica bem mais lenta.

Acesso por SSH:

```bash
podstack ssh dev
podstack ssh dev -- uname -a
```

Sem argumento, `podstack ssh` usa a machine default:

```bash
podstack machine default dev
podstack ssh
```

O PodStack gera uma chave ed25519 por machine e grava um bloco gerenciado em `~/.ssh/config` com o
alias `dev.podstack` (quando `ssh.manageConfig` está ativo). O usuário padrão é `podstack`.

Se o SSH falhar, leia o console serial (somente texto; não há VNC):

```bash
podstack machine console dev --no-follow --tail 65536
podstack machine logs dev --tail 200
```

Para parar, iniciar ou remover:

```bash
podstack machine stop dev
podstack machine start dev
podstack machine delete dev
```

`machine delete` remove a machine e o disco, sem confirmação interativa no CLI. Use com cuidado.

## 6. Kubernetes local

O Kubernetes roda em uma VM dedicada com k3s. O `kubectl` precisa estar instalado no host; o PodStack
não instala o cliente.

Pela GUI: abra **Kubernetes** e clique em **Habilitar**. A página mostra Pods e Services, logs por
container, port-forward e as ações **Desabilitar** e **Reset**.

Pelo CLI:

```bash
podstack kube start
podstack kube info
eval "$(podstack kube env)"
kubectl get nodes
```

`podstack kube env` imprime `export KUBECONFIG=...`; o kubeconfig fica em
`~/.local/share/podstack/kubeconfigs/` e não altera `~/.kube/config`.

Para trocar o contexto explicitamente:

```bash
podstack kube context
podstack kube context use podstack-default
```

Atenção: `podstack kube stop` e `podstack kube disable` fazem a mesma coisa nesta versão — removem a
VM, o disco e o kubeconfig. Não há um “parar e manter o cluster” separado. A troca de contexto
automática por projeto ainda não existe (T5-08 parcial).

## 7. Domínios `*.localhost` e HTTPS

### HTTP sem root

`*.localhost` resolve para loopback pelo próprio sistema; o PodStack escuta o proxy em
`127.0.0.1:18080` e encaminha pelo `Host` para o container. Não é preciso editar `/etc/hosts`.

```text
http://web.localhost:18080/     → container web, primeira porta TCP publicada
http://localhost:18080/         → índice de containers que publicam porta
```

### HTTPS com CA local

O HTTPS é opcional e usa o Caddy com uma CA interna:

```bash
podstack config set network.https true
systemctl --user restart podstack.service
```

Depois:

```text
https://web.localhost:18443/
```

O certificado é emitido sob demanda para `*.localhost` e `*.podstack.local` e assinado pela CA local
do PodStack. O navegador não confia nessa CA por padrão; ele mostra um aviso até você importar o
certificado. Para testar por `curl` sem alterar o trust store:

```bash
curl --cacert ~/.local/share/podstack/caddy/storage/pki/authorities/local/root.crt \
  https://web.localhost:18443/
```

O arquivo `root.crt` é a raiz da CA. A importação no navegador/sistema é manual nesta versão; veja
[`troubleshooting.md`](troubleshooting.md#https-e-ca). Sem o Caddy instalado, o daemon registra um
aviso e o HTTP continua funcionando; instale pelo setup ou por
`podstack setup --apply components --caddy`.

### Expor na LAN (opcional)

`network.exposeLan=true` faz o proxy escutar em `0.0.0.0`; o firewall do host precisa ser liberado
separadamente. Para uso local, mantenha o padrão em loopback.

## 8. Bandeja e atalhos

Fechar a janela esconde o PodStack para a bandeja quando o host StatusNotifier está disponível e
`app.keepRunningInTray`/`app.showInTray` estão ativos. O menu da bandeja oferece **Abrir PodStack**,
**Containers** (por projeto Compose, com iniciar/parar/reiniciar/logs), **Machines**, **Kubernetes**,
**Activity Monitor**, **Verificar atualizações…**, **Configurações** e **Sair**. Clique simples abre a
janela; clique do meio abre o Activity Monitor.

Atalhos implementados:

| Atalho | Ação |
|---|---|
| `Ctrl+N` | Novo container (na seção Containers) |
| `Ctrl+,` | Configurações |
| `Ctrl+B` | Mostrar/ocultar a barra lateral |
| `Ctrl+F` | Buscar na seção Containers |
| `F5` | Atualizar |
| `Ctrl+Enter` | Iniciar/parar o container selecionado |
| `Ctrl+Shift+L` | Abrir logs do container |
| `Ctrl+Shift+T` | Abrir terminal do container |
| `Esc` | Desselecionar/fechar |
| `Ctrl+Q` | Sair |

## 9. Próximos passos

- Comandos e flags: [`cli.md`](cli.md).
- Problemas de daemon, Podman, KVM, portas ou HTTPS: [`troubleshooting.md`](troubleshooting.md).
- Limitações do 0.1.3: [`README.md`](README.md#limitações-conhecidas-do-013).
