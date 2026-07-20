#!/usr/bin/env bash
# Benchmarks steady-state INPUT LATENCY and SAVE TIME of different terminal
# text editors once a large file is already open and settled.
#
# scripts/bench_editors.sh measures cold-start PAINT/READY time (opening a
# file). scripts/bench_editors_memory.sh measures peak memory during that
# same open+settle window. Neither measures what happens *after* that: how
# responsive the editor feels once a user is actually working in an already-
# open file -- moving around, typing, folding code, undoing/redoing, and
# saving. This script fills that gap.
#
# For each (file size, editor, syntax-highlight mode), it:
#   1. Launches the editor on a large synthetic .c file and waits for the
#      initial paint+ready settle (reusing bench_editors.sh's stability-poll
#      technique) -- this load time is NOT reported, it's just the gate
#      before the timed workload starts.
#   2. Runs one fixed, deterministic, deliberately demanding sequence of
#      "steps" against that single live session: folding (including 3x
#      fold-all/unfold-all cycles across a file dense with nested foldable
#      blocks), deep multi-directional navigation, whole-buffer selection,
#      two separate multi-line edits with heavy backspace/delete, a deep
#      undo/redo stack workout, and 3 save cycles.
#   3. Times each step by sending its keys via tmux send-keys, then polling
#      capture-pane until the screen stabilizes (same technique as the paint/
#      ready gate, applied per step) -- burst-level latency, not
#      per-keystroke, since tmux/awk polling resolution can't meaningfully
#      resolve single-keystroke timing.
#   4. Repeats the whole session with syntax highlighting forced off, so the
#      report can show highlighting's cost as its own delta table.
#
# neo has no search feature and no modes (always insert-like, like nano) --
# the workload excludes search entirely and needs no insert/normal mode
# dance for neo. vim is modal and launched with extra -c flags so it's a fair
# comparison: `foldmethod=indent foldlevel=99` (indentation-based folds that
# start open, mirroring neo's default view) and `ruler laststatus=2` (so pure
# cursor movement changes the captured pane text -- without a ruler, moving
# the cursor without scrolling produces no visible change at all).
#
# Standalone script by design -- no shared code with the sibling scripts
# (some duplication of the editor-registry/tmux-launch plumbing is accepted
# in exchange for each script being simple to reason about on its own).
#
# Usage:
#   ./scripts/bench_editors_input.sh [options]
#
# Examples:
#   ./scripts/bench_editors_input.sh
#   ./scripts/bench_editors_input.sh --editors neo,vim --sizes 10000,200000
#   ./scripts/bench_editors_input.sh --no-syntax-compare --iterations 1
#   ./scripts/bench_editors_input.sh --list
#
# Requires: tmux, awk, mktemp. bash >= 4.3 (associative arrays, namerefs).

set -uo pipefail

# ============================================================================
# Registered editors -- add more here. Key = display name, value = BASE
# launch command (syntax on/off flags and the filename are appended later).
#
# vim/nvim use --clean: without it, a personal ~/.vimrc's own mappings/plugins
# (autocomplete popups, abbreviations, auto-pairs, ...) can intercept scripted
# keystrokes -- confirmed by reproduction: typing a line then Enter ended up
# with a stray "main" (a keyword-completion pick) spliced into the typed
# text, only when the user's real vimrc was loaded. --clean gives a vanilla,
# reproducible baseline any machine will behave the same under.
#
# shiftwidth=4 matters even though nothing here looks like indentation: with
# foldmethod=indent, fold level = floor(indent_spaces / shiftwidth). The
# generated .c file indents 4 spaces per level, so under vim/nvim's default
# shiftwidth=8 every singly-nested line -- including int main(void) {'s own
# body, which fold_toggle/fold_untoggle specifically target -- rounds down to
# fold level 0, i.e. no fold to toggle at all. Confirmed by reproduction
# (:echo foldlevel(N) showed 0 on 4-space lines, 1 only on 8-space/doubly-
# nested lines, under the default shiftwidth).
#
# helix has no --clean equivalent; HELIX_CONFIG (generated once in Main,
# after --scratch-dir is resolved) is passed via -c at launch time instead --
# see run_session().
# ============================================================================
declare -A EDITORS=(
    [neo]="./bin/neo"
    # [vim]="vim --clean -c \"set nocompatible ruler laststatus=2 foldmethod=indent foldlevel=99 shiftwidth=4\""
    [nvim]="nvim --clean -c \"set nocompatible ruler laststatus=2 foldmethod=indent foldlevel=99 shiftwidth=4\""
    [helix]="hx"
    # [nano]="nano"  # no folding support at all -- fold_* ops report N/A if added
)

# Per-editor, per-mode extra CLI/config flags to force syntax highlighting on
# or off, appended after the base command in EDITORS above. helix has no
# per-mode syntax toggle wired up here (its highlighting comes from
# tree-sitter and isn't a simple on/off CLI flag) -- left absent, which makes
# its "on"/"off" runs identical; the syntax-cost delta table will just show
# ~0 for helix rather than a real on/off comparison.
declare -A SYNTAX_FLAG=(
    [neo:on]="--syntax"
    [neo:off]="--no-syntax"
    [vim:on]="-c \"syntax on\""
    [vim:off]="-c \"syntax off\""
    [nvim:on]="-c \"syntax on\""
    [nvim:off]="-c \"syntax off\""
)

