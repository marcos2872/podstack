#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Gera um RPM a partir da árvore atual. Requer rpmbuild e as dependências de
# build do spec (Fedora). O resultado fica em build/rpm/RPMS/.
set -euo pipefail

cd "$(dirname "$0")/.."

version="$(sed -n 's/^project(PodStack VERSION \([^ ]*\).*/\1/p' CMakeLists.txt | head -1)"
topdir="$PWD/build/rpm"
mkdir -p "$topdir"/{BUILD,BUILDROOT,RPMS,SOURCES,SPECS,SRPMS}

tarball="$topdir/SOURCES/podstack-$version.tar.gz"
tar --exclude-vcs --exclude=build --exclude=.git --exclude=coverage \
    --transform "s,^,podstack-$version/," -czf "$tarball" .

rpm_defines=(--define "_topdir $topdir" --define "_sourcedir $topdir/SOURCES")
if [ -n "${PODSTACK_UPDATE_PUBLIC_KEY:-}" ]; then
    rpm_defines+=(--define "podstack_update_public_key $PODSTACK_UPDATE_PUBLIC_KEY")
fi

rpmbuild -bb packaging/rpm/podstack.spec "${rpm_defines[@]}"

echo "RPM em $topdir/RPMS/"
