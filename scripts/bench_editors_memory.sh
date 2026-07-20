#!/usr/bin/env bash
# Benchmarks peak memory usage of different terminal text editors when
# opening files of varying sizes.
#
# Measures PEAK MEMORY: the highest memory usage observed for the editor
# process (and any children) from launch through settling (first paint, then
# a stability window -- same detection idea as scripts/bench_editors.sh),
# read from the kernel's own accounting rather than sampled/estimated:
#   - Primary: cgroup memory.peak for a systemd --user --scope wrapping the
#     editor -- a monotonic high-water-mark the kernel already tracks,
#     covers the editor and any children, no polling race to miss a spike.
#   - Fallback: /proc/<pid>/status VmHWM (also kernel-tracked, but only for
#     the one process) when systemd-run --user isn't available/functional.
#
# SAFETY: this script's whole point is to push editors toward high memory
# use, so containment is load-bearing, not optional hardening:
#   - Every launch is capped with a systemd cgroup MemoryMax (see
#     --mem-limit) so a single editor can never consume unbounded memory
#     and take down the whole machine -- it gets cleanly OOM-killed inside
#     its own cgroup instead.
#   - Generated test files are written to real disk (default /home/nathnael/dev/C/neo-text-editor/bench_tmp, not
#     /tmp, which is commonly tmpfs/RAM-backed on modern systemd machines)
#     so simply *generating* a huge file can't already exhaust RAM.
#   - A pre-flight size check (--max-test-bytes) refuses implausibly large
#     requests unless you pass --force (which does NOT bypass --mem-limit).
#
# Standalone script by design -- no shared code with scripts/bench_editors.sh
# (some duplication of the editor-registry/tmux-launch plumbing is accepted
# in exchange for each script being simple to reason about on its own).
#
# Usage:
#   ./scripts/bench_editors_memory.sh [options]
#
# Examples:
#   ./scripts/bench_editors_memory.sh
#   ./scripts/bench_editors_memory.sh --editors neo,vim,nano --sizes 100000,1000000
#   ./scripts/bench_editors_memory.sh --file /path/to/real/file.log
#   ./scripts/bench_editors_memory.sh --mem-limit 8G --sizes 20000000 --force
#   ./scripts/bench_editors_memory.sh --list
#
# Requires: tmux, awk, mktemp. bash >= 4 (associative arrays). systemd-run
# --user is optional but strongly recommended -- see the safety note above.

set -uo pipefail

# ============================================================================
# Registered editors -- add more here. Key = display name, value = command
# used to launch it (the file path is appended as the final argument).
# ============================================================================
declare -A EDITORS=(
    [neo]="./bin/neo"
    [vim]="vim"
    # [nvim]="nvim"
    # [nano]="nano"
    # [emacs]="emacs -nw"
    # [micro]="micro"
    # [helix]="hx"
    # [joe]="joe"
    # [kakoune]="kak"
    # [mle]="mle"
)

# ============================================================================
# Defaults (overridable via flags)
# ============================================================================
ITERATIONS=5
TIMEOUT_SECS=30
READY_TIMEOUT_SECS=""  # defaults to TIMEOUT_SECS if left unset by --ready-timeout
STABLE_COUNT=5          # consecutive identical polls required to call it "settled"
STABLE_INTERVAL=0.05    # seconds between polls while waiting for stability
PANE_WIDTH=120
PANE_HEIGHT=40
SIZES="100000,1000000,10000000"
FILE=""
ONLY_EDITORS=""
KEEP_FILES=0
LIST_ONLY=0
EXTRA_EDITOR_ARGS=()
SCRATCH_DIR="/home/nathnael/dev/C/neo-text-editor/bench_tmp"
MEM_LIMIT="4G"          # systemd MemoryMax= syntax (e.g. "4G", "512M"); "0"/"" disables containment
MAX_TEST_BYTES=$((2 * 1024 * 1024 * 1024)) # 2 GiB
FORCE=0