# ----------------------------------------------------------------------------
# Per-op keymap: KEYMAP["editor:op"] -> a '|'-separated segment DSL.
#   L:<literal text>   sent via `tmux send-keys -l` (exact bytes, no key-name
#                      interpretation -- used for typed source code).
#   K:<key1> <key2>... each space-separated token sent as its own
#                      `tmux send-keys` call (named keys like PageDown/C-z,
#                      or short literal command sequences like za/u/gg).
# An op missing for a given editor is skipped for that editor (reported N/A).
#
# vim's fold_toggle needs the cursor two lines below the file-open position
# (not one): with foldmethod=indent, the *header* line (e.g. "int main(void)
# {") sits at indent level 0 like the lines around it and owns no fold of its
# own -- the fold is the indented *body* below it. `za` must be pressed on a
# body line to find anything to toggle (verified interactively; neo instead
# treats the header line itself as foldable and toggles from there).
# ----------------------------------------------------------------------------
declare -A KEYMAP

repeat_key() {
    local key="$1" n="$2" out="" i
    for ((i = 0; i < n; i++)); do out+="$key "; done
    printf '%s' "${out% }"
}

KEYMAP[neo:fold_toggle]="K:Down C-f"
KEYMAP[neo:fold_untoggle]="K:C-f"
KEYMAP[neo:fold_all]="K:M-f"
KEYMAP[neo:unfold_all]="K:M-f"
KEYMAP[neo:nav_page_down]="K:$(repeat_key PageDown 25)"
KEYMAP[neo:nav_page_up]="K:$(repeat_key PageUp 15)"
KEYMAP[neo:nav_word_jump]="K:$(repeat_key C-Right 20)"
KEYMAP[neo:nav_arrow_down]="K:$(repeat_key Down 40)"
KEYMAP[neo:nav_arrow_up]="K:$(repeat_key Up 20)"
KEYMAP[neo:nav_home_end]="K:Home End Home End Home"
KEYMAP[neo:nav_select_all]="K:C-a"
KEYMAP[neo:nav_deselect]="K:Right"
KEYMAP[neo:edit_backspace]="K:$(repeat_key BSpace 40)"
KEYMAP[neo:edit_delete_forward]="K:$(repeat_key Delete 20)"
KEYMAP[neo:undo]="K:C-z"
KEYMAP[neo:redo]="K:C-y"
KEYMAP[neo:undo_redo_interleave]="K:$(repeat_key C-z 8) $(repeat_key C-y 5) $(repeat_key C-z 3)"
KEYMAP[neo:save]="K:C-s"

KEYMAP[vim:fold_toggle]="K:Escape Down Down za"
KEYMAP[vim:fold_untoggle]="K:Escape za"
KEYMAP[vim:fold_all]="K:Escape zM"
KEYMAP[vim:unfold_all]="K:Escape zR"
KEYMAP[vim:nav_page_down]="K:Escape $(repeat_key PageDown 25)"
KEYMAP[vim:nav_page_up]="K:Escape $(repeat_key PageUp 15)"
KEYMAP[vim:nav_word_jump]="K:Escape $(repeat_key w 20)"
KEYMAP[vim:nav_arrow_down]="K:Escape $(repeat_key Down 40)"
KEYMAP[vim:nav_arrow_up]="K:Escape $(repeat_key Up 20)"
KEYMAP[vim:nav_home_end]="K:Escape Home End Home End Home"
KEYMAP[vim:nav_select_all]="K:Escape gg V G"
KEYMAP[vim:nav_deselect]="K:Escape"
KEYMAP[vim:edit_backspace]="K:$(repeat_key BSpace 40)"
KEYMAP[vim:edit_delete_forward]="K:$(repeat_key Delete 20)"
KEYMAP[vim:undo]="K:Escape u"
KEYMAP[vim:redo]="K:Escape C-r"
KEYMAP[vim:undo_redo_interleave]="K:Escape $(repeat_key u 8) $(repeat_key C-r 5) $(repeat_key u 3)"
KEYMAP[vim:save]="K:Escape|L::w|K:Enter"

# nvim confirmed (interactively) to accept the same launch flags and behave
# identically to vim for every op below -- copy the whole vim keymap rather
# than re-typing it, so the two can't drift apart by accident.
for _vim_key in "${!KEYMAP[@]}"; do
    case "$_vim_key" in
    vim:*) KEYMAP["nvim:${_vim_key#vim:}"]="${KEYMAP[$_vim_key]}" ;;
    esac
done
unset _vim_key

