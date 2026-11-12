# Troubleshooting — PodStack 0.1.1

Comece pelos diagnósticos que não alteram nada:

```bash
podstack status
podstack doctor
systemctl --user status podstack.socket podstack.service
systemctl --user status podman.socket
journalctl --user -u podstack.service -n 200 --no-pager
```

Se precisar abrir um chamado, gere o relatório redigido:

```bash
podstack report --include-logs
```

O arquivo fica em `~/.local/state/podstack/reports/` com permissão `0600`, e o comando imprime o
caminho e o SHA-256. Revise o conteúdo antes de compartilhar; o relatório é redigido, mas pode conter
nomes de containers, caminhos e configuração.

## Daemon e socket

Sintomas:

- `Tempo esgotado aguardando o podstackd.`
- `PODSTACK_DAEMON_UNAVAILABLE`
- `podstack status` retorna exit 7.

Verificações:

```bash
systemctl --user status podstack.socket
systemctl --user status podstack.service
systemctl --user daemon-reload
systemctl --user start podstack.socket
ls -l "$XDG_RUNTIME_DIR/podstack/control.sock"
stat -c '%a %U %G' "$XDG_RUNTIME_DIR/podstack/control.sock"
journalctl --user -u podstack.service -n 200 --no-pager
```

O socket deve ser um Unix socket `0600`, no diretório `0700`, pertencente ao seu usuário. O daemon é
ativado na primeira conexão; não é um serviço que precisa ficar em execução o tempo todo.

Causas comuns:

- O pacote foi instalado ou atualizado e o user manager ainda não recarregou as units:
  `systemctl --user daemon-reload`.
- O socket está inativo: `systemctl --user start podstack.socket`.
- O daemon falhou ao iniciar: leia o journal. Se ele foi atualizado com a GUI/CLI ainda antiga,
  reinicie a sessão ou faça `systemctl --user restart podstack.service`.
- Um `PODSTACK_CONTROL_SOCKET` aponta para outro caminho (útil em desenvolvimento):
  `echo "$PODSTACK_CONTROL_SOCKET"` e use o mesmo valor no cliente.
- Permissão errada no socket ou no diretório: não use `chmod 0666`; o modelo espera o mesmo UID e
  `0600`.

Em desenvolvimento, o daemon pode ser iniciado manualmente para diagnóstico:

```bash
./build/dev/podstackd --socket /tmp/podstack-dev.sock
PODSTACK_CONTROL_SOCKET=/tmp/podstack-dev.sock ./build/dev/podstack status
```

## Podman e engine

Sintomas:

- `PODSTACK_ENGINE_UNAVAILABLE`
- `podstack status` mostra `engine: unavailable` ou `podman: unavailable`.
- Listas de containers vazias mesmo com containers existentes.

Verificações:

```bash
systemctl --user status podman.socket
systemctl --user enable --now podman.socket
podman --version
podman info
ls -l "$XDG_RUNTIME_DIR/podman/podman.sock"
```

O PodStack usa o Podman rootless por padrão. O socket esperado é
`$XDG_RUNTIME_DIR/podman/podman.sock`. Se você usa um caminho diferente, ajuste
`PODSTACK_PODMAN_SOCKET` ou `engine.socketPath`.

```bash
podstack setup --apply socket   # ativa o podman.socket sem helper
podstack status
```

Se o Podman estiver instalado mas desatualizado, atualize-o pelo gerenciador de pacotes. O PodStack
0.1.1 espera Podman 5.8 ou mais novo.

No modo rootful (`engine.mode=rootful`), o socket é `/run/podman/podman.sock` e o seu usuário precisa
ter acesso ao grupo correspondente. Rootful é opt-in explícito; a GUI mostra o badge e o aviso. Se o
socket não existir, volte para rootless ou configure o modo rootful corretamente.

## Helper e polkit

Sintomas:

- `PODSTACK_HELPER_REQUIRED` ou `PODSTACK_HELPER_UNAVAILABLE`.
- O setup não instala Podman/QEMU/Caddy e não configura DNS, CA ou portas 80/443.
- O prompt do polkit não aparece ou aparece “authentication agent not found”.

Verificações:

```bash
systemctl status podstack-helper.service
journalctl -u podstack-helper.service -n 100 --no-pager
pkaction --action-id dev.podstack.helper.install-packages --verbose
```

Instale o subpacote e habilite o serviço:

```bash
sudo dnf install ./podstack-helper-0.1.1-1.fc44.x86_64.rpm
sudo systemctl enable --now podstack-helper.service
```