SCRIPT_NAME="$(basename "$0")"
TMP_FILES=()
CURRENT_SESSION=""
CURRENT_UNIT=""
SYSTEMD_AVAILABLE=0
MEMORY_SWAP_MAX_SUPPORTED=0

usage() {
    cat <<EOF
Usage: $SCRIPT_NAME [options]

Options:
  --editors LIST         Comma-separated editor names to run (default: all registered that are found)
  --sizes LIST           Comma-separated line counts to generate and test (default: $SIZES)
  --file PATH            Benchmark this one file instead of generating synthetic files
  --iterations N         Runs per (editor, file) pair, reported as min/avg/max (default: $ITERATIONS)
  --timeout SECONDS      Max seconds to wait for first paint before marking TIMEOUT (default: $TIMEOUT_SECS)
  --ready-timeout SECS   Max additional seconds (after first paint) to wait for the screen to settle
                         before giving up and reading whatever peak was observed (default: same as --timeout)
  --stable-count N       Consecutive identical polls required to call the screen "settled" (default: $STABLE_COUNT)
  --stable-interval SECS Seconds between polls while waiting for stability (default: $STABLE_INTERVAL)
  --width COLS           tmux pane width (default: $PANE_WIDTH)
  --height ROWS          tmux pane height (default: $PANE_HEIGHT)
  --add "name=command"   Register an extra editor for this run (repeatable)
  --list                 List registered editors and whether they're found on this machine, then exit
  --keep-files           Don't delete generated synthetic test files afterward
  --scratch-dir PATH     Real-disk directory for generated test files (default: $SCRATCH_DIR -- avoid
                         tmpfs-backed paths like /tmp, or generating a huge file directly eats RAM)
  --mem-limit SIZE       Per-editor cgroup memory cap, systemd MemoryMax= syntax (default: $MEM_LIMIT).
                         "0" disables containment (NOT recommended -- see the safety note at the top
                         of this script). This is the guardrail that keeps a runaway editor from being
                         able to take down the whole machine.
  --max-test-bytes N     Refuse to test a file estimated/measured over this many bytes unless --force
                         is also passed (default: $MAX_TEST_BYTES)
  --force                Bypass the --max-test-bytes refusal. Does NOT bypass --mem-limit.
  -h, --help             Show this help

Examples:
  $SCRIPT_NAME
  $SCRIPT_NAME --editors neo,vim,nano --sizes 100000,1000000
  $SCRIPT_NAME --file /var/log/syslog
  $SCRIPT_NAME --mem-limit 8G --sizes 20000000 --force
EOF
}

log() { printf '%s\n' "$*" >&2; }
die() {
    log "Error: $*"
    exit 1
}

cleanup() {
    [ -n "$CURRENT_SESSION" ] && tmux kill-session -t "$CURRENT_SESSION" >/dev/null 2>&1
    if [ -n "$CURRENT_UNIT" ] && [ "$SYSTEMD_AVAILABLE" -eq 1 ]; then
        systemctl --user kill --signal=SIGKILL "${CURRENT_UNIT}.scope" >/dev/null 2>&1
        systemctl --user stop "${CURRENT_UNIT}.scope" >/dev/null 2>&1
        systemctl --user reset-failed "${CURRENT_UNIT}.scope" >/dev/null 2>&1
    fi
    if [ "$KEEP_FILES" -eq 0 ]; then
        local f
        for f in "${TMP_FILES[@]:-}"; do
            [ -n "$f" ] && rm -f "$f"
        done
    fi
}
trap cleanup EXIT
# Without an explicit exit, bash resumes the script after an INT/TERM trap
# handler returns instead of actually terminating -- which previously meant
# a Ctrl-C or `timeout`-sent SIGTERM mid-run would clean up (deleting the
# in-progress test file out from under the still-running rest of the
# script) and then keep going, hitting confusing "file not found" errors
# instead of actually stopping. The explicit exit here makes INT/TERM behave
# like every user actually expects: clean up, then stop.
trap 'cleanup; exit 130' INT
trap 'cleanup; exit 143' TERM

