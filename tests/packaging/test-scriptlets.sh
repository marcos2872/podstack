#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Testa os scriptlets de pacote com systemctl/loginctl/pkill/pgrep falsos.
# Nada na máquina real é parado: o PATH aponta para um sandbox temporário.
set -eu

repo="${1:-$(pwd)}"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

bin="$tmp/bin"
state="$tmp/state"
log="$tmp/log"
mkdir -p "$bin" "$state"
: >"$log"

fail() {
    echo "FAIL: $*" >&2
    echo "--- log ---" >&2
    cat "$log" >&2 || true
    exit 1
}

expect_log() {
    grep -F -- "$1" "$log" >/dev/null || fail "log não contém: $1"
}

expect_no_log() {
    if grep -F -- "$1" "$log" >/dev/null; then
        fail "log não deveria conter: $1"
    fi
}

reset_state() {
    rm -rf "$state"
    mkdir -p "$state"
    : >"$log"
    echo active >"$state/helper"
    for uid in 1000 1001; do
        echo active >"$state/uid-$uid-daemon"
        echo inactive >"$state/uid-$uid-helpersocket"
        : >"$state/uid-$uid-podstack-gui"
        : >"$state/uid-$uid-podstackd"
    done
}

run_script() {
    PATH="$bin:$PATH" \
        FAKE_STATE_DIR="$state" \
        FAKE_LOG="$log" \
        FAKE_GUI_IGNORE_KILL="${FAKE_GUI_IGNORE_KILL:-0}" \
        FAKE_GUI_IGNORE_TERM="${FAKE_GUI_IGNORE_TERM:-0}" \
        FAKE_HELPER_STOP_FAIL="${FAKE_HELPER_STOP_FAIL:-0}" \
        FAKE_HELPER_RESTART_FAIL="${FAKE_HELPER_RESTART_FAIL:-0}" \
        FAKE_HELPER_START_FAIL="${FAKE_HELPER_START_FAIL:-0}" \
        "$@" 2>>"$log"
}

# --- systemctl falso -------------------------------------------------------
cat >"$bin/systemctl" <<'SCRIPT'
#!/bin/sh
printf 'systemctl %s\n' "$*" >>"$FAKE_LOG"
state="$FAKE_STATE_DIR"
uid=""
machine=0
user_mode=0
args=""
while [ $# -gt 0 ]; do
    case "$1" in
        --machine)
            machine=1
            shift
            uid="$(printf '%s' "${1:-}" | sed -n 's/^user@\([0-9][0-9]*\)\.service$/\1/p')"
            ;;
        --user)
            user_mode=1
            ;;
        --runtime|--quiet|--global)
            ;;
        *)
            args="$args $1"
            ;;
    esac
    shift
done

has() {
    for word in $args; do
        [ "$word" = "$1" ] && return 0
    done
    return 1
}

if [ "$user_mode" = 1 ]; then
    [ -n "$uid" ] || exit 1
    if has mask; then
        echo masked >"$state/uid-$uid-mask"
        exit 0
    fi
    if has unmask; then
        echo unmasked >"$state/uid-$uid-mask"
        exit 0
    fi
    if has stop; then
        has podstack.service && echo inactive >"$state/uid-$uid-daemon"
        has podstack.socket && echo inactive >"$state/uid-$uid-socket"
        exit 0
    fi
    if has is-active; then
        [ "$(cat "$state/uid-$uid-daemon" 2>/dev/null || echo inactive)" = active ] && exit 0
        exit 3
    fi
    exit 0
fi

if has is-active; then
    [ "$(cat "$state/helper" 2>/dev/null || echo inactive)" = active ] && exit 0
    exit 3
fi
if has stop; then
    [ "${FAKE_HELPER_STOP_FAIL:-0}" = 1 ] && exit 1
    echo inactive >"$state/helper"
    exit 0
fi
if has restart; then
    [ "${FAKE_HELPER_RESTART_FAIL:-0}" = 1 ] && exit 1
    echo active >"$state/helper"
    exit 0
fi
if has start; then
    [ "${FAKE_HELPER_START_FAIL:-0}" = 1 ] && exit 1
    echo active >"$state/helper"
    exit 0
fi
if has enable; then
    echo active >"$state/helper"
    exit 0
fi
exit 0
SCRIPT

# --- loginctl falso --------------------------------------------------------
cat >"$bin/loginctl" <<'SCRIPT'
#!/bin/sh
printf 'loginctl %s\n' "$*" >>"$FAKE_LOG"
echo "1 1000 alice seat0 tty1"
echo "2 1001 bob seat0 tty2"
SCRIPT

