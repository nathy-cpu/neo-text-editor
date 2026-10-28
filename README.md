# Neo Text Editor

Neo is a lightweight terminal text editor built in modern C. It is designed to be highly responsive and handle large files. Neo features a full-screen file explorer, multi-tab support, system clipboard integration, dynamic syntax highlighting, and an embedded Lua configuration engine.

## Key Features

- **Piece Table Architecture**: Files are memory-mapped and never copied up front; edits are recorded as pieces over the original mapping and an append-only add buffer. Pieces and the line cache live in gap buffers, so edits near the cursor stay cheap even in very large files.
- **Fast Loading**: Because the original file is mmap'd and the line cache is sized exactly in one pass, massive files (100MB+) open in a fraction of a second. Soft wrapping is automatically turned off for files longer than `wrap_disable_line_threshold` lines.
- **Safe Saves**: Files are written atomically (temp file + rename), preserving permissions and symlinks, with optional `fdatasync` (`fsync_enabled`).
- **Visual File Explorer**: Press `Ctrl+E` to open a full-terminal overlay that lets you traverse directories and view file metadata (permissions, sizes, and timestamps).
- **Multi-Tab Support**: Open multiple files simultaneously without overwriting unsaved work. Files opened from the explorer replace the current tab only if it is unnamed and unmodified; otherwise they open in a new tab.
- **Clipboard Integration**: Copy, cut, and paste through the system clipboard (`wl-copy`/`wl-paste`, `xclip`, or `xsel`, whichever is available), falling back to an internal clipboard when none is.
- **Line Editing Commands**: Delete, move, and join lines, kill to end of line, indent/unindent selections, and insert lines above/below.
- **Undo/Redo**: Grouped undo history (e.g. a paste undoes in one step), bounded by `undo_limit`.
- **UTF-8 Aware Editing**: Cursor movement and deletion operate on codepoint boundaries.
- **Dynamic Lua Configuration**: Configure editor options, color schemes, custom keybindings, and syntax highlighting definitions using a Lua configuration file.
- **Code Folding**: Collapse and expand code blocks by indentation levels. Foldable lines display indicators (`v` for expanded, `>` for folded) in the line number gutter.
- **Integrated Logging & Log Viewer**: Built-in diagnostics logging featuring file and UI output channels. The scrollable, syntax-highlighted Log Viewer UI overlay can be toggled using `Ctrl+L`.
- **Soft Line Wrapping**: Optional line wrapping with visual row tracking and accurate cursor navigation across wrapped and folded lines.
- **Syntax Highlighting**: Built-in highlighting for C, C++, and log files, with support for user-defined languages loaded from config.

## Usage Guide

Run `neo` to start an empty buffer, or pass one or more files to open each in its own tab:

```bash
neo [filename ...]
```

### Command-Line Interface (CLI)

```bash
neo [options] [file [line_number_option] ...]
```

#### Options

- `-c, --config <path>`: Load configuration from `<path>` (defaults to `./config.lua`, falling back to `~/.config/neo/config.lua`).
- `-t, --tab-size <size>`: Set tab width, 1–16 (overrides config).
- `-n, --no-line-numbers`: Disable line numbers (overrides config).
- `--line-numbers`: Enable line numbers (overrides config).
- `-w, --no-wrap`: Disable line wrapping (overrides config).
- `--wrap`: Enable line wrapping (overrides config).
- `-s, --no-syntax`: Disable syntax highlighting (overrides config).
- `--syntax`: Enable syntax highlighting (overrides config).
- `-r, --read-only`: Open files in read-only mode.
- `--log-file <path>`: Set log file path (overrides config).
- `--log-level <level>`: Set log level (`DEBUG`, `INFO`, `WARN`, `ERROR`, `FATAL`).
- `--log-to-file` / `--no-log-to-file`: Enable/disable logging to file.
- `--log-to-ui` / `--no-log-to-ui`: Enable/disable log viewer overlay in UI.
- `--log-max-messages <num>`: Set maximum log message buffer size.
- `+<line>[:<col>]`: Jump to specific line and column (e.g. `+45:10`).
- `--line <number>`: Jump to specific line number.
- `--column <number>`: Jump to specific column number.
- `-h, --help`: Display the help message and exit.
- `-v, --version`: Display version information and exit.