# ============================================================================
# Argument parsing
# ============================================================================
while [ $# -gt 0 ]; do
    case "$1" in
    --editors)
        ONLY_EDITORS="$2"
        shift 2
        ;;
    --sizes)
        SIZES="$2"
        shift 2
        ;;
    --file)
        FILE="$2"
        shift 2
        ;;
    --iterations)
        ITERATIONS="$2"
        shift 2
        ;;
    --timeout)
        TIMEOUT_SECS="$2"
        shift 2
        ;;
    --ready-timeout)
        READY_TIMEOUT_SECS="$2"
        shift 2
        ;;
    --stable-count)
        STABLE_COUNT="$2"
        shift 2
        ;;
    --stable-interval)
        STABLE_INTERVAL="$2"
        shift 2
        ;;
    --width)
        PANE_WIDTH="$2"
        shift 2
        ;;
    --height)
        PANE_HEIGHT="$2"
        shift 2
        ;;
    --add)
        EXTRA_EDITOR_ARGS+=("$2")
        shift 2
        ;;
    --list)
        LIST_ONLY=1
        shift
        ;;
    --keep-files)
        KEEP_FILES=1
        shift
        ;;
    --scratch-dir)
        SCRATCH_DIR="$2"
        shift 2
        ;;
    --mem-limit)
        MEM_LIMIT="$2"
        shift 2
        ;;
    --max-test-bytes)
        MAX_TEST_BYTES="$2"
        shift 2
        ;;
    --force)
        FORCE=1
        shift
        ;;
    -h | --help)
        usage
        exit 0
        ;;
    *)
        die "Unknown argument: $1 (see --help)"
        ;;
    esac
done

command -v tmux >/dev/null 2>&1 || die "tmux is required but not found on PATH"
command -v awk >/dev/null 2>&1 || die "awk is required but not found on PATH"

[ -z "$READY_TIMEOUT_SECS" ] && READY_TIMEOUT_SECS="$TIMEOUT_SECS"

# Register any --add editors (may override a built-in name on purpose).
for kv in "${EXTRA_EDITOR_ARGS[@]:-}"; do
    [ -z "$kv" ] && continue
    name="${kv%%=*}"
    cmdline="${kv#*=}"
    [ "$name" = "$kv" ] && die "--add expects name=command, got: $kv"
    EDITORS["$name"]="$cmdline"
done

# ============================================================================
# Helpers used by --list (defined early so the early-exit below can use them,
# before paying for the systemd probe / scratch-dir setup that --list doesn't need)
# ============================================================================

# Prints the first token of a command string (the actual binary to look up).
editor_binary() { awk '{print $1}' <<<"$1"; }

