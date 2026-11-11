#!/usr/bin/env bash
# Smoke test E2E: self-test, smoke-test e verifica finestra reale.
# Richiede Xvfb e il binary jotilla.
set -euo pipefail

# GTK3 leaks (not our code) must not fail the smoke test under sanitizers.
export ASAN_OPTIONS="${ASAN_OPTIONS:+$ASAN_OPTIONS:}detect_leaks=0"
export JOTILLA_NO_SINGLE_INSTANCE=1

fail() {
    echo "FAIL: $*" >&2
    exit 1
}

if [ "$#" -lt 1 ] || [ "$#" -gt 2 ]; then
    echo "Usage: $0 /path/to/jotilla [/path/to/window_probe]" >&2
    exit 2
fi

BIN="$1"
PROBE="${2:-./build/window_probe}"

export GDK_BACKEND=x11
export JOTILLA_NO_SINGLE_INSTANCE=1
unset WAYLAND_DISPLAY

if [ ! -x "$BIN" ]; then
    fail "binary not found or not executable: $BIN"
fi

if [ ! -x "$PROBE" ]; then
    fail "window probe not found or not executable: $PROBE (build with X11 support)"
fi
PROBE="$(readlink -f "$PROBE")"

TMP="$(mktemp -d)"
cleanup() {
    rm -rf "$TMP"
}
trap cleanup EXIT

echo "==> self-test"
xvfb-run -a "$BIN" --self-test \
    --archive "$TMP/notes" \
    --config "$TMP/cfg"
echo "PASS: self-test"

echo "==> smoke-test"
xvfb-run -a timeout 15 "$BIN" --smoke-test \
    --archive "$TMP/notes2" \
    --config "$TMP/cfg2"
echo "PASS: smoke-test exit"

shopt -s nullglob
notes=("$TMP"/notes2/note-*.txt)
if [ "${#notes[@]}" -eq 0 ]; then
    fail "smoke-test did not create a first note in $TMP/notes2"
fi
echo "PASS: smoke-test created ${#notes[@]} note file(s)"

echo "==> real window"
WINDOW_OUT="$TMP/window_titles.txt"
WINDOW_ERR="$TMP/window_stderr.txt"

rc=0
if xvfb-run -a bash -c '
    set -uo pipefail
    app="$1"
    archive="$2"
    cfg="$3"
    probe="$4"

    "$app" --archive "$archive" --config "$cfg" &
    app_pid=$!

    sleep 2

    out=""
    status=1
    tries=0
    # Ritenta il probe per dare tempo al programma di creare la finestra.
    while [ "$tries" -lt 8 ]; do
        if out="$("$probe" 2>/dev/null)" && [ -n "$out" ]; then
            status=0
            break
        fi
        tries=$((tries + 1))
        sleep 0.5
    done

    if [ -n "$out" ]; then
        printf "%s\n" "$out"
    fi

    kill "$app_pid" 2>/dev/null || true
    wait "$app_pid" 2>/dev/null || true
    exit "$status"
' _ "$BIN" "$TMP/notes-window" "$TMP/cfg-window" "$PROBE" \
    >"$WINDOW_OUT" 2>"$WINDOW_ERR"; then
    rc=0
else
    rc=$?
fi

if [ -s "$WINDOW_OUT" ]; then
    echo "window titles observed:"
    sed 's/^/  /' "$WINDOW_OUT"
fi

if [ "$rc" -ne 0 ]; then
    if [ -s "$WINDOW_ERR" ]; then
        echo "--- window test stderr ---" >&2
        sed 's/^/  /' "$WINDOW_ERR" >&2
    fi
    fail "no real window with a title was found (probe exit $rc)"
fi

if ! grep -q '[^[:space:]]' "$WINDOW_OUT"; then
    fail "window probe produced no non-empty title"
fi

echo "PASS: real window created with a non-empty title"

echo "PASS: e2e smoke"
