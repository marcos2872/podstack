#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Benchmark reproduzível de desempenho do PodStack (TX-09 / NFR-PERF).
#
# Mede:
#   1. startup do processo CLI (`podstack version`);
#   2. latência de `podstack ps` contra um daemon (existente ou temporário);
#   3. RSS do `podstackd` após alguns segundos ocioso (/proc/<pid>/status VmRSS);
#   4. primeiro paint da GUI em modo offscreen (Qt painting log).
#
# As metas vêm de docs/PRD.md §8.1 e docs/testing.md §8:
#   - startup da GUI (cold): <= 1,0 s
#   - listagem de containers: <= 700 ms (usada como referência para o `ps`)
#   - memória em repouso: <= 180 MB RSS (a meta cobre GUI + daemon; aqui só o daemon)
#
# Uso:
#   scripts/bench.sh [--runs N] [--json] [--idle-seconds S]
#
# Não requer root, não instala nada e limpa o daemon/socket temporários no trap.

set -euo pipefail

umask 077
export LC_NUMERIC=C

ROOT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
BIN_DIR=${PODSTACK_BIN_DIR:-$ROOT_DIR/build/dev}
PODSTACK_BIN=$BIN_DIR/podstack
PODSTACKD_BIN=$BIN_DIR/podstackd
PODSTACK_GUI_BIN=$BIN_DIR/podstack-gui

RUNS=5
IDLE_SECONDS=10
EMIT_JSON=0
JSON_PATH=$ROOT_DIR/build/bench-results.json

META_GUI_MS=1000
META_PS_MS=700
META_RSS_MB=180

usage() {
    cat <<'EOF'
Benchmark reproduzível do PodStack (TX-09).

Opções:
  --runs N            Execuções cronometradas por métrica (default: 5).
  --idle-seconds S    Espera antes de amostrar o RSS do daemon (default: 10;
                      docs/testing.md §8 usa 60 s).
  --json              Também grava build/bench-results.json.
  -h, --help          Mostra esta ajuda.

Variáveis:
  PODSTACK_BIN_DIR          Diretório dos binários (default: build/dev).
  PODSTACK_CONTROL_SOCKET   Socket de um daemon já em execução (opcional).
EOF
}

die() {
    echo "bench: $*" >&2
    exit 1
}