O helper usa D-Bus de sistema e polkit. Ele precisa de uma sessão ativa com agente de autenticação
(GNOME/KDE/niri com polkit agent). Em SSH sem sessão gráfica, a autorização pode falhar; rode o setup
no desktop. O helper não aceita `Exec`/shell: se uma ação não estiver na allowlist, ela é recusada
por design.

Se o helper não for necessário para o seu caso, você ainda pode ativar o `podman.socket` manualmente
ou por `podstack setup --apply socket`; containers e machines não dependem do helper para operar.

## KVM, QEMU e virtiofsd

Sintomas:

- Criação de machine falha ou fica em estado `error`.
- A machine inicia, mas é muito lenta.
- Erro de virtiofsd ao usar mounts.

Verificações:

```bash
ls -l /dev/kvm
test -r /dev/kvm && test -w /dev/kvm && echo "KVM ok"
groups
qemu-system-x86_64 --version
virtiofsd --version
podstack setup --check
```

Sem `/dev/kvm`, o PodStack usa TCG (`-accel tcg`), que funciona mas é muito mais lento. Para liberar
KVM:

```bash
sudo usermod -aG kvm "$USER"
```

Depois saia e entre novamente na sessão. A virtualização também precisa estar habilitada na BIOS/UEFI
do host. Em uma máquina virtual aninhada, o hipervisor precisa expor VT-x/AMD-V.

Se o virtiofsd estiver em um caminho fora do padrão, defina `PODSTACK_VIRTIOFSD_BINARY` ou instale-o
no path esperado (`/usr/libexec/virtiofsd` é o fallback). O setup verifica QEMU, OVMF e virtiofsd;
se um deles estiver ausente e for obrigatório, a tela principal fica bloqueada até a instalação.

Para diagnosticar uma machine que não sobe:

```bash
podstack machine info dev
podstack machine logs dev --tail 200
podstack machine console dev --no-follow --tail 65536
journalctl --user -u podstack.service -n 200 --no-pager
```

O console é texto do serial; não há VNC. Se o cloud-init falhar, o estado fica `error` e o log mostra
o estágio.

## Machines e SSH

Sintomas:

- `podstack ssh dev` não conecta.
- `ssh dev.podstack` não existe ou usa a chave errada.
- `machine pull`/`push` falham.

Verificações:

```bash
podstack machine list
podstack machine info dev
podstack machine port ls dev
podstack ssh dev -- uname -a
```

A machine precisa estar `running`. O usuário padrão é `podstack`; a porta SSH é publicada em
`127.0.0.1` e aparece em `machine info`/`machine list`. O PodStack gera a chave ed25519 e grava o
bloco gerenciado em `~/.ssh/config` quando `ssh.manageConfig=true`.

Se a machine foi recriada e a chave de host mudou, remova a entrada antiga do `known_hosts` usando a
porta atual:

```bash
ssh-keygen -R "[127.0.0.1]:32222"
```

Se você desativou `ssh.manageConfig`, o alias `dev.podstack` não existe; use `podstack ssh dev` ou
configure o SSH manualmente. Se o SSH estiver exposto na LAN (`ssh.exposePort=true`), libere a porta
no firewall.

### X11 (`--x11`/`--x11-trusted`)

Sintomas: `Warning: No xauth data; using fake authentication data for X11 forwarding`,
`X11 forwarding request failed on channel 0` ou `Error: no DISPLAY environment variable specified`.

- No guest, o pacote `xauth` precisa estar instalado. VMs novas recebem via cloud-init; em VMs
  criadas antes do 0.1.1, instale manualmente:
  `sudo apt-get update && sudo apt-get install -y xauth` (Debian/Ubuntu) ou
  `sudo dnf install -y xauth` (Fedora).
- O `sshd` do guest precisa de `X11Forwarding yes` (o PodStack grava
  `/etc/ssh/sshd_config.d/10-podstack-x11.conf`). Confira com `sudo sshd -T | grep -i x11forwarding`.
- No host, o cliente SSH usa o cookie de `XAUTHORITY`/`~/.Xauthority`. Sem cookie ele avisa
  “No xauth data” e usa dados falsos; dependendo do servidor X isso resulta em
  `X11 connection rejected because of wrong authentication`. Se `~/.Xauthority` não existir,
  crie um cookie para o display atual: `xauth add :0 . "$(mcookie)"`. Teste com
  `podstack ssh <vm> --x11 -- 'echo $DISPLAY; xdpyinfo | head'`.
- No Ubuntu, o Firefox é um **snap** e roda com `HOME=/home/<user>/snap/firefox/common`, onde
  não existe `.Xauthority`. VMs novas recebem `~/.ssh/environment` com
  `XAUTHORITY=/home/<user>/.Xauthority` e `PermitUserEnvironment yes` no sshd. Em VMs antigas,
  rode `XAUTHORITY=$HOME/.Xauthority firefox` ou crie o arquivo manualmente.

