#!/usr/bin/env bash
# Benchmarks how long different terminal text editors take to open a file.
# Measures two things per (editor, file):
#   - PAINT:  time from process launch until the file's content first appears
#             on screen.
#   - READY:  time from launch until the screen stops changing at all (a
#             generic, editor-agnostic proxy for "done with startup work" --
#             background syntax highlighting, wrapping, indexing, etc. --
#             not just the first frame). This is normally >= PAINT.
#
# Flexible by design: editors are just entries in an associative array below
# (name -> launch command). Add a new editor by adding one line -- no other
# changes needed. You can also register one-off editors from the command
# line with --add, without touching this file at all.
#
# Usage:
#   ./scripts/bench_editors.sh [options]
#
# Examples:
#   ./scripts/bench_editors.sh                              # default sizes, all available editors
#   ./scripts/bench_editors.sh --editors neo,vim,nano        # only these
#   ./scripts/bench_editors.sh --sizes 1000,500000           # custom line counts
#   ./scripts/bench_editors.sh --file /path/to/real/file.log # benchmark one specific file
#   ./scripts/bench_editors.sh --add "hx=hx" --editors hx    # add helix ad hoc and test it
#   ./scripts/bench_editors.sh --list                        # show registered editors + availability
#
# Requires: tmux, awk, mktemp. bash >= 4 (associative arrays).

set -uo pipefail

# ============================================================================
# Registered editors -- add more here. Key = display name, value = command
# used to launch it (the file path is appended as the final argument).
# ============================================================================
declare -A EDITORS=(
    [neo]="./bin/neo"
    # [vim]="vim"
    # [nvim]="nvim"
    # [nano]="nano"
    # [emacs]="emacs -nw"
    # [micro]="micro"
    [helix]="hx"
    # [kakoune]="kak"
    # [joe]="joe"
    # [mle]="mle"
)

# ============================================================================
# Defaults (overridable via flags)
# ============================================================================
ITERATIONS=5
TIMEOUT_SECS=30
READY_TIMEOUT_SECS=""  # defaults to TIMEOUT_SECS if left unset by --ready-timeout
STABLE_COUNT=5         # consecutive identical polls required to call it "ready"
STABLE_INTERVAL=0.05   # seconds between polls while waiting for stability
PANE_WIDTH=120
PANE_HEIGHT=40
SIZES="100000,1000000,10000000"
FILE=""
ONLY_EDITORS=""
KEEP_FILES=0
LIST_ONLY=0
EXTRA_EDITOR_ARGS=()

SCRIPT_NAME="$(basename "$0")"
TMP_FILES=()
CURRENT_SESSION=""

usage() {
    cat <<EOF
Usage: $SCRIPT_NAME [options]

Options:
  --editors LIST         Comma-separated editor names to run (default: all registered that are found)
  --sizes LIST           Comma-separated line counts to generate and test (default: $SIZES)
  --file PATH            Benchmark this one file instead of generating synthetic files
  --iterations N         Runs per (editor, file) pair, reported as min/avg/max (default: $ITERATIONS)
  --timeout SECONDS      Max seconds to wait for first paint before marking PAINT/READY as TIMEOUT (default: $TIMEOUT_SECS)
  --ready-timeout SECS   Max additional seconds (after first paint) to wait for the screen to settle
                         before marking READY as UNSTABLE (default: same as --timeout)
  --stable-count N       Consecutive identical polls required to call the screen "settled" (default: $STABLE_COUNT)
  --stable-interval SECS Seconds between polls while waiting for stability (default: $STABLE_INTERVAL)
  --width COLS           tmux pane width (default: $PANE_WIDTH)
  --height ROWS          tmux pane height (default: $PANE_HEIGHT)
  --add "name=command"   Register an extra editor for this run (repeatable)
  --list                 List registered editors and whether they're found on this machine, then exit
  --keep-files           Don't delete generated synthetic test files afterward
  -h, --help             Show this help

Examples:
  $SCRIPT_NAME
  $SCRIPT_NAME --editors neo,vim,nano
  $SCRIPT_NAME --sizes 1000,500000
  $SCRIPT_NAME --file /var/log/syslog
  $SCRIPT_NAME --add "hx=hx" --editors hx,neo
EOF
}

log() { printf '%s\n' "$*" >&2; }
die() {
    log "Error: $*"
    exit 1
}

cleanup() {
    [ -n "$CURRENT_SESSION" ] && tmux kill-session -t "$CURRENT_SESSION" >/dev/null 2>&1
    if [ "$KEEP_FILES" -eq 0 ]; then
        local f
        for f in "${TMP_FILES[@]:-}"; do
            [ -n "$f" ] && rm -f "$f"
        done
    fi
}
trap cleanup EXIT INT TERM

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
# Helpers
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

