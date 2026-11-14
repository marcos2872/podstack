#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Garante as fronteiras dos módulos QML:
#   - ds/ não importa Pages/Features (nem o módulo guarda-chuva PodStack)
#   - pages/ e features/ não importam o guarda-chuva PodStack
#   - (--strict) controles nativos só existem em ds/primitives
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
qml="$root/src/gui/qml"
strict=0
[[ "${1:-}" == "--strict" ]] && strict=1
fail=0

check() {
    local description="$1"
    shift
    if "$@"; then
        echo "falha: $description"
        fail=1
    fi
}

echo "== fronteiras de import =="
check "ds/ importa PodStack.Pages ou PodStack.Features" \
    grep -rnE '^import PodStack\.(Pages|Features)' "$qml/ds"
check "pages/ importa o guarda-chuva PodStack" \
    grep -rnE '^import PodStack$' "$qml/pages"
check "features/ importa o guarda-chuva PodStack" \
    grep -rnE '^import PodStack$' "$qml/features"

if [[ $strict -eq 1 ]]; then
    echo "== controles nativos fora de ds/primitives =="
    check "Dialog/Button/ComboBox/TextField nativo fora de ds/primitives" \
        grep -rnE '^[[:space:]]*(Dialog|Button|ComboBox|TextField)[[:space:]]*\{' \
        "$qml/pages" "$qml/features" "$qml/Main.qml"
else
    count=$(grep -rhoE '^[[:space:]]*(Dialog|Button|ComboBox|TextField)[[:space:]]*\{' \
        "$qml/pages" "$qml/features" "$qml/Main.qml" 2>/dev/null | wc -l)
    echo "aviso: $count controles nativos restantes fora de ds/primitives (rode com --strict no fim da migração)"
fi

if [[ $fail -ne 0 ]]; then
    echo "fronteiras QML violadas"
    exit 1
fi
echo "fronteiras QML ok"