O agent forwarding é desligado por padrão. Com `--forward-ssh-agent`/`ForwardAgent yes`, processos no
guest podem usar as chaves carregadas no seu agent; ative só em machines confiáveis.

## Containers, Compose e builds

### Container não aparece ou não cria

```bash
podstack ps --all
podstack status
podman ps -a
```

Se o container foi criado fora do PodStack, ele deve aparecer na reconciliação. Se a imagem não
existir localmente, o create falha: faça o pull antes (**Images → Pull** ou
`podstack images pull <imagem>`). O CLI 0.1.1 não cria containers; use a GUI.

### Compose não sobe

```bash
docker-compose version
podstack compose ls
PODSTACK_COMPOSE_PROVIDER=docker-compose podstack compose up ~/projetos/app
```

O provider precisa estar instalado e apontar para o socket do Podman. Se o erro for
`PODSTACK_COMPOSE_PROVIDER_MISSING`, instale `docker-compose` ou configure
`PODSTACK_COMPOSE_PROVIDER` com o binário disponível. O `compose up` do CLI sempre roda destacado e
recebe o diretório do projeto, não um `-f`.

### Build falha

O `podstack build` transmite os passos e o erro do build. Confira o contexto, o `Dockerfile`, as
`--build-arg` e o histórico:

```bash
podstack builds
podstack build ./app -t local/app:dev -f Dockerfile
```

Para múltiplas plataformas, o host precisa de `binfmt_misc` para as arquiteturas estrangeiras; sem
isso, o build da plataforma adicional falha.

## Portas, proxy e firewall

Sintomas:

- `http://nome.localhost:8080/` retorna 404.
- A porta 8080 ou 8443 está em uso.
- O serviço funciona no host, mas não na LAN.

Verificações:

```bash
ss -ltnp | grep -E ':8080|:8443'
curl -v http://web.localhost:8080/
curl -I http://localhost:8080/
podstack config get network.httpPort
podstack config get network.httpsPort
podstack ps
```

O proxy HTTP escuta em loopback na `network.httpPort` (8080). O HTTPS, quando habilitado, escuta na
`network.httpsPort` (8443). A URL usa a porta do proxy, não a porta interna do container.

- **404**: o container não tem porta TCP publicada, ou o nome não corresponde a um container. Publique
  a porta na criação (o Podman rootless não publica portas depois do create; T6-10).
- **Porta em uso**: escolha outra porta com `podstack config set network.httpPort 18080` e reinicie
  `systemctl --user restart podstack.service`. A URL passa a usar a nova porta.
- **LAN**: `podstack config set network.exposeLan true` e reinicie. O firewall do host precisa ser
  liberado separadamente; o PodStack não abre as portas do proxy automaticamente.

Exemplos de firewall (use o que existir no seu sistema):

```bash
# firewalld (regra runtime; um reload/restart a remove)
sudo firewall-cmd --add-port=8080/tcp
sudo firewall-cmd --add-port=8443/tcp

# ufw
sudo ufw allow 8080/tcp
sudo ufw allow 8443/tcp
```

As portas 80/443 não são ativadas automaticamente pela GUI/CLI em 0.1.1. O helper tem a API de
`BindPorts`, mas a fiação do app ainda não a chama; use 8080/8443. Se `network.bridge=false`, o proxy
HTTP e o DNS ficam desligados.

## HTTPS e CA

Sintomas:

- `https://nome.localhost:8443/` não abre.
- O navegador mostra certificado inválido.
- O daemon registra que o HTTPS está indisponível.

Verificações:

```bash
caddy version
podstack config get network.https
podstack config get network.httpsPort
journalctl --user -u podstack.service -n 100 --no-pager
```

O HTTPS exige o Caddy instalado. Se ele não estiver:

```bash
podstack setup --apply components --caddy
```

Depois de habilitar `network.https=true`, reinicie o daemon:

```bash
podstack config set network.https true
systemctl --user restart podstack.service
```

A CA local fica em:

```text
~/.local/share/podstack/caddy/storage/pki/authorities/local/root.crt
```

Teste sem alterar o trust store:

```bash
curl --cacert ~/.local/share/podstack/caddy/storage/pki/authorities/local/root.crt \
  https://web.localhost:8443/
```

O navegador não confia na CA por padrão. A importação é manual nesta versão. No Fedora, para confiar
no sistema (operação que afeta todas as aplicações; use apenas em desenvolvimento):