# Runs one (editor, file) load and prints "paintMs readyMs" to stdout.
#   paintMs is elapsed ms from launch until the marker first appears.
#   readyMs is elapsed ms from launch until the screen stops changing for
#     STABLE_COUNT consecutive polls (STABLE_INTERVAL apart) after that --
#     a generic proxy for "done with startup work", not just first paint.
# paintMs is "TIMEOUT" if the marker never appeared within --timeout; readyMs
# is "UNSTABLE" if the screen never settled within --ready-timeout after
# paint (paintMs is still meaningful in that case). Cleans up its own tmux
# session before returning.
time_one_load() {
    local cmd="$1" file="$2" marker="$3"
    local session
    session="bench_$$_${RANDOM}_${RANDOM}"
    CURRENT_SESSION="$session"

    tmux new-session -d -s "$session" -x "$PANE_WIDTH" -y "$PANE_HEIGHT" -- \
        bash -c "$cmd \"\$0\"" "$file" >/dev/null 2>&1

    local start paint_deadline_ns
    start=$(date +%s%N)
    paint_deadline_ns=$((start + TIMEOUT_SECS * 1000000000))

    # Phase 1: wait for first paint (the marker appears anywhere on screen).
    local paint_ms="TIMEOUT" ready_ms="TIMEOUT"
    local content=""
    while :; do
        if ! tmux has-session -t "$session" 2>/dev/null; then
            # Session/pane ended before we ever saw the marker (e.g. the
            # editor crashed or exited immediately) -- stop waiting.
            break
        fi
        content=$(tmux capture-pane -t "$session" -p 2>/dev/null)
        if grep -qF "$marker" <<<"$content"; then
            paint_ms=$((($(date +%s%N) - start) / 1000000))
            break
        fi
        if [ "$(date +%s%N)" -ge "$paint_deadline_ns" ]; then
            break
        fi
        sleep 0.01
    done

    if [ "$paint_ms" = "TIMEOUT" ]; then
        tmux kill-session -t "$session" >/dev/null 2>&1
        CURRENT_SESSION=""
        echo "TIMEOUT TIMEOUT"
        return
    fi

    # Phase 2: keep polling until the rendered content stops changing for
    # STABLE_COUNT consecutive polls -- background syntax highlighting,
    # wrapping, indexing, etc. should have finished by then.
    ready_ms="UNSTABLE" # overwritten below if we actually detect stability
    local ready_deadline_ns=$(($(date +%s%N) + READY_TIMEOUT_SECS * 1000000000))
    local stable=0 last="$content"
    while :; do
        if ! tmux has-session -t "$session" 2>/dev/null; then
            # Process is gone -- as settled as it will ever get.
            ready_ms=$((($(date +%s%N) - start) / 1000000))
            break
        fi
        sleep "$STABLE_INTERVAL"
        content=$(tmux capture-pane -t "$session" -p 2>/dev/null)
        if [ "$content" = "$last" ]; then
            stable=$((stable + 1))
            if [ "$stable" -ge "$STABLE_COUNT" ]; then
                ready_ms=$((($(date +%s%N) - start) / 1000000))
                break
            fi
        else
            stable=0
        fi
        last="$content"
        if [ "$(date +%s%N)" -ge "$ready_deadline_ns" ]; then
            break
        fi
    done

    tmux kill-session -t "$session" >/dev/null 2>&1
    CURRENT_SESSION=""

    echo "$paint_ms $ready_ms"
}

# Runs $ITERATIONS loads for one (editor, file), prints:
#   "paintMin paintAvg paintMax readyMin readyAvg readyMax"
# All six are "TIMEOUT" if the editor never rendered at all; the ready
# triple is "UNSTABLE" if paint succeeded but the screen never settled.
bench_editor_on_file() {
    local name="$1" cmd="$2" file="$3" marker="$4"
    local -a paint_samples=() ready_samples=()
    local i result paint ready ever_unstable=0

    for ((i = 0; i < ITERATIONS; i++)); do
        result=$(time_one_load "$cmd" "$file" "$marker")
        read -r paint ready <<<"$result"
        if [ "$paint" = "TIMEOUT" ]; then
            echo "TIMEOUT TIMEOUT TIMEOUT TIMEOUT TIMEOUT TIMEOUT"
            return
        fi
        paint_samples+=("$paint")
        if [ "$ready" = "UNSTABLE" ]; then
            ever_unstable=1
        else
            ready_samples+=("$ready")
        fi
    done

    local pmin=${paint_samples[0]} pmax=${paint_samples[0]} psum=0
    for v in "${paint_samples[@]}"; do
        [ "$v" -lt "$pmin" ] && pmin=$v
        [ "$v" -gt "$pmax" ] && pmax=$v
        psum=$((psum + v))
    done
    local pavg=$((psum / ITERATIONS))

    if [ "$ever_unstable" -eq 1 ] || [ "${#ready_samples[@]}" -eq 0 ]; then
        echo "$pmin $pavg $pmax UNSTABLE UNSTABLE UNSTABLE"
        return
    fi

    local rmin=${ready_samples[0]} rmax=${ready_samples[0]} rsum=0
    for v in "${ready_samples[@]}"; do
        [ "$v" -lt "$rmin" ] && rmin=$v
        [ "$v" -gt "$rmax" ] && rmax=$v
        rsum=$((rsum + v))
    done
    local ravg=$((rsum / ITERATIONS))

    echo "$pmin $pavg $pmax $rmin $ravg $rmax"
}