# helix is "selection-first" (movements act on/collapse a selection) rather
# than vim-style modal editing, and reassigns several single-letter commands:
# undo/redo are u/U (capital U, not Ctrl-r), select-all is the single key %
# (vs vim's gg V G). Deselect is NOT ";" or "Escape" -- confirmed
# interactively that neither collapses a "%" selection here (subsequent
# movement/insert still acted on the old selection's anchor); a plain
# directional movement (e.g. Right) does collapse it correctly, same as
# neo's own nav_deselect, so that's what's used below. helix also has no
# code-folding feature at all -- its z-prefix is bound to viewport/scroll
# commands (center/top/bottom), not folds (confirmed: zc/zo/zm/zM/zR all
# produced no visible change on a foldable line). fold_toggle/fold_untoggle/
# fold_all/unfold_all are deliberately left unset below so those four steps
# report N/A for helix via the existing tolerant-skip handling, the same way
# a folding-less `nano` entry would.
KEYMAP[helix:nav_page_down]="K:Escape $(repeat_key PageDown 25)"
KEYMAP[helix:nav_page_up]="K:Escape $(repeat_key PageUp 15)"
KEYMAP[helix:nav_word_jump]="K:Escape $(repeat_key w 20)"
KEYMAP[helix:nav_arrow_down]="K:Escape $(repeat_key Down 40)"
KEYMAP[helix:nav_arrow_up]="K:Escape $(repeat_key Up 20)"
KEYMAP[helix:nav_home_end]="K:Escape Home End Home End Home"
KEYMAP[helix:nav_select_all]="K:Escape %"
KEYMAP[helix:nav_deselect]="K:Right"
KEYMAP[helix:edit_backspace]="K:$(repeat_key BSpace 40)"
KEYMAP[helix:edit_delete_forward]="K:$(repeat_key Delete 20)"
KEYMAP[helix:undo]="K:Escape u"
KEYMAP[helix:redo]="K:Escape U"
KEYMAP[helix:undo_redo_interleave]="K:Escape $(repeat_key u 8) $(repeat_key U 5) $(repeat_key u 3)"
KEYMAP[helix:save]="K:Escape|L::w|K:Enter"

# Fixed step order. Groups repeated more than once per iteration (see
# REPEATS below) report min/avg/max across those repetitions (pooled with
# --iterations re-runs too) rather than a single average -- so the report
# surfaces any degradation across repeated demanding operations instead of
# hiding it.
STEP_ORDER=(
    fold_toggle fold_untoggle fold_all unfold_all
    nav_page_down nav_page_up nav_word_jump nav_arrow_down nav_arrow_up
    nav_home_end nav_select_all nav_deselect
    edit_type edit_backspace edit_delete_forward
    undo redo undo_redo_interleave
    save
)
declare -A REPEATS=(
    [fold_all]=3 [unfold_all]=3
    [edit_type]=2 [edit_backspace]=2 [edit_delete_forward]=2
    [undo]=15 [redo]=15
    [save]=3
)
step_repeats() { echo "${REPEATS[$1]:-1}"; }

# Two distinct ~10-line source snippets typed at two different cursor
# locations (edit_type call 1 / call 2), so each lands in its own
# undo-history region. Real C tokens throughout so syntax highlighting has
# work to do while typing, not just while idle.
EDIT_BLOCK_1=(
    'void injected_block_one(void) {'
    '    int sum = 0;'
    '    for (int k = 0; k < 10; k++) {'
    '        if (k % 3 == 0) {'
    '            sum += k;'
    '        } else {'
    '            sum -= 1;'
    '        }'
    '    }'
    '    printf("block one sum = %d\n", sum);'
    '}'
)
EDIT_BLOCK_2=(
    'int injected_block_two(int base) {'
    '    int acc = base;'
    '    char *note = "second injected block";'
    '    if (base > 0) {'
    '        acc *= 2;'
    '    } else {'
    '        acc = 0;'
    '    }'
    '    printf("%s -> %d\n", note, acc);'
    '    return acc;'
    '}'
)

# ============================================================================
# Defaults (overridable via flags)
# ============================================================================
ITERATIONS=3
TIMEOUT_SECS=30          # initial paint timeout (untimed load-gate)
READY_TIMEOUT_SECS=""    # defaults to TIMEOUT_SECS
STEP_TIMEOUT_SECS=10     # per-step stabilization budget
STABLE_COUNT=5
STABLE_INTERVAL=0.01
PANE_WIDTH=120
PANE_HEIGHT=40
SIZES="100000,1000000,10000000"
ONLY_EDITORS=""
KEEP_FILES=0
LIST_ONLY=0
NO_SYNTAX_COMPARE=0
NO_SYNTAX_ONLY=1
EXTRA_EDITOR_ARGS=()
SCRATCH_DIR="./bench_tmp"

SCRIPT_NAME="$(basename "$0")"
TMP_FILES=()
CURRENT_SESSION=""

