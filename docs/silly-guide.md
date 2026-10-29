# silly-term: Source and Dependency Guide

This document describes the implementation under `src/` and the public interfaces under `src/headers/`. silly-term is a Linux terminal emulator written in C. raylib supplies the window, input, drawing, clipboard, and font logic; libvterm parses terminal control sequences and maintains the screen state; a pseudoterminal (PTY) connects that screen to a child shell.

## Runtime at a glance

```mermaid
flowchart LR
	Main[main.c: application loop]
	Window[window.c: sizing and fullscreen]
	Config[load_conf.c: config schema and parser]
	Input[input.c: keyboard and mouse]
	Binds[keybind.c: shortcut parsing]
	Selection[selection.c: range and text]
	Render[render.c: font and drawing]
	Term[term.c: libvterm and scrollback]
	Pty[pty_io.c: PTY and child process]
	Util[util.c: UTF-8 encoding]
	Raylib[raylib]
	VTerm[libvterm]
	Shell[child shell or command]

	Main --> Config
	Main --> Window
	Main --> Input
	Main --> Render
	Main --> Term
	Input --> Binds
	Input --> Selection
	Input --> Term
	Input --> Render
	Window --> View
	Selection --> Term
	Selection --> Util
	Render --> Term
	Render --> Selection
	Term --> Config
	Term --> Pty
	Main --> Raylib
	Input --> Raylib
	Binds --> Raylib
	Window --> Raylib
	Render --> Raylib
	Term --> VTerm
	Pty --> Shell
```

The arrows show source-level dependencies and external runtime libraries, not a strict layering rule. For example, `input.h` includes `render.h` for the `View` type used by mouse coordinate conversion, while the top-level window logic is isolated in `window.c` and `window.h`.

## Source modules and public interfaces

The headers in `src/headers/` declare the interfaces; implementations are in the matching `.c` files. The following table covers every source and header module.

| Module | Public types and functions | Responsibility and dependencies |
| --- | --- | --- |
| `main.c` | `main`; private `load_config` and `apply_actions` | Application composition. Parses `-c`, loads defaults/config, initializes the raylib window, `View`, `Term`, and `Input`, then handles resize, keyboard/mouse input, PTY output, and drawing each frame. Uses raylib and the input, config, window, render, and terminal APIs. Ignores `SIGPIPE` so a closed shell does not terminate the process during a write. |
| `window.h` / `window.c` | `window_open`, `window_show`, `window_handle_resize`, `window_refit`, `window_toggle_fullscreen` | Window-level lifecycle and refit logic. Creates the hidden window, applies undecorated/transparent/resizable flags, refits the terminal grid when the font or window size changes, and toggles borderless fullscreen. Depends on raylib and the terminal/view APIs. |
| `load_conf.h` / `load_conf.c` | `Rgb`, `Config`; `set_defaults`, `default_conf_path`, `load_conf` | Configuration schema, defaults, config path selection, and line-oriented parser. Uses C library routines only; it has no raylib or libvterm dependency. |
| `term.h` / `term.c` | `SbLine`, `Term`; `term_init`, `term_free`, `term_pump`, `term_write`, `term_resize`, `term_scroll`, `term_get_cell`, `term_clear_scrollback` | Terminal core. Owns libvterm objects, PTY master/child identifiers, current dimensions, cursor/alternate-screen state, title, mouse mode, and the scrollback ring. Depends on libvterm, `Config`, and `pty_io`; it has no raylib dependency. |
| `pty_io.h` / `pty_io.c` | `pty_spawn`, `pty_write_all`, `pty_set_size` | Linux PTY process and byte I/O. Starts the command with `forkpty`, makes the parent master descriptor nonblocking, writes with retry handling, and sends grid dimensions using `TIOCSWINSZ`. Depends on Linux/POSIX interfaces including `pty.h`, `poll`, and `ioctl`; the executable links `libutil`. |
| `input.h` / `input.c` | `Input`; `input_init`, `input_keyboard`, `input_mouse`, `input_take_actions` | Converts raylib keyboard/mouse events into libvterm key events or bytes written to the shell. Runs configured copy/paste/selection/scroll/font/fullscreen shortcuts, clipboard operations, selection dragging, wheel scrolling, and mouse-report forwarding. Depends on raylib, keybind, config, render's `View`, selection, and terminal APIs. |
| `keybind.h` / `keybind.c` | `Bind`, `BindSet`; `parse_bind`, `parse_bindset`, `bind_hit`, `bindset_hit`, `key_pressed` | Parses a shortcut string into a raylib key and modifier flags, then detects a press or repeat with an exact modifier match. Supports comma-separated alternative bindings and matches a keyset by any configured alternative. Depends on raylib for key identifiers and event state. |
| `selection.h` / `selection.c` | `Selection`; `sel_contains`, `sel_text` | Represents a linear, inclusive range in viewport row/column coordinates. Tests cell membership and exports selected cells as allocated UTF-8 text. Depends on `Term` for visible cells and `util` for encoding; no GUI API is used. |
| `render.h` / `render.c` | `View`; `view_init`, `view_free`, `view_pixel_width`, `view_pixel_height`, `render_frame`, `view_set_font_size` | Loads a font, computes cell metrics and cursor style, resolves terminal colors, and draws the background, cells, selection, and cursor. Uses raylib, configuration, terminal cells/state, and selection membership. |
| `util.h` / `util.c` | `utf8_encode` | Encodes one Unicode code point into one to four UTF-8 bytes. Used by selection text extraction; no external library dependency. |