# --- pkill falso -----------------------------------------------------------
cat >"$bin/pkill" <<'SCRIPT'
#!/bin/sh
printf 'pkill %s\n' "$*" >>"$FAKE_LOG"
sig=TERM
uid=""
name=""
while [ $# -gt 0 ]; do
    case "$1" in
        -TERM|-KILL)
            sig="${1#-}"
            ;;
        -u)
            shift
            uid="${1:-}"
            ;;
        -x)
            shift
            name="${1:-}"
            ;;
    esac
    shift
done
file="$FAKE_STATE_DIR/uid-$uid-$name"
if [ "$name" = podstack-gui ] && [ "${FAKE_GUI_IGNORE_KILL:-0}" = 1 ]; then
    : >"$file"
    exit 0
fi
if [ "$sig" = TERM ] && [ "${FAKE_GUI_IGNORE_TERM:-0}" = 1 ] && [ "$name" = podstack-gui ]; then
    : >"$file"
    exit 0
fi
rm -f "$file"
exit 0
SCRIPT

# --- pgrep falso -----------------------------------------------------------
cat >"$bin/pgrep" <<'SCRIPT'
#!/bin/sh
printf 'pgrep %s\n' "$*" >>"$FAKE_LOG"
uid=""
name=""
while [ $# -gt 0 ]; do
    case "$1" in
        -u)
            shift
            uid="${1:-}"
            ;;
        -x)
            shift
            name="${1:-}"
            ;;
    esac
    shift
done
file="$FAKE_STATE_DIR/uid-$uid-$name"
[ -f "$file" ] && exit 0
exit 1
SCRIPT

