# Empacotamento e distribuição — PodStack

> Alvos: **RPM** (Fedora, primário), **DEB** (Debian/Ubuntu) e **Flatpak** (cliente
> desktop). O produto precisa de systemd, polkit, SELinux, `/dev/kvm` e do socket do Podman;
> no Flatpak essas integrações dependem do host (daemon/helper instalados fora do sandbox).

---

## 1. Pacotes

| Pacote | Conteúdo | Dependências |
|---|---|---|
| `podstack` | GUI, CLI, daemon, QML, ícones, `.desktop`, AppStream | Qt 6.4+ (6.10 no Fedora 43+; 6.4 no Ubuntu 24.04), qtermwidget 2.0.0 vendorizado no DEB, libcurl, libarchive, libxcrypt, sqlite, podman ≥ 4.9 (5.8 recomendado), openssh-clients |
| `podstack-helper` | helper root, unit system, polkit, módulo SELinux | systemd, polkit, policycoreutils |
| `podstack-devel` (opcional) | headers/libs para plugins | — |

**Recomenda (não exige):** `qemu-kvm`, `qemu-img`, `cloud-utils` (cloud-localds), `virtiofsd`, `caddy`, `k3s`, `docker-compose` (provider Compose).

---

## 2. Layout de arquivos

```
/usr/bin/podstack
/usr/bin/podstackctl
/usr/bin/pds -> podstack
/usr/libexec/podstack/podstackd
/usr/libexec/podstack/podstack-helper
/usr/lib/systemd/user/podstack.socket
/usr/lib/systemd/user/podstack.service
/usr/lib/systemd/system/podstack-helper.service
/usr/share/dbus-1/system.d/dev.podstack.Helper1.conf
/usr/share/polkit-1/actions/dev.podstack.helper.policy
/usr/share/selinux/packages/podstack.pp
/usr/share/podstack/qml/
/usr/share/icons/hicolor/scalable/apps/dev.podstack.PodStack.svg
/usr/share/icons/hicolor/16x16/apps/dev.podstack.PodStack.png
/usr/share/icons/hicolor/24x24/apps/dev.podstack.PodStack.png
/usr/share/icons/hicolor/32x32/apps/dev.podstack.PodStack.png
/usr/share/icons/hicolor/48x48/apps/dev.podstack.PodStack.png
/usr/share/icons/hicolor/64x64/apps/dev.podstack.PodStack.png
/usr/share/icons/hicolor/128x128/apps/dev.podstack.PodStack.png
/usr/share/icons/hicolor/256x256/apps/dev.podstack.PodStack.png
/usr/share/icons/hicolor/512x512/apps/dev.podstack.PodStack.png
/usr/share/applications/dev.podstack.PodStack.desktop
/usr/share/metainfo/dev.podstack.PodStack.appdata.xml
/usr/share/licenses/podstack/
/usr/lib/sysusers.d/podstack.conf        # se necessário
/usr/lib/tmpfiles.d/podstack.conf        # se necessário
```

Dados do usuário (criados em runtime, nunca pelo RPM):

```
~/.config/podstack/
~/.local/share/podstack/
~/.local/state/podstack/
~/.cache/podstack/
$XDG_RUNTIME_DIR/podstack/
```

---

## 3. systemd (usuário)

### 3.1 `podstack.socket`

```ini
[Unit]
Description=PodStack control socket

[Socket]
ListenStream=%t/podstack/control.sock
SocketMode=0600
DirectoryMode=0700
RemoveOnStop=yes

[Install]
WantedBy=sockets.target
```

### 3.2 `podstack.service`

```ini
[Unit]
Description=PodStack daemon
Requires=podstack.socket
After=podstack.socket

[Service]
Type=notify
ExecStart=/usr/libexec/podstack/podstackd
Restart=on-failure
RestartSec=2
TimeoutStopSec=20
KillMode=process
Environment=QT_LOGGING_RULES=podstack.*=false

# hardening
NoNewPrivileges=yes
PrivateTmp=yes
ProtectSystem=strict
ProtectHome=read-only
ReadWritePaths=%h/.config/podstack %h/.local/share/podstack %h/.local/state/podstack %h/.cache/podstack %t/podstack
RestrictAddressFamilies=AF_UNIX AF_INET AF_INET6
SystemCallFilter=@system-service
LockPersonality=yes
MemoryDenyWriteExecute=yes
```