```bash
sudo cp ~/.local/share/podstack/caddy/storage/pki/authorities/local/root.crt \
  /etc/pki/ca-trust/source/anchors/podstack-local-ca.crt
sudo update-ca-trust
```

No Debian/Ubuntu:

```bash
sudo cp ~/.local/share/podstack/caddy/storage/pki/authorities/local/root.crt \
  /usr/local/share/ca-certificates/podstack-local-ca.crt
sudo update-ca-certificates
```

Em Firefox, importe o `root.crt` nas autoridades do próprio navegador. Para remover a confiança:

```bash
sudo rm -f /etc/pki/ca-trust/source/anchors/podstack-local-ca.crt
sudo update-ca-trust
```

Se o TLS funcionar mas a resposta for 502, o problema está no backend: confirme que o container
publica a porta e que `http://web.localhost:8080/` responde. Se o certificado não corresponder ao
nome, use um domínio coberto por `*.localhost`/`*.podstack.local` ou ajuste a label de porta/domínio.

## DNS e `*.podstack.local`

`*.localhost` não precisa de configuração: o sistema resolve para loopback. Teste:

```bash
getent hosts web.localhost
curl -I http://web.localhost:8080/
```

`*.podstack.local` usa o DNS userspace do PodStack em `127.0.0.1:5353` e um drop-in do
`systemd-resolved`. Em 0.1.1 a instalação desse drop-in ainda não está ligada à GUI/CLI; sem ele, o
nome não resolve. Verifique se o DNS está escutando:

```bash
ss -lunp | grep 5353
journalctl --user -u podstack.service -n 100 --no-pager
```

Se você precisar de `*.podstack.local`, instale o drop-in manualmente (operação de sistema; revise
antes):

```bash
sudo mkdir -p /etc/systemd/resolved.conf.d
sudo tee /etc/systemd/resolved.conf.d/podstack.conf >/dev/null <<'EOF'
[Resolve]
DNS=127.0.0.1:5353
Domains=~podstack.local
EOF
sudo systemctl reload systemd-resolved
```

Teste com `resolvectl query web.podstack.local` (quando disponível) ou `getent hosts`. Para remover:

```bash
sudo rm -f /etc/systemd/resolved.conf.d/podstack.conf
sudo systemctl reload systemd-resolved
```

Se o Avahi estiver ocupando `0.0.0.0:5353`, o daemon tenta conviver com `SO_REUSEADDR`; se ainda
assim o DNS não responder, o journal mostra o aviso. O DNS só sobe com `network.bridge=true` (padrão).
Para a maioria dos casos, prefira `*.localhost`, que não exige root, DNS ou CA.

## CRIU, checkpoint e cgroup rootless

Sintomas:

- `podstack checkpoint` falha com `PODSTACK_UNSUPPORTED` ou erro do engine.
- `restore` não encontra o checkpoint.
- O container não fica no estado esperado após o restore.

O checkpoint/restore usa CRIU e depende do host. Em rootless, é preciso cgroup v2 com delegação e o
binário `criu` disponível; muitos hosts não permitem o checkpoint rootless.

```bash
criu --version
criu check
stat -fc %T /sys/fs/cgroup
podstack inspect web | head
```

Se o host não oferecer o suporte, use o modo rootful (opt-in) ou trate checkpoint/restore como
indisponível. Não use `kill`/`pause` como substituto de um checkpoint persistente. O `restore` espera
um checkpoint criado para o mesmo container; confira o estado com `podstack ps --all`.

## Updates

Na GUI, a verificação fica em **Configurações → Geral**: o checkbox “Verificar atualizações ao
iniciar” e o botão **Verificar atualizações**. Não existe mais a aba **Updates**; canal, feed e
chave pública são internos (`updates.*`, editáveis pela CLI).

```bash
podstack update --check
podstack update --check --json --channel beta
```

O `update --check` não instala nada. Exit 3 significa que há uma versão nova; exit 1 significa que a
checagem está indisponível. A atualização é feita pelo gerenciador de pacotes (`sudo dnf upgrade
podstack`/`sudo apt upgrade podstack`) ou pelo canal que você usa.

Erros `PODSTACK_UPDATE_SIGNATURE`/`PODSTACK_UPDATE_MANIFEST` indicam feed ou assinatura inválidos.
Não tente contornar a verificação; confira rede, URL do feed e chave pública. Um feed sem chave
configurada retorna `unconfigured` e não aceita atualização.

## Erros PODSTACK

