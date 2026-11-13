#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Smoke de instalação limpa do RPM do PodStack em um container Fedora 44
# descartável (o container é criado via podman e removido ao final).
#
# Cobre:
#   - instalação via dnf do RPM principal (helper incluído);
#   - binários, unidades systemd de usuário, unit/polkit do helper;
#   - metainfo instalado + appstreamcli validate;
#   - daemon em background, socket com modo 0600;
#   - `podstack status` e `podstack doctor`.
#
# NÃO cobre: GUI (sem display), KVM (sem /dev/kvm), polkit real (sem systemd
# como PID 1), engine Podman de verdade (o serviço local é só para o smoke) e
# upgrade a partir de uma versão anterior.
#
# Uso:
#   scripts/clean-install-check.sh [--skip-build]
#   PODSTACK_RPM=/caminho/podstack-0.1.0-1.fc44.x86_64.rpm \
#       scripts/clean-install-check.sh --skip-build
#
# Sem --skip-build, chama scripts/build-rpm.sh antes do smoke.
set -euo pipefail

cd "$(dirname "$0")/.."

usage() {
    awk 'NR > 1 && /^set -euo pipefail$/ { exit } NR > 1 { sub(/^# ?/, ""); print }' "$0"
}

skip_build=0
deb=0
while [ "$#" -gt 0 ]; do
    case "$1" in
        --skip-build)
            skip_build=1
            ;;
        --deb)
            deb=1
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

if ! command -v podman >/dev/null 2>&1; then
    echo "podman não encontrado; instale podman para rodar o smoke." >&2
    exit 1
fi

if [ "$deb" -eq 1 ]; then
    if [ "$skip_build" -eq 0 ]; then
        scripts/build-deb.sh
    fi
    if [ -n "${PODSTACK_DEB:-}" ]; then
        deb_path="$PODSTACK_DEB"
    else
        deb_path="$(find build/deb -maxdepth 1 -type f -name 'podstack_*.deb' 2>/dev/null \
            | sort | head -1 || true)"
    fi
    if [ -z "$deb_path" ] || [ ! -f "$deb_path" ]; then
        echo "DEB não encontrado. Rode scripts/build-deb.sh ou defina PODSTACK_DEB." >&2
        exit 1
    fi
    deb_dir="$(cd "$(dirname "$deb_path")" && pwd)"
    deb_name="$(basename "$deb_path")"
    image="${PODSTACK_TEST_IMAGE:-ubuntu:24.04}"
    container="podstack-clean-install-deb-$$"
    cleanup() {
        podman rm -f "$container" >/dev/null 2>&1 || true
    }
    trap cleanup EXIT

    echo "== Container de teste: $image ($container) =="
    podman run --name "$container" -d \
        --volume "$deb_dir:/debs:ro,Z" \
        "$image" sleep infinity >/dev/null

    podman exec "$container" bash -euo pipefail -s <<'CONTAINER'
export HOME=/root
export XDG_RUNTIME_DIR=/run/user/0
install -d -m 0700 "$XDG_RUNTIME_DIR"
export DEBIAN_FRONTEND=noninteractive

echo "== Instalando o DEB e dependências =="
apt-get update
apt-get install -y --no-install-recommends ca-certificates appstream /debs/podstack_*.deb

fail=0
check_file() {
    if [ -e "$1" ]; then
        echo "[ok]    $1"
    else
        echo "[falha] $1"
        fail=1
    fi
}
check_exec() {
    if [ -x "$1" ]; then
        echo "[ok]    $1 (executável)"
    else
        echo "[falha] $1 (ausente ou sem permissão)"
        fail=1
    fi
}

echo
echo "== Binários =="
check_exec /usr/bin/podstack
check_exec /usr/bin/podstackd
check_exec /usr/bin/podstack-gui
check_exec /usr/libexec/podstack/podstack-helper

echo
echo "== Unidades systemd, polkit e metainfo =="
check_file /usr/lib/systemd/user/podstack.socket
check_file /usr/lib/systemd/user/podstack.service
check_file /usr/lib/systemd/system/podstack-helper.service
check_file /usr/share/polkit-1/actions/dev.podstack.helper.policy
check_file /usr/share/dbus-1/system.d/dev.podstack.Helper1.conf
check_file /usr/share/applications/dev.podstack.PodStack.desktop
check_file /usr/share/metainfo/dev.podstack.PodStack.metainfo.xml

echo
echo "== AppStream =="
appstreamcli validate --no-net /usr/share/metainfo/dev.podstack.PodStack.metainfo.xml

echo
echo "== Versão =="
podstack --version

echo
echo "== Daemon e socket =="
podman_socket="$XDG_RUNTIME_DIR/podman/podman.sock"
install -d -m 0700 "$XDG_RUNTIME_DIR/podman"
podman system service --timeout=0 "unix://$podman_socket" \
    >"$XDG_RUNTIME_DIR/podman-service.log" 2>&1 &
podman_pid=$!
export PODSTACK_PODMAN_SOCKET="$podman_socket"