while (($#)); do
    case $1 in
        --runs)
            (($# >= 2)) || die "--runs exige um valor"
            RUNS=$2
            shift 2
            ;;
        --idle-seconds)
            (($# >= 2)) || die "--idle-seconds exige um valor"
            IDLE_SECONDS=$2
            shift 2
            ;;
        --json)
            EMIT_JSON=1
            shift
            ;;
        -h | --help)
            usage
            exit 0
            ;;
        *)
            echo "bench: opção desconhecida: $1" >&2
            usage >&2
            exit 2
            ;;
    esac
done

[[ $RUNS =~ ^[0-9]+$ ]] && ((RUNS >= 1 && RUNS <= 100)) ||
    die "--runs deve ser um inteiro entre 1 e 100"
[[ $IDLE_SECONDS =~ ^[0-9]+$ ]] && ((IDLE_SECONDS >= 0 && IDLE_SECONDS <= 600)) ||
    die "--idle-seconds deve ser um inteiro entre 0 e 600"

[[ -x $PODSTACK_BIN ]] || die "não encontrado: $PODSTACK_BIN (rode cmake --build --preset dev)"
[[ -x $PODSTACKD_BIN ]] || die "não encontrado: $PODSTACKD_BIN (rode cmake --build --preset dev)"

bench_tmp=""
daemon_pid=""
daemon_mode="none"
daemon_socket=""
daemon_reason=""
daemon_podman_socket=""
gui_pid=""

cleanup() {
    local rc=$?
    trap - EXIT INT TERM

    if [[ -n $gui_pid ]] && kill -0 "$gui_pid" 2>/dev/null; then
        kill "$gui_pid" 2>/dev/null || true
        wait "$gui_pid" 2>/dev/null || true
    fi

    if [[ -n $daemon_pid ]] && kill -0 "$daemon_pid" 2>/dev/null; then
        kill "$daemon_pid" 2>/dev/null || true
        for _ in {1..30}; do
            kill -0 "$daemon_pid" 2>/dev/null || break
            sleep 0.1
        done
        kill -9 "$daemon_pid" 2>/dev/null || true
        wait "$daemon_pid" 2>/dev/null || true
    fi

    if [[ -n $bench_tmp && -d $bench_tmp ]]; then
        rm -rf "$bench_tmp"
    fi

    exit "$rc"
}
trap cleanup EXIT INT TERM

bench_tmp=$(mktemp -d "${TMPDIR:-/tmp}/podstack-bench.XXXXXX")

# ---------------------------------------------------------------------------
# Ambiente (capturado antes de isolar XDG_* para o daemon temporário)
# ---------------------------------------------------------------------------

ORIG_RUNTIME_DIR=${XDG_RUNTIME_DIR:-}
ENV_HOSTNAME=$(hostname 2>/dev/null || uname -n)
ENV_CPU=$(awk -F': ' '/model name/ { print $2; exit }' /proc/cpuinfo 2>/dev/null || true)
[[ -n $ENV_CPU ]] || ENV_CPU="desconhecido"
ENV_NPROC=$(nproc 2>/dev/null || echo "?")
ENV_MEM=$(free -h 2>/dev/null | awk '/^Mem:/ { print $2 }' || true)
[[ -n $ENV_MEM ]] || ENV_MEM="?"
ENV_KERNEL=$(uname -r)
if [[ -r /etc/fedora-release ]]; then
    ENV_FEDORA=$(head -n 1 /etc/fedora-release)
else
    ENV_FEDORA=$( ( . /etc/os-release 2>/dev/null && printf '%s' "${PRETTY_NAME:-desconhecido}" ) || echo "desconhecido")
fi
ENV_SESSION_TYPE=${XDG_SESSION_TYPE:-desconhecido}
ENV_DESKTOP=${XDG_CURRENT_DESKTOP:-desconhecido}
ENV_LOAD=$(awk '{ print $1 ", " $2 ", " $3 }' /proc/loadavg 2>/dev/null || echo "?")
ENV_BUILD_TYPE=$(sed -n 's/^CMAKE_BUILD_TYPE:STRING=//p' "$ROOT_DIR/build/dev/CMakeCache.txt" 2>/dev/null | head -n 1)
[[ -n $ENV_BUILD_TYPE ]] || ENV_BUILD_TYPE="desconhecido"
ENV_PODMAN=$(podman --version 2>/dev/null | head -n 1 || true)
[[ -n $ENV_PODMAN ]] || ENV_PODMAN="indisponível"

# ---------------------------------------------------------------------------
# Daemon: usa um já em execução ou sobe um temporário e isolado
# ---------------------------------------------------------------------------

default_control_socket() {
    if [[ -n ${PODSTACK_CONTROL_SOCKET:-} ]]; then
        printf '%s' "$PODSTACK_CONTROL_SOCKET"
    elif [[ -n $ORIG_RUNTIME_DIR ]]; then
        printf '%s/podstack/control.sock' "$ORIG_RUNTIME_DIR"
    fi
}

probe_daemon() {
    local socket=$1
    [[ -S $socket ]] || return 1
    timeout 5 env PODSTACK_CONTROL_SOCKET="$socket" "$PODSTACK_BIN" ps --format json \
        >/dev/null 2>&1
}

find_podman_socket() {
    if [[ -n ${PODSTACK_PODMAN_SOCKET:-} ]]; then
        printf '%s' "$PODSTACK_PODMAN_SOCKET"
    elif [[ -n $ORIG_RUNTIME_DIR && -S $ORIG_RUNTIME_DIR/podman/podman.sock ]]; then
        printf '%s/podman/podman.sock' "$ORIG_RUNTIME_DIR"
    elif [[ -S /run/podman/podman.sock ]]; then
        printf '%s' "/run/podman/podman.sock"
    fi
}

pid_for_socket() {
    local socket=$1
    local pid fd target inode start best="" best_start=0
    local listening_inodes=" "
    [[ -S $socket ]] || return 1

    # /proc/net/unix lista o inode sockfs (não o inode do arquivo devolvido por
    # stat). O path pode ter sido reutilizado por vários daemons ao longo do
    # tempo, então consideramos apenas sockets em LISTEN (estado 01) e, entre
    # eles, o processo mais recente — o dono atual do path.
    while read -r inode; do
        if [[ -n $inode ]]; then
            listening_inodes+="$inode "
        fi
    done < <(awk -v path="$socket" '$6 == "01" && $8 == path { print $7 }' /proc/net/unix 2>/dev/null)

    [[ $listening_inodes != " " ]] || return 1

    for pid in $(pgrep -x podstackd 2>/dev/null || true); do
        for fd in /proc/"$pid"/fd/*; do
            target=$(readlink "$fd" 2>/dev/null || true)
            [[ $target == socket:* ]] || continue
            inode=${target#socket:[}
            inode=${inode%]}
            if [[ $listening_inodes == *" $inode "* ]]; then
                start=$(awk '{ print $22 }' "/proc/$pid/stat" 2>/dev/null || echo 0)
                if ((start > best_start)); then
                    best=$pid
                    best_start=$start
                fi
                break
            fi
        done
    done

    [[ -n $best ]] || return 1
    printf '%s' "$best"
}

cli_env=()
daemon_ready=0

existing_socket=$(default_control_socket)
if [[ -n $existing_socket ]] && probe_daemon "$existing_socket"; then
    daemon_mode="existing"
    daemon_socket=$existing_socket
    daemon_pid=$(pid_for_socket "$daemon_socket" || true)
    cli_env=("PODSTACK_CONTROL_SOCKET=$daemon_socket")
    daemon_ready=1
    if [[ -z $daemon_pid ]]; then
        daemon_reason="daemon respondeu no socket, mas o PID não foi localizado"
    fi
else
    daemon_mode="ephemeral"
    mkdir -p "$bench_tmp/run" "$bench_tmp/config" "$bench_tmp/data" \
        "$bench_tmp/state" "$bench_tmp/cache"
    chmod 700 "$bench_tmp/run"
    daemon_socket="$bench_tmp/run/podstack/control.sock"
    daemon_podman_socket=$(find_podman_socket)

    daemon_env=(
        "XDG_RUNTIME_DIR=$bench_tmp/run"
        "XDG_CONFIG_HOME=$bench_tmp/config"
        "XDG_DATA_HOME=$bench_tmp/data"
        "XDG_STATE_HOME=$bench_tmp/state"
        "XDG_CACHE_HOME=$bench_tmp/cache"
        "PODSTACK_CONTROL_SOCKET=$daemon_socket"
    )
    if [[ -n $daemon_podman_socket ]]; then
        daemon_env+=("PODSTACK_PODMAN_SOCKET=$daemon_podman_socket")
    fi

    env "${daemon_env[@]}" "$PODSTACKD_BIN" --socket "$daemon_socket" \
        >"$bench_tmp/podstackd.log" 2>&1 &
    daemon_pid=$!

    for _ in {1..100}; do
        if [[ -S $daemon_socket ]]; then
            daemon_ready=1
            break
        fi
        if ! kill -0 "$daemon_pid" 2>/dev/null; then
            daemon_reason=$(tail -n 3 "$bench_tmp/podstackd.log" 2>/dev/null | tr '\n' ' ')
            break
        fi
        sleep 0.1
    done

    if ((daemon_ready)); then
        cli_env=("PODSTACK_CONTROL_SOCKET=$daemon_socket")
        if [[ -n $daemon_podman_socket ]]; then
            cli_env+=("PODSTACK_PODMAN_SOCKET=$daemon_podman_socket")
        fi
        sleep 0.3
    else
        # Mantém o PID para o trap encerrar um daemon que ficou vivo sem socket.
        [[ -n $daemon_reason ]] || daemon_reason="socket não apareceu em 10 s"
    fi
fi

# ---------------------------------------------------------------------------
# Coleta
# ---------------------------------------------------------------------------

now_ns() {
    date +%s%N
}

run_cli() {
    env "${cli_env[@]}" "$@"
}

# Um aquecimento por métrica (não contabilizado) e depois N amostras.
version_samples=()
version_reason=""
run_cli "$PODSTACK_BIN" version >/dev/null 2>&1 || true
for ((i = 1; i <= RUNS; i++)); do
    start=$(now_ns)
    if run_cli timeout 5 "$PODSTACK_BIN" version >/dev/null 2>&1; then
        end=$(now_ns)
        version_samples+=($(((end - start) / 1000000)))
    else
        version_reason="podstack version falhou"
        break
    fi
done

ps_samples=()
ps_reason=""
ps_errors=0
if ((daemon_ready)); then
    if run_cli timeout 10 "$PODSTACK_BIN" ps --format json >/dev/null 2>&1; then
        for ((i = 1; i <= RUNS; i++)); do
            start=$(now_ns)
            if run_cli timeout 10 "$PODSTACK_BIN" ps --format json >/dev/null 2>&1; then
                end=$(now_ns)
                ps_samples+=($(((end - start) / 1000000)))
            else
                ps_errors=$((ps_errors + 1))
            fi
        done
        if ((${#ps_samples[@]} == 0)); then
            ps_reason="podstack ps falhou em todas as execuções"
        fi
    else
        ps_reason=$(run_cli timeout 10 "$PODSTACK_BIN" ps --format json 2>&1 >/dev/null |
            head -n 2 | tr '\n' ' ' || true)
        [[ -n $ps_reason ]] || ps_reason="podstack ps falhou (daemon sem Podman?)"
    fi
else
    ps_reason=${daemon_reason:-"daemon indisponível"}
fi

rss_samples=()
rss_reason=""
if ((daemon_ready)) && [[ -n $daemon_pid ]] && kill -0 "$daemon_pid" 2>/dev/null; then
    if ((IDLE_SECONDS > 0)); then
        sleep "$IDLE_SECONDS"
    fi
    for ((i = 1; i <= RUNS; i++)); do
        rss_kb=$(awk '/^VmRSS:/ { print $2 }' "/proc/$daemon_pid/status" 2>/dev/null || true)
        if [[ -z $rss_kb ]]; then
            rss_reason="daemon encerrou durante a medição"
            break
        fi
        rss_samples+=($(awk -v kb="$rss_kb" 'BEGIN { printf "%.1f", kb / 1024 }'))
        if ((i < RUNS)); then
            sleep 1
        fi
    done
else
    rss_reason=${daemon_reason:-"PID do daemon indisponível"}
fi

gui_samples=()
gui_reason=""

measure_gui_once() {
    local dir fifo pid start end line now ms
    dir=$(mktemp -d "$bench_tmp/gui.XXXXXX")
    fifo=$dir/log.fifo
    mkfifo "$fifo"

    start=$(now_ns)
    env \
        "XDG_RUNTIME_DIR=$dir" \
        "XDG_CONFIG_HOME=$bench_tmp/gui-config" \
        "XDG_DATA_HOME=$bench_tmp/gui-data" \
        "XDG_STATE_HOME=$bench_tmp/gui-state" \
        "XDG_CACHE_HOME=$bench_tmp/gui-cache" \
        "PODSTACK_CONTROL_SOCKET=${daemon_socket:-$dir/control.sock}" \
        QT_QPA_PLATFORM=offscreen \
        QT_FORCE_STDERR_LOGGING=1 \
        QT_LOGGING_RULES='qt.widgets.painting=true' \
        "$PODSTACK_GUI_BIN" >/dev/null 2>"$fifo" &
    pid=$!
    gui_pid=$pid
    ms=""

    while IFS= read -r -t 15 line; do
        if [[ $line == *"Flushing top level"* ]]; then
            end=$(now_ns)
            ms=$(((end - start) / 1000000))
            break
        fi
        now=$(now_ns)
        if (((now - start) / 1000000 > 15000)); then
            break
        fi
    done <"$fifo"

    kill "$pid" 2>/dev/null || true
    wait "$pid" 2>/dev/null || true
    gui_pid=""
    rm -rf "$dir"

    if [[ -n $ms ]]; then
        printf '%s' "$ms"
    fi
}

if [[ -x $PODSTACK_GUI_BIN ]]; then
    mkdir -p "$bench_tmp/gui-config" "$bench_tmp/gui-data" "$bench_tmp/gui-state" \
        "$bench_tmp/gui-cache"
    # Aquece o cache QML fora das amostras; cada execução continua sendo um
    # processo novo (startup "cold" de processo, cache de disco quente).
    measure_gui_once >/dev/null 2>&1 || true
    for ((i = 1; i <= RUNS; i++)); do
        ms=$(measure_gui_once || true)
        if [[ -n $ms ]]; then
            gui_samples+=("$ms")
        fi
    done
    if ((${#gui_samples[@]} == 0)); then
        gui_reason="sem 1º paint em 15 s no modo offscreen"
    fi
else
    gui_reason="binário podstack-gui ausente"
fi

# ---------------------------------------------------------------------------
# Estatísticas
# ---------------------------------------------------------------------------

stats_min() {
    printf '%s\n' "$@" | LC_ALL=C sort -n | head -n 1
}

stats_max() {
    printf '%s\n' "$@" | LC_ALL=C sort -n | tail -n 1
}

stats_median() {
    printf '%s\n' "$@" | LC_ALL=C sort -n | awk '
        { value[NR] = $1 }
        END {
            if (NR == 0) exit 1
            if (NR % 2 == 1) {
                printf "%.6g", value[(NR + 1) / 2]
            } else {
                printf "%.6g", (value[NR / 2] + value[NR / 2 + 1]) / 2
            }
        }'
}

compare_le() {
    awk -v value="$1" -v meta="$2" 'BEGIN { print (value <= meta) ? "OK" : "ACIMA" }'
}

version_median="" version_min="" version_max="" version_status="informativo"
if ((${#version_samples[@]})); then
    version_median=$(stats_median "${version_samples[@]}")
    version_min=$(stats_min "${version_samples[@]}")
    version_max=$(stats_max "${version_samples[@]}")
fi

ps_median="" ps_min="" ps_max="" ps_status="não medido"
if ((${#ps_samples[@]})); then
    ps_median=$(stats_median "${ps_samples[@]}")
    ps_min=$(stats_min "${ps_samples[@]}")
    ps_max=$(stats_max "${ps_samples[@]}")
    ps_status=$(compare_le "$ps_median" "$META_PS_MS")
fi

rss_median="" rss_min="" rss_max="" rss_status="não medido"
if ((${#rss_samples[@]})); then
    rss_median=$(stats_median "${rss_samples[@]}")
    rss_min=$(stats_min "${rss_samples[@]}")
    rss_max=$(stats_max "${rss_samples[@]}")
    rss_status=$(compare_le "$rss_median" "$META_RSS_MB")
fi

gui_median="" gui_min="" gui_max="" gui_status="não medido"
if ((${#gui_samples[@]})); then
    gui_median=$(stats_median "${gui_samples[@]}")
    gui_min=$(stats_min "${gui_samples[@]}")
    gui_max=$(stats_max "${gui_samples[@]}")
    gui_status=$(compare_le "$gui_median" "$META_GUI_MS")
fi

# ---------------------------------------------------------------------------
# Saída legível
# ---------------------------------------------------------------------------

echo "== Ambiente =="
printf '  Host:      %s\n' "$ENV_HOSTNAME"
printf '  CPU:       %s (%s threads)\n' "$ENV_CPU" "$ENV_NPROC"
printf '  Memória:   %s\n' "$ENV_MEM"
printf '  Kernel:    %s\n' "$ENV_KERNEL"
printf '  Distro:    %s\n' "$ENV_FEDORA"
printf '  Sessão:    %s (%s)\n' "$ENV_SESSION_TYPE" "$ENV_DESKTOP"
printf '  Podman:    %s\n' "$ENV_PODMAN"
printf '  Build:     %s\n' "$ENV_BUILD_TYPE"
printf '  Carga:     %s\n' "$ENV_LOAD"
echo

if [[ $daemon_mode == "existing" ]]; then
    printf 'Daemon: existente (pid %s, socket %s)\n' "${daemon_pid:-?}" "$daemon_socket"
elif [[ $daemon_mode == "ephemeral" ]]; then
    if ((daemon_ready)); then
        printf 'Daemon: temporário (pid %s, socket %s)\n' "${daemon_pid:-?}" "$daemon_socket"
        if [[ -n $daemon_podman_socket ]]; then
            printf 'Podman: %s\n' "$daemon_podman_socket"
        fi
    else
        printf 'Daemon: falhou ao iniciar (%s)\n' "${daemon_reason:-erro desconhecido}"
    fi
else
    printf 'Daemon: indisponível (%s)\n' "${daemon_reason:-erro desconhecido}"
fi
echo

printf '== Resultados (%d amostras por métrica) ==\n' "$RUNS"
printf '%-36s %11s %11s %11s %12s %s\n' \
    "Métrica" "Mediana" "Mín" "Máx" "Meta" "Status"

if ((${#version_samples[@]})); then
    printf '%-36s %11s %11s %11s %12s %s\n' \
        "podstack version (startup CLI)" \
        "$(printf '%.0f ms' "$version_median")" \
        "$(printf '%.0f ms' "$version_min")" \
        "$(printf '%.0f ms' "$version_max")" \
        "—" "informativo"
else
    printf '%-36s não medido (%s)\n' "podstack version (startup CLI)" "${version_reason:-falhou}"
fi

if ((${#ps_samples[@]})); then
    printf '%-36s %11s %11s %11s %12s %s\n' \
        "podstack ps (latência RPC)" \
        "$(printf '%.0f ms' "$ps_median")" \
        "$(printf '%.0f ms' "$ps_min")" \
        "$(printf '%.0f ms' "$ps_max")" \
        "≤ ${META_PS_MS} ms" "$ps_status"
    if ((ps_errors > 0)); then
        printf '  (%d execução(ões) de ps falharam e foram ignoradas)\n' "$ps_errors"
    fi
else
    printf '%-36s não medido (%s)\n' "podstack ps (latência RPC)" "${ps_reason:-falhou}"
fi

if ((${#rss_samples[@]})); then
    printf '%-36s %11s %11s %11s %12s %s\n' \
        "RSS do podstackd (${IDLE_SECONDS}s ocioso)" \
        "$(printf '%.1f MB' "$rss_median")" \
        "$(printf '%.1f MB' "$rss_min")" \
        "$(printf '%.1f MB' "$rss_max")" \
        "≤ ${META_RSS_MB} MB" "$rss_status"
else
    printf '%-36s não medido (%s)\n' "RSS do podstackd" "${rss_reason:-falhou}"
fi

if ((${#gui_samples[@]})); then
    printf '%-36s %11s %11s %11s %12s %s\n' \
        "GUI offscreen (1º paint)" \
        "$(printf '%.0f ms' "$gui_median")" \
        "$(printf '%.0f ms' "$gui_min")" \
        "$(printf '%.0f ms' "$gui_max")" \
        "≤ ${META_GUI_MS} ms" "$gui_status"
else
    printf '%-36s não medido (%s)\n' "GUI offscreen (1º paint)" "${gui_reason:-falhou}"
fi
echo

cat <<EOF
== Limitações ==
  - Hardware do host não é o de referência (docs/testing.md §10); os números
    servem como linha de base local, não como fechamento isolado da TX-09.
  - GUI medida no modo offscreen (software) até o primeiro paint; não
    representa Wayland/GPU nem a meta de startup "cold" em hardware de referência.
  - RSS mede apenas o podstackd; o NFR-PERF-003 cobre GUI + daemon.
  - RSS amostrado após ${IDLE_SECONDS}s; docs/testing.md §8 especifica 60 s
    (use --idle-seconds 60 para reproduzir o método do documento).
  - RSS usa ${RUNS} amostras de 1 s após a espera; o daemon não é reiniciado
    entre elas. version/ps/GUI têm 1 aquecimento não contabilizado.
  - A meta de ${META_PS_MS} ms vem do NFR-PERF-002 (listagem de containers) e é
    usada como referência para a latência RPC do CLI.
  - ps mede o estado atual do host, sem criar containers; listas grandes
    (ex.: 1.000 containers) não são exercitadas.
  - Build local: ${ENV_BUILD_TYPE}; números de um build Debug não são
    comparáveis a um pacote de release.
EOF

# ---------------------------------------------------------------------------
# JSON
# ---------------------------------------------------------------------------

json_str() {
    local value=${1-}
    value=${value//\\/\\\\}
    value=${value//\"/\\\"}
    value=${value//$'\n'/\\n}
    value=${value//$'\r'/}
    value=${value//$'\t'/\\t}
    printf '"%s"' "$value"
}

json_number_array() {
    local first=1 value
    printf '['
    for value in "$@"; do
        ((first)) || printf ', '
        first=0
        printf '%s' "$value"
    done
    printf ']'
}

result_json() {
    local measured=$1 reason=$2 median=$3 min=$4 max=$5 status=$6
    shift 6
    if [[ $measured == "true" ]]; then
        printf '{"measured": true, "samples": %s, "median": %s, "min": %s, "max": %s, "status": %s}' \
            "$(json_number_array "$@")" "$median" "$min" "$max" "$(json_str "$status")"
    else
        printf '{"measured": false, "reason": %s}' "$(json_str "$reason")"
    fi
}

version_json=$(result_json "$([[ ${#version_samples[@]} -gt 0 ]] && echo true || echo false)" \
    "${version_reason:-}" "$version_median" "$version_min" "$version_max" "$version_status" \
    "${version_samples[@]}")
ps_json=$(result_json "$([[ ${#ps_samples[@]} -gt 0 ]] && echo true || echo false)" \
    "${ps_reason:-}" "$ps_median" "$ps_min" "$ps_max" "$ps_status" "${ps_samples[@]}")
rss_json=$(result_json "$([[ ${#rss_samples[@]} -gt 0 ]] && echo true || echo false)" \
    "${rss_reason:-}" "$rss_median" "$rss_min" "$rss_max" "$rss_status" "${rss_samples[@]}")
gui_json=$(result_json "$([[ ${#gui_samples[@]} -gt 0 ]] && echo true || echo false)" \
    "${gui_reason:-}" "$gui_median" "$gui_min" "$gui_max" "$gui_status" "${gui_samples[@]}")

if ((EMIT_JSON)); then
    mkdir -p "$(dirname "$JSON_PATH")"
    cat >"$JSON_PATH" <<EOF
{
  "tool": "podstack-bench",
  "generatedAt": "$(date -Iseconds)",
  "runs": $RUNS,
  "idleSeconds": $IDLE_SECONDS,
  "environment": {
    "hostname": $(json_str "$ENV_HOSTNAME"),
    "cpu": $(json_str "$ENV_CPU"),
    "nproc": ${ENV_NPROC:-0},
    "memory": $(json_str "$ENV_MEM"),
    "kernel": $(json_str "$ENV_KERNEL"),
    "distro": $(json_str "$ENV_FEDORA"),
    "sessionType": $(json_str "$ENV_SESSION_TYPE"),
    "desktop": $(json_str "$ENV_DESKTOP"),
    "podman": $(json_str "$ENV_PODMAN"),
    "buildType": $(json_str "$ENV_BUILD_TYPE"),
    "loadAverage": $(json_str "$ENV_LOAD")
  },
  "targets": {
    "guiFirstPaintMs": $META_GUI_MS,
    "psMs": $META_PS_MS,
    "daemonRssMb": $META_RSS_MB
  },
  "results": {
    "cliVersionMs": $version_json,
    "psMs": $ps_json,
    "daemonRssMb": $rss_json,
    "guiFirstPaintMs": $gui_json
  },
  "daemon": {
    "mode": $(json_str "$daemon_mode"),
    "ready": $([[ $daemon_ready -eq 1 ]] && echo true || echo false),
    "pid": ${daemon_pid:-null},
    "socket": $(json_str "$daemon_socket"),
    "podmanSocket": $(json_str "$daemon_podman_socket"),
    "error": $(json_str "$daemon_reason")
  },
  "limitations": [
    "Hardware do host não é o de referência (docs/testing.md §10).",
    "GUI medida em offscreen (software) até o primeiro paint; não representa Wayland/GPU.",
    "RSS mede apenas o podstackd; o NFR-PERF-003 cobre GUI + daemon.",
    "RSS amostrado após ${IDLE_SECONDS}s; docs/testing.md §8 especifica 60 s.",
    "RSS usa ${RUNS} amostras de 1 s após a espera; o daemon não é reiniciado entre elas.",
    "version/ps/GUI têm 1 aquecimento não contabilizado.",
    "A meta de ${META_PS_MS} ms é do NFR-PERF-002 (listagem de containers), usada como referência para o ps.",
    "ps mede o estado atual do host, sem criar containers; listas grandes não são exercitadas.",
    "Build local: ${ENV_BUILD_TYPE}."
  ]
}
EOF
    echo
    echo "JSON gravado em: $JSON_PATH"
fi
