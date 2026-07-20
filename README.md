# Neo Text Editor

Neo is a lightweight terminal text editor built in modern C. It is designed to be highly responsive and handle large files. Neo features a full-screen file explorer, multi-tab support, dynamic syntax highlighting, and an embedded Lua configuration engine.

## Key Features

- **Gap Buffer Architecture**: Text storage is managed using a linked list of gap buffers, ensuring O(1) character insertion and deletion performance, even for extremely large files.
- **O(N) Fast Loading**: The loading engine is optimized to load massive files (100MB+) in a fraction of a second.
- **Visual File Explorer**: Press `Ctrl+E` to open a full-terminal overlay that lets you traverse directories and view file metadata (permissions, sizes, and timestamps).
- **Multi-Tab Support**: Safely open multiple files simultaneously without overwriting unsaved work. Neo automatically spawns new tabs when loading files if the current tab is modified.
- **Dynamic Lua Configuration**: Fully configure editor options, color schemes, custom keybindings, and dynamic syntax highlighting definitions at runtime using a Lua configuration file.
- **Code Folding**: Collapse and expand code blocks by indentation levels. Foldable lines display indicators (`v` for expanded, `>` for folded) in the line number gutter.
- **Integrated Logging & Log Viewer**: Built-in diagnostics logging featuring file and UI output channels. The scrollable, syntax-highlighted Log Viewer UI overlay can be toggled using `Ctrl+L`.
- **Soft Line Wrapping**: Optional line wrapping with visual row tracking and accurate cursor navigation.
- **Rich Syntax Highlighting**: Built-in highlighting for C/C++ and Log files, with support for user-defined languages loaded dynamically from config.

## Usage Guide

Run `neo` to start an empty buffer, or pass a file to load it immediately:

```bash
neo [filename]
```

### Command-Line Interface (CLI)

```bash
neo [options] [file [line_number_option] ...]
```

#### Options

- `-c, --config <path>`: Load configuration from `<path>` (defaults to `~/.config/neo/config.lua` or local `config.lua`).
- `-t, --tab-size <size>`: Set tab width (overrides config).
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

### Keybindings

| Action | Shortcut |
| :--- | :--- |
| **Quit** | `Ctrl+Q` |
| **Save** | `Ctrl+S` |
| **Save As** | `Alt+S` |
| **File Explorer** | `Ctrl+E` |
| **Select All** | `Ctrl+A` |
| **Word Jump** | `Ctrl+Left/Right` |
| **Text Selection** | `Shift+Arrows` |
| **Open New Tab** | `Ctrl+T` |
| **Close Tab** | `Ctrl+W` |
| **Switch To Next Tab** | `Ctrl+N` |
| **Switch To Previous Tab** | `Ctrl+P` |
| **Toggle Code Fold** | `Ctrl+F` |
| **Toggle All Folds** | `Alt+F` |
| **Log Viewer** | `Ctrl+L` |

## Configuration

Neo can be configured dynamically using a Lua configuration file. By default, it looks for `config.lua` in the current working directory, or at `~/.config/neo/config.lua`.

An example configuration (`config.lua`):

```lua
tab_size = 4
show_line_numbers = true
wrap_lines = false
syntax_enabled = true
status_timeout = 3

-- Logging Configuration
log_file = "neo.log"
log_level = "INFO"
log_to_file = false
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
    toggle_all_folds = "alt-f"
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

Neo requires `gcc`, `make`, and `lua5.4` (for future plugin support) to build.

### Debug Build

To build the editor with AddressSanitizer active for safe development:

```bash
make build
```

The executable will be located at `./bin/neo`.

### Release Build

To build the editor for performance:

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

## Codebase Directory Structure

The codebase is structured logically into separate subsystems:
- **`src/core/`**: Core text editing data structures and logic (e.g., Piece Table buffer, history tracking, lines).
- **`src/features/`**: High-level editor features (e.g., config system, multi-tab layout, file explorer, syntax highlighting, logs view).
- **`src/terminal/`**: Low-level terminal interface and raw-mode console control.
- **`src/utils/`**: Shared generic utilities (e.g., dynamic array, gap buffer, CLI options parsing, atomic file I/O, logger).
- **`tests/`**: Regression and integration test suite.

## Developer Documentation

The codebase is heavily documented. If you are developing features, ensure your language server (like `clangd`) is active. Hovering over any subsystem function will provide full Doxygen-style documentation outlining parameters, return types, and expected behavior.

Currently, Neo is built using linux-specific headers. As such, it will only build on linux.

## TODO

- Add modal editing support
- Add support for sublime-text's navigation and editng shortcuts support
- Implement `Ctrl+C` and `Ctrl+V` clipboard integration
- Implement find and replace using regex
- Implement proper syntax highlighting for popular programming and scripting languages
- Build a Lua plugin API to allow users to write custom commands.
