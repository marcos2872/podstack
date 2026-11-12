#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Instala (ou remove) um desktop entry de desenvolvimento para o PodStack no
# perfil do usuário, apontando para build/dev/podstack-gui.
#
# Sem isso a dock não encontra dev.podstack.PodStack.desktop e mostra um ícone
# genérico, porque o pacote ainda não foi instalado em /usr/share.
#
# Uso:
#   scripts/install-dev-desktop.sh [--build-dir DIR]
#   scripts/install-dev-desktop.sh --uninstall
#
# O arquivo fica em $XDG_DATA_HOME/applications (default ~/.local/share) e os
# ícones em $XDG_DATA_HOME/icons/hicolor. Nada é instalado no sistema.
set -euo pipefail

cd "$(dirname "$0")/.."

build_dir="${PODSTACK_BUILD_DIR:-build/dev}"
uninstall=0

usage() {
    awk 'NR > 1 && /^set -euo pipefail$/ { exit } NR > 1 { sub(/^# ?/, ""); print }' "$0"
}

while [ "$#" -gt 0 ]; do
    case "$1" in
        --build-dir)
            [ "$#" -ge 2 ] || { echo "--build-dir exige um caminho." >&2; exit 2; }
            build_dir="$2"
            shift
            ;;
        --uninstall)
            uninstall=1
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            echo "Argumento desconhecido: $1" >&2
            usage >&2
            exit 2
            ;;
    esac
    shift
done

data_home="${XDG_DATA_HOME:-$HOME/.local/share}"
desktop_dir="$data_home/applications"
desktop_path="$desktop_dir/dev.podstack.PodStack.desktop"
icon_root="$data_home/icons/hicolor"

update_caches() {
    if command -v update-desktop-database >/dev/null 2>&1; then
        update-desktop-database "$desktop_dir" >/dev/null 2>&1 || true
    fi
    if command -v gtk-update-icon-cache >/dev/null 2>&1; then
        gtk-update-icon-cache -f -t "$icon_root" >/dev/null 2>&1 || true
    fi
}

if [ "$uninstall" -eq 1 ]; then
    rm -f "$desktop_path"
    rm -f "$icon_root/256x256/apps/dev.podstack.PodStack.png"
    rm -f "$icon_root/scalable/apps/dev.podstack.PodStack.svg"
    update_caches
    echo "Desktop entry de dev removido: $desktop_path"
    exit 0
fi

gui="$PWD/$build_dir/podstack-gui"
if [ ! -x "$gui" ]; then
    echo "Não encontrei $gui. Rode 'cmake --build --preset dev' primeiro." >&2
    exit 1
fi

install -d -m 0755 "$desktop_dir"
install -d -m 0755 "$icon_root/256x256/apps" "$icon_root/scalable/apps"
install -m 0644 assets/png/podstack-icon-256.png \
    "$icon_root/256x256/apps/dev.podstack.PodStack.png"
install -m 0644 assets/podstack-icon.svg \
    "$icon_root/scalable/apps/dev.podstack.PodStack.svg"

cat >"$desktop_path" <<EOF
[Desktop Entry]
Type=Application
Name=PodStack (dev)
Comment=PodStack em desenvolvimento (containers, VMs e Kubernetes)
Exec="$gui"
Icon=dev.podstack.PodStack
Terminal=false
Categories=Development;System;
StartupWMClass=dev.podstack.PodStack
Keywords=podman;docker;container;compose;kubernetes;vm;
EOF
chmod 0644 "$desktop_path"

if command -v desktop-file-validate >/dev/null 2>&1; then
    desktop-file-validate "$desktop_path" >/dev/null 2>&1 || true
fi
update_caches

echo "Desktop entry de dev instalado: $desktop_path"
echo "Reabra a GUI para a dock/menu usarem o ícone."
