# silly-term

A minimal GPU-accelerated terminal emulator for Linux, written in C with [raylib](https://www.raylib.com/) and [libvterm](https://www.leonerd.org.uk/code/libvterm/).

**Design goal:** maximum simplicity. One main file, flat state, data-driven config, no abstraction layers. Built to be read, modified, and stripped down by other developers.

---

## Features

- Truecolor rendering via libvterm + raylib
- Scrollback ring buffer with mouse wheel and keybind scrolling
- Mouse text selection with optional copy-on-select
- Alt-screen support (vim, htop, less, …)
- Configurable keybinds, colors, font, cursor style
- Transparent and undecorated window modes
- Resizable window with automatic PTY resize

**Not a goal:** tabs, splits, ligatures, shell integration, Sixel/Kitty graphics, Windows/macOS ports. Add those only if they can be done in under ~50 lines each.

---

## Dependencies

- `libvterm` (development headers)
- `raylib` (development headers)
- `pty.h` (from glibc — Linux only)
- A TrueType monospace font (DejaVu Sans Mono or Liberation Mono are auto-detected)

Debian/Ubuntu:
```sh
sudo apt install libvterm-dev libraylib-dev
```

Arch:
```sh
sudo pacman -S libvterm raylib
```

Fedora:
```sh
sudo dnf install libvterm-devel raylib-devel
```

---

## Build

```sh
cc -O2 -std=c11 -D_GNU_SOURCE \
   silly-term.c load_conf.c \
   $(pkg-config --cflags --libs raylib libvterm) \
   -lm -lutil -o silly-term
```

No Makefile on purpose — one line, one binary. If it grows past three lines, add a Makefile then.

---

## Run

```sh
./silly-term
./silly-term -c /path/to/config.conf
```

Without `-c`, the config is loaded from:

1. `$XDG_CONFIG_HOME/silly-term/sillyconf.conf`
2. `$HOME/.config/silly-term/sillyconf.conf`

If the file is missing, defaults are used silently. If `-c` points to a missing file, a warning is printed and defaults are used.

`ESC` is passed to the shell (not used to close the window). Close with your window manager's close button or `ctrl+d` in the shell.

---

## Configuration

The config file is flat `key = value`, one per line. `#` and `;` start comments. Values may be quoted with `"…"` or `'…'`.

```ini
# font
font_path      = /usr/share/fonts/TTF/DejaVuSansMono.ttf
font_size      = 20
line_height    = 1.2
font_smooth    = true

# window
title          = silly-term
cols           = 80
rows           = 24
padding        = 4
decorated      = true
resizable      = true
opacity        = 1.0
fps            = 60

# shell
shell          = /bin/bash
term           = xterm-256color

# scrolling
scrollback     = 10000
scroll_lines   = 3

# cursor
cursor_style     = block      # block | underline | bar
cursor_blink     = true
cursor_blink_ms  = 600
cursor_thickness = 2

# text
bold_bright    = true
fake_bold      = false

# colors
fg             = #c5c8c6
bg             = #1d1f21
cursor_color   = #c5c8c6
selection_fg   = #ffffff
selection_bg   = #373b41
color0         = #1d1f21
color1         = #cc6666
# … color2 .. color15

# selection
copy_on_select = false

# keybinds
key_copy         = ctrl+shift+c
key_paste        = ctrl+shift+v
key_scroll_up    = shift+pageup
key_scroll_down  = shift+pagedown
```

### Key syntax

`mod+mod+key`, case-insensitive. Modifiers: `ctrl` (`control`), `shift`, `alt`. Keys: single letters `a`–`z`, digits `0`–`9`, `f1`–`f12`, `pageup`, `pagedown`, `home`, `end`, `insert`, `delete`, `up`, `down`, `left`, `right`, `space`, `enter`, `tab`.

### Color syntax

`#rgb` or `#rrggbb`. Inline comments after colors work because `#` only starts a comment when preceded by whitespace.

### Range clamping

Numeric values are clamped to the documented range. Out-of-range values are silently clamped; malformed values are reported on stderr and the key is left at its default.

Run with a malformed config to see all errors at once — the parser continues on error and reports line numbers.

---

## Keybinds (defaults)

| Action | Default |
|---|---|
| Copy selection | `ctrl+shift+c` |
| Paste | `ctrl+shift+v` |
| Scroll up | `shift+pageup` |
| Scroll down | `shift+pagedown` |

Mouse: left-drag selects, middle-click pastes, wheel scrolls (or sends arrows in alt-screen).

---

## Architecture

Two files:

- `silly-term.c` — terminal core, PTY handling, rendering, input. All state in two structs (`T` for terminal, `S` for selection) plus a handful of config-derived globals.
- `load_conf.c` / `load_conf.h` — flat `key = value` parser driven by a single `Opt` table. Adding a config key is one row in `OPTS[]` plus one field in `Config`.

The codebase deliberately avoids:

- Classes, vtables, or interface structs with one implementation.
- Factory functions.
- Feature flags for always-on features.
- Logging levels.
- Any dependency beyond raylib and libvterm.

See `MAINTAINERS.md` and `LOAD_CONF.md` for the extension and simplification guides.

---

## Extending

- **New config key:** see `LOAD_CONF.md` §3.1 — three lines.
- **New keybind:** see `MAINTAINERS.md` §3.1 — five lines.
- **New rendering attribute:** see `MAINTAINERS.md` §3.2 — inline in `render()`.
- **New scrollback feature:** iterate `sb_line(i)` for `i` in `[0, T.sb_count)`. Don't abstract.

Every PR is reviewed against the simplification checklist in `MAINTAINERS.md` §4. Net line count must be ≤ 0 unless the feature is user-facing and explicitly requested.

---

## Limitations

- **Linux only.** Uses `forkpty`, `TIOCSWINSZ`, `O_NONBLOCK` on a PTY master. A macOS port needs `posix_openpt` + `grantpt` + `unlockpt` — doable, but not a priority.
- **No ligatures.** raylib's `DrawTextCodepoint` draws one codepoint at a time. Supporting ligatures means a shaping engine; out of scope.
- **No Sixel / Kitty graphics.** Alt-screen works; inline images don't.
- **No clipboard on Wayland without `wl-clipboard`-compatible GLFW.** raylib uses GLFW; on Wayland sessions without XWayland, `GetClipboardText`/`SetClipboardText` may fail.
- **Fixed 256-color palette beyond the 16 configurable entries.** The other 240 are xterm defaults, not configurable.

## Contributing

Read docs first. Then:

1. Check the simplification checklist (§4 in both docs).
2. Count lines: `git diff --stat` should show `-` ≥ `+` unless the feature is user-facing.
3. Run against a real shell, resize the window, open `vim`, select text, paste, scroll in `less`.
4. Open a PR with: what changed, why, and the before/after line count.

PRs that add abstraction, feature flags, or config keys without a caller will be rejected. That's the point of the project.