> Ajustar `ProtectHome`/`ReadWritePaths` para permitir mounts de projetos (ou relaxar para `read-only` + portal).

QEMU e virtiofsd não pertencem ao cgroup do serviço: são iniciados em scopes
transientes (`systemd-run --user --scope`, units `podstack-machine-<id>.scope`
e `podstack-virtiofsd-<hash>.scope`). `KillMode=process` é defesa em
profundidade para eventuais filhos que permaneçam no cgroup; não move
processos de cgroup nem substitui o scope.

### 3.3 Habilitação

```bash
# no %post do RPM
systemctl --global enable podstack.socket
```

O daemon sobe sob demanda na primeira conexão da GUI/CLI.

---

## 4. systemd (sistema) — helper

```ini
[Unit]
Description=PodStack privileged helper
After=dbus.service

[Service]
Type=dbus
BusName=dev.podstack.Helper1
ExecStart=/usr/libexec/podstack/podstack-helper
User=root
PrivateTmp=yes
ProtectHome=yes
ProtectKernelTunables=yes
ProtectControlGroups=yes
```

- D-Bus policy (`share/dbus-1/system.d/dev.podstack.Helper1.conf`) permite que apenas
  `root` possua o nome; chamadas de usuários são autorizadas pelo polkit.
- Ativação sob demanda via `dev.podstack.Helper1`.
- **Sem** `ProtectSystem=strict`: o helper precisa escrever em `/usr` e `/var` para o gerenciador
  de pacotes. A proteção efetiva é a allowlist fechada de pacotes + polkit + auditoria no journal.
- **Sem** `NoNewPrivileges=yes`: o `dnf` precisa da transição SELinux de `unconfined_service_t`
  para `rpm_script_t` ao executar scriptlets de RPM; com NNP o kernel nega `nnp_transition` e a
  instalação falha. A allowlist de pacotes continua sendo a fronteira de segurança.
- O helper nunca recebe `Exec`/shell; cada método monta `dnf`/`apt-get`/`pacman` com argumentos fixos.

### 4.1 Helper em desenvolvimento

`scripts/dev.sh` sobe apenas daemon/GUI do usuário e **não** instala o helper (que é root).
Para testar ações de setup que instalam/removem pacotes em uma máquina de dev:

```bash
cmake --build --preset dev
./scripts/dev-helper.sh          # pede confirmação e usa sudo
./scripts/dev-helper.sh --yes    # não interativo
./scripts/dev-helper.sh --uninstall
```

O script copia o binário do build para `/usr/libexec/podstack`, a unit para
`/usr/lib/systemd/system`, a política polkit e o D-Bus conf; depois habilita/inicia o serviço e
tenta recarregar o `dbus-broker`. Reinicie a sessão/máquina se o system bus ainda não tiver lido a
política. Para o caso comum de **Podman instalado e só `podman.socket` inativo**, não é necessário
helper: o setup usa `enable-socket` (`systemctl --user enable --now podman.socket`).

---

## 5. polkit

`dev.podstack.helper.policy`:

```xml
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE policyconfig PUBLIC
 "-//freedesktop//DTD PolicyKit Policy Configuration 1.0//EN"
 "http://www.freedesktop.org/standards/PolicyKit/1/policyconfig.dtd">
<policyconfig>
  <vendor>PodStack</vendor>
  <action id="dev.podstack.helper.bind-ports">
    <description>Bind privileged ports for PodStack</description>
    <message>Allow PodStack to listen on ports 80 and 443 for local domains?</message>
    <defaults>
      <allow_any>no</allow_any>
      <allow_inactive>no</allow_inactive>
      <allow_active>auth_admin_keep</allow_active>
    </defaults>
  </action>
  <action id="dev.podstack.helper.dns-zone">
    <description>Install local DNS zone for PodStack</description>
    <message>Allow PodStack to configure *.podstack.local domains?</message>
    <defaults>
      <allow_any>no</allow_any>
      <allow_inactive>no</allow_inactive>
      <allow_active>auth_admin_keep</allow_active>
    </defaults>
  </action>
  <action id="dev.podstack.helper.trust-ca">
    <description>Trust PodStack local CA</description>
    <message>Allow PodStack to install its local certificate authority?</message>
    <defaults>
      <allow_any>no</allow_any>
      <allow_inactive>no</allow_inactive>
      <allow_active>auth_admin_keep</allow_active>
    </defaults>
  </action>
</policyconfig>
```