## Main loop and data flow

1. **Startup:** `main` ignores `SIGPIPE`, accepts `-c path`, loads configuration, creates a hidden raylib window, initializes font metrics, and sets the initial window dimensions from the configured columns and rows. `window_show` exposes the actual window after `view_init`, and `term_init` then creates the libvterm state and starts the configured child command on a PTY.
2. **Resize:** if resizing is enabled and raylib reports a changed window size, `window_handle_resize` derives the viewport size from the pixel dimensions, padding, and `View.cw`/`View.ch`. It clamps the result to at least one cell and calls `term_resize`. The active selection is cleared when a new input event changes the terminal geometry.
3. **Keyboard input:** `input_keyboard` checks configured shortcuts first. Printable characters are drained from raylib's character queue and passed to libvterm; special keys use libvterm key events; Ctrl-letter and selected Ctrl-punctuation combinations are sent as control bytes. Input sent to the shell clears the selection and returns scrollback to the live bottom.
4. **Mouse input:** `input_mouse` maps pointer pixels to a clamped grid coordinate. Left-drag selects, middle-click pastes, and the wheel either changes the scrollback offset or sends up/down keys while the alternate screen is active. Mouse reporting is used only when the app negotiated a mouse mode and no shift key is held.
5. **Shell output:** `term_pump` reads available bytes from the nonblocking PTY master and feeds them to `vterm_input_write`. libvterm updates its screen and invokes callbacks for pushed scrollback lines, terminal properties, and output that must be written back to the PTY.
6. **Drawing:** `render_frame` clears the window and obtains each visible cell through `term_get_cell`. It draws backgrounds and glyphs, applies reverse/bold/underline/strike effects, overlays selection, and renders the cursor according to visibility, focus, blink, and configured style.
7. **Shutdown:** `main` frees the terminal and font resources, then closes the raylib window. `term_free` closes the PTY, signals the child with `SIGHUP`, releases scrollback allocations, and frees libvterm.

## Module contracts and behavior

### Configuration

`Config` is initialized from defaults by both `set_defaults` and `load_conf`. `default_conf_path` chooses `$XDG_CONFIG_HOME/silly-term/sillyconf.conf` when `XDG_CONFIG_HOME` is nonempty; otherwise it uses `$HOME/.config/silly-term/sillyconf.conf`. If neither environment variable is usable, no default path is available. `main` only warns when an explicitly supplied `-c` file cannot be opened; an unavailable implicit config falls back silently to defaults.

The parser accepts `key = value`, ignores blank lines and lines beginning with `#` or `;`, accepts matching single or double quotes around values, and recognizes inline comments when whitespace precedes `#`. Unknown keys and malformed values are reported to stderr with path and line number; parsing continues. Integer and floating-point values are clamped to the ranges below. Booleans are case-insensitive `true/yes/on/1` and `false/no/off/0`. Colors accept `#rgb`, `rgb`, `#rrggbb`, or `rrggbb`.

