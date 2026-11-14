Name:           podstack
Version:        0.1.3
Release:        1%{?dist}
Summary:        Native desktop for Podman containers, VMs and Kubernetes
License:        GPL-3.0-or-later AND GPL-2.0-or-later AND LGPL-2.0-or-later
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
BuildRequires:  openssl-devel
# Módulo SELinux (Fedora/refpolicy). Sem ele o %build falha de propósito:
# o carregamento no %post é tolerante, a compilação não.
BuildRequires:  selinux-policy-devel

Requires:       podman >= 5.8
Requires:       systemd >= 257
Requires:       qtermwidget
Requires:       qt6-qtsvg
Requires:       openssh-clients
Requires:       polkit
Requires(post): policycoreutils
Requires(postun): policycoreutils
Requires(pre):  procps-ng
Requires(preun): procps-ng
# A partir de 0.1.1 o helper é parte do pacote principal; substitui o
# subpacote antigo para que o upgrade remova a instalação separada.
Obsoletes:      podstack-helper < %{version}-%{release}
Provides:       podstack-helper = %{version}-%{release}

%description
PodStack is a native Linux desktop for Podman: containers, images, volumes,
networks, Compose projects, QEMU/KVM machines, Kubernetes and local HTTPS
domains in one application. The GUI/CLI talk to a per-user daemon over a
0600 Unix socket; privileged setup actions use an allowlisted helper with
polkit.

%prep
%autosetup -n %{name}-%{version}

%build
%cmake -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DPODSTACK_UPDATE_PUBLIC_KEY='%{?podstack_update_public_key}'
%cmake_build
# Compilação do módulo SELinux é obrigatória (falha quebra o build).
make -f /usr/share/selinux/devel/Makefile -C packaging/selinux podstack.pp

%install
%cmake_install
install -D -m 0644 packaging/selinux/podstack.pp \
    %{buildroot}%{_datadir}/selinux/packages/podstack.pp

%pre
# Encerra GUI/daemon/helper antigos antes do unpack. O runner
# `podstack update --install --runner` não é tocado. Roda também na
# instalação para não deixar um app de desenvolvimento com o mesmo nome.
log_update() {
    if [ -n "${PODSTACK_UPDATE_JOB_ID:-}" ] && command -v logger >/dev/null 2>&1; then
        logger -t podstack-update \
            "JOB_ID=${PODSTACK_UPDATE_JOB_ID} UID=${PODSTACK_UPDATE_UID:-} $*"
    fi
}
if [ "$1" -ge 1 ]; then
    touched_uids=""
    log_update "rpm %pre iniciado"
    restore_rpm() {
        for touched in $touched_uids; do
            systemctl --machine "user@${touched}.service" --user unmask --runtime \
                podstack.socket >/dev/null 2>&1 || :
        done
        systemctl start podstack-helper.service >/dev/null 2>&1 || :
    }
    if command -v systemctl >/dev/null 2>&1; then
        uids="$(loginctl list-sessions --no-legend 2>/dev/null | awk '{print $2}' | sort -u)"
        for uid in $uids; do
            touched_uids="$touched_uids $uid"
            systemctl --machine "user@${uid}.service" --user mask --runtime \
                podstack.socket >/dev/null 2>&1 || :
            systemctl --machine "user@${uid}.service" --user stop \
                podstack.socket podstack.service >/dev/null 2>&1 || :
            if systemctl --machine "user@${uid}.service" --user is-active --quiet \
                podstack.service 2>/dev/null; then
                pkill -TERM -u "$uid" -x podstackd >/dev/null 2>&1 || :
            fi
            pkill -TERM -u "$uid" -x podstack-gui >/dev/null 2>&1 || :
        done
        sleep 3
        for uid in $touched_uids; do
            pkill -KILL -u "$uid" -x podstack-gui >/dev/null 2>&1 || :
            if pgrep -u "$uid" -x podstack-gui >/dev/null 2>&1; then
                echo "PodStack: a GUI do uid ${uid} não encerrou; abortando o upgrade." >&2
                restore_rpm
                exit 1
            fi
            pkill -KILL -u "$uid" -x podstackd >/dev/null 2>&1 || :
            if pgrep -u "$uid" -x podstackd >/dev/null 2>&1; then
                echo "PodStack: o daemon do uid ${uid} não encerrou; abortando o upgrade." >&2
                restore_rpm
                exit 1
            fi
            if systemctl --machine "user@${uid}.service" --user is-active --quiet \
                podstack.service 2>/dev/null; then
                echo "PodStack: o daemon do uid ${uid} continua ativo; abortando o upgrade." >&2
                restore_rpm
                exit 1
            fi
        done
        if systemctl is-active --quiet podstack-helper.service 2>/dev/null; then
            if ! systemctl stop podstack-helper.service >/dev/null 2>&1; then
                echo "PodStack: não foi possível parar o helper; abortando o upgrade." >&2
                restore_rpm
                exit 1
            fi
            if systemctl is-active --quiet podstack-helper.service 2>/dev/null; then
                echo "PodStack: o helper ainda está ativo; abortando o upgrade." >&2
                restore_rpm
                exit 1
            fi
        fi
    else
        # Sem systemd: fallback por nome de processo; não toca no runner.
        pkill -TERM -x podstack-gui >/dev/null 2>&1 || :
        pkill -TERM -x podstackd >/dev/null 2>&1 || :
        sleep 3
        pkill -KILL -x podstack-gui >/dev/null 2>&1 || :
        pkill -KILL -x podstackd >/dev/null 2>&1 || :
        if pgrep -x podstackd >/dev/null 2>&1; then
            echo "PodStack: o daemon não encerrou; abortando o upgrade." >&2
            exit 1
        fi
        if pgrep -x podstack-gui >/dev/null 2>&1; then
            echo "PodStack: a GUI não encerrou; abortando o upgrade." >&2
            exit 1
        fi
    fi