usage() {
    cat <<EOF
Usage: $SCRIPT_NAME [options]

Options:
  --editors LIST         Comma-separated editor names to run (default: all registered that are found)
  --sizes LIST           Comma-separated line counts to generate and test (default: $SIZES)
  --iterations N         Full-session re-runs per (editor, file, syntax mode) (default: $ITERATIONS)
  --timeout SECONDS      Max seconds to wait for initial paint before giving up (default: $TIMEOUT_SECS)
  --ready-timeout SECS   Max additional seconds to wait for the initial settle (default: same as --timeout)
  --step-timeout SECS    Max seconds to wait for a single step to stabilize (default: $STEP_TIMEOUT_SECS)
  --stable-count N       Consecutive identical polls required to call the screen "settled" (default: $STABLE_COUNT)
  --stable-interval SECS Seconds between polls while waiting for stability (default: $STABLE_INTERVAL)
  --width COLS           tmux pane width (default: $PANE_WIDTH)
  --height ROWS          tmux pane height (default: $PANE_HEIGHT)
  --add "name=command"   Register an extra editor for this run (repeatable) -- also needs KEYMAP
                         entries hardcoded in this script to be exercised beyond N/A
  --no-syntax-compare    Skip the syntax-highlighting-off run (roughly halves total run time)
  --no-syntax-only       Skip the syntax-highlighting-on run (only test with syntax off)
  --list                 List registered editors and whether they're found on this machine, then exit
  --keep-files           Don't delete generated/edited test files afterward
  --scratch-dir PATH     Real-disk directory for test files (default: $SCRATCH_DIR)
  -h, --help             Show this help

Examples:
  $SCRIPT_NAME
  $SCRIPT_NAME --editors neo,vim --sizes 10000,200000
  $SCRIPT_NAME --no-syntax-compare --iterations 1
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
trap cleanup EXIT
trap 'cleanup; exit 130' INT
trap 'cleanup; exit 143' TERM

# ============================================================================
# Argument parsing
# ============================================================================
while [ $# -gt 0 ]; do
    case "$1" in
    --editors) ONLY_EDITORS="$2"; shift 2 ;;
    --sizes) SIZES="$2"; shift 2 ;;
    --iterations) ITERATIONS="$2"; shift 2 ;;
    --timeout) TIMEOUT_SECS="$2"; shift 2 ;;
    --ready-timeout) READY_TIMEOUT_SECS="$2"; shift 2 ;;
    --step-timeout) STEP_TIMEOUT_SECS="$2"; shift 2 ;;
    --stable-count) STABLE_COUNT="$2"; shift 2 ;;
    --stable-interval) STABLE_INTERVAL="$2"; shift 2 ;;
    --width) PANE_WIDTH="$2"; shift 2 ;;
    --height) PANE_HEIGHT="$2"; shift 2 ;;
    --add) EXTRA_EDITOR_ARGS+=("$2"); shift 2 ;;
    --no-syntax-compare) NO_SYNTAX_COMPARE=1; shift ;;
    --no-syntax-only) NO_SYNTAX_ONLY=1; shift ;;
    --list) LIST_ONLY=1; shift ;;
    --keep-files) KEEP_FILES=1; shift ;;
    --scratch-dir) SCRATCH_DIR="$2"; shift 2 ;;
    -h | --help) usage; exit 0 ;;
    *) die "Unknown argument: $1 (see --help)" ;;
    esac
done

command -v tmux >/dev/null 2>&1 || die "tmux is required but not found on PATH"
command -v awk >/dev/null 2>&1 || die "awk is required but not found on PATH"

[ -z "$READY_TIMEOUT_SECS" ] && READY_TIMEOUT_SECS="$TIMEOUT_SECS"

for kv in "${EXTRA_EDITOR_ARGS[@]:-}"; do
    [ -z "$kv" ] && continue
    name="${kv%%=*}"
    cmdline="${kv#*=}"
    [ "$name" = "$kv" ] && die "--add expects name=command, got: $kv"
    EDITORS["$name"]="$cmdline"
done

# ============================================================================
# Editor registry helpers
# ============================================================================
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

check_not_tmpfs() {
    local dir="$1" fstype
    fstype="$(df -PT "$dir" 2>/dev/null | awk 'NR==2{print $2}')"
    case "$fstype" in
    tmpfs | ramfs)
        log "Warning: --scratch-dir '$dir' is on a RAM-backed filesystem ($fstype)."
        log "  Large test files will consume system RAM directly just by existing."
        ;;
    esac
}
check_not_tmpfs "$SCRATCH_DIR"

# helix has no --clean flag; isolate it from any user ~/.config/helix via an
# explicit -c config instead, disabling two of its defaults that would
# otherwise skew this benchmark against every other editor here:
#   - auto-pairs: helix auto-inserts the matching bracket/quote as you type
#     the opening one. Confirmed by reproduction -- typing the literal
#     "void injected_block_one(void) {" left the buffer with an extra,
#     unwanted "}" appended, and since the benchmark's typed blocks already
#     contain their own explicit closing braces, every subsequent line's
#     brace collided with an auto-inserted one and the corruption compounded
#     (this alone made edit_type take ~10x longer than any other step, and
#     left the saved file without the expected typed text at all).
#   - LSP (clangd, auto-attached for .c files): produces inline diagnostics
#     none of the other editors have an equivalent of, confirmed via both
#     `hx --health` and a live diagnostic -- background analysis
#     noise/non-determinism the stability-poll technique isn't designed to
#     filter out.
# Generated once; appended in run_session() since --scratch-dir isn't
# resolved yet when the static EDITORS array above is declared.
HELIX_CONFIG="$SCRATCH_DIR/.hx_no_lsp.toml"
cat >"$HELIX_CONFIG" <<'EOF'
[editor]
auto-pairs = false