# ============================================================================
# Main
# ============================================================================

if [ "$LIST_ONLY" -eq 1 ]; then
    list_editors
    exit 0
fi

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

# Build the list of (label, file) pairs to test.
declare -a TEST_LABELS=()
declare -a TEST_FILES=()
if [ -n "$FILE" ]; then
    [ -f "$FILE" ] || die "File not found: $FILE"
    TEST_LABELS+=("$FILE")
    TEST_FILES+=("$FILE")
else
    IFS=',' read -ra size_list <<<"$SIZES"
    for lines in "${size_list[@]}"; do
        tmp="$(mktemp --suffix=.txt)"
        TMP_FILES+=("$tmp")
        generate_file "$lines" "$tmp"
        TEST_LABELS+=("${lines} lines")
        TEST_FILES+=("$tmp")
    done
fi

log "Editors under test: ${AVAILABLE_NAMES[*]}"
log "Iterations per (editor, file): $ITERATIONS   Paint timeout: ${TIMEOUT_SECS}s   Ready timeout: ${READY_TIMEOUT_SECS}s"
log "Stability: $STABLE_COUNT consecutive unchanged polls, ${STABLE_INTERVAL}s apart   Pane: ${PANE_WIDTH}x${PANE_HEIGHT}"
log ""

# Normalized row format: label|name|STATUS|pmin|pavg|pmax|rmin|ravg|rmax
# STATUS is OK, UNSTABLE (paint succeeded, screen never settled), or TIMEOUT
# (never painted at all). p*/r* are "-" unless meaningful for that STATUS.
RESULTS_FILE="$(mktemp)"
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
        read -r pmin pavg pmax rmin ravg rmax <<<"$result"
        if [ "$pmin" = "TIMEOUT" ]; then
            echo "$label|$name|TIMEOUT|-|-|-|-|-|-" >>"$RESULTS_FILE"
            log "TIMEOUT (never rendered within ${TIMEOUT_SECS}s)"
        elif [ "$rmin" = "UNSTABLE" ]; then
            echo "$label|$name|UNSTABLE|$pmin|$pavg|$pmax|-|-|-" >>"$RESULTS_FILE"
            log "paint avg ${pavg}ms -- UNSTABLE (never settled within ${READY_TIMEOUT_SECS}s after paint)"
        else
            echo "$label|$name|OK|$pmin|$pavg|$pmax|$rmin|$ravg|$rmax" >>"$RESULTS_FILE"
            log "paint avg ${pavg}ms, ready avg ${ravg}ms (min ${rmin}ms, max ${rmax}ms)"
        fi
    done
    log ""
done

# ============================================================================
# Report
# ============================================================================
echo "===================================================================="
echo "RESULTS (time in ms; lower is better; PAINT = first frame of content,"
echo "READY = screen stops changing at all -- includes background work)"
echo "===================================================================="

for idx in "${!TEST_FILES[@]}"; do
    label="${TEST_LABELS[$idx]}"
    echo ""
    echo "--- $label ---"

    fastest=$(awk -F'|' -v label="$label" '$1 == label && $3 == "OK" { print $8 }' "$RESULTS_FILE" | sort -n | head -1)

    {
        echo "EDITOR|PAINT(ms)|READY(ms)|READY MIN|READY MAX|VS FASTEST"
        awk -F'|' -v label="$label" -v fastest="${fastest:-0}" '
            $1 == label {
                if ($3 == "OK") {
                    ratio = (fastest > 0) ? sprintf("%.2fx", $8 / fastest) : "-"
                    printf "%s|%s|%s|%s|%s|%s|%d\n", $2, $5, $8, $7, $9, ratio, $8
                } else if ($3 == "UNSTABLE") {
                    printf "%s|%s|UNSTABLE|-|-|-|999999998\n", $2, $5
                } else {
                    printf "%s|TIMEOUT|TIMEOUT|-|-|-|999999999\n", $2
                }
            }' "$RESULTS_FILE" | sort -t'|' -k7,7n | cut -d'|' -f1-6
    } | column -t -s'|'
done

echo ""
echo "Done."
