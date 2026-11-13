#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Smoke test E2E do roteiro J1 (docs/sdd/testing.md §14): sobe um projeto Compose
# com nginx:alpine, valida a porta publicada por HTTP, os logs e o down.
#
# Opt-in: PODSTACK_E2E=1. Sem esse valor o script apenas explica como rodar.
# Requer daemon (podstackd) e Podman funcionando; se faltar algo, sai 0 (skip).

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PODSTACK="$ROOT/build/dev/podstack"

if [[ "${PODSTACK_E2E:-0}" != "1" ]]; then
    cat <<'EOF'
E2E J1 desabilitado.

Para rodar o smoke test com Podman/daemon reais:
  cmake --build --preset dev
  PODSTACK_E2E=1 scripts/e2e-smoke.sh

O script cria e remove um projeto Compose temporário (nginx:alpine) em uma
porta livre de 127.0.0.1; não usa sudo nem altera configurações do usuário.
EOF
    exit 0
fi

skip() {
    echo "SKIP: $*" >&2
    exit 0
}

fail() {
    echo "ERRO: $*" >&2
    exit 1
}

[[ -x "$PODSTACK" ]] || fail "binário não encontrado em $PODSTACK (rode: cmake --build --preset dev)"
command -v curl >/dev/null 2>&1 || skip "curl não encontrado"
command -v python3 >/dev/null 2>&1 || skip "python3 não encontrado"
command -v podman >/dev/null 2>&1 || skip "Podman não encontrado"

if command -v docker-compose >/dev/null 2>&1; then
    :
elif podman compose version >/dev/null 2>&1; then
    :
elif command -v docker >/dev/null 2>&1 && docker compose version >/dev/null 2>&1; then
    :
else
    skip "nenhum provider Compose encontrado (docker-compose, podman compose ou docker compose)"
fi

status_output="$("$PODSTACK" status 2>&1)" || skip "daemon/Podman indisponível: ${status_output//$'\n'/ }"
if ! grep -q '^estado: ok' <<<"$status_output"; then
    skip "daemon/Podman não está pronto: ${status_output//$'\n'/ }"
fi

free_port() {
    python3 - <<'PY'
import socket

sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
sock.bind(("127.0.0.1", 0))
print(sock.getsockname()[1])
sock.close()
PY
}

project_dir="$(mktemp -d "${TMPDIR:-/tmp}/podstack-e2e.XXXXXX")"
project_name=""
port="$(free_port)"
up_done=0

cleanup() {
    local code=$?
    if [[ "$up_done" == "1" ]]; then
        "$PODSTACK" compose down "$project_dir" >/dev/null 2>&1 || true
    fi
    rm -rf "$project_dir"
    exit "$code"
}
trap cleanup EXIT

cat >"$project_dir/compose.yaml" <<EOF
services:
  web:
    image: nginx:alpine
    ports:
      - "127.0.0.1:${port}:80"
EOF

echo "==> Compose up (${project_dir}, porta ${port})"
up_done=1
"$PODSTACK" compose up "$project_dir"

# O nome do projeto é derivado do diretório pelo provider; lê do compose ls
# para não duplicar a normalização de nomes.
for _ in $(seq 1 20); do
    project_name="$("$PODSTACK" compose ls 2>/dev/null \
        | awk -v dir="$project_dir" 'index($0, dir) { print $1; exit }')"
    [[ -n "$project_name" ]] && break
    sleep 0.5
done
[[ -n "$project_name" ]] || fail "projeto Compose não apareceu em 'podstack compose ls'"

echo "==> Aguardando http://127.0.0.1:${port}/"
body=""
deadline=$((SECONDS + 180))
while (( SECONDS < deadline )); do
    if body="$(curl -fsS --max-time 5 "http://127.0.0.1:${port}/" 2>/dev/null)"; then
        break
    fi
    sleep 2
done
[[ -n "$body" ]] || fail "a porta ${port} não respondeu HTTP em 180 s"
grep -qi 'nginx' <<<"$body" || fail "resposta HTTP não parece ser do nginx"

echo "==> Logs do serviço web"
# compose logs segue o stream (tail 200); o timeout encerra após capturar o tail.
log_output="$(timeout 10 "$PODSTACK" compose logs "$project_name" web 2>&1 || true)"
[[ -n "$log_output" ]] || fail "'compose logs' não retornou linhas"
grep -q 'GET /' <<<"$log_output" || {
    printf '%s\n' "$log_output" | tail -n 20 >&2
    fail "'compose logs' não contém a requisição HTTP"
}

echo "==> Compose down"
"$PODSTACK" compose down "$project_dir"
up_done=0

echo "OK: E2E J1 concluído (projeto ${project_name}, porta ${port})"