fi

%post
log_update() {
    if [ -n "${PODSTACK_UPDATE_JOB_ID:-}" ] && command -v logger >/dev/null 2>&1; then
        logger -t podstack-update \
            "JOB_ID=${PODSTACK_UPDATE_JOB_ID} UID=${PODSTACK_UPDATE_UID:-} $*"
    fi
}
log_update "rpm %post iniciado"
# O socket do usuário sobe sob demanda na primeira conexão da GUI/CLI.
systemd-tmpfiles --create podstack.conf >/dev/null 2>&1 || :
systemctl --global enable podstack.socket >/dev/null 2>&1 || :
# Tolerante a sistemas sem SELinux/semodule: o módulo é opcional em runtime.
semodule -i %{_datadir}/selinux/packages/podstack.pp >/dev/null 2>&1 || :
# Aplica os contextos do .fc ao binário já instalado (sem restorecon o tipo
# podstack_helper_exec_t não entra em vigor e a transição de domínio não ocorre).
restorecon -R /usr/libexec/podstack/podstack-helper /run/podstack >/dev/null 2>&1 || :
systemctl daemon-reload >/dev/null 2>&1 || :
systemctl enable podstack-helper.service >/dev/null 2>&1 || :
# `enable --now` não reinicia um helper já ativo; o restart é obrigatório
# para carregar a allowlist e o código novos.
systemctl restart podstack-helper.service >/dev/null 2>&1 || :
log_update "rpm %post: helper reiniciado"
for uid in $(loginctl list-sessions --no-legend 2>/dev/null | awk '{print $2}' | sort -u); do
    systemctl --machine "user@${uid}.service" --user daemon-reload >/dev/null 2>&1 || :
    systemctl --machine "user@${uid}.service" --user unmask --runtime \
        podstack.socket >/dev/null 2>&1 || :
done

%preun
if [ "$1" -eq 0 ]; then
    # Remoção final: encerra o app dos usuários logados antes de apagar os
    # binários e desabilita o helper.
    if command -v systemctl >/dev/null 2>&1; then
        for uid in $(loginctl list-sessions --no-legend 2>/dev/null | awk '{print $2}' | sort -u); do
            systemctl --machine "user@${uid}.service" --user stop \
                podstack.socket podstack.service >/dev/null 2>&1 || :
            pkill -TERM -u "$uid" -x podstack-gui >/dev/null 2>&1 || :
            pkill -TERM -u "$uid" -x podstackd >/dev/null 2>&1 || :
        done
        sleep 3
        for uid in $(loginctl list-sessions --no-legend 2>/dev/null | awk '{print $2}' | sort -u); do
            pkill -KILL -u "$uid" -x podstack-gui >/dev/null 2>&1 || :
            pkill -KILL -u "$uid" -x podstackd >/dev/null 2>&1 || :
        done
    else
        pkill -TERM -x podstack-gui >/dev/null 2>&1 || :
        pkill -TERM -x podstackd >/dev/null 2>&1 || :
        sleep 3
        pkill -KILL -x podstack-gui >/dev/null 2>&1 || :
        pkill -KILL -x podstackd >/dev/null 2>&1 || :
    fi
    systemctl disable --now podstack-helper.service >/dev/null 2>&1 || :
    semodule -r podstack >/dev/null 2>&1 || :
fi