podstackd >"$XDG_RUNTIME_DIR/podstackd.log" 2>&1 &
daemon_pid=$!
cleanup_container() {
    kill "$daemon_pid" "$podman_pid" 2>/dev/null || true
    wait "$daemon_pid" 2>/dev/null || true
}
trap cleanup_container EXIT

socket="$XDG_RUNTIME_DIR/podstack/control.sock"
for _ in $(seq 1 100); do
    if [ -S "$socket" ]; then
        break
    fi
    if ! kill -0 "$daemon_pid" 2>/dev/null; then
        echo "[falha] podstackd terminou antes de criar o socket" >&2
        cat "$XDG_RUNTIME_DIR/podstackd.log" >&2 || true
        exit 1
    fi
    sleep 0.1
done
if [ ! -S "$socket" ]; then
    echo "[falha] socket não apareceu: $socket" >&2
    cat "$XDG_RUNTIME_DIR/podstackd.log" >&2 || true
    exit 1
fi
echo "[ok]    socket: $socket"

mode=""
for _ in $(seq 1 20); do
    mode="$(stat -c '%a' "$socket")"
    [ "$mode" = "600" ] && break
    sleep 0.1
done
if [ "$mode" = "600" ]; then
    echo "[ok]    modo do socket: $mode"
else
    echo "[falha] modo do socket: $mode (esperado 600)"
    fail=1
fi

echo
echo "== podstack status =="
for _ in $(seq 1 50); do
    if [ -S "$podman_socket" ]; then
        break
    fi
    sleep 0.1
done
set +e
status_out="$(podstack status 2>&1)"
status_rc=$?
set -e
printf '%s\n' "$status_out"
if [ "$status_rc" -ne 0 ]; then
    if printf '%s\n' "$status_out" | grep -qi 'engine: *unavailable'; then
        echo "[aviso] status sem engine Podman (esperado no container de smoke)."
    else
        echo "[falha] podstack status retornou $status_rc" >&2
        fail=1
    fi
fi

echo
echo "== podstack doctor =="
set +e
doctor_out="$(podstack doctor 2>&1)"
doctor_rc=$?
set -e
printf '%s\n' "$doctor_out"
if [ "$doctor_rc" -ne 0 ]; then
    unexpected="$(printf '%s\n' "$doctor_out" | grep '^\[fail\]' \
        | grep -v 'podman.socket' | grep -vi 'kvm\|virtualiza' || true)"
    if [ -n "$unexpected" ]; then
        echo "[falha] doctor reportou falhas inesperadas:" >&2
        printf '%s\n' "$unexpected" >&2
        fail=1
    else
        echo "[aviso] doctor retornou $doctor_rc; apenas engine/KVM indisponíveis."
    fi
fi

echo
echo "== Resumo =="
echo "DEB: $(dpkg-query -W -f='${Package}-${Version}.${Architecture}' podstack)"
echo "Versão: $(podstack --version)"
echo "Socket: $socket (modo $mode)"
echo "status: rc=$status_rc; doctor: rc=$doctor_rc"
if [ "$fail" -ne 0 ]; then
    echo "Smoke de instalação limpa (DEB): FALHOU" >&2
    exit 1
fi
echo "Smoke de instalação limpa (DEB): OK"
CONTAINER

    echo "Smoke DEB concluído para $deb_name."
    exit 0
fi

if [ "$skip_build" -eq 0 ]; then
    scripts/build-rpm.sh
fi

if [ -n "${PODSTACK_RPM:-}" ]; then
    rpm_path="$PODSTACK_RPM"
else
    rpm_path="$(find build/rpm/RPMS -type f -name 'podstack-*.rpm' \
        ! -name 'podstack-helper-*' 2>/dev/null | sort | head -1 || true)"
fi
if [ -z "$rpm_path" ] || [ ! -f "$rpm_path" ]; then
    echo "RPM não encontrado. Rode scripts/build-rpm.sh ou defina PODSTACK_RPM." >&2
    exit 1
fi
rpm_dir="$(cd "$(dirname "$rpm_path")" && pwd)"
rpm_name="$(basename "$rpm_path")"

image="${PODSTACK_TEST_IMAGE:-fedora:44}"
container="podstack-clean-install-$$"
cleanup() {
    podman rm -f "$container" >/dev/null 2>&1 || true
}
trap cleanup EXIT

echo "== Container de teste: $image ($container) =="
podman run --name "$container" -d \
    --volume "$rpm_dir:/rpms:ro,Z" \
    "$image" sleep infinity >/dev/null

podman exec "$container" bash -euo pipefail -s <<'CONTAINER'
export HOME=/root
export XDG_RUNTIME_DIR=/run/user/0
install -d -m 0700 "$XDG_RUNTIME_DIR"

echo "== Instalando AppStream e os RPMs montados em /rpms =="
dnf install -y --setopt=install_weak_deps=False appstream /rpms/podstack-*.rpm