[editor.lsp]
enable = false
EOF
TMP_FILES+=("$HELIX_CONFIG")

# ============================================================================
# Test-file generation: a marker line, then a deterministic foldable
# `int main(void) { ... }`, then many small functions -- each foldable, each
# with 2-3 levels of nested foldable if/for blocks, interspersed multi-line
# block comments -- packed throughout the ENTIRE file so fold_all/unfold_all
# have real, nested, whole-buffer work to do, and syntax highlighting has
# real tokens everywhere, not just near the top.
# ============================================================================
generate_file() {
    local lines="$1" path="$2"
    awk -v n="$lines" 'BEGIN {
        printed = 0
        print "// BENCHMARK_MARKER_START_OF_FILE"; printed++
        print "int main(void) {"; printed++
        print "    int variable_1 = 1; /* entry point filler comment */"; printed++
        print "    char *label_1 = \"value_1\";"; printed++
        print "    for (int i = 0; i < variable_1; i++) {"; printed++
        print "        printf(\"Processing %d\\n\", i);"; printed++
        print "    }"; printed++
        print "    return 0;"; printed++
        print "}"; printed++
        print ""; printed++

        fn = 1
        while (printed < n) {
            print "/*"; printed++
            print " * helper_" fn " -- filler function for syntax-highlight stress"; printed++
            print " * generated for scripts/bench_editors_input.sh"; printed++
            print " */"; printed++
            print "int helper_" fn "(int x, int y) {"; printed++
            print "    int total = 0;"; printed++
            print "    if (x > 0) {"; printed++
            print "        for (int i = 0; i < x; i++) {"; printed++
            print "            if (i % 2 == 0) {"; printed++
            print "                total += i * y; // even branch"; printed++
            print "            } else {"; printed++
            print "                total -= i; // odd branch"; printed++
            print "            }"; printed++
            print "        }"; printed++
            print "    } else {"; printed++
            print "        total = -1;"; printed++
            print "    }"; printed++
            print "    char *tag = \"helper_" fn "\";"; printed++
            print "    printf(\"%s produced %d\\n\", tag, total);"; printed++
            print "    return total;"; printed++
            print "}"; printed++
            print ""; printed++
            fn++
        }
    }' >"$path"
}

first_line_marker() {
    awk 'NF { print; exit }' "$1" | cut -c1-40
}

human_ms() {
    awk -v v="$1" 'BEGIN {
        if (v == "" || v !~ /^[0-9]+$/) { print v; exit }
        printf "%d\n", v
    }'
}

# ============================================================================
# Key-sending + per-step stabilization timing
# ============================================================================

# Sends one op's full key sequence to the session. Segments are '|'-separated;
# "L:<text>" goes through send-keys -l (literal bytes, no key-name lookup),
# "K:<k1> <k2> ..." sends each space-separated token as its own send-keys call.
send_op() {
    local session="$1" seq="$2" seg
    while IFS= read -r seg; do
        [ -z "$seg" ] && continue
        case "$seg" in
        L:*)
            local text="${seg#L:}"
            # tmux's own command parser treats a trailing bare ';' at the
            # end of a `send-keys -l` argument as an (empty) chained-command
            # separator and silently drops it -- confirmed even with plain
            # `cat`, nothing editor-specific about it. Escaping it as '\;'
            # (only when it's the LAST character -- a mid-string ';' must
            # stay bare, escaping THOSE breaks them instead) fixes it. This
            # matters a lot here: virtually every typed C statement ends in
            # ';'.
            case "$text" in
            *\;) text="${text%;}\\;" ;;
            esac
            tmux send-keys -t "$session" -l -- "$text"
            ;;
        K:*)
            local keys="${seg#K:}" k
            for k in $keys; do
                tmux send-keys -t "$session" -- "$k"
            done
            ;;
        esac
    done <<<"${seq//|/$'\n'}"
}

# Sends $2's key sequence to session $1, then polls capture-pane until the
# screen stabilizes for STABLE_COUNT consecutive polls (or STEP_TIMEOUT_SECS
# elapses). Prints elapsed ms, or "UNSTABLE" if it never settled. Same
# stability-poll idea as scripts/bench_editors.sh's READY phase, applied per
# step -- so, like that script, the reported time includes the fixed
# STABLE_COUNT*STABLE_INTERVAL dwell overhead. That's a constant added to
# every measurement, not noise, so relative comparisons across editors/steps
# stay meaningful even though absolute numbers run a bit high.
run_step() {
    local session="$1" seq="$2"
    local start_ns end_ns deadline_ns last content stable=0 settled=0

    start_ns=$(date +%s%N)
    send_op "$session" "$seq"

    deadline_ns=$(($(date +%s%N) + STEP_TIMEOUT_SECS * 1000000000))
    last=$(tmux capture-pane -p -t "$session" 2>/dev/null)
    while :; do
        if ! tmux has-session -t "$session" 2>/dev/null; then
            settled=1
            break
        fi
        sleep "$STABLE_INTERVAL"
        content=$(tmux capture-pane -p -t "$session" 2>/dev/null)
        if [ "$content" = "$last" ]; then
            stable=$((stable + 1))
            if [ "$stable" -ge "$STABLE_COUNT" ]; then
                settled=1
                break
            fi
        else
            stable=0
        fi
        last="$content"
        if [ "$(date +%s%N)" -ge "$deadline_ns" ]; then
            break
        fi
    done

    if [ "$settled" -ne 1 ]; then
        echo "UNSTABLE"
        return
    fi
    end_ns=$(date +%s%N)
    echo $(((end_ns - start_ns) / 1000000))
}