| Keys | Defaults | Accepted range or format |
| --- | --- | --- |
| `font_path` | empty | Path; empty tries common system fonts |
| `font_size`, `line_height`, `letter_spacing`, `font_step`, `font_smooth` | `20`, `1.2`, `0`, `2`, `true` | `6..200`, `0.5..3.0`, `-10..50`, `1..20`, boolean |
| `title`, `cols`, `rows`, `padding`, `decorated`, `resizable`, `opacity`, `fps` | `silly-term`, `80`, `24`, `4`, `true`, `true`, `1.0`, `60` | `2..1000`, `1..1000`, `0..200`, boolean, boolean, `0.0..1.0`, `5..360` |
| `shell`, `term` | `$SHELL` or `/bin/bash`, `xterm-256color` | Strings; shell command is split on spaces, not parsed as shell syntax |
| `scrollback`, `scroll_lines` | `10000`, `3` | `0..1000000`, `1..100` |
| `cursor_style`, `cursor_blink`, `cursor_blink_ms`, `cursor_thickness` | `block`, `true`, `600`, `2` | `block`, `underline`, or `bar`; boolean; `100..5000`; `1..20` |
| `bold_bright`, `fake_bold`, `copy_on_select`, `mouse_reporting` | `true`, `false`, `false`, `true` | Boolean |
| `fg`, `bg`, `cursor_color`, `selection_fg`, `selection_bg` | `#c5c8c6`, `#1d1f21`, `#c5c8c6`, `#ffffff`, `#373b41` | 3- or 6-digit RGB hex |
| `color0` through `color15` | Standard values initialized in `set_defaults` | 3- or 6-digit RGB hex |
| `key_copy`, `key_paste`, `key_select_all`, `key_scroll_up`, `key_scroll_down`, `key_scroll_top`, `key_scroll_bottom`, `key_clear_scrollback`, `key_font_increase`, `key_font_decrease`, `key_font_reset`, `key_fullscreen` | `ctrl+shift+c`, `ctrl+shift+v, shift+insert`, `ctrl+shift+a`, `shift+pageup`, `shift+pagedown`, `shift+home`, `shift+end`, `ctrl+shift+k`, `ctrl+equal, ctrl+shift+equal, ctrl+kp_add`, `ctrl+minus, ctrl+kp_subtract`, `ctrl+0, ctrl+kp_0`, `f11` | Modifier/key combinations described below |

The keybind parser lowercases tokens. Supported modifiers are `ctrl`/`control`, `shift`, and `alt`; keys include letters, digits, `f1` through `f12`, `pageup`, `pagedown`, `home`, `end`, `insert`, `delete`, arrows, `space`, `enter`, `tab`, and keypad aliases such as `kp_add` and `kp_0`. A binding only matches when the held modifier set exactly matches its parsed modifiers, and key repeats count as hits.

### Terminal and PTY

`Term` exposes the libvterm screen and state to consumers while hiding PTY reads behind `term_pump`. Its result is `> 0` when output was processed, `0` when idle, and `-1` when the shell/PTY has exited or failed. Reads are nonblocking and processing is bounded to 512 KiB per call so one frame cannot drain unlimited output.

The scrollback is a bounded ring of copied `VTermScreenCell` rows. When full, it replaces the oldest row. `term_scroll` is disabled on the alternate screen and clamps the offset to the available history. `term_get_cell` combines the scrollback offset with the live libvterm screen. A clear-scrollback action resets the stored history without reintroducing those rows into the libvterm screen.

`pty_spawn` sets `TERM` from configuration and `COLORTERM=truecolor` in the child. It tokenizes the configured command on spaces/tabs into at most 31 arguments; quotes and shell expansion are not interpreted. If the command is empty, it starts `/bin/sh`. `pty_write_all` retries `EINTR` and `EAGAIN` using `poll` in 50 ms intervals, for up to about five seconds of retry attempts, then returns without a status value. `pty_set_size` sends the new dimensions to the PTY.

