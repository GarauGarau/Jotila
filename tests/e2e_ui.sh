#!/usr/bin/env bash
# Test UI E2E interattivo via XTest: digitazione, collapse, archivio, nuova nota.
# Richiede Xvfb e il binary xui.
set -euo pipefail

# Interactive X11 UI end-to-end test driven through XTest (tests/xui).
#
# Usage: tests/e2e_ui.sh [jotilla-binary] [xui-binary]
# Defaults: build/jotilla and build/xui (or xui next to the jotilla binary).

BIN="${1:-build/jotilla}"
XUI="${2:-}"

FAILED=0
XVFB_PID=""
APP_PID=""
TMP=""

pass() {
    echo "PASS: $*"
}

fail() {
    echo "FAIL: $*" >&2
    FAILED=$((FAILED + 1))
}

cleanup() {
    if [ -n "${APP_PID}" ] && kill -0 "${APP_PID}" 2>/dev/null; then
        kill "${APP_PID}" 2>/dev/null || true
        wait "${APP_PID}" 2>/dev/null || true
    fi
    if [ -n "${XVFB_PID}" ] && kill -0 "${XVFB_PID}" 2>/dev/null; then
        kill "${XVFB_PID}" 2>/dev/null || true
        wait "${XVFB_PID}" 2>/dev/null || true
    fi
    if [ -n "${TMP}" ]; then
        rm -rf "${TMP}"
    fi
}
trap cleanup EXIT

if [ ! -x "${BIN}" ]; then
    echo "FAIL: jotilla binary not found or not executable: ${BIN}" >&2
    exit 2
fi
BIN="$(readlink -f "${BIN}")"

if [ -z "${XUI}" ]; then
    XUI="$(dirname "${BIN}")/xui"
fi
if [ ! -x "${XUI}" ]; then
    echo "FAIL: xui binary not found or not executable: ${XUI}" >&2
    exit 2
fi
XUI="$(readlink -f "${XUI}")"

TMP="$(mktemp -d)"
_disp=90
while [ -e "/tmp/.X${_disp}-lock" ] || [ -e "/tmp/.X11-unix/X${_disp}" ]; do
    _disp=$((_disp + 1))
done
export DISPLAY=":${_disp}"
export GDK_BACKEND=x11
export JOTILLA_NO_SINGLE_INSTANCE=1
unset WAYLAND_DISPLAY

echo "==> UI E2E: bin=${BIN} xui=${XUI}"

# ---------------------------------------------------------------- Xvfb
Xvfb "${DISPLAY}" -screen 0 1280x1024x24 -nolisten tcp >"${TMP}/xvfb.log" 2>&1 &
XVFB_PID=$!