Ações separadas por método; nunca uma ação "root genérica".

---

## 6. SELinux

O módulo mínimo está em [`packaging/selinux/`](../../packaging/selinux):
`podstack.te`, `podstack.fc` e `podstack.if` (`policy_module(podstack, 0.1.3)`,
refpolicy/Fedora).

**Escopo:** apenas o helper privilegiado. O daemon do usuário, os containers, o
Caddy e o `systemd-socket-proxyd` não têm domínio dedicado neste módulo e seguem
a política padrão do sistema.

| Tipo | Uso | Contexto de arquivo |
|---|---|---|
| `podstack_helper_t` | domínio do `podstack-helper` | — |
| `podstack_helper_exec_t` | executável do helper | `/usr/libexec/podstack/podstack-helper` |
| `podstack_var_run_t` | pid/socket/fifo de runtime | `/run/podstack(/.*)?` |

Regras cobertas:

- `init_daemon_domain` + `dbus_system_domain` para o serviço
  `dev.podstack.Helper1`.
- Escrita dos drop-ins do `systemd-resolved` em
  `/etc/systemd/resolved.conf.d` (`systemd_conf_t`), incluindo a criação do
  diretório via `filetrans`.
- Escrita das âncoras de CA em `/etc/pki/ca-trust/source/anchors` (`cert_t`) e
  execução de `update-ca-trust`/`update-ca-certificates`.
- Units do `systemd-socket-proxyd` em `/etc/systemd/system`
  (`systemd_unit_file_t`), `systemctl` e `daemon-reload`.
- D-Bus com systemd (`init_dbus_chat`), systemd-resolved
  (`systemd_chat_resolved`, antigo `systemd_dbus_chat_resolved`) e polkit
  (`policykit_dbus_chat`).
- Leitura do certificado fornecido pelo chamador (`user_home_t`; no Fedora
  também `config_home_t`/`data_home_t` via `optional_policy` do módulo gnome) e
  capacidades `dac_override`/`dac_read_search`.
- `logging_send_syslog_msg` e estado de runtime em `/run/podstack`.
- Interface `podstack_domtrans_helper()` exportada em `podstack.if`.

**Fora do escopo:**

- Portas 80/443: quem faz o bind é o `systemd-socket-proxyd` (socket
  activation) e o Caddy; o helper apenas grava as units e chama `systemctl`.
  Por isso não há `corenet_port()`/`podstack_port_t` no módulo.
- Instalação/remoção de pacotes via `dnf`/`apt`/`pacman` e scriptlets de RPM:
  o helper executa o gerenciador, mas este módulo mínimo não cobre todo o fluxo
  `rpm_t`/`rpm_script_t`.
- Firewall (`firewall-cmd`/`ufw`): a execução do binário é permitida como
  `bin_t`, mas a comunicação D-Bus com o `firewalld` e o fluxo netfilter não
  estão cobertos.
- Containers e Caddy.

> A interface `admin_domain()` citada em rascunhos não existe em refpolicy nem
> em `selinux-policy` Fedora; as permissões são explícitas e mínimas, sem
> `unconfined_domain()`.

### 6.1 Compilar e validar

```bash
# compila o .pp (requer selinux-policy-devel)
make -f /usr/share/selinux/devel/Makefile -C packaging/selinux podstack.pp

# valida em diretório temporário, sem instalar no host
./scripts/check-selinux.sh
```

> `checkmodule` não processa `policy_module()` nem as macros refpolicy; por
> isso o `.te` deve ser compilado pelo Makefile do `selinux-policy-devel`
> (que roda `m4` antes), não diretamente.

