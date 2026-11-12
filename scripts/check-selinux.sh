#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Valida o módulo SELinux do PodStack sem instalá-lo no host.
#
# Requer o pacote selinux-policy-devel (Makefile do refpolicy),
# checkpolicy (checkmodule) e policycoreutils (semodule_package/
# semodule_unpackage). Se o devel não estiver presente, o script apenas
# informa o skip e sai com 0.
set -euo pipefail

cd "$(dirname "$0")/.."

devel_makefile="/usr/share/selinux/devel/Makefile"

if [ ! -f "$devel_makefile" ]; then
    echo "skip: selinux-policy-devel ausente"
    exit 0
fi

for tool in checkmodule semodule_package semodule_unpackage; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        echo "skip: $tool ausente (instale checkpolicy/policycoreutils)" >&2
        exit 0
    fi
done

tmpdir="$(mktemp -d "${TMPDIR:-/tmp}/podstack-selinux.XXXXXX")"
trap 'rm -rf "$tmpdir"' EXIT

cp packaging/selinux/podstack.te \
   packaging/selinux/podstack.fc \
   packaging/selinux/podstack.if \
   "$tmpdir/"

# O Makefile do refpolicy roda m4 + checkmodule + semodule_package.
make -f "$devel_makefile" -C "$tmpdir" podstack.pp

# Desempacota o .pp gerado e reempacota para validar explicitamente o
# semodule_unpackage/semodule_package (nada é carregado no host).
semodule_unpackage "$tmpdir/podstack.pp" \
                   "$tmpdir/podstack.mod" \
                   "$tmpdir/podstack.mod.fc"
semodule_package -o "$tmpdir/podstack-validado.pp" \
                 -m "$tmpdir/podstack.mod" \
                 -f "$tmpdir/podstack.mod.fc"

echo "ok: módulo compilado e empacotado em $tmpdir (não instalado)"
