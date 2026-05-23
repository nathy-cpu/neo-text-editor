# Neo Text Editor

Neo is a lightweight terminal text editor built in modern C. It is designed to be highly responsive and handle large files. Neo features a full-screen file explorer, multi-tab support, and dynamic syntax highlighting.

## Key Features

- **Gap Buffer Architecture**: Text storage is managed using a linked list of gap buffers, ensuring O(1) character insertion and deletion performance, even for extremely large files.
- **O(N) Fast Loading**: The loading engine is optimized to load massive files (100MB+) in a fraction of a second.
- **Visual File Explorer**: Press `Ctrl+E` to open a full-terminal overlay that lets you traverse directories and view file metadata (permissions, sizes, and timestamps).
- **Multi-Tab Support**: Safely open multiple files simultaneously without overwriting unsaved work. Neo automatically spawns new tabs when loading files if the current tab is modified.
- **Syntax Highlighting**: Built-in (hacky) support for C/C++ code.

## Usage Guide

Run `neo` to start an empty buffer, or pass a file to load it immediately:
```bash
neo [filename]
```

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

## Developer Documentation

The codebase is heavily documented. If you are developing features, ensure your language server (like `clangd`) is active. Hovering over any `neo.h` function will provide full Doxygen-style documentation outlining parameters, return types, and expected behavior.

Currently, Neo is built using linux-specific headers. As such, it will only build on linux.

## TODO
- Implement `Ctrl+C` and `Ctrl+V` clipboard integration 
- Implement proper syntax highlighting for popular programming and scripting languages
- Add a persistent configuration file (`~/.config/neo/init.lua`).
- Build a Lua plugin API to allow users to write custom commands.