fail=0
check_file() {
    if [ -e "$1" ]; then
        echo "[ok]    $1"
    else
        echo "[falha] $1"
        fail=1
    fi
}
check_exec() {
    if [ -x "$1" ]; then
        echo "[ok]    $1 (executável)"
    else
        echo "[falha] $1 (ausente ou sem permissão)"
        fail=1
    fi
}

echo
echo "== Binários =="
check_exec /usr/bin/podstack
check_exec /usr/bin/podstackd
check_exec /usr/bin/podstack-gui
check_exec /usr/libexec/podstack/podstack-helper

echo
echo "== Unidades systemd, polkit e metainfo =="
check_file /usr/lib/systemd/user/podstack.socket
check_file /usr/lib/systemd/user/podstack.service
check_file /usr/lib/systemd/system/podstack-helper.service
check_file /usr/share/polkit-1/actions/dev.podstack.helper.policy
check_file /usr/share/dbus-1/system.d/dev.podstack.Helper1.conf
check_file /usr/share/selinux/packages/podstack.pp
check_file /usr/share/applications/dev.podstack.PodStack.desktop
check_file /usr/share/metainfo/dev.podstack.PodStack.metainfo.xml

echo
echo "== AppStream =="
appstreamcli validate --no-net /usr/share/metainfo/dev.podstack.PodStack.metainfo.xml

echo
echo "== Versão =="
podstack --version

echo
echo "== Daemon e socket =="
podman_socket="$XDG_RUNTIME_DIR/podman/podman.sock"
install -d -m 0700 "$XDG_RUNTIME_DIR/podman"
podman system service --timeout=0 "unix://$podman_socket" \
    >"$XDG_RUNTIME_DIR/podman-service.log" 2>&1 &
podman_pid=$!
export PODSTACK_PODMAN_SOCKET="$podman_socket"

podstackd >"$XDG_RUNTIME_DIR/podstackd.log" 2>&1 &
daemon_pid=$!

cleanup_container() {
    kill "$daemon_pid" "$podman_pid" 2>/dev/null || true
    wait "$daemon_pid" 2>/dev/null || true
}
trap cleanup_container EXIT

socket="$XDG_RUNTIME_DIR/podstack/control.sock"
for _ in $(seq 1 100); do
    if [ -S "$socket" ]; then
        break
    fi
    if ! kill -0 "$daemon_pid" 2>/dev/null; then
        echo "[falha] podstackd terminou antes de criar o socket" >&2
        cat "$XDG_RUNTIME_DIR/podstackd.log" >&2 || true
        exit 1
    fi
    sleep 0.1
done
if [ ! -S "$socket" ]; then
    echo "[falha] socket não apareceu: $socket" >&2
    cat "$XDG_RUNTIME_DIR/podstackd.log" >&2 || true
    exit 1
fi
echo "[ok]    socket: $socket"

mode=""
for _ in $(seq 1 20); do
    mode="$(stat -c '%a' "$socket")"
    [ "$mode" = "600" ] && break
    sleep 0.1
done
if [ "$mode" = "600" ]; then
    echo "[ok]    modo do socket: $mode"
else
    echo "[falha] modo do socket: $mode (esperado 600)"
    fail=1
fi

echo
echo "== podstack status =="
for _ in $(seq 1 50); do
    if [ -S "$podman_socket" ]; then
        break
    fi
    sleep 0.1
done
set +e
status_out="$(podstack status 2>&1)"
status_rc=$?
set -e
printf '%s\n' "$status_out"
if [ "$status_rc" -ne 0 ]; then
    if printf '%s\n' "$status_out" | grep -qi 'engine: *unavailable'; then
        echo "[aviso] status sem engine Podman (esperado no container de smoke)."
    else
        echo "[falha] podstack status retornou $status_rc" >&2
        fail=1
    fi
fi

echo
echo "== podstack doctor =="
set +e
doctor_out="$(podstack doctor 2>&1)"
doctor_rc=$?
set -e
printf '%s\n' "$doctor_out"
if [ "$doctor_rc" -ne 0 ]; then
    unexpected="$(printf '%s\n' "$doctor_out" | grep '^\[fail\]' \
        | grep -v 'podman.socket' | grep -vi 'kvm\|virtualiza' || true)"
    if [ -n "$unexpected" ]; then
        echo "[falha] doctor reportou falhas inesperadas:" >&2
        printf '%s\n' "$unexpected" >&2
        fail=1
    else
        echo "[aviso] doctor retornou $doctor_rc; apenas engine/KVM indisponíveis."
    fi
fi

echo
echo "== Resumo =="
echo "RPM: $(rpm -q --qf '%{NAME}-%{VERSION}-%{RELEASE}.%{ARCH}' podstack)"
echo "Versão: $(podstack --version)"
echo "Socket: $socket (modo $mode)"
echo "status: rc=$status_rc; doctor: rc=$doctor_rc"
if [ "$fail" -ne 0 ]; then
    echo "Smoke de instalação limpa: FALHOU" >&2
    exit 1
fi
echo "Smoke de instalação limpa: OK"
CONTAINER

echo "Smoke concluído para $rpm_name."