O `%build` do RPM compila o módulo **sem** `|| :` (falha quebra o build). O
`%post` do `podstack-helper` carrega com `semodule -i` e roda `restorecon` no
binário; ambos tolerantes a host sem SELinux (`|| :`). O `%preun` remove com
`semodule -r podstack` apenas na remoção final (`$1 -eq 0`).

### 6.2 Verificar em runtime

```bash
ls -Z /usr/libexec/podstack/podstack-helper
ls -Zd /run/podstack
semodule -l | grep podstack
ausearch -m avc -ts recent        # negações do domínio podstack_helper_t
```

Se aparecer AVC, ajuste `podstack.te` e recarregue com `semodule -i`. Use
`audit2allow -w -a` apenas como diagnóstico, não como política final.

> **Ressalva:** módulo mínimo, **não validado em SELinux enforcing no 0.1.3**.
> A validação real depende de um build em Fedora com `selinux-policy-devel`
> (COPR/CI) e de um ciclo de setup com `enforcing` + `ausearch`. Em
> `permissive`, o módulo apenas registra negações.

---

## 7. RPM spec (esqueleto)

O spec real está em [`packaging/rpm/podstack.spec`](../../packaging/rpm/podstack.spec) e o build local em
[`scripts/build-rpm.sh`](../../scripts/build-rpm.sh):

```bash
./scripts/build-rpm.sh
# saída: build/rpm/RPMS/
```

Além do pacote principal, o subpacote **podstack-helper** leva o binário root, a unit system,
a política polkit e o D-Bus conf. O `%post` do pacote principal habilita `podstack.socket`
globalmente (`systemctl --global enable`).

<details>
<summary>Referência do spec</summary>

```spec
Name:           podstack
Version:        0.1.3
Release:        1%{?dist}
Summary:        Fast, light desktop for Podman containers, VMs and Kubernetes
License:        GPL-3.0-or-later
URL:            https://podstack.dev
Source0:        %{name}-%{version}.tar.gz
BuildRequires:  cmake >= 3.24
BuildRequires:  ninja-build
BuildRequires:  gcc-c++
BuildRequires:  qt6-qtbase-devel >= 6.10
BuildRequires:  qt6-qtdeclarative-devel >= 6.10
BuildRequires:  qt6-qtwayland-devel >= 6.10
BuildRequires:  qt6-qt5compat-devel
BuildRequires:  kf6-kstatusnotifieritem-devel
BuildRequires:  qtermwidget-devel
BuildRequires:  libarchive-devel
BuildRequires:  libcurl-devel
BuildRequires:  libxcrypt-devel
BuildRequires:  sqlite-devel
BuildRequires:  selinux-policy-devel
Requires:       podman >= 5.8
Requires:       systemd >= 257
Requires:       qtermwidget
Requires:       qt6-qtsvg
Requires:       openssh-clients

%description
PodStack is a desktop app for Podman: containers, images, volumes, networks,
Compose, Linux machines (QEMU/KVM), Kubernetes and local domains.

%package helper
Summary:        Privileged helper for PodStack
Requires:       podstack = %{version}-%{release}
Requires:       polkit
Requires(post):  policycoreutils
Requires(postun): policycoreutils

%description helper
Minimal root helper for DNS, privileged ports and local CA.

%prep
%autosetup

%build
%cmake -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
%cmake_build
make -f /usr/share/selinux/devel/Makefile -C packaging/selinux podstack.pp

%install
%cmake_install
install -D -m 0644 packaging/selinux/podstack.pp \
    %{buildroot}%{_datadir}/selinux/packages/podstack.pp
# systemd units, polkit, desktop, icons, metainfo

%post
%systemd_user_post podstack.socket podstack.service
%systemd_post podstack-helper.service

%post helper
semodule -i %{_datadir}/selinux/packages/podstack.pp >/dev/null 2>&1 || :
restorecon -R /usr/libexec/podstack/podstack-helper /run/podstack >/dev/null 2>&1 || :

%preun
%systemd_user_preun podstack.socket podstack.service

%preun helper
%systemd_preun podstack-helper.service
if [ "$1" -eq 0 ]; then
    semodule -r podstack >/dev/null 2>&1 || :
fi

%postun
%systemd_user_postun podstack.socket podstack.service
%systemd_postun podstack-helper.service

%files
%license LICENSE NOTICE
%doc README.md docs/
/usr/bin/podstack
/usr/bin/podstackctl
/usr/bin/pds
/usr/libexec/podstack/podstackd
/usr/lib/systemd/user/*
/usr/share/podstack/
/usr/share/applications/dev.podstack.PodStack.desktop
/usr/share/metainfo/dev.podstack.PodStack.appdata.xml
/usr/share/icons/hicolor/scalable/apps/dev.podstack.PodStack.svg
/usr/share/icons/hicolor/*/apps/dev.podstack.PodStack.png

%files helper
/usr/libexec/podstack/podstack-helper
/usr/lib/systemd/system/podstack-helper.service
/usr/share/dbus-1/system.d/dev.podstack.Helper1.conf
/usr/share/polkit-1/actions/dev.podstack.helper.policy
%{_datadir}/selinux/packages/podstack.pp
```

