#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."

BUILD_DIR="build/dev"
if [ ! -f "$BUILD_DIR/build.ninja" ]; then
    cmake --preset dev
fi

if [ "${PODSTACK_SKIP_BUILD:-0}" != "1" ]; then
    cmake --build --preset dev
fi

SOCKET="${PODSTACK_CONTROL_SOCKET:-${XDG_RUNTIME_DIR:-/tmp}/podstack/control.sock}"
export PODSTACK_CONTROL_SOCKET="$SOCKET"

DAEMON_PID=""
STATUS_OUTPUT="$(./build/dev/podstack status 2>/dev/null || true)"

if ! grep -q '^estado:' <<<"$STATUS_OUTPUT"; then
    rm -f "$SOCKET"
    ./build/dev/podstackd --socket "$SOCKET" &
    DAEMON_PID=$!

    for _ in $(seq 1 100); do
        if [ -S "$SOCKET" ]; then
            break
        fi
        sleep 0.05
    done
fi

cleanup() {
    if [ -n "$DAEMON_PID" ]; then
        kill "$DAEMON_PID" 2>/dev/null || true
        wait "$DAEMON_PID" 2>/dev/null || true
    fi
}
trap cleanup EXIT

./build/dev/podstack-gui "$@"
