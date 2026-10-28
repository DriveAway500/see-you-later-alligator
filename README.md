# silly-term

silly-term is a Linux terminal emulator written in C. It uses [raylib](https://www.raylib.com/) for the window, input, clipboard, and drawing; [libvterm](https://www.leonerd.org.uk/code/libvterm/) for terminal protocol handling; and a pseudoterminal (PTY) to run a shell or command.

The code is split into focused modules under `src/`. The project aims to keep the implementation direct and readable while supporting everyday interactive terminal use.

## Features

- Truecolor and indexed-color terminal output
- Scrollback with mouse-wheel and keyboard scrolling
- Mouse text selection, clipboard copy/paste, and optional copy-on-select
- Alternate-screen applications such as `vim`, `less`, and `htop`
- Configurable font, colors, cursor, key bindings, and window behavior
- Resizable window with automatic PTY dimension updates
- Transparent background and undecorated window options

## Requirements

- Linux, a C compiler, GNU Make, and `pkg-config`
- raylib development files
- libvterm development files
- A TrueType monospace font; DejaVu Sans Mono and Liberation Mono paths are tried automatically

The build links `libutil` for `forkpty` and `libm` for math functions. Install the raylib and libvterm development packages provided by your distribution. For example, on Debian/Ubuntu:

```sh
sudo apt install build-essential pkg-config libvterm-dev libraylib-dev fonts-dejavu-core
```

## Build and run

```sh
make
./silly-term
```

The Makefile builds all `src/*.c` files and places objects and the executable in `build/release/`. The root `silly-term` path is a symlink to the selected build. Additional build modes are available:

```sh
make MODE=debug
make MODE=asan
```

Run with an explicit configuration file using `-c`:

```sh
./silly-term -c /path/to/sillyconf.conf
```

Without `-c`, silly-term looks for `$XDG_CONFIG_HOME/silly-term/sillyconf.conf`, then `$HOME/.config/silly-term/sillyconf.conf`. If no configuration is available, built-in defaults are used. A missing explicitly supplied config prints a warning and also uses defaults.

`Esc` is sent to the shell rather than closing the window. Close the window with your window manager, or exit the shell with its normal command (for example, `Ctrl+D`).

## Controls

| Action | Default |
| --- | --- |
| Copy selection | `Ctrl+Shift+C` |
| Paste clipboard | `Ctrl+Shift+V` |
| Scroll up | `Shift+PageUp` |
| Scroll down | `Shift+PageDown` |
| Select text | Left mouse button, drag |
| Paste clipboard | Middle mouse button |
| Scroll | Mouse wheel |

In alternate-screen applications, the mouse wheel sends up/down keys to the application instead of scrolling the emulator's history.

## Configuration

Configuration uses one `key = value` pair per line. Blank lines and lines beginning with `#` or `;` are ignored. Values can be quoted with matching single or double quotes. Booleans accept `true`/`false`, `yes`/`no`, `on`/`off`, and `1`/`0`; colors accept three- or six-digit RGB hex values.

Example:

```ini
title = silly-term
font_size = 20
font_path = /usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf
cols = 80
rows = 24
shell = /bin/bash
term = xterm-256color
scrollback = 10000
cursor_style = block
opacity = 1.0
fg = #c5c8c6
bg = #1d1f21
key_copy = ctrl+shift+c
key_paste = ctrl+shift+v
```

Numeric values are clamped to their supported ranges. Unknown keys and malformed values are reported to stderr with line numbers; parsing continues and other valid options are retained. The complete option list, defaults, ranges, and parser behavior are documented in the [source and dependency guide](docs/silly-guide.md#module-contracts-and-behavior).

## Architecture

- `src/main.c` initializes configuration, the raylib window, terminal, and input, then runs the application loop.
- `src/term.c` connects libvterm to the PTY and manages screen state and scrollback.
- `src/pty_io.c` starts the child process and handles low-level PTY operations.
- `src/input.c` and `src/keybind.c` process keyboard/mouse events and configured shortcuts.
- `src/selection.c` tracks selected cells and exports selected text as UTF-8.
- `src/render.c` loads the font and draws the terminal grid, attributes, selection, and cursor.
- `src/load_conf.c` defines defaults and parses configuration; `src/util.c` contains the UTF-8 encoder used by selection.

Public module interfaces live in `src/headers/`. For the runtime flow, dependency boundaries, APIs, configuration reference, build details, and extension points, see [docs/silly-guide.md](docs/silly-guide.md).

## Scope and limitations

silly-term targets Linux and does not currently implement tabs, splits, ligatures, shell integration, or Sixel/Kitty inline graphics. It supports the alternate screen but not inline image protocols. The 16 ANSI palette entries are configurable; extended indexed colors are handled by libvterm. Clipboard behavior depends on raylib's windowing backend and may vary by desktop session.