Build local:

```bash
fedpkg local          # COPR
# ou
rpmbuild -ba podstack.spec
```

</details>

---

## 8. DEB (Ubuntu 24.04–26.04)

- Metadados em [`packaging/deb/control`](../../packaging/deb/control); build local em Ubuntu 24.04:
  ```bash
  cmake -S . -B build/ubuntu -G Ninja \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DCMAKE_INSTALL_PREFIX=/usr \
    -DPODSTACK_USE_VENDORED_QTERMWIDGET=ON
  cmake --build build/ubuntu
  PODSTACK_BUILD_DIR=build/ubuntu ./scripts/build-deb.sh
  # saída: build/deb/podstack_<versão>_<arch>.deb
  ```
- O `.deb` é construído no **Ubuntu 24.04** (Qt 6.4.2) e roda em 24.04, 25.10 e 26.04; o RPM
  continua em Fedora 44 (Qt 6.10). O script usa `cmake --install` em uma árvore staging com
  `DESTDIR`, `--strip` e `dpkg-deb --root-owner-group`; requer `dpkg-dev`.
- O qtermwidget **2.0.0** é vendorizado em [`third_party/qtermwidget`](../../third_party/qtermwidget)
  e linkado estaticamente no `podstack-gui`; não há `libqtermwidget6.so` em runtime. Os color
  schemes/keytabs vão para `/usr/share/podstack/qtermwidget6`, sem colidir com o
  `libqtermwidget6-2` do Ubuntu 26.04.
- `Depends`: Podman ≥ 4.9 (5.8 recomendado), Qt 6 (base/declarative/widgets/quick), com
  alternativas `t64` para o 24.04 e sem `t64` para o 26.04; `libqt6sql6-sqlite`, `qt6-qpa-plugins`,
  `qt6-wayland`, módulos QML (`qml6-module-qtquick*`, `qml6-module-qtqml*`,
  `qml6-module-qt5compat-graphicaleffects`), `libcurl4t64` + `libcurl3t64-gnutls`,
  `libarchive13t64`, `libssl3t64`, `libcrypt1` e `procps` (pkill/pgrep do `preinst`). Recomenda
  QEMU/KVM, virtiofsd, Caddy e o provider Compose.
- Instalação: `sudo apt install ./podstack_<versão>_amd64.deb`. O binário exige glibc 2.39
  (Ubuntu 24.04+); Debian 12 não é suportado por este pacote.
- O `postinst` recarrega as units, habilita `podstack.socket` por usuário
  (`systemctl --global enable`), remove a máscara runtime do socket e **reinicia** o
  `podstack-helper.service` (não apenas `enable --now`). O `preinst` encerra GUI/daemon/helper
  antigos antes do unpack; o `postrm` restaura helper/socket nos casos `abort-upgrade`/`abort-install`.
  O runner `podstack update --install --runner` nunca é encerrado.
- Para publicar em repositório APT, gerar `Packages`/`Release` assinados no CI (não incluído).

---

## 9. Flatpak

- Manifest em [`packaging/flatpak/dev.podstack.PodStack.yml`](../../packaging/flatpak/dev.podstack.PodStack.yml);
  build:
  ```bash
  ./scripts/build-flatpak.sh
  # saída: build/flatpak/repo
  ```