editor_available() {
    local bin
    bin="$(editor_binary "$1")"
    case "$bin" in
    ./* | /*) [ -x "$bin" ] ;;
    *) command -v "$bin" >/dev/null 2>&1 ;;
    esac
}

list_editors() {
    printf '%-12s %-20s %s\n' "NAME" "COMMAND" "AVAILABLE"
    for name in "${!EDITORS[@]}"; do
        if editor_available "${EDITORS[$name]}"; then
            printf '%-12s %-20s %s\n' "$name" "${EDITORS[$name]}" "yes"
        else
            printf '%-12s %-20s %s\n' "$name" "${EDITORS[$name]}" "no"
        fi
    done | sort
}

if [ "$LIST_ONLY" -eq 1 ]; then
    list_editors
    exit 0
fi

mkdir -p -- "$SCRATCH_DIR" || die "Could not create scratch directory: $SCRATCH_DIR"

# ============================================================================
# systemd-run --user availability probe (once, at startup)
# ============================================================================
if command -v systemd-run >/dev/null 2>&1 && systemctl --user show-environment >/dev/null 2>&1; then
    SYSTEMD_AVAILABLE=1
    if systemd-run --user --scope -p MemorySwapMax=0 --quiet -- true >/dev/null 2>&1; then
        MEMORY_SWAP_MAX_SUPPORTED=1
    else
        log "Warning: this system's systemd/cgroup setup doesn't accept MemorySwapMax --"
        log "  a capped editor could still spill into swap before being OOM-killed."
    fi
else
    log "Warning: systemd-run --user is not available/functional on this machine."
    log "  Peak-memory measurement falls back to polling /proc/<pid>/status VmHWM"
    log "  (less precise: misses any child processes an editor spawns), AND memory"
    log "  containment (--mem-limit) is DISABLED -- an editor can consume unbounded"
    log "  memory. --max-test-bytes is now your PRIMARY safeguard; consider lowering it."
fi

# ============================================================================
# Remaining helpers
# ============================================================================

check_not_tmpfs() {
    local dir="$1" fstype
    fstype="$(df -PT "$dir" 2>/dev/null | awk 'NR==2{print $2}')"
    case "$fstype" in
    tmpfs | ramfs)
        log "Warning: --scratch-dir '$dir' is on a RAM-backed filesystem ($fstype)."
        log "  Large test files will consume system RAM directly just by existing."
        log "  Point --scratch-dir at real disk (e.g. /home/nathnael/dev/C/neo-text-editor/bench_tmp is usually safe)."
        ;;
    esac
}
check_not_tmpfs "$SCRATCH_DIR"

# Generates a synthetic text file with $1 lines, first non-blank line used
# later as the "has it rendered yet" marker.
generate_file() {
    local lines="$1" path="$2"
    awk -v n="$lines" 'BEGIN {
        print "BENCHMARK_MARKER_START_OF_FILE";
        for (i = 1; i < n; i++) {
            printf "int variable_%d = %d; // some filler comment about the value\n", i, i;
        }
    }' >"$path"
}

# First non-empty line of a file, truncated to a safe length for a reliable
# (and wrap-resistant) grep target regardless of editor gutter width.
first_line_marker() {
    awk 'NF { print; exit }' "$1" | cut -c1-40
}

# Estimates the byte size generate_file() would produce for $1 lines, matching
# its actual output shape (fixed 33-byte marker line, then per-line width
# that grows slightly with the digit count of the line number). Deliberately
# conservative (rounds up) since this feeds a safety gate, not a report.
estimate_generated_bytes() {
    local lines="$1"
    awk -v n="$lines" 'BEGIN {
        if (n < 1) { print 33; exit }
        digits = length(n)
        per_line = 60 + digits
        printf "%.0f\n", 33 + (n - 1) * per_line
    }'
}

# Refuses (unless --force) when bytes exceeds --max-test-bytes. NOTE:
# --mem-limit containment stays fully active regardless of --force -- this
# is a "did you mean to do this?" guard, not the mechanism that actually
# prevents a whole-machine crash (that's --mem-limit).
check_size_or_die() {
    local label="$1" bytes="$2"
    if [ "$bytes" -gt "$MAX_TEST_BYTES" ] && [ "$FORCE" -ne 1 ]; then
        die "$label is ~$bytes bytes, over --max-test-bytes ($MAX_TEST_BYTES). Use a" \
            " smaller --sizes/--file, raise --max-test-bytes, or pass --force" \
            " (--mem-limit containment still applies either way)."
    fi
}

# Human-readable byte count ("12.3M", "1.4G", ...). Passes non-numeric input
# (e.g. "OOM"/"TIMEOUT" status words) straight through unchanged.
human_bytes() {
    awk -v b="$1" 'BEGIN {
        if (b == "" || b !~ /^[0-9]+$/) { print b; exit }
        split("B K M G T", units, " ")
        i = 1
        while (b >= 1024 && i < 5) { b /= 1024; i++ }
        printf (i == 1) ? "%d%s\n" : "%.1f%s\n", b, units[i]
    }'
}

# ============================================================================
# Memory measurement core
# ============================================================================

# Builds the argv used to launch the editor. `exec` replaces the wrapping
# bash with the editor process itself, so cgroup/process accounting
# attributes memory to the actual editor (and so the PID we track for the
# VmHWM fallback is the editor's own PID, not an intermediate shell's).
# Wraps in systemd-run's cgroup memory containment when available.
build_launch_argv() {
    local -n out="$1"
    local cmd="$2" file="$3" unit="$4"
    if [ "$SYSTEMD_AVAILABLE" -eq 1 ] && [ -n "$MEM_LIMIT" ] && [ "$MEM_LIMIT" != "0" ]; then
        out=(systemd-run --user --scope -u "$unit" -p "MemoryMax=$MEM_LIMIT")
        [ "$MEMORY_SWAP_MAX_SUPPORTED" -eq 1 ] && out+=(-p "MemorySwapMax=0")
        out+=(-- bash -c "exec $cmd \"\$0\"" "$file")
    else
        out=(bash -c "exec $cmd \"\$0\"" "$file")
    fi
}

# True if the given unit's scope was killed by the cgroup OOM killer.
detect_oom() {
    local unit="$1" result
    [ "$SYSTEMD_AVAILABLE" -eq 1 ] || return 1
    result="$(systemctl --user show "${unit}.scope" -p Result --value 2>/dev/null)"
    [ "$result" = "oom-kill" ]
}

reset_unit() {
    local unit="$1"
    [ "$SYSTEMD_AVAILABLE" -eq 1 ] || return 0
    systemctl --user reset-failed "${unit}.scope" >/dev/null 2>&1
}

# Prints the current peak-memory reading (bytes) for this run, or nothing on
# failure. Uses cgroup memory.peak when available -- a monotonic
# kernel-tracked high-water-mark covering the editor and any children, safe
# to read repeatedly or just once near the end. Falls back to
# /proc/<pid>/status VmHWM (also kernel-tracked, but single-process only and
# which disappears the instant the process exits, so the fallback path polls
# this every iteration rather than relying on one read at the end).
read_current_peak() {
    local unit="$1" pid="$2" val=""
    if [ "$SYSTEMD_AVAILABLE" -eq 1 ]; then
        # The scope may not be registered with systemd yet in the brief
        # window right after tmux new-session returns (systemd-run itself
        # has to start and do a D-Bus round-trip before the cgroup exists) --
        # retry briefly rather than silently reporting a false "0" peak for
        # very fast-exiting editors caught in that window.
        local cgroup_path attempt
        for attempt in 1 2 3; do
            cgroup_path="$(systemctl --user show "${unit}.scope" -p ControlGroup --value 2>/dev/null)"
            [ -n "$cgroup_path" ] && break
            sleep 0.02
        done
        [ -n "$cgroup_path" ] || return 1
        val="$(cat "/sys/fs/cgroup${cgroup_path}/memory.peak" 2>/dev/null)"
    elif [ -n "$pid" ]; then
        local kb
        kb="$(awk '/^VmHWM:/{print $2}' "/proc/$pid/status" 2>/dev/null)"
        [ -n "$kb" ] && val=$((kb * 1024))
    fi
    [ -n "$val" ] || return 1
    echo "$val"
}

# Runs one (editor, file) load and prints the observed peak memory in bytes,
# or "TIMEOUT" (never rendered at all) / "OOM" (killed by the memory cap
# before finishing) to stdout. Cleans up its own tmux session + systemd
# scope before returning.
time_one_load_memory() {
    local cmd="$1" file="$2" marker="$3"
    local session unit
    session="benchmem_$$_${RANDOM}_${RANDOM}"
    unit="$session"
    CURRENT_SESSION="$session"
    CURRENT_UNIT="$unit"

    local -a launch_argv=()
    build_launch_argv launch_argv "$cmd" "$file" "$unit"

    tmux new-session -d -s "$session" -x "$PANE_WIDTH" -y "$PANE_HEIGHT" -- \
        "${launch_argv[@]}" >/dev/null 2>&1

    local pane_pid=""
    if [ "$SYSTEMD_AVAILABLE" -ne 1 ]; then
        pane_pid="$(tmux display-message -p -t "$session" '#{pane_pid}' 2>/dev/null)"
    fi

    local start paint_deadline_ns peak_bytes=0 v
    start=$(date +%s%N)
    paint_deadline_ns=$((start + TIMEOUT_SECS * 1000000000))

    # Phase 1: wait for first paint (the marker appears anywhere on screen),
    # tracking peak memory throughout in case the process exits or gets
    # OOM-killed before ever rendering.
    local painted=0 content=""
    while :; do
        if ! tmux has-session -t "$session" 2>/dev/null; then
            break
        fi
        v="$(read_current_peak "$unit" "$pane_pid")" && [ "$v" -gt "$peak_bytes" ] && peak_bytes="$v"
        content=$(tmux capture-pane -t "$session" -p 2>/dev/null)
        if grep -qF "$marker" <<<"$content"; then
            painted=1
            break
        fi
        if [ "$(date +%s%N)" -ge "$paint_deadline_ns" ]; then
            break
        fi
        sleep 0.01
    done

    if [ "$painted" -eq 0 ]; then
        local oom=0
        detect_oom "$unit" && oom=1
        tmux kill-session -t "$session" >/dev/null 2>&1
        reset_unit "$unit"
        CURRENT_SESSION=""
        CURRENT_UNIT=""
        if [ "$oom" -eq 1 ]; then
            echo "OOM"
        else
            echo "TIMEOUT"
        fi
        return
    fi

    # Phase 2: keep polling (both content-for-stability and peak-memory)
    # until the rendered content stops changing for STABLE_COUNT consecutive
    # polls -- background syntax highlighting, wrapping, indexing, etc. can
    # still be growing memory usage after the first frame is shown.
    local ready_deadline_ns=$(($(date +%s%N) + READY_TIMEOUT_SECS * 1000000000))
    local stable=0 last="$content"
    while :; do
        if ! tmux has-session -t "$session" 2>/dev/null; then
            break
        fi
        sleep "$STABLE_INTERVAL"
        v="$(read_current_peak "$unit" "$pane_pid")" && [ "$v" -gt "$peak_bytes" ] && peak_bytes="$v"
        content=$(tmux capture-pane -t "$session" -p 2>/dev/null)
        if [ "$content" = "$last" ]; then
            stable=$((stable + 1))
            [ "$stable" -ge "$STABLE_COUNT" ] && break
        else
            stable=0
        fi
        last="$content"
        if [ "$(date +%s%N)" -ge "$ready_deadline_ns" ]; then
            break
        fi
    done

    # One last read before tearing anything down.
    v="$(read_current_peak "$unit" "$pane_pid")" && [ "$v" -gt "$peak_bytes" ] && peak_bytes="$v"

    local oom=0
    detect_oom "$unit" && oom=1

    tmux kill-session -t "$session" >/dev/null 2>&1
    reset_unit "$unit"
    CURRENT_SESSION=""
    CURRENT_UNIT=""

    if [ "$oom" -eq 1 ]; then
        echo "OOM"
    else
        echo "$peak_bytes"
    fi
}

# Runs $ITERATIONS loads for one (editor, file), prints "min avg max" in
# bytes, or a single status word (TIMEOUT/OOM) if any run failed -- mirrors
# scripts/bench_editors.sh's pattern of aborting remaining iterations once
# one clearly fails, rather than averaging over a mix of good/bad runs.
bench_editor_on_file() {
    local name="$1" cmd="$2" file="$3" marker="$4"
    local -a samples=()
    local i result

    for ((i = 0; i < ITERATIONS; i++)); do
        result=$(time_one_load_memory "$cmd" "$file" "$marker")
        if [ "$result" = "TIMEOUT" ] || [ "$result" = "OOM" ]; then
            echo "$result"
            return
        fi
        samples+=("$result")
    done

    local min=${samples[0]} max=${samples[0]} sum=0
    for v in "${samples[@]}"; do
        [ "$v" -lt "$min" ] && min=$v
        [ "$v" -gt "$max" ] && max=$v
        sum=$((sum + v))
    done
    echo "$min $((sum / ITERATIONS)) $max"
}

# ============================================================================
# Startup: sweep away state left behind by a previous hard-interrupted run
# ============================================================================
sweep_stale_bench_state() {
    local stale
    stale="$(tmux list-sessions -F '#{session_name}' 2>/dev/null | grep -E '^benchmem_[0-9]+_[0-9]+_[0-9]+$' || true)"
    if [ -n "$stale" ]; then
        log "Cleaning up stale tmux session(s) from a previous interrupted run:"
        while IFS= read -r s; do
            log "  killing tmux session: $s"
            tmux kill-session -t "$s" >/dev/null 2>&1
        done <<<"$stale"
    fi
    if [ "$SYSTEMD_AVAILABLE" -eq 1 ]; then
        stale="$(systemctl --user list-units 'benchmem_*.scope' --all --no-legend --plain 2>/dev/null | awk '{print $1}')"
        if [ -n "$stale" ]; then
            log "Cleaning up stale systemd scope(s) from a previous interrupted run:"
            while IFS= read -r s; do
                log "  stopping scope: $s"
                systemctl --user kill --signal=SIGKILL "$s" >/dev/null 2>&1
                systemctl --user stop "$s" >/dev/null 2>&1
                systemctl --user reset-failed "$s" >/dev/null 2>&1
            done <<<"$stale"
        fi
    fi
}
sweep_stale_bench_state

# ============================================================================
# Main
# ============================================================================

# Which editors to actually run.
declare -a RUN_NAMES=()
if [ -n "$ONLY_EDITORS" ]; then
    IFS=',' read -ra requested <<<"$ONLY_EDITORS"
    for n in "${requested[@]}"; do
        [ -z "${EDITORS[$n]:-}" ] && die "Unknown editor '$n' (see --list)"
        RUN_NAMES+=("$n")
    done
else
    for n in "${!EDITORS[@]}"; do RUN_NAMES+=("$n"); done
fi

declare -a AVAILABLE_NAMES=()
for n in "${RUN_NAMES[@]}"; do
    if editor_available "${EDITORS[$n]}"; then
        AVAILABLE_NAMES+=("$n")
    else
        log "Skipping '$n' (${EDITORS[$n]}): not found on PATH"
    fi
done
[ "${#AVAILABLE_NAMES[@]}" -eq 0 ] && die "No requested editors are available. Run --list to check."

# Build the list of (label, file) pairs to test, pre-flight size checking
# each one before ever generating/opening it.
declare -a TEST_LABELS=()
declare -a TEST_FILES=()
if [ -n "$FILE" ]; then
    [ -f "$FILE" ] || die "File not found: $FILE"
    real_bytes="$(stat -c %s -- "$FILE" 2>/dev/null || stat -f %z -- "$FILE" 2>/dev/null)"
    [ -n "$real_bytes" ] || die "Could not stat file: $FILE"
    check_size_or_die "$FILE" "$real_bytes"
    TEST_LABELS+=("$FILE")
    TEST_FILES+=("$FILE")
else
    IFS=',' read -ra size_list <<<"$SIZES"
    for lines in "${size_list[@]}"; do
        case "$lines" in '' | *[!0-9]*) die "--sizes entries must be positive integers, got: '$lines'" ;; esac
        check_size_or_die "${lines} lines" "$(estimate_generated_bytes "$lines")"
        tmp="$(mktemp --tmpdir="$SCRATCH_DIR" --suffix=.txt)"
        TMP_FILES+=("$tmp")
        generate_file "$lines" "$tmp"
        TEST_LABELS+=("${lines} lines")
        TEST_FILES+=("$tmp")
    done
fi

log "Editors under test: ${AVAILABLE_NAMES[*]}"
log "Iterations per (editor, file): $ITERATIONS   Mem limit: ${MEM_LIMIT:-none}   Scratch dir: $SCRATCH_DIR"
log "Paint timeout: ${TIMEOUT_SECS}s   Ready timeout: ${READY_TIMEOUT_SECS}s   Pane: ${PANE_WIDTH}x${PANE_HEIGHT}"
log ""

# Normalized row format: label|name|STATUS|min|avg|max (bytes)
# STATUS is OK, OOM, or TIMEOUT; min/avg/max are "-" unless STATUS is OK.
RESULTS_FILE="$(mktemp --tmpdir="$SCRATCH_DIR")"
TMP_FILES+=("$RESULTS_FILE")

for idx in "${!TEST_FILES[@]}"; do
    file="${TEST_FILES[$idx]}"
    label="${TEST_LABELS[$idx]}"
    marker="$(first_line_marker "$file")"
    [ -z "$marker" ] && die "Could not find a usable marker line in: $file"

    log "=== $label ($file) ==="
    for name in "${AVAILABLE_NAMES[@]}"; do
        printf '  %-10s ... ' "$name" >&2
        result=$(bench_editor_on_file "$name" "${EDITORS[$name]}" "$file" "$marker")
        case "$result" in
        TIMEOUT)
            echo "$label|$name|TIMEOUT|-|-|-" >>"$RESULTS_FILE"
            log "TIMEOUT (never rendered within ${TIMEOUT_SECS}s)"
            ;;
        OOM)
            echo "$label|$name|OOM|-|-|-" >>"$RESULTS_FILE"
            log "OOM (hit --mem-limit $MEM_LIMIT before finishing)"
            ;;
        *)
            read -r mn avg mx <<<"$result"
            echo "$label|$name|OK|$mn|$avg|$mx" >>"$RESULTS_FILE"
            log "peak avg $(human_bytes "$avg") (min $(human_bytes "$mn"), max $(human_bytes "$mx"))"
            ;;
        esac
    done
    log ""
done

# ============================================================================
# Report
# ============================================================================
echo "===================================================================="
echo "RESULTS (peak memory; lower is better; OOM = hit --mem-limit ($MEM_LIMIT)"
echo "before settling -- true peak is unknown, only that it reached the cap)"
echo "===================================================================="

for idx in "${!TEST_FILES[@]}"; do
    label="${TEST_LABELS[$idx]}"
    echo ""
    echo "--- $label ---"

    fastest=$(awk -F'|' -v label="$label" '$1 == label && $3 == "OK" { print $5 }' "$RESULTS_FILE" | sort -n | head -1)

    {
        echo "EDITOR|PEAK(avg)|MIN|MAX|VS LOWEST"
        awk -F'|' -v label="$label" -v fastest="${fastest:-0}" '
            $1 == label {
                if ($3 == "OK") {
                    ratio = (fastest > 0) ? sprintf("%.2fx", $5 / fastest) : "-"
                    printf "%s|%s|%s|%s|%s|%d\n", $2, $5, $4, $6, ratio, $5
                } else if ($3 == "OOM") {
                    printf "%s|OOM|-|-|-|999999998\n", $2
                } else {
                    printf "%s|TIMEOUT|-|-|-|999999999\n", $2
                }
            }' "$RESULTS_FILE" | sort -t'|' -k6,6n |
            while IFS='|' read -r ed peak mn mx ratio _; do
                case "$peak" in
                OOM | TIMEOUT) printf '%s|%s|%s|%s|%s\n' "$ed" "$peak" "$mn" "$mx" "$ratio" ;;
                *) printf '%s|%s|%s|%s|%s\n' "$ed" "$(human_bytes "$peak")" "$(human_bytes "$mn")" "$(human_bytes "$mx")" "$ratio" ;;
                esac
            done
    } | column -t -s'|'
done

echo ""
echo "Done."