The libvterm title callback accumulates title fragments and invokes `Term.on_title` on the final fragment. The main program connects that callback to raylib's `SetWindowTitle`. Cursor visibility, alternate-screen changes, and mouse mode are tracked as terminal properties; switching alternate-screen state resets the scrollback view to the bottom.

### Input, selection, and rendering

The clipboard is accessed through raylib. Paste converts line feeds to carriage returns, removes the line-feed half of CRLF pairs, and brackets the write with libvterm's paste start/end calls. Copy extracts the active selection and passes it to raylib. Mouse selection is linear in reading order and inclusive at both endpoints; a plain click clears the selection. Export omits the second cell of a wide character and trims trailing spaces on each selected row.

The renderer searches `font_path` first, then common DejaVu Sans Mono and Liberation Mono locations. It requests a fixed set of Latin, Greek, Cyrillic, punctuation, arrows, mathematical, box-drawing, block, braille, and replacement glyphs. If no usable font is found, it uses raylib's default font. Cell width is the measured width of `M` plus `letter_spacing`; cell height is `font_size * line_height`, both rounded up and clamped to at least one pixel.

Default terminal colors come from `Config`; indexed colors 0-15 use the configurable palette and indices above 15 are converted through libvterm. `bold_bright` maps bold foreground indices 0-7 to 8-15. Reverse video swaps foreground/background; selection overrides both. `fake_bold` draws a second glyph one pixel to the right. Window opacity affects the clear/background color; cell colors are drawn opaque. The cursor is hidden while scrolled back, follows libvterm's visibility property, and blinks only after the configured inactivity interval while the window is focused.

## Dependency boundaries

| Dependency | Used by | Purpose |
| --- | --- | --- |
| raylib | `main.c`, `window.c`, `input.c`, `keybind.c`, `render.c` | Window lifecycle, keyboard/mouse polling, clipboard, font loading, drawing, and fullscreen toggles |
| libvterm | `term.c`, `input.c`, `render.c`, `selection.c` (through terminal types) | Terminal parsing, screen/state/cell representation, key encoding, paste mode, mouse protocols, color conversion |
| Linux/POSIX PTY and process APIs | `pty_io.c`, `term.c` | `forkpty`, descriptor I/O, polling, resize ioctl, child lifecycle |
| `libutil` | Link step | Provides `forkpty` on the target Linux systems |
| `libm` | Link step | Math helpers used by input/rendering (`roundf`, `ceilf`) |

Only `main`, `window`, `input`, `keybind`, and `render` directly use raylib. The terminal core, configuration parser, selection logic, PTY helper, and UTF-8 helper do not use raylib. `input.c` still includes `render.h` because its mouse API accepts `View` cell metrics.

## Build targets

The repository's lowercase `makefile` compiles all `src/*.c`, placing object files, dependency files, and the binary under `build/<mode>/`. The root `silly-term` path is a symlink to the selected build binary.

```sh
make                 # release build
make MODE=debug      # debug symbols, no optimization
make MODE=asan       # AddressSanitizer build
make run ARGS="-c /path/to/sillyconf.conf"
make install-conf    # copy sillyconf.conf if it is not already installed
make clean
```

The compiler defaults to `cc`. Build flags use GNU C11 and warnings `-Wall -Wextra`. Dependencies are queried through `pkg-config` packages `raylib` and `vterm`; if that query yields no libraries, the makefile falls back to `-lraylib -lvterm`. Linking also uses `-lutil -lm`. The code is Linux-specific because it uses `forkpty`, `pty.h`, and `TIOCSWINSZ`.

## Extension points

To add a configuration setting, add a field to `Config` in `src/headers/load_conf.h`, assign its default in `set_defaults`, register its name/type/range in `OPTS` in `src/load_conf.c`, then consume and document it. To add a shortcut action, update the `Input` state and shortcut handling in `src/input.c`, keeping parsing behavior in `keybind.c` when a new key syntax is required. To add a new window operation, use `window.c` and `window.h` for the top-level resize/fullscreen logic; terminal screen and PTY behavior belong in `term.c` or `pty_io.c` according to ownership. Changes to cell display belong in `render.c`, while selection and text export remain centered on `selection.c` and `util.c`.