# Waits for the marker to first appear (paint), then for the screen to stop
# changing (ready). Prints READY or TIMEOUT. Not timed/reported -- this is
# just the gate before the timed workload starts.
wait_until_ready() {
    local session="$1" marker="$2"
    local start paint_deadline_ns content painted=0
    start=$(date +%s%N)
    paint_deadline_ns=$((start + TIMEOUT_SECS * 1000000000))
    while :; do
        if ! tmux has-session -t "$session" 2>/dev/null; then
            echo "TIMEOUT"
            return
        fi
        content=$(tmux capture-pane -p -t "$session" 2>/dev/null)
        if grep -qF "$marker" <<<"$content"; then
            painted=1
            break
        fi
        if [ "$(date +%s%N)" -ge "$paint_deadline_ns" ]; then
            echo "TIMEOUT"
            return
        fi
        sleep 0.01
    done
    [ "$painted" -eq 1 ] || { echo "TIMEOUT"; return; }

    local ready_deadline_ns=$(($(date +%s%N) + READY_TIMEOUT_SECS * 1000000000))
    local stable=0 last="$content"
    while :; do
        if ! tmux has-session -t "$session" 2>/dev/null; then
            break
        fi
        sleep "$STABLE_INTERVAL"
        content=$(tmux capture-pane -p -t "$session" 2>/dev/null)
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
    echo "READY"
}

# Editors needing a leading Escape+i to enter Insert mode before typed text
# registers (vim/nvim/helix are all modal; neo is always insert-like).
declare -A MODAL_EDITORS=([vim]=1 [nvim]=1 [helix]=1)

# Builds the edit_type DSL sequence for one typed block, per editor
# modal-ness: modal editors need a leading Escape+i to enter Insert mode; neo
# needs no mode switch at all.
#
# helix strips each line's leading whitespace before typing it: helix
# auto-indents on Enter (confirmed live -- no config key was found to turn
# this off, unlike auto-pairs/LSP above), and since our lines already carry
# their own explicit indentation, the two stack and compound line over line
# (observed directly: indentation growing 0/8/12+ spaces deep within a few
# lines instead of the intended 0/4/4/8). Typing bare content and letting
# helix's own auto-indent place it is clean and fast (confirmed); the other
# three editors don't auto-indent under this script's launch config, so they
# keep the explicit indentation as-is.
edit_type_seq() {
    local editor="$1" arr_name="$2"
    local -n _lines="$arr_name"
    local out="" line
    for line in "${_lines[@]}"; do
        if [ "$editor" = "helix" ]; then
            line="${line#"${line%%[^ ]*}"}"
        fi
        out+="L:${line}|K:Enter|"
    done
    out="${out%|}"
    if [ -n "${MODAL_EDITORS[$editor]:-}" ]; then
        printf 'K:Escape i|%s' "$out"
    else
        printf '%s' "$out"
    fi
}