- Runtime `org.kde.Platform` 6.10 + SDK, com `qtermwidget` compilado como módulo.
- Permissões: Wayland/X11, DRI, `--device=all` (para `/dev/kvm`), sockets do Podman e do
  PodStack, state/config/cache e notificações/StatusNotifier.
- **Limitações:** o helper privilegiado e as units systemd vivem **fora** do sandbox; o
  Flatpak funciona como cliente de um daemon/helper já instalados no host. O daemon não é
  iniciado pelo Flatpak; a conexão usa `xdg-run/podstack`. Documentar no AppStream/README.
- Requer `flatpak-builder` e o runtime; não é o canal primário.

## 10. AppImage

- Objetivo: demo e testes em distros sem RPM/DEB.
- Empacotar GUI + CLI + daemon; **sem** helper/systemd units (limitações documentadas).
- QML modules e platform plugins no AppDir.
- `linuxdeploy` + `appimagetool`.
- Funciona com Podman do host via socket do usuário.

---

## 11. Atualizações

- RPM: `dnf upgrade podstack` (recomendado).
- AppImage: atualizador próprio com assinatura Ed25519 (opt-in).
- Canal beta: COPR separado (`podstack-beta`).
- Verificação de versão: `podstack update --check` (exit 3 se desatualizado).
- Nunca auto-update silencioso em headless/CI.
- **Upgrade com o app aberto:** o `preinst` (Debian) e o `%pre` (RPM) percorrem os usuários
  logados, aplicam `mask --runtime podstack.socket`, param socket/service, encerram a GUI com
  SIGTERM (SIGKILL após 3 s) e o daemon (systemd ou `pkill -x podstackd`), conferem o helper e
  abortam com trap de limpeza se algo não parar. O `postinst`/`%post` reinicia o helper e remove a
  máscara; `postrm`/`%posttrans` restauram nos abortos. O `preinst` não toca em Podman, containers,
  machines nem no runner `podstack`.
- **SIGTERM na GUI:** a GUI instala um handler de SIGTERM/SIGINT via self-pipe +
  `QSocketNotifier`; ao receber o sinal, salva o `UiState` (geometria/seção/abas) e encerra, para a
  janela reabrir no mesmo lugar.
- **Health check:** o `%posttrans` valida `systemctl is-active podstack-helper.service` e
  `podstack-helper --version`, além do `unmask --runtime` por usuário; falha só retorna não-zero
  quando não consegue restaurar.
- Feed oficial: `podstack-update.json` anexado à release do GitHub + `podstack-update.json.minisig`
  (Ed25519/minisign). O manifesto assinado contém os canais `stable`/`beta` com versão, notas,
  `releaseUrl` e artefatos (`arch`, `format`, `url`, `sha256`).

### 11.1 Pipeline de release (`release.yml`)

O workflow [`.github/workflows/release.yml`](../../.github/workflows/release.yml) roda em
push de tag `v*` e em `workflow_dispatch`, em três jobs:

1. `rpm` (Fedora 44): valida o AppStream, configura/compila o preset `release`, roda
   `ctest`, gera o RPM (`scripts/build-rpm.sh`), assina os RPMs com GPG (opcional) e gera
   o SBOM CycloneDX (`scripts/generate-sbom.sh`);
2. `deb` (Ubuntu 24.04): valida o AppStream, configura com
   `-DCMAKE_INSTALL_PREFIX=/usr -DPODSTACK_USE_VENDORED_QTERMWIDGET=ON`, compila, instala
   os dados de runtime do qtermwidget, roda `ctest` e empacota o DEB
   (`scripts/build-deb.sh`);
3. `publish`: baixa os artefatos dos dois jobs, gera `SHA256SUMS` dos `.rpm`/`.deb`, assina
   com GPG (opcional), extrai as notas da seção da versão no
   [`CHANGELOG.md`](../../CHANGELOG.md) e gera `podstack-update.json`
   (`scripts/generate-update-manifest.sh`) com a URL base das releases do GitHub e o canal
   de `PODSTACK_UPDATE_CHANNEL` (variável do repositório), assina o manifesto com minisign
   (opcional) e publica os artefatos (`gh release create` ou `gh release upload --clobber`).
   A tag `vX.Y.Z` é conferida contra a versão de `CMakeLists.txt` antes da publicação.

