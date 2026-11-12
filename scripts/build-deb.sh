#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Gera um .deb binário a partir do build atual. Requer dpkg-deb (dpkg-dev) e as
# dependências de runtime declaradas em packaging/deb/control.
set -euo pipefail

cd "$(dirname "$0")/.."

build_dir="${PODSTACK_BUILD_DIR:-build/dev}"
version="$(sed -n 's/^project(PodStack VERSION \([^ ]*\).*/\1/p' CMakeLists.txt | head -1)"
arch="$(dpkg --print-architecture 2>/dev/null || echo amd64)"
root="$PWD/build/deb/podstack_${version}_${arch}"
out="$PWD/build/deb"

if ! command -v dpkg-deb >/dev/null 2>&1; then
    echo "dpkg-deb não encontrado; instale dpkg-dev." >&2
    exit 1
fi
if [ ! -f "$build_dir/build.ninja" ]; then
    cmake --preset dev
fi
cmake --build "$build_dir"

rm -rf "$root"
mkdir -p "$root/DEBIAN"
DESTDIR="$root" cmake --install "$build_dir" --prefix /usr
install -m 0755 packaging/deb/postinst "$root/DEBIAN/postinst"
install -m 0755 packaging/deb/prerm "$root/DEBIAN/prerm"

# `packaging/deb/control` é o controle de FONTE (usado por dpkg-buildpackage).
# O .deb é montado por staging + dpkg-deb, então precisa de um controle
# binário próprio: sem ${shlibs:Depends} (não há dpkg-shlibdeps no staging)
# e com as dependências de runtime explícitas. Alvo: Debian 12/Ubuntu 24.04.
cat >"$root/DEBIAN/control" <<EOF
Package: podstack
Version: ${version}-1
Architecture: ${arch}
Section: admin
Priority: optional
Maintainer: PodStack contributors <dev@podstack.dev>
Homepage: https://podstack.dev
Depends: podman (>= 5.0), systemd, polkitd, openssh-client, libqt6core6, libqt6gui6, libqt6widgets6, libqt6qml6, libqt6quick6, libqt6quickwidgets6, libqt6network6, libqt6dbus6, libqt6svg6, libqt6sql6, libqt6concurrent6, libarchive13, libcurl4, libsqlite3-0, libssl3, libcrypt1
Recommends: qemu-kvm, qemu-utils, virtiofsd, caddy, docker-compose-v2 | docker-compose
Description: Native desktop for Podman containers, VMs and Kubernetes
 PodStack provides containers, images, volumes, networks, Compose projects,
 QEMU/KVM machines, Kubernetes and local HTTPS domains in one native Linux
 application. The GUI and CLI talk to a per-user daemon over a 0600 Unix
 socket; privileged setup actions use an allowlisted polkit helper.
EOF

dpkg-deb --build --root-owner-group "$root" "$out/podstack_${version}_${arch}.deb"
echo "DEB em $out/podstack_${version}_${arch}.deb"