# ============================================================================
# Session driver: runs the whole fixed step sequence once against a live
# tmux session, appending one RESULTS_FILE row per timed call:
#   size_label|editor|syntax_mode|step|status|elapsed_ms
# status is OK, UNSTABLE, or N/A (op unsupported for this editor). All
# repetitions of a step (its own repeat count, and across --iterations) are
# pooled at report time into min/avg/max.
# ============================================================================
run_session() {
    local editor="$1" size_label="$2" run_file="$3" syntax_mode="$4"
    local marker
    marker="$(first_line_marker "$run_file")"

    local base_cmd="${EDITORS[$editor]}"
    local flag="${SYNTAX_FLAG[$editor:$syntax_mode]:-}"
    local full_cmd="$base_cmd $flag"
    [ "$editor" = "helix" ] && full_cmd="$full_cmd -c \"$HELIX_CONFIG\""

    local session
    session="benchin_$$_${RANDOM}_${RANDOM}"
    CURRENT_SESSION="$session"

    tmux new-session -d -s "$session" -x "$PANE_WIDTH" -y "$PANE_HEIGHT" -- \
        bash -c "exec $full_cmd \"\$0\"" "$run_file" >/dev/null 2>&1

    local ready
    ready=$(wait_until_ready "$session" "$marker")
    if [ "$ready" = "TIMEOUT" ]; then
        log "    [$editor/$syntax_mode] never became ready -- skipping this run"
        tmux kill-session -t "$session" >/dev/null 2>&1
        CURRENT_SESSION=""
        return
    fi

    emit() {
        local step="$1" status="$2" ms="$3"
        echo "$size_label|$editor|$syntax_mode|$step|$status|$ms" >>"$RESULTS_FILE"
    }

    run_and_emit() {
        local step="$1" seq="$2"
        local key="$editor:$step"
        if [ -z "${KEYMAP[$key]:-}" ]; then
            emit "$step" "N/A" "-"
            return
        fi
        local result
        result=$(run_step "$session" "$seq")
        if [ "$result" = "UNSTABLE" ]; then
            emit "$step" "UNSTABLE" "-"
        else
            emit "$step" "OK" "$result"
        fi
    }

    local step
    for step in "${STEP_ORDER[@]}"; do
        local reps
        reps=$(step_repeats "$step")

        case "$step" in
        edit_type)
            local i
            for ((i = 1; i <= reps; i++)); do
                local block_name="EDIT_BLOCK_$i"
                if [ "$i" -eq 2 ]; then
                    # Land the second typed block in a distinct undo-history
                    # region, further into the file. Discard the (untimed)
                    # settle wait so this nav nudge doesn't bleed into the
                    # next timed measurement.
                    run_step "$session" "K:$(repeat_key Down 20)" >/dev/null
                fi
                local seq
                seq="$(edit_type_seq "$editor" "$block_name")"
                local result
                result=$(run_step "$session" "$seq")
                if [ "$result" = "UNSTABLE" ]; then
                    emit "$step" "UNSTABLE" "-"
                else
                    emit "$step" "OK" "$result"
                fi
            done
            ;;
        save)
            local i
            for ((i = 1; i <= reps; i++)); do
                if [ "$i" -gt 1 ]; then
                    # Redirty with a throwaway space; discard the settle wait
                    # so it doesn't bleed into the next timed save.
                    run_step "$session" "L: " >/dev/null
                fi
                run_and_emit "$step" "${KEYMAP[$editor:$step]:-}"
            done
            ;;
        fold_all | unfold_all | undo | redo | edit_backspace | edit_delete_forward)
            local i
            for ((i = 1; i <= reps; i++)); do
                run_and_emit "$step" "${KEYMAP[$editor:$step]:-}"
            done
            ;;
        *)
            run_and_emit "$step" "${KEYMAP[$editor:$step]:-}"
            ;;
        esac
    done

    tmux kill-session -t "$session" >/dev/null 2>&1
    CURRENT_SESSION=""
}

# ============================================================================
# Startup: sweep away state left behind by a previous hard-interrupted run
# ============================================================================
sweep_stale_bench_state() {
    local stale
    stale="$(tmux list-sessions -F '#{session_name}' 2>/dev/null | grep -E '^benchin_[0-9]+_[0-9]+_[0-9]+$' || true)"
    if [ -n "$stale" ]; then
        log "Cleaning up stale tmux session(s) from a previous interrupted run:"
        while IFS= read -r s; do
            log "  killing tmux session: $s"
            tmux kill-session -t "$s" >/dev/null 2>&1
        done <<<"$stale"
    fi
}
sweep_stale_bench_state

# ============================================================================
# Main
# ============================================================================
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

declare -a SYNTAX_MODES=()
if [ "$NO_SYNTAX_ONLY" -eq 1 ]; then
    SYNTAX_MODES=(off)
else
    SYNTAX_MODES=(on)
    [ "$NO_SYNTAX_COMPARE" -eq 0 ] && SYNTAX_MODES+=(off)
fi

IFS=',' read -ra SIZE_LIST <<<"$SIZES"
for lines in "${SIZE_LIST[@]}"; do
    case "$lines" in '' | *[!0-9]*) die "--sizes entries must be positive integers, got: '$lines'" ;; esac
done

log "Editors under test: ${AVAILABLE_NAMES[*]}"
log "Sizes: $SIZES   Iterations: $ITERATIONS   Syntax modes: ${SYNTAX_MODES[*]}"
log "Scratch dir: $SCRATCH_DIR   Pane: ${PANE_WIDTH}x${PANE_HEIGHT}"
log ""

RESULTS_FILE="$(mktemp --tmpdir="$SCRATCH_DIR")"
TMP_FILES+=("$RESULTS_FILE")

for lines in "${SIZE_LIST[@]}"; do
    size_label="${lines} lines"
    template="$(mktemp --tmpdir="$SCRATCH_DIR" --suffix=.c)"
    TMP_FILES+=("$template")
    log "=== Generating $size_label ($template) ==="
    generate_file "$lines" "$template"

    for editor in "${AVAILABLE_NAMES[@]}"; do
        for mode in "${SYNTAX_MODES[@]}"; do
            log "  $editor (syntax $mode):"
            for ((iter = 1; iter <= ITERATIONS; iter++)); do
                run_file="$(mktemp --tmpdir="$SCRATCH_DIR" --suffix=.c)"
                TMP_FILES+=("$run_file")
                cp -- "$template" "$run_file"
                printf '    iteration %d/%d [%s] ... ' "$iter" "$ITERATIONS" "$run_file" >&2
                run_session "$editor" "$size_label" "$run_file" "$mode"
                log "done"
                [ "$KEEP_FILES" -eq 0 ] && rm -f -- "$run_file"
            done
        done
    done
    log ""