Artefatos publicados: `*.rpm`, `*.deb`, `SHA256SUMS`, `SHA256SUMS.asc` (se assinado),
`*.cdx.json`, `podstack-update.json` e `podstack-update.json.minisig` (se assinado).

Secrets usados:

| Secret | Uso | Sem ele |
|---|---|---|
| `GPG_PRIVATE_KEY` | assinar RPMs e `SHA256SUMS` | pacotes/checksums seguem sem assinatura GPG |
| `GPG_KEY_ID` | selecionar a chave GPG | RPMs não são assinados; `SHA256SUMS` usa a chave padrão do keyring importado |
| `PODSTACK_UPDATE_PUBLIC_KEY` | embutir a chave pública Ed25519 no build (`-DPODSTACK_UPDATE_PUBLIC_KEY`) e no RPM (`--define podstack_update_public_key`) | o updater usa `updates.publicKey` em runtime |
| `MINISIGN_PRIVATE_KEY` | assinar o manifesto de updates | manifesto é publicado sem `.minisig` |

### 11.2 Fallback sem secrets

Sem os secrets, o pipeline continua publicando a release: RPM/DEB saem sem assinatura
GPG, `SHA256SUMS` sai sem `.asc` e o manifesto sai sem `.minisig`. Nesse estado o
updater retorna `unconfigured` e não aceita o feed (a verificação de assinatura é
obrigatória). Para habilitar updates, importe a chave privada minisign, publique o
`.minisig` e configure `updates.publicKey` (ou rode o pipeline com
`PODSTACK_UPDATE_PUBLIC_KEY`). A assinatura de RPM é independente do feed: sem
`GPG_PRIVATE_KEY`, o pacote instala, mas não é verificado por GPG.

---

## 12. Instalação limpa (checklist)

**Automatizado:** [`scripts/clean-install-check.sh`](../../scripts/clean-install-check.sh) roda
em container Fedora 44 descartável (via Podman) e cobre instalação do RPM principal e do
`podstack-helper`; presença de binários, units systemd de usuário, unit/polkit do helper e
módulo SELinux; metainfo + `appstreamcli validate`; daemon com socket `0600`; `podstack
status` e `podstack doctor`. O [`packaging.yml`](../../.github/workflows/packaging.yml) executa o build do RPM em container
Fedora 44 e o build do DEB em container Ubuntu 24.04 em PRs que alteram `packaging/**`,
`scripts/build-*`, `scripts/clean-install-check.sh`, `CMakeLists.txt`, `third_party/**` ou
`release.yml`.

**Manual (máquina/VM de referência):** GUI em sessão gráfica real, KVM (`/dev/kvm`) e
machines com SSH, polkit real com systemd como PID 1, coexistência/remoção do Docker,
upgrade a partir de versão anterior, desinstalação e as demais combinações da matriz de
compatibilidade (Fedora 43/44, Ubuntu 24.04/26.04, Wayland/X11, GNOME, KDE Plasma, rootful).

1. `dnf install podstack` em Fedora 43 limpo.
2. `systemctl --user status podstack.socket` ativo.
3. Abrir GUI → setup → diagnóstico; instalar/atualizar Podman; Docker preservado (se presente); validar.
4. Repetir em máquina com Docker instalado; confirmar coexistência e que a remoção é opt-in.
5. Criar container → acessar.
6. Criar máquina → SSH.
7. `apt install ./podstack_0.1.3_amd64.deb` em Ubuntu 24.04 e 26.04 limpos; abrir a GUI sob
   Xvfb; validar o `podstack-helper` no D-Bus e `podstack status` com Podman 4.9/5.7.
7. `podstack doctor` verde.
8. Desinstalar → `dnf remove podstack` remove units; dados do usuário preservados (documentar).

---

## 13. Desinstalação

- RPM remove binários/units/polkit; **não** remove dados em `~/.config`, `~/.local/share`, `~/.cache`.
- `podstack reset` remove dados de runtime.
- Remover CA/DNS: `podstack proxy ca remove` / `podstack proxy dns remove` antes de desinstalar (ou o helper limpa no `%preun`).
- Documentar limpeza manual completa.