cat >"$bin/sleep" <<'SCRIPT'
#!/bin/sh
exit 0
SCRIPT
cat >"$bin/semodule" <<'SCRIPT'
#!/bin/sh
exit 0
SCRIPT
cat >"$bin/restorecon" <<'SCRIPT'
#!/bin/sh
exit 0
SCRIPT
chmod +x "$bin"/*

# PATH mínimo sem systemctl para exercitar o fallback sem systemd.
bin_nosd="$tmp/bin-nosd"
mkdir -p "$bin_nosd"
for cmd in loginctl pkill pgrep sleep; do
    ln -s "$bin/$cmd" "$bin_nosd/$cmd"
done
ln -s "$(command -v awk)" "$bin_nosd/awk"
ln -s "$(command -v sort)" "$bin_nosd/sort"
ln -s "$(command -v rm)" "$bin_nosd/rm"
ln -s "$(command -v sed)" "$bin_nosd/sed"

run_nosd() {
    PATH="$bin_nosd" \
        FAKE_STATE_DIR="$state" \
        FAKE_LOG="$log" \
        "$@" 2>>"$log"
}

# --- Debian: preinst encerra app/helper ------------------------------------
reset_state
run_script "$repo/packaging/deb/preinst" upgrade || fail "preinst upgrade falhou"
expect_log "systemctl --machine user@1000.service --user mask --runtime podstack.socket"
expect_log "systemctl --machine user@1001.service --user stop podstack.socket podstack.service"
expect_log "pkill -TERM -u 1000 -x podstack-gui"
expect_log "pkill -TERM -u 1001 -x podstack-gui"
expect_log "systemctl stop podstack-helper.service"
expect_no_log "pkill -TERM -u 1000 -x podstack "

# --- Debian: preinst aborta e restaura se a GUI não morrer ------------------
reset_state
FAKE_GUI_IGNORE_KILL=1 run_script "$repo/packaging/deb/preinst" upgrade && \
    fail "preinst deveria abortar com GUI viva"
grep -F 'a GUI do uid 1000 não encerrou' "$log" >/dev/null || fail "mensagem de aborto ausente"
expect_log "systemctl --machine user@1000.service --user unmask --runtime podstack.socket"
expect_log "systemctl start podstack-helper.service"

# --- Debian: sem systemd usa pkill/pgrep e não toca no runner --------------
reset_state
: >"$state/uid--podstack-gui"
: >"$state/uid--podstackd"
run_nosd "$repo/packaging/deb/preinst" install || fail "preinst sem systemd falhou"
expect_log "pkill -TERM -x podstack-gui"
expect_log "pkill -TERM -x podstackd"
expect_no_log "pkill -TERM -x podstack "

# --- Debian: postinst reinicia o helper e desmascara o socket --------------
reset_state
echo inactive >"$state/helper"
run_script "$repo/packaging/deb/postinst" configure || fail "postinst falhou"
expect_log "systemctl --global enable podstack.socket"
expect_log "systemctl restart podstack-helper.service"
expect_log "systemctl --machine user@1000.service --user unmask --runtime podstack.socket"
expect_log "systemctl --machine user@1001.service --user unmask --runtime podstack.socket"

# --- Debian: postinst falha alto quando não consegue religar o helper ------
reset_state
echo inactive >"$state/helper"
FAKE_HELPER_RESTART_FAIL=1 run_script "$repo/packaging/deb/postinst" configure && \
    fail "postinst deveria falhar com restart do helper quebrado"
expect_log "systemctl start podstack-helper.service"
grep -F 'falha ao religar o helper' "$log" >/dev/null || fail "remediação do helper ausente"

# --- Debian: postrm abort-upgrade restaura helper/socket -------------------
reset_state
run_script "$repo/packaging/deb/postrm" abort-upgrade || fail "postrm abort-upgrade falhou"
expect_log "systemctl restart podstack-helper.service"
expect_log "systemctl --machine user@1000.service --user unmask --runtime podstack.socket"

# --- Debian: prerm remove encerra o app e desabilita o helper --------------
reset_state
run_script "$repo/packaging/deb/prerm" remove || fail "prerm remove falhou"
expect_log "systemctl --machine user@1000.service --user stop podstack.socket podstack.service"
expect_log "pkill -TERM -u 1000 -x podstack-gui"
expect_log "systemctl disable --now podstack-helper.service"

# --- RPM: extrai %pre/%post/%posttrans e executa no sandbox ----------------
extract_section() {
    awk -v section="$1" '
        $0 == "%" section { active = 1; next }
        /^%[a-z]/ { active = 0 }
        active { print }
    ' "$repo/packaging/rpm/podstack.spec"
}

pre_script="$tmp/rpm-pre.sh"
post_script="$tmp/rpm-post.sh"
posttrans_script="$tmp/rpm-posttrans.sh"
extract_section pre >"$pre_script"
extract_section post >"$post_script"
extract_section posttrans >"$posttrans_script"
[ -s "$pre_script" ] || fail "não foi possível extrair %pre"
[ -s "$post_script" ] || fail "não foi possível extrair %post"
[ -s "$posttrans_script" ] || fail "não foi possível extrair %posttrans"

helper="$tmp/podstack-helper"
cat >"$helper" <<'SCRIPT'
#!/bin/sh
echo "0.1.3"
SCRIPT
chmod +x "$helper"
sed -i \
    -e "s#/usr/libexec/podstack/podstack-helper#$helper#g" \
    -e "s/%{_datadir}/\/usr\/share/g" \
    -e "s/%{version}/0.1.3/g" \
    "$post_script" "$posttrans_script"

reset_state
run_script /bin/sh "$pre_script" 2 || fail "%pre do RPM falhou"
expect_log "systemctl --machine user@1000.service --user mask --runtime podstack.socket"
expect_log "pkill -TERM -u 1000 -x podstack-gui"

reset_state
echo inactive >"$state/helper"
run_script /bin/sh "$post_script" 1 || fail "%post do RPM falhou"
expect_log "systemctl restart podstack-helper.service"
expect_log "systemctl --machine user@1000.service --user unmask --runtime podstack.socket"

reset_state
echo inactive >"$state/helper"
run_script /bin/sh "$posttrans_script" || fail "%posttrans do RPM falhou"
expect_log "systemctl restart podstack-helper.service"
expect_log "systemctl --machine user@1001.service --user unmask --runtime podstack.socket"
expect_log "is-active --quiet podstack-helper.service"

reset_state
echo inactive >"$state/helper"
FAKE_HELPER_RESTART_FAIL=1 FAKE_HELPER_START_FAIL=1 \
    run_script /bin/sh "$posttrans_script" && fail "%posttrans deveria falhar sem helper"
grep -F 'falha ao restaurar o helper' "$log" >/dev/null || \
    fail "mensagem de falha do %posttrans ausente"

# --- sintaxe dos scripts ---------------------------------------------------
for script in "$repo"/packaging/deb/preinst "$repo"/packaging/deb/postinst \
    "$repo"/packaging/deb/prerm "$repo"/packaging/deb/postrm; do
    sh -n "$script" || fail "sintaxe inválida em $script"
done
sh -n "$pre_script" || fail "sintaxe inválida no %pre"
sh -n "$post_script" || fail "sintaxe inválida no %post"
sh -n "$posttrans_script" || fail "sintaxe inválida no %posttrans"

echo "package scriptlets: ok"