done

# ============================================================================
# Report
# ============================================================================
echo "===================================================================="
echo "RESULTS (latency in ms; lower is better; min/avg/max shown for steps"
echo "that repeat within a session; N/A = op unsupported by that editor;"
echo "UNSTABLE = never settled within --step-timeout)"
echo "===================================================================="

print_matrix() {
    local size_label="$1" mode="$2"
    echo ""
    echo "--- $size_label | syntax $mode ---"

    {
        printf 'STEP'
        for editor in "${AVAILABLE_NAMES[@]}"; do printf '|%s|VS BEST' "$editor"; done
        printf '\n'

        for step in "${STEP_ORDER[@]}"; do
            printf '%s' "$step"

            local fastest_avg
            fastest_avg=$(awk -F'|' -v l="$size_label" -v m="$mode" -v s="$step" '
                $1==l && $3==m && $4==s && $5=="OK" {
                    sum[$2] += $6
                    count[$2]++
                }
                END {
                    min_avg = -1
                    for (e in sum) {
                        avg = int(sum[e] / count[e])
                        if (min_avg == -1 || avg < min_avg) {
                            min_avg = avg
                        }
                    }
                    print min_avg
                }' "$RESULTS_FILE")

            for editor in "${AVAILABLE_NAMES[@]}"; do
                local rows
                rows=$(awk -F'|' -v l="$size_label" -v e="$editor" -v m="$mode" -v s="$step" \
                    '$1==l && $2==e && $3==m && $4==s {print $5"|"$6}' "$RESULTS_FILE")
                if [ -z "$rows" ]; then
                    printf '|-|-'
                    continue
                fi
                local any_ok=0 any_unstable=0 any_na=0
                local -a vals=()
                while IFS='|' read -r st v; do
                    case "$st" in
                    OK) any_ok=1; vals+=("$v") ;;
                    UNSTABLE) any_unstable=1 ;;
                    N/A) any_na=1 ;;
                    esac
                done <<<"$rows"
                if [ "$any_ok" -eq 0 ]; then
                    if [ "$any_na" -eq 1 ]; then
                        printf '|N/A|-'
                    else
                        printf '|UNSTABLE|-'
                    fi
                    continue
                fi
                local min=${vals[0]} max=${vals[0]} sum=0 v
                for v in "${vals[@]}"; do
                    [ "$v" -lt "$min" ] && min=$v
                    [ "$v" -gt "$max" ] && max=$v
                    sum=$((sum + v))
                done
                local avg=$((sum / ${#vals[@]}))

                local ratio="-"
                if [ "$(awk -v f="$fastest_avg" 'BEGIN {print (f > 0) ? 1 : 0}')" -eq 1 ]; then
                    ratio=$(awk -v a="$avg" -v f="$fastest_avg" 'BEGIN { printf "%.2fx", a/f }')
                fi

                if [ "${#vals[@]}" -eq 1 ]; then
                    printf '|%d|%s' "$avg" "$ratio"
                else
                    printf '|%d/%d/%d (min/avg/max)|%s' "$min" "$avg" "$max" "$ratio"
                fi
            done
            printf '\n'
        done
    } | column -t -s'|'
}

print_delta() {
    local size_label="$1"
    [ "$NO_SYNTAX_COMPARE" -eq 1 ] && return
    [ "$NO_SYNTAX_ONLY" -eq 1 ] && return
    echo ""
    echo "--- $size_label | SYNTAX HIGHLIGHTING COST (on avg - off avg, ms) ---"
    {
        printf 'STEP'
        for editor in "${AVAILABLE_NAMES[@]}"; do printf '|%s' "$editor"; done
        printf '\n'
        for step in "${STEP_ORDER[@]}"; do
            printf '%s' "$step"
            for editor in "${AVAILABLE_NAMES[@]}"; do
                local on_avg off_avg
                on_avg=$(awk -F'|' -v l="$size_label" -v e="$editor" -v s="$step" \
                    '$1==l && $2==e && $3=="on" && $4==s && $5=="OK" {sum+=$6; n++} END{if(n>0) printf "%d", sum/n}' "$RESULTS_FILE")
                off_avg=$(awk -F'|' -v l="$size_label" -v e="$editor" -v s="$step" \
                    '$1==l && $2==e && $3=="off" && $4==s && $5=="OK" {sum+=$6; n++} END{if(n>0) printf "%d", sum/n}' "$RESULTS_FILE")
                if [ -z "$on_avg" ] || [ -z "$off_avg" ]; then
                    printf '|-'
                else
                    printf '|%d' "$((on_avg - off_avg))"
                fi
            done
            printf '\n'
        done
    } | column -t -s'|'
}

for lines in "${SIZE_LIST[@]}"; do
    size_label="${lines} lines"
    for mode in "${SYNTAX_MODES[@]}"; do
        print_matrix "$size_label" "$mode"
    done
    print_delta "$size_label"
done

echo ""
echo "Done."
