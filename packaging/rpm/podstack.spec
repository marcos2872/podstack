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

%post
# O socket do usuário sobe sob demanda na primeira conexão da GUI/CLI.
systemctl --global enable podstack.socket >/dev/null 2>&1 || :
# Tolerante a sistemas sem SELinux/semodule: o módulo é opcional em runtime.
semodule -i %{_datadir}/selinux/packages/podstack.pp >/dev/null 2>&1 || :
# Aplica os contextos do .fc ao binário já instalado (sem restorecon o tipo
# podstack_helper_exec_t não entra em vigor e a transição de domínio não ocorre).
restorecon -R /usr/libexec/podstack/podstack-helper /run/podstack >/dev/null 2>&1 || :
# Sem enable --now o app não encontra o nome D-Bus dev.podstack.Helper1.
systemctl enable --now podstack-helper.service >/dev/null 2>&1 || :

%preun
if [ "$1" -eq 0 ]; then
    systemctl disable --now podstack-helper.service >/dev/null 2>&1 || :
    semodule -r podstack >/dev/null 2>&1 || :
fi

%posttrans
# No upgrade de 0.1.0, o %preun do antigo podstack-helper remove o módulo
# SELinux e para o serviço depois do %post do pacote novo. Reaplica no fim
# da transação para o helper continuar disponível.
semodule -i %{_datadir}/selinux/packages/podstack.pp >/dev/null 2>&1 || :
restorecon -R /usr/libexec/podstack/podstack-helper /run/podstack >/dev/null 2>&1 || :
systemctl enable --now podstack-helper.service >/dev/null 2>&1 || :

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
%{_unitdir}/podstack-helper.service
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
