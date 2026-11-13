#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Testa o wrapper root-owned do update com um gerenciador falso: estado,
# log JSONL redigido e permissões, sem tocar em pacotes reais.
set -eu

exec_bin="${1:?caminho do podstack-update-exec}"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

mkdir -p "$tmp/bin" "$tmp/state" "$tmp/logs"
cat >"$tmp/bin/apt-get" <<'SCRIPT'
#!/bin/sh
echo "Authorization: Bearer super-secret"
echo "instalando pacote"
exit "${FAKE_MANAGER_EXIT:-0}"
SCRIPT
chmod +x "$tmp/bin/apt-get"

run() {
    PATH="$tmp/bin:$PATH" "$exec_bin" \
        --job-id update-exec-test \
        --uid "$(id -u)" \
        --state-dir "$tmp/state" \
        --log-file "$tmp/logs/$(id -u)/update-exec-test.jsonl" \
        --artifact "$tmp/podstack.deb" \
        --manager apt-get
}

run >"$tmp/out" 2>&1 || {
    echo "FAIL: wrapper falhou no caminho de sucesso" >&2
    cat "$tmp/out" >&2
    exit 1
}
grep -q '"phase":"done"' "$tmp/state/state.json" || {
    echo "FAIL: estado não ficou done" >&2
    cat "$tmp/state/state.json" >&2
    exit 1
}
grep -q '\[redacted\]' "$tmp/logs/$(id -u)/update-exec-test.jsonl" || {
    echo "FAIL: log sem redaction" >&2
    cat "$tmp/logs/$(id -u)/update-exec-test.jsonl" >&2
    exit 1
}
if grep -q 'super-secret' "$tmp/logs/$(id -u)/update-exec-test.jsonl"; then
    echo "FAIL: segredo vazou no log" >&2
    exit 1
fi
mode="$(stat -c '%a' "$tmp/logs/$(id -u)/update-exec-test.jsonl")"
[ "$mode" = "640" ] || {
    echo "FAIL: permissão do log $mode, esperado 640" >&2
    exit 1
}

rm -rf "$tmp/state" "$tmp/logs"
mkdir -p "$tmp/state" "$tmp/logs"
FAKE_MANAGER_EXIT=1 run >"$tmp/out" 2>&1 && {
    echo "FAIL: wrapper deveria falhar com o gerenciador" >&2
    exit 1
}
grep -q '"phase":"failed"' "$tmp/state/state.json" || {
    echo "FAIL: estado não ficou failed" >&2
    cat "$tmp/state/state.json" >&2
    exit 1
}

echo "update exec: ok"
