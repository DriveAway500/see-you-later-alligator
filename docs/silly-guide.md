# silly-term: Source File Overview

A minimal terminal emulator built on raylib (window and drawing), libvterm (escape-sequence parsing) and a pty (the shell). All behavior is configured through `sillyconf.conf`.

## Files and responsibilities

| File | Responsibility |
| --- | --- |
| `src/main.c` | Entry point. Loads the config, opens the window, creates the terminal, and runs the main loop (resize, input, read shell output, draw). Contains no terminal logic itself. |
| `src/load_conf.c/h` | Configuration. Defines the `Config` struct, default values, the config file path (`$XDG_CONFIG_HOME` or `~/.config`), and a table-driven parser with range checks and warnings for bad lines. |
| `src/term.c/h` | Terminal core. Wraps libvterm, owns the shell connection and the scrollback ring buffer, reads shell output, handles resize, and exposes cells to read. Has no raylib dependency. |
| `src/pty_io.c/h` | Low-level pty work. Forks the shell with `forkpty`, writes bytes to it reliably, and sets the window size on the pty. |
| `src/render.c/h` | Drawing. Loads the font, measures the cell size, resolves colors (palette, bold-bright, reverse video), and draws text, backgrounds, selection and cursor each frame. |
| `src/input.c/h` | Keyboard and mouse. Sends keys to the shell, runs the configurable shortcuts (copy, paste, scroll), handles mouse selection, middle-click paste and wheel scrolling. |
| `src/selection.c/h` | Selection logic. Tracks the selected range, tests whether a cell is selected, and extracts the selected text as UTF-8. Has no raylib dependency. |
| `src/keybind.c/h` | Shortcut parsing. Turns strings such as `ctrl+shift+v` into a key plus modifiers and checks whether it was pressed this frame. |
| `src/util.c/h` | Small helpers shared by modules (UTF-8 encoding). |
| `sillyconf.conf` | Example configuration listing every option with its default. |
| `Makefile` | Builds every `src/*.c` into `build/` and links the `silly-term` executable. |

## How data flows

1. **Keyboard or mouse:** `input` converts the event into bytes and `term_write` sends them to the shell through `pty_io`.
2. **Shell output:** `term_pump` reads from the pty and feeds libvterm, which updates the screen state and fills the scrollback.
3. **Drawing:** `render` reads cells through `term_get_cell` and draws them using colors and sizes from `Config`.

## Dependency rules

- `term`, `selection`, `pty_io`, `util` and `load_conf` do not use raylib, so they can be tested or reused without a window.
- `render`, `input` and `keybind` are the only modules that talk to raylib, along with `main`.
- Configuration is read once at startup by `main` and passed as `const Config *` to the modules that need it.

## Adding a config option

1. Add the field to `Config` in `load_conf.h`.
2. Set its default in `set_defaults` and add one line to the option table in `load_conf.c`.
3. Use it where needed and document it in `sillyconf.conf`.
