#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."

cmake --preset dev
cmake --build --preset dev

SOCKET="${XDG_RUNTIME_DIR:-/tmp}/podstack/control.sock"
DAEMON_PID=""

if [ ! -S "$SOCKET" ]; then
    ./build/dev/podstackd --socket "$SOCKET" &
    DAEMON_PID=$!
fi

cleanup() {
    if [ -n "$DAEMON_PID" ]; then
        kill "$DAEMON_PID" 2>/dev/null || true
    fi
}
trap cleanup EXIT

sleep 0.3
./build/dev/podstack-gui "$@"
