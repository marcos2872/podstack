#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Verifica invariantes do empacotamento systemd. O daemon roda como o próprio
# usuário e grava ~/.ssh/config, downloads/exportações em caminhos escolhidos e
# shares do virtiofsd: ProtectHome/PrivateTmp no serviço quebram esses fluxos
# (os scopes de QEMU/virtiofsd herdam o mount namespace do daemon).
set -eu

repo="${1:-$(pwd)}"
service="$repo/packaging/systemd/podstack.service"
socket="$repo/packaging/systemd/podstack.socket"
helper="$repo/packaging/systemd-system/podstack-helper.service"

fail() {
    echo "FAIL: $*" >&2
    exit 1
}

[ -f "$service" ] || fail "unit do daemon não encontrada: $service"
[ -f "$socket" ] || fail "unit do socket não encontrada: $socket"

# --- daemon ---------------------------------------------------------------
if grep -Eq '^[[:space:]]*ProtectHome=(read-only|tmpfs)' "$service"; then
    fail "podstack.service não pode restringir \$HOME (quebra ~/.ssh e shares)"
fi
if grep -Eq '^[[:space:]]*PrivateTmp=(yes|true)' "$service"; then
    fail "podstack.service não pode isolar /tmp (quebra shares via virtiofsd)"
fi
grep -Eq '^[[:space:]]*ProtectSystem=full' "$service" \
    || fail "podstack.service deve manter ProtectSystem=full"
grep -Eq '^[[:space:]]*StateDirectory=podstack' "$service" \
    || fail "podstack.service deve declarar StateDirectory=podstack"
grep -Eq '^[[:space:]]*CacheDirectory=podstack' "$service" \
    || fail "podstack.service deve declarar CacheDirectory=podstack"
grep -Eq '^[[:space:]]*ConfigurationDirectory=podstack' "$service" \
    || fail "podstack.service deve declarar ConfigurationDirectory=podstack"

# --- socket ---------------------------------------------------------------
grep -Eq '^[[:space:]]*ListenStream=%t/podstack/control\.sock' "$socket" \
    || fail "podstack.socket deve escutar em %t/podstack/control.sock"
grep -Eq '^[[:space:]]*SocketMode=0600' "$socket" || fail "SocketMode deve ser 0600"
grep -Eq '^[[:space:]]*DirectoryMode=0700' "$socket" || fail "DirectoryMode deve ser 0700"

# --- helper (system) ------------------------------------------------------
[ -f "$helper" ] || fail "unit do helper não encontrada: $helper"
grep -Eq '^[[:space:]]*Restart=(on-failure|always)' "$helper" \
    || fail "podstack-helper.service deve reiniciar após falha"

exit 0
