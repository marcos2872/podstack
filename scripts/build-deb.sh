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
cmake --build --preset dev

rm -rf "$root"
mkdir -p "$root/DEBIAN"
DESTDIR="$root" cmake --install "$build_dir" --prefix /usr
install -m 0644 packaging/deb/control "$root/DEBIAN/control"
install -m 0755 packaging/deb/postinst "$root/DEBIAN/postinst"
install -m 0755 packaging/deb/prerm "$root/DEBIAN/prerm"
sed -i "s/^Version: .*/Version: $version-1/" "$root/DEBIAN/control"
sed -i "s/^Architecture: .*/Architecture: $arch/" "$root/DEBIAN/control"

dpkg-deb --build --root-owner-group "$root" "$out/podstack_${version}_${arch}.deb"
echo "DEB em $out/podstack_${version}_${arch}.deb"
