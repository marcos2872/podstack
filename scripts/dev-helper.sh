#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Instala/remove o podstack-helper em uma máquina de desenvolvimento.
#
# O helper é um serviço root (systemd de sistema + D-Bus de sistema + polkit);
# o scripts/dev.sh sobe apenas daemon/GUI do usuário e não escreve em /usr.
# Este script pede confirmação e usa sudo para copiar os artefatos do build.
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${PODSTACK_BUILD_DIR:-$REPO_ROOT/build/dev}"

HELPER_BIN="$BUILD_DIR/podstack-helper"
SERVICE_SRC="$REPO_ROOT/packaging/systemd-system/podstack-helper.service"
POLICY_SRC="$REPO_ROOT/packaging/polkit/dev.podstack.helper.policy"
DBUS_SRC="$REPO_ROOT/packaging/dbus/dev.podstack.Helper1.conf"

HELPER_DST="/usr/libexec/podstack/podstack-helper"
SERVICE_DST="/usr/lib/systemd/system/podstack-helper.service"
POLICY_DST="/usr/share/polkit-1/actions/dev.podstack.helper.policy"
DBUS_DST="/usr/share/dbus-1/system.d/dev.podstack.Helper1.conf"

usage() {
    cat <<EOF
Uso: $(basename "$0") [--yes] [--uninstall]

  --yes, -y     não pergunta confirmação
  --uninstall   remove o helper, a unit, a política polkit e o D-Bus conf
  --help, -h    mostra esta ajuda

Instala a partir de: $BUILD_DIR
Se o binário não existir, rode antes: cmake --build --preset dev
EOF
}

MODE="install"
ASSUME_YES=0

while [ "$#" -gt 0 ]; do
    case "$1" in
        --yes|-y)
            ASSUME_YES=1
            ;;
        --uninstall)
            MODE="uninstall"
            ;;
        --help|-h)
            usage
            exit 0
            ;;
        *)
            echo "Opção desconhecida: $1" >&2
            usage >&2
            exit 2
            ;;
    esac
    shift
done

if [ "$(id -u)" -eq 0 ]; then
    SUDO=()
else
    SUDO=(sudo)
fi

if [ "$MODE" = "install" ] && [ ! -x "$HELPER_BIN" ]; then
    echo "Binário não encontrado: $HELPER_BIN" >&2
    echo "Rode: cmake --build --preset dev" >&2
    exit 1
fi

if [ "$MODE" = "uninstall" ]; then
    echo "Serão removidos:"
    echo "  $SERVICE_DST"
    echo "  $POLICY_DST"
    echo "  $DBUS_DST"
    echo "  $HELPER_DST"
else
    echo "Serão instalados:"
    echo "  $HELPER_BIN -> $HELPER_DST"
    echo "  $SERVICE_SRC -> $SERVICE_DST"
    echo "  $POLICY_SRC -> $POLICY_DST"
    echo "  $DBUS_SRC -> $DBUS_DST"
fi

if [ "$ASSUME_YES" -ne 1 ]; then
    read -r -p "Continuar? [s/N] " answer
    case "$answer" in
        s|S|sim|SIM|y|Y|yes|YES) ;;
        *)
            echo "Cancelado."
            exit 1
            ;;
    esac
fi

if [ "$MODE" = "uninstall" ]; then
    "${SUDO[@]}" systemctl disable --now podstack-helper.service 2>/dev/null || true
    "${SUDO[@]}" rm -f "$SERVICE_DST" "$POLICY_DST" "$DBUS_DST" "$HELPER_DST"
    "${SUDO[@]}" systemctl daemon-reload
    echo "Helper removido."
    echo "Reinicie a sessão/máquina para o system bus descarregar a política antiga."
    exit 0
fi

"${SUDO[@]}" install -Dm755 "$HELPER_BIN" "$HELPER_DST"
"${SUDO[@]}" install -Dm644 "$SERVICE_SRC" "$SERVICE_DST"
"${SUDO[@]}" install -Dm644 "$POLICY_SRC" "$POLICY_DST"
"${SUDO[@]}" install -Dm644 "$DBUS_SRC" "$DBUS_DST"

"${SUDO[@]}" systemctl daemon-reload
if ! "${SUDO[@]}" systemctl enable --now podstack-helper.service; then
    echo "Falha ao iniciar o podstack-helper." >&2
    echo "Se o D-Bus ainda não carregou a política, reinicie a máquina e rode:" >&2
    echo "  systemctl status podstack-helper.service" >&2
    exit 1
fi

# O system bus lê a política em /usr/share/dbus-1/system.d ao iniciar/reload.
if systemctl list-unit-files dbus-broker.service >/dev/null 2>&1; then
    "${SUDO[@]}" systemctl reload dbus-broker 2>/dev/null || true
elif systemctl list-unit-files dbus.service >/dev/null 2>&1; then
    "${SUDO[@]}" systemctl reload dbus 2>/dev/null || true
fi

sleep 0.5
if busctl --system list 2>/dev/null | grep -q 'dev.podstack.Helper1'; then
    echo "Helper disponível em dev.podstack.Helper1."
else
    echo "Aviso: o serviço não apareceu no barramento de sistema." >&2
    echo "Verifique: systemctl status podstack-helper.service" >&2
    echo "Se necessário, reinicie a máquina para recarregar a política do D-Bus." >&2
fi

echo "Pronto. O polkit pedirá sua senha ao usar ações privilegiadas do setup."