| Código | Significado | O que fazer |
|---|---|---|
| `PODSTACK_DAEMON_UNAVAILABLE` | O CLI não conseguiu conectar ao `podstackd`. | Veja “Daemon e socket”; confira o socket e o journal. |
| `PODSTACK_ENGINE_UNAVAILABLE` | Podman/socket indisponível. | Ative `podman.socket`, confira `podman info` e a versão. |
| `PODSTACK_HELPER_REQUIRED` | A ação exige o helper privilegiado. | Instale/habilite `podstack-helper` e autentique no polkit. |
| `PODSTACK_HELPER_UNAVAILABLE` | O helper não respondeu. | Confira `systemctl status podstack-helper.service` e o journal. |
| `PODSTACK_PERMISSION_DENIED` | Permissão negada no socket, arquivo ou grupo. | Verifique UID, modo `0600` e acesso rootful. |
| `PODSTACK_NOT_FOUND` | Container, machine, arquivo ou recurso não existe. | Confira nome/ID com `ps --all`/`machine list`. |
| `PODSTACK_CONFLICT` | Nome, porta ou subnet em uso. | Escolha outro nome/porta ou remova o conflito. |
| `PODSTACK_VALIDATION` | Valor de configuração inválido. | Consulte `podstack config schema` e corrija o valor. |
| `PODSTACK_INVALID_PARAMS` | Argumentos ou parâmetros inválidos. | Revise a sintaxe e as flags do comando. |
| `PODSTACK_UNSUPPORTED` | Recurso não suportado no ambiente (KVM, CRIU, virtiofsd, Caddy). | Instale/habilite o componente ou use a alternativa documentada. |
| `PODSTACK_TIMEOUT` | A operação excedeu o tempo. | Verifique engine/VM/rede e tente novamente. |
| `PODSTACK_CANCELLED` | O job foi cancelado. | Reexecute quando estiver pronto. |
| `PODSTACK_COMPOSE_PROVIDER_MISSING` | Provider Compose não encontrado. | Instale `docker-compose` ou defina `PODSTACK_COMPOSE_PROVIDER`. |
| `PODSTACK_UPDATE_SIGNATURE`/`PODSTACK_UPDATE_MANIFEST` | Feed de update inválido ou assinatura rejeitada. | Confira rede, URL e chave; não ignore a assinatura. |
| `PODSTACK_IO_ERROR` | Falha de arquivo, SSH/SCP, arquivo compactado ou rede. | Confira caminhos, permissões, espaço em disco e conectividade. |
| `PODSTACK_DB_ERROR` | Falha no SQLite/estado do daemon. | Reinicie o daemon; se persistir, preserve o estado e gere um report. |
| `PODSTACK_CONFIRMATION_REQUIRED` | Ação destrutiva sem confirmação explícita. | Use a GUI ou `--force`/`--yes` quando o comando oferecer. |
| `PODSTACK_CONFIG_WRITE_FAILED` | Não foi possível gravar a configuração. | Confira permissões de `~/.config/podstack` e espaço em disco. |
| `PODSTACK_BUILD_ERROR` | O build falhou. | Leia a saída do `podstack build` e confira contexto/Dockerfile. |
| `PODSTACK_EXEC_FAILED` | O comando no container falhou ao executar. | Confira se o container está rodando e se o comando existe. |

A lista completa e o formato `{code, message, detail, remediation}` estão em
[`../api-rpc.md`](../api-rpc.md).

## Variáveis de ambiente úteis

| Variável | Efeito |
|---|---|
| `PODSTACK_CONTROL_SOCKET` | Usa outro socket de controle (desenvolvimento/teste). |
| `PODSTACK_PODMAN_SOCKET` | Usa outro socket do Podman. |
| `PODSTACK_PODMAN_CLI` | Usa outro binário do Podman para operações que chamam o CLI. |
| `PODSTACK_COMPOSE_PROVIDER` | Força o binário do provider Compose. |
| `PODSTACK_SSH_CONFIG` | Usa outro arquivo de configuração SSH. |
| `PODSTACK_SSH_BINARY` / `PODSTACK_SCP_BINARY` | Usa outros binários de SSH/SCP. |
| `PODSTACK_VIRTIOFSD_BINARY` | Usa outro binário do virtiofsd. |

Essas variáveis são principalmente para diagnóstico e desenvolvimento; para o uso normal, deixe os
defaults.

## Limitações relacionadas

As limitações completas do 0.1.1 estão em [`README.md`](README.md#limitações-conhecidas-do-010).
As mais relevantes para diagnóstico são: console serial sem VNC, export/clone sem snapshots
internos, portas não publicadas não suportadas em rootless (T6-10), T5-07/T5-08 parciais, Flatpak
como cliente de um daemon/helper no host e ausência de fiação automática para CA/`*.podstack.local`
e portas 80/443.