ready=0
for _ in $(seq 1 50); do
    if [ -S /tmp/.X11-unix/X${DISPLAY#:} ] && xset q >/dev/null 2>&1; then
        ready=1
        break
    fi
    sleep 0.1
done
if [ "${ready}" -ne 1 ]; then
    echo "FAIL: Xvfb ${DISPLAY} did not become ready" >&2
    cat "${TMP}/xvfb.log" >&2 || true
    exit 1
fi
pass "Xvfb ${DISPLAY} started"

# ------------------------------------------------------- helpers
wait_find() {
    local pattern="$1"
    local timeout="${2:-5}"
    local out=""
    local tries=$((timeout * 10))
    local i

    for ((i = 0; i < tries; ++i)); do
        if out="$("${XUI}" find "${pattern}" 2>/dev/null)"; then
            printf '%s\n' "${out}"
            return 0
        fi
        sleep 0.1
    done
    return 1
}

wait_find_gone() {
    local pattern="$1"
    local timeout="${2:-5}"
    local tries=$((timeout * 10))
    local i

    for ((i = 0; i < tries; ++i)); do
        if ! "${XUI}" find "${pattern}" >/dev/null 2>&1; then
            return 0
        fi
        sleep 0.1
    done
    return 1
}

note_txt_count() {
    find "${TMP}/notes" -maxdepth 1 -type f -name 'note-*.txt' 2>/dev/null | wc -l
}

note_contains() {
    grep -r -q -F --include='note-*.txt' -- "$1" "${TMP}/notes" 2>/dev/null
}

# ------------------------------------------------------- launch
"${BIN}" --archive "${TMP}/notes" --config "${TMP}/cfg" \
    >"${TMP}/app.log" 2>&1 &
APP_PID=$!

if ! line="$(wait_find 'Untitled' 5)"; then
    fail "note window 'Untitled' did not appear within 5s"
    cat "${TMP}/app.log" >&2 || true
    exit 1
fi
read -r _ WIN X Y W H TITLE <<<"${line}"
pass "note window found (win=${WIN} ${X} ${Y} ${W} ${H})"

# ------------------------------------------------- type into body
"${XUI}" click "${WIN}" $((X + W / 2)) $((Y + H / 2))
sleep 2.0
"${XUI}" type "${WIN}" 'hello ui test'
sleep 3.0

if note_contains 'hello ui test'; then
    pass "typed text persisted to archive"
else
    fail "typed text not found in any note-*.txt"
fi

if line="$(wait_find 'hello ui test' 2)"; then
    pass "note title updated to typed first line"
else
    fail "note title did not update to 'hello ui test'"
fi

# ------------------------------------------------------- collapse
if line="$(wait_find 'hello ui test' 2)"; then
    read -r _ WIN X Y W H TITLE <<<"${line}"
    "${XUI}" click "${WIN}" $((X + W - 52)) $((Y + 15))
    sleep 0.6
    if line="$(wait_find 'hello ui test' 2)"; then
        read -r _ WIN X Y W H TITLE <<<"${line}"
        if [ "${H}" -le 40 ]; then
            pass "collapse reduced height to ${H}"
        else
            fail "collapse left height at ${H} (> 40)"
        fi
    else
        fail "note window disappeared after collapse"
    fi

    "${XUI}" click "${WIN}" $((X + W - 52)) $((Y + 15))
    sleep 0.6
    if line="$(wait_find 'hello ui test' 2)"; then
        read -r _ WIN X Y W H TITLE <<<"${line}"
        if [ "${H}" -ge 100 ]; then
            pass "expand restored height to ${H}"
        else
            fail "expand restored height only to ${H} (< 100); app overwrites the expanded height with the collapsed height"
        fi
    else
        fail "note window disappeared after expand"
    fi
else
    fail "could not re-locate note window for collapse test"
fi

# --------------------------------------------------- archive shortcut
if line="$(wait_find 'hello ui test' 2)"; then
    read -r _ WIN X Y W H TITLE <<<"${line}"
else
    fail "could not re-locate note window for archive shortcut"
fi
"${XUI}" click "${WIN}" $((X + W / 2)) $((Y + H / 2))
sleep 1.0
"${XUI}" ctrlkey "${WIN}" e

if line="$(wait_find 'Archive' 5)"; then
    read -r _ AWIN AX AY AW AH ATITLE <<<"${line}"
    pass "ctrl+e opened archive window (win=${AWIN})"

    # Riaprire una nota gia' aperta deve riusare la finestra esistente.
    "${XUI}" key "${AWIN}" Down || true
    sleep 0.5
    "${XUI}" key "${AWIN}" Return || true
    sleep 1.5
    reopen_count="$("${XUI}" find 'hello ui test' | wc -l | tr -d ' ')"
    if [ "${reopen_count}" -eq 1 ]; then
        pass "reopening an open note reused its window"
    else
        fail "reopening an open note created ${reopen_count} windows"
    fi

    # No window manager: close the archive through its client-side header
    # button so it does not keep the process alive during the close test.
    "${XUI}" click "${AWIN}" $((AX + AW - 16)) $((AY + 20)) || true
    sleep 1.0
    if wait_find_gone 'Archive' 2; then
        pass "archive window closed"
    else
        fail "could not close archive window"
    fi
else
    fail "ctrl+e did not open an Archive window within 5s"
fi

# -------------------------------------------------------- new note
if line="$(wait_find 'hello ui test' 2)"; then
    read -r _ WIN X Y W H TITLE <<<"${line}"
    "${XUI}" click "${WIN}" $((X + W / 2)) $((Y + H / 2))
    sleep 1.0
    "${XUI}" ctrlkey "${WIN}" n
else
    fail "could not focus note window for ctrl+n"
fi

if line="$(wait_find 'Untitled' 5)"; then
    read -r _ NWIN NX NY NW NH NTITLE <<<"${line}"
    sleep 1.0
    count="$(note_txt_count)"
    if [ "${count}" -ge 2 ]; then
        pass "ctrl+n created a second note (${count} note files)"
    else
        fail "ctrl+n did not create a second note (${count} note files)"
    fi

    # Title promotion: # first line + Shift+Enter leaves the note body.
    "${XUI}" click "${NWIN}" $((NX + NW / 2)) $((NY + NH / 2))
    sleep 0.3
    "${XUI}" type "${NWIN}" '#Promoted Title'
    "${XUI}" key "${NWIN}" Return 1
    sleep 1.0
    if wait_find 'Promoted Title' 3; then
        pass "Shift+Enter promoted the # first line to the title bar"
    else
        fail "title promotion did not update the title bar"
    fi
    if note_contains '#Promoted Title'; then
        pass "promoted title persisted as the TXT # header"
    else
        fail "promoted title header missing from TXT"
    fi

    # Close the extra window so the following close test can observe the
    # process lifetime without a lingering note window.
    "${XUI}" key "${NWIN}" Escape
    if wait_find_gone 'Untitled' 3; then
        pass "extra new-note window closed"
    else
        fail "extra new-note window did not close"
    fi
else
    fail "ctrl+n did not open a new note window within 5s"
fi

# ----------------------------------------------------------- close
if line="$(wait_find 'hello ui test' 2)"; then
    read -r _ WIN X Y W H TITLE <<<"${line}"
    "${XUI}" click "${WIN}" $((X + W - 18)) $((Y + 15))

    exited=0
    for _ in $(seq 1 50); do
        if ! kill -0 "${APP_PID}" 2>/dev/null; then
            exited=1
            break
        fi
        sleep 0.1
    done

    if [ "${exited}" -eq 1 ]; then
        pass "process exited after closing the last note window"
        APP_PID=""
    else
        fail "process still running 5s after closing the last note window (no gtk_main_quit on last-window destroy)"
    fi

    if note_contains 'hello ui test'; then
        pass "note text persisted after close"
    else
        fail "note text missing after close"
    fi
else
    fail "could not locate note window for close test"
fi

if [ -n "${APP_PID}" ] && kill -0 "${APP_PID}" 2>/dev/null; then
    kill "${APP_PID}" 2>/dev/null || true
    wait "${APP_PID}" 2>/dev/null || true
    APP_PID=""
fi

# --------------------------------------------------------- relaunch
"${BIN}" --archive "${TMP}/notes" --config "${TMP}/cfg" \
    >"${TMP}/app-relaunch.log" 2>&1 &
APP_PID=$!

if line="$(wait_find 'Untitled' 5)"; then
    pass "relaunch created a new note when no notes were open"
else
    fail "relaunch did not create a new note after all notes were closed"
fi

if note_contains 'hello ui test'; then
    pass "closed note text remains in the archive"
else
    fail "closed note text disappeared from the archive"
fi

"${XUI}" find 'hello ui test' >/dev/null 2>&1 || true

if [ "${FAILED}" -eq 0 ]; then
    echo "ALL UI E2E STEPS PASSED"
    exit 0
fi

echo "${FAILED} UI E2E step(s) FAILED" >&2
if [ -s "${TMP}/app.log" ]; then
    echo "--- app log ---" >&2
    cat "${TMP}/app.log" >&2
fi
exit 1
