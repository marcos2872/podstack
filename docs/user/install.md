# Instalação e primeiro start — PodStack 0.1.3

Este guia cobre a instalação do PodStack 0.1.3 em Fedora 43/44 e Ubuntu 24.04/26.04, o
primeiro start, o setup em 7 etapas, o helper privilegiado e a desinstalação. Para o
fluxo de uso depois da instalação, veja [`quickstart.md`](quickstart.md).

## 1. Requisitos

| Item | Requisito | Observação |
|---|---|---|
| Sistema | Fedora 43/44 ou Ubuntu 24.04+ | Wayland ou X11. O RPM é o caminho principal no Fedora; o DEB é construído no Ubuntu 24.04 e roda em 24.04–26.04. |
| Podman | Fedora: 5.8+; Ubuntu: 4.9+ (5.8 recomendado) | O PodStack usa o `podman.socket` do usuário; não reimplementa o runtime. |
| systemd | Fedora: 257+; Ubuntu: 255+ | Socket e serviço de usuário `podstack.socket`/`podstack.service`. |
| Polkit | Agente de autenticação na sessão | Necessário para o `podstack-helper`; em sessão gráfica o prompt aparece normalmente. |
| OpenSSH | Cliente `ssh` e `scp` | Usado pelas machines e pelo encaminhamento de chaves. |
| Qt | Qt 6.4+ em runtime | Fedora 43/44: 6.10/6.11; Ubuntu 24.04: 6.4.2. O DEB leva o qtermwidget 2.0.0 embutido (estático); no Fedora o RPM usa o qtermwidget do sistema. |
| KVM | `/dev/kvm` legível e gravável | Opcional. Sem KVM, as machines usam TCG (muito mais lentas). |
| QEMU/OVMF/virtiofsd | Instalados para machines | O setup pode instalar; no Ubuntu 24.04 usa `ovmf`, `qemu-system-x86`, `qemu-utils` e `virtiofsd`; a 0.1.3 reconhece `OVMF_CODE_4M.fd`. |
| Caddy | Opcional | Necessário para HTTPS em `*.localhost`/`*.podstack.local`. |
| `docker-compose` | Opcional | Provider usado pelos projetos Compose; a engine continua sendo o Podman. |
| k3s no host | Não necessário | O Kubernetes roda em uma VM dedicada criada pelo PodStack. |

Confira o Podman antes de instalar:

```bash
podman --version
podman info
```

`podman info` deve funcionar sem `sudo`. Se o socket rootless não estiver ativo:

```bash
systemctl --user enable --now podman.socket
```

## 2. Instalar por RPM (Fedora)

Com o RPM baixado ou gerado localmente:

```bash
sudo dnf install ./podstack-0.1.3-1.fc44.x86_64.rpm
```

Ajuste `fc44`/`x86_64` para a release e a arquitetura do seu Fedora (por exemplo, `fc43`).

Desde a 0.1.1 o `podstack-helper` faz parte do pacote principal e é habilitado/iniciado pelo
`%post`; não há mais um subpacote separado para instalar.

Se você configurou um repositório do PodStack, o pacote pode ser instalado por nome:

```bash
sudo dnf install podstack
```

O `%post` do pacote habilita `podstack.socket` globalmente para os usuários. O daemon continua
subindo sob demanda, na primeira conexão da GUI ou do CLI.

Para gerar o RPM a partir do código:

```bash
./scripts/build-rpm.sh
# saída: build/rpm/RPMS/
```

## 3. Instalar por DEB (Ubuntu 24.04–26.04)

O `.deb` é construído no **Ubuntu 24.04** (Qt 6.4) e roda em 24.04, 25.10 e 26.04. O qtermwidget
2.0.0 vai embutido no binário (link estático), então não é necessário instalar `qtermwidget6`.
Para gerar o pacote a partir do código, use um ambiente Ubuntu 24.04:

