# Empacotamento e distribuição — PodStack

> Alvo primário: **Fedora 43+** via RPM. AppImage para demonstração. **Flatpak não está previsto no v1** (reavaliar sob demanda). O produto precisa de integração com systemd, polkit, SELinux e `/dev/kvm`, o que um sandbox Flatpak não entrega.

---

## 1. Pacotes

| Pacote | Conteúdo | Dependências |
|---|---|---|
| `podstack` | GUI, CLI, daemon, QML, ícones, `.desktop`, AppStream | Qt 6.10+, libcurl, sqlite, podman |
| `podstack-helper` | helper root, unit system, polkit | systemd, polkit |
| `podstack-selinux` | política SELinux | `selinux-policy-targeted` |
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
NoNewPrivileges=yes
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

- Módulo `podstack.te` com tipos:
  - `podstack_helper_t` (domínio do helper)
  - `podstack_helper_exec_t`
  - portas: `podstack_port_t` (80/443/8080/8443) ou reutilizar `http_port_t`
  - `podstack_var_run_t` para sockets
- Regras mínimas:
  - helper lê/escreve `/etc/systemd/resolved.conf.d/`, `/etc/pki/ca-trust/source/anchors/`, `/run/podstack/`.
  - helper sinaliza `systemd-resolved` (`systemctl reload`).
  - helper faz bind em portas allowlisted.
- Testar com `selinux=enforcing` e `ausearch -m avc`.
- Documentar `setsebool`/`semanage` se necessário.

---

## 7. RPM spec (esqueleto)

```spec
Name:           podstack
Version:        0.1.0
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
BuildRequires:  sqlite-devel
Requires:       podman >= 5.8
Requires:       systemd >= 257
Requires:       qtermwidget
Requires:       qt6-qtsvg

%description
PodStack is a desktop app for Podman: containers, images, volumes, networks,
Compose, Linux machines (QEMU/KVM), Kubernetes and local domains.

%package helper
Summary:        Privileged helper for PodStack
Requires:       podstack = %{version}-%{release}
Requires:       polkit

%description helper
Minimal root helper for DNS, privileged ports and local CA.

%package selinux
Summary:        SELinux policy for PodStack
Requires:       selinux-policy-targeted

%prep
%autosetup

%build
%cmake -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
%cmake_build

%install
%cmake_install
# systemd units, polkit, desktop, icons, metainfo

%post
%systemd_user_post podstack.socket podstack.service
%systemd_post podstack-helper.service

%preun
%systemd_user_preun podstack.socket podstack.service
%systemd_preun podstack-helper.service

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

%files selinux
%{_datadir}/selinux/packages/podstack.pp
```

Build local:

```bash
fedpkg local          # COPR
# ou
rpmbuild -ba podstack.spec
```

---

## 8. AppImage

- Objetivo: demo e testes em distros sem RPM.
- Empacotar GUI + CLI + daemon; **sem** helper/systemd units (limitações documentadas).
- QML modules e platform plugins no AppDir.
- `linuxdeploy` + `appimagetool`.
- Funciona com Podman do host via socket do usuário.

---

## 9. Flatpak (não previsto no v1; reavaliar)

- **Não** é requisito do v1 e não entrega o produto completo: sem helper, sem units, sem `/dev/kvm` por padrão.
- Se avaliado no futuro, só faz sentido como cliente de um daemon já instalado no host:
  - `--filesystem=xdg-run/podman`
  - `--filesystem=xdg-run/podstack`
  - `--talk-name=org.freedesktop.Notifications`
  - `--talk-name=org.kde.StatusNotifierWatcher`
- Documentar claramente; não é o canal primário.

---

## 10. Atualizações

- RPM: `dnf upgrade podstack` (recomendado).
- AppImage: atualizador próprio com assinatura Ed25519 (opt-in).
- Canal beta: COPR separado (`podstack-beta`).
- Verificação de versão: `podstack update --check` (exit 3 se desatualizado).
- Nunca auto-update silencioso em headless/CI.

---

## 11. Instalação limpa (checklist)

1. `dnf install podstack` em Fedora 43 limpo.
2. `systemctl --user status podstack.socket` ativo.
3. Abrir GUI → setup → diagnóstico; instalar/atualizar Podman; Docker preservado (se presente); validar.
4. Repetir em máquina com Docker instalado; confirmar coexistência e que a remoção é opt-in.
5. Criar container → acessar.
6. Criar máquina → SSH.
7. `podstack doctor` verde.
8. Desinstalar → `dnf remove podstack` remove units; dados do usuário preservados (documentar).

---

## 12. Desinstalação

- RPM remove binários/units/polkit; **não** remove dados em `~/.config`, `~/.local/share`, `~/.cache`.
- `podstack reset` remove dados de runtime.
- Remover CA/DNS: `podstack proxy ca remove` / `podstack proxy dns remove` antes de desinstalar (ou o helper limpa no `%preun`).
- Documentar limpeza manual completa.