Line/column targets are clamped to the file's length.

### Keybindings

#### Configurable (defaults)

These can be rebound in the `keybindings` table of `config.lua` (see [Configuration](#configuration)).

| Action | Config name | Default |
| :--- | :--- | :--- |
| **Quit** (press twice if a tab is unsaved) | `quit` | `Ctrl+Q` |
| **Save** | `save` | `Ctrl+S` |
| **Save As** | `save_as` | `Alt+S` |
| **File Explorer** | `explorer` | `Ctrl+E` |
| **Open New Tab** | `new_tab` | `Ctrl+T` |
| **Close Tab** (press twice if unsaved) | `close_tab` | `Ctrl+W` |
| **Switch To Next Tab** | `next_tab` | `Ctrl+N` |
| **Switch To Previous Tab** | `prev_tab` | `Ctrl+P` |
| **Log Viewer** | `show_logs` | `Ctrl+L` |
| **Toggle Code Fold** | `toggle_fold` | `Ctrl+F` |
| **Toggle All Folds** | `toggle_all_folds` | `Alt+F` |
| **Copy** | `copy` | `Ctrl+C` |
| **Cut** | `cut` | `Ctrl+X` |
| **Paste** | `paste` | `Ctrl+V` |
| **Delete Line** | `delete_line` | `Ctrl+D` |
| **Kill To End Of Line** | `kill_to_end` | `Ctrl+K` |
| **Join Lines** | `join_lines` | `Ctrl+J` |
| **Move Line Up / Down** | `move_line_up` / `move_line_down` | `Alt+Up` / `Alt+Down` |

#### Built-in

| Action | Shortcut |
| :--- | :--- |
| **Move Cursor** | `Arrows`, `Home`, `End`, `PgUp`, `PgDn` |
| **Word Jump** | `Ctrl+Left/Right` |
| **Start / End Of File** | `Ctrl+Home` / `Ctrl+End` |
| **Text Selection** | `Shift+` any of the movement keys above |
| **Select All** | `Ctrl+A` |
| **Undo / Redo** | `Ctrl+Z` / `Ctrl+Y` |
| **Indent Selection** | `Tab` (with a selection; inserts a tab otherwise) |
| **Unindent** | `Shift+Tab` |
| **Delete Word Left / Right** | `Ctrl+Backspace` / `Ctrl+Delete` (or `Alt+`) |
| **Insert Line Below / Above** | `Ctrl+Enter` / `Ctrl+Shift+Enter` |

Some terminals (notably Ghostty, with its default keybinds) consume `Shift+Arrows`, `Ctrl+Shift+Left/Right`, and `Ctrl+Enter`. Neo accepts alternates for these: add `Alt` to the selection chords (e.g. `Shift+Alt+Arrows`, `Shift+Ctrl+Alt+Left/Right`), and use `Alt+Enter` / `Alt+Shift+Enter` for inserting lines.

In the **file explorer**, use `Up/Down` to move and `Enter` to open a file or directory; `Esc`, `Ctrl+E`, or `Ctrl+Q` closes it. In the **log viewer**, `Up/Down` and `PgUp/PgDn` scroll; `Esc`, `Ctrl+Q`, or the `show_logs` key closes it. In the **Save As** prompt, `Enter` accepts and `Esc` cancels.

Neo has no mouse support.

## Configuration

Neo is configured with a Lua file. It loads the path given with `-c`/`--config`, or `config.lua` in the current working directory; if that file does not exist, it falls back to `~/.config/neo/config.lua`, and then to built-in defaults. Command-line options are applied on top of the loaded config. All options are Lua globals, and any key you leave out keeps its default.

### Editor Options

| Key | Type | Default | Notes |
| :--- | :--- | :--- | :--- |
| `tab_size` | int | `4` | Must be 1–16. |
| `show_line_numbers` | bool | `true` | |
| `wrap_lines` | bool | `true` | |
| `wrap_disable_line_threshold` | int | `50000` | Wrapping is turned off for files with more lines than this; `0` never turns it off. |
| `syntax_enabled` | bool | `true` | |
| `status_timeout` | int | `5` | Seconds a status message stays visible. |
| `undo_limit` | int | `1000` | |
| `fsync_enabled` | bool | `true` | `fdatasync` before the atomic rename on save. |
| `log_file` | string | `"neo.log"` | |
| `log_level` | string | `"INFO"` | `DEBUG`, `INFO`, `WARN`, `ERROR`, `FATAL` (case-insensitive). |
| `log_to_file` | bool | `false` | |
| `log_to_ui` | bool | `true` | |
| `log_max_messages` | int | `1000` | |

### Colors

The `colors` table maps highlight groups to ANSI SGR parameters (the part between `ESC[` and `m`, e.g. `"38;5;208"`). Accepted groups and defaults: `keyword` (`"34"`), `type` (`"32"`), `string` (`"38;5;208"`), `character` (`"33"`), `number` (`"35"`), `comment` (`"90"`), `symbol` (`"36"`), `match` (`"34"`).

### Key Chords

The `keybindings` table maps the config names listed under [Keybindings](#configurable-defaults) to key chords. A chord is zero or more case-insensitive `ctrl-`, `alt-`, or `shift-` prefixes followed by a single character or one of `backspace`, `delete`, `insert`, `home`, `end`, `pageup`, `pagedown`, `arrow_left`, `arrow_right`, `arrow_up`, `arrow_down`, `tab`, `f1`–`f12` (e.g. `"ctrl-s"`, `"alt-arrow_up"`, `"ctrl-shift-arrow_left"`). A chord that cannot be parsed leaves the default in place.

### Languages

The `languages` table is a list of language definitions with the fields `name` and `extensions` (required), and `keywords`, `types`, `single_line_comment`, `multi_line_comment_start`, and `multi_line_comment_end`. A definition whose `name` matches a built-in language (`C`, `C++`, `Log`, case-insensitive) replaces it; otherwise it is added. Extensions beginning with `.` must match the file's last extension exactly; other entries match anywhere in the filename.

### Example

The repository's [`config.lua`](config.lua):

```lua
tab_size = 4
show_line_numbers = true
wrap_lines = false
fsync_enabled = true
syntax_enabled = true
status_timeout = 3

-- Logging Configuration
log_file = "neo.log"
log_level = "INFO"
log_to_file = true
log_to_ui = true
log_max_messages = 1000

colors = {
    keyword = "35",
    type = "36",
    string = "32",
    comment = "90"
}

keybindings = {
    save = "ctrl-s",
    quit = "ctrl-q",
    new_tab = "ctrl-t",
    close_tab = "ctrl-w",
    next_tab = "ctrl-n",
    prev_tab = "ctrl-p",
    save_as = "alt-s",
    show_logs = "ctrl-l",
    toggle_fold = "ctrl-f",
    toggle_all_folds = "alt-f",
    copy = "ctrl-c",
    cut = "ctrl-x",
    paste = "ctrl-v",
    delete_line = "ctrl-d",
    move_line_up = "alt-arrow_up",
    move_line_down = "alt-arrow_down",
    join_lines = "ctrl-j",
    kill_to_end = "ctrl-k"
}

languages = {
    {
        name = "Lua",
        extensions = { ".lua" },
        keywords = { "and", "break", "do", "else", "elseif", "end", "false", "for", "function", "if", "in", "local", "nil", "not", "or", "repeat", "return", "then", "true", "until", "while" },
        types = { "io", "math", "string", "table", "os" },
        single_line_comment = "--",
        multi_line_comment_start = "--[[",
        multi_line_comment_end = "]]"
    }
}
```

## Build and Installation

Neo requires `gcc`, `make`, and the Lua 5.4 development library (`liblua5.4-dev` on Debian/Ubuntu) to build. For system clipboard support, install one of `wl-clipboard`, `xclip`, or `xsel`.

### Debug Build

To build the editor with AddressSanitizer and UndefinedBehaviorSanitizer enabled for safe development:

```bash
make build
```

The executable will be located at `./bin/neo`. `make run` builds (if needed) and launches it.

### Release Build

To build the editor for performance (`-O3`, assertions disabled):

```bash
make release
```

### Installation

To install the release build system-wide:

```bash
sudo make install
```

This will place the `neo` binary in `/usr/local/bin`. You can override this location:

```bash
sudo make install PREFIX=/opt/custom
```

## Development

### Testing

```bash
make test                                # clean rebuild with ASan + UBSan, then run every test
make testq TESTFLAGS='--filter buffer'   # quick loop: no clean, pass flags to the harness
```

All test files are `#include`d into `tests/main.c` as a single translation unit and built into `bin/neo_test`. Each test runs in its own forked child with a 10-second timeout and stdin pointed at `/dev/null`; its output is shown only if it fails. Editor-level tests drive the real input and render code through a fake `Platform` (`tests/helpers/test_platform.h`) with scripted keys and captured frames.

Harness flags (pass via `TESTFLAGS`):

- `--filter <substr>`: Run only tests whose full name (`suite.test`) contains `<substr>`.
- `--list`: List matching tests without running them.
- `-v`: Show every test's captured output, not just failures.
- `--no-fork`: Run tests in-process (for `gdb`/`rr`); no timeouts, and a crash stops the run.

Results are reported as `PASS`, `FAIL`, `CRASH`, `TIMEOUT`, `XFAIL`, or `XPASS`. A test marked `TEST_XFAIL` documents a known bug; once it starts passing (`XPASS`) the suite fails until it is promoted to `TEST()`.

### Formatting and Linting

- `make format`: Run `clang-format` over `src/`.
- `make lint`: Format, then run `clang-tidy` (diagnostics and static analyzer checks).

CI (`.github/workflows/ci.yml`) runs `make lint`, `make test`, and `make release` on every push to `main` and on pull requests.

### Benchmarks

The `scripts/` directory contains tmux-driven benchmarks that compare Neo against other terminal editors on generated (or supplied) files. Build `./bin/neo` first. They require `tmux`, `awk`, `column`, GNU `date`/`mktemp`, and bash 4.3+. Editors are registered in an array at the top of each script; `--add "name=command"` adds one ad hoc and `--list` shows which are available. Run any script with `--help` for all options.

- **`bench_editors.sh`**: Time to open a file: *PAINT* (first frame showing the file's content) and *READY* (screen stops changing).
- **`bench_editors_memory.sh`**: Peak memory from launch until the screen settles, via cgroup `memory.peak` under `systemd-run --user` (falling back to `VmHWM`). Each editor is capped by `--mem-limit` (default `4G`).
- **`bench_editors_input.sh`**: Steady-state input latency and save time in an already-open large file (folding, navigation, select-all, typing, deletion, undo/redo, save).

```bash
./scripts/bench_editors.sh --editors neo,helix --sizes 1000,500000
```

### Codebase Directory Structure

- **`src/main.c`**: Entry point: CLI parsing, config loading, and the main refresh/keypress loop.
- **`src/core/`**: Core text editing data structures (piece-table buffer, line cache, undo/redo history).
- **`src/features/`**: High-level editor features (editor state and rendering, input handling, config system, tabs, file explorer, syntax highlighting, log viewer).
- **`src/terminal/`**: Low-level terminal interface: raw mode, key decoding, and signal handling.
- **`src/platform/`**: The `Platform` abstraction through which all key input, screen output, window size queries, and clipboard access flow, so tests can substitute fakes.
- **`src/utils/`**: Shared generic utilities (dynamic array, gap buffer, slices, UTF-8 helpers, CLI options parsing, atomic file I/O, clipboard, logger).
- **`tests/`**: Unit and regression test suites, the test harness, and fake platform helpers.
- **`scripts/`**: Benchmark scripts comparing Neo against other editors.

### Developer Documentation

The public headers carry Doxygen-style comments. With a language server such as `clangd` active, hovering over a function shows its parameters, return value, and behavior.

Neo depends on POSIX and Linux/glibc APIs (e.g. `mmap`, `fdatasync`, `termios`) and is developed and tested on Linux only.

## TODO

- Add modal editing support
- Add support for Sublime Text's navigation and editing shortcuts
- Implement find and replace using regex
- Implement proper syntax highlighting for popular programming and scripting languages
- Build a Lua plugin API to allow users to write custom commands.