%posttrans
# Recuperação do RPM: o %preun antigo pode parar o helper depois do %post do
# pacote novo, e o gerenciador de sistema não desmascara unit de usuário.
# Valida por usuário, faz o health check do helper e só falha se não
# conseguir restaurar.
semodule -i %{_datadir}/selinux/packages/podstack.pp >/dev/null 2>&1 || :
restorecon -R /usr/libexec/podstack/podstack-helper /run/podstack >/dev/null 2>&1 || :
helper_rc=0
unmask_rc=0
systemctl daemon-reload >/dev/null 2>&1 || helper_rc=1
systemctl enable podstack-helper.service >/dev/null 2>&1 || helper_rc=1
systemctl restart podstack-helper.service >/dev/null 2>&1 || helper_rc=1
for uid in $(loginctl list-sessions --no-legend 2>/dev/null | awk '{print $2}' | sort -u); do
    systemctl --machine "user@${uid}.service" --user daemon-reload >/dev/null 2>&1 || :
    if ! systemctl --machine "user@${uid}.service" --user unmask --runtime \
        podstack.socket >/dev/null 2>&1; then
        if ! systemctl --machine "user@${uid}.service" --user unmask --runtime \
            podstack.socket >/dev/null 2>&1; then
            unmask_rc=1
            echo "PodStack: o socket do uid ${uid} continua mascarado; execute" \
                "'systemctl --user unmask --runtime podstack.socket'." >&2
        fi
    fi
done
if ! systemctl is-active --quiet podstack-helper.service 2>/dev/null; then
    systemctl start podstack-helper.service >/dev/null 2>&1 || :
fi
if ! systemctl is-active --quiet podstack-helper.service 2>/dev/null; then
    helper_rc=1
fi
helper_version="$(/usr/libexec/podstack/podstack-helper --version 2>/dev/null || true)"
if [ "$helper_version" != "%{version}" ]; then
    helper_rc=1
fi
if [ "$helper_rc" -ne 0 ]; then
    echo "PodStack: falha ao restaurar o helper (versão '${helper_version}', esperada '%{version}');" \
        "execute 'systemctl restart podstack-helper.service' e reabra o app." >&2
    exit 1
fi
if [ "$unmask_rc" -ne 0 ]; then
    echo "PodStack: um ou mais sockets de usuário continuam mascarados;" \
        "execute 'systemctl --user unmask --runtime podstack.socket' e reabra o app." >&2
    exit 1
fi

%files
%license LICENSE NOTICE
%{_datadir}/licenses/podstack/qtermwidget
%{_bindir}/podstack
%{_bindir}/podstackd
%{_bindir}/podstack-gui
%{_userunitdir}/podstack.socket
%{_userunitdir}/podstack.service
%{_datadir}/applications/dev.podstack.PodStack.desktop
%{_datadir}/metainfo/dev.podstack.PodStack.metainfo.xml
%{_datadir}/icons/hicolor/scalable/apps/dev.podstack.PodStack.svg
%{_datadir}/icons/hicolor/16x16/apps/dev.podstack.PodStack.png
%{_datadir}/icons/hicolor/24x24/apps/dev.podstack.PodStack.png
%{_datadir}/icons/hicolor/32x32/apps/dev.podstack.PodStack.png
%{_datadir}/icons/hicolor/48x48/apps/dev.podstack.PodStack.png
%{_datadir}/icons/hicolor/64x64/apps/dev.podstack.PodStack.png
%{_datadir}/icons/hicolor/128x128/apps/dev.podstack.PodStack.png
%{_datadir}/icons/hicolor/256x256/apps/dev.podstack.PodStack.png
%{_datadir}/icons/hicolor/512x512/apps/dev.podstack.PodStack.png
%{_libexecdir}/podstack/podstack-helper
%{_libexecdir}/podstack/podstack-update-exec
%{_unitdir}/podstack-helper.service
%{_tmpfilesdir}/podstack.conf
%config(noreplace) /etc/logrotate.d/podstack
%{_datadir}/polkit-1/actions/dev.podstack.helper.policy
%{_datadir}/dbus-1/system.d/dev.podstack.Helper1.conf
%{_datadir}/selinux/packages/podstack.pp

%changelog
* Thu Nov 12 2026 PodStack contributors <dev@podstack.dev> - 0.1.3-1
- Detect Ubuntu OVMF paths and use distro package names in the setup.
- Surface setup install failures in the GUI log and footer.

* Thu Nov 12 2026 PodStack contributors <dev@podstack.dev> - 0.1.2-1
- Build with Qt 6.4-compatible fallbacks.
- Vendor qtermwidget 2.0.0 as a static library for the Ubuntu 24.04 DEB.
- Fix the DEB dependencies, strip the binaries and split the release jobs.

* Thu Nov 12 2026 PodStack contributors <dev@podstack.dev> - 0.1.1-1
- Merge the allowlisted helper into the main podstack RPM.
- Enable/start podstack-helper.service and create the user dirs in %post.
- Use the systemd socket activation fd and allow Podman child processes.

* Wed Nov 11 2026 PodStack contributors <dev@podstack.dev> - 0.1.0-1
- Initial RPM packaging for the 0.1.0 release.