```bash
sudo apt install dpkg-dev cmake ninja-build g++ \
  qt6-base-dev qt6-declarative-dev qt6-wayland-dev qt6-5compat-dev \
  qt6-svg-dev qt6-tools-dev libarchive-dev libcurl4-openssl-dev \
  libcrypt-dev libsqlite3-dev libssl-dev

cmake -S . -B build/ubuntu -G Ninja \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCMAKE_INSTALL_PREFIX=/usr \
  -DPODSTACK_USE_VENDORED_QTERMWIDGET=ON
cmake --build build/ubuntu
PODSTACK_BUILD_DIR=build/ubuntu ./scripts/build-deb.sh
# saída: build/deb/podstack_0.1.3_amd64.deb
```

Instale o arquivo publicado para a sua arquitetura:

```bash
sudo apt install ./podstack_0.1.3_amd64.deb
```

O pacote inclui o binário, as units do helper e os dados do terminal em
`/usr/share/podstack/qtermwidget6`. O `postinst` habilita `podstack.socket` por usuário e o
`podstack-helper.service`; o daemon sobe sob demanda na primeira conexão da GUI/CLI.

## 4. Build a partir do código

O caminho suportado para uso diário é o pacote. Para testar uma versão local:

```bash
# dependências (Fedora); o bloco do README, com openssl-devel para o CMake
sudo dnf install cmake ninja-build gcc-c++ \
  qt6-qtbase-devel qt6-qtdeclarative-devel qt6-qtwayland-devel \
  qtermwidget-devel libarchive-devel libcurl-devel libxcrypt-devel sqlite-devel \
  openssl-devel openssh-clients

# provider Compose (runtime, opcional para projetos Compose)
sudo dnf install docker-compose

# build e testes
cmake --preset dev
cmake --build --preset dev
ctest --preset dev

# daemon + GUI, com build incremental
./scripts/dev.sh

# depois do primeiro build, sem recompilar
PODSTACK_SKIP_BUILD=1 ./scripts/dev.sh
```

O `scripts/dev.sh` usa `$XDG_RUNTIME_DIR/podstack/control.sock`, inicia o daemon se necessário e
reaproveita um daemon já ativo. Para isolar do daemon do sistema:

```bash
PODSTACK_CONTROL_SOCKET=/tmp/podstack-dev.sock PODSTACK_SKIP_BUILD=1 ./scripts/dev.sh
```

O script de desenvolvimento **não instala** o `podstack-helper`, que é um serviço root. Para testar
ações privilegiadas do setup em uma máquina de desenvolvimento:

```bash
./scripts/dev-helper.sh          # pede confirmação e usa sudo
./scripts/dev-helper.sh --yes    # não interativo
./scripts/dev-helper.sh --uninstall
```

O CLI do build fica em `build/dev/podstack`:

```bash
./build/dev/podstack --help
./build/dev/podstack doctor
```

## 5. Primeiro start

Depois da instalação, recarregue as units de usuário e inicie o socket:

```bash
systemctl --user daemon-reload
systemctl --user start podstack.socket
systemctl --user status podstack.socket
```

O estado esperado é `active (listening)`. O `podstack.service` não é habilitado para iniciar no
boot; ele é ativado na primeira conexão ao socket e permanece ativo até o fim da sessão. Para
confirmar que o daemon responde:

```bash
podstack status
```

Saída esperada (os valores de Podman e socket variam):

```text
estado: ok
engine: running
podman: v5.8.9
socket: /run/user/1000/podman/podman.sock
```

Abra a GUI pelo menu de aplicativos (**PodStack**) ou pelo binário:

```bash
podstack-gui
```

O socket de controle fica em `$XDG_RUNTIME_DIR/podstack/control.sock`, com diretório `0700` e socket
`0600`. Não há TCP e o daemon roda com o seu usuário; não é preciso `sudo` para usar containers,
machines ou Kubernetes.

## 6. Setup/onboarding em 7 etapas

Na primeira execução, a GUI abre o assistente e mantém a tela principal bloqueada enquanto os
checks obrigatórios não passam. As etapas são:

