#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Sobe a GUI em offscreen por alguns segundos e falha em qualquer warning de
# QML (handler órfão, propriedade inexistente, TypeError). O `qmllint` não vê
# essas classes de erro porque `backend` é context property: só em runtime.
#
# Uso: scripts/qml-smoke.sh [binário]   (padrão: build/dev/podstack-gui)
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
bin="${1:-$root/build/dev/podstack-gui}"
seconds="${PODSTACK_SMOKE_SECS:-8}"

if [[ ! -x "$bin" ]]; then
    echo "erro: binário não encontrado: $bin (compile antes)" >&2
    exit 1
fi

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
mkdir -p "$work/run"
chmod 700 "$work/run"

log="$work/gui.log"
timeout "$seconds" env \
    QT_QPA_PLATFORM=offscreen \
    XDG_CONFIG_HOME="$work/config" \
    XDG_DATA_HOME="$work/data" \
    XDG_RUNTIME_DIR="$work/run" \
    PODSTACK_CONTROL_SOCKET="$work/run/none.sock" \
    "$bin" >"$log" 2>&1 || true

# Ruído do backend offscreen, não do app. No Qt 6.4 o plugin de bandeja ainda
# tenta conectar um sinal que não existe na plataforma offscreen
# (systemTrayWindowChanged), o que não tem relação com o QML carregado.
filtered="$(grep -E 'warn:|error:|TypeError|ReferenceError|is not a function|no signal of the target' \
    "$log" | grep -vE 'This plugin does not support|systemTrayWindowChanged' || true)"

if [[ -n "$filtered" ]]; then
    echo "== warnings de QML =="
    echo "$filtered"
    echo
    echo "falha: a GUI emitiu avisos ao carregar (ver acima)" >&2
    exit 1
fi

echo "qml smoke: ok (${seconds}s, sem warnings)"