1. **Boas-vindas** — explica o que será feito; nada é alterado.
2. **Diagnóstico** — checks somente leitura de Docker, Podman, QEMU/KVM, `/dev/kvm`, virtiofsd,
   systemd de usuário, SQLite, Caddy e k3s.
3. **Podman** — instala/atualiza o Podman e ativa o `podman.socket` rootless. O Docker **não** é
   removido por padrão; a remoção é uma ação separada e opt-in.
4. **QEMU/KVM** — instala QEMU, OVMF, virtiofsd e libvirt opcional. No Ubuntu 24.04 usa `ovmf`,
   `qemu-system-x86`, `qemu-utils` e `virtiofsd`, e reconhece `OVMF_CODE_4M.fd`/`OVMF_VARS_4M.fd`;
   se o helper falhar, a mensagem aparece no log/rodapé do setup. Sem `/dev/kvm`, avisa e permite
   TCG, com desempenho muito inferior.
5. **Componentes** — socket/rootless/SQLite são obrigatórios; Caddy é opcional. O k3s não é
   instalado no host: o cluster roda em uma VM dedicada e é habilitado depois na página Kubernetes.
6. **Validação** — checklist final, separando obrigatórios de opcionais. Os obrigatórios são Podman
   com socket, systemd de usuário, SQLite, QEMU, OVMF e virtiofsd.
7. **Pronto** — resumo e abertura do app; grava `setup.completed`.

O assistente pode ser reexecutado em **Configurações → Executar configuração**. As ações
privilegiadas passam pelo helper e pelo polkit; a GUI nunca executa `sudo` ou shell.

Os mesmos checks podem ser feitos pela CLI:

```bash
podstack setup --check       # somente leitura
podstack setup --validate    # validação final; exit 1 se houver pendência obrigatória
podstack setup               # resumo do estado atual (igual a --status)
```

Instalações e ativações pelo CLI:

```bash
podstack setup --apply socket                 # ativa o podman.socket; não exige helper
podstack setup --apply podman                 # instala/atualiza o Podman; exige helper
podstack setup --apply qemu                   # instala QEMU/OVMF/virtiofsd; exige helper
podstack setup --apply components --caddy     # instala o Caddy; exige helper
```

`--apply` inicia um job no daemon e imprime o `jobId`; o progresso detalhado aparece na GUI e nos
eventos do daemon.

## 7. Helper e polkit

O `podstack-helper` é um serviço de sistema, com API declarativa e allowlist. Ele existe para ações
que precisam de root, não para executar comandos arbitrários:

| Ação | Para que serve |
|---|---|
| `install-packages` / `remove-packages` | Instalar/remover pacotes allowlistados (Podman, QEMU, virtiofsd, Caddy, remoção opt-in do Docker). |
| `bind-ports` | Criar unidades systemd para as portas 80/443. |
| `dns-zone` | Instalar/remover o drop-in do `systemd-resolved` para `*.podstack.local`. |
| `trust-ca` | Confiar/remover a CA local no trust store do sistema. |
| `open-ports` / `close-ports` | Abrir/fechar portas no firewall em modo runtime. |

Verifique o serviço:

```bash
systemctl status podstack-helper.service
journalctl -u podstack-helper.service -n 100 --no-pager
```

Se o helper não estiver instalado, o setup ainda consegue ativar o `podman.socket`; as ações que
instalam pacotes, configuram DNS/CA ou abrem portas falham com `PODSTACK_HELPER_REQUIRED` ou
`PODSTACK_HELPER_UNAVAILABLE`.

O polkit pede autenticação na sessão ativa. Em uma sessão sem agente polkit (por exemplo, SSH sem
sessão gráfica), a autorização falha; rode o setup a partir do desktop.

## 8. Verificar a instalação

| Verificação | Comando | Resultado esperado |
|---|---|---|
| Versão | `podstack version` | `PodStack 0.1.3` |
| Socket de usuário | `systemctl --user status podstack.socket` | `active (listening)` |
| Daemon e engine | `podstack status` | `estado: ok` e `engine: running` |
| Diagnóstico rápido | `podstack doctor` | checks `[ok]`; exit 0 |
| Validação do setup | `podstack setup --validate` | `Validação OK.`; exit 0 |

`podstack doctor` verifica o socket do daemon e o socket do Podman. Se algum check falhar, ele mostra
a ação sugerida e retorna exit 1. Se o daemon não responder, o CLI retorna exit 7; erros de engine
`PODSTACK_ENGINE_UNAVAILABLE` também usam exit 7.

## 9. Desinstalar

Remova os pacotes:

```bash
# RPM
sudo systemctl --global disable podstack.socket
sudo dnf remove podstack-helper podstack

# DEB
sudo systemctl --global disable podstack.socket
sudo apt remove podstack
```

Depois da remoção, recarregue as units de usuário:

```bash
systemctl --user daemon-reload
```

A desinstalação remove binários, units, política polkit e arquivos de sistema. **Os dados do
usuário não são removidos**:

```text
~/.config/podstack/       # configuração (config.json, 0600)
~/.local/share/podstack/  # machines, kubeconfigs, CA/Caddy
~/.local/state/podstack/  # estado, jobs, reports
~/.cache/podstack/        # cache, incluindo imagens cloud
$XDG_RUNTIME_DIR/podstack/  # socket e estado de runtime (some no logout)
```

Os containers, imagens, volumes e redes continuam no storage do Podman; o PodStack não os apaga ao
ser desinstalado. Machines e arquivos exportados também continuam no disco.

Antes de desinstalar, remova as machines pela GUI/CLI se quiser limpar os blocos gerenciados em
`~/.ssh/config`. Se sobrar algum bloco, remova manualmente o trecho entre:

```text
# >>> podstack: <nome>
...
# <<< podstack: <nome>
```

Se você ativou a CA local ou o DNS de `*.podstack.local`, remova os artefatos do sistema:

```bash
# Fedora
sudo rm -f /etc/pki/ca-trust/source/anchors/podstack-local-ca.crt
sudo update-ca-trust

# Debian/Ubuntu
sudo rm -f /usr/local/share/ca-certificates/podstack-local-ca.crt
sudo update-ca-certificates

sudo rm -f /etc/systemd/resolved.conf.d/podstack.conf
sudo systemctl reload systemd-resolved
```

Para apagar também os dados do usuário (operação irreversível; pare a GUI e o daemon antes e faça
backup):

```bash
systemctl --user stop podstack.service
rm -rf ~/.config/podstack ~/.local/share/podstack ~/.local/state/podstack ~/.cache/podstack
```

## 9.1 Atualização com o app aberto

O pacote novo encerra GUI/daemon/helper antigos antes do unpack e o `postinst`/`%post` reinicia o
helper de verdade. Containers e machines continuam rodando; QEMU/virtiofsd ficam em scopes próprios.

- **GUI/CLI (0.1.4+):** use **Instalar atualização** ou `podstack update --install`. O app fecha,
  instala em uma unidade transiente e reabre ao final; falhas restauram a versão instalada e deixam
  `~/.local/state/podstack/updates/state.json` para retomar (`--resume`) ou limpar
  (`--clear-state`).
- **Manual:** `sudo apt install ./podstack_<versão>_amd64.deb` ou `sudo dnf upgrade podstack`
  executa os scripts do pacote; o runner não é encerrado.

Logs do update e do app: `podstack system logs --component update` e
`podstack system logs --component helper`; a GUI tem a página **Logs**.

## 10. Limitações de instalação

- O Flatpak é um cliente: o daemon e o helper precisam estar instalados no host.
- A validação de instalação limpa (T7-07/T7-14) ainda está em andamento; trate o checklist acima
  como a verificação mínima da versão 0.1.4.
- Esta documentação não fixa uma URL de repositório; use o arquivo RPM/DEB gerado localmente ou o
  repositório que você configurou.

Para problemas de instalação, veja [`troubleshooting.md`](troubleshooting.md).
